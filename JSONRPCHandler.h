/*---------------------------------------------------------*\
| JSONRPCHandler.h                                          |
|                                                           |
|   JSON-RPC Request Handler                                |
|                                                           |
|   This file is part of the OpenRGB project                |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <mutex>
#include <future>
#include <nlohmann/json.hpp>
#include "RGBController.h"
#include "ResourceManager.h"
#include "ProfileManager.h"
#include "JSONRPCProtocol.h"

class JSONRPCHandler
{
public:
    JSONRPCHandler(std::vector<RGBController*>& controllers,
                   ResourceManager* resource_manager,
                   ProfileManagerInterface* profile_manager);

    ~JSONRPCHandler();

    // Main request handler
    nlohmann::json  HandleRequest(const nlohmann::json& request);

    // Batch request support
    nlohmann::json  HandleBatchRequest(const nlohmann::json& requests);

    // Set profile manager
    void            SetProfileManager(ProfileManagerInterface* profile_manager);

    // Helper functions for external use
    nlohmann::json  ControllerToJSON(RGBController* controller);

private:
    // Method dispatchers
    nlohmann::json  CallMethod(const std::string& method,
                              const nlohmann::json& params);

    // Device management methods
    nlohmann::json  GetControllers(const nlohmann::json& params);
    nlohmann::json  GetControllerCount(const nlohmann::json& params);
    nlohmann::json  GetControllerData(const nlohmann::json& params);
    nlohmann::json  GetControllerInfo(const nlohmann::json& params);
    nlohmann::json  RescanDevices(const nlohmann::json& params);

    // Color control methods
    nlohmann::json  SetLEDColor(const nlohmann::json& params);
    nlohmann::json  SetZoneColor(const nlohmann::json& params);
    nlohmann::json  SetAllColors(const nlohmann::json& params);
    nlohmann::json  SetMultipleColors(const nlohmann::json& params);

    // Mode control methods
    nlohmann::json  SetMode(const nlohmann::json& params);
    nlohmann::json  SetCustomMode(const nlohmann::json& params);
    nlohmann::json  UpdateMode(const nlohmann::json& params);

    // Zone management methods
    nlohmann::json  GetZones(const nlohmann::json& params);
    nlohmann::json  ResizeZone(const nlohmann::json& params);
    nlohmann::json  ClearSegments(const nlohmann::json& params);
    nlohmann::json  AddSegment(const nlohmann::json& params);

    // Profile management methods
    nlohmann::json  GetProfiles(const nlohmann::json& params);
    nlohmann::json  SaveProfile(const nlohmann::json& params);
    nlohmann::json  LoadProfile(const nlohmann::json& params);
    nlohmann::json  DeleteProfile(const nlohmann::json& params);

    // Server info methods
    nlohmann::json  GetProtocolVersion(const nlohmann::json& params);
    nlohmann::json  GetServerInfo(const nlohmann::json& params);
    nlohmann::json  GetClients(const nlohmann::json& params);

    // Plugin methods
    nlohmann::json  GetPlugins(const nlohmann::json& params);
    nlohmann::json  CallPlugin(const nlohmann::json& params);

    // Helper functions
    nlohmann::json  CreateError(int code, const std::string& message,
                               const nlohmann::json& data = nullptr);
    nlohmann::json  CreateResult(const nlohmann::json& result, int id);
    bool            ValidateDeviceIndex(unsigned int device_idx);
    bool            ValidateZoneIndex(unsigned int device_idx,
                                     unsigned int zone_idx);
    RGBColor        ParseColor(const nlohmann::json& color_obj);
    nlohmann::json  ZoneToJSON(RGBController* controller, int zone_idx);
    nlohmann::json  ModeToJSON(RGBController* controller, int mode_idx);
    nlohmann::json  LEDToJSON(RGBController* controller, int led_idx);

    std::vector<RGBController*>&    controllers;
    ResourceManager*                resource_manager;
    ProfileManagerInterface*        profile_manager;

    // Async rescan management
    std::shared_future<void>        rescan_future;
    std::mutex                      rescan_mutex;
    bool                            rescan_in_progress = false;
};
