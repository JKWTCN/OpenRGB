/*---------------------------------------------------------*\
| RGBController_EVisionV98ProKeyboard.cpp                   |
|                                                           |
|   RGBController for EVision V98PRO keyboard               |
|                                                           |
|   This file is part of the OpenRGB project                |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#include "RGBControllerKeyNames.h"
#include "RGBController_EVisionV98ProKeyboard.h"
#include "LogManager.h"

#define NA  0xFFFFFFFF

static unsigned int matrix_map[6][22] =
    { {   0,  NA,   1,   2,   3,   4,  NA,   5,   6,   7,   8,  NA,   9,  10,  11,  12,  NA,  13,  14,  NA,  NA,  NA },
      {  15,  16,  17,  18,  19,  20,  21,  22,  23,  24,  25,  26,  27,  28,  NA,  NA,  NA,  29,  30,  31,  32,  33 },
      {  34,  NA,  35,  36,  37,  38,  39,  40,  41,  42,  43,  44,  45,  46,  47,  NA,  NA,  48,  49,  50,  51,  52 },
      {  53,  NA,  NA,  54,  55,  56,  57,  58,  59,  60,  61,  62,  63,  NA,  64,  NA,  NA,  65,  66,  67,  68,  NA },
      {  69,  NA,  NA,  70,  71,  72,  73,  74,  75,  76,  77,  78,  79,  80,  NA,  NA,  81,  NA,  82,  83,  84,  85 },
      {  86,  87,  88,  NA,  NA,  NA,  NA,  89,  NA,  NA,  NA,  NA,  90,  91,  NA,  92,  NA,  93,  94,  95,  96,  NA } };

static const char* led_names[] =
{
    KEY_EN_ESCAPE,
    KEY_EN_F1,
    KEY_EN_F2,
    KEY_EN_F3,
    KEY_EN_F4,
    KEY_EN_F5,
    KEY_EN_F6,
    KEY_EN_F7,
    KEY_EN_F8,
    KEY_EN_F9,
    KEY_EN_F10,
    KEY_EN_F11,
    KEY_EN_F12,
    KEY_EN_DELETE,
    KEY_EN_INSERT,
    KEY_EN_BACK_TICK,
    KEY_EN_1,
    KEY_EN_2,
    KEY_EN_3,
    KEY_EN_4,
    KEY_EN_5,
    KEY_EN_6,
    KEY_EN_7,
    KEY_EN_8,
    KEY_EN_9,
    KEY_EN_0,
    KEY_EN_MINUS,
    KEY_EN_PLUS,
    KEY_EN_BACKSPACE,
    KEY_EN_PAGE_UP,
    KEY_EN_NUMPAD_LOCK,
    KEY_EN_NUMPAD_DIVIDE,
    KEY_EN_NUMPAD_TIMES,
    KEY_EN_NUMPAD_MINUS,
    KEY_EN_TAB,
    KEY_EN_Q,
    KEY_EN_W,
    KEY_EN_E,
    KEY_EN_R,
    KEY_EN_T,
    KEY_EN_Y,
    KEY_EN_U,
    KEY_EN_I,
    KEY_EN_O,
    KEY_EN_P,
    KEY_EN_LEFT_BRACKET,
    KEY_EN_RIGHT_BRACKET,
    KEY_EN_BACK_SLASH,
    KEY_EN_PAGE_DOWN,
    KEY_EN_NUMPAD_7,
    KEY_EN_NUMPAD_8,
    KEY_EN_NUMPAD_9,
    KEY_EN_NUMPAD_PLUS,
    KEY_EN_CAPS_LOCK,
    KEY_EN_A,
    KEY_EN_S,
    KEY_EN_D,
    KEY_EN_F,
    KEY_EN_G,
    KEY_EN_H,
    KEY_EN_J,
    KEY_EN_K,
    KEY_EN_L,
    KEY_EN_SEMICOLON,
    KEY_EN_QUOTE,
    KEY_EN_ANSI_ENTER,
    KEY_EN_NUMPAD_4,
    KEY_EN_NUMPAD_5,
    KEY_EN_NUMPAD_6,
    KEY_EN_LEFT_SHIFT,
    KEY_EN_Z,
    KEY_EN_X,
    KEY_EN_C,
    KEY_EN_V,
    KEY_EN_B,
    KEY_EN_N,
    KEY_EN_M,
    KEY_EN_COMMA,
    KEY_EN_PERIOD,
    KEY_EN_FORWARD_SLASH,
    KEY_EN_RIGHT_SHIFT,
    KEY_EN_UP_ARROW,
    KEY_EN_NUMPAD_1,
    KEY_EN_NUMPAD_2,
    KEY_EN_NUMPAD_3,
    KEY_EN_NUMPAD_ENTER,
    KEY_EN_LEFT_CONTROL,
    KEY_EN_LEFT_WINDOWS,
    KEY_EN_LEFT_ALT,
    KEY_EN_SPACE,
    KEY_EN_RIGHT_FUNCTION,
    KEY_EN_RIGHT_CONTROL,
    KEY_EN_LEFT_ARROW,
    KEY_EN_DOWN_ARROW,
    KEY_EN_RIGHT_ARROW,
    KEY_EN_NUMPAD_0,
    KEY_EN_NUMPAD_PERIOD
};

static const unsigned char led_slot_map[] =
{
      0,   1,   2,   3,   4,   5,   6,   7,   8,   9,  10,  11,  12,  13,  14,
     15,  16,  17,  18,  19,  20,  21,  22,  23,  24,  25,  26,  27,  28,  29,
     90,  91,  92,  93,  30,  31,  32,  33,  34,  35,  36,  37,  38,  39,  40,
     41,  42,  43,  44,  94,  95,  96, 100,  45,  46,  47,  48,  49,  50,  51,
     52,  53,  54,  55,  56,  58,  97,  98,  99,  60,  61,  62,  63,  64,  65,
     66,  67,  68,  69,  70,  71,  72,  73, 101, 102,  74,  75,  76,  77,  80,
     84,  85,  87,  88,  89, 103, 104
};

static const unsigned int led_count = sizeof(led_slot_map) / sizeof(led_slot_map[0]);

RGBController_EVisionV98ProKeyboard::RGBController_EVisionV98ProKeyboard(EVisionV98ProKeyboardController* controller_ptr)
{
    controller              = controller_ptr;

    name                    = controller->GetDeviceName();
    vendor                  = "EVision";
    type                    = DEVICE_TYPE_KEYBOARD;
    description             = "EVision V98PRO Keyboard Device";
    location                = controller->GetDeviceLocation();
    serial                  = controller->GetSerialString();

    mode Direct;
    Direct.name             = "Direct";
    Direct.value            = EVISION_V98PRO_MODE_DIRECT;
    Direct.flags            = MODE_FLAG_HAS_PER_LED_COLOR | MODE_FLAG_HAS_BRIGHTNESS;
    Direct.brightness_min   = EVISION_V98PRO_BRIGHTNESS_OFF;
    Direct.brightness_max   = EVISION_V98PRO_BRIGHTNESS_HIGHEST;
    Direct.brightness       = EVISION_V98PRO_BRIGHTNESS_HIGHEST;
    Direct.color_mode       = MODE_COLORS_PER_LED;
    modes.push_back(Direct);

    mode Static;
    Static.name             = "Static";
    Static.value            = EVISION_V98PRO_MODE_STATIC;
    Static.flags            = MODE_FLAG_HAS_MODE_SPECIFIC_COLOR | MODE_FLAG_HAS_RANDOM_COLOR | MODE_FLAG_HAS_BRIGHTNESS | MODE_FLAG_AUTOMATIC_SAVE;
    Static.brightness_min   = EVISION_V98PRO_BRIGHTNESS_OFF;
    Static.brightness_max   = EVISION_V98PRO_BRIGHTNESS_HIGHEST;
    Static.brightness       = EVISION_V98PRO_BRIGHTNESS_HIGHEST;
    Static.colors_min       = 1;
    Static.colors_max       = 1;
    Static.color_mode       = MODE_COLORS_MODE_SPECIFIC;
    Static.colors.resize(1);
    modes.push_back(Static);

    mode Off;
    Off.name                = "Off";
    Off.value               = EVISION_V98PRO_MODE_OFF;
    Off.flags               = MODE_FLAG_AUTOMATIC_SAVE;
    Off.color_mode          = MODE_COLORS_NONE;
    modes.push_back(Off);

    SetupZones();
}

RGBController_EVisionV98ProKeyboard::~RGBController_EVisionV98ProKeyboard()
{
    for(unsigned int zone_idx = 0; zone_idx < zones.size(); zone_idx++)
    {
        if(zones[zone_idx].matrix_map != NULL)
        {
            delete zones[zone_idx].matrix_map;
        }
    }

    delete controller;
}

void RGBController_EVisionV98ProKeyboard::SetupZones()
{
    zone new_zone;

    new_zone.name               = ZONE_EN_KEYBOARD;
    new_zone.type               = ZONE_TYPE_MATRIX;
    new_zone.leds_min           = led_count;
    new_zone.leds_max           = led_count;
    new_zone.leds_count         = led_count;
    new_zone.matrix_map         = new matrix_map_type;
    new_zone.matrix_map->height = 6;
    new_zone.matrix_map->width  = 22;
    new_zone.matrix_map->map    = (unsigned int *)&matrix_map;

    zones.push_back(new_zone);

    for(unsigned int led_idx = 0; led_idx < led_count; led_idx++)
    {
        led new_led;

        new_led.name  = led_names[led_idx];
        new_led.value = led_slot_map[led_idx];
        leds.push_back(new_led);
    }

    SetupColors();
}

void RGBController_EVisionV98ProKeyboard::ResizeZone(int /*zone*/, int /*new_size*/)
{
}

