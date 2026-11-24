<p align="center"><img src="BitSimulator/icons/logo.png" alt="BitSimulator" width="480"></p>

<p align="center">
  <b>Draw digital circuits, or write them in an HDL, and watch them run gate by gate.</b><br>
  <a href="https://parsaes80.github.io/BitSimulator/">Try it in your browser</a> ·
  <a href="https://github.com/parsaes80/BitSimulator/releases">Download for Windows and Linux</a>
</p>

# Bit Simulator

Bit Simulator is a free, open source **digital circuit simulator** and **HDL synthesiser**. Build a circuit from gates and flip-flops, or write it in **Verilog**, **SystemVerilog** or **VHDL** and let Bit Simulator turn it into a real gate-level schematic. Then run it, watch every wire change, and inspect the signals in a waveform viewer, as in ModelSim or Questa.

![Image](https://github.com/user-attachments/assets/598f7088-9dfa-43cf-8e53-41b766eee1a3)

## Features

- **Schematic editor:** AND, OR, NOT, NAND, NOR, XOR and XNOR gates, multiplexers, flip-flops and latches (D, T, JK, SR), sources up to 64 bits wide, and displays.
- **HDL to circuit:** compile Verilog, SystemVerilog or VHDL (VHDL-2008), from the built-in editor or a whole project folder. The top module is found automatically, and a design can mix Verilog and VHDL.
- **Hierarchy:** every submodule becomes a block you can open with a double-click, down to the gates, with live values inside while the circuit runs.
- **Simulation:** start, pause, continue and reset. Wires light up as they carry a 1, and flip-flops behave as in an HDL simulator: they take their new value at the clock edge, with synchronous or asynchronous resets and enables. Designs as large as a full RISC-V CPU compile and run.
- **Waveforms:** a wave viewer for every input, output and block port, with cursors, zoom, radixes and bus splitting. Export to VCD for GTKWave, Surfer or ModelSim.
- **Stimulus:** sources can count, step through Gray code or a walking one, give random numbers, play a list of values or a drawn waveform, or hold a constant.
- **Editing:** undo and redo, copy, cut and paste, a search bar to find any input or output, and a Properties panel to change any component after placing it.
- **Saving:** circuits are saved as `.bitsim` files, compiled designs included, so they open without the HDL files.
- **Light and dark mode**, following your system's setting.
- **Runs anywhere:** Windows and Linux apps, and a browser version that needs no install.

# Getting Started

**In the browser:** open [the web version](https://parsaes80.github.io/BitSimulator/). It does everything the desktop app does, except:
- it compiles Verilog only, not VHDL or SystemVerilog packages, and has no **Compile Dir** tab;
- saves are kept in the browser: **Open...** imports a `.bitsim` file, and right-click a save › **Download** to get it back;
- the first compile downloads the synthesiser (about 50 MB).

**On your computer:** download the zip for your system from the [releases page](https://github.com/parsaes80/BitSimulator/releases), unzip it, and run **BitSimulator**. Everything it needs comes with it.

# Using Bit Simulator

## Drawing a circuit

Pick a component in the toolbar and **middle click** on the canvas to place it. To wire two components, **right click** on a port and drag to another port; the wire connects if the connection is valid.

| Action | Control |
|---|---|
| Place the selected component | Middle click on empty space |
| Connect two ports | Right click on a port and drag to another port |
| Pan | Left click on empty space and drag |
| Select / move a component | Left click it / drag it |
| Delete (with its wires) | Delete or Backspace |
| Copy / cut / paste | Ctrl+C / Ctrl+X / Ctrl+V (pastes at the mouse pointer) |
| Undo / redo | Ctrl+Z / Ctrl+Y |
| Zoom | Mouse wheel |
| Find an input or output | Type in the search bar (top right); regular expressions work |

The **Properties** panel shows the settings of the selected component, such as the number of inputs, the flip-flop type or a source's values, and lets you change them. With nothing selected, it sets up the next component you place. Drag the panel by its title bar to move it; double-click the title bar to minimize it.

## Sources and displays

A **source** drives its pins with a new value every N ticks. Its **Mode** decides the values:

| Mode | What it outputs |
|---|---|
| Counter | Start, Start + Step, Start + 2·Step, ..., wrapping around at its width |
| Gray code | A counter in which exactly one pin changes per step |
| Walking one | A single 1 moving one pin per step: 1, 2, 4, 8, ... |
| Random | Random numbers; the same seed gives the same numbers every run |
| List | The numbers you enter (decimal, `0x` hex or `0b` binary) |
| Waveform | A pattern of 0s and 1s for each pin; type it or click its steps |
| Constant | Always the same number |

A list or waveform repeats; uncheck **Repeat** to hold its last value instead. For example, the waveform `1 1 0` without Repeat is a reset pulse at the start of a run.

A **display** shows the value on its pins. Sources and displays get names ("Source 1", "Display 2"), which you can change in the Properties panel; the waveforms use them.

## Running the simulation

- **Start** runs the circuit and becomes **Pause**. While it runs, you can move, pan and zoom, but not change the circuit.
- **Pause** freezes the circuit with its values shown; **Start** continues from there.
- **Reset** sets every value back to 0 and clears the waveforms, keeping the circuit.
- **Clear** empties the canvas.

Flip-flops start at 0, so a circuit that needs a reset (a CPU, say) waits for one: make its reset source the waveform `1 1 0` with Repeat unchecked.

## Compiling HDL

Write or paste your code in the **Compile file** tab and press **Compile**. The design appears on the canvas, ready to run; if the code has an error, the error is shown and the current circuit stays. Verilog, SystemVerilog and VHDL are all recognized automatically.

To compile a whole project, open the **Compile Dir** tab and choose a folder. Every `.v`, `.sv`, `.vhd` and `.vhdl` file in it is listed; uncheck the ones to leave out, such as testbenches, and press **Compile directory**.

The top module's inputs become sources and its outputs displays, named after the ports. A 1-bit input toggles every tick (a clock), a wider input counts, and an input named like a reset (`rst`, `reset`, `rst_n`, ...) stays 0.

Try this multiplier:

```verilog
module top (
    input  wire       clk,
    input  wire [7:0] a,
    input  wire [7:0] b,
    output reg  [8:0] result
);

always @(posedge clk) begin
    result <= a * b;
end

endmodule
```

![Image](https://github.com/user-attachments/assets/56b2722c-8d40-4972-800e-267351b7e81e)

## Blocks

Each submodule is drawn as a **block** showing its instance and module name, e.g. `alu (ALU)`.

- **Double-click** a block to open it. Blocks inside it open the same way.
- The path at the top left, e.g. `RISCV_FPGA › core (RISCV) › alu (ALU)`, shows where you are. Click an entry to go back to it, or press **Esc** to go up one level.
- While the circuit runs, a block's inside shows its live values.
- Only the top level can be edited; to change a block, change the code and compile again.

## Waveforms

The **Waveforms** tab shows every source and display over time. Check a block's ports in the tree on the left to add them; they show the whole run, even when added later.

| Action | Control |
|---|---|
| Move in time | Mouse wheel, or drag |
| Scroll the rows | Shift+mouse wheel |
| Zoom around the pointer | Ctrl+mouse wheel, or Zoom in / Zoom out |
| See the whole run | Fit |
| Place the cursor | Click |
| Measure between two points | Shift+click for a second cursor |
| Radix, expand, move, remove | Right-click a row |
| Split a bus into bits or words | Right-click a row › Split into |

The tab has its own **Start / Pause** and **Reset** buttons, and **Follow** keeps the newest tick in view. **Export VCD...** saves the run for GTKWave, Surfer, ModelSim or any other wave viewer.

## Saving and opening

The **Files** tab has **New**, **Open...**, **Save** and **Save As...** (Ctrl+N, Ctrl+O, Ctrl+S, Ctrl+Shift+S) and lists your saved circuits; double-click one to open it, or right-click it to delete it. The window title shows a `*` while there are unsaved changes, and Bit Simulator asks before discarding them.

# Building from Source

Clone with the Graphviz submodule:

```
git clone --recursive https://github.com/parsaes80/BitSimulator.git
```

## Desktop app

You need Qt 6 (Bit Simulator uses 6.12) with Qt Widgets, CMake 3.16 or newer, and a C++ compiler. On Linux, also install `bison` and `flex` (Arch: `pacman -S bison flex`; Debian/Ubuntu: `apt install bison flex`).

The easiest way is to open the folder in **Qt Creator**, configure the project to use the release package, and build using the IDE itself. From a terminal:

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=~/Qt/6.12.0/gcc_64
cmake --build build
```

The app is `build/BitSimulator/BitSimulator`, with its tools next to it. The first build also builds Graphviz, which takes a few minutes.

## Browser version

You need:
- Qt 6.12.0's **WebAssembly (single-threaded)** component, plus the desktop Qt of the same version;
- **Emscripten 5.0.5**, installed with [emsdk](https://github.com/emscripten-core/emsdk). Qt 6.12.0 refuses any other version, so a Linux distribution's `emscripten` package usually won't work;
- `npm`, `bison` and `flex`.

The build finds the emsdk folder by itself. It uses the `EMSDK` environment variable if it is set, otherwise `-DBITSIM_EMSDK=<folder>`, which defaults to `/usr/local/bin/emsdk`. To install Emscripten:

```
git clone https://github.com/emscripten-core/emsdk.git <folder>
<folder>/emsdk install 5.0.5
<folder>/emsdk activate 5.0.5
```

Then build:

```
~/Qt/6.12.0/wasm_singlethread/bin/qt-cmake -S . -B build/wasm -G Ninja -DCMAKE_BUILD_TYPE=Release -DBITSIM_EMSDK=<folder>
cmake --build build/wasm
```

`build/wasm/BitSimulator` is the whole site. To try it, serve it with Emscripten's server, which always sends the newest files:

```
<folder>/upstream/emscripten/emrun --no-browser --no-emrun-detect --serve-after-close --port 8765 build/wasm/BitSimulator/index.html
```

and open `http://localhost:8765/index.html`. Every push to `main` also builds the browser version on GitHub and publishes it to GitHub Pages (`.github/workflows/pages.yml`).

## Tests

On Linux, `./run_tests.sh` builds and runs all the tests. On any system, configure with `-DBITSIM_BUILD_TESTS=ON` and build the `check` target.

## Releases

Pushing a version tag (`git push origin v3.2`) builds the Windows and Linux zips on GitHub, runs the tests, checks that each zip starts on its own, and attaches both to a draft release for the tag: add the notes and publish it on the releases page (`.github/workflows/release.yml`). The tag must match `project(BitSimulator VERSION ...)` in `CMakeLists.txt`. **Run workflow** on the Actions tab makes the same zips as a test, without a release.

# Third Party Software


This project includes binaries from:

- Yosys – Licensed under the ISC License.
  Copyright (C) 2012-2025 Claire Xenia Wolf.
  License in ./licenses/Yosys-ISC.txt

- Noto Sans (browser version only) – the interface font, licensed under the SIL Open Font License 1.1.
  BitSimulator/fonts/NotoSans-Regular.ttf and NotoSans-Bold.ttf. License in ./licenses/NotoSans-OFL-1.1.txt

- YoWASP Yosys (browser version only) – Yosys compiled to WebAssembly by the YoWASP project, licensed under the ISC License.
  Fetched from npm (@yowasp/yosys) when the browser version is built. License in ./licenses/Yosys-ISC.txt

- sv2v – Licensed under the BSD 3-Clause License. Converts SystemVerilog (packages, imports, enums, ...) to the Verilog yosys reads.
  Binaries in third_party/sv2v. License in ./licenses/sv2v-BSD-3.txt

- Graphviz – Licensed under the Eclipse Public License 2.0. Built from source (third_party/graphviz);
  BitSimulator's change to it is third_party/graphviz-support/ns-compact-arrays.patch.
  License in ./licenses/Graphviz-EPL-2.0.txt

- GHDL – Licensed under the GNU General Public License 2.0 or later. Turns VHDL into Verilog.
  Linux: the unmodified ghdl binary and its VHDL libraries are in third_party/yosys_bundle (bin/ghdl, lib/ghdl).
  Its source: https://github.com/ghdl/ghdl, version 6.0.0-dev, commit ed0da3cfa.
  Windows: the unmodified ghdl.exe and its VHDL libraries are in third_party/ghdl (bin/ghdl.exe, lib/ghdl),
  taken from GHDL's own Windows build ghdl-mcode-6.0.0-ucrt64.zip, because the OSS CAD Suite for Windows
  does not include ghdl. Its source: https://github.com/ghdl/ghdl/releases/tag/v6.0.0, version 6.0.0, commit e589c698c.
  License in ./licenses/GHDL-GPL-2.0-or-later.txt. The IEEE VHDL-2008 library sources in
  lib/ghdl/src/ieee2008 are under the Apache License 2.0 (the LICENSE file next to them).

- Windows runtime libraries – the DLLs next to yosys.exe and yosys-abc.exe (third_party/oss-cad-suite/bin)
  come unmodified from the OSS CAD Suite release 2025-11-29 (https://github.com/YosysHQ/oss-cad-suite-build),
  which builds them with Fedora's MinGW packages. The DLLs next to ghdl.exe (third_party/ghdl/bin) come
  unmodified from GHDL's v6.0.0 Windows build, which uses MSYS2's UCRT64 packages.
  - GNU Readline 8.2 (libreadline8.dll, needed by yosys.exe) – GNU General Public License 3.0 or later.
    License in ./licenses/GPL-3.0.txt. Source: https://ftp.gnu.org/gnu/readline/ and
    https://src.fedoraproject.org/rpms/mingw-readline
  - GNU termcap 1.3.1 (libtermcap-0.dll, needed by libreadline8.dll) – GNU General Public License 2.0 or later.
    License in ./licenses/GPL-2.0.txt. Source: https://ftp.gnu.org/gnu/termcap/ and
    https://src.fedoraproject.org/rpms/mingw-termcap
  - GCC runtime: libgcc_s_seh-1.dll and libstdc++-6.dll in oss-cad-suite/bin (GCC 13, Fedora MinGW), and
    libgcc_s_seh-1.dll in ghdl/bin (GCC 15.2.0, MSYS2) – GNU General Public License 3.0 or later with the
    GCC Runtime Library Exception 3.1. Licenses in ./licenses/GPL-3.0.txt and
    ./licenses/GCC-Runtime-Library-Exception-3.1.txt. Source: https://gcc.gnu.org/,
    https://src.fedoraproject.org/rpms/mingw-gcc and https://github.com/msys2/MINGW-packages
  - Under permissive licenses, with their notices in their sources: MinGW-w64 winpthreads
    (libwinpthread-1.dll, MIT and BSD style), libffi 3.4 (libffi-8.dll, MIT), Tcl 8.6.12 (tcl86.dll,
    Tcl/BSD style) and zlib 1.2.13 and 1.3.2 (zlib1.dll, zlib License).

This project uses Qt under the terms of the LGPLv3, unmodified. The source contains no Qt code. The
desktop app links Qt dynamically, and a binary release that includes the Qt libraries ships them
unmodified. The browser version links Qt statically into the page; since BitSimulator's source is
public, it can be rebuilt with a modified Qt. Qt's source is available from
https://download.qt.io/archive/qt/. License in ./licenses/Qt-LGPL3.txt, together with
./licenses/GPL-3.0.txt, which the LGPLv3 builds on. The browser version also serves Qt's page
loader, qtloader.js, which Qt licenses under the GPLv3 only (./licenses/GPL-3.0.txt).

The build copies ./licenses next to the executable (and next to the page in the browser version), and
the **?** button at the right end of the toolbar lists every part with its license.