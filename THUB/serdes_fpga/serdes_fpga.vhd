-- $Id: serdes_fpga.vhd,v 1.15 2007-12-07 21:52:13 jschamba Exp $
-------------------------------------------------------------------------------
-- Title      : SERDES_FPGA
-- Project    : 
-------------------------------------------------------------------------------
-- File       : serdes_fpga.vhd
-- Author     : J. Schambach
-- Company    : 
-- Created    : 2005-12-19
-- Last update: 2007-12-07
-- Platform   : 
-- Standard   : VHDL'93
-------------------------------------------------------------------------------
-- Description: Top Level Component for the THUB SERDES FPGAs
-------------------------------------------------------------------------------
-- Copyright (c) 2005 
-------------------------------------------------------------------------------
-- Revisions  :
-- Date        Version  Author  Description
-- 2005-12-19  1.0      jschamba        Created
-------------------------------------------------------------------------------
LIBRARY ieee;
USE ieee.std_logic_1164.ALL;
LIBRARY altera_mf;
USE altera_mf.altera_mf_components.ALL;
LIBRARY lpm;
USE lpm.lpm_components.ALL;
LIBRARY altera;
USE altera.altera_primitives_components.ALL;
USE work.my_conversions.ALL;
USE work.my_utilities.ALL;

ENTITY serdes_fpga IS
  PORT
    (
      clk                                                : IN    std_logic;  -- Master clock
      -- ***** Mictor outputs *****
      mt                                                 : OUT   std_logic_vector(31 DOWNTO 0);
      mt_clk                                             : OUT   std_logic;
      -- ***** bus to main fpga *****
      ma                                                 : INOUT std_logic_vector(35 DOWNTO 0);
      m_all                                              : IN    std_logic_vector(3 DOWNTO 0);
      -- ***** SRAM *****
      -- ADDR[17] and ADDR[18] on schematic are actually TCK and TDO, so renamed 
      -- ADDR[19] and ADDR[20] to be ADDR[17] and ADDR[18], respectively
      sra_addr, srb_addr                                 : OUT   std_logic_vector(18 DOWNTO 0);
      sra_d                                              : INOUT std_logic_vector(31 DOWNTO 0);
      srb_d                                              : INOUT std_logic_vector(31 DOWNTO 0);
      sra_tck, srb_tck                                   : OUT   std_logic;  -- JTAG
      sra_tdo, srb_tdo                                   : IN    std_logic;  -- JTAG
      sra_rw, srb_rw                                     : OUT   std_logic;
      sra_oe_n, srb_oe_n                                 : OUT   std_logic;
      sra_bw, srb_bw                                     : OUT   std_logic_vector(3 DOWNTO 0);
      sra_adv, srb_adv                                   : OUT   std_logic;
      sra_clk, srb_clk                                   : OUT   std_logic;
      -- ***** SERDES *****
      ch0_rxd, ch1_rxd, ch2_rxd, ch3_rxd                 : IN    std_logic_vector(17 DOWNTO 0);
      ch0_txd, ch1_txd, ch2_txd, ch3_txd                 : OUT   std_logic_vector(17 DOWNTO 0);
      ch0_den, ch1_den, ch2_den, ch3_den                 : OUT   std_logic;
      ch0_ren, ch1_ren, ch2_ren, ch3_ren                 : OUT   std_logic;
      ch0_sync, ch1_sync, ch2_sync, ch3_sync             : OUT   std_logic;
      ch0_tpwdn_n, ch1_tpwdn_n, ch2_tpwdn_n, ch3_tpwdn_n : OUT   std_logic;
      ch0_rpwdn_n, ch1_rpwdn_n, ch2_rpwdn_n, ch3_rpwdn_n : OUT   std_logic;
      ch0_tck, ch1_tck, ch2_tck, ch3_tck                 : OUT   std_logic;
      ch0_lock_n, ch1_lock_n, ch2_lock_n, ch3_lock_n     : IN    std_logic;
      ch0_refclk, ch1_refclk, ch2_refclk, ch3_refclk     : OUT   std_logic;
      ch0_rclk, ch1_rclk, ch2_rclk, ch3_rclk             : IN    std_logic;
      ch0_loc_le, ch1_loc_le, ch2_loc_le, ch3_loc_le     : OUT   std_logic;
      ch0_line_le, ch1_line_le, ch2_line_le, ch3_line_le : OUT   std_logic;
      -- ***** LEDs *****
      led                                                : OUT   std_logic_vector(1 DOWNTO 0);
      -- ***** test points *****
      tp                                                 : IN    std_logic_vector(15 DOWNTO 2)

      );
