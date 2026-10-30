{
  description = "Common Desktop Environment (Motif) window chrome for Hyprland";

  inputs.nixpkgs.url = "https://channels.nixos.org/nixpkgs-unstable/nixexprs.tar.zst";
  inputs.hyprland.url = "github:hyprwm/Hyprland";
  inputs.hyprland.inputs.nixpkgs.follows = "nixpkgs";

  outputs =
    inputs:
    let
      inherit (inputs.nixpkgs) lib;
      system = "x86_64-linux";
      pkgs = inputs.nixpkgs.legacyPackages.${system};
      hyprland = inputs.hyprland.packages.${system}.default;
    in
    {
      devShells.${system}.default = pkgs.mkShell.override { stdenv = pkgs.gcc14Stdenv; } {
        name = "hypr-CDE-borders";

        inputsFrom = [ hyprland ];

        packages = [
          hyprland
          pkgs.cmake
          pkgs.pkg-config
          pkgs.cairo
        ];
      };

      packages.${system} = {
        default = inputs.self.packages.${system}.cde;

        cde = lib.makeOverridable (
          { hyprland }:
          pkgs.hyprlandPlugins.mkHyprlandPlugin {
            pluginName = "cde";
            version = "0.1";

            inherit hyprland;

            src = ./.;

            # cmake, not make: CMakeLists.txt has an install(TARGETS), which is what makes
            # the nix install phase put the .so in $out/lib. hyprpm runs `make` instead
            # (hyprpm.toml) and reads cde.so out of the source tree. Same split as
            # hyprbars, which ships both.
            nativeBuildInputs = [ pkgs.cmake ];

            dontStrip = true;

            meta = {
              homepage = "https://github.com/wyspr/hypr-CDE-borders";
              description = "Common Desktop Environment (Motif) window chrome for Hyprland";
              license = lib.licenses.gpl3;
              maintainers = with lib.maintainers; [ wyspr ];
            };
          }
        ) { inherit hyprland; };
      };
    };
}
