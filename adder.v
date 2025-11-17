module adder (
    input  wire [3:0] digit,   // UNSIGNED(3 downto 0)
    input  wire       clk,
    output reg  [2:0] state,   // INTEGER RANGE 0 TO 6
    output reg  [6:0] seg7
);

    reg [6:0] reg_seg;
    reg [6:0] data;
    reg [2:0] stt = 0;

    // State output
    always @(*) begin
        state = stt;
    end

    // Combinational decoder
    always @(*) begin
        case (digit)
            4'b0000: data = ~7'b1000000; // '0'
            4'b0001: data = ~7'b1111001; // '1'
            4'b0010: data = ~7'b0100100; // '2'
            4'b0011: data = ~7'b0110000; // '3'
            4'b0100: data = ~7'b0011001; // '4'
            4'b0101: data = ~7'b0010010; // '5'
            4'b0110: data = ~7'b0000010; // '6'
            4'b0111: data = ~7'b1111000; // '7'
            4'b1000: data = ~7'b0000000; // '8'
            4'b1001: data = ~7'b0010000; // '9'
            4'b1010: data = ~7'b0001000; // 'A'
            4'b1011: data = ~7'b0000011; // 'B'
            4'b1100: data = ~7'b1000110; // 'C'
            4'b1101: data = ~7'b0100001; // 'D'
            4'b1110: data = ~7'b0000110; // 'E'
            4'b1111: data = ~7'b0001110; // 'F'
            default: data = 7'b0000000;
        endcase
    end

    // Clocked latch of decoded value
    always @(posedge clk) begin
        reg_seg <= data;
    end

    // Output mux + counter
    always @(posedge clk) begin
        case (stt)
            0: seg7 <= {reg_seg[6], 6'b000000};
            1: seg7 <= {1'b0, reg_seg[5], 5'b00000};
            2: seg7 <= {2'b00, reg_seg[4], 4'b0000};
            3: seg7 <= {3'b000, reg_seg[3], 3'b000};
            4: seg7 <= {4'b0000, reg_seg[2], 2'b00};
            5: seg7 <= {5'b00000, reg_seg[1], 1'b0};
            6: seg7 <= {6'b000000, reg_seg[0]};
            default: seg7 <= 7'b0000000;
        endcase

        // increment stt
        if (stt == 6)
            stt <= 0;
        else
            stt <= stt + 1;
    end

endmodule