END serdes_fpga;


ARCHITECTURE a OF serdes_fpga IS

  COMPONENT serdes_poweron IS
    PORT (
      clk         : IN  std_logic;
      tpwdn_n     : OUT std_logic;
      rpwdn_n     : OUT std_logic;
      sync        : OUT std_logic;
      ch_lock_n   : IN  std_logic;
      ch_ready    : OUT std_logic;
      pll_locked  : IN  std_logic;
      rxd         : IN  std_logic_vector (17 DOWNTO 0);
      serdes_data : IN  std_logic_vector (17 DOWNTO 0);
      txd         : OUT std_logic_vector (17 DOWNTO 0);
      areset_n    : IN  std_logic);
  END COMPONENT serdes_poweron;

  COMPONENT smif IS
    PORT (
      clk40mhz   : IN  std_logic;
      clk20mhz   : IN  std_logic;
      datain     : IN  std_logic_vector(11 DOWNTO 0);
      data_type  : IN  std_logic_vector(3 DOWNTO 0);
      serdes_out : OUT std_logic_vector(17 DOWNTO 0);
      serdes_reg : OUT std_logic_vector(7 DOWNTO 0);
      areset     : IN  std_logic);
  END COMPONENT smif;

  COMPONENT pll
    PORT
      (
        areset : IN  std_logic := '0';
        inclk0 : IN  std_logic := '0';
        c0     : OUT std_logic;
        c1     : OUT std_logic;
        c2     : OUT std_logic;
        locked : OUT std_logic
        );
  END COMPONENT;

  COMPONENT zbt_ctrl_top
    PORT (
      clk           : IN    std_logic;
      RESET_N       : IN    std_logic;  -- active LOW asynchronous reset
-- local bus interface
      ADDR          : IN    std_logic_vector(18 DOWNTO 0);
      DATA_IN       : IN    std_logic_vector(31 DOWNTO 0);
      DATA_OUT      : OUT   std_logic_vector(31 DOWNTO 0);
      RD_WR_N       : IN    std_logic;  -- active LOW write
      ADDR_ADV_LD_N : IN    std_logic;  -- advance/load address (active LOW load)
      DM            : IN    std_logic_vector(3 DOWNTO 0);  -- data mask bits                   
-- SRAM interface
      SA            : OUT   std_logic_vector(18 DOWNTO 0);  -- address bus to RAM   
      DQ            : INOUT std_logic_vector(31 DOWNTO 0);  -- data to/from RAM
      RW_N          : OUT   std_logic;  -- active LOW write
      ADV_LD_N      : OUT   std_logic;  -- active LOW load
      BW_N          : OUT   std_logic_vector(3 DOWNTO 0)  -- active LOW byte enables
      );
  END COMPONENT;

  COMPONENT LFSR IS
    PORT (
      RESETn : IN  std_logic;
      clock  : IN  std_logic;
      d      : OUT std_logic_vector(16 DOWNTO 0));
  END COMPONENT LFSR;

  COMPONENT mux18x2 IS
    PORT (
      data0x : IN  std_logic_vector (17 DOWNTO 0);
      data1x : IN  std_logic_vector (17 DOWNTO 0);
      sel    : IN  std_logic;
      result : OUT std_logic_vector (17 DOWNTO 0));
  END COMPONENT mux18x2;

  COMPONENT mux17x4 IS
    PORT (
      data0x : IN  std_logic_vector (16 DOWNTO 0);
      data1x : IN  std_logic_vector (16 DOWNTO 0);
      data2x : IN  std_logic_vector (16 DOWNTO 0);
      data3x : IN  std_logic_vector (16 DOWNTO 0);
      sel    : IN  std_logic_vector (1 DOWNTO 0);
      result : OUT std_logic_vector (16 DOWNTO 0));
  END COMPONENT mux17x4;

  COMPONENT decoder IS
    PORT (
      input_sig : IN  std_logic;
      adr       : IN  std_logic_vector(1 DOWNTO 0);
      y         : OUT std_logic_vector(3 DOWNTO 0));
  END COMPONENT decoder;

  SIGNAL areset_n          : std_logic;
  SIGNAL globalclk         : std_logic;
  SIGNAL pll_20mhz         : std_logic;
  SIGNAL pll_80mhz         : std_logic;
  SIGNAL pll_160mhz        : std_logic;  -- PLL 4x output
  SIGNAL pll_160mhz_p      : std_logic;  -- PLL 4x output with phase shift
  SIGNAL clk20mhz          : std_logic;
  SIGNAL div2out           : std_logic;
  SIGNAL serdes_clk        : std_logic;
  SIGNAL pll_locked        : std_logic;
  SIGNAL counter_q         : std_logic_vector (16 DOWNTO 0);
  SIGNAL lsfr_d            : std_logic_vector (16 DOWNTO 0);
  SIGNAL serdes_data       : std_logic_vector (17 DOWNTO 0);
  SIGNAL serdes_tst_data   : std_logic_vector (17 DOWNTO 0);
  SIGNAL txfifo_rdreq      : std_logic;
  SIGNAL txfifo_q          : std_logic_vector (17 DOWNTO 0);
  SIGNAL txfifo_full       : std_logic;
  SIGNAL txfifo_empty      : std_logic;
  SIGNAL rxfifo_rdreq      : std_logic;
  SIGNAL rxfifo_q          : std_logic_vector (17 DOWNTO 0);
  SIGNAL rxfifo_full       : std_logic;
  SIGNAL rxfifo_empty      : std_logic;
  SIGNAL ctr_enable        : std_logic;
  SIGNAL s_rw_n            : std_logic;
  SIGNAL s_addr_adv_ld_n   : std_logic;
  SIGNAL s_dm              : std_logic_vector (3 DOWNTO 0);
  SIGNAL s_sram_dataout    : std_logic_vector(31 DOWNTO 0);
  SIGNAL s_txfifo_q        : std_logic_vector(16 DOWNTO 0);
  SIGNAL s_txfifo_aclr     : std_logic;
  SIGNAL s_txfifo_rdreq    : std_logic;
  SIGNAL s_txfifo_empty    : std_logic;
  SIGNAL s_rxfifo_q        : std_logic_vector(16 DOWNTO 0);
  SIGNAL s_rxfifo_aclr     : std_logic;
  SIGNAL s_rxfifo_rdreq    : std_logic;
  SIGNAL s_rxfifo_wrreq    : std_logic;
  SIGNAL s_rxfifo_empty    : std_logic;
  SIGNAL s_errorctr        : std_logic_vector(31 DOWNTO 0);
  SIGNAL s_ctr_aclr        : std_logic;
  SIGNAL s_ch0_locked      : std_logic;
  SIGNAL s_ch1_locked      : std_logic;
  SIGNAL s_ch2_locked      : std_logic;
  SIGNAL s_ch3_locked      : std_logic;
  SIGNAL s_send_data       : std_logic;
  SIGNAL s_smif_dataout    : std_logic_vector (15 DOWNTO 0);
  SIGNAL s_smif_fifo_empty : std_logic;
  SIGNAL s_smif_select     : std_logic_vector(1 DOWNTO 0);
  SIGNAL s_smif_rdenable   : std_logic;
  SIGNAL s_smif_datain     : std_logic_vector(11 DOWNTO 0);
  SIGNAL s_smif_datatype   : std_logic_vector(3 DOWNTO 0);
  SIGNAL s_serdes_out      : std_logic_vector(17 DOWNTO 0);
  SIGNAL s_ch0_txd         : std_logic_vector(17 DOWNTO 0);
  SIGNAL s_ch1_txd         : std_logic_vector(17 DOWNTO 0);
  SIGNAL s_ch2_txd         : std_logic_vector(17 DOWNTO 0);
  SIGNAL s_ch3_txd         : std_logic_vector(17 DOWNTO 0);
  SIGNAL s_serdes_reg      : std_logic_vector(7 DOWNTO 0);

  SIGNAL s_ch0fifo_rdreq : std_logic;
  SIGNAL s_ch0fifo_aclr  : std_logic;
  SIGNAL s_ch0fifo_empty : std_logic;
  SIGNAL s_ch0fifo_q     : std_logic_vector(15 DOWNTO 0);
  SIGNAL s_ch1fifo_rdreq : std_logic;
  SIGNAL s_ch1fifo_aclr  : std_logic;
  SIGNAL s_ch1fifo_empty : std_logic;
  SIGNAL s_ch1fifo_q     : std_logic_vector(15 DOWNTO 0);
  SIGNAL s_ch2fifo_rdreq : std_logic;
  SIGNAL s_ch2fifo_aclr  : std_logic;
  SIGNAL s_ch2fifo_empty : std_logic;
  SIGNAL s_ch2fifo_q     : std_logic_vector(15 DOWNTO 0);
  SIGNAL s_ch3fifo_rdreq : std_logic;
  SIGNAL s_ch3fifo_aclr  : std_logic;
  SIGNAL s_ch3fifo_empty : std_logic;
  SIGNAL s_ch3fifo_q     : std_logic_vector(15 DOWNTO 0);

  TYPE State_type IS (State0, State1, State1a, State2, State3);
  SIGNAL state : State_type;

