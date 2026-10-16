{
  description = "Common Desktop Environment (Motif) window chrome for Hyprland";

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
            pkgs = import inputs.nixpkgs { inherit system; };
            lib = inputs.nixpkgs.lib;
          }
        );
    in
    {
      devShells = forEachSystem (
        { pkgs, ... }:
        {
          default = pkgs.mkShell.override { stdenv = pkgs.gcc14Stdenv; } {
            name = "hypr-CDE-borders";

            packages = [
              pkgs.hyprland
              pkgs.cmake
              pkgs.pkg-config
              pkgs.cairo
              # For the pixel-diff check in AGENTS.md `## Verification`.
              pkgs.grim
              pkgs.imagemagick
            ];

            inputsFrom = [ pkgs.hyprland ];
          };
        }
      );

      packages = forEachSystem (
        { pkgs, lib, ... }:
        rec {
          # A plain attrset rather than the `finalAttrs: { ... }` form: the latter is a
          # function, so `nix build .#default` rejects it. mkHyprlandPlugin accepts either.
          cde = pkgs.hyprlandPlugins.mkHyprlandPlugin {
            pluginName = "cde";
            version = "0.1";

            src = ./.;

            # cmake, not make: CMakeLists.txt has an install(TARGETS), which is what makes
            # the nix install phase put the .so in $out/lib. hyprpm runs `make` instead
            # (hyprpm.toml) and reads cde.so out of the source tree. Same split as
            # hyprbars, which ships both.
            nativeBuildInputs = [ pkgs.cmake ];

            # The plugin's symbols are resolved by the compositor at dlopen time; stripping
            # a backtrace out of a crash inside it is not worth the bytes.
            dontStrip = true;

            meta = {
              homepage = "https://github.com/wyspr/hypr-CDE-borders";
              description = "Common Desktop Environment (Motif) window chrome for Hyprland";
              license = lib.licenses.gpl3;
              inherit (pkgs.hyprland.meta) platforms;
              maintainers = with lib.maintainers; [ wyspr ];
            };
          };

          default = cde;
        }
      );
    };
}
