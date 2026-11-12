call oss-cad-suite\environment.bat
yosys -p "read_verilog -sv code.v; synth -top top -noalumacc; abc -g AND,OR,XOR,NAND,NOR,XNOR,MUX; write_json code_netlist.json; show -format dot -prefix code_graph"