BEGIN

  -----------------------------------------------------------------------------
  -- Clocks and resets
  -----------------------------------------------------------------------------
  areset_n <= '1';  -- asynchronous global reset, active low

  -- create 20 MHz clock with TFF:
  div2 : TFF PORT MAP (
    t    => '1',
    clk  => globalclk,
    clrn => '1',
    prn  => '1',
    q    => div2out);

  -- PLL
  pll_instance : pll PORT MAP (
    areset => '0',
    inclk0 => clk,
    c0     => pll_160mhz,
    c1     => pll_160mhz_p,
    c2     => pll_80mhz,
    locked => pll_locked);

  global_clk_buffer1 : global PORT MAP (a_in => clk, a_out => globalclk);
  global_clk_buffer2 : global PORT MAP (a_in => div2out, a_out => clk20mhz);

  serdes_clk <= clk20mhz;
  -- serdes_clk <= '0';
  -- serdes_clk <= globalclk;

  -----------------------------------------------------------------------------
  -- SERDES-MAIN FPGA interface
  -----------------------------------------------------------------------------
  -- Serdes <-> Master Interface is implemented as 36 lines that are assigned
  -- as follows:
  -- ma[15..0]  : 16 bit (Serdes FIFO) data from Serdes to Master (output)
  -- ma[16]     : FIFO empty (output)
  -- ma[18..17] : Select one of 4 Serdes FIFOs (input)
  -- ma[19]     : Read Enable to FIFO (input)
  --
  -- ma[31..20] : 12 bit data from Master to Serdes (input)
  -- ma[35..32] : 4 bit data type from Master (input)
  --
  -- Currently, there are 4 "data types" from Master to Serdes defined:
  --    "trigger", "bunch reset", "reset", and "load register"
  -- each action is triggered on leading edge of 40MHz clock and is followed
  -- by on clock of wait state (defined in smif.vhd)

  ma(35 DOWNTO 17) <= (OTHERS => 'Z');  -- tri-state unused outputs to master FPGA

  -- SERDES (S) to MASTER (M) interface
  ma(15 DOWNTO 0) <= s_smif_dataout;    -- 16bit data from S to M
  ma(16)          <= s_smif_fifo_empty; -- FIFO empty indicator from S to M
  s_smif_select   <= ma(18 DOWNTO 17);  -- select from M to S to select 1 of 4 FIFOs
  s_smif_rdenable <= ma(19);            -- read enable from M to S for FIFO

  -- MASTER (M) to SERDES (S) interface
  s_smif_datain   <= ma(31 DOWNTO 20);  -- 12bit data from M to S 
  s_smif_datatype <= ma(35 DOWNTO 32);  -- 4bit data type indicator from M to S

  smif_inst : smif PORT MAP (
    clk40mhz   => globalclk,
    clk20mhz   => clk20mhz,
    datain     => s_smif_datain,
    data_type  => s_smif_datatype,
    serdes_out => s_serdes_out,
    serdes_reg => s_serdes_reg,
    areset     => '0');

  -----------------------------------------------------------------------------
  -- Mictors
  -----------------------------------------------------------------------------
