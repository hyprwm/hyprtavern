{
  lib,
  stdenv,
  cmake,
  pkg-config,
  hyprwire,
  hyprutils,
  version ? "git",
  shortRev ? "",
}:
stdenv.mkDerivation {
  pname = "hyprlock";
  inherit version;

  src = ../.;

  nativeBuildInputs = [
    cmake
    pkg-config
  ];

  buildInputs = [
    hyprwire
    hyprutils
  ];

  cmakeFlags = lib.mapAttrsToList lib.cmakeFeature {
    HYPRTAVERN_COMMIT = shortRev;
    HYPRTAVERN_VERSION_COMMIT = "";
  };

  meta = {
    homepage = "https://github.com/hyprwm/hyprtavern";
    description = "A modern, simple and consistent session bus for IPC discovery.";
    license = lib.licenses.bsd3;
    platforms = lib.platforms.linux;
    mainProgram = "hyprtavern";
  };
}
