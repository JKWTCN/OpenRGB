/*---------------------------------------------------------*\
| ENESMBusInterface_i2c_smbus.cpp                           |
|                                                           |
|   ENE SMBus interface for I2C/SMBus                       |
|                                                           |
|   Adam Honse (CalcProgrammer1)                21 Nov 2021 |
|                                                           |
|   This file is part of the OpenRGB project                |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#include "ENESMBusInterface_i2c_smbus.h"
#include "LogManager.h"
#include <chrono>
#include <mutex>
#include <unordered_map>

/*---------------------------------------------------------*\
| ENE SMBus bus recovery                                    |
|                                                           |
| Transfer failures on ENE DRAM are usually caused by a     |
| collision with another SMBus master (BIOS/AGESA SPD       |
| telemetry, hardware monitors).  The SMBus controller then |
| stays in an error state and every further transfer fails, |
| freezing the lighting.                                    |
|                                                           |
| Two recovery layers work together:                        |
|  - The bus layer (i2c_smbus_pawnio) reloads the SMBus     |
|    module after repeated failures, which re-initializes   |
|    the controller - the same recovery path as restarting  |
|    OpenRGB, which is known to recover the bus.            |
|  - This layer stops all ENE traffic for a short backoff   |
|    window instead of hammering the dead bus, then probes  |
|    the device with a register read until it answers.      |
\*---------------------------------------------------------*/
typedef struct
{
    bool                                    backing_off;
    int                                     backoff_ms;
    std::chrono::steady_clock::time_point   next_probe;
} ENEBusRecoveryState;

static std::unordered_map<i2c_smbus_interface*, ENEBusRecoveryState> ene_bus_recovery_states;
static std::mutex                                                    ene_bus_recovery_mutex;

/*---------------------------------------------------------*\
| Probe the device with a register read (device-name byte). |
| Reads are safer than writes while the device state is     |
| unknown.  Returns true when the bus answers.              |
\*---------------------------------------------------------*/
static bool ENEProbeBus(i2c_smbus_interface* bus, ene_dev_id dev)
{
    int result = bus->i2c_smbus_write_word_data(dev, 0x00, 0x0010);    /* ENE_REG_DEVICE_NAME */

    if(result >= 0)
    {
        result = bus->i2c_smbus_read_byte_data(dev, 0x81);
    }

    return(result >= 0);
}

/*---------------------------------------------------------*\
| Returns false while the bus is backed off after failures. |
| Once the backoff window expires, probes the device and    |
| resumes automatically when it answers.                    |
\*---------------------------------------------------------*/
static bool ENEBusReady(i2c_smbus_interface* bus, ene_dev_id dev)
{
    std::lock_guard<std::mutex> lock(ene_bus_recovery_mutex);

    ENEBusRecoveryState& state = ene_bus_recovery_states[bus];

    if(!state.backing_off)
    {
        return(true);
    }

    if(std::chrono::steady_clock::now() < state.next_probe)
    {
        return(false);
    }

    if(ENEProbeBus(bus, dev))
    {
        state.backing_off   = false;
        state.backoff_ms    = 0;

        LOG_INFO("[ENE SMBus] Bus answered probe at device 0x%02X, resuming transfers", dev);

        return(true);
    }

    /*-------------------------------------------------*\
    | The device was working before the failure, so a  |
    | silent probe means the SMBus controller is stuck |
    | in an error state.  Ask the bus to re-initialize |
    | itself (PawnIO module reload - the same recovery |
    | as restarting OpenRGB) and probe once more.      |
    \*-------------------------------------------------*/
    bus->RecoverBus();

    if(ENEProbeBus(bus, dev))
    {
        state.backing_off   = false;
        state.backoff_ms    = 0;

        LOG_INFO("[ENE SMBus] Bus answered probe at device 0x%02X after recovery, resuming transfers", dev);

        return(true);
    }

    if(state.backoff_ms < 60000)
    {
        state.backoff_ms = (state.backoff_ms == 0) ? 2000 : state.backoff_ms * 2;
    }

    state.next_probe = std::chrono::steady_clock::now() + std::chrono::milliseconds(state.backoff_ms);

    LOG_WARNING("[ENE SMBus] Probe failed at device 0x%02X, backing off %d ms", dev, state.backoff_ms);

    return(false);
}

