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
      # Built from the host's nixpkgs: the PAM module is loaded into the
      # host's sudo and login processes.
      default = pkgs.callPackage ./package.nix { };
      defaultText = lib.literalExpression "pkgs.callPackage ./nix/package.nix { }";
      description = "The Biopass package.";
    };

    services = lib.mkOption {
      type = lib.types.listOf lib.types.str;
      default = [ "sudo" "login" ];
      example = [ "sudo" "login" "sddm" "greetd" "gdm-password" ];
      description = ''
        PAM services where a face or fingerprint match replaces the
        password. A failed or cancelled match falls through to the password
        prompt.
      '';
    };
  };

  config = lib.mkIf cfg.enable {
    # A "sufficient" match ends the auth stack, so it would skip a required
    # second factor ordered after it.
    assertions = map (name: {
      assertion = !(pam.${name}.googleAuthenticator.enable or false)
        && !(pam.${name}.duoSecurity.enable or false);
      message = "programs.biopass.services: ${name} uses a second factor (Google Authenticator or Duo) that Biopass would skip. Remove ${name} from the list.";
    }) cfg.services;

    environment.systemPackages = [ cfg.package ];
    # The app saves the keyring password through pkexec.
    security.polkit.enable = lib.mkDefault true;

    # Biopass runs these by absolute /usr paths: the PAM module runs the
    # helper, the app runs biopass-keyring through pkexec, and the bundled
    # libcamera loads its IPA modules from /usr/lib/biopass.
    systemd.tmpfiles.rules = [
      "L+ /usr/bin/biopass-helper - - - - ${cfg.package}/bin/biopass-helper"
      "L /usr/bin/pkexec - - - - /run/wrappers/bin/pkexec"
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
