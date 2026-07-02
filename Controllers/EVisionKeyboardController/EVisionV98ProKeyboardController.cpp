/*---------------------------------------------------------*\
| EVisionV98ProKeyboardController.cpp                       |
|                                                           |
|   Driver for EVision V98PRO keyboard                      |
|                                                           |
|   This file is part of the OpenRGB project                |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#include <algorithm>
#include <cstring>
#include <chrono>
#include <thread>
#include "EVisionV98ProKeyboardController.h"
#include "LogManager.h"
#include "StringUtils.h"

EVisionV98ProKeyboardController::EVisionV98ProKeyboardController(hid_device* dev_handle, const char* path, std::string dev_name)
{
    dev                 = dev_handle;
    location            = path;
    name                = dev_name;
    direct_initialized  = false;
    direct_mode_enabled = false;
}

EVisionV98ProKeyboardController::~EVisionV98ProKeyboardController()
{
    hid_close(dev);
}

std::string EVisionV98ProKeyboardController::GetDeviceLocation()
{
    return("HID: " + location);
}

std::string EVisionV98ProKeyboardController::GetDeviceName()
{
    return(name);
}

std::string EVisionV98ProKeyboardController::GetSerialString()
{
    wchar_t serial_string[128];
    int ret = hid_get_serial_number_string(dev, serial_string, 128);

    if(ret != 0)
    {
        return("");
    }

    return(StringUtils::wstring_to_string(serial_string));
}

void EVisionV98ProKeyboardController::SetMode
    (
    unsigned char       mode,
    unsigned char       brightness,
    unsigned char       speed,
    unsigned char       direction,
    unsigned char       random,
    RGBColor            color
    )
{
    unsigned char packet[EVISION_V98PRO_PACKET_SIZE];

    memset(packet, 0x00, sizeof(packet));

    packet[0x00] = 0x04;
    packet[0x03] = 0x06;
    packet[0x04] = 0x18;
    packet[0x05] = 0x00;
    packet[0x09] = mode;
    packet[0x0A] = brightness;
    packet[0x0B] = speed;
    packet[0x0C] = direction;
    packet[0x0D] = random;
    packet[0x0E] = RGBGetRValue(color);
    packet[0x0F] = RGBGetGValue(color);
    packet[0x10] = RGBGetBValue(color);
    packet[0x15] = 0x08;
    packet[0x16] = 0x07;
    packet[0x17] = 0xFF;
    packet[0x18] = 0xFF;
    packet[0x1C] = 0x01;

    SendPacket(packet);

    direct_mode_enabled = (mode == EVISION_V98PRO_MODE_DIRECT);
}

void EVisionV98ProKeyboardController::SetDirect
    (
    const std::vector<RGBColor>& colors,
    const unsigned char*          slot_map,
    unsigned int                  led_count,
    unsigned char                 brightness
    )
{
    SetDirectMode(brightness);
    SetDirectColors(colors, slot_map, led_count, brightness);
}

void EVisionV98ProKeyboardController::SetDirectMode(unsigned char brightness)
{
    InitializeDirect();
    SetMode(EVISION_V98PRO_MODE_DIRECT, brightness, 0x00, 0x01, 0x01, ToRGBColor(0xFF, 0x00, 0x00));

    /* Official software follows a mode set with a 0x06/0x18 refresh request. */
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    unsigned char packet[EVISION_V98PRO_PACKET_SIZE];

    memset(packet, 0x00, sizeof(packet));

    packet[0x00] = 0x04;
    packet[0x03] = 0x06;
    packet[0x04] = 0x18;
    packet[0x05] = 0x18;
    packet[0x09] = EVISION_V98PRO_MODE_STATIC;
    packet[0x0A] = EVISION_V98PRO_BRIGHTNESS_HIGHEST;
    packet[0x0B] = 0x03;
    packet[0x0E] = 0xDA;
    packet[0x0F] = 0xFF;
    packet[0x15] = 0x08;
    packet[0x16] = 0x07;
    packet[0x17] = 0xFF;
    packet[0x18] = 0xFF;
    packet[0x1C] = 0x01;

    SendPacket(packet);
}

void EVisionV98ProKeyboardController::SetDirectColors
    (
    const std::vector<RGBColor>& colors,
    const unsigned char*          slot_map,
    unsigned int                  led_count,
    unsigned char                 brightness
    )
{
    unsigned char color_data[EVISION_V98PRO_SLOT_COUNT * 3];
    memset(color_data, 0x00, sizeof(color_data));

    for(unsigned int led_idx = 0; led_idx < led_count; led_idx++)
    {
        unsigned char slot = slot_map[led_idx];

        if(slot < EVISION_V98PRO_SLOT_COUNT)
        {
            color_data[(slot * 3) + 0] = RGBGetRValue(colors[led_idx]);
            color_data[(slot * 3) + 1] = RGBGetGValue(colors[led_idx]);
            color_data[(slot * 3) + 2] = RGBGetBValue(colors[led_idx]);
        }
    }

    InitializeDirect();

    if(!direct_mode_enabled)
    {
        SetDirectMode(brightness);
    }

    SendBegin(false);

    unsigned int offset = 0;
    while(offset < sizeof(color_data))
    {
        unsigned char packet_size = (unsigned char)std::min<unsigned int>(0x38, sizeof(color_data) - offset);
        SendTableData(0x0B, &color_data[offset], packet_size, offset, false);
        offset += packet_size;
    }

    SendEnd(false);
    DrainResponses(9);
}

