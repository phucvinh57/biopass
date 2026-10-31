#include "fingerprint_ffi.h"

#include <cstring>

#include "fingerprint_auth.h"

extern "C" {

void* fingerprint_auth_new(void) {
  biopass::FingerprintMethodConfig default_config;
  return static_cast<void*>(new biopass::FingerprintAuth(default_config));
}

void fingerprint_auth_free(void* auth) {
  if (!auth)
    return;
  delete static_cast<biopass::FingerprintAuth*>(auth);
}

bool fingerprint_is_available(void* auth) {
  if (!auth)
    return false;
  return static_cast<biopass::FingerprintAuth*>(auth)->isAvailable();
}

char** fingerprint_list_enrolled_fingers(void* auth, const char* username, int* count) {
  if (!auth || !username || !count) {
    if (count)
      *count = 0;
    return nullptr;
  }

  auto* fp_auth = static_cast<biopass::FingerprintAuth*>(auth);
  std::vector<std::string> fingers = fp_auth->listEnrolledFingers(username);

  if (fingers.empty()) {
    *count = 0;
    return nullptr;
  }

  // +1 for the null terminator
  char** result = new char*[fingers.size() + 1];

  for (size_t i = 0; i < fingers.size(); ++i) {
    result[i] = new char[fingers[i].length() + 1];
    std::strcpy(result[i], fingers[i].c_str());
  }

  result[fingers.size()] = nullptr;

  *count = fingers.size();
  return result;
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
  return static_cast<biopass::FingerprintAuth*>(auth)->enroll(username, finger_name, callback,
                                                              user_data);
}

bool fingerprint_remove_finger(void* auth, const char* username, const char* finger_name) {
  if (!auth || !username || !finger_name)
    return false;
  return static_cast<biopass::FingerprintAuth*>(auth)->removeFinger(username, finger_name);
}

}  // extern "C"