/*---------------------------------------------------------*\
| Called after a failed transfer; enters the backoff state  |
| so the next frames do not hammer the dead bus.            |
\*---------------------------------------------------------*/
static void ENEMarkBusFailure(i2c_smbus_interface* bus, ene_dev_id dev)
{
    std::lock_guard<std::mutex> lock(ene_bus_recovery_mutex);

    ENEBusRecoveryState& state = ene_bus_recovery_states[bus];

    state.backing_off   = true;
    state.backoff_ms    = (state.backoff_ms < 2000) ? 2000 : state.backoff_ms;
    state.next_probe    = std::chrono::steady_clock::now() + std::chrono::milliseconds(state.backoff_ms);

    LOG_WARNING("[ENE SMBus] Transfer failed at device 0x%02X, backing off %d ms", dev, state.backoff_ms);
}

ENESMBusInterface_i2c_smbus::ENESMBusInterface_i2c_smbus(i2c_smbus_interface* bus)
{
    this->bus = bus;
}

ENESMBusInterface_i2c_smbus::~ENESMBusInterface_i2c_smbus()
{

}

ene_interface_type ENESMBusInterface_i2c_smbus::GetInterfaceType()
{
    return(ENE_INTERFACE_TYPE_I2C_SMBUS);
}

std::string ENESMBusInterface_i2c_smbus::GetLocation()
{
    std::string return_string(bus->info.device_name);
    return("I2C: " + return_string);
}

int ENESMBusInterface_i2c_smbus::GetMaxBlock()
{
    return(30);
}

unsigned char ENESMBusInterface_i2c_smbus::ENERegisterRead(ene_dev_id dev, ene_register reg)
{
    if(!ENEBusReady(bus, dev))
    {
        return(0);
    }

    //Write ENE register
    int result = bus->i2c_smbus_write_word_data(dev, 0x00, ((reg << 8) & 0xFF00) | ((reg >> 8) & 0x00FF));

    //Read ENE value
    if(result >= 0)
    {
        result = bus->i2c_smbus_read_byte_data(dev, 0x81);
    }

    if(result < 0)
    {
        ENEMarkBusFailure(bus, dev);
        return(0);
    }

    return((unsigned char)result);
}

void ENESMBusInterface_i2c_smbus::ENERegisterWrite(ene_dev_id dev, ene_register reg, unsigned char val)
{
    if(!ENEBusReady(bus, dev))
    {
        return;
    }

    //Write ENE register
    int result = bus->i2c_smbus_write_word_data(dev, 0x00, ((reg << 8) & 0xFF00) | ((reg >> 8) & 0x00FF));

    //Write ENE value
    if(result >= 0)
    {
        result = bus->i2c_smbus_write_byte_data(dev, 0x01, val);
    }

    if(result < 0)
    {
        ENEMarkBusFailure(bus, dev);
    }
}

void ENESMBusInterface_i2c_smbus::ENERegisterWriteBlock(ene_dev_id dev, ene_register reg, unsigned char * data, unsigned char sz)
{
    if(!ENEBusReady(bus, dev))
    {
        return;
    }

    //Write ENE register
    int result = bus->i2c_smbus_write_word_data(dev, 0x00, ((reg << 8) & 0xFF00) | ((reg >> 8) & 0x00FF));

    if(result >= 0)
    {
        //Write ENE block data
        result = bus->i2c_smbus_write_block_data(dev, 0x03, sz, data);
    }

    /*-------------------------------------------------*\
    | Do not fall back to byte writes.  Some ENE RAM    |
    | controllers lock up when a failed block transfer  |
    | is followed by writes through command 0x01.       |
    | Enter backoff instead - the bus layer will reload |
    | the SMBus module and the next probe resumes.      |
    \*-------------------------------------------------*/
    if(result < 0)
    {
        ENEMarkBusFailure(bus, dev);
    }
}
