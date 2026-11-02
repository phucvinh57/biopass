#include "tpm_seal.h"

#include <stdlib.h>
#include <string.h>
#include <tss2/tss2_tcti_device.h>
#include <unistd.h>

#include <vector>

namespace biopass {

namespace {

constexpr char kTpmDevice[] = "/dev/tpmrm0";

// TPM response codes without the handle/parameter/session number bits, so
// they compare against the TPM2_RC_* constants.
TSS2_RC baseRc(TSS2_RC rc) {
  if ((rc & TSS2_RC_LAYER_MASK) != TSS2_TPM_RC_LAYER) {
    return rc;
  }
  if ((rc & TPM2_RC_FMT1) != 0) {
    return rc & (TPM2_RC_FMT1 | 0x3F);
  }
  return rc;
}

bool isTpmLayer(TSS2_RC rc) { return (rc & TSS2_RC_LAYER_MASK) == TSS2_TPM_RC_LAYER; }

TPMT_SYM_DEF aes128Cfb() {
  TPMT_SYM_DEF sym{};
  sym.algorithm = TPM2_ALG_AES;
  sym.keyBits.aes = 128;
  sym.mode.aes = TPM2_ALG_CFB;
  return sym;
}

TPML_PCR_SELECTION pcr7Selection() {
  TPML_PCR_SELECTION sel{};
  sel.count = 1;
  sel.pcrSelections[0].hash = TPM2_ALG_SHA256;
  sel.pcrSelections[0].sizeofSelect = 3;
  sel.pcrSelections[0].pcrSelect[0] = 0x80;  // PCR 7
  return sel;
}

// Flushes every handle it holds (objects and sessions) on scope exit.
class Handles {
 public:
  explicit Handles(ESYS_CONTEXT* esys) : esys_(esys) {}
  ~Handles() {
    for (auto it = handles_.rbegin(); it != handles_.rend(); ++it) {
      Esys_FlushContext(esys_, *it);
    }
  }
  Handles(const Handles&) = delete;
  Handles& operator=(const Handles&) = delete;
  void add(ESYS_TR handle) { handles_.push_back(handle); }

