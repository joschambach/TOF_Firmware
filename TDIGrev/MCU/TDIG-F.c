// $Id: TDIG-F.c,v 1.21 2010-07-19 15:47:47 jschamba Exp $

// TDIG-F.c
/*
** These SBIR data are furnished with SBIR/STTR rights under Grant No. DE-FG03-02ER83373 and
** BNL Contract No. 79217.  For a period of 4 years after acceptance of all items delivered
** under this Grant, the Government, BNL and Rice agree to use these data for the following
** purposes only: Government purposes, research purposes, research publication purposes,
** research presentation purposes and for purposes of Rice to fulfill its obligations to
** provide deliverables to BNL and DOE under the Prime Award; and they shall not be disclosed
** outside the Government, BNL or Rice (including disclosure for procurement purposes) during
** such period without permission of Blue Sky, LLC except that, subject to the foregoing use
** and disclosure prohibitions, such data may be disclosed for use by support contractors.
** After the aforesaid 4-year period the Government has a royalty-free license to use, and to
** authorize others to use on its behalf, these data for Government purposes, and the
** Government, BNL and Rice shall be relieved of all disclosure prohibitions and have no
** liability for unauthorized use of these data by third parties.
** This Notice shall be affixed to any reproductions of these data in whole or in part.
*/

// main program for PIC24HJxxGP506 as used on TDIG-D, E, F boards

// This is now done with a compiler definition, so no longer 
// necessary to define here:
//JS: Uncomment this, if this is to be donwloaded via CANbus
//JS	#define DOWNLOAD_CODE

// Define the FIRMWARE ID
#define FIRMWARE_ID_0 'B'      // 0x12 0x42
// WB-11H make downloaded version have different ID
#ifdef DOWNLOAD_CODE
    #define FIRMWARE_ID_1 0x92
#else
    #define FIRMWARE_ID_1 0x12
#endif

// Define implementation on the TDIG board (I/O ports, etc)
#define TDIG 1              // This is a TDIG board.
#define CONFIG_CPU 1		// Make TDIG-F_Board.h define the CPU options
#include "TDIG-F_Board.h"

#define RC15_TOGGLE 1		// Make a toggle bit on RC15/OSC2

// Define the library includes
#include "ecan.h"           // Include for E-CAN peripheral
#include "i2c.h"            // Include for I2C peripheral library
#include "stddef.h"         // Standard definitions
#include "string.h"         // Definitions for string functions

// Define our routine includes
#include "rtspApi.h"		// Definitions of reprogramming routines
#include "TDIG-F_I2C.h"		// Include prototypes for our I2C routines
#include "TDIG-F_JTAG.h"    // Include for our JTAG macros
#include "TDIG-F_SPI.h"     // Include for our SPI (EEPROM) macros
#include "TDIG-F_MCU_PLD.h" // Include for our parallel interface to FPGA

/* define the CAN HLP Packet Format and function codes */
#include "CAN_HLP3.h"       // WB-11U: Use consolidated file

/* This conditional determines whether to actually send CAN packets */
#define SENDCAN 1           // Define to send

// Define the cycle count for toggling TDC Power.
// Button S1 must be pressed for BUTTONLIMIT trips thru main loop before
// ENABLE_TDC_POWER bit is toggled
#define BUTTONLIMIT 6

// Spin Counter
#define SPINLIMIT 20

/* ECAN1 stuff */
#define NBR_ECAN_BUFFERS 4

typedef unsigned int ECAN1MSGBUF [NBR_ECAN_BUFFERS][8];
ECAN1MSGBUF  ecan1msgBuf __attribute__((space(dma)));  // Buffer to TRANSMIT

void ecan1Init(unsigned int board_id);
void dma0Init(void);
void dma2Init(void);

// Routines Defined in this module:
// MCU configuration / control
int Initialize_OSC(unsigned int oscsel);              // initialize the CPU oscillator
void Switch_OSC(unsigned int mcuoscsel);               // Switch CPU oscillator (low level)
void spin(int cycle);		// delay
void clearIntrflags(void);	// Clear interrupt flags
// CAN message routines
// Send a CAN message - fills in     board id               message type  number of payload bytes    &payload[0]
void send_CAN1_message (unsigned int board_id, unsigned int message_type, unsigned int bytes, unsigned char *bp);
// void send_CAN1_alert   (unsigned int board_id);
// void send_CAN1_diagnostic (unsigned int board_id, unsigned int bytes, unsigned char *buf);
// void send_CAN1_hptdcmismatch (unsigned int board_id, unsigned int tdcno,
//                            unsigned int index, unsigned char expectedbyte, unsigned char gotbyte);
void send_CAN1_data (unsigned int board_id, unsigned int bytes, unsigned char *bp);

/* MCU Memory and Reprogramming */
typedef unsigned short UWord16;
typedef unsigned long  UWord32;
typedef union tureg32 {
    UWord32 Val32;         // 32 bit value
    struct {
        UWord16 LW;         // 16 bit value lower
        UWord16 HW;         // 16 bit value upper
    } Word;
    unsigned char Val[4];            // array of chars
} UReg32;

UReg32 temp;

void writePMRow(unsigned char *, unsigned long);
void writeConfRegByte(unsigned char, unsigned char);

void read_MCU_pm (unsigned char *, unsigned long);
void write_MCU_pm (unsigned char *, unsigned long);
//JS20090821 void erase_MCU_pm (unsigned long);
// Dummy routine defined here to allow us to run the "alternate" code (downloaded)
//void __attribute__((__noreturn__, __weak__, __noload__, address(MCU2ADDRESS) )) jumpto(void);

// WB-11U: - Prototypes for A/D Converter Routines
void initialize_ad_converter(void);
unsigned int sample_ad_converter(unsigned int chan);
// WB-11U: end

//JS20090821: had to move the addresses up by 0x400, since linker complained about not
// being able to allocate address at 0x3000 (??). Program too big?

// WB-11P: - Weird looking braces make the compiler happier (eliminates "missing braces" warning).
//JS: put HPTDC setup bits into program memory, three sets of configurations
//#define PM_ROW	__attribute__((space(prog), aligned(1024)))
//unsigned char PM_ROW basic_setup_pm[NBR_HPTDCS][J_HPTDC_SETUPBYTES] = {

//#if !defined (DOWNLOAD_CODE)
//    unsigned char __attribute__((space(prog), aligned(1024), address(0x003400)))
//#else
//    unsigned char __attribute__((space(prog), aligned(1024), address(0x007400)))
//#endif
unsigned char __attribute__((__section__(".hptdc_config"), space(prog)))
    basic_setup_pm[NBR_HPTDCS][J_HPTDC_SETUPBYTES] = {
        {
            #include "HPTDC.inc"	// TDC 1
	    },
        {
            #include "HPTDC.inc"	// TDC 2
	    },
        {
            #include "HPTDC.inc"	// TDC 3
        }
    };

/* WB-11P - Weird looking braces make the compiler happier (eliminates "missing braces" warning).
 *  Bring in conditional addresses from Jo's download version
 */
//unsigned char PM_ROW enable_final_pm [NBR_HPTDCS][J_HPTDC_CONTROLBYTES] = {
//#if !defined (DOWNLOAD_CODE)
//    unsigned char __attribute__((space(prog), aligned(1024), address(0x003800)))
//#else
//    unsigned char __attribute__((space(prog), aligned(1024), address(0x007800)))
//#endif
unsigned char __attribute__((__section__(".hptdc_ctrl"), space(prog)))
    enable_final_pm [NBR_HPTDCS][J_HPTDC_CONTROLBYTES] = {
        {
            #include "HPTDC_ctrl.inc"	// TDC 1
	    },
        {
            #include "HPTDC_ctrl.inc"	// TDC 2
        },
        {
            #include "HPTDC_ctrl.inc"	// TDC 3
        }
    };

unsigned char basic_setup [NBR_HPTDCS][J_HPTDC_SETUPBYTES];
unsigned char enable_final [NBR_HPTDCS][J_HPTDC_CONTROLBYTES];

unsigned char readback_control[J_HPTDC_CONTROLBYTES];
unsigned char readback_setup  [J_HPTDC_SETUPBYTES];

unsigned char hptdc_setup  [NBR_HPTDCS+1][J_HPTDC_SETUPBYTES];
unsigned char hptdc_control[NBR_HPTDCS+1][J_HPTDC_CONTROLBYTES];

unsigned char readback_buffer[2048];        // readback general buffer

// Large-Block Download Buffer
#define BLOCK_BUFFERSIZE 256                // NEVER MAKE THIS LESS THAN 8
unsigned char block_buffer [BLOCK_BUFFERSIZE];

unsigned int block_bytecount;
unsigned int block_status;
unsigned long int block_checksum;
unsigned long int eeprom_address;

// WB-11W CAN1 error flag
unsigned int can1error = 0;      // mark for no CAN1 error condition

// Current DAC setting
unsigned int current_dac;

unsigned int pld_ident; // will get identification byte from PLD
unsigned int board_posn = 0;    // Gets board-position from switch
unsigned int clock_status;      // will get identification from Clock switch/select (OSCCON)
unsigned int clock_failed = 0;      // will get set by clock fail interrupt
unsigned int clock_requested;   // will get requested clock ID
                                // 00 = board, 01= PLL, 08= tray
// WB-11V: - Temperature alert limits (WB-11X)
unsigned int mcu_hilimit=0;   // gets upper limit setting for MCU temperature
unsigned int mcu_lolimit=0;   // gets hysteresis setting for MCU temperature
unsigned int tino1_limit=0;   // gets limit setting for TINO1 value
unsigned int tino2_limit=0;   // gets limit setting for TINO2 value
unsigned int temp_alert=0;    // signals necessity for sending alert message
unsigned int temp_alert_throttle=0; // delays successive temperature alert messges
// WB-11V: end

// WB-11U: - A/D converter buffers - Global so they can be filled-in by interrupt routine
unsigned int tino1;      // gets channel digitized value
unsigned int tino2;      // gets channel digitized value
// WB-11U: end

#ifndef DOWNLOAD_CODE
unsigned int timerExpired = 0;
#endif

