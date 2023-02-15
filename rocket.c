/**
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/binary_info.h"
#include "hardware/i2c.h"
#include "math.h"

// By default these devices  are on bus address 0x68
static int addr = 0x68;

#define PI 3.14159265359

/**
 * @brief Corrects the accelerometer values for gravity
 * 
 * @param acc the accelerometer data
 * @param angle the angle of the rocket
 */
void acc_cal_grav(double acc[3], double angle[3]) {
    /**
     * If you really think about it... This function should be called frame_shift();
     *
     * To correct the accelerometer readings for gravity we need to know the angle that the rocket is facing.
     * Once we know this angle we can use 3 rotation matrixes (which use a lot of googling to find) to find the correct x y and z components.
     * I could have figured it out with trig and a lot of thought but I dont have time for that
     * 
     * We need to perform the calculation in 3 steps:
     * 1) Rotate around ROLL
     * 2) Rotate around PITCH
     * 3) Rotate around YAW
     * 
     * Look up wiki rotation matrix
     */

    double gravity[3] = {0, 0, -1};      // Vector of gravity on the world plane
    double rad[3] = {angle[0]*PI/180,     // Euler angle of the rocket in radians
                     angle[1]*PI/180,
                     angle[3]*PI/180};

    // Now the actual conversion

 // gravity[0]  Nothing happens this time
    gravity[1] *= cos(rad[0]) - sin(rad[0]);
    gravity[2] *= sin(rad[0]) + cos(rad[0]);

    gravity[0] *= cos(rad[1]) + sin(rad[1]);
 // gravity[1] Nothing happens this time
    gravity[2] *= -sin(rad[1]) + cos(rad[1]);

    gravity[0] *= cos(rad[2]) - sin(rad[2]);
    gravity[1] *= sin(rad[2]) + cos(rad[2]);
 // gravity[2] *= -sin(rad[1]) + cos(rad[1]);


    // Now the gravitational vector should be rotated to be pointing in the correct direction so we can simply remove this vector from the acceleration
    acc[0] -= gravity[0];
    acc[1] -= gravity[1];
    acc[2] -= gravity[2];

}

static void mpu6050_reset() {
    // Two byte reset. First byte register, second byte data
    // There are a load more options to set up the device in different ways that could be added here
    uint8_t buf[] = {0x6B, 0x00};
    i2c_write_blocking(i2c_default, addr, buf, 2, false);

    // Now to calibrate the sensor and set it to 2000 deg/sec and 16g sensitivity
    // Page 14 of register map has this information
    buf[0] = 0x1C;  // Accel location
    buf[1] = 0b11111000;  // Options
    // Do this 6 times just to really pump it in
    for (int i = 0; i < 6; i ++) {
        i2c_write_blocking(i2c_default, addr, buf, 2, false);
    }

    // test gyro
    buf[0] = 0x1B;  // Gyro location
    buf[1] = 0b11111000;  // options
    for (int i = 0; i < 6; i ++) {
        i2c_write_blocking(i2c_default, addr, buf, 2, false);
    }

    // Set sample rate to 1khz
    buf[0] = 0x19; // SMPRT_DIV
    buf[1] = 0x07; // 8khz / (1 / SMPRT_DIV)
    i2c_write_blocking(i2c_default, addr, buf, 2, false);

    // Set low pass filter to ~180
    buf[0] = 0x1A;          // CONFIG
    buf[1] = 0b00000001;    // Low pass to 188 hz also sets the gyroscope to 1 khz
    i2c_write_blocking(i2c_default, addr, buf, 2, false);

    // Enable the FIFO in general and reset it
    buf[0] = 0x6A;          // USER_CTRL
    buf[1] = 0b01000100;    // Enable and reset the fifio
    i2c_write_blocking(i2c_default, addr, buf, 2, false);

    // Enable the FIFO for the accelerometer and the Gyroscope
    buf[0] = 0x23;          // FIFO_EN
    buf[1] = 0b01111000;    // Turn on GX GY GZ and all of ACCEL
    i2c_write_blocking(i2c_default, addr, buf, 2, false);

    // Enable the FIFO in general and reset it
    buf[0] = 0x6A;          // USER_CTRL
    buf[1] = 0b01000100;    // Enable and reset the fifio
    i2c_write_blocking(i2c_default, addr, buf, 2, false);

}

