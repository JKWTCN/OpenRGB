/*---------------------------------------------------------*\
| CorsairDRAMController.cpp                                 |
|                                                           |
|   Driver for Corsair DRAM RGB controllers                 |
|                                                           |
|   Adam Honse (CalcProgrammer1)                30 Jun 2019 |
|   Erik Gilling (konkers)                      25 Sep 2020 |
|                                                           |
|   This file is part of the OpenRGB project                |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#include <cstring>
#include <chrono>
#include "CRC.h"
#include "CorsairDRAMController.h"

using namespace std::chrono_literals;

CorsairDRAMController::CorsairDRAMController(i2c_smbus_interface *bus, corsair_dev_id dev, unsigned int leds_count, std::string dev_name)
{
    this->bus           = bus;
    this->dev           = dev;
    this->led_count     = 10;
    this->name          = dev_name;

    ReadIdentificationData();
}

CorsairDRAMController::~CorsairDRAMController()
{
}

unsigned int CorsairDRAMController::GetLEDCount()
{
    return(led_count);
}

std::string CorsairDRAMController::GetDeviceLocation()
{
    std::string return_string(bus->device_name);
    char addr[5];
    snprintf(addr, 5, "0x%02X", dev);
    return_string.append(", address ");
    return_string.append(addr);
    return("I2C: " + return_string);
}

std::string CorsairDRAMController::GetDeviceName()
{
    return(name);
}

void CorsairDRAMController::SetColorsDirect(RGBColor* colors)
{
    /*-----------------------------------------------------*\
    | Packet format:                                        |
    |   Size n is (LED count * 3) + 2                       |
    |   0:          Command byte (0x0A or 0x0C)             |
    |   1 to (n-2): LED color data in R/G/B order           |
    |   (n-1):      CRC8 of bytes 0 to (n-2)                |
    \*-----------------------------------------------------*/
    unsigned int   direct_packet_size   = (led_count * 3) + 2;
    unsigned char* direct_packet        = new unsigned char[direct_packet_size];

    direct_packet[0]                    = 0x0A;

    for(unsigned int led_idx = 0; led_idx < led_count; led_idx++)
    {
        unsigned int offset             = (led_idx * 3) + 1;

        direct_packet[offset + 0]       = RGBGetRValue(colors[led_idx]);
        direct_packet[offset + 1]       = RGBGetGValue(colors[led_idx]);
        direct_packet[offset + 2]       = RGBGetBValue(colors[led_idx]);
    }

    direct_packet[direct_packet_size - 1] = CRCPP::CRC::Calculate(direct_packet, (direct_packet_size - 1), CRCPP::CRC::CRC_8());

    printf("Sending packet: ");
    for(unsigned int i = 0; i < direct_packet_size; i++)
    {
        printf( "%02X ", direct_packet[i]);
    }
    printf("\r\n");

    bus->i2c_smbus_write_block_data(dev, CORSAIR_DRAM_REG_DIRECT_COMMAND, 32, direct_packet);
    if(direct_packet_size > 32)
    {
        bus->i2c_smbus_write_block_data(dev, CORSAIR_DRAM_REG_DIRECT_COMMAND_2, direct_packet_size - 32, direct_packet + 32);
    }
}

