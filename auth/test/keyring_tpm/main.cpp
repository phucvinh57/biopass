// Seals and unseals against swtpm (keyring/tpm_seal.h): unbound and PCR 7
// bound passwords round-trip, a changed PCR 7 or another TPM refuses them.
// Exits 77 (skipped) when swtpm isn't installed.

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <tss2/tss2_esys.h>
#include <tss2/tss2_tcti_swtpm.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

#include "tpm_seal.h"

namespace {

int g_failures = 0;

void expect(bool cond, const std::string& what) {
  std::printf("[%s] %s\n", cond ? "PASS" : "FAIL", what.c_str());
  if (!cond) {
    g_failures++;
  }
}

// One swtpm process with its own state dir, killed on destruction.
class Swtpm {
 public:
  explicit Swtpm(int port) : port_(port) {
    char tmpl[] = "/tmp/biopass-swtpm-XXXXXX";
    if (mkdtemp(tmpl) == nullptr) {
      return;
    }
    dir_ = tmpl;
    pid_ = fork();
    if (pid_ == 0) {
      const int devnull = open("/dev/null", O_RDWR);
      dup2(devnull, STDOUT_FILENO);
      dup2(devnull, STDERR_FILENO);
      const std::string state = "dir=" + dir_;
      const std::string server = "type=tcp,port=" + std::to_string(port);
      const std::string ctrl = "type=tcp,port=" + std::to_string(port + 1);
      execlp("swtpm", "swtpm", "socket", "--tpm2", "--tpmstate", state.c_str(), "--server",
             server.c_str(), "--ctrl", ctrl.c_str(), "--flags", "not-need-init,startup-clear",
             static_cast<char*>(nullptr));
      _exit(127);
    }
  }
  // Stops swtpm through its control channel: Ubuntu's AppArmor profile for
  // swtpm refuses signals from unconfined processes. Never waits forever.
  ~Swtpm() {
    if (pid_ > 0) {
      sendShutdown();
      bool reaped = false;
      for (int i = 0; i < 50 && !reaped; ++i) {
        reaped = waitpid(pid_, nullptr, WNOHANG) == pid_;
        if (!reaped) {
          std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
      }
      if (!reaped) {
        kill(pid_, SIGKILL);
        std::printf("swtpm %d did not stop\n", static_cast<int>(pid_));
      }
    }
    if (!dir_.empty()) {
      const std::string cmd = "rm -rf '" + dir_ + "'";
      if (system(cmd.c_str()) != 0) {
        std::printf("cleanup failed\n");
      }
    }
  }
  // CMD_SHUTDOWN (3) on the control port.
  void sendShutdown() const {
    const int fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
      return;
    }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port_ + 1));
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0) {
      const uint8_t cmd[4] = {0, 0, 0, 3};
      if (write(fd, cmd, sizeof(cmd)) == sizeof(cmd)) {
        uint8_t resp[4];
        if (read(fd, resp, sizeof(resp)) < 0) {
          // Shutting down anyway.
        }
      }
    }
    close(fd);
  }

  std::string conf() const { return "host=127.0.0.1,port=" + std::to_string(port_); }

  // Retries while swtpm starts listening.
  std::unique_ptr<biopass::TpmContext> connect() const {
    for (int i = 0; i < 50; ++i) {
      if (auto ctx = biopass::TpmContext::open(Tss2_Tcti_Swtpm_Init, conf().c_str())) {
        return ctx;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return nullptr;
  }

  // Extends PCR 7 in every bank, as a Secure Boot change would.
  bool extendPcr7() const {
    size_t size = 0;
    if (Tss2_Tcti_Swtpm_Init(nullptr, &size, conf().c_str()) != TSS2_RC_SUCCESS) {
      return false;
    }
    auto* tcti = static_cast<TSS2_TCTI_CONTEXT*>(calloc(1, size));
    ESYS_CONTEXT* esys = nullptr;
    bool ok = Tss2_Tcti_Swtpm_Init(tcti, &size, conf().c_str()) == TSS2_RC_SUCCESS &&
              Esys_Initialize(&esys, tcti, nullptr) == TSS2_RC_SUCCESS;
    if (ok) {
      TPML_DIGEST_VALUES digests{};
      digests.count = 1;
      digests.digests[0].hashAlg = TPM2_ALG_SHA256;
      memset(digests.digests[0].digest.sha256, 0x42, TPM2_SHA256_DIGEST_SIZE);
      ok = Esys_PCR_Extend(esys, ESYS_TR_PCR7, ESYS_TR_PASSWORD, ESYS_TR_NONE, ESYS_TR_NONE,
                           &digests) == TSS2_RC_SUCCESS;
    }
    if (esys != nullptr) {
      Esys_Finalize(&esys);
    }
    Tss2_Tcti_Finalize(tcti);
    free(tcti);
    return ok;
  }

 private:
  int port_;
  std::string dir_;
  pid_t pid_ = -1;
};

bool haveSwtpm() { return system("command -v swtpm >/dev/null 2>&1") == 0; }

}  // namespace

