/*---------------------------------------------------------*\
| JSONRPCHandler.cpp                                        |
|                                                           |
|   JSON-RPC Request Handler Implementation                 |
|                                                           |
|   This file is part of the OpenRGB project                |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

/*---------------------------------------------------------*\
| Modified by JKWTCN <jkwtcn@icloud.com>                   |
| Date: 2026-04-02                                          |
| Changes:                                                  |
|   - Added async device rescan support                    |
|   - Enhanced scan completion event handling              |
|   - Added rescan state management with mutex             |
\*---------------------------------------------------------*/

#include "JSONRPCHandler.h"
#include "WebSocketServer.h"
#include <algorithm>
#include <sstream>

JSONRPCHandler::JSONRPCHandler(std::vector<RGBController*>& controllers,
                               ResourceManager* resource_manager,
                               ProfileManagerInterface* profile_manager)
    : controllers(controllers), resource_manager(resource_manager),
      profile_manager(profile_manager)
{
}

JSONRPCHandler::~JSONRPCHandler()
{
}

void JSONRPCHandler::SetProfileManager(ProfileManagerInterface* profile_manager)
{
    this->profile_manager = profile_manager;
}

/*---------------------------------------------------------*\
| Main Request Handler                                      |
\*---------------------------------------------------------*/
nlohmann::json JSONRPCHandler::HandleRequest(const nlohmann::json& request)
{
    try
    {
        // Validate JSON-RPC 2.0 request format
        if(!request.contains("jsonrpc") || request["jsonrpc"] != "2.0")
        {
            return CreateError(JSONRPCProtocol::INVALID_REQUEST,
                             "Missing or invalid 'jsonrpc' field");
        }

        if(!request.contains("method"))
        {
            return CreateError(JSONRPCProtocol::INVALID_REQUEST,
                             "Missing 'method' field");
        }

        // Extract request fields
        std::string method = request["method"];
        nlohmann::json params = request.value("params", nlohmann::json::object());
        int id = request.value("id", 0);

        // Call the method
        nlohmann::json result = CallMethod(method, params);

        // Check if result is an error
        if(result.contains("error"))
        {
            nlohmann::json response;
            response["jsonrpc"] = "2.0";
            response["error"] = result["error"];
            response["id"] = id;
            return response;
        }

        // Return success response
        return CreateResult(result, id);
    }
    catch(const std::exception& e)
    {
        return CreateError(JSONRPCProtocol::INTERNAL_ERROR,
                          std::string("Internal error: ") + e.what());
    }
}

nlohmann::json JSONRPCHandler::HandleBatchRequest(const nlohmann::json& requests)
{
    nlohmann::json responses = nlohmann::json::array();

    if(!requests.is_array())
    {
        return CreateError(JSONRPCProtocol::INVALID_REQUEST,
                         "Batch request must be an array");
    }

    for(const auto& request : requests)
    {
        responses.push_back(HandleRequest(request));
    }

    return responses;
}

