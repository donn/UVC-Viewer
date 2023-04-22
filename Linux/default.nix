{
    pkgs ? import <nixpkgs> {}
}:

with pkgs; clangStdenv.mkDerivation {
    name = "uvc-viewer";
    version = "alpha";

    src = ./.;
    
    buildInputs = [
        libjpeg
        libusb
        xorg.libX11
        xorg.libXrandr
        xorg.libXinerama
        xorg.libXcursor
        xorg.libXi
        libGL
        libGLU
        libpulseaudio
    ];

    nativeBuildInputs = [
        cmake
        pkg-config
    ];
}