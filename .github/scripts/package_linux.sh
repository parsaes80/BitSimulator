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

# Wayland and X11 both: Qt picks Wayland on a Wayland desktop (XDG_SESSION_TYPE) and X11 elsewhere
export EXTRA_PLATFORM_PLUGINS="libqwayland.so"
# The Wayland plugin's own plugins (window shell, title bars, EGL buffers), which the Qt plugin predates; linuxdeploy adds their libraries
wayland_plugins=(
    wayland-shell-integration/libxdg-shell.so
    wayland-decoration-client/libadwaita.so
    wayland-decoration-client/libbradient.so
    wayland-graphics-integration-client/libqt-plugin-wayland-egl.so
)
deps_only=()
for plugin in "${wayland_plugins[@]}"; do
    mkdir -p "$work/AppDir/usr/plugins/$(dirname "$plugin")"
    cp "$qt/plugins/$plugin" "$work/AppDir/usr/plugins/$plugin"
    deps_only+=(--deploy-deps-only "$work/AppDir/usr/plugins/$plugin")
done

# The plugin finds Qt through qmake; the AppImages unpack themselves, since runners have no FUSE
export QMAKE="$qt/bin/qmake"
export LD_LIBRARY_PATH="$qt/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export APPIMAGE_EXTRACT_AND_RUN=1
linuxdeploy-x86_64.AppImage --appdir "$work/AppDir" --executable "$app/BitSimulator" \
    --desktop-file "$work/bitsimulator.desktop" --icon-file "$work/bitsimulator.png" "${deps_only[@]}" --plugin qt
unset LD_LIBRARY_PATH

# Flat layout as before (the app finds its tools in the working directory), with Qt in lib/ and plugins/
stage="$out/$name"
rm -rf "$stage"
mkdir -p "$stage"
cp "$work/AppDir/usr/bin/BitSimulator" "$stage/"
cp -r "$work/AppDir/usr/lib" "$stage/lib"
cp -r "$work/AppDir/usr/plugins" "$stage/plugins"
# The libraries already look in $ORIGIN; the app's own path pointed into usr/, and every plugin sits two folders below lib/
patchelf --set-rpath '$ORIGIN/lib' "$stage/BitSimulator"
find "$stage/plugins" -name '*.so' -exec patchelf --set-rpath '$ORIGIN/../../lib' {} +
for plugin in platforms/libqxcb.so platforms/libqwayland.so "${wayland_plugins[@]}"; do
    [[ -f "$stage/plugins/$plugin" ]] || { echo "The package has no plugins/$plugin" >&2; exit 1; }
done
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

# Smoke tests with the Qt install hidden: the app must keep running for 10 s (timeout then gives 124)
smoke_test() {
    local label="$1" status=0
    shift
    (cd "$stage" && "$@" timeout 10 ./BitSimulator) || status=$?
    if [[ $status -ne 124 ]]; then
        echo "On $label the packaged app exited with status $status instead of running; a library or plugin is probably missing" >&2
        exit 1
    fi
    echo "Smoke test passed on $label"
}
mv "$qt" "$qt.hidden"
trap 'mv "$qt.hidden" "$qt" 2>/dev/null; rm -rf "$work"' EXIT

smoke_test X11 xvfb-run -a env -u WAYLAND_DISPLAY QT_QPA_PLATFORM=xcb

# Wayland under a headless Weston; QT_QPA_PLATFORM=wayland fails instead of falling back to X11
export XDG_RUNTIME_DIR="$work/runtime"
mkdir -m 700 "$XDG_RUNTIME_DIR"
weston --backend=headless --socket=bitsim-smoke --idle-time=0 &
weston_pid=$!
for _ in {1..100}; do [[ -S "$XDG_RUNTIME_DIR/bitsim-smoke" ]] && break; sleep 0.1; done
[[ -S "$XDG_RUNTIME_DIR/bitsim-smoke" ]] || { echo "Weston did not start, so the Wayland smoke test cannot run" >&2; exit 1; }
smoke_test Wayland env -u DISPLAY WAYLAND_DISPLAY=bitsim-smoke QT_QPA_PLATFORM=wayland
kill "$weston_pid" 2>/dev/null || true

mv "$qt.hidden" "$qt"
# A run can leave a saves folder behind
rm -rf "$stage/saves"

rm -f "$out/$name.zip"
(cd "$out" && zip -qr9 "$name.zip" "$name")
echo "Packaged $out/$name.zip ($(du -h "$out/$name.zip" | cut -f1))"
