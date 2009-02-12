-- $Id: tcd_interface.vhd,v 1.8 2009-02-12 22:50:00 jschamba Exp $
-------------------------------------------------------------------------------
-- Title      : TCD Interface
-- Project    : THUB
-------------------------------------------------------------------------------
-- File       : tcd_interface.vhd
-- Author     : 
-- Company    : 
-- Created    : 2006-09-01
-- Last update: 2009-02-12
-- Platform   : 
-- Standard   : VHDL'93
-------------------------------------------------------------------------------
-- Description: Interface to TCD signals
-------------------------------------------------------------------------------
-- Copyright (c) 2006 
-------------------------------------------------------------------------------
-- Revisions  :
-- Date        Version  Author  Description
-- 2006-09-01  1.0      jschamba        Created
-------------------------------------------------------------------------------

LIBRARY ieee;
USE ieee.std_logic_1164.ALL;

LIBRARY lpm;
USE lpm.lpm_components.ALL;
LIBRARY altera_mf;
USE altera_mf.altera_mf_components.ALL;
LIBRARY altera;
USE altera.altera_primitives_components.ALL;

ENTITY tcd IS
  
  PORT (
    rhic_strobe : IN  std_logic;        -- TCD RHIC strobe
    data_strobe : IN  std_logic;        -- TCD data clock
    data        : IN  std_logic_vector (3 DOWNTO 0);   -- TCD data
    clock       : IN  std_logic;        -- 40 MHz clock
    trgword     : OUT std_logic_vector (19 DOWNTO 0);  -- captured 20bit word
    master_rst  : OUT std_logic;        -- indicates master reset command
    trigger     : OUT std_logic;        -- strobe signal sync'd to clock
    evt_trg     : OUT std_logic         -- this signal indicates an event
    );

END ENTITY tcd;

ARCHITECTURE a OF tcd IS
  TYPE type_sreg IS (S1, S2, S3, S4, S5);
  SIGNAL sreg : type_sreg;

  SIGNAL s_reg1       : std_logic_vector (3 DOWNTO 0);
  SIGNAL s_reg2       : std_logic_vector (3 DOWNTO 0);
  SIGNAL s_reg3       : std_logic_vector (3 DOWNTO 0);
  SIGNAL s_reg4       : std_logic_vector (3 DOWNTO 0);
  SIGNAL s_reg5       : std_logic_vector (3 DOWNTO 0);
  SIGNAL s_reg20_1    : std_logic_vector (19 DOWNTO 0);
  SIGNAL s_reg20_2    : std_logic_vector (19 DOWNTO 0);
  SIGNAL s_trg_unsync : std_logic;
  SIGNAL s_trg_short  : std_logic;
  SIGNAL s_stage1     : std_logic;
  SIGNAL s_stage2     : std_logic;
  SIGNAL s_stage3     : std_logic;
  SIGNAL s_mstage1    : std_logic;
  SIGNAL s_mstage2    : std_logic;
  SIGNAL s_mstage3    : std_logic;
  SIGNAL s_l0like     : std_logic;
  SIGNAL s_trigger    : std_logic;
  SIGNAL s_reset      : std_logic;
  SIGNAL s_mstr_rst   : std_logic;
  
BEGIN  -- ARCHITECTURE a

  -- inv_data_strobe <= NOT data_strobe;

  -- capture the trigger data in a cascade of 5 4-bit registers
  -- with the tcd data clock on trailing clock edge.

  dff_cascade : PROCESS (data_strobe) IS
  BEGIN
    IF falling_edge(data_strobe) THEN
      s_reg1 <= data;
      s_reg2 <= s_reg1;
      s_reg3 <= s_reg2;
      s_reg4 <= s_reg3;
      s_reg5 <= s_reg4;
      
    END IF;
  END PROCESS dff_cascade;

  -- On the rising edge of the RHIC strobe, latch the 5 4-bit registers into a
  -- 20-bit register.
  reg20_1 : PROCESS (rhic_strobe) IS
  BEGIN
    IF rising_edge(rhic_strobe) THEN
      s_reg20_1 (19 DOWNTO 16) <= s_reg5;
      s_reg20_1 (15 DOWNTO 12) <= s_reg4;
      s_reg20_1 (11 DOWNTO 8)  <= s_reg3;
      s_reg20_1 (7 DOWNTO 4)   <= s_reg2;
      s_reg20_1 (3 DOWNTO 0)   <= s_reg1;
    END IF;
  END PROCESS reg20_1;

  -- use this as the trigger word output, sync to 40MHz clock
  -- also delay by 1 40MHz clock
  PROCESS (clock) IS
  BEGIN  -- PROCESS
    IF falling_edge(clock) THEN
      trgword   <= s_reg20_2;
      s_reg20_2 <= s_reg20_1;
    END IF;
  END PROCESS;


