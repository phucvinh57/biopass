#include "sealed_file.h"

#include <string.h>
#include <tss2/tss2_mu.h>

namespace biopass {

namespace {

constexpr uint8_t kMagic[4] = {'B', 'P', 'K', 'S'};

constexpr TPMA_OBJECT kSealedAttrs =
    TPMA_OBJECT_FIXEDTPM | TPMA_OBJECT_FIXEDPARENT | TPMA_OBJECT_NODA;

void putU16(uint8_t* p, uint16_t v) {
  p[0] = static_cast<uint8_t>(v >> 8);
  p[1] = static_cast<uint8_t>(v);
}

void putU32(uint8_t* p, uint32_t v) {
  p[0] = static_cast<uint8_t>(v >> 24);
  p[1] = static_cast<uint8_t>(v >> 16);
  p[2] = static_cast<uint8_t>(v >> 8);
  p[3] = static_cast<uint8_t>(v);
}

uint16_t getU16(const uint8_t* p) { return static_cast<uint16_t>((p[0] << 8) | p[1]); }

uint32_t getU32(const uint8_t* p) {
  return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
         (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}

}  // namespace

TPM2B_PUBLIC sealedObjectTemplate(const TPM2B_DIGEST* policy) {
  TPM2B_PUBLIC pub{};
  TPMT_PUBLIC& area = pub.publicArea;
  area.type = TPM2_ALG_KEYEDHASH;
  area.nameAlg = TPM2_ALG_SHA256;
  area.objectAttributes = kSealedAttrs;
  if (policy != nullptr) {
    area.authPolicy = *policy;
  } else {
    area.objectAttributes |= TPMA_OBJECT_USERWITHAUTH;
  }
  area.parameters.keyedHashDetail.scheme.scheme = TPM2_ALG_NULL;
  return pub;
}

bool sealedPublicMatches(const TPM2B_PUBLIC& pub, bool pcr7) {
  const TPMT_PUBLIC& area = pub.publicArea;
  const TPMA_OBJECT want = pcr7 ? kSealedAttrs : (kSealedAttrs | TPMA_OBJECT_USERWITHAUTH);
  return area.type == TPM2_ALG_KEYEDHASH && area.nameAlg == TPM2_ALG_SHA256 &&
         area.objectAttributes == want &&
         area.parameters.keyedHashDetail.scheme.scheme == TPM2_ALG_NULL &&
         area.authPolicy.size == (pcr7 ? 32 : 0);
}

std::vector<uint8_t> serializeSealed(const SealedBlob& blob) {
  if (!sealedPublicMatches(blob.pub, blob.pcr7)) {
    return {};
  }
  std::vector<uint8_t> out(kMaxSealedFileSize, 0);
  uint8_t* p = out.data();
  memcpy(p, kMagic, sizeof(kMagic));
  p[4] = kSealedVersion;
  p[5] = blob.pcr7 ? kSealedFlagPcr7 : 0;
  putU16(p + 6, 0);
  putU32(p + 8, blob.uid);
  p[12] = kPrimaryTemplateEccSrk;
  putU16(p + 13, blob.pcr7 ? TPM2_ALG_SHA256 : 0);
  putU32(p + 15, blob.pcr7 ? kPcr7Mask : 0);

  size_t offset = kSealedHeaderSize;
  if (Tss2_MU_TPM2B_PUBLIC_Marshal(&blob.pub, out.data(), out.size(), &offset) != TSS2_RC_SUCCESS ||
      Tss2_MU_TPM2B_PRIVATE_Marshal(&blob.priv, out.data(), out.size(), &offset) !=
          TSS2_RC_SUCCESS) {
    return {};
  }
  out.resize(offset);
  return out;
}

std::optional<SealedBlob> parseSealed(const uint8_t* data, size_t size) {
  if (data == nullptr || size < kSealedHeaderSize || size > kMaxSealedFileSize) {
    return std::nullopt;
  }
  if (memcmp(data, kMagic, sizeof(kMagic)) != 0 || data[4] != kSealedVersion) {
    return std::nullopt;
  }
  const uint8_t flags = data[5];
  if ((flags & ~kSealedFlagPcr7) != 0 || getU16(data + 6) != 0 ||
      data[12] != kPrimaryTemplateEccSrk) {
    return std::nullopt;
  }

  SealedBlob blob;
  blob.pcr7 = (flags & kSealedFlagPcr7) != 0;
  blob.uid = getU32(data + 8);
  const uint16_t pcr_alg = getU16(data + 13);
  const uint32_t pcr_mask = getU32(data + 15);
  if (blob.pcr7 ? (pcr_alg != TPM2_ALG_SHA256 || pcr_mask != kPcr7Mask)
                : (pcr_alg != 0 || pcr_mask != 0)) {
    return std::nullopt;
  }

  size_t offset = kSealedHeaderSize;
  if (Tss2_MU_TPM2B_PUBLIC_Unmarshal(data, size, &offset, &blob.pub) != TSS2_RC_SUCCESS ||
      Tss2_MU_TPM2B_PRIVATE_Unmarshal(data, size, &offset, &blob.priv) != TSS2_RC_SUCCESS ||
      offset != size) {
    return std::nullopt;
  }
  if (!sealedPublicMatches(blob.pub, blob.pcr7)) {
    return std::nullopt;
  }
  return blob;
}

}  // namespace biopass
