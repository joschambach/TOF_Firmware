-- $Id: uc_fpga_interface.vhd,v 1.7 2008-05-14 20:45:43 jschamba Exp $
-------------------------------------------------------------------------------
-- Title      : Micro-FPGA Interface
-- Project    : 
-------------------------------------------------------------------------------
-- File       : uc_fpga_interface.vhd
-- Author     : 
-- Company    : 
-- Created    : 2006-06-27
-- Last update: 2008-05-14
-- Platform   : 
-- Standard   : VHDL'93
-------------------------------------------------------------------------------
-- Description: Interface for bus between Micro and Main FPGA
-------------------------------------------------------------------------------
-- Copyright (c) 2006 
-------------------------------------------------------------------------------
-- Revisions  :
-- Date        Version  Author  Description
-- 2006-06-27  1.0      jschamba        Created
-------------------------------------------------------------------------------

LIBRARY ieee;
USE ieee.std_logic_1164.ALL;

ENTITY uc_fpga_interface IS
  PORT (
    clock         : IN  std_logic;
    arstn         : IN  std_logic;
    dir           : IN  std_logic;
    ctl           : IN  std_logic;
    ds            : IN  std_logic;
    uc_data_in    : IN  std_logic_vector(7 DOWNTO 0);
    reg1          : IN  std_logic_vector(7 DOWNTO 0);
    reg2          : IN  std_logic_vector(7 DOWNTO 0);
    reg3          : IN  std_logic_vector(7 DOWNTO 0);
    reg4          : IN  std_logic_vector(7 DOWNTO 0);
    reg5          : IN  std_logic_vector(7 DOWNTO 0);
    reg6          : IN  std_logic_vector(7 DOWNTO 0);
    reg7          : IN  std_logic_vector(7 DOWNTO 0);
    reg8          : IN  std_logic_vector(7 DOWNTO 0);
    serdes_reg    : IN  std_logic_vector(7 DOWNTO 0);
    serdes_statma : IN  std_logic_vector(3 DOWNTO 0);
    serdes_statmb : IN  std_logic_vector(3 DOWNTO 0);
    serdes_statmc : IN  std_logic_vector(3 DOWNTO 0);
    serdes_statmd : IN  std_logic_vector(3 DOWNTO 0);
    serdes_statme : IN  std_logic_vector(3 DOWNTO 0);
    serdes_statmf : IN  std_logic_vector(3 DOWNTO 0);
    serdes_statmg : IN  std_logic_vector(3 DOWNTO 0);
    serdes_statmh : IN  std_logic_vector(3 DOWNTO 0);
    reg_addr      : OUT std_logic_vector(2 DOWNTO 0);
    sreg_addr     : OUT std_logic_vector(3 DOWNTO 0);
    reg_load      : OUT std_logic;
    sreg_load     : OUT std_logic;
    reg_clr       : OUT std_logic;
    uc_data_out   : OUT std_logic_vector(7 DOWNTO 0)
    );
END uc_fpga_interface;

ARCHITECTURE SYN OF uc_fpga_interface IS

  TYPE State_type IS (State0, State1, State2, State3, State4,
                      State5, State6, State7);
  SIGNAL state : State_type;

  SIGNAL is_address : boolean;
  SIGNAL is_data_w  : boolean;
  SIGNAL addr       : std_logic_vector(4 DOWNTO 0);
  SIGNAL ser_regrd  : std_logic_vector(7 DOWNTO 0);

BEGIN  -- ARCHITECTURE SYN

  is_address <= (ctl = '1') AND (ds = '1') AND (dir = '0');
  is_data_w  <= (ctl = '0') AND (ds = '1') AND (dir = '0');

  reg_addr  <= addr(2 DOWNTO 0);
  sreg_addr <= addr(3 DOWNTO 0);

  uc_fpga_sm : PROCESS (clock, arstn) IS
  BEGIN  -- PROCESS uc_fpga_sm
    IF arstn = '0' THEN                     -- asynchronous reset (active low)
      addr      <= (OTHERS => '1');
      reg_load  <= '0';
      sreg_load <= '0';
    ELSIF clock'event AND clock = '1' THEN  -- rising clock edge
      reg_load  <= '0';
      sreg_load <= '0';
      reg_clr   <= '0';

      IF is_address AND (uc_data_in(7 DOWNTO 5) = "100") THEN
        addr <= uc_data_in(4 DOWNTO 0);
      END IF;

      IF is_data_w AND (addr(4) = '1') THEN  -- Serdes register load
        sreg_load <= '1';
      ELSIF is_data_w AND (addr = "00111") THEN           -- register clear
        reg_clr <= '1';                 -- on address "0x87"
      ELSIF is_data_w AND (addr(4 DOWNTO 3) = "00") THEN  -- "regular" register load
        reg_load <= '1';                -- on addresses 0x80 - 0x87
      END IF;

    END IF;
  END PROCESS uc_fpga_sm;

  ser_regrd(7 DOWNTO 4) <= serdes_reg(7 DOWNTO 4);
  ser_regrd(3 DOWNTO 0) <=
    serdes_statma WHEN addr = "10001" ELSE
    serdes_statmb WHEN addr = "10010" ELSE
    serdes_statmc WHEN addr = "10011" ELSE
    serdes_statmd WHEN addr = "10100" ELSE
    serdes_statme WHEN addr = "10101" ELSE
    serdes_statmf WHEN addr = "10110" ELSE
    serdes_statmg WHEN addr = "10111" ELSE
    serdes_statmh WHEN addr = "11000" ELSE
    serdes_reg(3 DOWNTO 0);
  
  uc_data_out <=
    reg1      WHEN addr = "00000" ELSE
    reg2      WHEN addr = "00001" ELSE
    reg3      WHEN addr = "00010" ELSE
    reg4      WHEN addr = "00011" ELSE
    reg5      WHEN addr = "00100" ELSE
    reg6      WHEN addr = "00101" ELSE
    reg7      WHEN addr = "00110" ELSE
    reg8      WHEN addr = "00111" ELSE
    ser_regrd WHEN addr(4) = '1' ELSE
    "11111111";
  
  
END ARCHITECTURE SYN;