void EVisionV98ProKeyboardController::InitializeDirect()
{
    unsigned char packet[EVISION_V98PRO_PACKET_SIZE];

    if(direct_initialized)
    {
        return;
    }

    LOG_TRACE("[EVision V98PRO] InitializeDirect");

    memset(packet, 0x00, sizeof(packet));
    packet[0x00] = 0x04;
    packet[0x03] = 0x1A;
    packet[0x04] = 0x06;
    SendPacket(packet);

    SendQuery(0x03, 0x18, 0x00);
    SendQuery(0x03, 0x18, 0x18);
    SendQuery(0x05, 0x18, 0x00);
    SendQuery(0x05, 0x18, 0x18);

    SendTable(0x0A);
    SendTable(0x08);
    SendTable(0x26);

    direct_initialized = true;
}

void EVisionV98ProKeyboardController::DrainResponses(unsigned int expected_responses)
{
    unsigned char packet[EVISION_V98PRO_PACKET_SIZE];

    for(unsigned int response_idx = 0; response_idx < expected_responses; response_idx++)
    {
        int bytes_read = hid_read_timeout(dev, packet, EVISION_V98PRO_PACKET_SIZE, 20);

        if(bytes_read <= 0)
        {
            break;
        }
    }
}

void EVisionV98ProKeyboardController::SendPacket(unsigned char* packet, bool read_response)
{
    unsigned char command = packet[0x03];
    int bytes_written = hid_write(dev, packet, EVISION_V98PRO_PACKET_SIZE);

    if(bytes_written != EVISION_V98PRO_PACKET_SIZE)
    {
        LOG_ERROR("[EVision V98PRO] HID write failed for command 0x%02X at %s, wrote %d bytes", command, location.c_str(), bytes_written);
        return;
    }

    LOG_TRACE("[EVision V98PRO] HID write command 0x%02X at %s", command, location.c_str());

    if(!read_response)
    {
        return;
    }

    int bytes_read = hid_read_timeout(dev, packet, EVISION_V98PRO_PACKET_SIZE, 100);

    if(bytes_read < 0)
    {
        LOG_DEBUG("[EVision V98PRO] HID read failed after command 0x%02X at %s", command, location.c_str());
    }
    else if(bytes_read == 0)
    {
        LOG_DEBUG("[EVision V98PRO] HID read timed out after command 0x%02X at %s", command, location.c_str());
    }
}

void EVisionV98ProKeyboardController::SendQuery(unsigned char command, unsigned char page, unsigned char target)
{
    unsigned char packet[EVISION_V98PRO_PACKET_SIZE];

    memset(packet, 0x00, sizeof(packet));

    packet[0x00] = 0x04;
    packet[0x03] = command;
    packet[0x04] = page;
    packet[0x05] = target;

    SendPacket(packet);
}

void EVisionV98ProKeyboardController::SendBegin(bool read_response)
{
    unsigned char packet[EVISION_V98PRO_PACKET_SIZE];

    memset(packet, 0x00, sizeof(packet));

    packet[0x00] = 0x04;
    packet[0x03] = 0x01;

    SendPacket(packet, read_response);
}

void EVisionV98ProKeyboardController::SendEnd(bool read_response)
{
    unsigned char packet[EVISION_V98PRO_PACKET_SIZE];

    memset(packet, 0x00, sizeof(packet));

    packet[0x00] = 0x04;
    packet[0x03] = 0x02;

    SendPacket(packet, read_response);
}

void EVisionV98ProKeyboardController::SendTable(unsigned char command)
{
    unsigned char data[EVISION_V98PRO_SLOT_COUNT * 3];

    memset(data, 0x00, sizeof(data));

    SendBegin(false);

    unsigned int offset = 0;
    while(offset < sizeof(data))
    {
        unsigned char packet_size = (unsigned char)std::min<unsigned int>(0x38, sizeof(data) - offset);
        SendTableData(command, &data[offset], packet_size, offset, false);
        offset += packet_size;
    }

    SendEnd(false);
    DrainResponses(9);
}

void EVisionV98ProKeyboardController::SendTableData(unsigned char command, unsigned char* data, unsigned char data_size, unsigned int data_offset, bool read_response)
{
    unsigned char packet[EVISION_V98PRO_PACKET_SIZE];

    memset(packet, 0x00, sizeof(packet));

    packet[0x00] = 0x04;
    packet[0x03] = command;
    packet[0x04] = data_size;
    packet[0x05] = data_offset & 0xFF;
    packet[0x06] = (data_offset >> 8) & 0xFF;
    packet[0x07] = (data_offset >> 16) & 0xFF;

    memcpy(&packet[0x08], data, data_size);

    SendPacket(packet, read_response);
}