-- mt(17 DOWNTO 0)  <= s_serdes_out;
-- mt(11 DOWNTO 0)  <= s_smif_datain;
-- mt(15 DOWNTO 12) <= s_smif_datatype;
-- mt(23 DOWNTO 18) <= s_txfifo_q(7 DOWNTO 2);
-- mt(31 DOWNTO 24) <= s_serdes_reg;

--  mt(17 DOWNTO 0)  <= ch0_rxd;
--  mt(23 DOWNTO 18) <= s_txfifo_q(7 DOWNTO 2);
--  mt(15 DOWNTO 0)  <= s_rxfifo_q(15 DOWNTO 0);
--  mt(23 DOWNTO 16) <= s_txfifo_q(7 DOWNTO 0);
--  mt(30 DOWNTO 24) <= s_errorctr(6 DOWNTO 0);
--  mt(31)           <= s_ch0_locked;

-- mt_clk <= globalclk;
--  mt_clk <= serdes_clk;
--  mt_clk <= pll_80mhz;

  mt     <= (OTHERS => '0');
  mt_clk <= '0';

  -----------------------------------------------------------------------------
  -- SRAM
  -----------------------------------------------------------------------------
  -- SRAM defaults
  sra_addr <= (OTHERS => '0');
  srb_addr <= (OTHERS => '0');
  sra_tck  <= '0';
  srb_tck  <= '0';
  sra_rw   <= '1';
  srb_rw   <= '1';
  sra_oe_n <= '0';
  srb_oe_n <= '0';
  sra_bw   <= (OTHERS => '0');
  srb_bw   <= (OTHERS => '0');
  sra_adv  <= '0';
  srb_adv  <= '0';
  sra_clk  <= pll_160mhz;
  srb_clk  <= pll_160mhz;

  sra_d <= (OTHERS => 'Z');
  srb_d <= (OTHERS => 'Z');

  -----------------------------------------------------------------------------
  -- SERDES defaults
  -----------------------------------------------------------------------------

  -- channel 0
  ch0_den     <= s_serdes_reg(0);       -- tx enabled by serdes register bit 0
  ch0_ren     <= s_serdes_reg(0);       -- rx enabled by serdes register bit 0
  ch0_loc_le  <= '0';                   -- local loopback disabled
  ch0_line_le <= '0';                   -- line loopback disabled
  ch0_txd     <= s_ch0_txd;
  led(0)      <= ch0_lock_n;

  -- channel 1
  ch1_den     <= s_serdes_reg(1);       -- tx enabled by serdes register bit 1
  ch1_ren     <= s_serdes_reg(1);       -- rx enabled by serdes_register bit 1
  ch1_loc_le  <= '0';                   -- local loopback disabled
  ch1_line_le <= '0';                   -- line loopback disabled
  ch1_txd     <= s_ch1_txd;
  led(1)      <= ch1_lock_n;

  -- channel 2
  ch2_den     <= s_serdes_reg(2);       -- tx enabled by serdes register bit 2
  ch2_ren     <= s_serdes_reg(2);       -- rx enabled by serdes register bit 2
  ch2_loc_le  <= '0';
  ch2_line_le <= '0';
  ch2_txd     <= s_ch2_txd;

  -- channel 3
  ch3_den     <= s_serdes_reg(3);       -- tx enabled by serdes register bit 3
  ch3_ren     <= s_serdes_reg(3);       -- rx enabled by serdes register bit 3
  ch3_loc_le  <= '0';
  ch3_line_le <= '0';
  ch3_txd     <= s_ch3_txd;


  -- tx clocks and rx refclocks
  -- all of these clocks come directly to the SERDES
  -- daughtercard (differentially), so just put GND ON
  -- those lines if not used.
  -- when resistor is changed to this path, assign proper clock here:
  ch0_tck    <= serdes_clk;
  ch1_tck    <= serdes_clk;
  ch2_tck    <= serdes_clk;
  ch3_tck    <= serdes_clk;
  ch0_refclk <= serdes_clk;
  ch1_refclk <= serdes_clk;
  ch2_refclk <= serdes_clk;
  ch3_refclk <= serdes_clk;

  -----------------------------------------------------------------------------
  -- SERDES utilities
  -----------------------------------------------------------------------------

  -- MUX for serdes TX
  mux_inst : mux18x2 PORT MAP (
    data0x => serdes_tst_data,          -- test data
    data1x => s_serdes_out,             -- "real" data
    sel    => s_serdes_reg(7),
    result => serdes_data);             -- goes to poweron sm first to be muxed
                                        -- with poweron data


  -- Counter as data input to channel 0 TX
