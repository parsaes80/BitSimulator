"""Installs Qt for Windows (MSVC) straight from download.qt.io, for the release workflow.

Usage: python install_qt_windows.py <version> <compiler> <output folder> <module>...
Example: python install_qt_windows.py 6.12.0 msvc2022_64 C:/Qt qtbase
Qt then sits in <output folder>/<version>/<compiler>.

aqtinstall 3.3.0 can't read the per-compiler repository layout Qt uses since 6.12.0; this does
only what the workflow needs: read the package index, download the named modules' archives,
check their SHA-1 and unpack them with 7-Zip. It uses the standard library only.
"""

import hashlib
import subprocess
import sys
import tempfile
import urllib.request
import xml.etree.ElementTree as ET
from pathlib import Path

REPOSITORY = "https://download.qt.io/online/qtsdkrepository/windows_x86/desktop"


def fetch(url: str) -> bytes:
    for attempt in range(3):
        try:
            with urllib.request.urlopen(url, timeout=120) as response:
                return response.read()
        except OSError as error:
            if attempt == 2:
                raise SystemExit(f"Could not download {url}: {error}")
            print(f"Retrying {url} ({error})", flush=True)
    raise AssertionError


def main() -> None:
    if len(sys.argv) < 5:
        raise SystemExit(__doc__)
    version, compiler, output, *modules = sys.argv[1:]
    digits = version.replace(".", "")
    folder = f"{REPOSITORY}/qt6_{digits}/qt6_{digits}_{compiler}"
    package_name = f"qt.qt6.{digits}.win64_{compiler}"

    index = ET.fromstring(fetch(f"{folder}/Updates.xml"))
    package = next((p for p in index.findall("PackageUpdate") if p.findtext("Name") == package_name), None)
    if package is None:
        raise SystemExit(f"{package_name} is not in {folder}/Updates.xml")
    package_version = package.findtext("Version")
    archives = [a.strip() for a in package.findtext("DownloadableArchives", "").split(",") if a.strip()]

    output_path = Path(output) / version / compiler
    output_path.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory() as download_dir:
        for module in modules:
            # Archive names are "<module>-Windows-...7z"; the dash keeps qtquick3d from matching qtquick3dphysics
            matches = [a for a in archives if a.startswith(module + "-")]
            if len(matches) != 1:
                raise SystemExit(f"Expected one archive for {module}, found {matches} in {archives}")
            url = f"{folder}/{package_name}/{package_version}{matches[0]}"
            print(f"Downloading {url}", flush=True)
            data = fetch(url)
            expected = fetch(url + ".sha1").decode().split()[0].lower()
            actual = hashlib.sha1(data).hexdigest()
            if actual != expected:
                raise SystemExit(f"SHA-1 mismatch for {matches[0]}: expected {expected}, got {actual}")
            archive = Path(download_dir) / matches[0]
            archive.write_bytes(data)
            subprocess.run(["7z", "x", "-y", f"-o{output_path}", str(archive)], check=True, stdout=subprocess.DEVNULL)

    if not (output_path / "bin" / "Qt6Core.dll").exists():
        raise SystemExit(f"Qt6Core.dll is not in {output_path / 'bin'} after unpacking")
    print(f"Qt {version} {compiler} ({', '.join(modules)}) is in {output_path}")


if __name__ == "__main__":
    main()
