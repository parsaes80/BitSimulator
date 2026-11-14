# Bit Simulator 

An interactive **digital circuit simulator** and **Verilog synthesiser**.

# Descrtiption 

**Bit Simulator** is designed to simulate any digital circuit, also it also has the ability to compile **Verilog** code in program and import the schematic of the synthesised circuit, meaning its a full **Verilog simulator**. 

This application is using **C++** and the **Qt framework** for the GUI layer. It is also using the **Yosys** open source synthesiser to compile the verilog code. For the layout of the components after the synthesis its using the **Graphviz** library. 

# Running Bit Simulator

Download the appropriate zip file for your OS. Unzip the zip file, and run the executable called **BitSimulator**. Graphviz comes with it; nothing else needs to be installed.

# Using Bit Simulator

You can add individual logic components to the project by selecting the component button in the toolbar and **middle clicking** on an empty spot in the Schematic Editor. You can connect components to each other by pressing **right click** near a port of a component and dragging it to the port of another component, if the connection is **valid** it will connect.

Controls in the Schematic Editor:

| Action | Control |
|---|---|
| Place the selected component | Middle click on empty space |
| Connect two ports with a wire | Right click on a port and drag to another port |
| Pan the schematic | Left click on empty space and drag |
| Select / move a component | Left click on it / left click and drag it |
| Deselect | Left click on empty space |
| Delete the selected component | Delete or Backspace |
| Zoom in / out | Mouse wheel |

The **Properties** panel on the right of the Schematic Editor shows the settings of the selected component (number of inputs, register type, source values, and so on) and lets you change them after the component has been placed. With nothing selected, it says so and shows the settings for the next component you place. A change that would remove a pin with a wire on it is refused until you remove the wire.

A **source** drives its output pins (one per bit, lowest bit on top) with a new value every N simulation ticks ("Every"). Its **Mode** decides the values:

| Mode | What it outputs |
|---|---|
| Counter | Start, Start + Step, Start + 2·Step, ... (or counting down). Wraps around at the number of bits: with 8 bits, 255 is followed by 0. |
| Gray code | A counter in Gray code: exactly one pin changes per step. |
| Walking one | A single 1 moving one pin per step: 1, 2, 4, 8, ... |
| Random | A random number over all bits every step; the same seed gives the same numbers on every run. |
| List | The numbers you enter (up to 65,536), then the list repeats. Decimal, `0x` hex and `0b` binary all work. |
| Waveform | A pattern of 0s and 1s for each pin, each repeating on its own. Type a pattern or click its steps. |
| Constant | Always the same number. |

Sources can be up to 64 bits wide. The panel shows the first few values, so you can see what a setting does. When Verilog is compiled, a 1-bit input becomes a waveform toggling every tick, and a wider input a counter that holds each value for 2 ticks.

Running the simulation:

- **Start** runs the circuit. The button then turns into **Pause**.
- While the simulation is running the circuit is read-only: you cannot place, wire, delete or edit components. You can still move them, pan and zoom.
- **Pause** freezes the simulation with its values still shown. Pressing **Start** again continues from where it stopped.
- If you change the circuit while it is paused, that run ends and the next **Start** begins again from the first tick.
- **Clear** stops the simulation and empties the Schematic Editor.

