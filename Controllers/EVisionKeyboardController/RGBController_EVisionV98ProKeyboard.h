/*---------------------------------------------------------*\
| RGBController_EVisionV98ProKeyboard.h                     |
|                                                           |
|   RGBController for EVision V98PRO keyboard               |
|                                                           |
|   This file is part of the OpenRGB project                |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#pragma once

#include "EVisionV98ProKeyboardController.h"
#include "RGBController.h"

class RGBController_EVisionV98ProKeyboard : public RGBController
{
public:
    RGBController_EVisionV98ProKeyboard(EVisionV98ProKeyboardController* controller_ptr);
    ~RGBController_EVisionV98ProKeyboard();

    void        SetupZones();
    void        ResizeZone(int zone, int new_size);
    void        DeviceUpdateLEDs();
    void        UpdateZoneLEDs(int zone);
    void        UpdateSingleLED(int led);
    void        DeviceUpdateMode();

private:
    EVisionV98ProKeyboardController* controller;
};