int main()
{
    unsigned long int lwork;
    unsigned long int lwork2;
    unsigned long int lwork3;
   	unsigned long int laddrs;
    unsigned int save_SR;           // image of status register while we block interrupts
    unsigned int i, j, k, l;        // working indexes
	unsigned int tglbit = 0x0;
	unsigned int switches = 0x0;
	unsigned int jumpers = 0x0;
//	unsigned int buttoncount = 0x0;
    unsigned int ecsrwbits = 0x0;
	unsigned int ledbits = NO_LEDS;
    unsigned int fpga_crc = 0;      // will get image of CRC Error bit from ECSR (assume no error to start)
	unsigned int board_temp = 0;	// will get last-read board temperature word
    unsigned int replylength;       // will get length of reply message
    unsigned char bwork[10];     // scratchpad
    unsigned char sendbuf[10], retbuf[10];
    unsigned char maskoff;      // working reg used for masking bits
    unsigned char *wps;              // working pointer
    unsigned char *wpd;              // working pointer

	//JS
	int isConfiguring = 0;
	int configuredEeprom = 1;
	int bSendAlarms = 1;

// This applies to first-image code, does not apply to second "download" image.
#if !defined (DOWNLOAD_CODE)
// Configure the oscillator for MCU internal (until we get going)
    Initialize_OSC(OSCSEL_FRCPLL);          // initialize the CPU oscillator to on-chip

// be sure we are running from standard interrupt vector
    save_SR = INTCON2;
    save_SR &= 0x7FFF;  // clear the ALTIVT bit
    INTCON2 = save_SR;  // and restore it.
#else
// This applies to second-image code.
// Note that ALTIVT bit gets set just prior to starting the second image, but just to be sure
    INTCON2 |= 0x8000;      // This is the ALTIVT bit
#endif

// We will want to run at priority 0 mostly
    SR &= 0x011F;          // Lower CPU priority to allow interrupts
    CORCONbits.IPL3=0;     // Lower CPU priority to allow user interrupts

//JS: watchdog timer: turn it off for initialization
	_SWDTEN = 0;

#if defined (RC15_IO) // RC15 will be I/O (in TDIG-D-Board.h)
	TRISC = 0x7FFF;		// make RC15 an output
#else
//  RC15/OSC2 is set up to output TCY clock on pin 40 = CLK_DIV2_OUT, TP1
#endif

/* 03-Jan-2007
** Initialize PORTD bits[4..9] pins [52, 53, 54, 55, 42, 43]
** for control of JTAG and EEPROM
*/
// Make D0 an output (pin 46 = MCU_TDC_TDI) initialize 0
// Make D1 an input  (pin 49 = MCU_TDC_TDO)
// Make D2 an output (pin 50 = MCU_TDC_TCK) initialize 0
// Make D3 an output (pin 51 = MCU_TDC_TMS) initialize 0
// Make D4 an input  (pin 52 = MCU_EE_DATA)
// Make D5 an output (pin 53 = MCU_EE_DCLK) initialize 0
// Make D6 an output (pin 54 = MCU_EE_ASDO) initialize 0
// Make D7 an output (pin 55 = MCU_EE_NCS)  initialize 1
// Make D8 an output (pin 42 = MCU_SEL_EE2) initialize 0
// Make D9 an output (pin 43 = MCU_CONFIG_PLD)  initialize 1
	LATD  = (0xFFFF & MCU_EE_initial & MCU_TDC_initial); // Initial bits
    TRISD = (0xFFFF & MCU_EE_dirmask & MCU_TDC_dirmask); // I/O configuration

/* this gives the following configuration
    MCU_EE_DCLK = 0;
    MCU_EE_ASDO = 0;
    MCU_EE_NCS = 1;
    MCU_SEL_EE2 = 0;
    MCU_CONFIG_PLD = 1;
*/
/* Port G bits used for various control functions
** Pin Port.Bit Dir'n Initial Signal Name
**   1    G.15  Out     1     MCU_TEST
**  62    G.14  Out     1     PLD_RESETB
**  64    G.13  Out     0     USB_RESETB
**  63    G.12  Out     1     PLD_DEVOE
**   8    G.9   Out     0     MCU_SEL_TERM
**   6    G.8   Out     1     MCU_SEL_LOCAL_OSC
**   5    G.7   Out     1     MCU_EN_LOCAL_OSC
**   4    G.6   Out     1     I2CA_RESETB
*/
    LATG = PORTG_initial;       // Initial settings port G (I2CA_RESETB must be Hi)
    TRISG = PORTG_dirmask;      // Directions port G
    MCU_SEL_TERM = 0;           // CAN terminator OFF

/* Initialize Port F bits for UCONFIG_IN, DCONFIG_OUT */
    TRISF = PORTF_dirmask;      // bit 3 is output
    DCONFIG_OUT = UCONFIG_IN;   // Bit 2 copied to output

/* Initialize Port B bits for output */
    AD1PCFGH = ANALOG1716;  // ENABLE analog 17, 16 only (RC1 pin 2, RC2 pin 3)
    AD1PCFGL = ALLDIGITAL;  // Disable Analog function from B-Port Bits 15..0

    LATB = 0x0000;          // All zeroes
    TRISB = 0xDFE0;         // Set directions

// Clear all interrupts
	clearIntrflags();

// Set up the I2C routines
	I2C_Setup();

    I2CA_RESETB = 0;      // Lower I2CA_RESETB
	spin(SPINLIMIT);		// wait a while
    I2CA_RESETB = 1;      // Raise I2CA_RESETB

// Initialize and Set Threshhold DAC
#if defined (DAC_ADDR)	// if address defined, it exists
    current_dac = DAC_INITIAL;	// defined in TDIG-F_Board.h
    current_dac = Write_DAC((unsigned char *)&current_dac);       // Set it to 2500 mV
#endif // defined (DAC_ADDR)

// Initialize Extended CSR (to PLD, etc)
	Initialize_ECSR();		// I2C == write4C = 00 00

// Initialize and Read Switches, button, and Jumpers
	Initialize_Switches();  // I2C == write22 = 00 9F etc.
// Get board-position switch
	switches = Read_MCP23008(SWCH_ADDR, MCP23008_GPIO);
    board_posn = (switches & BOARDSW4_MASK)>>BOARDSW4_SHIFT;   // position 0..7
// IF ECO14_SW4 is defined, we need to compensate for the bit0<-->bit2 swap error
#if defined (ECO14_SW4)
	j = board_posn & 2;	// save the center bit
	if ((board_posn&1)==1) j |= 4;	// fix the 4 bit
	if ((board_posn&4)==4) j |= 1;	// fix the 1 bit
	board_posn = j;
#endif // defined (ECO14_SW4)

// Initialize and Turn Off LEDs
    Initialize_LEDS();

// Initialize and Read Serial Number
#if defined (SN_ADDR) // if address defined, it exists
    Write_device_I2C1 (SN_ADDR, CM00_CTRL, CM00_CTRL_I2C);     // set I2C mode
#endif
    for (j=0; j<8; j++) {
#if defined (SN_ADDR)
        bwork[j] = (unsigned char)Read_MCP23008(SN_ADDR,j);        // go get sn byte
#else
        bwork[j] = j;
#endif
//      Briefly display each byte of Serial Number on LEDs
        Write_device_I2C1 (LED_ADDR, MCP23008_OLAT, (unsigned int)(bwork[j]^0xFF)); //
//      spin (SPINLIMIT);
    }

// Initialize and Read Temperature Monitor
#if defined (TMPR_ADDR)
//    Initialize_Temp (MCP9801_CFGR_RES12);   // configure 12 bit resolution
    Initialize_Temp (MCP9801_CFGR_RES12|MCP9801_CFGR_FQUE4);   // configure 12 bit resolution, fault Queue=4
    board_temp = Read_Temp ();
    j = board_temp ^ 0xFFFF;        // flip bits for LED
    Write_device_I2C1 (LED_ADDR, MCP23008_OLAT, (j&0xFF)); // display LSByte
    Write_device_I2C1 (LED_ADDR, MCP23008_OLAT, ((j>>8)&0xFF));
#endif // defined (TMPR_ADDR)

/* -------------------------------------------------------------------------------------------------------------- */
	switches = Read_MCP23008(SWCH_ADDR, MCP23008_GPIO);
    jumpers = (switches & JUMPER_MASK)>>JUMPER_SHIFT;

/* -----------------12/9/2006 11:39AM----------------
** Jumper JU2.1-2 now controls MCU_SEL_LOCAL_OSC
** and MCU_EN_LOCAL_OSC
** Installing the jumper forces low on MCU_...OSC
** disabling it.
 --------------------------------------------------*/
    if ( (jumpers & JUMPER_1_2) == JUMPER_1_2) { // See if jumper IN 1-2
                                        // Jumper INSTALLED inhibits local osc.
        spin(50);   // JS: Wait a little for external clock to be valid, should be at least 50
                    // WB: Lengthened from 40 to 45 per BNL_20080809 version)
					// JS: Lengthened to 50
        Initialize_OSC (OSCSEL_TRAY);       //  Use TRAY clock
    } else {                        // Jumper OUT (use local osc)
        Initialize_OSC (OSCSEL_BOARD);       //  Use BOARD clock
    }                               // end else turn ON local osc
    clock_status = OSCCON;     // read the OSCCON reg - returns clock status

// Delay power-on by 2 second + 1 second x board-position switch value
//JS: make this a little faster: 1/4 second + 1/4 second x board-position switch value
//JS #define DELAYPOWER 36
#define DELAYPOWER 9

#ifndef DOWNLOAD_CODE
#if defined (DELAYPOWER)
    //JS j = board_posn + 2;
    j = board_posn + 1;
    while ( j != 0 ) {
        spin(DELAYPOWER);               // delay approx 1 second per 36 counts
        --j;
    }                                   // end while spinning 1/3 sec per board posn
#endif                                  // DELAYPOWER conditional
#endif

// Turn on power to HPTDC
    ecsrwbits |= ECSR_TDC_POWER;      // Write the TDC Power-ON bit
    Write_device_I2C1 (ECSR_ADDR, MCP23008_OLAT, ecsrwbits);
//		TDC Power is being turned on, delay a while
    ledbits = 0x7F;		// turn on orange LED
    Write_device_I2C1 (LED_ADDR, MCP23008_OLAT, (jumpers^ledbits) );

    spin(5);

#define INIT_FROM_EEPROM2 1 // define it
#ifdef INIT_FROM_EEPROM2
	//JS:  try to initialize FPGA from EEPROM 2
    i = 2;
	// loop here if it failed the first time
    do {
    	MCU_CONFIG_PLD = 0; // disable FPGA configuration
    	if (i==2) {
    		sel_EE2;        // select EEPROM #2
    	} else {
			sel_EE1;        // else it was #1
        } // end if select EEPROM #
        set_EENCS;      //
        MCU_CONFIG_PLD = 1; // re-enable FPGA
        j = waitfor_FPGA();     // wait for FPGA to reconfigure
        if (j != 0) {
			i--;        // try again from #1
        } // end if had timeout
	} while ((i != 0) && (j != 0));       // try til either both were used or no error
	configuredEeprom = i;

#else
// Make sure FPGA has configured and reset it
    waitfor_FPGA();
#endif

// reset FPGA  and initialize FPGA registers
    reset_FPGA();
    init_regs_FPGA(board_posn);

/* -------------------------------------------------------------------------------------------------------------- */
/* ECAN1 Initialization
   Configure DMA Channel 0 for ECAN1 Transmit (buffers[0] )
   Configure DMA Channel 2 for ECAN1 Receive  (buffers[1] )
*/
    ecan1Init(board_posn);
    dma0Init();                     // defined in ECAN1Config.c (copied here)
    dma2Init();                     // defined in ECAN1Config.c (copied here)

/* Enable ECAN1 Interrupt */
    IEC2bits.C1IE = 1;              // Interrupt Enable ints from ECAN1
    C1INTEbits.TBIE = 1;            // ECAN1 Transmit Buffer Interrupt Enable
    C1INTEbits.RBIE = 1;            // ECAN1 Receive  Buffer Interrupt Enable
    C1INTEbits.RBOVIE = 1;          // WB-11W: ECAN1 Receive  Buffer Overflow Interrupt Enable
    C1INTEbits.ERRIE  = 1;          // WB-11W: ECAN1 Error    Interrupt Enable

/* Initialize large-block download */
    block_status = BLOCK_NOTSTARTED;    // no block transfer started yet
    block_bytecount = 0;
    block_checksum = 0L;

/* Configure the HPTDCs */
	unsigned int nvmAdru, nvmAdr;
	int temp;

	// first retrieve the configuration bits from program memory
	nvmAdru=__builtin_tblpage(basic_setup_pm);
	nvmAdr=__builtin_tbloffset(basic_setup_pm);
	// Read the page and place the data into readback_buffer array
	temp = flashPageRead(nvmAdru, nvmAdr, (int *)readback_buffer);
	memcpy ((unsigned char *)basic_setup, (unsigned char *)readback_buffer, J_HPTDC_SETUPBYTES*NBR_HPTDCS);

	// retrieve control bits from program memory
	nvmAdru=__builtin_tblpage(enable_final_pm);
	nvmAdr=__builtin_tbloffset(enable_final_pm);
	temp = flashPageRead(nvmAdru, nvmAdr, (int *)readback_buffer);
	memcpy ((unsigned char *)enable_final, (unsigned char *)readback_buffer, J_HPTDC_CONTROLBYTES*NBR_HPTDCS);

    for (j=1; j<=NBR_HPTDCS; j++) {
        select_hptdc(JTAG_MCU, j);        // select MCU controlling which HPTDC
        // Copy "base" initialization to working inititalization
        memcpy (&hptdc_setup[j][0], &basic_setup[j-1][0], J_HPTDC_SETUPBYTES);
        hptdc_setup[j][5] &= 0xFFF0;    // clear old TDC ID value
	    // Fix up HPTDC ID byte in working initialization (Revised method)
        // board_posn 0,4 have TDCs 0x0,0x1,0x2
        // board_posn 1,5 have TDCs 0x4,0x5,0x6
        // board_posn 2,6 have TDCs 0x8,0x9,0xA
        // board_posn 3,7 have TDCs 0xC,0xD,0xE
        // ((lo 2 bits of board posn) << 2 bits) or'd with (lo 2 bits of (index-1))
        hptdc_setup[j][5] |= ((board_posn&0x3)<<2) | ((j-1)&0x3); // compute and insert new value
#ifdef LOCAL_HEADER_BOARD0
		if (((board_posn&0x3) == 0) && (j == 1)) {
			hptdc_setup[j][4] |= 0x1; // turn on local header for board 0 and 4, TDC 1
		}
#endif

        // Fix up Parity bit in working initialization
		insert_parity (&hptdc_setup[j][0], J_HPTDC_SETUPBITS);
        write_hptdc_setup (j, (unsigned char *)&hptdc_setup[j][0], (unsigned char *)readback_setup);
        // check for match between working and read-back initialization
        maskoff = 0xFF;
        for (i=0; i<J_HPTDC_SETUPBYTES;i++){ // checking readback
            if (i == (J_HPTDC_SETUPBYTES-1)) maskoff = 0x7F;     // don't need last bit!
            if ((unsigned char)hptdc_setup[j][i] != (((unsigned char)readback_setup[i])&maskoff)) {
                if ((ledbits & 0x10) != 0) {
                    ledbits &= 0xEF;    //
                    Write_device_I2C1 (LED_ADDR, MCP23008_OLAT, ledbits);
                    // set LED 4 if there was an error
                    // send a mismatch CAN error
                } // end if have not already seen 1 error
            } // end if got a mismatch
        } // end loop over checking readback
        // LED 4 SET if there was an error
        reset_hptdc (j, &enable_final[j-1][0], &hptdc_control[j][0]);    // JTAG the hptdc reset sequence (WB-11X save control word)
    } // end loop over all NBR_HPTDCS(3)

// If no error, turn on LED 6
    if ((ledbits & 0x10) != 0) {    // bit 4 is hi (led OFF) if no mismatch error issued
        ledbits &= 0xBF;    //
        Write_device_I2C1 (LED_ADDR, MCP23008_OLAT, ledbits);
    } // end if no error
    spin(0);

// Lo jumpers select TDC for JTAG using CONFIG_1 register in FPGA
// Re-enabled and moved to before ALERT message 08-Mar-07
    select_hptdc(JTAG_HDR,(jumpers&0x3));        // select HEADER (J15) controlling which HPTDC
    ledbits = (ledbits|0x0F) ^ (jumpers & 0x0F);    //
    Write_device_I2C1 (LED_ADDR, MCP23008_OLAT, ledbits);

    spin(0);           // spin loop

    j = 0;

// Do an HPTDC Reset
    write_FPGA (CONFIG_2_RW, ~CONFIG_2_TDCRESET); // HPTDC_RESET = 0;  // lower HPTDC Reset bit
    write_FPGA (CONFIG_2_RW,  CONFIG_2_TDCRESET); // HPTDC_RESET = 1; // raise "reset" bit
	spin(0);
    write_FPGA (CONFIG_2_RW, ~CONFIG_2_TDCRESET); // HPTDC_RESET = 0; // Lower RESET bit

/* -------------------------------------------------------------------------------------------------------------- */
	switches = Read_MCP23008(SWCH_ADDR, MCP23008_GPIO);
    jumpers = (switches & JUMPER_MASK)>>JUMPER_SHIFT;

// Lo jumpers select TDC for JTAG using CONFIG_1 register in FPGA
    select_hptdc(JTAG_HDR,(jumpers&0x3));        // select HEADER (J15) controlling which HPTDC
    spin(5);           // spin loop

/* WB-11U: Initialize A/D Converter module */
    initialize_ad_converter();

/* Send an "Alert" message to say we are on-line */
      retbuf[0] = 0xFF;
      retbuf[1] = 0x0;
      retbuf[2] = 0x0;
	
      if (configuredEeprom == 2) 
	      retbuf[3] = 0x0; // FPGA EEPROM2 was loaded
	  else
		  retbuf[3] = 0xff;	// FPGA EEPROM1 was loaded

	  // if the  reset was due to Watchdog Timeout:
	  if (_WDTO) {
		  _WDTO = 0;
		  retbuf[3] &= 0xef;
      }

	  // tray clock didn't configure properly:
      if (clock_status != 0x2200) {
        memcpy ((unsigned char *)&retbuf[1], (unsigned char *)&clock_status, 2);
      }

      send_CAN1_message (board_posn, (C_BOARD | C_ALERT), 4, (unsigned char *)&retbuf);

/* Send a "Diagnostic" message to show which OSC we think we are running */
//    send_CAN1_diagnostic (board_posn, 2, (unsigned char *)&clock_status);

// WB-11U: Changed to use Timer 6 and 7 so Timer2/3 is available for A/D converter
#ifndef DOWNLOAD_CODE
/* setup timer: Combine Timer 6 and 7 for a 32 bit timer;
    combined timer is controlled by Timer 6 control bits,
    but fires Timer 7 interrupt  */
	timerExpired = 0;		// global flag to indicate expired timer 1
    T6CON = 0;              // clear Timer 6 control register
    T7CON = 0;              // clear Timer 7 control register
    T2CONbits.TCKPS = 0b11; // Set prescale to 1:256
    TMR6 = 0;               // clear Timer 6 timer register
    TMR7 = 0;               // clear Timer 7 timer register
    PR6 = 0xffff;           // load period register (low 16 bits)
    PR7 = 0x0004;           // load period register (high 16 bits)
    IPC12bits.T7IP0 = 1;    // Timer 7 Interrupt priority = 1
    IPC12bits.T7IP1 = 0;
    IPC12bits.T7IP2 = 0;

    IFS3bits.T7IF = 0;      // clear interrupt status flag Timer 7
    IEC3bits.T7IE = 1;      // enable Timer 7 interrupt

    T6CONbits.T32 = 1;      // Enable 32-bit timer operation
    T6CONbits.TON = 1;      // Turn on Timer 2
#endif
// WB-11U: end timer assignment changes

	//JS: watchdog timer: turn it on
	_SWDTEN = 1;

//***************** MAIN LOOP ******************************************
/* Look for Have-a-Message */
    do {                            // Do Forever

		//JS: watchdog timer: reset WDT
		ClrWdt();


/* WB-11V: multiple buffers to support broadcast messages
 * Buffer[1][] is for "our address received message"
 * Buffer[2][] is for "broadcast address received messge"
 * Reworked the way buffers are pointed to from here on
 */
        unsigned int rcvmsgindex = 0;
        if ( C1RXFUL1bits.RXFUL1 ) {
            rcvmsgindex = 1;
        } else if ( C1RXFUL1bits.RXFUL2 ) {
            rcvmsgindex = 2;
        }
        if ( rcvmsgindex != 0 ) {
            unsigned int rcvmsglen = ecan1msgBuf[rcvmsgindex][2] & 0x000F; //JS WB-11V
            wps = (unsigned char *)&ecan1msgBuf[rcvmsgindex][3];   // point to message text (source) WB-11V
// Dispatch to Message Code handlers.
// Note that Function Code symbolics are defined already shifted.
            retbuf[0] = ecan1msgBuf[rcvmsgindex][3];  // pre-fill reply payload[0] with "subcommand"
            retbuf[1] = C_STATUS_OK;            // Assume all is well payload[1] (status OK)
            switch ((ecan1msgBuf[rcvmsgindex][0] & C_CODE_MASK)) {  // Major switch on WRITE or READ COMMAND
                case C_WRITE:  // Process a "Write"
                    replylength = 2;                    // Assume 2 byte reply for Writes
                    // now decode the "Write-To" Subcommand from inside message
                    switch ((*wps++)&0xFF) {       // look at and dispatch SUB-command, point to remainder of message

// WB-11V: Add temperature alert limit setting.
// NOTE that temperature alert setting in chip ignores the lower 7-bits of the value (always reads back 0).
                        case C_WS_TEMPALERTS:
                            if (rcvmsglen == TEMPALERTS_LEN) {   // check length for proper value
                                memcpy ((unsigned char *)&mcu_hilimit, wps, 2);   // copy 2 bytes from incoming message
                                wps += 2;
                                memcpy ((unsigned char *)&tino1_limit, wps, 2);   // copy 2 bytes from incoming message
                                wps += 2;
                                memcpy ((unsigned char *)&tino2_limit, wps, 2);   // copy 2 bytes from incoming message
                                wps += 2;
                                temp_alert = 0;         // clear any temperature alert status
                                // Initialize the MCP9801 temperature chip (U37) registers
                                mcu_lolimit = 0;
                                if (mcu_hilimit > 0x500) mcu_lolimit = mcu_hilimit - 0x500;       // lower limit is upper limit - 5 degrees C.
                                Write16_Temp(MCP9801_LIMT, mcu_hilimit); // Set upper limit
                                Write16_Temp(MCP9801_HYST, mcu_lolimit); // Set upper limit
                            } else retbuf[1] = C_STATUS_INVALID;    // else mark invalid
                            break;
// WB-11V: End Add temperature alert limit setting

                        case C_WS_LED:              // Write to LED register
                            Write_device_I2C1 (LED_ADDR, MCP23008_OLAT, ~(*wps));
                            break;

                        case C_WS_SEND_ALARM:		// Set bSendAlarms variable
                            if (rcvmsglen == 2) {   // check length for proper value
                            	bSendAlarms = (int)(*wps);
                            } 
							else 
								retbuf[1] = C_STATUS_LTHERR;    // else mark wrong message length
                            break;  // end case C_WS_LED

                        case C_WS_OSCSRCSEL:        // Change Oscillators if possible
                            j = *wps++ ;               // which one is it?
                            // Length must be right AND two copies must match AND selection must be valid (...CAN_HLP3.h)
                            if ((ecan1msgBuf[rcvmsgindex][2] == OSCSRCSEL_LEN) && (j == *wps) &&
                                ( (j == OSCSEL_JUMPER) || (j == OSCSEL_BOARD) || (j == OSCSEL_TRAY) || (j == OSCSEL_FRCPLL) ) ) {
                                if (j == OSCSEL_JUMPER) {   // if "check jumper" requested then
                                    switches = Read_MCP23008(SWCH_ADDR, MCP23008_GPIO);
                                    jumpers = (switches & JUMPER_MASK)>>JUMPER_SHIFT;
									/*  Jumper JU2.1-2 now controls MCU_SEL_LOCAL_OSC and MCU_EN_LOCAL_OSC
									 **  Installing the jumper forces low on MCU_...OSC
									 */
                                    if ( (jumpers & JUMPER_1_2) == JUMPER_1_2) { // See if jumper IN 1-2
                                        // Jumper INSTALLED inhibits local osc. use TRAY
                                        j = OSCSEL_TRAY;
                                    } else {                        // Jumper OUT (use local osc)
                                        j = OSCSEL_BOARD;       //  Use BOARD clock
                                    }                               // end else turn ON local osc
                                } // end if requesting clock from "jumper" setting
                                retbuf[1] = Initialize_OSC(j); // Switch to it if possible
                                clock_status = OSCCON;     // read the OSCCON reg

                            } else retbuf[1] = C_STATUS_INVALID;    // else mark invalid
                            break;                  // End of Change Oscillators

                        case C_WS_FPGARESET:        // Issue an FPGA Reset
                            memcpy ((unsigned char *)&lwork, wps, 4);   // copy 4 bytes from incoming message
                            // Confirm length is correct and constant agrees
                            if ((rcvmsglen == FPGARESET_LEN) && (lwork == FPGARESET_CONST)) {
                                reset_FPGA();       // do the reset if all is ok
                            } else retbuf[1] = C_STATUS_INVALID;    // else mark invalid
                            break;

                        case C_WS_FPGAREG:          // Write to FPGA Register(s)
                            i = 3;
                            while (i <= rcvmsglen) { // for length of message (1, 2, or 3 reg,val pairs)
                                j = *wps++;
                                write_FPGA (j, (*wps++));
                                i+=2;
                            } // end while have something in message (1, 2, or 3 reg,val pairs)
                            break;

                        case C_WS_TOGGLETINO:       // WB-11V: Toggle TINO_TEST_MCU line "n" times.
                            i = 1;
                            if (rcvmsglen == TOGGLETINO_LEN) {
                                memcpy ((unsigned char *)&i, wps, 2);  // copy the count from the message
                                if (i==0) i=1;
                            } // end if decoding a count
                            for (j=0; j<i; j++) {   // issue toggle sequence
                                Write_device_I2C1 (ECSR_ADDR, MCP23008_OLAT, (ecsrwbits |ECSR_TINO_TEST_MCU) ); // H
                                Write_device_I2C1 (ECSR_ADDR, MCP23008_OLAT, (ecsrwbits&~ECSR_TINO_TEST_MCU) ); // L
                            } // end for issuing toggle sequence
                            break;

                        case C_WS_BLOCKSTART:       // Start Block Download
                            block_status = BLOCK_INPROGRESS;
                            block_bytecount = 0;    // clear block buffer counter
                            block_checksum = 0L;    // clear block buffer checksum
							//JS20090821: set all bytes in block_buffer to 0xff
							memset((void *)block_buffer, 0xff, BLOCK_BUFFERSIZE);
                            wpd = (unsigned char *)&block_buffer[0];    // point destination to buffer
                            // Copy any data from message.  We don't need to check buffer length since it was just set "empty"
                            for (i=1; i<rcvmsglen; i++) {   // copy any remaining bytes
                                    *wpd++ = *wps;        // copy byte into buffer
                                    block_checksum += (*wps++)&0xFF;
                                    block_bytecount++;
                            } // end loop over any bytes in message
                            break;

                        case C_WS_BLOCKDATA:        // Block Data Download
                            if (block_status == BLOCK_INPROGRESS) {
                                for (i=1; i<rcvmsglen; i++) {
                                    if (block_bytecount < BLOCK_BUFFERSIZE) {
                                        *wpd++ = *wps;        // copy byte into buffer
                                        block_checksum += (*wps++)&0xFF;
                                        block_bytecount++;
                                    } else {                // buffer is full
                                        retbuf[1] = C_STATUS_OVERRUN;       // SET ERROR REPLY
                                    } // end if can put data in buffer
                                } // end for loop to copy data
                                // end if had block in progress
                            } else {        // else block was not in progress, send error reply
                                retbuf[1] = C_STATUS_NOSTART;       // SET ERROR REPLY
                            } // end else block was not in progress
                            break;

                        case C_WS_BLOCKEND:         // End Data Download
                            if (block_status == BLOCK_INPROGRESS) {
                                // mark end of block and send bytes and checksum
                                block_status = BLOCK_ENDED;
                                memcpy ((unsigned char *)&retbuf[2], (unsigned char *)&block_bytecount, 2);    // copy bytecount
                                memcpy ((unsigned char *)&retbuf[4], (unsigned char *)&block_checksum, 4);    // copy checksum
                                replylength = 8;            // Update length of reply
                            } else {        // else block was not in progress, send error reply
                                retbuf[1] = C_STATUS_NOSTART;       // ERROR REPLY
                            } // end else block was not in progress
                            break;

                        case C_WS_TARGETCFGS:
                        case C_WS_TARGETCFG1:
                        case C_WS_TARGETCFG2:
                        case C_WS_TARGETCFG3:
                            if (block_status == BLOCK_ENDED) {
                                if (block_bytecount == J_HPTDC_SETUPBYTES) {    // if bytecount OK
									// first retrieve the configuration bits from program memory
									nvmAdru=__builtin_tblpage(basic_setup_pm);
									nvmAdr=__builtin_tbloffset(basic_setup_pm);
									// Read the page and place the data into readback_buffer array
									temp = flashPageRead(nvmAdru, nvmAdr, (int *)readback_buffer);
                                    if ((retbuf[0]&0x3) == 0) { // are we doing all 3?
										i = 0;
										k = NBR_HPTDCS;
									} else {
                                        i = (retbuf[0]&0x3)-1;       // set first and last to be the one
                                        k = i+1;          			// set first and last to be the one
									}

                                    for (j=i; j<k; j++) {   // put the data into the readback buffer
										memcpy ((unsigned char *)&readback_buffer[j*J_HPTDC_SETUPBYTES], block_buffer, J_HPTDC_SETUPBYTES);
									}

									// Erase the page in Flash
									temp = flashPageErase(nvmAdru,nvmAdr);

									// Program the page with modified data
									temp = flashPageWrite(nvmAdru, nvmAdr, (int *)readback_buffer);
                                } else {  // Length is not right
                                    retbuf[1] = C_STATUS_LTHERR;     // SET ERROR REPLY
                                } // end else length was not OK
                            } else {        // else block was not ended, send error reply
                                retbuf[1] = C_STATUS_NOSTART;       // ERROR REPLY
                            } // end else block was not in progress

							break;

                        case C_WS_TARGETHPTDCS:
                        case C_WS_TARGETHPTDC1:
                        case C_WS_TARGETHPTDC2:
                        case C_WS_TARGETHPTDC3:
                            if (block_status == BLOCK_ENDED) {
                                if (block_bytecount == J_HPTDC_SETUPBYTES) {    // if bytecount OK
                                    if ((retbuf[0]&0x3) == 0) { // are we doing all 3?
                                        i = 1;          // yes, set first
                                        k = NBR_HPTDCS;          // yes, set last
                                    } else {            // Not all 3,
                                        i = (retbuf[0]&0x3);       // set first and last to be the one
                                        k = i;          // set first and last to be the one
                                    } // end if one or all 3 HPTDCs.
                                    for (j=i; j<=k; j++) {   // put the data into an HPTDC
                                        select_hptdc(JTAG_MCU, j);        // select MCU controlling which HPTDC
                                        // Copy the received buffer into the setup buffer
                                        memcpy (&hptdc_setup[j][0], &block_buffer[0], J_HPTDC_SETUPBYTES);
                                        hptdc_setup[j][5] &= 0xFFF0;    // clear old TDC ID value
                                        // Fix up HPTDC ID byte in working initialization (Revised method)
                                        // board_posn 0,4 have TDCs 0x0,0x1,0x2
                                        // board_posn 1,5 have TDCs 0x4,0x5,0x6
                                        // board_posn 2,6 have TDCs 0x8,0x9,0xA
                                        // board_posn 3,7 have TDCs 0xC,0xD,0xE
                                        // ((lo 2 bits of board posn) << 2 bits) or'd with (lo 2 bits of (index-1))
                                        hptdc_setup[j][5] |= ((board_posn&0x3)<<2) | ((j-1)&0x3); // compute and insert new value
                                        // Fix up Parity bit in working initialization
                                        insert_parity (&hptdc_setup[j][0], J_HPTDC_SETUPBITS);
                                        write_hptdc_setup (j, (unsigned char *)&hptdc_setup[j][0], (unsigned char *)readback_setup);
                                        // check for match between working and read-back initialization
                                        maskoff = 0xFF;
                                        for (l=0; l<J_HPTDC_SETUPBYTES;l++){ // checking readback
                                            if (l == (J_HPTDC_SETUPBYTES-1)) maskoff = 0x7F;     // don't need last bit!
                                            if ((unsigned char)hptdc_setup[j][l] != (((unsigned char)readback_setup[l])&maskoff)) {
                                                retbuf[1] = C_STATUS_BADCFG;    // bad configuration status
                                                if ((ledbits & 0x10) != 0) {
                                                    ledbits &= 0xEF;    //
                                                    Write_device_I2C1 (LED_ADDR, MCP23008_OLAT, ledbits);
                                                // set LED 4 if there was an error
                                                } // end if have not already seen 1 error
                                            } // end if got a mismatch
                                        } // end loop over checking readback
                                        // LED 4 SET if there was an error
                                        reset_hptdc (j, &enable_final[j-1][0], &hptdc_control[j][0]);    // JTAG the hptdc reset sequence (WB-11X save control word)
                                    } // end loop over one or all HPTDCs
                                    // restore Test header access to JTAG
                                    // Lo jumpers select TDC for JTAG using CONFIG_1 register in FPGA
                                    select_hptdc(JTAG_HDR,(jumpers&0x3));        // select HEADER (J15) controlling which HPTDC
                                } else {  // Length is not right
                                    retbuf[1] = C_STATUS_LTHERR;     // SET ERROR REPLY
                                } // end else length was not OK
                            } else {        // else block was not ended, send error reply
                                retbuf[1] = C_STATUS_NOSTART;       // ERROR REPLY
                            } // end else block was not in progress
                            break;

                        case C_WS_RSTSEQHPTDCS:
                        case C_WS_RSTSEQHPTDC1:
                        case C_WS_RSTSEQHPTDC2:
                        case C_WS_RSTSEQHPTDC3:
                            if ((retbuf[0]&0x3) == 0) { // are we doing all 3?
                               i = 1;          // yes, set first
                               k = NBR_HPTDCS;          // yes, set last
                            } else {            // Not all 3,
                               i = (retbuf[0]&0x3);       // set first and last to be the one
                               k = i;          // set first and last to be the one
                            } // end if one or all 3 HPTDCs.
                            for (j=i; j<=k; j++) {   // put the data into an HPTDC
                               select_hptdc(JTAG_MCU, j);        // select MCU controlling which HPTDC
                               reset_hptdc (j, &enable_final[j-1][0], &hptdc_control[j][0]);    // JTAG the hptdc reset sequence (WB-11X save control word)
                            } // end loop over one or all HPTDCs
                            // restore Test header access to JTAG
                            // Lo jumpers select TDC for JTAG using CONFIG_1 register in FPGA
                            select_hptdc(JTAG_HDR,(jumpers&0x3));        // select HEADER (J15) controlling which HPTDC
                            break;

                        case C_WS_RECONFIGEE1:              // Reconfigure FPGA using EEPROM #1
                        case C_WS_RECONFIGEE2:              // Reconfigure FPGA using EEPROM #2
                            memcpy ((unsigned char *)&lwork, wps, 4);   // copy 4 bytes from incoming message
                            // Confirm length and have proper code
                            if ((rcvmsglen == RECONFIG_LEN) && (lwork == RECONFIG_CONST)) {
                                i = ecan1msgBuf[rcvmsgindex][3] & 0x3;    // get which EEPROM we are doing
                                retbuf[1] = C_STATUS_OK;        // assume FPGA configuration OK
								// loop here if it failed the first time
                                do {
                                    MCU_CONFIG_PLD = 0; // disable FPGA configuration
                                    if (i==2) {
                                        sel_EE2;        // select EEPROM #2
                                    } else {
                                        sel_EE1;        // else it was #1
                                    } // end if select EEPROM #
                                    set_EENCS;      //
                                    MCU_CONFIG_PLD = 1; // re-enable FPGA
                                    j = waitfor_FPGA();     // wait for FPGA to reconfigure
                                    if (j != 0) {
                                        retbuf[1] = C_STATUS_TMOFPGA;   // report an FPGA configuration timeout
                                        i--;        // try again from #1
                                    } // end if had timeout
                                } while ((i != 0) && (j != 0));       // try til either both were used or no error
                                reset_FPGA();       // reset FPGA
                                init_regs_FPGA(board_posn);   // initialize FPGA
                                pld_ident = read_FPGA (IDENT_7_R);
                                retbuf[2] = pld_ident;  // tell the FPGA ID code value
                                replylength = 3;
                                fpga_crc = 0;
                            } else {        // else we could not do it
                                retbuf[1] = C_STATUS_INVALID;   // Assume ERROR REPLY
                                replylength = 2;
                            } // end if we could not do it
                            break;

						//JS:
                        case C_WS_FPGA_CONF0:
							MCU_CONFIG_PLD = 0; // disable FPGA configuration
							isConfiguring = 1;
							break;							

                        case C_WS_FPGA_CONF1:
							MCU_CONFIG_PLD = 1; // disable FPGA configuration
                            waitfor_FPGA(); // wait for FPGA to reconfigure
                            reset_FPGA();   // reset FPGA
                            init_regs_FPGA(board_posn); // initialize FPGA
							// assume CRC_ERROR = 0 after reset
			                fpga_crc = 0;
							isConfiguring = 0;
							break;
						//JS End							

                        case C_WS_TARGETEEPROM2:
                            if (block_status == BLOCK_ENDED) {
                                if ((block_bytecount != 0) && (block_bytecount <= 256) ){    // if bytecount OK
                                    // copy eeprom address
                                    memcpy ((unsigned char *)&eeprom_address, wps, 4);    // copy eeprom target address
                                    eeprom_address &= 0xFFFF00L; // mask off lowest bits (byte in page)
                                    wps += 4;
                                    sel_EE2;            // select EEPROM #2
                                    if ((*wps)==1) {// see if need to erase
                                        // Write-enable the CSR, data doesn't matter
                                        spi_write (EE_AL_WREN, MS2LSBIT, 0, (unsigned char *)&retbuf);
                                        // Do the erase, data doesn't matter
                                        spi_write_adr (EE_AL_ERAS, (unsigned char *)&eeprom_address, MS2LSBIT, 0, (unsigned char *)&retbuf[0]);
                                        // wait for completion
                                        spi_wait (EE_AL_RDSR, EE_AL_BUSY);
                                    } // end if need to erase
                                    // Write-enable the CSR, data doesn't matter
                                    spi_write (EE_AL_WREN, MS2LSBIT, 0, (unsigned char *)&retbuf);
                                    // write the bytes (Altera .RBF gets written LS bit to MS bit
                                    spi_write_adr (EE_AL_WRDA, (unsigned char *)&eeprom_address, LS2MSBIT, block_bytecount, (unsigned char *)&block_buffer[0]);
                                    // wait for completion
                                    spi_wait (EE_AL_RDSR, EE_AL_BUSY);
                                    retbuf[1] = C_STATUS_OK;    // assume all was well
                                    // Read back data and check for match (Altera .RBF was written LSbit to MSbit)
                                    for (i=0; i<block_bytecount; i+= 8) {
                                        lwork = eeprom_address + i;
                                        spi_read_adr (EE_AL_RDDA, (unsigned char *)&lwork, LS2MSBIT, 8, (unsigned char *)&sendbuf[0]);
                                        // WB:11T - Fix for non-mult of 8
                                        k = block_bytecount-i;
                                        if (k > 8) k = 8;
                                        for (j=0; j<k; j++)
                                            if (sendbuf[j] != block_buffer[i+j]) retbuf[1] = C_STATUS_BADEE2;
                                    } // end loop checking readback of newly written data

                                    sel_EE1;        // de-select EEPROM #2
                                    set_EENCS;      //
                                } else {  // Length is not right
                                    retbuf[1] = C_STATUS_LTHERR;     // SET ERROR REPLY
                                } // end else length was not OK
                            } else {        // else block was not ended, send error reply
                                retbuf[1] = C_STATUS_NOSTART;       // ERROR REPLY
                            } // end else block was not in progress
                            break;  // end case C_WS_TARGETEEPROM2
// WB-11H If second image is running we do not want to download another second image
#if !defined (DOWNLOAD_CODE)
                        case C_WS_TARGETMCU:
                            if (block_status == BLOCK_ENDED) {
                                // check for correct length of stored block and incoming message
                                if ( (block_bytecount != 0) && (rcvmsglen == 6) ) {
                                    // lengths OK, set parameters for doing this block
                                    j = 0;          // assume start is begin of buffer
                                    k = block_bytecount;        // assume just going block
                                    memcpy ((unsigned char *)&laddrs, wps, 4);   // copy 4 address bytes from incoming message
                                    // WB-11U check starting address and length for OK
                                    if ( ((laddrs >= MCU2ADDRESS)&&(laddrs+(k>>2))<= MCU2UPLIMIT) || 
										((laddrs >= MCU2IVTL) && ((laddrs+(k>>2)) <= MCU2IVTH)) ) {
										int numRows = 1;

                                        lwork = laddrs;        // save for later
                                        wps += 4;
                                        if ((*wps)==ERASE_PRESERVE) {     // need to preserve before erase
                                            lwork = laddrs & PAGE_MASK;   // mask to page start
                                            for (i=0; i<PAGE_BYTES; i+=4) {
                                                read_MCU_pm ((unsigned char *)&readback_buffer[i], lwork);
                                                lwork += 2;
                                            } // end for loop over all bytes in save block
                                            lwork = laddrs & PAGE_MASK;
                                            j = (unsigned int)(laddrs & OFFSET_MASK); // save offset for copy
                                            j <<= 1;        // *2 for bytes
                                            k = PAGE_BYTES;                   // reprogram whole block
											numRows = 8;
                                        } // end if need to save before copy

										//JS20090821: if address is not "row" aligned, 
										// or if we are not programming a whole row
										// preserve the existing row first
										else if (((laddrs & ROW_OFFSET_MASK) != 0) 		// address not row aligned
												|| (block_bytecount != ROW_BYTES)) {	// not a whole row
											// need to preserve the rest of the row in these cases
                                            lwork = laddrs & ROW_MASK;   // mask to row start
                                            for (i=0; i<ROW_BYTES; i+=4) {
                                                read_MCU_pm ((unsigned char *)&readback_buffer[i], lwork);
                                                lwork += 2;
                                            } // end for loop over all bytes in save block
                                            lwork = laddrs & ROW_MASK;
                                            j = (unsigned int)(laddrs & ROW_OFFSET_MASK); // save offset for copy
                                            j <<= 1;        // *2 for bytes
										}

                                        // put the new data over the old[j] thru old[j+block_bytecount-1]
                                        memcpy ((unsigned char *)&readback_buffer[j], block_buffer, block_bytecount);
                                        save_SR = SR;           // save the Status Register
                                        SR |= 0xE0;             // Raise CPU priority to lock out  interrupts
                                        if ( ((*wps)==ERASE_NORMAL) || ((*wps)==ERASE_PRESERVE) ) {// see if need to erase
											//JS20090821: new routine to erase the page
											nvmAdru = (laddrs&0xffff0000) >> 16;
											nvmAdr = laddrs&0x0000ffff;
											temp = flashPageErase(nvmAdru, nvmAdr);
                                        } // end if need to erase the page
                                        // now write the block_bytecount or PAGE_BYTES starting at actual address or begin page
                                        lwork2 = lwork;
										
										//JS20090821: here is the new row wise programming
										for(i=0; i<numRows; i++) {
											// each row is 256 bytes in the buffer, 
											// each instruction word is 24 bit instruction plus 8 bits dummy (0)
											writePMRow((unsigned char *)(readback_buffer + (i*256)), lwork);
											lwork += 128; // 2 * 64 instructions addresses, address advances by  2
										}

                                        SR = save_SR;           // restore the saved status register
                                        // now check for correct writing
                                        lwork = lwork2;                             // recall the start address
                                        for (i=0; i<k; i+=4) {                  // read either k= block_bytecount or 2048
                                            read_MCU_pm ((unsigned char *)&bwork, lwork); // read a word
                                            for (j=0; j<3; j++) {       // check each word, ignoring highest byte
                                                if (bwork[j] != readback_buffer[i+j]) {
													retbuf[1] = C_STATUS_BADEE2;
//													retbuf[2] = readback_buffer[i+0];
//													retbuf[3] = readback_buffer[i+1];
//													retbuf[4] = readback_buffer[i+2];
//													retbuf[5] = readback_buffer[i+3];
//													retbuf[6] = j;
//													replylength = 7;
												}
                                            } // end loop checking 4 bytes within each word
                                            lwork += 2L;    // next write address
                                        } // end loop over bytes
                                    } else { // else wasn't valid address for write
                                        retbuf[1] = C_STATUS_BADADDRS; // set error reply
                                    } // end WB-11U check for valid addresses to write
                                } else {  // Length is not right
                                    retbuf[1] = C_STATUS_LTHERR;     // SET ERROR REPLY
                                } // end else length was not OK
                            } else {        // else block was not ended, send error reply
                                retbuf[1] = C_STATUS_NOSTART;       // ERROR REPLY
                            } // end else block was not in progress
                            break;  // end case C_WS_TARGETMCU
#endif // #if !defined (DOWNLOAD_CODE)

						case C_WS_MAGICNUMWR:
                            if (rcvmsglen == 3) {
                            	// Copy any data from message.
								memset(readback_buffer, 0, 4);
                            	wpd = (unsigned char *)readback_buffer;    // point destination to buffer
                            	for (i=1; i<3; i++) {   // copy any remaining bytes
                                    *wpd++ = *wps++;        // copy byte into buffer
                            	} // end loop over any bytes in message

								// magic address at end of PIC24HJ64 device program memory
                                laddrs = MAGICADDRESS;  // Magic address (0xABFE)
                                save_SR = SR;           // save the Status Register
                                SR |= 0xE0;             // Raise CPU priority to lock out  interrupts
								//JS20090821: new routine to erase the page
								nvmAdru = (laddrs&0xffff0000) >> 16;
								nvmAdr = laddrs&0x0000ffff;
								temp = flashPageErase(nvmAdru, nvmAdr);
								
//JS20090821                                erase_MCU_pm ((laddrs & PAGE_MASK));      // erase the page
								//JS20090821: should this be replaced by a whole row programming as well?
                                write_MCU_pm ((unsigned char *)readback_buffer, laddrs); // Write a word
                                SR = save_SR;           // restore the saved status register

                            } else {        // else block was not ended, send error reply
                                retbuf[1] = C_STATUS_LTHERR;     // SET ERROR REPLY
                            } // end else block was not in progress

                            break;  // end case C_WS_MAGICNUMWR

						case C_WS_CONF_REG: //  write Configuration Register 
                            if (rcvmsglen == 3) {
								unsigned char cfgRegAddr = *wps++; 	// recvBuffer[1] (which configuration register)
								unsigned char cfgRegVal = *wps;		// recvBuffer[2]
								cfgRegAddr &= 0xFE; // even addresses only
                                save_SR = SR;           // save the Status Register
                                SR |= 0xE0;             // Raise CPU priority to lock out  interrupts
								writeConfRegByte(cfgRegVal, cfgRegAddr);
                                SR = save_SR;           // restore the saved status register
                            } else {  // Length is not right
                            	retbuf[1] = C_STATUS_LTHERR;     // SET ERROR REPLY
                            } // end else length was not OK
							break;

                        case C_WS_BLOCKCKSUM:       // Block Data Checksum
                            retbuf[1] = C_STATUS_INVALID;
                            break;

                        case C_WS_TARGETCTRLS:           // Copy Control word to ALL TDCs PM
                        case C_WS_TARGETCTRL1:           // Copy Control word to TDC #1 PM
                        case C_WS_TARGETCTRL2:           // Copy Control word to TDC #2 PM
                        case C_WS_TARGETCTRL3:           // Copy Control word to TDC #3 PM
							// first retrieve the configuration bits from program memory
							nvmAdru=__builtin_tblpage(enable_final_pm);
							nvmAdr=__builtin_tbloffset(enable_final_pm);
							// Read the page and place the data into readback_buffer array
							temp = flashPageRead(nvmAdru, nvmAdr, (int *)readback_buffer);
                            if ((retbuf[0]&0x3) == 0) { // are we doing all 3?
                                i = 0;                  // yes, set first is number 1
                                k = NBR_HPTDCS;         // yes, set last is NBR_HPTDCS
                            } else {                    // Not all 3,
                                i = (retbuf[0]&0x3) - 1;    // set first to be the one specified
                                k = i+1;                  // and last to be the one specified
                            } // end if one or all 3 HPTDCs.
                            for (j=i; j<k; j++) {   // put the data into one or more HPTDCs
								memcpy ((unsigned char *)&readback_buffer[j*J_HPTDC_CONTROLBYTES], wps, J_HPTDC_CONTROLBYTES);
                            } // end loop over one or more HPTDCs
							// Erase the page in Flash
							temp = flashPageErase(nvmAdru,nvmAdr);

							// Program the page with modified data
							temp = flashPageWrite(nvmAdru, nvmAdr, (int *)readback_buffer);
                            break; // end of C_WS_CONTROLTDCx

                        case C_WS_CONTROLTDCS:           // Copy Control word to ALL TDCs
                        case C_WS_CONTROLTDC1:           // Copy Control word to TDC #1
                        case C_WS_CONTROLTDC2:           // Copy Control word to TDC #2
                        case C_WS_CONTROLTDC3:           // Copy Control word to TDC #3
                            if ((retbuf[0]&0x3) == 0) { // are we doing all 3?
                                j = 1;                  // yes, set first is number 1
                                k = NBR_HPTDCS;         // yes, set last is NBR_HPTDCS
                            } else {                    // Not all 3,
                                j = (retbuf[0]&0x3);    // set first to be the one specified
                                k = j;                  // and last to be the one specified
                            } // end if one or all 3 HPTDCs.
                            for (i=j; i<=k; i++) {   // put the data into one or more HPTDCs
                                select_hptdc(JTAG_MCU, i);        // select MCU controlling which HPTDC
                                // Copy the received buffer (wps) into the setup buffer
                                memcpy (&hptdc_control[i][0], wps, J_HPTDC_CONTROLBYTES);
                                control_hptdc(i, (unsigned char *)&hptdc_control[i][0]);
                            } // end loop over one or more HPTDCs
                            select_hptdc(JTAG_HDR,(jumpers&0x3));        // select HEADER (J15) controlling which HPTDC
                            break; // end of C_WS_CONTROLTDCx

                        case C_WS_THRESHHOLD:        // Write-to-THRESHHOLD DAC
                            current_dac = Write_DAC (((unsigned char *)&ecan1msgBuf[rcvmsgindex][3])+1); // point to LSB of DAC setting
                            for (i=0; i<8; i++) ecan1msgBuf[0][i] = ecan1msgBuf[rcvmsgindex][i]; // save message for echo reply
                            break;

                        case C_WS_MCURESTARTA:              // 0x8D MCU Start new image
                        case C_WS_MCURESET:                 // 0x8F MCU Reset (POR Reset)
                            retbuf[1] = C_STATUS_INVALID;   // Assume ERROR REPLY
                            replylength = 2;
                            memcpy ((unsigned char *)&lwork, wps, 4);   // copy 4 bytes from incoming message
                            // Confirm length is 5 and have proper code
                            if ((rcvmsglen == MCURESET_LEN) && (lwork == MCURESET_CONST)) {
                                retbuf[1] = C_STATUS_OK;            // acknowledge we are going to do it
                                send_CAN1_message (board_posn, (C_BOARD | C_WRITE_REPLY), replylength, (unsigned char *)&retbuf);
                                while (C1TR01CONbits.TXREQ0==1) {};    // wait for transmit to complete
                                if ((ecan1msgBuf[rcvmsgindex][3] & 0xFF) == C_WS_MCURESTARTA) {  // if we are starting new code
#ifndef DOWNLOAD_CODE
                                    // stop interrupts
                                    CORCONbits.IPL3=1;     // WB-11H Raise CPU priority to lock out user interrupts
                                    save_SR = SR;          // save the Status Register
                                    SR |= 0xE0;            // Raise CPU priority to lock out interrupts
									// be sure we are running from alternate interrupt vector
                                    INTCON2 |= 0x8000;     // This is the ALTIVT bit
#endif
									__asm__ volatile ("goto 0x4000");

                                } // end if we are starting second image.
                                __asm__ volatile ("reset");  // else we do "reset" (_resetPRI)
                            } // end if have valid reset or start new image message
                            break;

                        default:                    // Undecodeable
                            retbuf[1] = C_STATUS_INVALID;       // ERROR REPLY
                            break;
                    }                           // end switch on Write Subcommand
                    // Send the reply to the WRITE message
                    send_CAN1_message (board_posn, (C_BOARD | C_WRITE_REPLY), replylength, (unsigned char *)&retbuf);

                    break;  // end case C_WRITE

                case C_READ:    // Process a "Read"
                    replylength = 1;        // default reply length for a Read
                    retbuf[0] = ecan1msgBuf[rcvmsgindex][3] & 0xFF;   // copy address byte

                                // now decode the "Read-from" location inside message
                    switch ((*wps++)&0xFF) {       // look at and dispatch SUB-command, point to remainder of message
                        case (C_RS_CONTROLTDCS):   // WB-11X Read control word all TDCs
                        case (C_RS_CONTROLTDC1):   // WB-11X Read Control word from TDC #1
                        case (C_RS_CONTROLTDC2):   // WB-11X Read Control word from TDC #2
                        case (C_RS_CONTROLTDC3):   // WB-11X Read Control word from TDC #3
                            replylength = 6;       // WB-11X reply is 6 bytes
                            l = retbuf[0] & 0x3;   // WB-11X mask which one we want
                            if (l == 0) {           // WB-11X multiple Control msgs
                                i = 1;
                                k = NBR_HPTDCS;
                            } else {
                                i = l;
                                k = i;
                            }
                            for (l=i; l<=k; l++) {
                                retbuf[0] = (C_RS_CONTROLTDCS & 0xFC) | (l&0x03);
                                memcpy (&retbuf[1], &hptdc_control[l][0], 5);   // WB-11X copy last one used (saved)
                                send_CAN1_message (board_posn, (C_BOARD | C_READ_REPLY), replylength, (unsigned char *)&retbuf);
                                spin(0);
                            } // end loop over one or all tdc control words
                            replylength = 0;
                            break;

                        case (C_RS_CONFIGTDCS):     // WB-11X Read Configuration block from All TDCs
                        case (C_RS_CONFIGTDC1):     // WB-11X Read Configuration block for TDC #1
                        case (C_RS_CONFIGTDC2):     // WB-11X Read Configuration block for TDC #2
                        case (C_RS_CONFIGTDC3):     // WB-11X Read Configuration block for TDC #3
                            if ((retbuf[0]&0x3) == 0) { // are we doing all 3?
                               i = 1;          // yes, set first
                               k = NBR_HPTDCS;          // yes, set last
                            } else {            // Not all 3,
                               i = (retbuf[0]&0x3);       // set first and last to be the one
                               k = i;          // set first and last to be the one
                            } // end if one or all 3 HPTDCs.
                            // send the first parts of the reply
                            for (l=i; l<=k; l++) {   // WB-11X loop over one or 3 HPTDCs
                                retbuf[0] = (C_RS_CONFIGTDCS & 0xFC) | (l & 0x03);  // WB-11X mask for which HPTDC 1, 2, 3
                                replylength = 8;        // WB-11X First messages are 8 bytes
                                for (j=0; j<J_HPTDC_SETUPBYTES; j+= 7) {    // WB-11X Loop over bytes in array
                                    if ((j+7) > J_HPTDC_SETUPBYTES) replylength = (J_HPTDC_SETUPBYTES - j) + 1; // WB-11X last msg short
                                    memcpy ((unsigned char *)&retbuf[1],(unsigned char *)&hptdc_setup[l][j], (replylength-1));
                                    send_CAN1_message (board_posn, (C_BOARD | C_READ_REPLY), replylength, (unsigned char *)&retbuf);
                                    spin(3);            // WB-11X - delay so as not to overrun CANBus (on TCPU) approx 100 msec
                                } // WB-11X end loop over messages
                            } // WB-11X end loop over one or 3 HPTDCs
                            replylength = 0;        // WB-11X - inhibit extra message
                            break;

                        case (C_RS_STATUS1):              // READ STATUS #1
                        case (C_RS_STATUS2):              // READ STATUS #2
                        case (C_RS_STATUS3):              // READ STATUS #3
                            /* Read the HPTDC Status using the JTAG port, Send result via CAN */
                            i = ecan1msgBuf[rcvmsgindex][3] & 0x3;            // LOW 2 bits are TDC#
                            read_hptdc_status (i, (unsigned char *)&retbuf[1], (sizeof(retbuf)-1));
                            // send the first part of the reply
                            replylength = 8;
                            send_CAN1_message (board_posn, (C_BOARD | C_READ_REPLY), 8, (unsigned char *)&retbuf);
                            // send the second part of the reply
                            retbuf[1] = retbuf[8];      // copy last byte for sending
                            replylength = 2;
                            // second part gets sent at end of Switch statement.
							//JS: wait a little, so TCPU can receive this:
							spin(0);
                            break; // end case C_RS_STATUSx

                        case (C_RS_THRESHHOLD):         // WB-11U Return threshhold setting
                            memcpy ((unsigned char *)&retbuf[1], (unsigned char *)&current_dac, 2);
                            replylength = 3;
                            break;  // end case C_RS_THRESHHOLD

                        case (C_RS_MCUMEM ):            // Return MCU Memory 4-bytes
                            if (rcvmsglen == 5) { // check for correct length of incoming message
                                memcpy ((unsigned char *)&lwork, wps, 4);   // copy 4 bytes from incoming message
                            } else {        // allow continued reads w/o address
                                lwork += 2L;
                            } // end else continued reads
                            if (lwork <= (MCU2UPLIMIT+2) ) {        // WB-11U: if in range
                                read_MCU_pm ((unsigned char *)&retbuf[1], lwork); // Read from requested location
                                replylength = 5;
                            } // WB-11U: end if in range
                            break; // end case C_RS_MCUMEM

                        case (C_RS_MCUCKSUM ):            // Return MCU Memory checksum
                            if (rcvmsglen == 8) { // check for correct length of incoming message
                                memcpy ((unsigned char *)&laddrs, wps, 4);   // copy 4 bytes from incoming message, address
                                wps+=4;     // step to count
                                lwork2 = 0L;         // clear checksum
                                lwork = 0L;         // clear count
                                memcpy ((unsigned char *)&lwork, wps, 3);   // copy 3 bytes from incoming message, length
                                while ((lwork != 0L) && (laddrs <= (MCU2UPLIMIT+2))) {
                                    read_MCU_pm ((unsigned char *)&lwork3, laddrs); // Read from requested location
                                    lwork2 += lwork3;
                                    laddrs += 2L;
                                    lwork--;
                                }
                                memcpy ((unsigned char *)&retbuf[1], (unsigned char *)&lwork2, 4);   // copy checksum to reply
                                replylength = 5;
                            }
                            break; // end case C_RS_MCUCKSUM

                        case (C_RS_FIRMWID):            // Return MCU Firmware ID
                            retbuf[1] = FIRMWARE_ID_0;
                            retbuf[2] = FIRMWARE_ID_1;
                            retbuf[3] = (unsigned char)(read_FPGA (IDENT_7_R)&0xFF);
                            replylength = 4;
                            break; // end case C_RS_FIRMWID

#if defined (SN_ADDR) // if address defined, it exists
                        case (C_RS_SERNBR):           // Return the board Serial Number
                            Write_device_I2C1 (SN_ADDR, CM00_CTRL, CM00_CTRL_I2C);     // set I2C mode
                            for (j=1; j<8; j++) {       // we only return 7 bytes (of 8)
                                retbuf[j] = (unsigned char)Read_MCP23008(SN_ADDR,j);        // go get sn byte
                            }
                            replylength = 8;
                            break; // end case C_RS_SERNBR
#endif // defined (SN_ADDR)

                        case (C_RS_STATUSB):              // READ STATUS Board
                            board_temp = Read_Temp();       // TDIG board temperature
                            memcpy ((unsigned char *)&retbuf[1], (unsigned char *)&board_temp, 2);
                                                            // ECSR byte
                            retbuf[3] = (unsigned char)Read_MCP23008(ECSR_ADDR, MCP23008_GPIO);
                                                            // A/D from TINO 1 signal
                            memcpy ((unsigned char *)&retbuf[4], (unsigned char *)&tino1, 2);
                                                            // A/D from TINO 2 signal
                            memcpy ((unsigned char *)&retbuf[6], (unsigned char *)&tino2, 2);
                            replylength = 8;
                            break; // end case C_RS_STATUSB

                        case (C_RS_TEMPBRD):           // Return the board Temperatures
                            board_temp = Read_Temp ();       // TDIG board temperature
                            memcpy ((unsigned char *)&retbuf[1], (unsigned char *)&board_temp, 2);        // copy temperature to message
                                                            // A/D from TINO 1 signal
                            memcpy ((unsigned char *)&retbuf[3], (unsigned char *)&tino1, 2);
                                                            // A/D from TINO 2 signal
                            memcpy ((unsigned char *)&retbuf[5], (unsigned char *)&tino2, 2);
                            replylength = 7;
                            break; // end case C_RS_TEMPBRD

                        case (C_RS_CLKSTATUS):        // Read MCU Clock Status
                            memcpy ((unsigned char *)&retbuf[1], (unsigned char *)&clock_status, 2);
                            memcpy ((unsigned char *)&retbuf[3], (unsigned char *)&clock_failed, 1);
                            memcpy ((unsigned char *)&retbuf[4], (unsigned char *)&clock_requested, 1);
                            replylength = 5;
                            break;  // end case C_RS_CLKSTATUS

                        case (C_RS_FPGAREG):          // Read from FPGA Register(s)
                            replylength = 1;
                            i = 2;
                            while (i <= rcvmsglen) { // for length of message (1, 2, or 3 reg,val pairs)
                                retbuf[replylength] = *wps++;
                                retbuf[replylength+1] = read_FPGA((unsigned int)(retbuf[replylength]&0xFF));
                                replylength +=2;
                                i++;
                            } // end while have something in message (1, 2, or 3 reg,val pairs)
                            break;                  // end C_RS_FPGAREG

                        case (C_RS_EEPROM2):
                            replylength = 8;        // fixed length reply
                            memcpy ((unsigned char *)&eeprom_address, wps, 4);    // copy eeprom target address
                            MCU_CONFIG_PLD = 0; // disable FPGA configuration
                            sel_EE2;            // select EEPROM #2
                            // Read back data (Altera .RBF was written LSbit to MSbit)  7-bytes
                            spi_read_adr (EE_AL_RDDA, (unsigned char *)&eeprom_address, LS2MSBIT, 7, (unsigned char *)&retbuf[1]);
                            sel_EE1;        // de-select EEPROM #2
                            set_EENCS;      //
                            MCU_CONFIG_PLD = 1; // re-enable FPGA
                            waitfor_FPGA(); // wait for FPGA to reconfigure
                            reset_FPGA();   // reset FPGA
                            init_regs_FPGA(board_posn); // initialize FPGA

                            break;      // end C_RS_EEPROM2

                        case (C_RS_EEP2CKSUM):
                            replylength = 5;        // fixed length reply
                            memcpy ((unsigned char *)&eeprom_address, wps, 4);    // copy eeprom start address
                            wps+=4;
                            laddrs = 0L;        // clear sector count
                            memcpy ((unsigned char *)&laddrs, wps, 3);             // copy number of sectors to read
                            MCU_CONFIG_PLD = 0; // disable FPGA configuration
                            sel_EE2;            // select EEPROM #2
                            // Read back data (Altera .RBF was written LSbit to MSbit)
                            lwork = 0L;         // will build-up checksum
                            while (laddrs != 0L) {    // while we have sectors to read (sector = 256 bytes)
                                // read the sector
                                spi_read_adr (EE_AL_RDDA, (unsigned char *)&eeprom_address, LS2MSBIT, 256, (unsigned char *)&block_buffer[0]);
                                // Add the bytes to the checksum
                                for (i=0; i<256; i++) lwork += (unsigned long)(block_buffer[i]&0xFF);
                                eeprom_address += 256;      // increment the block address'
                                laddrs--;                   // count down blocks to do
                            }   // end while have blocks to do
                            memcpy ((unsigned char *)&retbuf[1], (unsigned char *)&lwork, 4);   // copy result to reply
                            sel_EE1;        // de-select EEPROM #2
                            set_EENCS;      //
                            MCU_CONFIG_PLD = 1; // re-enable FPGA
                            waitfor_FPGA(); // wait for FPGA to reconfigure
                            reset_FPGA();   // reset FPGA
                            init_regs_FPGA(board_posn); // initialize FPGA
                            break;      // end C_RS_EEPCKSUM

                        case (C_RS_JSW):              // Return the Jumper/Switch settings (U35)
                            retbuf[1] = (unsigned char)Read_MCP23008(SWCH_ADDR, MCP23008_GPIO);
                            replylength = 2;
                            break; // end case C_RS_JSW

                        case (C_RS_ECSR):             // Return the Extended CSR settings (U36)
                            retbuf[1] = (unsigned char)Read_MCP23008(ECSR_ADDR, MCP23008_GPIO);
                            replylength = 2;
                            break; // end case C_RS_ECSR

                        case (C_RS_DIAGNOSTIC):       // Diagnostic read - interpretation subject to change
                            break; // end case C_RS_DIAGNOSTIC

                        default:    // Undecodable Read only echoes subcommand
                            break; // end case default

                    } // end switch on READ SUBCOMMAND (Address)

                    if (replylength != 0) {        // WB-11X - inhibit extra message
                        send_CAN1_message (board_posn, (C_BOARD | C_READ_REPLY), replylength, (unsigned char *)&retbuf);
                    } // WB-11X end if inhibit extra message
                    break;

                default:                 // All others are undecodable, ignore
                    break;
            }                               // end MAJOR switch on WRITE or READ COMMAND

// WB-11V Mark Receive buffer we just worked on as OK to reuse
            if (rcvmsgindex == 1) C1RXFUL1bits.RXFUL1 = 0;
            if (rcvmsgindex == 2) C1RXFUL1bits.RXFUL2 = 0;

        } // end if have a message to process

// WB-11W: Add check for CAN1 error / overflow
        if (can1error) {          // see if we have CAN1 error flag (overflow/CANerror)
            lwork = C_ALERT_ERRCAN1_CODE | (can1error<<8);
            send_CAN1_message (board_posn, (C_BOARD | C_ALERT), C_ALERT_ERRCAN1_LEN, (unsigned char *)&lwork);
            can1error = 0;        // clear overflow message
        } // end if we have CAN1 error flag (overflow/CANerror)
// WB-11W: end Add check for CAN1 error

// Mostly Idle
        tglbit ^= 1;        // toggle the bit in port
		MCU_TEST = tglbit;
#if defined (RC15_IO) // RC15 will be I/O (in TDIG-D-Board.h)
		LATCbits.LATC15 = tglbit;        // make it like RG15
#endif
#define UDCONFIGBITS 1
#if defined (UDCONFIGBITS)
		// Copy UCONFIGI bit to DCONFIGO bit to allow testing of corresponding LVDS signals (define UDCONFIGBITS)
		DCONFIG_OUT = UCONFIG_IN;
#endif

// WB-11J,R Check ECSR for change-of-state on PLD_CRC_ERROR bit
		if ((isConfiguring == 0) && (bSendAlarms == 1)) { //JS: only check when we are not configuring and alarms turned on
			j = Read_MCP23008(ECSR_ADDR, MCP23008_GPIO) & ECSR_PLD_CRC_ERROR; // Read the port bit
			if ( j != fpga_crc) {  // see if it has changed
            	fpga_crc = j;
            	lwork = C_ALERT_CKSUM_CODE | (j<<8);
            	send_CAN1_message (board_posn, (C_BOARD | C_ALERT), C_ALERT_CKSUM_LEN, (unsigned char *)&lwork);
			}
		}
// WB-11J end
// WB-11R - Clock status monitoring
//        if (clock_status != OSCCON) {   // if something changed (interrupt routine)
//        if (OSCCONbits.CF != 0) {   // if clock failed (interrupt routine)
        if ((clock_failed != 0) && (bSendAlarms == 1)) {   // if clock failed (interrupt routine)
            clock_status = OSCCON;
            lwork = C_ALERT_CLOCKFAIL_CODE;
            send_CAN1_message (board_posn, (C_BOARD | C_ALERT), C_ALERT_CLOCKFAIL_LEN, (unsigned char *)&lwork);
            clock_failed = 0;       // clear the failed code
        }
// WB-11R - End Clock status monitoring

// WB-11V - Temperature Alert Monitoring (WB-11X)
        if ((temp_alert_throttle == 0) && (bSendAlarms == 1)) {     // if time to send another one
            if (temp_alert != 0) {          // if temperature alert
                lwork = (temp_alert<<8) | C_ALERT_OVERTEMP_CODE;  // upper byte is event mask
                send_CAN1_message (board_posn, (C_BOARD | C_ALERT), C_ALERT_OVERTEMP_LEN, (unsigned char *)&lwork);
                temp_alert_throttle = TEMPALERTS_INTERVAL;
                temp_alert = 0;             // clear alert status
            } else {  // else do not have temperature alert
                temp_alert_throttle = 0;    // arm ourselves for next trip.
            } // end else do not have temperature alert
        } else { // else just count down delay
            temp_alert_throttle--;
        } // end else just count down delay
// WB-11V - End Temperature Alert Monitoring

#ifndef DOWNLOAD_CODE
		if (timerExpired == 1) {
			timerExpired = 0;
			// magic address at end of PIC24HJ64 device program memory
            read_MCU_pm ((unsigned char *)readback_buffer, MAGICADDRESS);
			if (*((unsigned int *)readback_buffer) == 0x3412) {
                // stop interrupts
                CORCONbits.IPL3=1;     // Raise CPU priority to lock out user interrupts
                save_SR = SR;          // save the Status Register
                SR |= 0xE0;            // Raise CPU priority to lock out interrupts
				// be sure we are running from alternate interrupt vector
                INTCON2 |= 0x8000;     // This is the ALTIVT bit
				__asm__ volatile ("goto 0x4000");
                //jumpto();    // jump to new code
			}
		}
#endif

        // Mostly Idle, Check for change of switches/jumpers/button
		if ( (Read_MCP23008(SWCH_ADDR, MCP23008_GPIO)) != switches ) {        // if changed
			switches = Read_MCP23008(SWCH_ADDR, MCP23008_GPIO);
			if ((switches & BUTTON)==BUTTON) {            // if Button
				spin(0);
				switches = Read_MCP23008(SWCH_ADDR, MCP23008_GPIO);
				if ((switches & BUTTON)==BUTTON) {        // still button?
				} // end if have second switch
			} // end if have first switch
			jumpers = (switches & JUMPER_MASK)>>JUMPER_SHIFT;

// Lo jumpers select TDC for JTAG using CONFIG_1 register in FPGA
			select_hptdc(JTAG_HDR,(jumpers&0x3));        // select HEADER (J15) controlling which HPTDC
			ledbits = (ledbits|0x0F) ^ (jumpers & 0x0F);    //
			Write_device_I2C1 (LED_ADDR, MCP23008_OLAT, ledbits);
			i = (switches & 0xF)>>1;   // position 0..7
// IF ECO14_SW4 is defined, we need to compensate for the bit0<-->bit2 swap error
#if defined (ECO14_SW4)
			j = i & 2; // save the center bit
			if ((i&1)==1) j |= 4;  // fix the 4 bit
			if ((i&4)==4) j |= 1;  // fix the 1 bit
#endif // defined (ECO14_SW4)
		} // end if something changed
    } while (1); // end do forever
}

