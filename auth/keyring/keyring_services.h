#pragma once

#include <algorithm>
#include <array>
#include <string_view>

namespace biopass {

// PAM services of login screens, the only ones where biopass-helper hands the
// saved password to PAM: that is where pam_gnome_keyring / pam_kwallet pick it
// up. sudo, su and polkit never get it. Lock screens that reuse one of these
// services as root (GNOME's unlocks through gdm-password) do; ones that run
// PAM as the user can't read the sealed file.
inline constexpr std::array<std::string_view, 7> kKeyringServices = {
    "gdm-password", "sddm", "lightdm", "login", "greetd", "lxdm", "ly",
};

inline bool isKeyringService(std::string_view service) {
  return std::find(kKeyringServices.begin(), kKeyringServices.end(), service) !=
         kKeyringServices.end();
}

}  // namespace biopass