--  trgword <= s_reg20_1;

  -- now check if there is a valid trigger command:
  trg : PROCESS (s_reg20_1(19 DOWNTO 16)) IS
  BEGIN
    CASE s_reg20_1(19 DOWNTO 16) IS
      WHEN "0100" =>                    -- "4" (trigger0)
        s_trg_unsync <= '1';
        s_l0like     <= '1';
      WHEN "0101" =>                    -- "5" (trigger1)
        s_trg_unsync <= '1';
        s_l0like     <= '1';
      WHEN "0110" =>                    -- "6" (trigger2)
        s_trg_unsync <= '1';
        s_l0like     <= '1';
      WHEN "0111" =>                    -- "7" (trigger3)
        s_trg_unsync <= '1';
        s_l0like     <= '1';
      WHEN "1000" =>                    -- "8" (pulser0)
        s_trg_unsync <= '1';
        s_l0like     <= '1';
      WHEN "1001" =>                    -- "9" (pulser1)
        s_trg_unsync <= '1';
        s_l0like     <= '1';
      WHEN "1010" =>                    -- "10" (pulser2)
        s_trg_unsync <= '1';
        s_l0like     <= '1';
      WHEN "1011" =>                    -- "11" (pulser3)
        s_trg_unsync <= '1';
        s_l0like     <= '1';
      WHEN "1100" =>                    -- "12" (config)
        s_trg_unsync <= '1';
        s_l0like     <= '1';
      WHEN "1101" =>                    -- "13" (abort)
        s_trg_unsync <= '1';
        s_l0like     <= '0';
      WHEN "1110" =>                    -- "14" (L1accept)
        s_trg_unsync <= '1';
        s_l0like     <= '0';
      WHEN "1111" =>                    -- "15" (L2accept)
        s_trg_unsync <= '1';
        s_l0like     <= '0';
      WHEN OTHERS =>
        s_trg_unsync <= '0';
        s_l0like     <= '0';
    END CASE;

    -- master reset command
    IF s_reg20_1(19 DOWNTO 16) = "0010" THEN
      s_mstr_rst <= '1';
    ELSE
      s_mstr_rst <= '0';
    END IF;
  END PROCESS trg;

  -- shorten s_trg_unsync to 2 data strobe clock cycles
  s_reset <= '0';                       -- no reset
  shorten : PROCESS (data_strobe, s_reset) IS
  BEGIN
    IF s_reset = '1' THEN               -- asynchronous reset (active low)
      s_trg_short <= '0';
      sreg        <= S1;
    ELSIF falling_edge(data_strobe) THEN
      s_trg_short <= '0';

      CASE sreg IS
        WHEN S1 =>
          s_trg_short <= s_trg_unsync;
          IF s_trg_unsync = '1' THEN
            sreg <= S2;
          END IF;
        WHEN S2 =>
          s_trg_short <= s_trg_unsync;
          sreg        <= S3;
        WHEN S3 =>
          sreg <= S4;
        WHEN S4 =>
          sreg <= S5;
        WHEN S5 =>
          sreg <= S1;
        WHEN OTHERS =>
          sreg <= S1;
      END CASE;
      
    END IF;
  END PROCESS shorten;


  -- when a valid trigger command is found, synchronize the resulting trigger
  -- to the 40MHz clock with a 3 stage DFF cascade and to make the signal
  -- exactly 1 clock wide
  syncit : PROCESS (clock) IS
  BEGIN
    IF rising_edge(clock) THEN
      s_stage1 <= s_trg_short;
      s_stage2 <= s_stage1;
      s_stage3 <= s_stage2;

      s_mstage1 <= s_mstr_rst;
      s_mstage2 <= s_mstage1;
      s_mstage3 <= s_mstage2;
    END IF;
  END PROCESS syncit;

  s_trigger <= s_stage2 AND (NOT s_stage3);

  trigger <= s_trigger;
  evt_trg <= s_trigger AND s_l0like;

  master_rst <= s_mstage2 AND (NOT s_mstage3);
  
END ARCHITECTURE a;