// WB-11H
// Replaced old initialize_OSC routine

int Initialize_OSC (unsigned int selectosc){
/* initialize the CPU oscillator (works with settings in TDIG-D_CAN_HLP3.h)
** Call with: selectosc as follows:
**      selectosc == OSCSEL_BOARD  == 0 == Use on-Board oscillator (40 MHz)
**      selectosc == OSCSEL_TRAY   == 8 == Use Tray oscillator (40 MHz)
**      selectosc == OSCSEL_FRCPLL == 1 == Use MCU Fast RC + PLL (40 MHz)
*/
    int retstat = 0;                    // Assume OK return
    clock_requested = selectosc;        // global request monitor
    switch (selectosc) {
        case (OSCSEL_TRAY):             // Selecting TRAY clock
            MCU_SEL_LOCAL_OSC = 0;      // turns off sel-local-osc
            MCU_EN_LOCAL_OSC = 0;       // turns off en-local-osc
            Switch_OSC(MCU_EXTERN);     // Change MCU to External Osc
            break;
        case (OSCSEL_BOARD):            // Selecting BOARD clock
            MCU_SEL_LOCAL_OSC = 1;      // turns on sel-local-osc
            MCU_EN_LOCAL_OSC = 1;       // turns on en-local-osc
            spin(0);                    // wait for local osc to turn on
            Switch_OSC(MCU_EXTERN);     // Change MCU to External Osc
            break;
        case (OSCSEL_FRCPLL):           // Selecting Fast RC w/PLL for 40MHz
            Switch_OSC(MCU_FRCPLL);     /* Switch to FRCPLL */
            PLLFBD=20;                  /* M= 20*/
            CLKDIVbits.PLLPOST=0;       /* N1=2 */
            CLKDIVbits.PLLPRE=0;        /* N2=2 */
            OSCTUN=0;                   /* Tune FRC oscillator, if FRC is used */
            while(OSCCONbits.LOCK!=1) {}; /* Wait for PLL to lock */
            break;
        default:
            retstat = 1;                // mark bad return
            break;
    } // end else switching to external oscillator (board or tray)
    if (retstat == 0) {
        ecan1Init(board_posn);      // if clock has changed we must reinitialize CANBus
        dma0Init();                     // defined in ECAN1Config.c (copied here)
        dma2Init();                     // defined in ECAN1Config.c (copied here)
    }
    return(retstat);                    // return the status
}

