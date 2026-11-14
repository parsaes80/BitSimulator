# Bit Simulator 

An interactive **digital circuit simulator** and **Verilog synthesiser**.

# Descrtiption 

**Bit Simulator** is designed to simulate any digital circuit, also it also has the ability to compile **Verilog** code in program and import the schematic of the synthesised circuit, meaning its a full **Verilog simulator**. 

This application is using **C++** and the **Qt framework** for the GUI layer. It is also using the **Yosys** open source synthesiser to compile the verilog code. For the layout of the components after the synthesis its using the **Graphviz** library. 

# Running Bit Simulator

Download the appropriate zip file for your OS. Unzip the zip file, and run the executable called **BitSimulator**. On linux make sure you have the **graphviz** package installed.

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

- Graphviz – Licensed under the Common Public License 1.0.
  License in ./Licenses/Graphviz-CPL.txt

This project uses Qt under the terms of LGPLv3. No Qt code or binaries
are distributed; only dynamically linked. License in ./licenses/Qt-LGPL3.txt