void CorsairDRAMController::SetEffect
    (
    unsigned char mode,
    unsigned char speed,
    unsigned char direction,
    bool          random,
    unsigned char red1,
    unsigned char grn1,
    unsigned char blu1,
    unsigned char red2,
    unsigned char grn2,
    unsigned char blu2
    )
{
    if(mode == effect_mode)
    {
        return;
    }

    effect_mode = mode;

    direct_mode = (effect_mode == CORSAIR_DRAM_MODE_DIRECT);

    if(direct_mode)
    {
        return;
    }

    bus->i2c_smbus_write_byte_data(dev, 0x26, 0x01);
    std::this_thread::sleep_for(1ms);
    bus->i2c_smbus_write_byte_data(dev, 0x21, 0x00);
    std::this_thread::sleep_for(1ms);

    unsigned char random_byte;

    if(random)
    {
        random_byte = CORSAIR_DRAM_EFFECT_RANDOM_COLORS;
    }
    else
    {
        random_byte = CORSAIR_DRAM_EFFECT_CUSTOM_COLORS;
    }

    bus->i2c_smbus_write_byte_data(dev, CORSAIR_DRAM_REG_COMMAND, effect_mode);  // Mode
    bus->i2c_smbus_write_byte_data(dev, CORSAIR_DRAM_REG_COMMAND, speed);        // Speed
    bus->i2c_smbus_write_byte_data(dev, CORSAIR_DRAM_REG_COMMAND, random_byte);  // Custom color
    bus->i2c_smbus_write_byte_data(dev, CORSAIR_DRAM_REG_COMMAND, direction);    // Direction
    bus->i2c_smbus_write_byte_data(dev, CORSAIR_DRAM_REG_COMMAND, red1);         // Custom color 1 red
    bus->i2c_smbus_write_byte_data(dev, CORSAIR_DRAM_REG_COMMAND, grn1);         // Custom color 1 green
    bus->i2c_smbus_write_byte_data(dev, CORSAIR_DRAM_REG_COMMAND, blu1);         // Custom color 1 blue
    bus->i2c_smbus_write_byte_data(dev, CORSAIR_DRAM_REG_COMMAND, 0xFF);
    bus->i2c_smbus_write_byte_data(dev, CORSAIR_DRAM_REG_COMMAND, red2);         // Custom color 2 red
    bus->i2c_smbus_write_byte_data(dev, CORSAIR_DRAM_REG_COMMAND, grn2);         // Custom color 2 green
    bus->i2c_smbus_write_byte_data(dev, CORSAIR_DRAM_REG_COMMAND, blu2);         // Custom color 2 blue
    bus->i2c_smbus_write_byte_data(dev, CORSAIR_DRAM_REG_COMMAND, 0xFF);
    bus->i2c_smbus_write_byte_data(dev, CORSAIR_DRAM_REG_COMMAND, 0x00);
    bus->i2c_smbus_write_byte_data(dev, CORSAIR_DRAM_REG_COMMAND, 0x00);
    bus->i2c_smbus_write_byte_data(dev, CORSAIR_DRAM_REG_COMMAND, 0x00);
    bus->i2c_smbus_write_byte_data(dev, CORSAIR_DRAM_REG_COMMAND, 0x00);
    bus->i2c_smbus_write_byte_data(dev, CORSAIR_DRAM_REG_COMMAND, 0x00);
    bus->i2c_smbus_write_byte_data(dev, CORSAIR_DRAM_REG_COMMAND, 0x00);
    bus->i2c_smbus_write_byte_data(dev, CORSAIR_DRAM_REG_COMMAND, 0x00);
    bus->i2c_smbus_write_byte_data(dev, CORSAIR_DRAM_REG_COMMAND, 0x00);

    bus->i2c_smbus_write_byte_data(dev, 0x82, 0x01);
    WaitReady();
}

bool CorsairDRAMController::WaitReady()
{
    int i = 0;
    while (bus->i2c_smbus_read_byte_data(dev, 0x41) != 0x00)
    {
        i++;
        std::this_thread::sleep_for(1ms);
    }

    return false;
}

void CorsairDRAMController::ReadIdentificationData()
{
    unsigned char   identification_data[34];

    bus->i2c_smbus_write_byte_data(dev, 0x61, 0x00);
    bus->i2c_smbus_write_byte_data(dev, 0x21, 0x00);

    for(unsigned int i = 0; i < 34; i++)
    {
        identification_data[i] = bus->i2c_smbus_read_byte_data(dev, CORSAIR_DRAM_REG_IDENTIFICATION_DATA);
    }

    printf("%02X: Identification data packet: ", dev);
    for(unsigned int i = 0; i < 34; i++)
    {
        printf( "%02X ", identification_data[i]);
    }
    printf("\r\n");
}