--  counter17b : lpm_counter GENERIC MAP (
--    LPM_WIDTH     => 17,
--    LPM_TYPE      => "LPM_COUNTER",
--    LPM_DIRECTION => "UP")
--    PORT MAP (
--      clock  => serdes_clk,
--      q      => counter_q,
--      clk_en => '1',
--      aclr   => s_ctr_aclr);

  -- (L)inear (F)eedback (S)hift (R)egister as data generator (17 bit pseudo random numbers)
  datagen : LFSR
    PORT MAP (
      RESETn => '1',
      clock  => serdes_clk,
      d      => lsfr_d);

  -- SERDES data:
  s_send_data <= s_ch0_locked AND s_serdes_reg(6);  -- control sending of data with serdes register bit 6

-- serdes_tst_data(16 DOWNTO 0) <= counter_q;
  serdes_tst_data(15 DOWNTO 0) <= lsfr_d(15 DOWNTO 0);
  serdes_tst_data(16)          <= lsfr_d(16) AND s_serdes_reg(6);
  serdes_tst_data(17)          <= s_send_data;


  -- test feature:
  -- latch tx data into a dual clock 17bit fifo for later comparison
  txfifo : dcfifo
    GENERIC MAP (
      intended_device_family => "Cyclone II",
      lpm_hint               => "MAXIMIZE_SPEED=5",
      lpm_numwords           => 16,
      lpm_showahead          => "ON",
      lpm_type               => "dcfifo",
      lpm_width              => 17,
      lpm_widthu             => 4,
      overflow_checking      => "ON",
      rdsync_delaypipe       => 4,
      underflow_checking     => "ON",
      wrsync_delaypipe       => 4)
    PORT MAP (
      wrclk   => serdes_clk,
      rdreq   => s_txfifo_rdreq,
      aclr    => s_txfifo_aclr,
      rdclk   => ch0_rclk,
      wrreq   => serdes_tst_data(17),
      data    => serdes_tst_data(16 DOWNTO 0),
      rdempty => s_txfifo_empty,
      q       => s_txfifo_q
      );

  -- latch rx data into a single clock 17bit fifo for later comparison
  rxfifo : scfifo
    GENERIC MAP (
      add_ram_output_register => "ON",
      intended_device_family  => "Cyclone II",
      lpm_numwords            => 16,
      lpm_showahead           => "ON",
      lpm_type                => "scfifo",
      lpm_width               => 17,
      lpm_widthu              => 4,
      overflow_checking       => "ON",
      underflow_checking      => "ON",
      use_eab                 => "ON"
      )
    PORT MAP (
      rdreq => s_rxfifo_rdreq,
      aclr  => s_rxfifo_aclr,
      clock => ch0_rclk,
      wrreq => s_rxfifo_wrreq,
      data  => ch0_rxd(16 DOWNTO 0),
      empty => s_rxfifo_empty,
      q     => s_rxfifo_q
      );

  s_rxfifo_wrreq <= ch0_rxd(17) AND s_ch0_locked;
  s_txfifo_aclr  <= NOT s_ch0_locked;
  s_rxfifo_aclr  <= NOT s_ch0_locked;
