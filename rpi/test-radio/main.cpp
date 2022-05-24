#include <cstdlib>
#include <iostream>
#include <cstring>

#include "peripherals/nrf24l01.h"
#include "peripherals/sx1278.h"

volatile bool irq = false;

NRF24L01 nrf{16,5};
SX1278 lora{26};

void loraIrq(int gpio, int level, uint32_t tick, SX1278 * lora) {
    irq = true;
}

void loraMain() {
    if (!lora.initialize()) 
        std::cout << "Error initializing LoRa module" << std::endl;
#ifdef ARCH_RPI
    gpioSetISRFuncEx(0, RISING_EDGE, 0, (gpioISRFuncEx_t) loraIrq, & lora);
#else
    std::cout << "NO INTERRUPT" << std::endl;
#endif

    uint8_t idx = 0;
    size_t sent = 0;
    size_t acked = 0;
    auto start = std::chrono::steady_clock::now();
    lora.enableTransmitter();
    while (true) {
        uint8_t msg[32];
        for (int i = 0; i < sizeof(msg); ++i)
            msg[i] = idx;
        ++idx;
        lora.transmit(msg, sizeof(msg));
        ++sent;
        cpu::delay_ms(20); // for 12800bps
        if (irq) {
            lora.clearIrq();
            ++acked;
            irq = false;
        }
        auto t = std::chrono::steady_clock::now();
        auto d = std::chrono::duration_cast<std::chrono::milliseconds>(t - start);
        if (d.count() >= 1000) {
            std::cout << sent << "/" << acked << " packages sent/acked." << std::endl;
            sent = 0;
            acked = 0;
            start = t;
        }
    }
}


void nrf24l01irq(int gpio, int level, uint32_t tick, NRF24L01 * radio) {
/*    
    std::cout << "interrupt " << radio << std::endl;
    auto status = radio->getStatus();
    if (status.txDataFailIrq())
        std::cout << "Data sent failure" << std::endl;
    if(status.txDataSentIrq())
        std::cout << "Data sent OK" << std::endl;
    std::cout << "status " << (int)status.raw << std::endl;
    irq = true;
*/
    irq = true;
}

void nrfMain() {
    if (! nrf.initialize("TEST1", "TEST2")) {
        std::cout << "Failed to initialize NRF" << std::endl;
    }
    nrf.standby();
#ifdef ARCH_RPI
    gpioSetISRFuncEx(6, FALLING_EDGE, 0, (gpioISRFuncEx_t) nrf24l01irq, & nrf);
#else
    std::cout << "NO INTERRUPT" << std::endl;
#endif

    uint8_t idx = 0;
    size_t sent = 0;
    size_t acked = 0;
    auto start = std::chrono::steady_clock::now();
    nrf.enableTransmitter();
    while (true) {
        uint8_t msg[32];
        for (int i = 0; i < sizeof(msg); ++i)
            msg[i] = idx;
        ++idx;
        if (nrf.transmitNoAck(msg, sizeof(msg)))
            ++sent;
        cpu::delay_ms(4);
        if (irq) {
            nrf.clearIrq();
            ++acked;
            irq = false;
        }
        auto t = std::chrono::steady_clock::now();
        auto d = std::chrono::duration_cast<std::chrono::milliseconds>(t - start);
        if (d.count() >= 1000) {
            std::cout << sent << "/" << acked << " packages sent/acked." << std::endl;
            sent = 0;
            acked = 0;
            start = t;
        }
    }
}


/** Transmitter test. 
 
    Sends the given ammount of bytes per second for ever. 
 */

int main(int argc, char * argv[]) {
    gpio::initialize();
    spi::initialize();
    if (argc == 2) {
        if (strcmp(argv[1], "lora") == 0) {
            std::cout << "lora:" << std::endl;
            loraMain();
        } else if (strcmp(argv[1], "nrf") == 0) {
            std::cout << "nrf:" << std::endl;
            nrfMain();
        }
    }
    std::cout << "nrf or lora?" << std::endl;
}
