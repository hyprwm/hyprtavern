{
  lib,
  inputs,
  self,
}:
let
  mkDate =
    longDate:
    (lib.concatStringsSep "-" [
      (builtins.substring 0 4 longDate)
      (builtins.substring 4 2 longDate)
      (builtins.substring 6 2 longDate)
    ]);

  version = lib.removeSuffix "\n" (builtins.readFile ../VERSION);
in
{
  default = inputs.self.overlays.hyprtavern;

  hyprtavern-with-deps = lib.composeManyExtensions [
    inputs.hyprwire.overlays.hyprwire-with-deps
    inputs.hyprtoolkit.overlays.hyprtoolkit-with-deps
    inputs.hyprwire-protocols.overlays.default
    self.overlays.hyprtavern
  ];

  hyprtavern = final: prev: {
    hyprtavern = prev.callPackage ./default.nix {
      stdenv = prev.gcc15Stdenv;
      version =
        version
        + "+date="
        + (mkDate (inputs.self.lastModifiedDate or "19700101"))
        + "_"
        + (inputs.self.shortRev or "dirty");
      shortRev = self.sourceInfo.shortRev or "dirty";
    };
  };
}