--  s_ctr_aclr    <= NOT s_ch0_locked;

  -- now compare
  data_compare : PROCESS (serdes_clk, areset_n) IS
    VARIABLE b_ch0valid : boolean := false;
  BEGIN
    IF areset_n = '0' THEN              -- asynchronous reset (active low)
      s_errorctr     <= (OTHERS => '0');
      s_txfifo_rdreq <= '0';
      s_rxfifo_rdreq <= '0';
      
    ELSIF serdes_clk'event AND serdes_clk = '0' THEN  -- trailing clock edge
      s_txfifo_rdreq <= '0';
      s_rxfifo_rdreq <= '0';
      IF ((s_rxfifo_empty = '0') AND (s_txfifo_empty = '0')) THEN
        IF (s_rxfifo_q /= s_txfifo_q) THEN
          s_errorctr <= s_errorctr + 1;
        END IF;
        s_txfifo_rdreq <= '1';
        s_rxfifo_rdreq <= '1';
      END IF;
    END IF;
  END PROCESS data_compare;

  -----------------------------------------------------------------------------
  -- Rx FIFOs
  -----------------------------------------------------------------------------
  ch0fifo : dcfifo
    GENERIC MAP (
      intended_device_family => "Cyclone II",
      lpm_hint               => "MAXIMIZE_SPEED=5",
      lpm_numwords           => 4096,
      lpm_showahead          => "ON",
      lpm_type               => "dcfifo",
      lpm_width              => 16,
      lpm_widthu             => 12,
      overflow_checking      => "ON",
      rdsync_delaypipe       => 4,
      underflow_checking     => "ON",
      wrsync_delaypipe       => 4)
    PORT MAP (
      wrclk   => ch0_rclk,
      rdreq   => s_ch0fifo_rdreq,
      aclr    => s_ch0fifo_aclr,
      rdclk   => pll_80mhz,
      wrreq   => ch0_rxd(17),
      data    => ch0_rxd(15 DOWNTO 0),
      rdempty => s_ch0fifo_empty,
      q       => s_ch0fifo_q
      );

  s_ch0fifo_aclr <= NOT s_ch0_locked;

  ch1fifo : dcfifo
    GENERIC MAP (
      intended_device_family => "Cyclone II",
      lpm_hint               => "MAXIMIZE_SPEED=5",
      lpm_numwords           => 4096,
      lpm_showahead          => "ON",
      lpm_type               => "dcfifo",
      lpm_width              => 16,
      lpm_widthu             => 12,
      overflow_checking      => "ON",
      rdsync_delaypipe       => 4,
      underflow_checking     => "ON",
      wrsync_delaypipe       => 4)
    PORT MAP (
      wrclk   => ch1_rclk,
      rdreq   => s_ch1fifo_rdreq,
      aclr    => s_ch1fifo_aclr,
      rdclk   => pll_80mhz,
      wrreq   => ch1_rxd(17),
      data    => ch1_rxd(15 DOWNTO 0),
      rdempty => s_ch1fifo_empty,
      q       => s_ch1fifo_q
      );

  s_ch1fifo_aclr <= NOT s_ch1_locked;

  ch2fifo : dcfifo
    GENERIC MAP (
      intended_device_family => "Cyclone II",
      lpm_hint               => "MAXIMIZE_SPEED=5",
      lpm_numwords           => 4096,
      lpm_showahead          => "ON",
      lpm_type               => "dcfifo",
      lpm_width              => 16,
      lpm_widthu             => 12,
      overflow_checking      => "ON",
      rdsync_delaypipe       => 4,
      underflow_checking     => "ON",
      wrsync_delaypipe       => 4)
    PORT MAP (
      wrclk   => ch2_rclk,
      rdreq   => s_ch2fifo_rdreq,
      aclr    => s_ch2fifo_aclr,
      rdclk   => pll_80mhz,
      wrreq   => ch2_rxd(17),
      data    => ch2_rxd(15 DOWNTO 0),
      rdempty => s_ch2fifo_empty,
      q       => s_ch2fifo_q
      );

  s_ch2fifo_aclr <= NOT s_ch2_locked;

  ch3fifo : dcfifo
    GENERIC MAP (
      intended_device_family => "Cyclone II",
      lpm_hint               => "MAXIMIZE_SPEED=5",
      lpm_numwords           => 4096,
      lpm_showahead          => "ON",
      lpm_type               => "dcfifo",
      lpm_width              => 16,
      lpm_widthu             => 12,
      overflow_checking      => "ON",
      rdsync_delaypipe       => 4,
      underflow_checking     => "ON",
      wrsync_delaypipe       => 4)
    PORT MAP (
      wrclk   => ch3_rclk,
      rdreq   => s_ch3fifo_rdreq,
      aclr    => s_ch3fifo_aclr,
      rdclk   => pll_80mhz,
      wrreq   => ch3_rxd(17),
      data    => ch3_rxd(15 DOWNTO 0),
      rdempty => s_ch3fifo_empty,
      q       => s_ch3fifo_q
      );

  s_ch3fifo_aclr <= NOT s_ch3_locked;

  rdreq_decode : decoder PORT MAP (
    input_sig => s_smif_rdenable,
    adr       => s_smif_select,
    y(0)      => s_ch0fifo_rdreq,
    y(1)      => s_ch1fifo_rdreq,
    y(2)      => s_ch2fifo_rdreq,
    y(3)      => s_ch3fifo_rdreq);

  rxmux_inst : mux17x4 PORT MAP (
    data0x(15 DOWNTO 0) => s_ch0fifo_q,
    data0x(16)          => s_ch0fifo_empty,
    data1x(15 DOWNTO 0) => s_ch1fifo_q,
    data1x(16)          => s_ch1fifo_empty,
    data2x(15 DOWNTO 0) => s_ch2fifo_q,
    data2x(16)          => s_ch2fifo_empty,
    data3x(15 DOWNTO 0) => s_ch3fifo_q,
    data3x(16)          => s_ch3fifo_empty,
    sel                 => s_smif_select,
    result(15 DOWNTO 0) => s_smif_dataout,
    result(16)          => s_smif_fifo_empty);


  -----------------------------------------------------------------------------
  -- SERDES power on procedures
  -----------------------------------------------------------------------------
  
  poweron_ch0 : serdes_poweron PORT MAP (
    clk         => globalclk,
    tpwdn_n     => ch0_tpwdn_n,
    rpwdn_n     => ch0_rpwdn_n,
    sync        => ch0_sync,
    ch_lock_n   => ch0_lock_n,
    ch_ready    => s_ch0_locked,
    pll_locked  => pll_locked,
    rxd         => ch0_rxd,
    serdes_data => serdes_data,
    txd         => s_ch0_txd,
    areset_n    => s_serdes_reg(0));


  poweron_ch1 : serdes_poweron PORT MAP (
    clk         => globalclk,
    tpwdn_n     => ch1_tpwdn_n,
    rpwdn_n     => ch1_rpwdn_n,
    sync        => ch1_sync,
    ch_lock_n   => ch1_lock_n,
    ch_ready    => s_ch1_locked,
    pll_locked  => pll_locked,
    rxd         => ch1_rxd,
    serdes_data => serdes_data,
    txd         => s_ch1_txd,
    areset_n    => s_serdes_reg(1));

  poweron_ch2 : serdes_poweron PORT MAP (
    clk         => globalclk,
    tpwdn_n     => ch2_tpwdn_n,
    rpwdn_n     => ch2_rpwdn_n,
    sync        => ch2_sync,
    ch_lock_n   => ch2_lock_n,
    ch_ready    => s_ch2_locked,
    pll_locked  => pll_locked,
    rxd         => ch2_rxd,
    serdes_data => serdes_data,
    txd         => s_ch2_txd,
    areset_n    => s_serdes_reg(2));

  poweron_ch3 : serdes_poweron PORT MAP (
    clk         => globalclk,
    tpwdn_n     => ch3_tpwdn_n,
    rpwdn_n     => ch3_rpwdn_n,
    sync        => ch3_sync,
    ch_lock_n   => ch3_lock_n,
    ch_ready    => s_ch3_locked,
    pll_locked  => pll_locked,
    rxd         => ch3_rxd,
    serdes_data => serdes_data,
    txd         => s_ch3_txd,
    areset_n    => s_serdes_reg(3));

  -----------------------------------------------------------------------------
  -- SRAM control
  -----------------------------------------------------------------------------

