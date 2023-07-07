#include "platform/platform.h"
#include "platform/peripherals/nrf24l01.h"
#include "platform/peripherals/ssd1306.h"

using namespace platform;

/** Chip Pinout
               -- VDD             GND --
         DEBUG -- (00) PA4   PA3 (16) -- SCK
               -- (01) PA5   PA2 (15) -- MISO
               -- (02) PA6   PA1 (14) -- MOSI
               -- (03) PA7   PA0 (17) -- UPDI
               -- (04) PB5   PC3 (13) -- NRF_CS
               -- (05) PB4   PC2 (12) -- NRF_RXTX
       NRF_IRQ -- (06) PB3   PC1 (11) -- 
               -- (07) PB2   PC0 (10) -- 
           SDA -- (08) PB1   PB0 (09) -- SCL
 */
class Repeater {
public:

    static constexpr gpio::Pin NRF_CS = 13;
    static constexpr gpio::Pin NRF_RXTX = 12;
    static constexpr gpio::Pin NRF_IRQ = 6;

    static constexpr gpio::Pin DEBUG_PIN = 0;

    static inline NRF24L01 nrf_{NRF_CS, NRF_RXTX};
    static inline SSD1306 oled_;
    static inline uint16_t msgs_;
    static inline uint16_t msgsNow_;
    static inline uint16_t msgsLastSec_;
    static inline uint8_t buffer_[32];


    static void initialize() {
        // set CLK_PER prescaler to 2, i.e. 10Mhz, which is the maximum the chip supports at voltages as low as 3.3V
        CCP = CCP_IOREG_gc;
        CLKCTRL.MCLKCTRLB = CLKCTRL_PEN_bm; 
        //cpu::delayMs(100);
        // initialize basic peripherals
        gpio::initialize();
        spi::initialize();
        i2c::initializeMaster();

        // initialize the RTC that fires every second for a semi-accurate real time clock keeping on the AVR, also start the timer
        //RTC.CLKSEL = RTC_CLKSEL_INT1K_gc; // select internal oscillator divided by 32
        //RTC.PITINTCTRL |= RTC_PI_bm; // enable the interrupt
        //RTC.PITCTRLA = RTC_PERIOD_CYC32768_gc | RTC_PITEN_bm;



        gpio::output(DEBUG_PIN);
        gpio::high(DEBUG_PIN);
        
        while (true) {
            cpu::delayMs(500);
            gpio::low(DEBUG_PIN);
            cpu::delayMs(500);
            gpio::high(DEBUG_PIN);
        }

        // initialize the OLED display
        oled_.initialize128x32();
        oled_.normalMode();
        oled_.clear32();
        oled_.write(0,0,"NRF REPEATER");
        oled_.write(0,2, "Total:");
        oled_.write(0,3, "Last:");
        oled_.write(64, 2, "Errors:");

/*
        if (nrf_.initialize("AAAAA", "BBBBB")) 
           oled_.write(64, 0, "NRF OK");
        else 
            oled_.write(64, 0, "NRF FAIL"); 
        nrf_.standby();
        nrf_.enableReceiver();
    */
    }

    static void loop() {
        /*
        if (gpio::read(NRF_IRQ)) {
            nrf_.clearDataReadyIrq();
            while (nrf_.receive(buffer_, 32)) {
                ++msgs_;
                ++msgsNow_;
            }
        }
        */
        if (RTC.PITINTFLAGS == RTC_PI_bm) {
            RTC.PITINTFLAGS = RTC_PI_bm;
            if (gpio::read(DEBUG_PIN))
                gpio::low(DEBUG_PIN);
            else 
                gpio::high(DEBUG_PIN);
            oled_.write(35,2, msgs_);
        }

    }


}; // Repeater

void setup() {
    Repeater::initialize();
}

void loop() {
    Repeater::loop();
}

#ifdef FOOBAR


