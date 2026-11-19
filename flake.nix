{
  description = "Biopass: face and fingerprint login for Linux";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-26.05";

  outputs = { self, nixpkgs }:
    let
      system = "x86_64-linux";
      pkgs = nixpkgs.legacyPackages.${system};
    in
    {
      packages.${system} = rec {
        biopass = pkgs.callPackage ./nix/package.nix { };
        default = biopass;
      };

      nixosModules = rec {
        biopass = import ./nix/module.nix self;
        default = biopass;
      };

      checks.${system} = {
        inherit (self.packages.${system}) biopass;
        module = (nixpkgs.lib.nixosSystem {
          inherit system;
          modules = [
            self.nixosModules.biopass
            {
              boot.loader.grub.enable = false;
              fileSystems."/" = { device = "/dev/null"; fsType = "ext4"; };
              system.stateVersion = "26.05";
              programs.biopass.enable = true;
            }
          ];
        }).config.system.build.toplevel;
      };
    };
}
