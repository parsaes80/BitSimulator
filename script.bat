call oss-cad-suite\environment.bat
yosys -p "read_verilog code.v; synth -top top -noalumacc; abc -g AND,OR,NAND,NOR,XOR,XNOR; write_json code_netlist.json; show -format dot -prefix code_graph"
