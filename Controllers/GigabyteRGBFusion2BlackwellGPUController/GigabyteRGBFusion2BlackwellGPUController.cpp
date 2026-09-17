/*---------------------------------------------------------*\
| GigabyteRGBFusion2BlackwellGPUController.cpp              |
|                                                           |
|   Driver for Gigabyte RGB Fusion 2 Blackwell GPU          |
|                                                           |
|   This file is part of the OpenRGB project                |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#include <chrono>
#include <thread>
#include "GigabyteRGBFusion2BlackwellGPUController.h"
#include "GigabyteRGBFusion2BlackwellGPUDefinitions.h"
#include "LogManager.h"

using namespace std::chrono_literals;

RGBFusion2BlackwellGPUController::RGBFusion2BlackwellGPUController(i2c_smbus_interface* bus, rgb_fusion_dev_id dev, std::string dev_name, int gpu_layout)
{
    this->bus           = bus;
    this->dev           = dev;
    this->name          = dev_name;
    this->gpu_layout    = gpu_layout;
}

RGBFusion2BlackwellGPUController::~RGBFusion2BlackwellGPUController()
{

}

std::string RGBFusion2BlackwellGPUController::GetDeviceLocation()
{
    std::string return_string(bus->info.device_name);
    char addr[5];
    snprintf(addr, 5, "0x%02X", dev);
    return_string.append(", address ");
    return_string.append(addr);
    return("I2C: " + return_string);
}

std::string RGBFusion2BlackwellGPUController::GetDeviceName()
{
    return(name);
}

void RGBFusion2BlackwellGPUController::SaveConfig()
{
    uint8_t data_pkt[64] = { 0x13, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    bus->i2c_write_block(dev, sizeof(data_pkt), data_pkt);
}

void RGBFusion2BlackwellGPUController::SetMode(uint8_t type, uint8_t zone, uint8_t mode, fusion2_config zone_config)
{
    if(zone >= RGB_FUSION_2_BLACKWELL_GPU_NUMBER_OF_ZONES || zone_config.numberOfColors > 8)
    {
        LOG_WARNING("[%s] Invalid zone/color count: zone %u, colors %u", name.c_str(),
                    static_cast<unsigned int>(zone), static_cast<unsigned int>(zone_config.numberOfColors));
        return;
    }

    if(zone_config.numberOfColors == 0 || mode == RGB_FUSION2_BLACKWELL_GPU_MODE_DIRECT
       || mode == RGB_FUSION2_BLACKWELL_GPU_MODE_STATIC)
        this->zone_color[zone] = zone_config.colors[0];

    /************************************************************************************\
    *                                                                                    *
    *       Packet (total size = 64 bytes)                                               *
    * TYPE      MODE SPD  BRT  R    G    B    0    ZONE SZ0-8                            *
    * 0x12 0x01 0x08 0x06 0x0A 0xFF 0xFF 0x00 0x00 0x00 0x08 [R] [G] [B] [R] [G] [B] ... *
    *                                                                                    *
    * SZ is the amount of colors that will be sent in the format of 3 bytes RGB          *
    *                                                                                    *
    \************************************************************************************/
    uint8_t zone_pkt[64] = {type, 0x01, mode, zone_config.speed, zone_config.brightness, (uint8_t)RGBGetRValue(this->zone_color[zone]), (uint8_t)RGBGetGValue(this->zone_color[zone]), (uint8_t)RGBGetBValue(this->zone_color[zone]), 0x00, zone, zone_config.numberOfColors, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

    if(zone_config.numberOfColors > 0)
    {
        int currentPos = 12;
        switch(gpu_layout)
        {
            case RGB_FUSION2_BLACKWELL_GPU_AORUS_MASTER_5080_LAYOUT:
            case RGB_FUSION2_BLACKWELL_GPU_AORUS_MASTER_5090D_V2_ICE_LAYOUT:
                currentPos = 11;
                break;
            default:
                break;
        }

        for(uint8_t i = 0; i < zone_config.numberOfColors; i++)
        {
            zone_pkt[currentPos + 0] = RGBGetRValue(zone_config.colors[i]);
            zone_pkt[currentPos + 1] = RGBGetGValue(zone_config.colors[i]);
            zone_pkt[currentPos + 2] = RGBGetBValue(zone_config.colors[i]);
            currentPos += 3;
        }
    }

    const bool aorus_5080_zone = gpu_layout == RGB_FUSION2_BLACKWELL_GPU_AORUS_MASTER_5080_LAYOUT
                                 && zone < RGB_FUSION_2_BLACKWELL_GPU_NUMBER_OF_ZONES;

    int result = bus->i2c_write_block(dev, sizeof(zone_pkt), zone_pkt);
    const bool retried = result < 0 && aorus_5080_zone;

    if(result < 0 && aorus_5080_zone)
    {
        std::this_thread::sleep_for(9ms);
        result = bus->i2c_write_block(dev, sizeof(zone_pkt), zone_pkt);

        if(result >= 0)
        {
            LOG_INFO("[%s] I2C write retry succeeded for zone %u",
                     name.c_str(), static_cast<unsigned int>(zone));
        }
    }

    // Trace actual outgoing logo colors, not only the initial mode. Keep
    // output bounded when the frontend streams a different color every frame.
    if(aorus_5080_zone && zone >= 3)
    {
        StreamTrace& trace = logo_trace[zone - 3];
        const auto now = std::chrono::steady_clock::now();
        const RGBColor color = ToRGBColor(zone_pkt[5], zone_pkt[6], zone_pkt[7]);
        if(!trace.valid)
        {
            trace.start = now;
            trace.last = now;
        }
        const long long gap = std::chrono::duration_cast<std::chrono::milliseconds>(now - trace.last).count();
        if(gap > trace.max_gap_ms)
            trace.max_gap_ms = gap;
        ++trace.writes;
        trace.retries += retried ? 1 : 0;
        trace.failures += result < 0 ? 1 : 0;
        if(!trace.valid || color != trace.color)
        {
            ++trace.changes;
            if(trace.changes <= 12)
            {
                LOG_INFO("[%s] Logo TX zone %u: RGB %02X%02X%02X, gap %lld ms, result %d",
                         name.c_str(), static_cast<unsigned int>(zone),
                         static_cast<unsigned int>(zone_pkt[5]), static_cast<unsigned int>(zone_pkt[6]),
                         static_cast<unsigned int>(zone_pkt[7]), gap, result);
            }
        }
        trace.color = color;
        trace.last = now;
        trace.valid = true;
        const long long elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - trace.start).count();
        if(elapsed >= 5000)
        {
            LOG_INFO("[%s] Logo stream zone %u: %lld ms, writes %u, changes %u, retries %u, failures %u, max gap %lld ms",
                     name.c_str(), static_cast<unsigned int>(zone), elapsed, trace.writes,
                     trace.changes, trace.retries, trace.failures, trace.max_gap_ms);
            trace.start = now;
            trace.writes = trace.changes = trace.retries = trace.failures = 0;
            trace.max_gap_ms = 0;
        }
    }

    if(zone < RGB_FUSION_2_BLACKWELL_GPU_NUMBER_OF_ZONES)
    {
        if(aorus_5080_zone && (!zone_first_write_logged[zone]
           || zone_last_type[zone] != type || zone_last_mode[zone] != mode))
        {
            LOG_INFO("[%s] I2C state instance %p zone %u: type 0x%02X, mode 0x%02X, speed 0x%02X, colors %u, RGB %02X%02X%02X, brightness %u, result %d",
                     name.c_str(), static_cast<void*>(this), static_cast<unsigned int>(zone), static_cast<unsigned int>(type),
                     static_cast<unsigned int>(mode), static_cast<unsigned int>(zone_config.speed),
                     static_cast<unsigned int>(zone_config.numberOfColors), static_cast<unsigned int>(zone_pkt[5]),
                     static_cast<unsigned int>(zone_pkt[6]), static_cast<unsigned int>(zone_pkt[7]),
                     static_cast<unsigned int>(zone_config.brightness), result);
            zone_first_write_logged[zone] = true;
            zone_last_type[zone] = type;
            zone_last_mode[zone] = mode;
        }

        if(result < 0 && !zone_write_failed[zone])
        {
            LOG_WARNING("[%s] I2C write failed for zone %u (result %d)", name.c_str(), static_cast<unsigned int>(zone), result);
            zone_write_failed[zone] = true;
        }
        else if(result >= 0 && zone_write_failed[zone])
        {
            LOG_INFO("[%s] I2C writes resumed for zone %u", name.c_str(), static_cast<unsigned int>(zone));
            zone_write_failed[zone] = false;
        }
    }

}

void RGBFusion2BlackwellGPUController::SetZone(uint8_t zone, uint8_t mode, fusion2_config zone_config)
{
    if(mode == RGB_FUSION2_BLACKWELL_GPU_MODE_BREATHING)
        zone_config.brightness = RGB_FUSION2_BLACKWELL_GPU_BRIGHTNESS_MAX;

    switch(gpu_layout)
    {
        case RGB_FUSION2_BLACKWELL_GPU_AORUS_MASTER_5090D_V2_ICE_LAYOUT:
            if(mode == RGB_FUSION2_BLACKWELL_GPU_MODE_DIRECT)
                mode = RGB_FUSION2_BLACKWELL_GPU_MODE_STATIC;
            break;
        default:
            break;
    }

    uint8_t type = RGB_FUSION2_BLACKWELL_GPU_REG_COLOR;
    if(mode != RGB_FUSION2_BLACKWELL_GPU_MODE_DIRECT)
        type = RGB_FUSION2_BLACKWELL_GPU_REG_MODE;

    SetMode(type, zone, mode, zone_config);
}
