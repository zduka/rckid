#include <avr/io.h>

#define I2C_ADDRESS 0x43
#define BOOTLOADER_SIZE 0x200

#define CMD_RESERVED 0x00
#define CMD_WRITE_BUFFER 0x01
#define CMD_WRITE_PAGE 0x02
#define CMD_READ_PAGE 0x03
#define CMD_CLEAR_INDEX 0x04
#define CMD_RESET 0x05

#define I2C_DATA_MASK (TWI_DIF_bm | TWI_DIR_bm) 
#define I2C_DATA_TX (TWI_DIF_bm | TWI_DIR_bm)
#define I2C_DATA_RX (TWI_DIF_bm)
#define I2C_START_MASK (TWI_APIF_bm | TWI_AP_bm | TWI_DIR_bm)
#define I2C_START_TX (TWI_APIF_bm | TWI_AP_bm | TWI_DIR_bm)
#define I2C_START_RX (TWI_APIF_bm | TWI_AP_bm)
#define I2C_STOP_MASK (TWI_APIF_bm | TWI_DIR_bm)
#define I2C_STOP_TX (TWI_APIF_bm | TWI_DIR_bm)
#define I2C_STOP_RX (TWI_APIF_bm)

/** Upon start the bootloader checks the AVR_IRQ pin. If pulled low, the bootloader will be entered, otherwise the main app code starts immediately. The AVR_IRQ is generally held high by the RPi at 3V3 (which still registers as logical 1) to enable the AVR to signal interrupts so pulling it low works as exceptional case. 
*/
static bool bootloaderRequested() {
    return ! (VPORTA.IN & PIN4_bm);
}


/** The I2C bootloader actual code.
 */
static void bootloader() {
    uint8_t command;
    uint8_t arg;
    uint8_t buffer[MAPPED_PROGMEM_PAGE_SIZE];
    uint8_t i = 0;
    // enable the display backlight when entering the bootloader for some output (like say, eventually an OTA:) Baclight is connected to PA5 and is active high
    // TODO TODO 

    // initialize the I2C in slave mode w/o interrupts
    // turn I2C off in case it was running before
    TWI0.MCTRLA = 0;
    TWI0.SCTRLA = 0;
    // make sure that the pins are nout out - HW issue with the chip, will fail otherwise
    PORTB.OUTCLR = 0x03; // PB0, PB1
    // set the address and disable general call, disable second address and set no address mask (i.e. only the actual address will be responded to)
    TWI0.SADDR = I2C_ADDRESS << 1;
    TWI0.SADDRMASK = 0;
    // enable the TWI in slave mode, enable all interrupts
    TWI0.SCTRLA = TWI_DIEN_bm | TWI_APIEN_bm | TWI_PIEN_bm  | TWI_ENABLE_bm;
    // bus Error Detection circuitry needs Master enabled to work 
    TWI0.MCTRLA = TWI_ENABLE_bm;   
    // start the main loop that processes the commands
    while (true) {
        // wait for I2C interrupt
        while (TWI0.SSTATUS & (TWI_DIF_bm | TWI_APIF_bm) == 0) {} 
        uint8_t status = TWI0.SSTATUS;
        // sending data to accepting master is on our fastpath as is checked first, the next byte in the buffer is sent, wrapping the index on 256 bytes 
        if ((status & I2C_DATA_MASK) == I2C_DATA_TX) {
            TWI0.SDATA = buffer[i++];
            TWI0.SCTRLB = TWI_SCMD_RESPONSE_gc;
            i = i % MAPPED_PROGMEM_PAGE_SIZE;
        // a byte has been received from master. Store it and send either ACK if we can store more, or NACK if we can't store more
        } else if ((status & I2C_DATA_MASK) == I2C_DATA_RX) {
            switch (command) {
                case CMD_RESERVED:
                    command = TWI0.SDATA;
                    break;
                case CMD_WRITE_BUFFER:
                    buffer[i++] = TWI0.SDATA;
                    break;
                default:
                    arg = TWI0.SDATA;
                    break;
            }
            TWI0.SCTRLB = TWI_SCMD_RESPONSE_gc;
            i = i % MAPPED_PROGMEM_PAGE_SIZE;
        // master requests slave to write data, simply say we are ready as we always send the buffer
        } else if ((status & I2C_START_MASK) == I2C_START_TX) {
            TWI0.SCTRLB = TWI_ACKACT_ACK_gc + TWI_SCMD_RESPONSE_gc;
        // master requests to write data itself, first the command code
        } else if ((status & I2C_START_MASK) == I2C_START_RX) {
            command = CMD_RESERVED; // command will be filled in 
            TWI0.SCTRLB = TWI_SCMD_RESPONSE_gc;
        // sending finished, there is nothing to do 
        } else if ((status & I2C_STOP_MASK) == I2C_STOP_TX) {
            TWI0.SCTRLB = TWI_SCMD_COMPTRANS_gc;
        // receiving finished, if we are in command mode, process the command, otherwise there is nothing to be done 
        } else if ((status & I2C_STOP_MASK) == I2C_STOP_RX) {
            TWI0.SCTRLB = TWI_SCMD_COMPTRANS_gc;
            // TODO play with busy flag
            switch (command) {
                case CMD_WRITE_PAGE: {
                    uint8_t * page = (uint8_t*)(MAPPED_PROGMEM_START + arg * MAPPED_PROGMEM_PAGE_SIZE);
                    for (uint8_t i = 0; i < MAPPED_PROGMEM_PAGE_SIZE; ++i)
                        page[i] = buffer[i];
                    _PROTECTED_WRITE_SPM(NVMCTRL.CTRLA, NVMCTRL_CMD_PAGEERASEWRITE_gc);
			        while(NVMCTRL.STATUS & NVMCTRL_FBUSY_bm);                
                    break;
                }
                case CMD_READ_PAGE: {
                    uint8_t * page = (uint8_t*)(MAPPED_PROGMEM_START + arg * MAPPED_PROGMEM_PAGE_SIZE);
                    for (uint8_t i = 0; i < MAPPED_PROGMEM_PAGE_SIZE; ++i)
                        buffer[i] = page[i];
                    i = 0;
                    break;
                }
                case CMD_CLEAR_INDEX:
                    i = 0;
                    break;
                case CMD_RESET:
                    _PROTECTED_WRITE(RSTCTRL.SWRR, RSTCTRL_SWRE_bm);
                default:
                    // nothing to be done for the rest
                    break;
            }
        // error - not sure what to do, perhaps reset the bootloader? 
        } else {
            // TODO
        }
    }
}


/* Define application pointer type */
typedef void (*const app_t)(void);

/** Bootloader initialization. 
 
    We bypass the main() and standard files so that w
*/
__attribute__((naked)) __attribute__((constructor)) 
void boot() {
    /* Initialize system for C support */
    asm volatile("clr r1");
    // do we need bootloader? 
    if (bootloaderRequested()) 
        bootloader();
    // enable the boot lock so that app can't override the bootloader and then start the app
    NVMCTRL.CTRLB = NVMCTRL_BOOTLOCK_bm;
    // start the application
    app_t app = (app_t)(BOOTLOADER_SIZE / sizeof(app_t));
    app();    
}