void Switch_OSC(unsigned int mcuoscsel) {         /* Switch Clock Oscillator */
/* Parameter mcuoscsel ends up in W0
** WE MUST USE ASM TO INSURE THE EXACT SEQUENCE OF UNLOCK OPERATION CYCLES
** mcuoscsel determines Oscillator to use
**   0 = FRC
**   1 = FRC+PLL
**   2 = Primary Oscillator (XT, HS, or EC set by _FOSC()
**   3 = Primary Oscillator + PLL (XT, HS, or EC set by _FOSC()
**   4 = Secondary Oscillator (SOSC)
**   5 = Low Power RC Oscillator (LPRC)
**   6 = Fast RC oscillator with div. by 16
**   7 = Fast RC oscillator with div by n
** TDIG- uses (1)FRC+PLL and (2)Primary=EC
** WARNING: NO ERROR CHECKING ON mcuoscsel !
*/
    __asm__ volatile ("mov #OSCCON+1,W1");   // set up unlock sequence
    __asm__ volatile ("disi #6");           // Disable interrupts
    __asm__ volatile ("mov #0x78,W2");      //
    __asm__ volatile ("mov #0x9A,W3");
    __asm__ volatile ("mov.b W2,[W1]");
    __asm__ volatile ("mov.b W3,[W1]");
    __asm__ volatile ("mov.b W0,[W1]");

    __asm__ volatile ("mov #0x01,W0");    // 0x01 = Switch Oscillators
    __asm__ volatile ("mov #OSCCON,W1");   // set up unlock sequence
    __asm__ volatile ("disi #6");           // Disable interrupts
    __asm__ volatile ("mov #0x46,W2");      //
    __asm__ volatile ("mov #0x57,W3");
    __asm__ volatile ("mov.b W2,[W1]");
    __asm__ volatile ("mov.b W3,[W1]");
    __asm__ volatile ("mov.b W0,[W1]");
    while ((OSCCONbits.OSWEN != 0) && (OSCCONbits.CF != 0)){}    // wait for switch to happen or fail
    if (OSCCONbits.CF != 0) clock_failed = 1;
}
// WB-11H end