![Image](https://github.com/user-attachments/assets/598f7088-9dfa-43cf-8e53-41b766eee1a3)
You can also write **Verilog** code in the **HDL Editor** tab of the program. After that pressing the compile button will turn you Verilog code into a fully sythesized circuit, and you can simulate it. Compiling stops a running simulation. If the code compiles, the new circuit replaces the one in the Schematic Editor; if it has an error, the error is shown and the current circuit is kept. **THE MODULE MUST ALWAYS BE NAMED "top".**
![Image](https://github.com/user-attachments/assets/56b2722c-8d40-4972-800e-267351b7e81e)

Here is the a sample verolog code you can try, it creates a circuit that mutiplies two numbers: 
```
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

# Compiling the Project
The easiet way to compile the project is to install **Qt Creator** IDE and clone and open the project there. Then use the IDE to build the project. The IDE will install the Qt dependancies.  

Everything the app runs besides itself is in `third_party/`: `yosys_bundle` (yosys for Linux), `oss-cad-suite` (yosys for Windows) and `graphviz`; the build copies or builds them next to the executable.

Graphviz is a git submodule in `third_party/graphviz`. Clone with `git clone --recursive`, or run `git submodule update --init --depth 1` after cloning; if you forget, the first CMake run fetches it.

The first build also builds Graphviz's `dot` (a few minutes) and puts it next to the executable; later builds reuse it. What is built and why is described in `third_party/graphviz-support/Graphviz.cmake`:

- Graphviz 16.1.1 with one change (`ns-compact-arrays.patch`): the slowest part of dot's layout runs on a compact copy of its data. It makes exactly the same choices, so the layouts are the same, about twice as fast on large circuits.
- Only `dot`, linked statically, built with link-time optimization, and with GCC also profile-guided optimization (option `BITSIM_DOT_PGO`).
- Without Pango or Cairo: dot uses Graphviz's built-in font metrics, needs no font libraries, and lays out a circuit the same way on every OS.

Building Graphviz needs `bison` and `flex`. On Linux install them from your package manager (Arch: `pacman -S bison flex`, Debian/Ubuntu: `apt install bison flex`). On Windows nothing is needed: CMake fetches Graphviz's own copies (the small `graphviz-build-utilities` submodule inside `third_party/graphviz`).

# Light and Dark Mode

BitSimulator starts in your system's light or dark setting and follows it while it runs. The round button at the right end of the toolbar switches between the two modes; once you have used it, the app keeps your choice until it is closed.

# Changing the Colors

All colors are in one style sheet in `BitSimulator/BitSimulator.ui`, written in dark-mode colors. In Qt Designer, select `BitSimulatorClass` and edit its `styleSheet` property; in a text editor, search for `BitSimulator theme`.

- The table at the top pairs every dark color with its light-mode color. Light mode is the same style sheet with each color swapped for its partner, so a color is defined once per mode.
- To change a color, replace it in the rules (each color is always written the same way, so find and replace works) and in its table row. A color without a row looks the same in both modes, and the app prints a warning naming it.
- The canvas, the components and the toolbar icons are drawn in code. They read their colors from the `CircuitCanvas { qproperty-... }` rule at the bottom, e.g. `qproperty-wireHighColor` for a wire carrying 1 and `qproperty-dotColor` for the dot grid. A component's inside is tinted with its outline color; `qproperty-componentTint` sets how strongly (0 = no tint, 0.5 = halfway, 1 = the outline color).
- The arrow, check-mark, sun and moon images are in `BitSimulator/theme`.

# Running the Tests

The project has automated tests in the `tests` folder. They are optional: a normal build does not build them.

On Linux, from the project folder:

```
./run_tests.sh
```

This builds the app and the tests in `build/tests` and runs them all without opening any windows. `./run_tests.sh -R simulator` runs only the tests whose name contains "simulator", and `BITSIM_SHOW_GUI=1 ./run_tests.sh` shows the windows while the tests drive them.

On any system (or inside an IDE), turn on the CMake option `BITSIM_BUILD_TESTS` and build the target `check`:

```
cmake -S . -B build/tests -DBITSIM_BUILD_TESTS=ON
cmake --build build/tests --target check
```

In Qt Creator you can also switch `BITSIM_BUILD_TESTS` on under Projects > Build Settings > CMake and use the Test Results pane.

| Test | What it checks |
|---|---|
| `tst_itemprops` | Small helper functions (bits needed for a value, select lines of a multiplexer, ...) |
| `tst_simulator` | The simulation itself: every gate's truth table, multiplexers, flip-flops and latches, pause and continue |
| `tst_scene` | Components and their pins, the editing rules, and how the drawing is turned into what the simulator runs |
| `tst_panel` | The Properties panel, with simulated clicks and typing |
| `tst_lifecycle` | Start, Pause, Clear and Compile with the simulator running in its own thread |
| `tst_hdlcompiler` | Compiling Verilog (needs yosys and Graphviz; skipped if they are missing) |

# Third Party Software


This project includes binaries from:

- Yosys – Licensed under the ISC License.
  Copyright (C) 2012-2025 Claire Xenia Wolf.
  License in ./Licenses/Yosys-ISC.txt

- Graphviz – Licensed under the Eclipse Public License 2.0. Built from source (third_party/graphviz);
  BitSimulator's change to it is third_party/graphviz-support/ns-compact-arrays.patch.
  License in ./Licenses/Graphviz-EPL-2.0.txt

This project uses Qt under the terms of LGPLv3. No Qt code or binaries
are distributed; only dynamically linked. License in ./licenses/Qt-LGPL3.txt