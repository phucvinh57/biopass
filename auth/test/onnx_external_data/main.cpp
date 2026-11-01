// An ONNX initializer can keep its data in an external file, which ONNX
// Runtime opens by itself while building the session. Checks that
// makeEngineAsUser() (face/common/onnx_session.h) builds the session as the
// user, so such a file is only read if the user could read it. The root
// checks run with sudo or inside `unshare --map-root-user --map-auto`.

#include <pwd.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>

#include "onnx_session.h"

namespace {

int g_failures = 0;

void expect(bool cond, const char* what) {
  std::printf("[%s] %s\n", cond ? "PASS" : "FAIL", what);
  if (!cond) {
    g_failures++;
  }
}

// Minimal protobuf writer, enough for the ONNX messages below.
std::string varint(uint64_t v) {
  std::string out;
  while (v >= 0x80) {
    out.push_back(static_cast<char>((v & 0x7f) | 0x80));
    v >>= 7;
  }
  out.push_back(static_cast<char>(v));
  return out;
}

std::string intField(int field, uint64_t v) { return varint(field << 3 | 0) + varint(v); }

std::string bytesField(int field, const std::string& v) {
  return varint(field << 3 | 2) + varint(v.size()) + v;
}

// Model: Y = X + W, where W is a float[16] initializer whose 64 bytes live
// in `location`. Adding an input and using more than one element keep the
// graph optimizers from touching W, so the session really loads it.
std::string externalDataModel(const std::string& location) {
  const std::string entry_location = bytesField(1, "location") + bytesField(2, location);
  const std::string entry_length = bytesField(1, "length") + bytesField(2, "64");
  const std::string tensor = intField(1, 16)                   // dims: [16]
                             + intField(2, 1)                  // data_type: FLOAT
                             + bytesField(8, "W")              // name
                             + bytesField(13, entry_location)  // external_data
                             + bytesField(13, entry_length)    //
                             + intField(14, 1);                // data_location: EXTERNAL
  const std::string node =
      bytesField(1, "X") + bytesField(1, "W") + bytesField(2, "Y") + bytesField(4, "Add");
  const std::string dim = intField(1, 16);
  const std::string shape = bytesField(1, dim);
  const std::string tensor_type = intField(1, 1) + bytesField(2, shape);
  const std::string type = bytesField(1, tensor_type);
  const std::string input = bytesField(1, "X") + bytesField(2, type);
  const std::string output = bytesField(1, "Y") + bytesField(2, type);
  const std::string graph = bytesField(1, node) + bytesField(2, "g") + bytesField(5, tensor) +
                            bytesField(11, input) + bytesField(12, output);
  const std::string opset = bytesField(1, "") + intField(2, 13);
  return intField(1, 8)           // ir_version
         + bytesField(7, graph)   // graph
         + bytesField(8, opset);  // opset_import
}

void writeFile(const std::string& path, const std::string& content) {
  std::ofstream(path, std::ios::binary) << content;
}

bool builds(const std::string& username, const std::string& model_path) {
  try {
    biopass::makeEngineAsUser<biopass::OnnxSession>(username, model_path, "test");
    return true;
  } catch (const std::exception&) {
    return false;
  }
}

}  // namespace

int main() {
  std::string tmpl = "/tmp/biopass-onnx-XXXXXX";
  const char* tmp = mkdtemp(tmpl.data());
  expect(tmp != nullptr, "create temp dir");
  if (tmp == nullptr) {
    return 1;
  }
  const std::string dir = tmp;
  chmod(dir.c_str(), 0755);

  const std::string data = dir + "/weights.bin";
  writeFile(data, std::string(64, '\0'));
  chmod(data.c_str(), 0644);
  const std::string model = dir + "/model.onnx";
  writeFile(model, externalDataModel(data));
  chmod(model.c_str(), 0644);

  if (geteuid() != 0) {
    const struct passwd* self = getpwuid(geteuid());
    expect(self != nullptr && builds(self->pw_name, model),
           "model with readable external data builds");
    std::printf("SKIP root checks (run with sudo or unshare --map-root-user --map-auto)\n");
  } else {
    expect(builds("nobody", model), "model with external data the user can read builds");

    // External data only root can read: ORT opens it itself, by path.
    const std::string secret = dir + "/secret.bin";
    writeFile(secret, std::string(64, '\0'));
    chmod(secret.c_str(), 0600);
    const std::string secret_model = dir + "/secret.onnx";
    writeFile(secret_model, externalDataModel(secret));
    chmod(secret_model.c_str(), 0644);
    expect(builds("root", secret_model), "root itself does open the external data file");
    expect(!builds("nobody", secret_model), "the user's model can't make root read it");

    // A FIFO the user can't open: refused at once instead of blocking.
    const std::string fifo = dir + "/fifo";
    expect(mkfifo(fifo.c_str(), 0600) == 0, "create root-only FIFO");
    const std::string fifo_model = dir + "/fifo.onnx";
    writeFile(fifo_model, externalDataModel(fifo));
    chmod(fifo_model.c_str(), 0644);
    alarm(10);  // a hang becomes a failed run
    expect(!builds("nobody", fifo_model), "external data FIFO is refused without blocking");
    alarm(0);

    expect(!builds("biopass-no-such-user", model), "unknown user is refused");
  }

  std::printf("%s\n", g_failures == 0 ? "ALL PASS" : "FAILURES");
  return g_failures == 0 ? 0 : 1;
}
