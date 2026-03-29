/*---------------------------------------------------------*\
| CorsairDRAMControllerDetect.cpp                           |
|                                                           |
|   Detector for Corsair DRAM RGB controllers               |
|                                                           |
|   Adam Honse (CalcProgrammer1)                30 Jun 2019 |
|   Erik Gilling (konkers)                      25 Sep 2020 |
|                                                           |
|   This file is part of the OpenRGB project                |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#include <vector>
#include "Detector.h"
#include "CorsairDRAMController.h"
#include "RGBController_CorsairDRAM.h"
#include "SettingsManager.h"
#include "LogManager.h"
#include "i2c_smbus.h"
#include "pci_ids.h"

using namespace std::chrono_literals;

json corsair_dominator_models =
{
    {
        "CMT",
        {
            {"name",  "Corsair Dominator Platinum"},
            {"leds",  12}
        }
    },
    {
        "CMH",
        {
            {"name",  "Corsair Vengeance Pro SL"},
            {"leds",  10}
        }
    },
    {
        "CMN",
        {
            {"name",  "Corsair Vengeance RGB RT"},
            {"leds",  10}
        }
    },
    {
        "CMG",
        {
            {"name",  "Corsair Vengeance RGB RS"},
            {"leds",  6}
        }
    },
    {
        "CMP",
        {
            {"name",  "Corsair Dominator Titanium"},
            {"leds",  11}
        }
    }
};

#define CORSAIR_DRAM_NAME "Corsair DRAM"

bool TestForCorsairDRAMController(i2c_smbus_interface *bus, unsigned char address)
{
    int res = bus->i2c_smbus_write_quick(address, I2C_SMBUS_WRITE);

    LOG_DEBUG("[%s] Trying address %02X", CORSAIR_DRAM_NAME, address);

    if(res < 0)
    {
        LOG_DEBUG("[%s] Failed: res was %04X", CORSAIR_DRAM_NAME, res);
        return false;
    }

    res = bus->i2c_smbus_read_byte_data(address, 0x43);

    if(!(res == 0x1A || res == 0x1B || res == 0x1C))
    {
        LOG_DEBUG("[%s] Failed: expected 0x1a or 0x1b, got %04X", CORSAIR_DRAM_NAME, res);
        return false;
    }

    res = bus->i2c_smbus_read_byte_data(address, 0x44);

    if(!(res == 0x03 || res == 0x04))
    {
        LOG_DEBUG("[%s] Failed: expected 0x04, got %04X", CORSAIR_DRAM_NAME, res);
        return false;
    }

    return true;
}

void DetectCorsairDRAMControllers(std::vector<i2c_smbus_interface *> &busses)
{
    SettingsManager* settings_manager = ResourceManager::get()->GetSettingsManager();

    json corsair_dominator_settings = settings_manager->GetSettings("CorsairDominatorSettings");

    if(!corsair_dominator_settings.contains("model"))
    {
        // Set default value
        corsair_dominator_settings["model"] = "CMT";
        settings_manager->SetSettings("CorsairDominatorSettings", corsair_dominator_settings);
        settings_manager->SaveSettings();
    }

    std::string model = corsair_dominator_settings["model"];

    for(unsigned int bus = 0; bus < busses.size(); bus++)
    {
        IF_DRAM_SMBUS(busses[bus]->pci_vendor, busses[bus]->pci_device)
        {
            LOG_DEBUG("[%s] Testing bus %d", CORSAIR_DRAM_NAME, bus);

            std::vector<unsigned char> addresses;

            for(unsigned char addr = 0x58; addr <= 0x5F; addr++)
            {
                addresses.push_back(addr);
            }

            for(unsigned char addr = 0x18; addr <= 0x1F; addr++)
            {
                addresses.push_back(addr);
            }

            for(unsigned char addr : addresses)
            {
                if(TestForCorsairDRAMController(busses[bus], addr))
                {
                    unsigned int leds;
                    std::string name;

                    if(corsair_dominator_models.contains(model))
                    {
                        leds = corsair_dominator_models[model]["leds"];
                        name = corsair_dominator_models[model]["name"];
                    }
                    else
                    {
                        leds = corsair_dominator_models["CMT"]["leds"];
                        name = corsair_dominator_models["CMT"]["name"];
                    }

                    LOG_DEBUG("[%s] Model: %s, Leds: %d", CORSAIR_DRAM_NAME, name.c_str(), leds);

                    CorsairDRAMController*     controller    = new CorsairDRAMController(busses[bus], addr, leds, name);
                    RGBController_CorsairDRAM* rgbcontroller = new RGBController_CorsairDRAM(controller);

                    ResourceManager::get()->RegisterRGBController(rgbcontroller);
                }

                std::this_thread::sleep_for(10ms);
            }
        }
        else
        {
            LOG_DEBUG("[%s] Bus %d is not a DRAM bus", CORSAIR_DRAM_NAME, bus);
        }
    }
}

REGISTER_I2C_DETECTOR(CORSAIR_DRAM_NAME, DetectCorsairDRAMControllers);
