use std::ffi::{CStr, CString};
use std::os::raw::c_char;

#[link(name = "biopass_fingerprint")]
extern "C" {
    fn fingerprint_auth_new() -> *mut std::ffi::c_void;
    fn fingerprint_auth_free(auth: *mut std::ffi::c_void);
    fn fingerprint_is_available(auth: *mut std::ffi::c_void) -> bool;
    fn fingerprint_list_enrolled_fingers(
        auth: *mut std::ffi::c_void,
        username: *const c_char,
        count: *mut i32,
    ) -> *mut *mut c_char;

    fn fingerprint_free_string_array(array: *mut *mut c_char, count: i32);

    fn fingerprint_enroll(
        auth: *mut std::ffi::c_void,
        username: *const c_char,
        finger_name: *const c_char,
        callback: Option<
            unsafe extern "C" fn(
                done: bool,
                status: *const c_char,
                user_data: *mut std::ffi::c_void,
            ),
        >,
        user_data: *mut std::ffi::c_void,
    ) -> bool;

    fn fingerprint_remove_finger(
        auth: *mut std::ffi::c_void,
        username: *const c_char,
        finger_name: *const c_char,
    ) -> bool;
}

pub struct FingerprintAuth {
    inner: *mut std::ffi::c_void,
}

impl FingerprintAuth {
    pub fn new() -> Self {
        let inner = unsafe { fingerprint_auth_new() };
        FingerprintAuth { inner }
    }

    pub fn is_available(&self) -> bool {
        unsafe { fingerprint_is_available(self.inner) }
    }

    pub fn list_enrolled_fingers(&self, username: &str) -> Result<Vec<String>, String> {
        let username_c = CString::new(username).map_err(|_| "Invalid username".to_string())?;

        let mut count = 0i32;
        let array = unsafe {
            fingerprint_list_enrolled_fingers(self.inner, username_c.as_ptr(), &mut count)
        };

        if array.is_null() {
            return Ok(Vec::new());
        }

        let mut result = Vec::new();

        for i in 0..count {
            let ptr = unsafe { *array.add(i as usize) };
            if !ptr.is_null() {
                let finger_name = unsafe { CStr::from_ptr(ptr) }.to_string_lossy().to_string();
                result.push(finger_name);
            }
        }

        unsafe {
            fingerprint_free_string_array(array, count);
        }

        Ok(result)
    }

    pub fn enroll(
        &self,
        username: &str,
        finger_name: &str,
        app_handle: &tauri::AppHandle,
    ) -> Result<bool, String> {
        let username_c = CString::new(username).map_err(|_| "Invalid username".to_string())?;
        let finger_name_c =
            CString::new(finger_name).map_err(|_| "Invalid finger name".to_string())?;

        unsafe extern "C" fn enroll_callback(
            done: bool,
            status: *const c_char,
            user_data: *mut std::ffi::c_void,
        ) {
            use tauri::Emitter;
            let app = &*(user_data as *const tauri::AppHandle);
            let status_str = CStr::from_ptr(status).to_string_lossy().into_owned();

            #[derive(serde::Serialize, Clone)]
            struct ProgressPayload {
                done: bool,
                status: String,
            }

            app.emit(
                "fingerprint-enroll-status",
                ProgressPayload {
                    done,
                    status: status_str,
                },
            )
            .ok();
        }

        let success = unsafe {
            fingerprint_enroll(
                self.inner,
                username_c.as_ptr(),
                finger_name_c.as_ptr(),
                Some(enroll_callback),
                app_handle as *const tauri::AppHandle as *mut std::ffi::c_void,
            )
        };

        Ok(success)
    }

    pub fn remove_finger(&self, username: &str, finger_name: &str) -> Result<bool, String> {
        let username_c = CString::new(username).map_err(|_| "Invalid username".to_string())?;
        let finger_name_c =
            CString::new(finger_name).map_err(|_| "Invalid finger name".to_string())?;

        let success = unsafe {
            fingerprint_remove_finger(self.inner, username_c.as_ptr(), finger_name_c.as_ptr())
        };

        Ok(success)
    }
}

impl Default for FingerprintAuth {
    fn default() -> Self {
        Self::new()
    }
}

impl Drop for FingerprintAuth {
    fn drop(&mut self) {
        unsafe {
            fingerprint_auth_free(self.inner);
        }
    }
}