/*---------------------------------------------------------*\
| Method Dispatcher                                         |
\*---------------------------------------------------------*/
nlohmann::json JSONRPCHandler::CallMethod(const std::string& method,
                                          const nlohmann::json& params)
{
    // Device Management
    if(method == JSONRPCProtocol::Methods::GET_CONTROLLERS)
    {
        return GetControllers(params);
    }
    else if(method == JSONRPCProtocol::Methods::GET_CONTROLLER_COUNT)
    {
        return GetControllerCount(params);
    }
    else if(method == JSONRPCProtocol::Methods::GET_CONTROLLER_DATA)
    {
        return GetControllerData(params);
    }
    else if(method == JSONRPCProtocol::Methods::GET_CONTROLLER_INFO)
    {
        return GetControllerInfo(params);
    }
    else if(method == JSONRPCProtocol::Methods::RESCAN_DEVICES)
    {
        return RescanDevices(params);
    }
    // Color Control
    else if(method == JSONRPCProtocol::Methods::SET_LED_COLOR)
    {
        return SetLEDColor(params);
    }
    else if(method == JSONRPCProtocol::Methods::SET_ZONE_COLOR)
    {
        return SetZoneColor(params);
    }
    else if(method == JSONRPCProtocol::Methods::SET_ALL_COLORS)
    {
        return SetAllColors(params);
    }
    else if(method == JSONRPCProtocol::Methods::SET_MULTIPLE_COLORS)
    {
        return SetMultipleColors(params);
    }
    // Mode Control
    else if(method == JSONRPCProtocol::Methods::SET_MODE)
    {
        return SetMode(params);
    }
    else if(method == JSONRPCProtocol::Methods::SET_CUSTOM_MODE)
    {
        return SetCustomMode(params);
    }
    else if(method == JSONRPCProtocol::Methods::UPDATE_MODE)
    {
        return UpdateMode(params);
    }
    // Zone Management
    else if(method == JSONRPCProtocol::Methods::GET_ZONES)
    {
        return GetZones(params);
    }
    else if(method == JSONRPCProtocol::Methods::RESIZE_ZONE)
    {
        return ResizeZone(params);
    }
    else if(method == JSONRPCProtocol::Methods::CLEAR_SEGMENTS)
    {
        return ClearSegments(params);
    }
    else if(method == JSONRPCProtocol::Methods::ADD_SEGMENT)
    {
        return AddSegment(params);
    }
    // Profile Management
    else if(method == JSONRPCProtocol::Methods::GET_PROFILES)
    {
        return GetProfiles(params);
    }
    else if(method == JSONRPCProtocol::Methods::SAVE_PROFILE)
    {
        return SaveProfile(params);
    }
    else if(method == JSONRPCProtocol::Methods::LOAD_PROFILE)
    {
        return LoadProfile(params);
    }
    else if(method == JSONRPCProtocol::Methods::DELETE_PROFILE)
    {
        return DeleteProfile(params);
    }
    // Server Information
    else if(method == JSONRPCProtocol::Methods::GET_PROTOCOL_VERSION)
    {
        return GetProtocolVersion(params);
    }
    else if(method == JSONRPCProtocol::Methods::GET_SERVER_INFO)
    {
        return GetServerInfo(params);
    }
    else if(method == JSONRPCProtocol::Methods::GET_CLIENTS)
    {
        return GetClients(params);
    }
    // Plugin Management
    else if(method == JSONRPCProtocol::Methods::GET_PLUGINS)
    {
        return GetPlugins(params);
    }
    else if(method == JSONRPCProtocol::Methods::CALL_PLUGIN)
    {
        return CallPlugin(params);
    }
    else
    {
        return CreateError(JSONRPCProtocol::METHOD_NOT_FOUND,
                          "Method not found: " + method);
    }
}

/*---------------------------------------------------------*\
| Device Management Methods                                 |
\*---------------------------------------------------------*/
nlohmann::json JSONRPCHandler::GetControllers(const nlohmann::json& params)
{
    nlohmann::json result;
    nlohmann::json controllers_array = nlohmann::json::array();

    for(unsigned int i = 0; i < controllers.size(); i++)
    {
        controllers_array.push_back(ControllerToJSON(controllers[i]));
    }

    result["controllers"] = controllers_array;
    return result;
}

nlohmann::json JSONRPCHandler::GetControllerCount(const nlohmann::json& params)
{
    nlohmann::json result;
    result["count"] = controllers.size();
    return result;
}

nlohmann::json JSONRPCHandler::GetControllerData(const nlohmann::json& params)
{
    if(!params.contains("deviceIndex"))
    {
        return CreateError(JSONRPCProtocol::INVALID_PARAMS,
                          "Missing 'deviceIndex' parameter");
    }

    unsigned int device_idx = params["deviceIndex"];

    if(!ValidateDeviceIndex(device_idx))
    {
        return CreateError(JSONRPCProtocol::ERR_DEVICE_INDEX_OUT_OF_RANGE,
                          "Device index out of range");
    }

    nlohmann::json result;
    result["controller"] = ControllerToJSON(controllers[device_idx]);
    return result;
}

nlohmann::json JSONRPCHandler::GetControllerInfo(const nlohmann::json& params)
{
    if(!params.contains("deviceIndex"))
    {
        return CreateError(JSONRPCProtocol::INVALID_PARAMS,
                          "Missing 'deviceIndex' parameter");
    }

    unsigned int device_idx = params["deviceIndex"];

    if(!ValidateDeviceIndex(device_idx))
    {
        return CreateError(JSONRPCProtocol::ERR_DEVICE_INDEX_OUT_OF_RANGE,
                          "Device index out of range");
    }

    RGBController* controller = controllers[device_idx];

    nlohmann::json result;
    result["name"] = controller->name;
    result["vendor"] = controller->vendor;
    result["description"] = controller->description;
    result["type"] = controller->type;
    result["location"] = controller->location;
    result["serial"] = controller->serial;

    return result;
}

