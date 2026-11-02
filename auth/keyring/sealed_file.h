#pragma once

#include <tss2/tss2_tpm2_types.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace biopass {

// On-disk format of /var/lib/biopass/keyring/<uid>.sealed, version 1. All
// integers big-endian; nothing may follow the last field.
//
//   0  4  magic "BPKS"
//   4  1  version (1)
//   5  1  flags: bit 0 = bound to PCR 7 (sha256); other bits must be 0
//   6  2  reserved, 0
//   8  4  uid the password was saved for
//  12  1  primary key template id (1 = ECC P-256 SRK, owner hierarchy)
//  13  2  PCR bank hash alg (TPM2_ALG_SHA256) when bound, else 0
//  15  4  PCR mask (0x80 = PCR 7) when bound, else 0
//  19  N  TPM2B_PUBLIC  of the sealed object (Tss2_MU)
//  19+N M TPM2B_PRIVATE of the sealed object (Tss2_MU)
inline constexpr size_t kSealedHeaderSize = 19;
inline constexpr size_t kMaxSealedFileSize = 4096;
inline constexpr uint8_t kSealedVersion = 1;
inline constexpr uint8_t kSealedFlagPcr7 = 0x01;
inline constexpr uint8_t kPrimaryTemplateEccSrk = 1;
inline constexpr uint32_t kPcr7Mask = 0x80;

struct SealedBlob {
  uint32_t uid = 0;
  bool pcr7 = false;
  TPM2B_PUBLIC pub{};
  TPM2B_PRIVATE priv{};
};

// The public area of a sealed password. Unbound objects unseal with an empty
// auth value; bound ones only through `policy` (PolicyPCR on PCR 7).
TPM2B_PUBLIC sealedObjectTemplate(const TPM2B_DIGEST* policy);

// True when `pub` is exactly what sealedObjectTemplate() makes, bound or not
// as `pcr7` says (the policy digest itself is checked by the TPM).
bool sealedPublicMatches(const TPM2B_PUBLIC& pub, bool pcr7);

// Empty on failure.
std::vector<uint8_t> serializeSealed(const SealedBlob& blob);

// nullopt for anything that isn't a well-formed version 1 file.
std::optional<SealedBlob> parseSealed(const uint8_t* data, size_t size);

}  // namespace biopass
