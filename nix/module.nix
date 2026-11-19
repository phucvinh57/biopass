self:
{ config, lib, pkgs, ... }:

let
  cfg = config.programs.biopass;
  pam = config.security.pam.services;
in
{
  options.programs.biopass = {
    enable = lib.mkEnableOption "Biopass face and fingerprint login";

    package = lib.mkOption {
      type = lib.types.package;
      default = self.packages.${pkgs.stdenv.hostPlatform.system}.biopass;
      defaultText = lib.literalExpression "biopass.packages.\${system}.biopass";
      description = "The Biopass package.";
    };

    services = lib.mkOption {
      type = lib.types.listOf lib.types.str;
      default = [ "sudo" "login" "polkit-1" ];
      example = [ "sudo" "login" "polkit-1" "sddm" "greetd" "gdm-password" ];
      description = ''
        PAM services where a face or fingerprint match replaces the
        password. A failed or cancelled match falls through to the password
        prompt.
      '';
    };
  };

  config = lib.mkIf cfg.enable {
    environment.systemPackages = [ cfg.package ];
    services.fprintd.enable = lib.mkDefault true;

    # Biopass runs these by absolute /usr paths: the PAM module runs the
    # helper, the app runs biopass-keyring through pkexec, and the bundled
    # libcamera loads its IPA modules from /usr/lib/biopass.
    systemd.tmpfiles.rules = [
      "L+ /usr/bin/biopass-helper - - - - ${cfg.package}/bin/biopass-helper"
      "L+ /usr/bin/pkexec - - - - /run/wrappers/bin/pkexec"
      "L+ /usr/libexec/biopass - - - - ${cfg.package}/libexec/biopass"
      "L+ /usr/lib/biopass - - - - ${cfg.package}/lib/biopass"
      "L+ /usr/share/biopass - - - - ${cfg.package}/share/biopass"
      "d /var/lib/biopass 0755 root root -"
    ];

    # Before every password rule of the service, so the face or fingerprint
    # is tried first. "sufficient": a match ends the auth stack; anything
    # else (no match, error, cancelled) is ignored and the password rules
    # run. Keyring unlock (the authtok option) is not set up here: NixOS
    # runs the keyring modules before the password rule.
    security.pam.services = lib.genAttrs cfg.services (name: {
      # Biopass reads fingerprints through fprintd itself; pam_fprintd would
      # ask a second time.
      fprintAuth = lib.mkDefault false;
      rules.auth.biopass = {
        # unix-early exists only when a keyring module needs the password.
        order =
          let auth = pam.${name}.rules.auth;
          in (lib.min (auth.unix-early or auth.unix).order auth.unix.order) - 10;
        control = "sufficient";
        modulePath = "${cfg.package}/lib/security/libbiopass_pam.so";
      };
    });
  };
}