// 0X3C+SA0 - 0x3C or 0x3D
#define I2C_ADDRESS 0x3C

// Define proper RST_PIN if required.
#define RST_PIN -1


volatile bool nrf_irq = false;
volatile bool lora_irq = false;
volatile bool tick = false;
uint8_t ticks = 0;
uint16_t nrf_received = 0;
uint16_t nrf_errors = 0;
uint16_t lora_received = 0;
uint16_t lora_errors = 0;
uint8_t nrf_x = 0;
uint8_t lora_x = 0;
bool tickMark = false;

void nrfIrq() {
    nrf_irq = true;
    //gpio::high(DEBUG_PIN);
}

void loraIrq() {
    lora_irq = true;
}

ISR(TCB0_INT_vect) {
    TCB0.INTFLAGS = TCB_CAPT_bm;
    if (++ticks >= 100) {
        tick = true;
        ticks -= 100;
    }
}

void setup() {
    cpu::delayMs(100);
    gpio::initialize();
    spi::initialize();
    i2c::initializeMaster();
  
    oled.initialize128x32();
    oled.normalMode();
    oled.clear32();

    gpio::output(DEBUG_PIN);
    gpio::low(DEBUG_PIN);

    if (! nrf.initialize("TEST2", "TEST1"))
        oled.write(0,0," NRF FAIL");
    else
        oled.write(0,0," NRF OK");
    if (! lora.initialize()) 
        oled.write(64, 0, "LORA_FAIL");
    else
        oled.write(64,0, "LORA OK");
    gpio::input(NRF_IRQ);
    gpio::input(LORA_IRQ);
    attachInterrupt(digitalPinToInterrupt(NRF_IRQ), nrfIrq, FALLING);
    attachInterrupt(digitalPinToInterrupt(LORA_IRQ), loraIrq, RISING);

    nrf.standby();
    nrf.enableReceiver();

    lora.standby();
    lora.enableReceiver();

    // start the 1kHz timer for ticks
    // TODO change this to 8kHz for audio recording, or use different timer? 
    TCB0.CTRLB = TCB_CNTMODE_INT_gc;
    TCB0.INTCTRL = TCB_CAPT_bm;
    TCB0.CCMP = 50000; // for 100Hz    
    TCB0.CTRLA = TCB_CLKSEL_CLKDIV2_gc | TCB_ENABLE_bm;
}

void loop() {
    if (lora_irq) {
        lora_irq = false;
        lora.clearIrq();
        uint8_t msg[32];
        lora.receive(msg, 32);
        lora_received += 1;
        while (++lora_x != msg[0])
            ++lora_errors;
    }

    if (nrf_irq) {
        nrf_irq = false;
        nrf.clearDataReadyIrq();
        uint8_t msg[32];
        while (nrf.receive(msg, 32)) {
            nrf_received += 1;
            while (++nrf_x != msg[0])
                ++nrf_errors;
            //x = msg[0];
        }
        gpio::low(DEBUG_PIN);
    }
    if (tick) {
        tickMark = ! tickMark;
        tick = false;
        oled.gotoXY(0,0);
        oled.writeChar(tickMark ? '-' : '|');

        oled.gotoXY(0,1);
        oled.write(nrf_received, ' ');
        oled.gotoXY(0,2);
        oled.write(nrf_errors, ' ');
        /*
        oled.gotoXY(0,2);
        oled.write(nrf.getStatus().raw, ' ');
        oled.gotoXY(50,2);
        oled.write(nrf.readRegister(NRF24L01::CONFIG), ' ');
        */
        nrf_received = 0;
        nrf_errors = 0;


        oled.gotoXY(64,1);
        oled.write(lora_received, ' ');
        oled.gotoXY(64, 2);
        oled.write(lora_errors, ' ');
        oled.gotoXY(64, 3);
        oled.write(lora.packetRssi(), ' ');

        lora_received = 0;
        lora_errors = 0;
    }
}


#endif