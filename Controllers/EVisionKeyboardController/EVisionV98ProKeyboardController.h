/*---------------------------------------------------------*\
| EVisionV98ProKeyboardController.h                         |
|                                                           |
|   Driver for EVision V98PRO keyboard                      |
|                                                           |
|   This file is part of the OpenRGB project                |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#pragma once

#include <string>
#include <vector>
#include <hidapi.h>
#include "RGBController.h"

#define EVISION_V98PRO_PACKET_SIZE                 64
#define EVISION_V98PRO_SLOT_COUNT                 128

enum
{
    EVISION_V98PRO_MODE_STATIC                    = 0x06,
    EVISION_V98PRO_MODE_DIRECT                    = 0x13,
    EVISION_V98PRO_MODE_OFF                       = 0x14,
};

enum
{
    EVISION_V98PRO_BRIGHTNESS_OFF                 = 0x00,
    EVISION_V98PRO_BRIGHTNESS_HIGHEST             = 0x04,
};

class EVisionV98ProKeyboardController
{
public:
    EVisionV98ProKeyboardController(hid_device* dev_handle, const char* path, std::string dev_name);
    ~EVisionV98ProKeyboardController();

    std::string GetDeviceLocation();
    std::string GetDeviceName();
    std::string GetSerialString();

    void        SetMode
                    (
                    unsigned char       mode,
                    unsigned char       brightness,
                    unsigned char       speed,
                    unsigned char       direction,
                    unsigned char       random,
                    RGBColor            color
                    );

    void        SetDirect
                    (
                    const std::vector<RGBColor>& colors,
                    const unsigned char*          slot_map,
                    unsigned int                  led_count,
                    unsigned char                 brightness
                    );

    void        SetDirectMode(unsigned char brightness);

    void        SetDirectColors
                    (
                    const std::vector<RGBColor>& colors,
                    const unsigned char*          slot_map,
                    unsigned int                  led_count,
                    unsigned char                 brightness
                    );

private:
    hid_device*             dev;
    std::string             location;
    std::string             name;
    bool                    direct_initialized;
    bool                    direct_mode_enabled;

    void        InitializeDirect();
    void        DrainResponses(unsigned int expected_responses);
    void        SendPacket(unsigned char* packet, bool read_response = true);
    void        SendQuery(unsigned char command, unsigned char page, unsigned char target);
    void        SendBegin(bool read_response = true);
    void        SendEnd(bool read_response = true);
    void        SendTable(unsigned char command);
    void        SendTableData(unsigned char command, unsigned char* data, unsigned char data_size, unsigned int data_offset, bool read_response = true);
};
