# Repackages the release .deb. Biopass hardcodes /usr paths (the PAM module
# runs /usr/bin/biopass-helper, the app runs /usr/libexec/biopass/
# biopass-keyring through pkexec); the NixOS module (module.nix) links them
# into /usr.
{ lib
, stdenv
, fetchurl
, dpkg
, autoPatchelfHook
, wrapGAppsHook3
, gtk3
, webkitgtk_4_1
, libsoup_3
, glib
, cairo
, gdk-pixbuf
, pango
, libjpeg_turbo
, libglvnd
, tpm2-tss
, libxcrypt
, libxcrypt-legacy
, linux-pam
, libfprint

, libyaml
, gnutls
, libevent
, udev
, zlib
, openssl
}:

stdenv.mkDerivation rec {
  pname = "biopass";
  version = "1.5.1";

  src = fetchurl {
    url = "https://github.com/phucvinh57/biopass/releases/download/${version}/biopass_${version}_amd64.deb";
    hash = "sha256-WSUpkzhYNme/W+r/vX9cYyaaqhhg0oOnhGi1a0Irm+w=";
  };

  nativeBuildInputs = [ dpkg autoPatchelfHook wrapGAppsHook3 ];

  buildInputs = [
    gtk3
    webkitgtk_4_1
    libsoup_3
    glib
    cairo
    gdk-pixbuf
    pango
    libjpeg_turbo
    libglvnd
    tpm2-tss
    libxcrypt
    libxcrypt-legacy
    linux-pam
    libfprint
    libyaml
    gnutls
    libevent
    udev
    zlib
    openssl
    stdenv.cc.cc.lib
  ];

  # The bundled libcamera and onnxruntime live next to the binaries.
  appendRunpaths = [ "${placeholder "out"}/lib/biopass" ];

  unpackPhase = ''
    runHook preUnpack
    dpkg-deb -x $src .
    runHook postUnpack
  '';

  installPhase = ''
    runHook preInstall
    mkdir -p $out
    cp -r usr/bin usr/lib usr/libexec usr/share $out/
    cp -r lib/security $out/lib/
    rm -rf $out/share/pam-configs
    substituteInPlace $out/share/applications/biopass.desktop \
      --replace-quiet "Exec=biopass" "Exec=$out/bin/biopass"
    runHook postInstall
  '';

  # The helper and the PAM module are run as root by PAM: no GApps wrapper
  # (it would set environment variables in the login process).
  dontWrapGApps = true;
  postFixup = ''
    wrapGApp $out/bin/biopass
  '';

  meta = {
    description = "Multi-modal biometric login (face and fingerprint) for Linux";
    homepage = "https://github.com/phucvinh57/biopass";
    license = lib.licenses.mit;
    platforms = [ "x86_64-linux" ];
    sourceProvenance = [ lib.sourceTypes.binaryNativeCode ];
    mainProgram = "biopass";
  };
}