--  zbt_ctrl_top_inst1 : zbt_ctrl_top
--    PORT MAP (
--      clk                   => pll_160mhz_p,
--      RESET_N               => pll_locked,
---- local bus interface
--      ADDR(15 DOWNTO 0)     => ma(15 DOWNTO 0),
--      ADDR(18 DOWNTO 16)    => (OTHERS => '0'),
--      DATA_IN(17 DOWNTO 0)  => ma(33 DOWNTO 16),
--      DATA_IN(31 DOWNTO 18) => (OTHERS => '0'),
--      DATA_OUT              => s_sram_dataout,
--      RD_WR_N               => s_rw_n,
--      ADDR_ADV_LD_N         => s_addr_adv_ld_n,
--      DM                    => s_dm,
---- SRAM interface
--      SA                    => srb_addr,     -- SRAM B
--      DQ                    => srb_d,
--      RW_N                  => srb_rw,
--      ADV_LD_N              => srb_adv,
--      BW_N                  => srb_bw
--      -- SA                    => sra_addr,  -- SRAM A
--      -- DQ                    => sra_d,
--      -- RW_N                  => sra_rw,
--      -- ADV_LD_N              => sra_adv,
--      -- BW_N                  => sra_bw
--      );

--  mt(15 DOWNTO 0) <= s_sram_dataout(15 DOWNTO 0);

