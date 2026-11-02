// Checks the sealed password file format (keyring/sealed_file.h): a blob
// round-trips, and anything malformed or inconsistent is refused.

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "sealed_file.h"

namespace {

int g_failures = 0;

void expect(bool cond, const std::string& what) {
  std::printf("[%s] %s\n", cond ? "PASS" : "FAIL", what.c_str());
  if (!cond) {
    g_failures++;
  }
}

int finish() {
  if (g_failures != 0) {
    std::printf("%d FAILED\n", g_failures);
    return 1;
  }
  std::printf("ALL PASS\n");
  return 0;
}

}  // namespace

biopass::SealedBlob makeBlob(bool pcr7) {
  biopass::SealedBlob blob;
  blob.uid = 1000;
  blob.pcr7 = pcr7;
  TPM2B_DIGEST policy{};
  policy.size = 32;
  std::memset(policy.buffer, 0xab, 32);
  blob.pub = biopass::sealedObjectTemplate(pcr7 ? &policy : nullptr);
  blob.pub.publicArea.unique.keyedHash.size = 32;
  std::memset(blob.pub.publicArea.unique.keyedHash.buffer, 0x11, 32);
  blob.priv.size = 100;
  std::memset(blob.priv.buffer, 0x5a, 100);
  return blob;
}

bool parses(const std::vector<uint8_t>& data) {
  return biopass::parseSealed(data.data(), data.size()).has_value();
}

int main() {
  for (const bool pcr7 : {false, true}) {
    const std::string tag = pcr7 ? " (pcr7)" : " (unbound)";
    const biopass::SealedBlob blob = makeBlob(pcr7);
    const std::vector<uint8_t> data = biopass::serializeSealed(blob);
    expect(!data.empty() && data.size() <= biopass::kMaxSealedFileSize, "serializes" + tag);
    const auto back = biopass::parseSealed(data.data(), data.size());
    expect(back.has_value() && back->uid == 1000 && back->pcr7 == pcr7 && back->priv.size == 100 &&
               std::memcmp(back->priv.buffer, blob.priv.buffer, 100) == 0 &&
               back->pub.publicArea.authPolicy.size == (pcr7 ? 32 : 0),
           "round-trips" + tag);

    auto mutate = [&](size_t at, uint8_t value) {
      std::vector<uint8_t> copy = data;
      copy[at] = value;
      return copy;
    };
    expect(!parses(mutate(0, 'X')), "bad magic refused" + tag);
    expect(!parses(mutate(4, 2)), "unknown version refused" + tag);
    expect(!parses(mutate(5, data[5] | 0x02)), "unknown flag refused" + tag);
    expect(!parses(mutate(5, data[5] ^ 0x01)), "flag/policy mismatch refused" + tag);
    expect(!parses(mutate(7, 1)), "reserved bytes refused" + tag);
    expect(!parses(mutate(12, 2)), "unknown primary template refused" + tag);
    expect(!parses(mutate(16, 0x40)), "other PCR mask refused" + tag);

    std::vector<uint8_t> truncated(data.begin(), data.end() - 1);
    expect(!parses(truncated), "truncated refused" + tag);
    std::vector<uint8_t> trailing = data;
    trailing.push_back(0);
    expect(!parses(trailing), "trailing byte refused" + tag);
    expect(!parses(std::vector<uint8_t>(data.begin(), data.begin() + 10)),
           "short header refused" + tag);
  }

  std::vector<uint8_t> huge(biopass::kMaxSealedFileSize + 1, 0);
  expect(!parses(huge), "over 4 KiB refused");
  expect(!biopass::parseSealed(nullptr, 0).has_value(), "null refused");

  // An object the template wouldn't make (e.g. auth value allowed while bound).
  biopass::SealedBlob odd = makeBlob(true);
  odd.pub.publicArea.objectAttributes |= TPMA_OBJECT_USERWITHAUTH;
  expect(biopass::serializeSealed(odd).empty(), "bound object with userWithAuth not written");
  biopass::SealedBlob odd2 = makeBlob(false);
  odd2.pub.publicArea.objectAttributes &= ~TPMA_OBJECT_FIXEDTPM;
  expect(biopass::serializeSealed(odd2).empty(), "duplicable object not written");
  return finish();
}
