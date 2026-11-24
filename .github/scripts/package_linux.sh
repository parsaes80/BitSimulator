#!/usr/bin/env bash
# Packages a Release build into <out dir>/<name>.zip: the app, Qt (linuxdeploy), the tools and the licenses
# Usage: package_linux.sh <build dir> <Qt dir> <out dir> [name]
# Needs linuxdeploy-x86_64.AppImage and linuxdeploy-plugin-qt-x86_64.AppImage on PATH, patchelf, zip, xvfb-run
set -euo pipefail

build="$(realpath "$1")"
qt="$(realpath "$2")"
out="$(realpath -m "$3")"
name="${4:-BitSimulator_Linux_x64}"
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
app="$build/BitSimulator"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

# linuxdeploy wants a desktop file and an icon; only the libraries and plugins it collects are packaged
cat > "$work/bitsimulator.desktop" <<'EOF'
[Desktop Entry]
Type=Application
Name=BitSimulator
Exec=BitSimulator
Icon=bitsimulator
Categories=Development;Electronics;
EOF
cp "$root/BitSimulator/icons/icon-256.png" "$work/bitsimulator.png"

# The plugin finds Qt through qmake; the AppImages unpack themselves, since runners have no FUSE
export QMAKE="$qt/bin/qmake"
export LD_LIBRARY_PATH="$qt/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export APPIMAGE_EXTRACT_AND_RUN=1
linuxdeploy-x86_64.AppImage --appdir "$work/AppDir" --executable "$app/BitSimulator" \
    --desktop-file "$work/bitsimulator.desktop" --icon-file "$work/bitsimulator.png" --plugin qt
unset LD_LIBRARY_PATH

# Flat layout as before (the app finds its tools in the working directory), with Qt in lib/ and plugins/
stage="$out/$name"
rm -rf "$stage"
mkdir -p "$stage"
cp "$work/AppDir/usr/bin/BitSimulator" "$stage/"
cp -r "$work/AppDir/usr/lib" "$stage/lib"
cp -r "$work/AppDir/usr/plugins" "$stage/plugins"
# The libraries already look in $ORIGIN and the plugins in $ORIGIN/../../lib; the app's own path pointed into usr/
patchelf --set-rpath '$ORIGIN/lib' "$stage/BitSimulator"
printf '[Paths]\nPrefix = .\nLibraries = lib\nPlugins = plugins\n' > "$stage/qt.conf"

cp -r "$app/yosys_bundle" "$app/licenses" "$stage/"
cp "$app/sv2v" "$app/dot" "$stage/"
# The bundles' git files are not part of the app
find "$stage" \( -name .gitignore -o -name .gitattributes \) -delete
chmod a+x "$stage/BitSimulator" "$stage/sv2v" "$stage/dot" "$stage"/yosys_bundle/bin/*

# Nothing may still come from the Qt install
if ldd "$stage/BitSimulator" "$stage"/plugins/*/*.so | grep -E "not found|$qt"; then
    echo "The packaged app still needs the libraries above from outside its folder" >&2
    exit 1
fi

# Smoke test: with the Qt install hidden, the app must start on X11 and keep running for 10 s (timeout gives 124)
mv "$qt" "$qt.hidden"
status=0
(cd "$stage" && xvfb-run -a timeout 10 ./BitSimulator) || status=$?
mv "$qt.hidden" "$qt"
if [[ $status -ne 124 ]]; then
    echo "The packaged app exited with status $status instead of running; a library or plugin is probably missing" >&2
    exit 1
fi
# A run can leave a saves folder behind
rm -rf "$stage/saves"
echo "Smoke test passed: the packaged app starts on its own"

rm -f "$out/$name.zip"
(cd "$out" && zip -qr9 "$name.zip" "$name")
echo "Packaged $out/$name.zip ($(du -h "$out/$name.zip" | cut -f1))"