void spin(count) {
	int i, j;
	j=count + 1;
	do {
		for (i=0xFFFF; i!=0; --i){}	// spin loop
	} while (--j != 0);
}


void clearIntrflags(void){
/* Clear Interrupt Flag Status Registers */
// DMA1, ADC1, UART1, SP1, Timer3,2, OC2, IC2, DMA0, Timer1, OC1, IC1, INT0
    IFS0=0;                             // Interrupt flag Status Register 0

// UART2, INT2, Timer5,4, OC4,3, DMA2, IC8,7, AD2, INT1, CN1, I2C1M, I2C1S
    IFS1=0;                             // Interrupt flag Status Register 1

// Timer6, DMA4, OC8,7,6,5,4,3, DMA3, CAN1, SPI2, SPI2E
    IFS2=0;                             // Interrupt flag Status Register 2

// DMA5, CAN2, INT4, INT3, Timer9,8, I2C2M, I2C2S, Timer7
    IFS3=0;                             // Interrupt flag Status Register 3

// CAN2tx, CAN1tx, DMA7,6, UART2e, UART1e
    IFS4=0;                             // Interrupt flag Status Register 4
}


void ecan1Init(unsigned int board_id) {
/* Initialize ECAN #1  and put board_id into mask / filter */
/* Request Configuration Mode */
    C1CTRL1bits.REQOP=4;                // Request configuration mode
    while(C1CTRL1bits.OPMODE!=4);       // Wait for configuration mode active

/* FCAN is selected to be FCY
** FCAN = FCY = 20MHz */
    C1CTRL1bits.CANCKS = 0x1;           // FCAN = FCY == 20MHz depends on board and PLL
/*
Bit Time = (Sync Segment (1*TQ) +  Propagation Delay (3*TQ) +
 Phase Segment 1 (3*TQ) + Phase Segment 2 (3TQ) ) = 10*TQ = NTQ
 Baud Prescaler CiCFG1<BRP>  = (FCAN /(2�NTQ�FBAUD)) � 1
 BRP = (20MHz / 2*10*1MBaud))-1 = 0
*/
	/* Baud Rate Prescaler */
	C1CFG1bits.BRP = CAN1_BRP;

	/* Synchronization Jump Width set to SJW TQ */
	C1CFG1bits.SJW = CAN1_SJW;

	/* Propagation Segment time is PRSEG TQ */
	C1CFG2bits.PRSEG = CAN1_PRSEG;

	/* Phase Segment 1 time is SEG1PH TQ */
    C1CFG2bits.SEG1PH = CAN1_SEG1PH;

	/* Phase Segment 2 time is set to be programmable */
	C1CFG2bits.SEG2PHTS = CAN1_SEG2PHTS;
	/* Phase Segment 2 time is SEG2PH TQ */
	C1CFG2bits.SEG2PH = CAN1_SEG2PH;

	/* Bus line is sampled SAM times at the sample point */
	C1CFG2bits.SAM = CAN1_SAM;
/* -------------------------------*/

/* 4 CAN Message (FIFO) Buffers in DMA RAM (minimum number) */
    C1FCTRLbits.DMABS=0b000;            // Page 189

/*	Filter Configuration
    ecan1WriteRxAcptFilter(int n, long identifier, unsigned int exide,
        unsigned int bufPnt, unsigned int maskSel)
        n = 0 to 15 -> Filter number
        identifier -> SID <10:0> : EID <17:0>
        exide = 0 -> Match messages with standard identifier addresses
        exide = 1 -> Match messages with extended identifier addresses
        bufPnt = 0 to 14  -> RX Buffer 0 to 14
        bufPnt = 15 -> RX FIFO Buffer
        maskSel = 0 ->  Acceptance Mask 0 register contains mask
        maskSel = 1 ->  Acceptance Mask 1 register contains mask
        maskSel = 2 ->  Acceptance Mask 2 register contains mask
        maskSel = 3 ->  No Mask Selection
*/
    C1CTRL1bits.WIN = 1;                  // SFR maps to filter window

// Select Acceptance Filter Mask 0 for Acceptance Filter 0
    C1FMSKSEL1bits.F0MSK = 0x0;

// Configure Acceptance Filter Mask 0 register to
//      Mask board_id in SID<6:4> per HLP 3 protocol
// WB-11V    C1RXM0SIDbits.SID = 0x03F0; // 0b011 1brd 0000
    C1RXM0SIDbits.SID = 0x07F0; // WB-11V: 0b111 1brd 0000

/* WB-11V ------------ start of Filter 0 intitialization -------------------------------------------------------- */
// Configure Acceptance Filter 0 to match Standard Identifier == TDIG Board ID
    C1RXF0SIDbits.SID = (C_BOARD>>2)|(board_id<<4);  // 0biii ibrd xxxx

// Configure Acceptance Filter 0 for Standard Identifier
    C1RXM0SIDbits.MIDE = 0x1;
    C1RXM0SIDbits.EID = 0x0;

// Acceptance Filter 0 uses message buffer 1 to store message
    C1BUFPNT1bits.F0BP = 1;

// Filter 0 enabled
    C1FEN1bits.FLTEN0 = 0x1;
/* WB-11V -------------- end of Filter 0 intitialization --------------------------------------------------------- */

/* WB-11V ------------ start of Filter 1 intitialization --------------------------------------------------------- */
// Configure Acceptance Filter 1 to match Standard Identifier board address 127.
    C1RXF1SIDbits.SID = 127<<4;  // 0b111 1111 xxxx

// Configure Acceptance Filter 1 for Standard Identifier
    C1RXM0SIDbits.MIDE = 0x1;
    C1RXM0SIDbits.EID = 0x0;

// Acceptance Filter 1 uses message buffer 2 to store message
    C1BUFPNT1bits.F1BP = 2;

// Filter 1 enabled
    C1FEN1bits.FLTEN1 = 0x1;
/* WB-11V -------------- end of Filter 1 intitialization --------------------------------------------------------- */

// Clear window bit to access ECAN control registers
    C1CTRL1bits.WIN = 0;

/* Enter Normal Mode */
    C1CTRL1bits.REQOP=0;                // Request normal mode
    while(C1CTRL1bits.OPMODE!=0);       // Wait for normal mode

/* ECAN transmit/receive message control */
    C1RXFUL1=0x0000;                    // mark RX Buffers 0..15 empty
    C1RXFUL2=0x0000;                    // mark RX Buffers 16..31 empty
    C1RXOVF1=0x0000;                    // clear RX Buffers 0..15 overflow
    C1RXOVF2=0x0000;                    // clear RX Buffers 16..31 overflow
	C1TR01CONbits.TXEN0=1;			/* ECAN1, Buffer 0 is a Transmit Buffer */
	C1TR01CONbits.TXEN1=0;			/* ECAN1, Buffer 1 is a Receive Buffer */
    C1TR01CONbits.TX0PRI=0b11;      /* Message Buffer 0 Priority Level highest */
    C1TR01CONbits.TX1PRI=0b11;      /* Message Buffer 1 Priority Level highest */
}


