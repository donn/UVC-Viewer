{
    pkgs ? import <nixpkgs> {}
}:

with pkgs; let uvc-viewer = import ./. {}; in (mkShell.override { stdenv = clangStdenv; }) {
    buildInputs = [
        lldb
        python3Full
        zsh
    ] ++ uvc-viewer.buildInputs ++ uvc-viewer.nativeBuildInputs;
}