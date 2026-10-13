{
  inputs.nixpkgs.url = "https://channels.nixos.org/nixpkgs-unstable/nixexprs.tar.zst";

  outputs =
    inputs:
    let
      supportedSystems = [
        "x86_64-linux"
        "aarch64-linux"
        "x86_64-darwin"
        "aarch64-darwin"
      ];
      forEachSystem =
        f:
        inputs.nixpkgs.lib.genAttrs supportedSystems (
          system:
          f {
            pkgs = import inputs.nixpkgs {
              inherit system;
              # overlays = [ inputs.self.overlays.default ];
            };
            lib = inputs.nixpkgs.lib;
          }
        );
    in
    {
      devShells = forEachSystem (
        { pkgs, ... }:
        {
          default = pkgs.mkShell.override { stdenv = pkgs.gcc14Stdenv; } {
            name = "hyprland-plugins";
            buildInputs = [ pkgs.hyprland ];
            inputsFrom = [ pkgs.hyprland ];
          };
        }
      );

      packages = forEachSystem (
        { pkgs, lib, ... }: {
          default = pkgs.hyprlandPlugins.mkHyprlandPlugin (finalAttrs: {
            pluginName = "hycde";
            version = "0.0-alpha";

            src = ./.;

            nativeBuildInputs = [ pkgs.cmake ];

            meta = {
              homepage = "https://github.com/outfoxxed/hy3";
              description = "Hyprland plugin for CDE like window chrome";
              license = lib.licenses.gpl3;
              inherit (pkgs.hyprland.meta) platforms;
              maintainers = with lib.maintainers; [
                wyspr
              ];
            };
          });
        }
      );
    };
}