/* DMA Initialization for ECAN1 Transmission */
void dma0Init(void){
/* Set up DMA for ECAN1 Transmit ----------------------------------------- */
     DMACS0=0;                          // Clear DMA collision flags

/* Continuous, no Ping-Pong, Normal, Full, Mem-to-Periph, Word, disabled */
     DMA0CON=0x2020;

/* Peripheral Address Register */
     DMA0PAD=0x0442;    /* ECAN 1 (C1TXD register) */

/* Transfers to do = DMA0CNT+1 */
 	 DMA0CNT=0x0007;

/* DMA IRQ 70. (ECAN1 Tx Data) select */
     DMA0REQ=0x0046;

/* point DMA0STA to start address of data-to-transmit buffer */
     DMA0STA=  __builtin_dmaoffset(ecan1msgBuf);

/* Enable DMA2 channel */
     DMA0CONbits.CHEN=1;
}
/* ----------------------------------------------------------------------- */


/* DMA Initialization for ECAN1 Reception */
void dma2Init(void){
/* Set up DMA for ECAN1 Receive  ----------------------------------------- */
     DMACS0=0;                          // Clear DMA collision flags

/* Continuous, no Ping-Pong, Normal, Full, Periph-to-Mem, Word, disabled */
     DMA2CON=0x0020;

/* Peripheral Address Register */
     DMA2PAD=0x0440;    /* ECAN 1 (C1RXD register) */

/* Transfers to do = DMA0CNT+1 */
 	 DMA2CNT=0x0007;

/* DMA IRQ 34. (ECAN1 Rx Data Ready) select */
	 DMA2REQ=0x0022;	/* ECAN 1 Receive */

/* point DMA2STA to start address of receive-data buffer */
     DMA2STA= __builtin_dmaoffset(ecan1msgBuf);

/* Enable DMA2 channel */
     DMA2CONbits.CHEN=1;
}

void send_CAN1_message (unsigned int board_id, unsigned int message_type, unsigned int bytes, unsigned char *payload)
{
/* Write a Message to ECAN1 Transmit Buffer and Request Message Transmission
** Call with:
**      board_id = identifier of board to be stuffed into message header
**      message_id = type of message being sent gets or'd with board_id
**      bytes = number of payload bytes to send
**      *payload = pointer to payload buffer
**          payload[0] is usually the "subcommand" (HLP 3.0)
**          payload[1] is usually "status" (HLP 3.0)
*/
/* ------------------------------------------------
Builds ECAN1 message ID into buffer[0] words [0..2]
 -------------------------------------------------- */
	unsigned long msg_id;
    unsigned char *cp;
	unsigned int i;
#if defined (SENDCAN)
    i = bytes;
    if (i > 8) i=8;         // at most 8 bytes of payload
    while (C1TR01CONbits.TXREQ0==1) {};    // wait for transmit to complete
    msg_id = (unsigned long)((board_id&0x7)<<6); // stick in board ID
    msg_id |= message_type;
    ecan1msgBuf[0][0] = msg_id;  // extended ID =0, no remote xmit
    ecan1msgBuf[0][1] = 0;

/* ------------------------------------------------
** Builds ECAN1 payload Length and Data into buffer words [2..6]
 -------------------------------------------------- */
    ecan1msgBuf[0][2] = i;
    cp = (unsigned char *)&ecan1msgBuf[0][3];
    if (i > 0) {
       // message length up to 7 more
        memcpy (cp, payload, i);          // copy the message
    } // end if have additional bytes in message
/* Request the message be transmitted */
    C1TR01CONbits.TXREQ0=1;             // Mark message buffer ready-for-transmit
#endif
}


void send_CAN1_data (unsigned int board_id, unsigned int bytes, unsigned char *bp)
{
/* Write a Data Message to ECAN1 Transmit Buffer
   Request Message Transmission			*/
/* ------------------------------------------------
Builds ECAN1 message ID into buffer[0] words [0..2]
 -------------------------------------------------- */
	unsigned long msg_id;
    unsigned char *cp, *wp;
	unsigned int i;
#if defined (SENDCAN)
    if (bytes <= 8) {
	    while (C1TR01CONbits.TXREQ0==1); 	// wait for transmit to complete
        msg_id = (unsigned long)((board_id&0x7)<<6); // stick in board ID
        msg_id |= (C_BOARD | C_DATA);    // reply constant part
        ecan1msgBuf[0][0] = msg_id;  // extended ID =0, no remote xmit
        ecan1msgBuf[0][1] = 0;
        ecan1msgBuf[0][2] = 0;
/* ------------------------------------------------
** Builds ECAN1 payload Length and Data into buffer words [2..6]
 -------------------------------------------------- */
        ecan1msgBuf[0][2] += (bytes&0xF);       // message length
		wp = bp;
		cp = (unsigned char *)&ecan1msgBuf[0][3];
		for (i=0; i<bytes; i++) {
			*cp++ = *wp++;
		}

/* Request the message be transmitted */
        C1TR01CONbits.TXREQ0=1;             // Mark message buffer ready-for-transmit
    } // end if have proper length to send
#endif
}


/* -----------------12/8/2006 10:15AM----------------
How do these get hooked to hardware???
 ans: by magic name "C1Interrupt" and attribute
 --------------------------------------------------*/
//void __attribute__((__interrupt__))_C1Interrupt(void)
void __attribute__((interrupt,no_auto_psv))_C1Interrupt(void)  
{
// WB-11W: Revised interrupt routine inserted
    if (C1INTFbits.RBOVIF) {    // If interrupt was from Overflow
        can1error = C1VEC;     // Mark which one caused interrupt
        C1INTFbits.RBOVIF = 0;  // and clear the interrupt
    } // end if interrupt was from Overflow
    if (C1INTFbits.ERRIF) {    // If interrupt was from Error
        can1error = C1VEC;     // Mark which one caused interrupt
        C1INTFbits.ERRIF = 0;  // and clear the interrupt
    } // end if interrupt was from Error
    if (C1INTFbits.TBIF) {     // If interrupt was from Tx Buffer
        C1INTFbits.TBIF = 0;            // Clear Tx Buffer Interrupt
    } // end if interupt was from Tx Buffer
    if (C1INTFbits.RBIF) {     // If interrupt was from Rx Buffer
        C1INTFbits.RBIF = 0;            // Clear Rx Buffer Interrupt
    } // end if interrupt was from Rx Buffer
    IFS2bits.C1IF = 0;        // clear interrupt flag ECAN1 Event
}

//void __attribute__((__interrupt__))_OscillatorFail(void)
void __attribute__((interrupt,no_auto_psv))_OscillatorFail(void)  
{
/* This is the Exception interrupt handler for Oscillator Clock Fail.
** we switch back to the FRC/PLL oscillator, clear the interrupt, and
** Return
*/
    INTCON1bits.OSCFAIL=0;      // clear the trap flag
    clock_failed = 1;           // set the program flag
    clock_status = OSCCON;          // Remember the status before we change it
    Initialize_OSC (OSCSEL_FRCPLL);
}

// WB-11U: Changed timer assignment to T6/T7 so T2/T3 is available for A/D converter
// Timer 7 Interrupt Service Routine
//void _ISR _T7Interrupt(void)
void __attribute__((interrupt,no_auto_psv))_T7Interrupt(void)  
{
    IFS3bits.T7IF = 0;      // clear interrupt status flag Timer 7
    IEC3bits.T7IE = 0;      // disable Timer 7 interrupt
    T6CON = 0;              // clear Timer 6 control register (turn timer off)
    T7CON = 0;              // clear Timer 7 control register (turn timer off)
#if !defined (DOWNLOAD_CODE)    // WB-11P: Not defined for download code
	timerExpired = 1;		// indicate to main program that timer has expired
#endif // not defined DOWNLOAD_CODE
}
// WB-11U: end timer change

