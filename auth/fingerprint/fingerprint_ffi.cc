#include "fingerprint_ffi.h"

#include <spdlog/spdlog.h>

#include <cstring>
#include <exception>

#include "fingerprint_auth.h"

namespace {

// A C++ exception must not unwind into the Rust caller (undefined
// behaviour), so each entry point catches everything and returns failure.
void logException(const char* fn) {
  try {
    throw;
  } catch (const std::exception& e) {
    spdlog::error("{}: {}", fn, e.what());
  } catch (...) {
    spdlog::error("{}: unknown exception", fn);
  }
}

}  // namespace

extern "C" {

void* fingerprint_auth_new(void) {
  try {
    biopass::FingerprintMethodConfig default_config;
    return static_cast<void*>(new biopass::FingerprintAuth(default_config));
  } catch (...) {
    logException(__func__);
    return nullptr;
  }
}

void fingerprint_auth_free(void* auth) {
  if (!auth)
    return;
  delete static_cast<biopass::FingerprintAuth*>(auth);
}

bool fingerprint_is_available(void* auth) {
  if (!auth)
    return false;
  try {
    return static_cast<biopass::FingerprintAuth*>(auth)->isAvailable();
  } catch (...) {
    logException(__func__);
    return false;
  }
}

char** fingerprint_list_enrolled_fingers(void* auth, const char* username, int* count) {
  if (!auth || !username || !count) {
    if (count)
      *count = 0;
    return nullptr;
  }

  *count = 0;
  char** result = nullptr;
  size_t filled = 0;
  try {
    auto* fp_auth = static_cast<biopass::FingerprintAuth*>(auth);
    std::vector<std::string> fingers = fp_auth->listEnrolledFingers(username);

    if (fingers.empty()) {
      return nullptr;
    }

    // +1 for the null terminator
    result = new char*[fingers.size() + 1]();

    for (; filled < fingers.size(); ++filled) {
      result[filled] = new char[fingers[filled].length() + 1];
      std::strcpy(result[filled], fingers[filled].c_str());
    }

    *count = static_cast<int>(fingers.size());
    return result;
  } catch (...) {
    logException(__func__);
    if (result) {
      for (size_t i = 0; i < filled; ++i) {
        delete[] result[i];
      }
      delete[] result;
    }
    *count = 0;
    return nullptr;
  }
}

void fingerprint_free_string_array(char** array, int count) {
  if (!array)
    return;

  for (int i = 0; i < count; ++i) {
    delete[] array[i];
  }
  delete[] array;
}

bool fingerprint_enroll(void* auth, const char* username, const char* finger_name,
                        EnrollProgressCallback callback, void* user_data) {
  if (!auth || !username || !finger_name)
    return false;
  try {
    return static_cast<biopass::FingerprintAuth*>(auth)->enroll(username, finger_name, callback,
                                                                user_data);
  } catch (...) {
    logException(__func__);
    return false;
  }
}

bool fingerprint_remove_finger(void* auth, const char* username, const char* finger_name) {
  if (!auth || !username || !finger_name)
    return false;
  try {
    return static_cast<biopass::FingerprintAuth*>(auth)->removeFinger(username, finger_name);
  } catch (...) {
    logException(__func__);
    return false;
  }
}

}  // extern "C"
