# NixOS

The flake packages the release `.deb` (`packages.x86_64-linux.biopass`) and has a NixOS module (`nixosModules.biopass`).

## Setup

In your system flake:

```nix
{
  inputs.biopass.url = "github:phucvinh57/biopass";

  outputs = { nixpkgs, biopass, ... }: {
    nixosConfigurations.<host> = nixpkgs.lib.nixosSystem {
      modules = [
        biopass.nixosModules.biopass
        {
          programs.biopass.enable = true;
          # PAM services where a face or fingerprint match replaces the password.
          programs.biopass.services = [ "sudo" "login" "polkit-1" ];
        }
      ];
    };
  };
}
```

Rebuild, then open **Biopass** to enroll your face or fingerprint. Keep a root shell open the first time you test `sudo`.

## What the module does

- Installs the package and enables `fprintd`.
- Adds `auth sufficient libbiopass_pam.so` before the password rules of each listed service, and turns off `pam_fprintd` there (Biopass reads fingerprints itself). A failed or cancelled match falls through to the password.
- Links `/usr/bin/biopass-helper`, `/usr/libexec/biopass`, `/usr/lib/biopass`, `/usr/share/biopass` and `/usr/bin/pkexec` into place: Biopass runs them by these absolute paths.

## Limitations

- x86_64 only.
- Keyring unlock (the `authtok` option, [Keyring.md](Keyring.md)) isn't set up: NixOS runs the keyring modules before the password rule, so the saved password would arrive too late.
