#pragma once

#include "platform/i2c.h"

class MPU6050 : i2c::Device {
public:

    struct AccelData {
        int16_t x = 0;
        int16_t y = 0;
        int16_t z = 0;

        AccelData() = default;
    private:
        friend class MPU6050;
        AccelData(uint8_t const * buffer): 
            x{static_cast<int16_t>(buffer[0] << 8 | buffer[1])},
            y{static_cast<int16_t>(buffer[2] << 8 | buffer[3])},
            z{static_cast<int16_t>(buffer[4] << 8 | buffer[5])} {
        }
    }; 

    static_assert(sizeof(AccelData) == 6);

    MPU6050(uint8_t address = 0x68):
        i2c::Device{address} {
    }

    void reset() {
        // Two byte reset. First byte register, second byte data
        // There are a load more options to set up the device in different ways that could be added here
        uint8_t buf[] = { CMD_RESET, 0x00 };
        write(buf, 2);
    }

    AccelData readAccel() {
        uint8_t cmd[] = { CMD_READ_ACCEL };
        uint8_t buffer[6];
        i2c::transmit(address, cmd, 1, buffer, sizeof(buffer));
        return AccelData{buffer};
    }

    AccelData readGyro() {
        uint8_t cmd[] = { CMD_READ_GYRO };
        uint8_t buffer[6];
        i2c::transmit(address, cmd, 1, buffer, sizeof(buffer));
        return AccelData{buffer};
    }

    int16_t readTemp() {
        uint8_t cmd[] = { CMD_READ_TEMP };
        uint8_t buffer[2];
        i2c::transmit(address, cmd, 1, buffer, sizeof(buffer));
        return buffer[0] << 8 | buffer[1];
    }

private:
    static constexpr uint8_t CMD_READ_ACCEL = 0x3b;
    static constexpr uint8_t CMD_READ_TEMP = 0x41;
    static constexpr uint8_t CMD_READ_GYRO = 0x43;
    static constexpr uint8_t CMD_RESET = 0x6b;

}; 