#if defined (SENDMISMATCH)
void send_CAN1_hptdcmismatch (unsigned int board_id, unsigned int tdcno, unsigned int index, unsigned char expectedbyte, unsigned char gotbyte)
{
/* Write a Message to ECAN1 Transmit Buffer
   Request Message Transmission			*/
/* ------------------------------------------------
Builds ECAN1 message ID into buffer[0] words [0..2]
 -------------------------------------------------- */
	unsigned long msg_id;
//  unsigned char *cp, *wp;
//  unsigned int i;
#if defined (SENDCAN)
    while (C1TR01CONbits.TXREQ0==1); 	// wait for transmit to complete
    msg_id = (unsigned long)((board_id&0x7)<<6); // stick in board ID
    msg_id |= (C_BOARD | C_ALERT );   // identify the message ALERT
    ecan1msgBuf[0][0] = msg_id;  // extended ID =0, no remote xmit
    ecan1msgBuf[0][1] = 0;
    ecan1msgBuf[0][2] = 0;
/* ------------------------------------------------
** Builds ECAN1 payload Length and Data into buffer words [2..6]
 -------------------------------------------------- */
    ecan1msgBuf[0][2] += 6;       // message length
    ecan1msgBuf[0][3] = ((unsigned char)tdcno)<<8 | 0x11;
    ecan1msgBuf[0][4] = (unsigned)index;
    ecan1msgBuf[0][5] = (((unsigned char)expectedbyte)<<8) | (unsigned char)gotbyte;

/* Request the message be transmitted */
    C1TR01CONbits.TXREQ0=1;             // Mark message buffer ready-for-transmit
#endif
}
#endif

unsigned long get_MCU_pm (UWord16, UWord16); //JS: in rtspApi.s

void read_MCU_pm (unsigned char *buf, unsigned long addrs){
/* Read from MCU program memory address "addrs"
** and return value to "buf" buffer array of chars
** Uses W0, W1, and TBLPAG
*/
    unsigned long retval;
    retval = get_MCU_pm ((unsigned)(addrs>>16), (unsigned)(addrs&0xFFFF));
    *buf = retval & 0xFF;   // LSByte
    retval>>= 8;
    *(buf+1) = retval & 0xFF; // 2nd Byte
    retval>>= 8;
    *(buf+2) = retval & 0xFF; // 3rd Byte
    retval>>= 8;
    *(buf+3) = retval & 0xFF;  // MSByte
}

//JS20090821: new routine to write a whole row, with workaround from errata
#define PM_ROW_WRITE 		0x4001
#define CFG_BYTE_WRITE 		0x4000

extern void WriteLatch(UWord16, UWord16, UWord16, UWord16);
extern void WriteMem(UWord16);

void writePMRow(unsigned char * ptrData, unsigned long sourceAddr)
{
	int    size,size1;
	UReg32 temp;
	UReg32 tempAddr;
	UReg32 tempData;

	for(size = 0,size1=0; size < 64; size++) // one row of 64 instructions (256 bytes)
	{
		
		temp.Val[0]=ptrData[size1+0];
		temp.Val[1]=ptrData[size1+1];
		temp.Val[2]=ptrData[size1+2];
		temp.Val[3]=0; // MSB always 0
		size1+=4;

	   	WriteLatch((unsigned)(sourceAddr>>16), (unsigned)(sourceAddr&0xFFFF), temp.Word.HW, temp.Word.LW);

		/* Device ID errata workaround: Save data at any address that has LSB 0x18 */
		if((sourceAddr & 0x0000001F) == 0x18)
		{
			tempAddr.Val32 = sourceAddr;
			tempData.Val32 = temp.Val32;
		}
		sourceAddr += 2;
	}

	/* Device ID errata workaround: Reload data at address with LSB of 0x18 */
	WriteLatch(tempAddr.Word.HW, tempAddr.Word.LW, tempData.Word.HW, tempData.Word.LW);

	WriteMem(PM_ROW_WRITE);
}

void put_MCU_pm (UWord16, UWord16, UWord16, UWord16);
void wrt_MCU_pm (void);

void write_MCU_pm (unsigned char *buf, unsigned long addrs){
/* Write to MCU program memory address "addrs"
** 4th byte is always zero.
*/
    UReg32 data;
    data.Val[0] = *(buf);
    data.Val[1] = *(buf+1);
    data.Val[2] = *(buf+2);
    data.Val[3] = 0;                // upper byte not real, must be zero
    put_MCU_pm ((unsigned)(addrs>>16), (unsigned)(addrs&0xFFFF), data.Word.HW, data.Word.LW);
    wrt_MCU_pm ();
}

void put_MCU_pm (UWord16 addrh,UWord16 addrl, UWord16 valh,UWord16 vall) {
/* Put data into table latch
*/
    TBLPAG = addrh;
    __asm__ volatile ("tblwtl W3,[W1]");    // write data latch L
    __asm__ volatile ("tblwth W2,[W1]");    // write data latch H
    return;
}

void wrt_MCU_pm (void) {
/* Execute the program memory write sequence
*/
// Need to set interrupt level really high / lock out interrupts
    int save_SR;            //
    NVMCON = 0x4003;        // Operation is Memory Write Word
    save_SR = SR;           // Save the status register
    SR |= 0xE0;             // Raise priority to lock out  interrupts
    NVMKEY = 0x55;          // Unlock 1
    NVMKEY = 0xAA;          // Unlock 2
    NVMCONbits.WR = 1;          // Set the "Write" bit
    __asm__ volatile ("nop");   // required NOPs for timing
    __asm__ volatile ("nop");   // required NOPs for timing
    while (NVMCONbits.WR) {}    // Spin until done
    SR = save_SR;           // restore the saved status register
}

void writeConfRegByte(unsigned char data, unsigned char cfgRegister)
{
	int     ret;
	UWord16 sourceAddr;
	UWord16 val;

	sourceAddr = (UWord16)cfgRegister;
	val = 0xFF00 | (UWord16)data;

	ret = confByteErase(0xF8, sourceAddr);

	WriteLatch(0xF8, sourceAddr, 0xFFFF, val);
	WriteMem(CFG_BYTE_WRITE);
}

/* WB-11U: A/D Converter support */
//void __attribute__((__interrupt__))_ADC1Interrupt(void)
void __attribute__((interrupt,no_auto_psv))_ADC1Interrupt(void)  
{
/* This is the interrupt handler for A/D converter module 1.
** We get the value, prepare for the next sample, process for alert limit, and clear the done.
** Also: we check for an Alert condition from the MCP9801 using the RF.6 bit
*/
    IFS0bits.AD1IF = 0;         // Clear interrupt flag
// WB-11V: Activate the overtemperature alert bit
    MCU_HEAT_ALERT = 1;     // WB-11V
// WB-11V: Check for MCP9801 alert
//    if (MCU_HEAT_ALERT == 0) temp_alert |= ALERT_MASK_MCU;
// WB-11V: end

    if ((AD1CHS0 & 0x1F) ==  TINO_TEMP1) {     // AN16(pin2)
        tino1 = ADC1BUF0;            // Read the conversion result
        AD1CHS0 = TINO_TEMP2;        // switch to AN17(pin3)
// WB-11V: Temperature alert
        if (tino1_limit != 0) {     // if we have set the limit
            if (tino1 > tino1_limit) { // if we are over limit
                temp_alert |= ALERT_MASK_TINO1;        // set the alert
            } else temp_alert &= ~ALERT_MASK_TINO1;    // else clear the alert
        } // end if processing Temperature Alert
// WB-11V: End Temperature alert
    } else {
        tino2 = ADC1BUF0;            // Read the conversion result
        AD1CHS0 =  TINO_TEMP1;       // switch back to AN16 (pin2)
// WB-11V: Temperature alert
        if (tino2_limit != 0) {     // if we have set the limit
            if (tino2 > tino2_limit) { // if we are over limit
                temp_alert |= ALERT_MASK_TINO2;        // set the alert
            } else temp_alert &= ~ALERT_MASK_TINO2;    // else clear the alert
        } // end if processing Temperature Alert
    } // endl else channel 2 check
// WB-11V: End Temperature alert
// WB-11V: Check for MCP9801 Alert
    if (MCU_HEAT_ALERT == 0) {      // stays low if alert condition
        temp_alert |= ALERT_MASK_MCU;
    } else {                        // else no alert condition
        temp_alert &= ~ALERT_MASK_MCU;
    }
    MCU_HEAT_ALERT = 0;     // WB-11V turn it off until next time

    AD1CON1bits.DONE = 0;        // Clear done
}

void initialize_ad_converter(void) {
/* This routine initializes the A/D Converter SFRs for the appropriate mode */
    AD1CON1bits.ADON = 0;       // Turn OFF the A/D module
    IEC0bits.AD1IE = 0;         // Disable AD1 interrupt
    IFS0bits.AD1IF = 0;         // Clear interrupt flag
    /* ADCON1  15    = ADON = 0 == AD not operating (while setting registers)
     *         14    = <not used>
     *         13    = ADSIDL = 0 = continue while in idle mode
     *         12    = ADDMABM = 0 = DMA = scatter/gather
     *         11    = <not used>
     *         10    = AD12B == 1 = 12-bit operation
     *          9:8  = FORM == 00 = (unsigned) integer resule
     *          7:5  = SSRC == 111 = Auto-convert
     *          4    = <not used>
     *          3    = SISAM == Simultaneous sampling (0 = no)
     *          2    = ASAM  == ADC Sample Auto Start (1 = start after conversion)
     *          1    = SAMP  == Sampling
     *          0    = DONE
     */
    //AD1CON1 = 0x0004;       // 10 bit: ASAM=1 sampling starts immediately after prev. convert ends
    AD1CON1 = 0x04E4;       // 12bit + SSRC Auto + ASAM=1 auto sample/convert

    /* ADCON2  15:13 = 0 == Vrefh = AVdd, Vrefl = AVss
     *         12:11 = 0 == <not used>
     *         10    = 0 == CSCNA = No input channel scan
     *          9:8  = 0 == CHPS = Channel 0
     *          7    =      BUFS = buffer status
     *          6    = 0 == <not used>
     *          5:2  = SMPI == Increment rate for DMA (0000=every conversion)
     *          1    = BUFM == buffer fill mode
     *          0    = ALTS == Alternate Input Sample Mode
     */
    AD1CON2 = 0x0;  // Alternate sample a only

    /* ADCON3  15    = 0 == ADC clock from system clock
     *         14:13 = <not used>
     *         12:8  = SAMC = AutoSample Time Bits
     *          7:0  = ADCS = ADC Conversion Clock Select
     */
    AD1CON3 = 0x1FFF;      // Autosample 31, Tad = internal 256 Tcy

    /* ADCON4  15:3 <not used>
     *          2:0  = DMABL = Number of DMA Buffer Loc'ns per Analog Input
     */
    // AD1CON4 = 0x0;

    /* ADCHS0  15   = 0 = CH0NB = B-sample Channel 0 Negative Input Select is VrefL
     *        14:13 = <not used>
     *        12:8  = 0 = CH0SB = B-sample Channel 0 positive input select
     *         7    = 0 = CH0NA = A-sample Channel 0 Negative Input Select is VrefL
     *         6:5  = <not used>
     *         4:0  = 10000 = CH0SA = A-sample Channel 0 Positive input select AN16 (pin 2)
     */
    AD1CHS0 =  TINO_TEMP1;      // AN16(pin2)
    IEC0bits.AD1IE = 1;             // Enable AD1 interrupt
    AD1CON1bits.ADON = 1;       // Turn on the A/D module
}

/* WB-11U: end A/D Converter support */


