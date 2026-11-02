#pragma once

#include <tss2/tss2_esys.h>
#include <tss2/tss2_tcti.h>

#include <cstdint>
#include <memory>

#include "sealed_file.h"
#include "secret_buffer.h"

namespace biopass {

enum class TpmStatus {
  Ok,
  NoTpm,           // no TPM device, or it can't be opened
  OwnerAuthSet,    // the owner hierarchy has a password: unsupported
  PcrUnsupported,  // no active SHA-256 PCR bank
  PolicyFailed,    // PCR 7 changed since the password was saved
  LoadFailed,      // the TPM no longer accepts the blob (cleared, other TPM)
  Error,
};

// An ESYS context on one TCTI. Not copyable; closes everything on
// destruction.
class TpmContext {
 public:
  // `init` is a TCTI init function (Tss2_Tcti_Device_Init in production,
  // Tss2_Tcti_Swtpm_Init in tests) and `conf` its config string.
  static std::unique_ptr<TpmContext> open(TSS2_TCTI_INIT_FUNC init, const char* conf);
  // The kernel resource manager, /dev/tpmrm0. Null if there is none.
  static std::unique_ptr<TpmContext> openDevice();

  ~TpmContext();
  TpmContext(const TpmContext&) = delete;
  TpmContext& operator=(const TpmContext&) = delete;

  // Seals `secret` (1..kMaxSecretLen bytes) for `uid`, bound to the current
  // PCR 7 value when `pcr7`, and checks that it unseals again.
  TpmStatus seal(const SecretBuffer& secret, bool pcr7, uint32_t uid, SealedBlob& out);

  // Unseals `blob` into `out`.
  TpmStatus unseal(const SealedBlob& blob, SecretBuffer& out);

 private:
  TpmContext(TSS2_TCTI_CONTEXT* tcti, ESYS_CONTEXT* esys) : tcti_(tcti), esys_(esys) {}

  TSS2_TCTI_CONTEXT* tcti_;
  ESYS_CONTEXT* esys_;
};

}  // namespace biopass
