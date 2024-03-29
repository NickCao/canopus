{
  inputs = {
    nixpkgs.url = "github:NickCao/nixpkgs";
    flake-utils.url = "github:numtide/flake-utils";
  };
  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem
      (system:
        let pkgs = import nixpkgs { inherit system; }; in
        rec {
          packages.default = pkgs.stdenv.mkDerivation {
            name = "canopus";
            src = self;
            nativeBuildInputs = with pkgs;[
              meson
              ninja
              pkg-config
            ];
            buildInputs = with pkgs;[
              nixVersions.stable
              boost
              libseccomp
            ];
          };
          devShell = pkgs.mkShell {
            inputsFrom = [ packages.default ];
          };
        });
}