//*****************************************************************************************************************//
/* REVISION HISTORY MOVED FROM BEGINNING OF FILE */
//      17-Feb-2009 thru 19-Feb-2009, W. Burton
//          Firmware ID is 0x11 0x58 (11 X)
//          Add CANBus messge to report HPTDC Configuration (C_RS_CONFIGTDCx)
//          Add CANBus message to report HPTDC Control word. (C_RS_CONTROLTDCx)
//          Add facility to copy enable_final[][] into hptdc_control[][] when enable_final is used by reset_hptdc.
//          "Alarm" is now "Alert" for consistency.
//      30-Jan-2009 W. Burton
//          Firmware ID is 0x11 0x57 (11 W)
//          Add CAN1 error checking / interrupts.
//      14-Jan-2009 thru 14-Jan-2009, W. Burton
//          Firmware ID is still 0x11 0x56 (11V)
//          Remove extraneous CAN1 routines send_CAN1_write_reply(), send_CAN1_alert(), send_CAN1_diagnostic;
//          these functions are now performed using send_CAN1_message().
//      10-Dec-2008 thru 17-Dec-2008, W. Burton
//          Firmware ID is 0x11 0x56 (11V)
//          Add C_WS_TEMPALARMS: Write Temperature alarm limits.
//              Increase U37 (MCU Temperature) "Fault Queue" to 4 to avoid chattering.
//              Message 1x7 length 2 09 mm issued approx. once per 5 seconds when an
//              over-temperature limit condition appears.
//              mm is bit field indicating which limit(s) exceeded (bit 0 = MCU, 1= TINO1, 2 = TINO2).
//              Note that MCP9801 temperature chip uses only 9-MS bits for alarm setting.
//              MCP9801 Hysteresis(low limit) register as automatically set to 5 deg C below upper limit.
//          Add C_WS_TOGGLETINO: Toggle_TINO_TEST_MCU line function to CANBus protocol.
//              This feature requires FPGA firmware date 2008-12-11 or later (with TINO_TEST_PLD line
//              held high.
//              Generated pulses are approx 130 microseconds wide.
//              Multiple pulses occur at approx 130 microsecond intervals.
//              Location "tdcpowerbit" is now called ecsrwbits (for TDC power and TINO TEST).
//          Fix HPTDC Configuration readback compare indexing error.
//      12-Nov-2008 thru 14-Nov-2008, W. Burton
//          Firmware ID is 0x11 0x56 (11V)
//          Add recognition of "Broadcast" messages to CANBus protocol handlers.
//              This uses additional filtering and message buffer resources.
//      10-Sep-2008, W. Burton
//          Firmware ID is still 0x11 0x55 (11U)
//          Changed Jo's timer stuff from using Timers2/3 to using Timers6/7 because Timer2/3 is the
//              only set which will drive AD#1
//          A/D conversion experiments with different modes - now working with sample/convert interrupts.
//      09-Sep-2008, W. Burton
//          Firmware ID is still 0x11 0x55 (11U)
//          Add A/D conversion support (Analog inputs from TINO)
//              initialize_ad_converter(), sample_ad_converter()
//          Add A/D converstion result to Board Status (0xB0) and temperature (0x09) messages.
//      08-Sep-2008, W. Burton
//          Firmware ID is still 0x11 0x55 (11U)
//          replaced HPTDC.INC file with version from Jo Schambach's email 9/5/2008
//      05-Sep-2008, W. Burton
//          Firmware ID is still 0x11 0x55 (11U)
//          Use consolidated CANBus HLP header file and define TDIG symbol.
//      04-Sep-2008, W. Burton
//          Firmware ID is still 0x11 0x55 (11U)
//          Add read_threshhold message processing (C_RS_THRESHHOLD)
//      03-Sep-2008, W. Burton
//          Firmware ID is still 0x11 0x55 (11U)
//          Limit reading MCU memory to available range to avoid spurious reset (C_RS_MCUMEM).
//      02-Sep-2008, W. Burton
//          Firmware ID is 0x11 0x55 (11U)
//          Add MCU memory address limits to C_WS_TARGETMCU
//          Symbolify the "Magic Address"
//          Add function MCU Memory Checksum (from TCPU)
//          Add function EEPROM2 Memory Checksum (From TCPU)
//      29-Aug-2008
//          Start implementing Read EEPROM2 checksum function (C_RS_EEP2CKSUM)
//      29-Aug-2008,
//          Firmware ID is 0x11 0x54 (11T)
//          Lengthen startup clock select (per Jo Schambach version - BNL_20080809)
//      05-Aug-2008,
//          Firmware ID is 0x11 0x54 (11T)
//          Fix EEPROM2 write check for non-multiple-of-8 byte writes.
//          Add C_RS_EEPROM2 to read EEPROM2 contents
//      01-Aug-2008, version from J. Schambach (email 7/24/2008)
//          Add a delay before switching to external (tray) clock.
//          Include CRT.S to source files in downloaded code.
//          Rename project names to exclude version number in the name (so it fits in source tree)
//      22-Jul-2008, W. Burton
//          Firmware ID is 0x11 0x52  (11R)
//          Add code for clock-fail detect, clock source switching via CANBus, clock status request via CAN.
//          send_CAN1_ALERT() function removed; using send_can1_message
//          Clean-up code for FPGA SEU (Checksum) Detect.  Reinitialize at FPGA reload.
//          Move bulk of change history to bottom of file
//      26-Jun-2008, W. Burton
//          Firmware ID is 0x11 0x50  (11P)
//          Clean up compiler warnings and dead code.
//          Restructure project file for creation of download image from same set of source files.
//          Bring in Program space addresses from Jo's download version and make address conditional.
//      26-Jun-2008, W. Burton.
//          Firmware ID is 0x11 0x4E  (11N)
//          Updated comment block to include information from Drupal and emails about this version (11N)
//          This is the version from archive TDIG-F_BNL_20080619_0.zip from J. Schambach.
//          Automatic switchover process:
//              1. Normal MCU code image #1 inititalization is performed.  This includes time-staggered startup dependent on
//                  board position.
//              2. The normal CANBus sign-on message is issued.
//              3. The automatic-switchover timer is started.
//              4. The normal message processing main loop is entered.
//              5. If the switchover timer expires a special section of the main-loop code is entered which examines the
//                  magic-number location in high program memory.
//                  If the location contains a matching magic-number, a switch to the second code image is performed.
//                      This is as if a properly-formed CANBus message to do the switch had been received.
//                  An additional special CANBus message has been defined which erases and writes the magic-number location.
//                  By writing a non-matching value to the location, the switchover can be prevented.
//                  The interpretation of this CANBus message is only available in Code Image #1.
//          19-Jun-2008:
//              Magic Number address write (C_WS_MAGICNUMWR)
//              ISR for automatic code-switching timer (conditional, not for download)
//              Place HPTDC_CONTROLBYTES into special address 0x3400.
//              Place HPTDC_SETUPBYTES at address 0x3000.
//          27-May-2008:
//              Firmware ID is 0x11 0x4D (11M)
//              Fixed HPTDC reset sequence to enable channels after reconfiguration (version 11M)
//          01-May-2008:
//              Aux Inputs configuration (cfig_test018).
//          24-Apr-2008:
//              Configuration cfig_test014.
//          22-Apr-2008:
//              Firmware ID is 0x11 0x4C (11L)
//              Added new command codes for writing HPTDC configuration and control bits to flash (program) memory
//              C_WS_TARGETCFGx (0x44, 0x45, 0x46, 0x47) - configuration bits
//              C_WS_TARGETCTRLx (0x48, 0x49, 0x4A, 0x4B) - control bits
//              Adds provision for 3 different configurations for the 3 different HPTDCs stored in program memory.
//              Copy of saved HPTDC configurations to RAM prior to use.
//              Do not configure local-header for boards 0 and 4.
//          13-Mar-2008:
//              Firmware ID is
//              New prototype for init_regs_FPGA()
//              Fixed spelling errors.
//              DCONFIG_OUT no longer inverted.
//              All FPGA register initializations into init_regs_FPGA
//              Added CRC_ERROR detection and CAN_Alert message.
//              HPTDC reset shortly before CAN sign-on mesage.
//              Init_Regs_FPGA register dependent initialization for board position 0 and 12.
//              Modified reset sequence to disable channels during reset; use final_control_word at end of sequence.
//          10-Mar-2008:
//              Firmware ID is 0x11 0x49 (11I)
//              use cfig_test016x
//              DAC output now initializes to 2500mV
//              ESCR bit comments fixed.
//              Reset spins() in JTAG now reduced to spin(1)
//              Added new "execute reset sequence" CANBus commands
//              Board 0 power-on delay increased to 2 seconds.
//              Local header turned on for TDC boards 0 and 4
//          13-Feb-2008:
//              Version 11H from Blue Sky Electronics.
//
//      15-Oct-2007, W. Burton, Firmware ID is 0x11 x48 (11H)
//          Use read_hptdc_status() routine instead of duplicate code.
//          Rename OSCCONH to OSCCON+1 and OSCCONL to OSCCON (in routine switch_osc()) so we don't need modified .GLD
//          Changes marked //WB-11H
//          TDIG-Board.h file edited to default use "new" tdc numbering scheme.
//      11-thru-13-Oct-2007, W. Burton.  Firmware ID is 0x11 0x48 (11H)
//          If the second (download) image is running, we do not want to download over it.
//          CPU starts up using internal oscillator; then examines jumpers and changes to Board or Tray external Oscillator.
//          CLOCK JUMPER (PINS 1-2) IS EXAMINED ONLY ONCE AT START-UP TO DETERMINE CLOCK SOURCE!
//          For DOWNLOADED code (second image), the OSCILLATOR IS NOT CHANGED
//          Added reset_TAP() before and after HPTDC JTAG access.
//          Set CPU priority=0 and select interrupt vector depending on DOWNLOAD_CODE
//             "normal" code = standard interrupts; "DOWNLOAD_CODE" = alternate interrupts.
//          "Download" code has id 0x91 0x48 (91H)
//          Changes marked //WB-11H
//      10-Oct-2007, J. Schambach Firmware ID is 0x11 0xFA
//			Fixed DMA initialization
//			Added spin(0) after first message in response to get status
//			Mask of the length field properly in decoding received CAN messages
//			Add ifdef statements around all sections of the code that shouldn't be part
//				of downloaded code (conditional: DOWNLOAD_CODE)
//			All changes are marked by "//JS"
//      19-Sep-2007, W. Burton Firmware ID is 0x11 0xF
//          Revised HPTDC reset sequence in TDIG-F_JTAG.c
//      08-Sep-2007, W. Burton Firmware ID is 0x11 0xF
//          Delayed power-on is defined (approx 1 second per board position)
//          Updated filenames for RevF boards in preparation for new release
//          Fixed DCONFIG_OUT not functioning as expected.
//          Fixed false reports of reconfiguration readback error.
//      07-Sep-2007, W. Burton, Version 11E
//      06-Sep-2007, W. Burton, Version 11E
//          Copy UCONFIGI bit to DCONFIGO bit to allow testing of corresponding LVDS signals (define UDCONFIGBITS)
//          Alternate JTAG timing (conditional SLOWERJTAG in tdig-d-jtag.h - defined 9/7/07)
//          Revised HPTDC numbering scheme per Jo Schambach 6-Sep-07 email (conditional REVTDCNUMBER)
//          Conditional code for delayed TDC power-on (conditional on DELAYPOWER definition)
//          Correct reply message for Read Temperature and Get Status TDC.
//          CANBus termination state must be OFF.
//      29-Jun-2007, W. Burton
//          Add FPGA read ID register to "Request Firmware Identifiers" (C_RS_FIRMWID) reply.
//          Add defining conditionals for type of PIC chip to be used
//          Move CAN Bus High Level Protocol (HLP) codes and defines to #include file TDIG-D_CAN_HLP3.h
//          Remove FPGA Ident byte from MCU startup Alert message.
//          Make sure "ALT" interrupt vector selector set in INTCON2 prior to jump to image 2;
//          Make sure to use "Standard" interrupt vector table in this image.
//      19-Jun-2007, W. Burton
//          Fix HPTDC Configuration/Reset indexing which caused "parity error" when getting
//              HPTDC status
//      09-Jun-2007, W. Burton
//          More work on MCU reprogram/reset
//          Ease restriction on EEPROM programming, 0 < bytes <= 256 is OK.
//      07-Jun-2007, W. Burton
//          Work on MCU erase function.
//          Add C_RS_MCUMEM (Read, len:5, msg: 0x4C <adrL> <adr2> <adr3> <adrM>)
//          to read and report 4 bytes from MCU address specified.
//          Reply is (Read Reply, len: 5, msg: 0x4C <memL> <mem2> <mem3> <mem4=00>)
//          This can also be used to read the MCU ID bytes from F80010 thru F80016.
//      06-Jun-2007, W. Burton
//          Added routine jumpto() to give this code a link into MCU2 code; Added definition
//          of MCU2ADDRESS in tdig-d_board.h to define the MCU2 entry point start address
//      19-May-2007, W. Burton
//          Reset MCU message added.
//          Reconfigure FPGA payload code changed.
//      18-May-2007, W. Burton
//          FIRMWARE ID added; Read FPGA Regs fixed.
//      17-May-2007, W. Burton
//          Work on MCU Reprogramming begins.
//      14-May-2007, W. Burton
//          Implement TDC Write Control Word (40 bits) C_WS_CONTROLTDCx messages.
//          Migrate CAN parameters for long-cable from TCPU.
//          EEPROM #2 Altera and STMicro.
//      11-May-2007, W. Burton
//          Implement FPGA Reset and FPGA Write Register messages.
//      10-May-2007, W. Burton
//          Corrected status return from FPGA Reconfiguration command.
//      04-May-2007, W. Burton
//          Reworked SPI write.  Enabled Reconfiguration commands.
//      03-May-2007, W. Burton
//          Make sure HPTDC is selected via jumpers.
//          LED D6 is now HPTDCs config readback OK when on.
//      30-Apr-2007, W. Burton
//          Make sure FPGA "MCU_PLD" registers are initialized.
//          Rework EEPROM write sequence.
//  Program #11B -
//      27-Apr-2007, W. Burton
//          More work on TDIG-D EEPROM #2 Write
// Program # 11A -
//      22-Apr-2007, W. Burton
//          Convert to use new TDIG-D_SPI.c routines
//      19-Apr-2007
//          More work on EEPROM #2 Write
//      18-Apr-2007
//      Add double-read for FIFO status during data readout transfer
//      ADD CONDITIONALS:
//         DODATATEST if defined, tries to read-out data.
//         SENDCAN if defined, sends CAN messages
//      13-Apr-2007
//      Add ability to read out data via CAN bus.
//          After initialization of HPTDC, Issue the FIFO Reset stuff (Strobe_10, Strobe_9)
//          In "idle" loop, check for data available and send it.
//          Code is conditionalized on DODATATEST being defined.
//      IF Board ID= 0 or 4 then Select this board as First in readout chain (Config_0 bit 1)
//      Fix use of FPGA CONFIG_1 register for selection of MCU or BBlaster in control of JTAG.
//      Changed "write config_1" to select_hptdc();
//      Update Local Oscillator and JTAG HPTDC # selection on change during Idle loop, Display JUMPERS in LEDs (0..3)
//      12-Apr-2007
//      Read and report MCU_PLD ID register (7) in 1 byte of startup message
//      11-Apr-2007
//      Add C_RS_functions for Read Serial Number, Switches, ECSR.
//      10-Apr-2007
//      Confirm operation of MCU-controlled CAN termination (MCU_SEL_TERM)
//      29-Mar-2007
//      Move HPTDC/JTAG routines to TDIG-D_JTAG.c file; restructure .H files; add symbolic names for some magic numbers.
//      27-Mar-2007
//      Restructure send_CAN1_message() and decode "writes"
//      26-Mar-2007
//      Bring into agreement (more or less) with ver10K
//      13-Feb-2007
//      Begin implementation of CANbuf HLP version 3.0 download protocols.
// Program #10F (still) - 14-Feb-2007
//      Fix problem with HPTDC configuration array indexing.
//      Turn on LED 0 when HPTDC configuration complete
//      Wait for button press after intitializing HPTDC power and before doing configuration.
//      Be sure to copy jumper settings to config_1 bits when done with program configuration
// Program #10F (still) - 10-Feb-2007
//      Change control words to only enable channel 1
//      FIFO test now writes 64, then reads 64.
//      CAN Data messages get sent AND Diagnostic message gets sent each cycle.
//      We will probably want this as part of Self-Test with an alert if it does not
//      work correctly.
// Program #10F (still) - 09-Feb-2007
//      Added pushbutton (SW1) to:
//          1. First push after initialization, triggers start of FIFO loop.
//          2. During FIFO loop, pressing button causes STROBE_12 (after read cycle)
//      CAN messages inhibited.
// Program #10F - 8-Feb-2007
//      MCU FIFO self test with CAN diagnostic
// Program #10E - 31-Jan-2007 thru 2-Feb-2007, WDB
//      Conditionalize EEPROM #2 stuff and JTAG stuff
//      Add first test of MCU-to-FPGA code.
//      define FPGA_WRITE_ADDRA as the first FPGA address to write to.
//      define FPGA_WRITE_ADDRB as the second FPGA address to write to.
//      define FPGA_WRITE_ADDRC as the third FPGA address to write to.
//      define FPGA_READ_ADDRA as the first FPGA address to read from.
//      define FPGA_READ_ADDRB as the second FPGA address to read from.
//      define FPGA_READ_ADDRC as the third FPGA address to read from.
//      Program writes/reads, and compares.  DIFFERENCE is reported in LEDs
//
// Program #10d - 23-Jan-2007, WDB
//      Try reading out the EEPROM #2 ID code.
//      Was the DAC change really beneficial?
//      Convert DAC setting routine to use ADDRESS of value to be set; initialization
//      changed to copy constant to "current value".
// Program #10c - 17-Jan thru 23-Jan-2007, WDB
//      Take HPTDC Configuration (Setup) from Include file.
//         To Incorporate new HPTDC Setup (Configuration) information:
//          1) Use the spreadsheet "HPTDC JTAG configurator_PIC24.xls" to manipulate
//             the configuration and format bits for inclusion.
//          2) COPY the cells I3 through I43 from the spreadsheet.
//          3) PASTE the cells (as text) into the file "HPTDC.INC".
//          4) Recompile (BUILD ALL) the TDIG project.
//             After download and release of reset, the configuration will be loaded into
//             the 3 HPTDCs.//      Inhibit CAN messages for bench testing.
//      CAN messages are inhibited for bench testing.
//      Configure and Control HPTDCs is working.
//		Add routine fixup_parity to scan bitstream and set parity.
//		Continue working on "Configuring HPTDCs"
// Program #10c - 16-Jan-2007, WDB
//		Add code to read "STATUS" of HPTDCs.
//		Rearrange startup code and process CAN message to set threshhold
// Program #10b - 15-Jan-2007, WDB
//      ID Code finally works.
// Program #10b - 6-Jan-2007, WDB
//      Now try getting HPTDC ID code thru JTAG for all 3 HPTDCs
//      Parameterize CAN messages.
//      Simplify CAN transmit message assembly.
//      Rename send_CAN_alert() to send_CAN1_alert()
//		Echo matching input messages.
//		Add conditional code based on ECO14_SW4 to compensate for bit-swap in Board ID switch.
// Program #10b1 - 4-Jan-2007, WDB
//      20MHz internal clocking
//      Incorporate changes from "Program 7k thru Program 7n"
//      This takes care of small CPLD enable requirement.
//		Reformat Startup CAN message per HLP_2 document (adds routine send_CAN_alert).
// Program #10a - Begin implementing JTAG configuration of TDC chips.
// Program #9e - Interpret and process the "THRESHHOLD" command
// 		Startup messge includes board-id in 1st word.
//		This amounts to: Spinning until we have a message.
//		Examining the message ID
//			If it is the Threshhold message, write the value to the DAC
//			Set up and send the response.
//		Include "board number" in message mask / filter.
//		Send "ERROR" message to all unrecognized messages.
// Program #9d - Loopback
// Program #9c -
//		Try changing the CAN data transmitted.
// Program #9b -
//		Move board-specific I2C stuff into TDIG-D_I2C.c
//		CAN initialization so CAN Receive doesn't crash.
//		CAN send a message!
// Program #8j -
//      Redo local clock for 40 MHz. check timing using RC15/Osc2 pin 40.
// Program #8h -
//		I2C spin(delay) are commented out so they take place very fast.
//		TDIG power-on request is disabled.
//      MCU-Test blink is also on D9 (RC15/Osc2, pin 40)
//      CAN bus:
//			Set up.
// Program #7g - Add "Reset" pulse delayed after enabling TDC power
// Program #6f - Changed constant names to the form _I2Cx
//      for use with new C30 and libraries.
//      Change routine write_MCP23008() to write_device_I2C1();
//		16-Nov-06, WDB
// Program #6 - Set RB[0..3] to Outputs.
//		Read jumpers and copy the settings to LEDs and
//      MCU_PLD_DATA[0..3] == RB[0..3].
// Program #5 - Read and display User ID bytes from hardware.
//      Then use "amber" led (D7) to indicate TDC Power status
// Program #4 - Read Silicon Serial Number and display in LEDs (slowly) and
//      Configure and read Temperature Monitor (U37).
//		6-Nov-06, WDB Converted from Program #3
// Program #3 - Initialize Header/Switch/Button input (I2C Device)
//		Read rotary switch and copy to corresponding LED output.
//		Check pushbutton for set and count when set.
//      When count reaches BUTTONLIMIT, toggle TDC power.
//   4-Nov-06, WDB Converted from Program #2
// Program #2 - Initialize and ramp-up DAC
//   4-Nov-06, WDB Converted from Program #1
// Program #1 - Initialize I2C, I2C Device, and cycle LEDs thru U34
//   1-Nov-06, WDB Converted from Program #0
// Program #0 - blink the LED (D9) associated with RC15/OSC2
