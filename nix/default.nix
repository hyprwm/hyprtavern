{
  lib,
  stdenv,
  cmake,
  pkg-config,
  gtest,
  hyprwire,
  hyprutils,
  hyprtoolkit,
  hyprwire-protocols,
  pixman,
  aquamarine,
  hyprgraphics,
  libdrm,
  cairo,
  pango,
  libGL,
  libxkbcommon,
  glaze,
  version ? "git",
  shortRev ? "",
}:
stdenv.mkDerivation {
  pname = "hyprtavern";
  inherit version;

  src = ../.;

  nativeBuildInputs = [
    cmake
    pkg-config
  ];

  buildInputs = [
    gtest
    hyprwire
    hyprutils
    libdrm
    pixman
    glaze
    cairo
    pango
    hyprgraphics
    hyprtoolkit
    aquamarine
    libGL
    hyprwire-protocols
    libxkbcommon
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
