#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// C interface to FingerprintAuth for the Rust app.

typedef enum {
  AUTH_SUCCESS = 0,
  AUTH_FAILURE = 1,
  AUTH_UNAVAILABLE = 2,
  AUTH_RETRY = 3,
} FingerprintAuthResult;

typedef struct {
  int retries;
} FingerprintAuthConfig;

typedef void (*EnrollProgressCallback)(bool done, const char* status, void* user_data);

void* fingerprint_auth_new(void);
void fingerprint_auth_free(void* auth);
bool fingerprint_is_available(void* auth);
char** fingerprint_list_enrolled_fingers(void* auth, const char* username, int* count);
void fingerprint_free_string_array(char** array, int count);
bool fingerprint_enroll(void* auth, const char* username, const char* finger_name,
                        EnrollProgressCallback callback, void* user_data);
bool fingerprint_remove_finger(void* auth, const char* username, const char* finger_name);

#ifdef __cplusplus
}
#endif