--  sram_sm : PROCESS (pll_160mhz_p, pll_locked) IS
--  BEGIN  -- PROCESS uc_fpga_sm
--    IF pll_locked = '0' THEN                            -- asynchronous reset (active low)
--      state           <= State0;
--      s_dm            <= (OTHERS => '0');
--      s_rw_n          <= '0';
--      s_addr_adv_ld_n <= '1';
--    ELSIF pll_160mhz_p'event AND pll_160mhz_p = '1' THEN  -- rising clock edge
--      s_dm            <= (OTHERS => '0');
--      s_rw_n          <= '0';
--      s_addr_adv_ld_n <= '1';
--      CASE state IS
-- WHEN State0 =>
--          IF ma(35) = '1' THEN
--            state <= State1;
--          END IF;
-- WHEN State1 =>
--          s_addr_adv_ld_n <= '0';
--          s_rw_n          <= ma(34);
--          s_dm            <= (OTHERS => '1');
--          state           <= State1a;
-- WHEN State1a =>                                 -- this state is for testing of the waveforms only
--          s_addr_adv_ld_n <= '0';
--          s_rw_n          <= ma(34);
--          s_dm            <= (OTHERS => '1');
--          state           <= State2;
-- WHEN State2 =>
--          s_addr_adv_ld_n <= '0';
--          state           <= State3;
-- WHEN State3 =>
--          IF ma(35) = '0' THEN
--            state <= State0;
--          END IF;
--      END CASE;
--    END IF;
--  END PROCESS sram_sm;

END a;
