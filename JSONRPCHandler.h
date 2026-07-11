/*---------------------------------------------------------*\
| JSONRPCHandler.h                                          |
|                                                           |
|   JSON-RPC Request Handler                                |
|                                                           |
|   This file is part of the OpenRGB project                |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

/*---------------------------------------------------------*\
| Modified by JKWTCN <jkwtcn@icloud.com>                   |
| Date: 2026-04-02                                          |
| Changes:                                                  |
|   - Added async rescan management members                |
|   - Added RescanDevices() method declaration             |
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
    nlohmann::json  HandleRequest(const nlohmann::json& request,
                                  bool client_is_loopback = false);

    // Batch request support
    nlohmann::json  HandleBatchRequest(const nlohmann::json& requests,
                                       bool client_is_loopback = false);

    // Set profile manager
    void            SetProfileManager(ProfileManagerInterface* profile_manager);

    // Helper functions for external use
    nlohmann::json  ControllerToJSON(RGBController* controller);
    nlohmann::json  ControllerToScanCompleteJSON(RGBController* controller);
    bool            TakeShutdownRequested();

private:
    // Method dispatchers
    nlohmann::json  CallMethod(const std::string& method,
                              const nlohmann::json& params,
                              bool client_is_loopback);

    // Device management methods
    nlohmann::json  GetControllers(const nlohmann::json& params);
    nlohmann::json  GetControllerCount(const nlohmann::json& params);
    nlohmann::json  GetControllerData(const nlohmann::json& params);
    nlohmann::json  GetControllerInfo(const nlohmann::json& params);
    nlohmann::json  RescanDevices(const nlohmann::json& params);

    // Color control methods
    nlohmann::json  SetLEDColor(const nlohmann::json& params);
    nlohmann::json  SetZoneColor(const nlohmann::json& params);
    nlohmann::json  SetZoneMultipleLed(const nlohmann::json& params);
    nlohmann::json  SetMultipleZoneMultipleLed(const nlohmann::json& params);
    nlohmann::json  SetAllColors(const nlohmann::json& params);
    nlohmann::json  SetMultipleColors(const nlohmann::json& params);
    nlohmann::json  SetKeyColor(const nlohmann::json& params);

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
    nlohmann::json  ShutdownServer(const nlohmann::json& params,
                                   bool client_is_loopback);

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
    nlohmann::json  LEDToScanCompleteJSON(RGBController* controller, unsigned int led_idx,
                                          unsigned int x, unsigned int y);
    nlohmann::json  MatrixLEDsToScanCompleteJSON(RGBController* controller, const zone& matrix_zone);

    /*---------------------------------------------------------*\
    | Acquires the ResourceManager device-list mutex.  Every   |
    | RPC method that touches the controllers vector must hold |
    | the returned lock for the whole access so it cannot race |
    | with an asynchronous rescan rebuilding the list.         |
    \*---------------------------------------------------------*/
    std::unique_lock<std::mutex> LockControllerList();

    std::vector<RGBController*>&    controllers;
    ResourceManager*                resource_manager;
    ProfileManagerInterface*        profile_manager;

    // Async rescan management
    std::shared_future<void>        rescan_future;
    std::mutex                      rescan_mutex;
    bool                            rescan_in_progress = false;
    bool                            shutdown_requested = false;
};