 private:
  ESYS_CONTEXT* esys_;
  std::vector<ESYS_TR> handles_;
};

// The TCG ECC P-256 storage root key template, under the owner hierarchy.
// Primary keys are derived from the hierarchy seed, so the same key comes
// back every time without being stored.
TSS2_RC createPrimary(ESYS_CONTEXT* esys, ESYS_TR* out) {
  TPM2B_SENSITIVE_CREATE sensitive{};
  TPM2B_PUBLIC pub{};
  TPMT_PUBLIC& area = pub.publicArea;
  area.type = TPM2_ALG_ECC;
  area.nameAlg = TPM2_ALG_SHA256;
  area.objectAttributes = TPMA_OBJECT_FIXEDTPM | TPMA_OBJECT_FIXEDPARENT |
                          TPMA_OBJECT_SENSITIVEDATAORIGIN | TPMA_OBJECT_USERWITHAUTH |
                          TPMA_OBJECT_NODA | TPMA_OBJECT_RESTRICTED | TPMA_OBJECT_DECRYPT;
  TPMS_ECC_PARMS& ecc = area.parameters.eccDetail;
  ecc.symmetric.algorithm = TPM2_ALG_AES;
  ecc.symmetric.keyBits.aes = 128;
  ecc.symmetric.mode.aes = TPM2_ALG_CFB;
  ecc.scheme.scheme = TPM2_ALG_NULL;
  ecc.curveID = TPM2_ECC_NIST_P256;
  ecc.kdf.scheme = TPM2_ALG_NULL;
  area.unique.ecc.x.size = 32;
  area.unique.ecc.y.size = 32;
  TPM2B_DATA outside{};
  TPML_PCR_SELECTION creation_pcr{};
  return Esys_CreatePrimary(esys, ESYS_TR_RH_OWNER, ESYS_TR_PASSWORD, ESYS_TR_NONE, ESYS_TR_NONE,
                            &sensitive, &pub, &outside, &creation_pcr, out, nullptr, nullptr,
                            nullptr, nullptr);
}

TpmStatus primaryStatus(TSS2_RC rc) {
  const TSS2_RC base = baseRc(rc);
  if (base == TPM2_RC_BAD_AUTH || base == TPM2_RC_AUTH_FAIL) {
    return TpmStatus::OwnerAuthSet;
  }
  return TpmStatus::Error;
}

// A session salted with `salt_key`, so its parameter encryption key never
// crosses the bus in clear. `attrs` picks command (DECRYPT) or response
// (ENCRYPT) encryption.
TSS2_RC startSession(ESYS_CONTEXT* esys, ESYS_TR salt_key, TPM2_SE type, TPMA_SESSION attrs,
                     ESYS_TR* out) {
  const TPMT_SYM_DEF sym = aes128Cfb();
  TSS2_RC rc = Esys_StartAuthSession(esys, salt_key, ESYS_TR_NONE, ESYS_TR_NONE, ESYS_TR_NONE,
                                     ESYS_TR_NONE, nullptr, type, &sym, TPM2_ALG_SHA256, out);
  if (rc != TSS2_RC_SUCCESS) {
    return rc;
  }
  rc = Esys_TRSess_SetAttributes(esys, *out, attrs | TPMA_SESSION_CONTINUESESSION, 0xff);
  if (rc != TSS2_RC_SUCCESS) {
    Esys_FlushContext(esys, *out);
  }
  return rc;
}

// True when the TPM has an active SHA-256 bank with PCR 7.
bool hasPcr7(ESYS_CONTEXT* esys) {
  const TPML_PCR_SELECTION sel = pcr7Selection();
  UINT32 counter = 0;
  TPML_PCR_SELECTION* sel_out = nullptr;
  TPML_DIGEST* values = nullptr;
  const TSS2_RC rc = Esys_PCR_Read(esys, ESYS_TR_NONE, ESYS_TR_NONE, ESYS_TR_NONE, &sel, &counter,
                                   &sel_out, &values);
  const bool ok = rc == TSS2_RC_SUCCESS && sel_out != nullptr && values != nullptr &&
                  sel_out->count == 1 && sel_out->pcrSelections[0].hash == TPM2_ALG_SHA256 &&
                  (sel_out->pcrSelections[0].pcrSelect[0] & 0x80) != 0 && values->count == 1;
  Esys_Free(sel_out);
  Esys_Free(values);
  return ok;
}

// The policy digest of PolicyPCR(sha256:7) at the current PCR value.
TSS2_RC pcr7PolicyDigest(ESYS_CONTEXT* esys, TPM2B_DIGEST& out) {
  TPMT_SYM_DEF sym{};
  sym.algorithm = TPM2_ALG_NULL;
  ESYS_TR trial = ESYS_TR_NONE;
  TSS2_RC rc =
      Esys_StartAuthSession(esys, ESYS_TR_NONE, ESYS_TR_NONE, ESYS_TR_NONE, ESYS_TR_NONE,
                            ESYS_TR_NONE, nullptr, TPM2_SE_TRIAL, &sym, TPM2_ALG_SHA256, &trial);
  if (rc != TSS2_RC_SUCCESS) {
    return rc;
  }
  Handles handles(esys);
  handles.add(trial);
  const TPM2B_DIGEST current{};  // empty: use the PCR's current value
  const TPML_PCR_SELECTION sel = pcr7Selection();
  rc = Esys_PolicyPCR(esys, trial, ESYS_TR_NONE, ESYS_TR_NONE, ESYS_TR_NONE, &current, &sel);
  if (rc != TSS2_RC_SUCCESS) {
    return rc;
  }
  TPM2B_DIGEST* digest = nullptr;
  rc = Esys_PolicyGetDigest(esys, trial, ESYS_TR_NONE, ESYS_TR_NONE, ESYS_TR_NONE, &digest);
  if (rc == TSS2_RC_SUCCESS) {
    out = *digest;
  }
  Esys_Free(digest);
  return rc;
}

// Loads `blob` under `primary` and unseals it into `out`.
TpmStatus unsealUnder(ESYS_CONTEXT* esys, ESYS_TR primary, const SealedBlob& blob,
                      SecretBuffer& out) {
  out.clear();
  Handles handles(esys);
  ESYS_TR object = ESYS_TR_NONE;
  TSS2_RC rc = Esys_Load(esys, primary, ESYS_TR_PASSWORD, ESYS_TR_NONE, ESYS_TR_NONE, &blob.priv,
                         &blob.pub, &object);
  if (rc != TSS2_RC_SUCCESS) {
    return isTpmLayer(rc) ? TpmStatus::LoadFailed : TpmStatus::Error;
  }
  handles.add(object);

  ESYS_TR session = ESYS_TR_NONE;
  const TPM2_SE type = blob.pcr7 ? TPM2_SE_POLICY : TPM2_SE_HMAC;
  rc = startSession(esys, primary, type, TPMA_SESSION_ENCRYPT, &session);
  if (rc != TSS2_RC_SUCCESS) {
    return TpmStatus::Error;
  }
  handles.add(session);
  if (blob.pcr7) {
    const TPM2B_DIGEST current{};
    const TPML_PCR_SELECTION sel = pcr7Selection();
    rc = Esys_PolicyPCR(esys, session, ESYS_TR_NONE, ESYS_TR_NONE, ESYS_TR_NONE, &current, &sel);
    if (rc != TSS2_RC_SUCCESS) {
      return TpmStatus::Error;
    }
  }

  TPM2B_SENSITIVE_DATA* data = nullptr;
  rc = Esys_Unseal(esys, object, session, ESYS_TR_NONE, ESYS_TR_NONE, &data);
  if (rc != TSS2_RC_SUCCESS) {
    const TSS2_RC base = baseRc(rc);
    return (base == TPM2_RC_POLICY_FAIL || base == TPM2_RC_PCR_CHANGED) ? TpmStatus::PolicyFailed
                                                                        : TpmStatus::Error;
  }
  const bool ok = data->size > 0 && out.assign(data->buffer, data->size);
  explicit_bzero(data->buffer, sizeof(data->buffer));
  Esys_Free(data);
  return ok ? TpmStatus::Ok : TpmStatus::Error;
}

}  // namespace

std::unique_ptr<TpmContext> TpmContext::open(TSS2_TCTI_INIT_FUNC init, const char* conf) {
  size_t size = 0;
  if (init(nullptr, &size, conf) != TSS2_RC_SUCCESS || size == 0) {
    return nullptr;
  }
  auto* tcti = static_cast<TSS2_TCTI_CONTEXT*>(calloc(1, size));
  if (tcti == nullptr) {
    return nullptr;
  }
  if (init(tcti, &size, conf) != TSS2_RC_SUCCESS) {
    free(tcti);
    return nullptr;
  }
  ESYS_CONTEXT* esys = nullptr;
  if (Esys_Initialize(&esys, tcti, nullptr) != TSS2_RC_SUCCESS) {
    Tss2_Tcti_Finalize(tcti);
    free(tcti);
    return nullptr;
  }
  return std::unique_ptr<TpmContext>(new TpmContext(tcti, esys));
}

std::unique_ptr<TpmContext> TpmContext::openDevice() {
  if (access(kTpmDevice, F_OK) != 0) {
    return nullptr;
  }
  return open(Tss2_Tcti_Device_Init, kTpmDevice);
}

TpmContext::~TpmContext() {
  Esys_Finalize(&esys_);
  Tss2_Tcti_Finalize(tcti_);
  free(tcti_);
}

TpmStatus TpmContext::seal(const SecretBuffer& secret, bool pcr7, uint32_t uid, SealedBlob& out) {
  if (secret.empty() || secret.size() > sizeof(TPM2B_SENSITIVE_DATA::buffer)) {
    return TpmStatus::Error;
  }
  Handles handles(esys_);
  ESYS_TR primary = ESYS_TR_NONE;
  TSS2_RC rc = createPrimary(esys_, &primary);
  if (rc != TSS2_RC_SUCCESS) {
    return primaryStatus(rc);
  }
  handles.add(primary);

  TPM2B_DIGEST policy{};
  if (pcr7) {
    if (!hasPcr7(esys_)) {
      return TpmStatus::PcrUnsupported;
    }
    if (pcr7PolicyDigest(esys_, policy) != TSS2_RC_SUCCESS) {
      return TpmStatus::Error;
    }
  }

  ESYS_TR session = ESYS_TR_NONE;
  if (startSession(esys_, primary, TPM2_SE_HMAC, TPMA_SESSION_DECRYPT, &session) !=
      TSS2_RC_SUCCESS) {
    return TpmStatus::Error;
  }
  handles.add(session);

  TPM2B_SENSITIVE_CREATE sensitive{};
  sensitive.sensitive.data.size = static_cast<UINT16>(secret.size());
  memcpy(sensitive.sensitive.data.buffer, secret.data(), secret.size());
  const TPM2B_PUBLIC tmpl = sealedObjectTemplate(pcr7 ? &policy : nullptr);
  TPM2B_DATA outside{};
  TPML_PCR_SELECTION creation_pcr{};
  TPM2B_PRIVATE* priv = nullptr;
  TPM2B_PUBLIC* pub = nullptr;
  rc = Esys_Create(esys_, primary, session, ESYS_TR_NONE, ESYS_TR_NONE, &sensitive, &tmpl, &outside,
                   &creation_pcr, &priv, &pub, nullptr, nullptr, nullptr);
  explicit_bzero(&sensitive, sizeof(sensitive));
  if (rc != TSS2_RC_SUCCESS) {
    Esys_Free(priv);
    Esys_Free(pub);
    return TpmStatus::Error;
  }

  SealedBlob blob;
  blob.uid = uid;
  blob.pcr7 = pcr7;
  blob.pub = *pub;
  blob.priv = *priv;
  Esys_Free(priv);
  Esys_Free(pub);

  // Prove the blob round-trips before anything is stored.
  SecretBuffer check;
  const TpmStatus status = unsealUnder(esys_, primary, blob, check);
  if (status != TpmStatus::Ok) {
    return status;
  }
  if (!check.equals(secret)) {
    return TpmStatus::Error;
  }
  out = blob;
  return TpmStatus::Ok;
}

TpmStatus TpmContext::unseal(const SealedBlob& blob, SecretBuffer& out) {
  out.clear();
  Handles handles(esys_);
  ESYS_TR primary = ESYS_TR_NONE;
  const TSS2_RC rc = createPrimary(esys_, &primary);
  if (rc != TSS2_RC_SUCCESS) {
    return primaryStatus(rc);
  }
  handles.add(primary);
  return unsealUnder(esys_, primary, blob, out);
}

}  // namespace biopass
