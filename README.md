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
| Delete the selected component | Delete or Backspace |
| Zoom in / out | Mouse wheel |
![Image](https://github.com/user-attachments/assets/598f7088-9dfa-43cf-8e53-41b766eee1a3)
You can also write **Verilog** code in the **HDL Editor** tab of the program. After that pressing the compile button will turn you Verilog code into a fully sythesized circuit, and you can simulate it. **THE MODULE MUST ALWAYS BE NAMED "top".**
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

# Third Party Software


This project includes binaries from:

- Yosys – Licensed under the ISC License.
  Copyright (C) 2012-2025 Claire Xenia Wolf.
  License in ./Licenses/Yosys-ISC.txt

- Graphviz – Licensed under the Common Public License 1.0.
  License in ./Licenses/Graphviz-CPL.txt

This project uses Qt under the terms of LGPLv3. No Qt code or binaries
are distributed; only dynamically linked. License in ./licenses/Qt-LGPL3.txt