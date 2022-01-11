#pragma once
#include "platform.h"
#include "gpio.h"

#if (defined ARCH_RP2040)
#include <hardware/spi.h>
#include <pico/binary_info.h>
#elif (defined ARCH_AVR_MEGATINY)
#elif (defined ARCH_ARDUINO)
#include <SPI.h>
#else
ARCH_NOT_SUPPORTED;
#endif


namespace spi {

#if (defined ARCH_RP2040)
    inline void initialize(unsigned miso, unsigned mosi, unsigned sck) {
        spi_init(spi0, 500000); // init spi0 at 0.5Mhz
        gpio_set_function(miso, GPIO_FUNC_SPI);
        gpio_set_function(mosi, GPIO_FUNC_SPI);
        gpio_set_function(sck, GPIO_FUNC_SPI);
        // Make the SPI pins available to picotool
        bi_decl(bi_3pins_with_func(miso, mosi, sck, GPIO_FUNC_SPI));        
    }
#else
    inline void initialize() {
#if (defined ARCH_AVR_MEGATINY)
        SPI0.CTRLA = SPI_MASTER_bm | SPI_ENABLE_bm | SPI_CLK2X_bm;
        SPI0.CTRLB = SPI_SSD_bm;
#if (defined ARCH_ATTINY_1616) | (defined ARCH_ATTINY_3216)
        gpio::output(16); // SCK
        gpio::input(15); // MISO
        gpio::output(14); // MOSI
#else
        ARCH_NOT_SUPPORTED;
#endif
#elif (defined ARCH_ARDUINO)
        SPI.begin();
#endif
    }
#endif

    inline void setCs(gpio::Pin cs, bool value) {
#if (defined ARCH_RP2040)
        asm volatile("nop \n nop \n nop");
        value ? gpio::low(cs) : gpio::high(cs);
        asm volatile("nop \n nop \n nop");        
#else
        value ? gpio::low(cs) : gpio::high(cs);
#if (defined ARCH_ATTINY_1616) | (defined ARCH_ATTINY_3216)
#elif (defined ARCH_ARDUINO)
        if (value)
            SPI.beginTransaction(SPISettings(8000000, MSBFIRST, SPI_MODE0));
        else
            SPI.endTransaction();
#endif        
#endif
    }

    inline uint8_t transfer(uint8_t value) {
#if (defined ARCH_RP2040)
        uint8_t result;
        spi_read_blocking(spi0, value, & result, 1);
        return result;
#elif (defined ARCH_ATTINY_1616) | (defined ARCH_ATTINY_3216)
        // megatinycore says this speeds up by 10% - have to check that this is really the truth
        //asm volatile("nop");
        SPI0.DATA = value;
        while (! (SPI0.INTFLAGS & SPI_IF_bm));
        return SPI0.DATA;            
#elif (defined ARCH_ARDUINO)
        return SPI.transfer(value);
#endif
    }

    inline void transfer(uint8_t * data, size_t size) {
        for (uint8_t * end = data + size; data != end; ++data)
            spi::transfer(*data);
    }

} // namespace spi