int main() {
  if (!haveSwtpm()) {
    std::printf("SKIP: swtpm not found\n");
    return 77;
  }
  setenv("TSS2_LOG", "all+NONE", 1);
  const int port = 20000 + (getpid() % 20000);
  Swtpm tpm(port);
  std::unique_ptr<biopass::TpmContext> ctx = tpm.connect();
  expect(ctx != nullptr, "connect to swtpm");
  if (!ctx) {
    return 1;
  }

  biopass::SecretBuffer secret;
  const std::string pw = "p\xc3\xa4ss word 123";
  secret.assign(pw.data(), pw.size());

  biopass::SealedBlob unbound;
  expect(ctx->seal(secret, false, 1000, unbound) == biopass::TpmStatus::Ok, "seal unbound");
  biopass::SecretBuffer out;
  expect(ctx->unseal(unbound, out) == biopass::TpmStatus::Ok && out.equals(secret),
         "unseal unbound");

  biopass::SealedBlob bound;
  expect(ctx->seal(secret, true, 1000, bound) == biopass::TpmStatus::Ok && bound.pcr7,
         "seal bound to PCR 7");
  expect(ctx->unseal(bound, out) == biopass::TpmStatus::Ok && out.equals(secret), "unseal bound");

  biopass::SecretBuffer longest;
  const std::string max(biopass::kMaxSecretLen, 'z');
  longest.assign(max.data(), max.size());
  biopass::SealedBlob big;
  expect(ctx->seal(longest, false, 1000, big) == biopass::TpmStatus::Ok &&
             ctx->unseal(big, out) == biopass::TpmStatus::Ok && out.equals(longest),
         "128-byte password");

  biopass::SealedBlob tampered = unbound;
  tampered.priv.buffer[tampered.priv.size / 2] ^= 0xff;
  expect(ctx->unseal(tampered, out) == biopass::TpmStatus::LoadFailed && out.empty(),
         "tampered blob refused");

  expect(tpm.extendPcr7(), "extend PCR 7");
  expect(ctx->unseal(bound, out) == biopass::TpmStatus::PolicyFailed && out.empty(),
         "bound blob refused after PCR 7 changed");
  expect(ctx->unseal(unbound, out) == biopass::TpmStatus::Ok && out.equals(secret),
         "unbound blob unaffected by PCR 7");

  ctx.reset();
  Swtpm other(port + 2);
  std::unique_ptr<biopass::TpmContext> other_ctx = other.connect();
  expect(other_ctx != nullptr, "connect to a second swtpm");
  if (other_ctx) {
    expect(other_ctx->unseal(unbound, out) == biopass::TpmStatus::LoadFailed && out.empty(),
           "another TPM refuses the blob");
  }

  if (g_failures != 0) {
    std::printf("%d FAILED\n", g_failures);
    return 1;
  }
  std::printf("ALL PASS\n");
  return 0;
}
