
LIBRARY IEEE;
USE IEEE.STD_LOGIC_1164.ALL;
USE IEEE.NUMERIC_STD.ALL;

LIBRARY WORK;
USE WORK.ALL;

-----------------------------------------------------
--
--  This block will contain a decoder to decode a 4-bit number
--  to a 7-bit vector suitable to drive a HEX dispaly
--
--  It is a purely combinational block (think Pattern 1) and
--  is similar to a block you designed in Lab 1.
--
--------------------------------------------------------

ENTITY adder IS
	PORT(
        digit : IN  UNSIGNED(3 DOWNTO 0);  -- number 0 to 0xF
		clk : IN STD_LOGIC;
		state : OUT INTEGER RANGE 0 TO 6;
        seg7 : OUT STD_LOGIC_VECTOR(6 DOWNTO 0)  -- one per segment
	);
END;


ARCHITECTURE behavioral OF adder IS
signal reg,data :STD_LOGIC_VECTOR(6 downto 0);

signal stt : integer range 0 to 7:=0;
BEGIN
	state<=stt;
	combinational : PROCESS (digit,clk)
	BEGIN
			CASE digit IS
			WHEN "0000" =>
				data <= not "1000000"; -- displays '0'
			WHEN "0001" =>
				data <= not "1111001"; -- displays '1'
			WHEN "0010" =>
				data <= not "0100100"; -- displays '2'
			WHEN "0011" =>
				data <= not "0110000"; -- displays '3'
			WHEN "0100" =>
				data <= not "0011001"; -- displays '4'
			WHEN "0101" =>
				data <= not "0010010"; -- displays '5'
			WHEN "0110" =>
				data <= not "0000010"; -- displays '6'
			WHEN "0111" =>
				data <= not "1111000"; -- displays '7'
			WHEN "1000" =>
				data <= not "0000000"; -- displays '8'
			WHEN "1001" =>
				data <= not "0010000"; -- displays '9'
			WHEN "1010" =>
				data <= not "0001000"; -- displays 'A'
			WHEN "1011" =>
				data <= not "0000011"; -- displays 'B'
			WHEN "1100" =>
				data <= not "1000110"; -- displays 'C'
			WHEN "1101" =>
				data <= not "0100001"; -- displays 'D'
			WHEN "1110" =>
				data <= not "0000110"; -- displays 'E'
			WHEN "1111" =>
				data <= not "0001110"; -- displays 'F'
			WHEN others=>
				data <= "0000000";
		END case;
	
	END PROCESS;
	
	PROCESS (digit,clk)
	BEGIN
	if rising_edge(clk) then
		reg<=data;
	end if;
	END PROCESS;

	PROCESS (clk)
	BEGIN
		if rising_edge(clk) then
			CASE stt IS
            WHEN 0 =>
                seg7 <= reg(6) & '0' & '0' & '0' & '0' & '0' & '0';
            WHEN 1 =>
                seg7 <= '0' & reg(5) & '0' & '0' & '0' & '0' & '0';
            WHEN 2 =>
                seg7 <= '0' & '0' & reg(4) & '0' & '0' & '0' & '0';
            WHEN 3 =>
                seg7 <= '0' & '0' & '0' & reg(3) & '0' & '0' & '0';
            WHEN 4 =>
                seg7 <= '0' & '0' & '0' & '0' & reg(2) & '0' & '0';
            WHEN 5 =>
                seg7 <= '0' & '0' & '0' & '0' & '0' & reg(1) & '0';
            WHEN 6 =>
                seg7 <= '0' & '0' & '0' & '0' & '0' & '0' & reg(0);
			WHEN others=>
				seg7 <= "0000000";
        END CASE;
		
		stt <= stt+1;
		if stt = 6 then 
			stt <= 0;	
		end if;	
	   END IF;	
	END PROCESS;
	
END behavioral;