nlohmann::json JSONRPCHandler::RescanDevices(const nlohmann::json& params)
{
    std::lock_guard<std::mutex> lock(rescan_mutex);

    // Check if a rescan is already in progress
    if(rescan_in_progress)
    {
        // Check if the previous rescan has completed
        if(rescan_future.valid() &&
           rescan_future.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        {
            // Rescan is still in progress
            nlohmann::json result;
            result["success"] = false;
            result["message"] = "Rescan already in progress";
            return result;
        }
        else
        {
            // Previous rescan completed, reset flag
            rescan_in_progress = false;
        }
    }

    if(resource_manager)
    {
        // Mark rescan as in progress
        rescan_in_progress = true;

        // Launch async rescan in background thread
        rescan_future = std::async(std::launch::async, [this]()
        {
            resource_manager->RescanDevices();

            // Reset flag when done
            std::lock_guard<std::mutex> lock(rescan_mutex);
            rescan_in_progress = false;
        });

        // Detach the future to allow it to run in background
        // The result will be communicated via scanComplete event
    }

    nlohmann::json result;
    result["success"] = true;
    result["message"] = "Rescan started";
    return result;
}

/*---------------------------------------------------------*\
| Color Control Methods                                     |
\*---------------------------------------------------------*/
nlohmann::json JSONRPCHandler::SetLEDColor(const nlohmann::json& params)
{
    if(!params.contains("deviceIndex") || !params.contains("ledIndex") || !params.contains("color"))
    {
        return CreateError(JSONRPCProtocol::INVALID_PARAMS,
                          "Missing required parameters: deviceIndex, ledIndex, color");
    }

    unsigned int device_idx = params["deviceIndex"];
    unsigned int led_idx = params["ledIndex"];

    if(!ValidateDeviceIndex(device_idx))
    {
        return CreateError(JSONRPCProtocol::ERR_DEVICE_INDEX_OUT_OF_RANGE,
                          "Device index out of range");
    }

    RGBController* controller = controllers[device_idx];

    if(led_idx >= controller->leds.size())
    {
        return CreateError(JSONRPCProtocol::ERR_LED_INDEX_OUT_OF_RANGE,
                          "LED index out of range");
    }

    RGBColor color = ParseColor(params["color"]);
    controller->SetLED(led_idx, color);
    controller->UpdateLEDs();

    nlohmann::json result;
    result["success"] = true;
    return result;
}

nlohmann::json JSONRPCHandler::SetZoneColor(const nlohmann::json& params)
{
    if(!params.contains("deviceIndex") || !params.contains("zoneIndex") || !params.contains("color"))
    {
        return CreateError(JSONRPCProtocol::INVALID_PARAMS,
                          "Missing required parameters: deviceIndex, zoneIndex, color");
    }

    unsigned int device_idx = params["deviceIndex"];
    unsigned int zone_idx = params["zoneIndex"];

    if(!ValidateDeviceIndex(device_idx))
    {
        return CreateError(JSONRPCProtocol::ERR_DEVICE_INDEX_OUT_OF_RANGE,
                          "Device index out of range");
    }

    RGBController* controller = controllers[device_idx];

    if(zone_idx >= controller->zones.size())
    {
        return CreateError(JSONRPCProtocol::ERR_ZONE_INDEX_OUT_OF_RANGE,
                          "Zone index out of range");
    }

    RGBColor color = ParseColor(params["color"]);
    controller->SetAllZoneLEDs(zone_idx, color);
    controller->UpdateLEDs();

    nlohmann::json result;
    result["success"] = true;
    return result;
}

nlohmann::json JSONRPCHandler::SetAllColors(const nlohmann::json& params)
{
    if(!params.contains("deviceIndex") || !params.contains("color"))
    {
        return CreateError(JSONRPCProtocol::INVALID_PARAMS,
                          "Missing required parameters: deviceIndex, color");
    }

    unsigned int device_idx = params["deviceIndex"];

    if(!ValidateDeviceIndex(device_idx))
    {
        return CreateError(JSONRPCProtocol::ERR_DEVICE_INDEX_OUT_OF_RANGE,
                          "Device index out of range");
    }

    RGBController* controller = controllers[device_idx];
    RGBColor color = ParseColor(params["color"]);

    controller->SetAllLEDs(color);
    controller->UpdateLEDs();

    nlohmann::json result;
    result["success"] = true;
    return result;
}

nlohmann::json JSONRPCHandler::SetMultipleColors(const nlohmann::json& params)
{
    if(!params.contains("deviceIndex") || !params.contains("colors"))
    {
        return CreateError(JSONRPCProtocol::INVALID_PARAMS,
                          "Missing required parameters: deviceIndex, colors");
    }

    unsigned int device_idx = params["deviceIndex"];

    if(!ValidateDeviceIndex(device_idx))
    {
        return CreateError(JSONRPCProtocol::ERR_DEVICE_INDEX_OUT_OF_RANGE,
                          "Device index out of range");
    }

    RGBController* controller = controllers[device_idx];
    const auto& colors = params["colors"];

    if(!colors.is_array())
    {
        return CreateError(JSONRPCProtocol::INVALID_PARAMS,
                          "'colors' must be an array");
    }

    for(const auto& color_item : colors)
    {
        if(!color_item.contains("ledIndex") || !color_item.contains("color"))
        {
            continue;
        }

        unsigned int led_idx = color_item["ledIndex"];
        if(led_idx < controller->leds.size())
        {
            RGBColor color = ParseColor(color_item["color"]);
            controller->SetLED(led_idx, color);
        }
    }

    controller->UpdateLEDs();

    nlohmann::json result;
    result["success"] = true;
    return result;
}

/*---------------------------------------------------------*\
| Mode Control Methods                                      |
\*---------------------------------------------------------*/
nlohmann::json JSONRPCHandler::SetMode(const nlohmann::json& params)
{
    if(!params.contains("deviceIndex") || !params.contains("modeIndex"))
    {
        return CreateError(JSONRPCProtocol::INVALID_PARAMS,
                          "Missing required parameters: deviceIndex, modeIndex");
    }

    unsigned int device_idx = params["deviceIndex"];
    unsigned int mode_idx = params["modeIndex"];

    if(!ValidateDeviceIndex(device_idx))
    {
        return CreateError(JSONRPCProtocol::ERR_DEVICE_INDEX_OUT_OF_RANGE,
                          "Device index out of range");
    }

    RGBController* controller = controllers[device_idx];

    if(mode_idx >= controller->modes.size())
    {
        return CreateError(JSONRPCProtocol::ERR_MODE_INDEX_OUT_OF_RANGE,
                          "Mode index out of range");
    }

    controller->SetMode(mode_idx);
    controller->UpdateLEDs();

    nlohmann::json result;
    result["success"] = true;
    return result;
}

nlohmann::json JSONRPCHandler::SetCustomMode(const nlohmann::json& params)
{
    if(!params.contains("deviceIndex"))
    {
        return CreateError(JSONRPCProtocol::INVALID_PARAMS,
                          "Missing 'deviceIndex' parameter");
    }

    unsigned int device_idx = params["deviceIndex"];

    if(!ValidateDeviceIndex(device_idx))
    {
        return CreateError(JSONRPCProtocol::ERR_DEVICE_INDEX_OUT_OF_RANGE,
                          "Device index out of range");
    }

    RGBController* controller = controllers[device_idx];
    controller->SetCustomMode();
    controller->UpdateLEDs();

    nlohmann::json result;
    result["success"] = true;
    return result;
}

nlohmann::json JSONRPCHandler::UpdateMode(const nlohmann::json& params)
{
    if(!params.contains("deviceIndex") || !params.contains("mode"))
    {
        return CreateError(JSONRPCProtocol::INVALID_PARAMS,
                          "Missing required parameters: deviceIndex, mode");
    }

    unsigned int device_idx = params["deviceIndex"];

    if(!ValidateDeviceIndex(device_idx))
    {
        return CreateError(JSONRPCProtocol::ERR_DEVICE_INDEX_OUT_OF_RANGE,
                          "Device index out of range");
    }

    RGBController* controller = controllers[device_idx];
    const auto& mode_obj = params["mode"];

    if(!mode_obj.contains("index"))
    {
        return CreateError(JSONRPCProtocol::INVALID_PARAMS,
                          "Mode object missing 'index' field");
    }

    unsigned int mode_idx = mode_obj["index"];

    if(mode_idx >= controller->modes.size())
    {
        return CreateError(JSONRPCProtocol::ERR_MODE_INDEX_OUT_OF_RANGE,
                          "Mode index out of range");
    }

    // Update mode parameters if provided
    if(mode_obj.contains("speed") || mode_obj.contains("direction") ||
       mode_obj.contains("colors") || mode_obj.contains("value"))
    {
        if(mode_obj.contains("speed"))
        {
            controller->modes[mode_idx].speed = mode_obj["speed"];
        }
        if(mode_obj.contains("direction"))
        {
            controller->modes[mode_idx].direction = mode_obj["direction"];
        }
        if(mode_obj.contains("colors"))
        {
            const auto& colors = mode_obj["colors"];
            if(colors.is_array())
            {
                controller->modes[mode_idx].colors.clear();
                for(const auto& color : colors)
                {
                    controller->modes[mode_idx].colors.push_back(ParseColor(color));
                }
            }
        }
    }

    controller->SetMode(mode_idx);
    controller->UpdateLEDs();

    nlohmann::json result;
    result["success"] = true;
    return result;
}

/*---------------------------------------------------------*\
| Zone Management Methods                                   |
\*---------------------------------------------------------*/
nlohmann::json JSONRPCHandler::GetZones(const nlohmann::json& params)
{
    if(!params.contains("deviceIndex"))
    {
        return CreateError(JSONRPCProtocol::INVALID_PARAMS,
                          "Missing 'deviceIndex' parameter");
    }

    unsigned int device_idx = params["deviceIndex"];

    if(!ValidateDeviceIndex(device_idx))
    {
        return CreateError(JSONRPCProtocol::ERR_DEVICE_INDEX_OUT_OF_RANGE,
                          "Device index out of range");
    }

    nlohmann::json result;
    nlohmann::json zones_array = nlohmann::json::array();
    RGBController* controller = controllers[device_idx];

    for(unsigned int i = 0; i < controller->zones.size(); i++)
    {
        zones_array.push_back(ZoneToJSON(controller, i));
    }

    result["zones"] = zones_array;
    return result;
}

nlohmann::json JSONRPCHandler::ResizeZone(const nlohmann::json& params)
{
    if(!params.contains("deviceIndex") || !params.contains("zoneIndex") || !params.contains("newSize"))
    {
        return CreateError(JSONRPCProtocol::INVALID_PARAMS,
                          "Missing required parameters: deviceIndex, zoneIndex, newSize");
    }

    unsigned int device_idx = params["deviceIndex"];
    unsigned int zone_idx = params["zoneIndex"];
    int new_size = params["newSize"];

    if(!ValidateDeviceIndex(device_idx))
    {
        return CreateError(JSONRPCProtocol::ERR_DEVICE_INDEX_OUT_OF_RANGE,
                          "Device index out of range");
    }

    RGBController* controller = controllers[device_idx];

    if(zone_idx >= controller->zones.size())
    {
        return CreateError(JSONRPCProtocol::ERR_ZONE_INDEX_OUT_OF_RANGE,
                          "Zone index out of range");
    }

    // Check if zone is resizable by comparing leds_min and leds_max
    if(controller->zones[zone_idx].leds_min == controller->zones[zone_idx].leds_max)
    {
        return CreateError(JSONRPCProtocol::ERR_RESIZE_NOT_SUPPORTED,
                          "Zone is not resizeable");
    }

    controller->ResizeZone(zone_idx, new_size);

    nlohmann::json result;
    result["success"] = true;
    return result;
}

nlohmann::json JSONRPCHandler::ClearSegments(const nlohmann::json& params)
{
    if(!params.contains("deviceIndex") || !params.contains("zoneIndex"))
    {
        return CreateError(JSONRPCProtocol::INVALID_PARAMS,
                          "Missing required parameters: deviceIndex, zoneIndex");
    }

    unsigned int device_idx = params["deviceIndex"];
    unsigned int zone_idx = params["zoneIndex"];

    if(!ValidateDeviceIndex(device_idx))
    {
        return CreateError(JSONRPCProtocol::ERR_DEVICE_INDEX_OUT_OF_RANGE,
                          "Device index out of range");
    }

    RGBController* controller = controllers[device_idx];

    if(zone_idx >= controller->zones.size())
    {
        return CreateError(JSONRPCProtocol::ERR_ZONE_INDEX_OUT_OF_RANGE,
                          "Zone index out of range");
    }

    controller->ClearSegments(zone_idx);

    nlohmann::json result;
    result["success"] = true;
    return result;
}

nlohmann::json JSONRPCHandler::AddSegment(const nlohmann::json& params)
{
    if(!params.contains("deviceIndex") || !params.contains("zoneIndex") || !params.contains("segment"))
    {
        return CreateError(JSONRPCProtocol::INVALID_PARAMS,
                          "Missing required parameters: deviceIndex, zoneIndex, segment");
    }

    unsigned int device_idx = params["deviceIndex"];
    unsigned int zone_idx = params["zoneIndex"];

    if(!ValidateDeviceIndex(device_idx))
    {
        return CreateError(JSONRPCProtocol::ERR_DEVICE_INDEX_OUT_OF_RANGE,
                          "Device index out of range");
    }

    RGBController* controller = controllers[device_idx];

    if(zone_idx >= controller->zones.size())
    {
        return CreateError(JSONRPCProtocol::ERR_ZONE_INDEX_OUT_OF_RANGE,
                          "Zone index out of range");
    }

    const auto& segment_obj = params["segment"];

    unsigned int start_idx = segment_obj.value("start", 0);
    unsigned int leds_count = segment_obj.value("length", 0);
    std::string name = segment_obj.value("name", "");

    segment new_seg;
    new_seg.name = name;
    new_seg.start_idx = start_idx;
    new_seg.leds_count = leds_count;

    controller->AddSegment(zone_idx, new_seg);

    nlohmann::json result;
    result["success"] = true;
    return result;
}

/*---------------------------------------------------------*\
| Profile Management Methods                                |
\*---------------------------------------------------------*/
nlohmann::json JSONRPCHandler::GetProfiles(const nlohmann::json& params)
{
    nlohmann::json result;
    nlohmann::json profiles_array = nlohmann::json::array();

    if(profile_manager)
    {
        for(const auto& name : profile_manager->profile_list)
        {
            profiles_array.push_back(name);
        }
    }

    result["profiles"] = profiles_array;
    return result;
}

nlohmann::json JSONRPCHandler::SaveProfile(const nlohmann::json& params)
{
    if(!params.contains("profileName"))
    {
        return CreateError(JSONRPCProtocol::INVALID_PARAMS,
                          "Missing 'profileName' parameter");
    }

    std::string profile_name = params["profileName"];
    bool include_sizes = params.value("includeSizes", true);

    if(!profile_manager)
    {
        return CreateError(JSONRPCProtocol::ERR_PROFILE_SAVE_FAILED,
                          "Profile manager not initialized");
    }

    profile_manager->SaveProfile(profile_name, include_sizes);

    nlohmann::json result;
    result["success"] = true;
    return result;
}

nlohmann::json JSONRPCHandler::LoadProfile(const nlohmann::json& params)
{
    if(!params.contains("profileName"))
    {
        return CreateError(JSONRPCProtocol::INVALID_PARAMS,
                          "Missing 'profileName' parameter");
    }

    std::string profile_name = params["profileName"];

    if(!profile_manager)
    {
        return CreateError(JSONRPCProtocol::ERR_PROFILE_NOT_FOUND,
                          "Profile manager not initialized");
    }

    std::vector<std::string> profile_names = profile_manager->profile_list;

    if(std::find(profile_names.begin(), profile_names.end(), profile_name) == profile_names.end())
    {
        return CreateError(JSONRPCProtocol::ERR_PROFILE_NOT_FOUND,
                          "Profile not found: " + profile_name);
    }

    profile_manager->LoadProfile(profile_name);
    profile_manager->LoadSizeFromProfile(profile_name);

    nlohmann::json result;
    result["success"] = true;
    return result;
}

nlohmann::json JSONRPCHandler::DeleteProfile(const nlohmann::json& params)
{
    if(!params.contains("profileName"))
    {
        return CreateError(JSONRPCProtocol::INVALID_PARAMS,
                          "Missing 'profileName' parameter");
    }

    std::string profile_name = params["profileName"];

    if(!profile_manager)
    {
        return CreateError(JSONRPCProtocol::ERR_PROFILE_NOT_FOUND,
                          "Profile manager not initialized");
    }

    std::vector<std::string> profile_names = profile_manager->profile_list;

    if(std::find(profile_names.begin(), profile_names.end(), profile_name) == profile_names.end())
    {
        return CreateError(JSONRPCProtocol::ERR_PROFILE_NOT_FOUND,
                          "Profile not found: " + profile_name);
    }

    profile_manager->DeleteProfile(profile_name);

    nlohmann::json result;
    result["success"] = true;
    return result;
}

/*---------------------------------------------------------*\
| Server Information Methods                                |
\*---------------------------------------------------------*/
nlohmann::json JSONRPCHandler::GetProtocolVersion(const nlohmann::json& params)
{
    nlohmann::json result;
    result["protocolVersion"] = JSONRPCProtocol::PROTOCOL_VERSION;
    result["jsonrpcVersion"] = JSONRPCProtocol::JSON_RPC_VERSION;
    return result;
}

nlohmann::json JSONRPCHandler::GetServerInfo(const nlohmann::json& params)
{
    nlohmann::json result;
    result["serverVersion"] = VERSION_STRING;
    result["protocolVersion"] = JSONRPCProtocol::PROTOCOL_VERSION;
    result["sdkVersion"] = "1.0";
    result["capabilities"] = nlohmann::json::array();
    result["capabilities"].push_back("deviceManagement");
    result["capabilities"].push_back("colorControl");
    result["capabilities"].push_back("modeControl");
    result["capabilities"].push_back("zoneManagement");
    result["capabilities"].push_back("profileManagement");
    result["capabilities"].push_back("eventNotifications");
    return result;
}

nlohmann::json JSONRPCHandler::GetClients(const nlohmann::json& params)
{
    nlohmann::json result;
    result["clients"] = nlohmann::json::array();
    // This will be implemented by WebSocketServer
    return result;
}

/*---------------------------------------------------------*\
| Plugin Methods                                            |
\*---------------------------------------------------------*/
nlohmann::json JSONRPCHandler::GetPlugins(const nlohmann::json& params)
{
    nlohmann::json result;
    result["plugins"] = nlohmann::json::array();
    // Plugin support will be added later
    return result;
}

nlohmann::json JSONRPCHandler::CallPlugin(const nlohmann::json& params)
{
    if(!params.contains("pluginName") || !params.contains("method") || !params.contains("params"))
    {
        return CreateError(JSONRPCProtocol::INVALID_PARAMS,
                          "Missing required parameters: pluginName, method, params");
    }

    return CreateError(JSONRPCProtocol::ERR_PLUGIN_NOT_FOUND,
                      "Plugin support not yet implemented");
}

/*---------------------------------------------------------*\
| Helper Functions                                          |
\*---------------------------------------------------------*/
nlohmann::json JSONRPCHandler::CreateError(int code, const std::string& message,
                                           const nlohmann::json& data)
{
    nlohmann::json error;
    error["code"] = code;
    error["message"] = message;

    if(!data.is_null())
    {
        error["data"] = data;
    }

    nlohmann::json result;
    result["error"] = error;
    return result;
}

nlohmann::json JSONRPCHandler::CreateResult(const nlohmann::json& result_data, int id)
{
    nlohmann::json response;
    response["jsonrpc"] = "2.0";
    response["result"] = result_data;
    response["id"] = id;
    return response;
}

bool JSONRPCHandler::ValidateDeviceIndex(unsigned int device_idx)
{
    return device_idx < controllers.size();
}

bool JSONRPCHandler::ValidateZoneIndex(unsigned int device_idx, unsigned int zone_idx)
{
    if(!ValidateDeviceIndex(device_idx))
    {
        return false;
    }

    return zone_idx < controllers[device_idx]->zones.size();
}

RGBColor JSONRPCHandler::ParseColor(const nlohmann::json& color_obj)
{
    RGBColor color = 0;

    if(color_obj.contains("red"))
    {
        unsigned int red = color_obj["red"];
        unsigned int green = color_obj.value("green", 0);
        unsigned int blue = color_obj.value("blue", 0);

        color = ToRGBColor(red, green, blue);
    }
    else if(color_obj.is_string())
    {
        std::string color_str = color_obj;
        if(color_str.size() == 7 && color_str[0] == '#')
        {
            std::stringstream ss;
            ss << std::hex << color_str.substr(1);
            unsigned int color_val;
            ss >> color_val;
            color = color_val;
        }
    }
    else if(color_obj.is_number())
    {
        color = color_obj;
    }

    return color;
}

nlohmann::json JSONRPCHandler::ControllerToJSON(RGBController* controller)
{
    nlohmann::json json_obj;

    json_obj["name"] = controller->name;
    json_obj["vendor"] = controller->vendor;
    json_obj["description"] = controller->description;
    json_obj["type"] = controller->type;
    json_obj["location"] = controller->location;
    json_obj["serial"] = controller->serial;

    json_obj["ledCount"] = controller->leds.size();
    json_obj["zoneCount"] = controller->zones.size();
    json_obj["modeCount"] = controller->modes.size();

    // Add LEDs
    nlohmann::json leds_array = nlohmann::json::array();
    for(unsigned int i = 0; i < controller->leds.size(); i++)
    {
        leds_array.push_back(LEDToJSON(controller, i));
    }
    json_obj["leds"] = leds_array;

    // Add modes
    nlohmann::json modes_array = nlohmann::json::array();
    for(unsigned int i = 0; i < controller->modes.size(); i++)
    {
        modes_array.push_back(ModeToJSON(controller, i));
    }
    json_obj["modes"] = modes_array;

    return json_obj;
}

nlohmann::json JSONRPCHandler::ZoneToJSON(RGBController* controller, int zone_idx)
{
    nlohmann::json json_obj;
    const zone& z = controller->zones[zone_idx];

    json_obj["name"] = z.name;
    json_obj["type"] = z.type;
    json_obj["ledsMin"] = z.leds_min;
    json_obj["ledsMax"] = z.leds_max;
    json_obj["ledsCount"] = z.leds_count;

    // Convert matrix_map to JSON if it exists
    if(z.matrix_map)
    {
        nlohmann::json matrix_obj;
        matrix_obj["height"] = z.matrix_map->height;
        matrix_obj["width"] = z.matrix_map->width;

        nlohmann::json map_array = nlohmann::json::array();
        if(z.matrix_map->map)
        {
            for(unsigned int i = 0; i < (z.matrix_map->height * z.matrix_map->width); i++)
            {
                map_array.push_back(z.matrix_map->map[i]);
            }
        }
        matrix_obj["map"] = map_array;
        json_obj["matrixMap"] = matrix_obj;
    }
    else
    {
        json_obj["matrixMap"] = nullptr;
    }

    nlohmann::json segments_array = nlohmann::json::array();
    for(unsigned int i = 0; i < z.segments.size(); i++)
    {
        nlohmann::json segment_obj;
        segment_obj["name"] = z.segments[i].name;
        segment_obj["start"] = z.segments[i].start_idx;
        segment_obj["length"] = z.segments[i].leds_count;
        segments_array.push_back(segment_obj);
    }
    json_obj["segments"] = segments_array;

    return json_obj;
}

nlohmann::json JSONRPCHandler::ModeToJSON(RGBController* controller, int mode_idx)
{
    nlohmann::json json_obj;
    const mode& m = controller->modes[mode_idx];

    json_obj["name"] = m.name;
    json_obj["value"] = m.value;
    json_obj["flags"] = m.flags;
    json_obj["speedMin"] = m.speed_min;
    json_obj["speedMax"] = m.speed_max;
    json_obj["brightnessMin"] = m.brightness_min;
    json_obj["brightnessMax"] = m.brightness_max;
    json_obj["colorsMin"] = m.colors_min;
    json_obj["colorsMax"] = m.colors_max;
    json_obj["speed"] = m.speed;
    json_obj["brightness"] = m.brightness;
    json_obj["direction"] = m.direction;
    json_obj["colorMode"] = m.color_mode;

    nlohmann::json colors_array = nlohmann::json::array();
    for(unsigned int i = 0; i < m.colors.size(); i++)
    {
        RGBColor color = m.colors[i];
        nlohmann::json color_obj;
        color_obj["red"] = RGBGetRValue(color);
        color_obj["green"] = RGBGetGValue(color);
        color_obj["blue"] = RGBGetBValue(color);
        colors_array.push_back(color_obj);
    }
    json_obj["colors"] = colors_array;

    return json_obj;
}

nlohmann::json JSONRPCHandler::LEDToJSON(RGBController* controller, int led_idx)
{
    nlohmann::json json_obj;
    const led& l = controller->leds[led_idx];

    json_obj["name"] = l.name;
    json_obj["value"] = l.value;

    return json_obj;
}