static void mpu6050_read_fifo(int16_t accel[3], int16_t gyro[3]) {
    uint8_t buffer[6];

    // Start reading acceleration registers from register 0x3B for 6 bytes
    uint8_t val = 0x3B;
    i2c_write_blocking(i2c_default, addr, &val, 1, true); // true to keep master control of bus
    i2c_read_blocking(i2c_default, addr, buffer, 6, false);

    for (int i = 0; i < 3; i++) {
        accel[i] = (buffer[i * 2] << 8 | buffer[(i * 2) + 1]);
    }

    // Now gyro data from reg 0x43 for 6 bytes
    val = 0x43;
    i2c_write_blocking(i2c_default, addr, &val, 1, true);
    i2c_read_blocking(i2c_default, addr, buffer, 6, false);  // False - finished with bus

    for (int i = 0; i < 3; i++) {
        gyro[i] = (buffer[i * 2] << 8 | buffer[(i * 2) + 1]);
    }
}

static void mpu6050_read_raw(int16_t accel[3], int16_t gyro[3]) {
    uint8_t buffer[6];

    // Start reading acceleration registers from register 0x3B for 6 bytes
    uint8_t val = 0x3B;
    i2c_write_blocking(i2c_default, addr, &val, 1, true); // true to keep master control of bus
    i2c_read_blocking(i2c_default, addr, buffer, 6, false);

    for (int i = 0; i < 3; i++) {
        accel[i] = (buffer[i * 2] << 8 | buffer[(i * 2) + 1]);
    }

    // Now gyro data from reg 0x43 for 6 bytes
    val = 0x43;
    i2c_write_blocking(i2c_default, addr, &val, 1, true);
    i2c_read_blocking(i2c_default, addr, buffer, 6, false);  // False - finished with bus

    for (int i = 0; i < 3; i++) {
        gyro[i] = (buffer[i * 2] << 8 | buffer[(i * 2) + 1]);
    }
}

void initialize() {
    // This will get the board ready for flight
    stdio_init_all();

    // Turn on light cuz why not
    gpio_pull_up(PICO_DEFAULT_LED_PIN);

    // This will use I2C0 on the default SDA and SCL pins (4, 5 on a Pico)
    i2c_init(i2c_default, 400 * 1000);
    gpio_set_function(PICO_DEFAULT_I2C_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(PICO_DEFAULT_I2C_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(PICO_DEFAULT_I2C_SDA_PIN);
    gpio_pull_up(PICO_DEFAULT_I2C_SCL_PIN);

    // Set up the mpu
    mpu6050_reset();
}

int main() {
    initialize();

    int16_t acceleration[3], gyro[3];
    double acc[3] = {0, 0, 0};
    double gy[3] = {0, 0, 0};
    double pos[3] = {0, 0, 0};
    double angle[3] = {0, 90, 90};

    int16_t AOFF[3] = {-1051, -7, -1525};
    int16_t GOFF[3] = {-710, 840, -990};
    uint8_t count_8[2] = {0, 0};
    uint16_t count;

    sleep_ms(500);

    while (1) {

        // See fifo count
        uint8_t buf[1] = {0x72};
        i2c_write_blocking(i2c_default, addr, buf, 1, true);
        i2c_read_blocking(i2c_default, addr, count_8, 1, false);
        count = ((uint16_t)count_8[0] << 8) | (uint16_t)count_8[1];
        
        mpu6050_read_fifo(acceleration, gyro);

        acc[0] = (((round(acceleration[0] / 10) * 10) + AOFF[0]) / 2048.0);
        acc[1] = (((round(acceleration[1] / 10) * 10) + AOFF[1]) / 2048.0);
        acc[2] = (((round(acceleration[2] / 10) * 10) + AOFF[2]) / 2048.0);

        gy[0] = ((round(gyro[0] / 10) * 10) + GOFF[0]) / 16.3835;
        gy[1] = ((round(gyro[1] / 10) * 10) + GOFF[1]) / 16.3835;
        gy[2] = ((round(gyro[2] / 10) * 10) + GOFF[2]) / 16.3835;

        // acc_cal_grav(acc, angle);

        angle[0] += gy[0] / 55;
        angle[1] += gy[1] / 55;
        angle[2] += gy[2] / 55;

        pos[0] += (acc[0] * 9.8) / 55;
        pos[1] += (acc[1] * 9.8) / 55;
        pos[2] += (acc[2] * 9.8) / 55;
        
        printf("Pos. X = %8.3f, Y = %8.3f, Z = %8.3f        ", pos[0], pos[1], pos[2]);
        printf("Ang. X = %8.3f, Y = %8.3f, Z = %8.3f        ", angle[0], angle[1], angle[2]);
        printf("Fifo = %d\n", count);
        //printf("Acc. X = %8.3f, Y = %8.3f, Z = %8.3f        ", acc[0], acc[1], acc[2]);
        //printf("Gyr. X = %8.3f, Y = %8.3f, Z = %8.3f\n", gy[0], gy[1], gy[2]);
        sleep_ms(1);

    }

    return 0;
}