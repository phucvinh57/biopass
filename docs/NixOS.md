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
          programs.biopass.services = [ "sudo" "login" ];
        }
      ];
    };
  };
}
```

Rebuild, then open **Biopass** to enroll your face or fingerprint. Keep a root shell open the first time you test `sudo`.

## What the module does

- Installs the package, built from your system's nixpkgs.
- Adds `auth sufficient libbiopass_pam.so` before the password rules of each listed service, and turns off `pam_fprintd` there (Biopass reads fingerprints itself). A failed or cancelled match falls through to the password.
- Refuses a service that also uses Google Authenticator or Duo: a Biopass match would skip that second factor.
- Links `/usr/bin/biopass-helper`, `/usr/libexec/biopass`, `/usr/lib/biopass`, `/usr/share/biopass` and `/usr/bin/pkexec` into place: Biopass runs them by these absolute paths.

## Limitations

- polkit (`polkit-1`) needs more setup, see [Polkit.md](Polkit.md): Biopass ignores it by default.
- Fingerprint needs `services.fprintd.enable = true;`. The module doesn't turn it on, because that also adds `pam_fprintd` to every other PAM service.
- `/usr/bin/biopass-helper`, `/usr/libexec/biopass`, `/usr/lib/biopass` and `/usr/share/biopass` are owned by the module and replaced on boot.

- x86_64 only.
- Keyring unlock (the `authtok` option, [Keyring.md](Keyring.md)) isn't set up: NixOS runs the keyring modules before the password rule, so the saved password would arrive too late.