void RGBController_EVisionV98ProKeyboard::DeviceUpdateLEDs()
{
    LOG_TRACE("[EVision V98PRO] DeviceUpdateLEDs");

    if(modes[active_mode].value == EVISION_V98PRO_MODE_DIRECT)
    {
        controller->SetDirectColors(colors, led_slot_map, led_count, modes[active_mode].brightness);
    }
}

void RGBController_EVisionV98ProKeyboard::UpdateZoneLEDs(int /*zone*/)
{
    DeviceUpdateLEDs();
}

void RGBController_EVisionV98ProKeyboard::UpdateSingleLED(int /*led*/)
{
    DeviceUpdateLEDs();
}

void RGBController_EVisionV98ProKeyboard::DeviceUpdateMode()
{
    LOG_TRACE("[EVision V98PRO] DeviceUpdateMode %s", modes[active_mode].name.c_str());

    RGBColor color = ToRGBColor(0xFF, 0x00, 0x00);

    if(modes[active_mode].colors.size() > 0)
    {
        color = modes[active_mode].colors[0];
    }

    if(modes[active_mode].value == EVISION_V98PRO_MODE_DIRECT)
    {
        controller->SetDirectMode(modes[active_mode].brightness);
        DeviceUpdateLEDs();
        return;
    }

    controller->SetMode
            (
            modes[active_mode].value,
            modes[active_mode].brightness,
            0x03,
            0x00,
            (modes[active_mode].color_mode == MODE_COLORS_RANDOM),
            color
            );
}
