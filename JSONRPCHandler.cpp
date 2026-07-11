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

JSONRPCHandler::JSONRPCHandler(std::vector<RGBController *> &controllers,
                               ResourceManager *resource_manager,
                               ProfileManagerInterface *profile_manager)
    : controllers(controllers), resource_manager(resource_manager),
      profile_manager(profile_manager)
{
}

JSONRPCHandler::~JSONRPCHandler()
{
}

void JSONRPCHandler::SetProfileManager(ProfileManagerInterface *profile_manager)
{
    this->profile_manager = profile_manager;
}

/*---------------------------------------------------------*\
| Main Request Handler                                      |
\*---------------------------------------------------------*/
nlohmann::json JSONRPCHandler::HandleRequest(const nlohmann::json &request,
                                             bool client_is_loopback)
{
    try
    {
        // Validate JSON-RPC 2.0 request format
        if (!request.contains("jsonrpc") || request["jsonrpc"] != "2.0")
        {
            return CreateError(JSONRPCProtocol::INVALID_REQUEST,
                               "Missing or invalid 'jsonrpc' field");
        }

        if (!request.contains("method"))
        {
            return CreateError(JSONRPCProtocol::INVALID_REQUEST,
                               "Missing 'method' field");
        }

        // Extract request fields
        std::string method = request["method"];
        nlohmann::json params = request.value("params", nlohmann::json::object());
        int id = request.value("id", 0);

        // Call the method
        nlohmann::json result = CallMethod(method, params, client_is_loopback);

        // Check if result is an error
        if (result.contains("error"))
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
    catch (const std::exception &e)
    {
        return CreateError(JSONRPCProtocol::INTERNAL_ERROR,
                           std::string("Internal error: ") + e.what());
    }
}

nlohmann::json JSONRPCHandler::HandleBatchRequest(const nlohmann::json &requests,
                                                  bool client_is_loopback)
{
    nlohmann::json responses = nlohmann::json::array();

    if (!requests.is_array())
    {
        return CreateError(JSONRPCProtocol::INVALID_REQUEST,
                           "Batch request must be an array");
    }

    for (const auto &request : requests)
    {
        responses.push_back(HandleRequest(request, client_is_loopback));
    }

    return responses;
}

bool JSONRPCHandler::TakeShutdownRequested()
{
    bool requested = shutdown_requested;
    shutdown_requested = false;
    return requested;
}

/*---------------------------------------------------------*\
| Method Dispatcher                                         |
\*---------------------------------------------------------*/
nlohmann::json JSONRPCHandler::CallMethod(const std::string &method,
                                          const nlohmann::json &params,
                                          bool client_is_loopback)
{
    // Device Management
    if (method == JSONRPCProtocol::Methods::GET_CONTROLLERS)
    {
        return GetControllers(params);
    }
    else if (method == JSONRPCProtocol::Methods::GET_CONTROLLER_COUNT)
    {
        return GetControllerCount(params);
    }
    else if (method == JSONRPCProtocol::Methods::GET_CONTROLLER_DATA)
    {
        return GetControllerData(params);
    }
    else if (method == JSONRPCProtocol::Methods::GET_CONTROLLER_INFO)
    {
        return GetControllerInfo(params);
    }
    else if (method == JSONRPCProtocol::Methods::RESCAN_DEVICES)
    {
        return RescanDevices(params);
    }
    // Color Control
    else if (method == JSONRPCProtocol::Methods::SET_LED_COLOR)
    {
        return SetLEDColor(params);
    }
    else if (method == JSONRPCProtocol::Methods::SET_ZONE_COLOR)
    {
        return SetZoneColor(params);
    }
    else if (method == JSONRPCProtocol::Methods::SET_ZONE_MULTIPLE_LED)
    {
        return SetZoneMultipleLed(params);
    }
    else if (method == JSONRPCProtocol::Methods::SET_MULTIPLE_ZONE_MULTIPLE_LED)
    {
        return SetMultipleZoneMultipleLed(params);
    }
    else if (method == JSONRPCProtocol::Methods::SET_ALL_COLORS)
    {
        return SetAllColors(params);
    }
    else if (method == JSONRPCProtocol::Methods::SET_MULTIPLE_COLORS)
    {
        return SetMultipleColors(params);
    }
    else if (method == JSONRPCProtocol::Methods::SET_KEY_COLOR)
    {
        return SetKeyColor(params);
    }
    // Mode Control
    else if (method == JSONRPCProtocol::Methods::SET_MODE)
    {
        return SetMode(params);
    }
    else if (method == JSONRPCProtocol::Methods::SET_CUSTOM_MODE)
    {
        return SetCustomMode(params);
    }
    else if (method == JSONRPCProtocol::Methods::UPDATE_MODE)
    {
        return UpdateMode(params);
    }
    // Zone Management
    else if (method == JSONRPCProtocol::Methods::GET_ZONES)
    {
        return GetZones(params);
    }
    else if (method == JSONRPCProtocol::Methods::RESIZE_ZONE)
    {
        return ResizeZone(params);
    }
    else if (method == JSONRPCProtocol::Methods::CLEAR_SEGMENTS)
    {
        return ClearSegments(params);
    }
    else if (method == JSONRPCProtocol::Methods::ADD_SEGMENT)
    {
        return AddSegment(params);
    }
    // Profile Management
    else if (method == JSONRPCProtocol::Methods::GET_PROFILES)
    {
        return GetProfiles(params);
    }
    else if (method == JSONRPCProtocol::Methods::SAVE_PROFILE)
    {
        return SaveProfile(params);
    }
    else if (method == JSONRPCProtocol::Methods::LOAD_PROFILE)
    {
        return LoadProfile(params);
    }
    else if (method == JSONRPCProtocol::Methods::DELETE_PROFILE)
    {
        return DeleteProfile(params);
    }
    // Server Information
    else if (method == JSONRPCProtocol::Methods::GET_PROTOCOL_VERSION)
    {
        return GetProtocolVersion(params);
    }
    else if (method == JSONRPCProtocol::Methods::GET_SERVER_INFO)
    {
        return GetServerInfo(params);
    }
    else if (method == JSONRPCProtocol::Methods::GET_CLIENTS)
    {
        return GetClients(params);
    }
    else if (method == JSONRPCProtocol::Methods::SHUTDOWN)
    {
        return ShutdownServer(params, client_is_loopback);
    }
    // Plugin Management
    else if (method == JSONRPCProtocol::Methods::GET_PLUGINS)
    {
        return GetPlugins(params);
    }
    else if (method == JSONRPCProtocol::Methods::CALL_PLUGIN)
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
nlohmann::json JSONRPCHandler::GetControllers(const nlohmann::json &params)
{
    auto lock = LockControllerList();

    nlohmann::json result;
    nlohmann::json controllers_array = nlohmann::json::array();

    for (unsigned int i = 0; i < controllers.size(); i++)
    {
        controllers_array.push_back(ControllerToJSON(controllers[i]));
    }

    result["controllers"] = controllers_array;
    return result;
}

nlohmann::json JSONRPCHandler::GetControllerCount(const nlohmann::json &params)
{
    auto lock = LockControllerList();

    nlohmann::json result;
    result["count"] = controllers.size();
    return result;
}

nlohmann::json JSONRPCHandler::GetControllerData(const nlohmann::json &params)
{
    if (!params.contains("deviceIndex"))
    {
        return CreateError(JSONRPCProtocol::INVALID_PARAMS,
                           "Missing 'deviceIndex' parameter");
    }

    auto lock = LockControllerList();

    unsigned int device_idx = params["deviceIndex"];

    if (!ValidateDeviceIndex(device_idx))
    {
        return CreateError(JSONRPCProtocol::ERR_DEVICE_INDEX_OUT_OF_RANGE,
                           "Device index out of range");
    }

    nlohmann::json result;
    result["controller"] = ControllerToJSON(controllers[device_idx]);
    return result;
}

nlohmann::json JSONRPCHandler::GetControllerInfo(const nlohmann::json &params)
{
    if (!params.contains("deviceIndex"))
    {
        return CreateError(JSONRPCProtocol::INVALID_PARAMS,
                           "Missing 'deviceIndex' parameter");
    }

    auto lock = LockControllerList();

    unsigned int device_idx = params["deviceIndex"];

    if (!ValidateDeviceIndex(device_idx))
    {
        return CreateError(JSONRPCProtocol::ERR_DEVICE_INDEX_OUT_OF_RANGE,
                           "Device index out of range");
    }

    RGBController *controller = controllers[device_idx];

    nlohmann::json result;
    result["name"] = controller->name;
    result["vendor"] = controller->vendor;
    result["description"] = controller->description;
    result["type"] = controller->type;
    result["location"] = controller->location;
    result["serial"] = controller->serial;

    return result;
}

nlohmann::json JSONRPCHandler::RescanDevices(const nlohmann::json &params)
{
    std::lock_guard<std::mutex> lock(rescan_mutex);

    // Check if a rescan is already in progress
    if (rescan_in_progress)
    {
        // Check if the previous rescan has completed
        if (rescan_future.valid() &&
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

    if (resource_manager)
    {
        // Mark rescan as in progress
        rescan_in_progress = true;

        // Launch async rescan in background thread
        rescan_future = std::async(std::launch::async, [this]()
                                   {
            resource_manager->RescanDevices();

            // Reset flag when done
            std::lock_guard<std::mutex> lock(rescan_mutex);
            rescan_in_progress = false; });

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
nlohmann::json JSONRPCHandler::SetLEDColor(const nlohmann::json &params)
{
    if (!params.contains("deviceIndex") || !params.contains("ledIndex") || !params.contains("color"))
    {
        return CreateError(JSONRPCProtocol::INVALID_PARAMS,
                           "Missing required parameters: deviceIndex, ledIndex, color");
    }

    unsigned int led_idx = params["ledIndex"];
    RGBColor color = ParseColor(params["color"]);

    auto lock = LockControllerList();

    unsigned int device_idx = params["deviceIndex"];

    if (!ValidateDeviceIndex(device_idx))
    {
        return CreateError(JSONRPCProtocol::ERR_DEVICE_INDEX_OUT_OF_RANGE,
                           "Device index out of range");
    }

    RGBController *controller = controllers[device_idx];

    if (led_idx >= controller->leds.size())
    {
        return CreateError(JSONRPCProtocol::ERR_LED_INDEX_OUT_OF_RANGE,
                           "LED index out of range");
    }

    controller->SetLED(led_idx, color);
    controller->UpdateLEDs();

    nlohmann::json result;
    result["success"] = true;
    return result;
}

nlohmann::json JSONRPCHandler::SetZoneColor(const nlohmann::json &params)
{
    if (!params.contains("deviceIndex") || !params.contains("zoneIndex") || !params.contains("color"))
    {
        return CreateError(JSONRPCProtocol::INVALID_PARAMS,
                           "Missing required parameters: deviceIndex, zoneIndex, color");
    }

    unsigned int zone_idx = params["zoneIndex"];
    RGBColor color = ParseColor(params["color"]);

    auto lock = LockControllerList();

    unsigned int device_idx = params["deviceIndex"];

    if (!ValidateDeviceIndex(device_idx))
    {
        return CreateError(JSONRPCProtocol::ERR_DEVICE_INDEX_OUT_OF_RANGE,
                           "Device index out of range");
    }

    RGBController *controller = controllers[device_idx];

    if (zone_idx >= controller->zones.size())
    {
        return CreateError(JSONRPCProtocol::ERR_ZONE_INDEX_OUT_OF_RANGE,
                           "Zone index out of range");
    }

    controller->SetAllZoneLEDs(zone_idx, color);
    controller->UpdateLEDs();

    nlohmann::json result;
    result["success"] = true;
    return result;
}

nlohmann::json JSONRPCHandler::SetZoneMultipleLed(const nlohmann::json &params)
{
    if (!params.contains("deviceIndex") || !params.contains("zoneIndex") || !params.contains("color"))
    {
        return CreateError(JSONRPCProtocol::INVALID_PARAMS,
                           "Missing required parameters: deviceIndex, zoneIndex, color");
    }

    const nlohmann::json &colors = params["color"];
    if (params.contains("Force") && !params["Force"].is_boolean())
    {
        return CreateError(JSONRPCProtocol::INVALID_PARAMS,
                           "'Force' must be a boolean");
    }
    const bool force = params.value("Force", false);

    if (!colors.is_array() || colors.empty())
    {
        return CreateError(JSONRPCProtocol::INVALID_PARAMS,
                           "'color' must be a non-empty array of RGB arrays");
    }

    std::vector<RGBColor> parsed_colors;
    parsed_colors.reserve(colors.size());
    for (const nlohmann::json &rgb : colors)
    {
        if (!rgb.is_array() || rgb.size() != 3
            || !rgb[0].is_number_unsigned() || !rgb[1].is_number_unsigned() || !rgb[2].is_number_unsigned()
            || rgb[0].get<unsigned int>() > 255 || rgb[1].get<unsigned int>() > 255 || rgb[2].get<unsigned int>() > 255)
        {
            return CreateError(JSONRPCProtocol::INVALID_PARAMS,
                               "Each color must be an RGB array containing three integers from 0 to 255");
        }

        parsed_colors.push_back(ToRGBColor(rgb[0].get<unsigned int>(),
                                           rgb[1].get<unsigned int>(),
                                           rgb[2].get<unsigned int>()));
    }

    auto lock = LockControllerList();
    unsigned int device_idx = params["deviceIndex"];
    unsigned int zone_idx = params["zoneIndex"];

    if (!ValidateDeviceIndex(device_idx))
    {
        return CreateError(JSONRPCProtocol::ERR_DEVICE_INDEX_OUT_OF_RANGE,
                           "Device index out of range");
    }

    RGBController *controller = controllers[device_idx];
    if (zone_idx >= controller->zones.size())
    {
        return CreateError(JSONRPCProtocol::ERR_ZONE_INDEX_OUT_OF_RANGE,
                           "Zone index out of range");
    }

    zone &target_zone = controller->zones[zone_idx];
    if (parsed_colors.size() != target_zone.leds_count)
    {
        if (force)
        {
            target_zone.leds_count = static_cast<unsigned int>(parsed_colors.size());
            target_zone.flags &= ~ZONE_FLAG_RESIZE_EFFECTS_ONLY;

            std::size_t total_led_count = 0;
            for (std::size_t controller_zone_idx = 0;
                 controller_zone_idx < controller->zones.size(); ++controller_zone_idx)
            {
                total_led_count += controller->GetLEDsInZone(static_cast<unsigned int>(controller_zone_idx));
            }
            controller->leds.resize(total_led_count);
            controller->SetupColors();
        }
        else if (parsed_colors.size() < target_zone.leds_min || parsed_colors.size() > target_zone.leds_max)
        {
            return CreateError(JSONRPCProtocol::INVALID_PARAMS,
                               "Color count is outside the zone's supported LED count range");
        }
        else
        {
            controller->ResizeZone(zone_idx, static_cast<int>(parsed_colors.size()));
            if (controller->zones[zone_idx].leds_count != parsed_colors.size())
            {
                return CreateError(JSONRPCProtocol::ERR_RESIZE_NOT_SUPPORTED,
                                   "Zone does not support the requested LED count");
            }
        }
    }

    for (std::size_t led_idx = 0; led_idx < parsed_colors.size(); ++led_idx)
    {
        controller->zones[zone_idx].colors[led_idx] = parsed_colors[led_idx];
    }
    controller->UpdateLEDs();

    nlohmann::json result;
    result["success"] = true;
    result["ledCount"] = parsed_colors.size();
    result["forced"] = force;
    return result;
}

nlohmann::json JSONRPCHandler::SetMultipleZoneMultipleLed(const nlohmann::json &params)
{
    if (!params.contains("deviceIndex") || !params.contains("color"))
    {
        return CreateError(JSONRPCProtocol::INVALID_PARAMS,
                           "Missing required parameters: deviceIndex, color");
    }

    if (params.contains("Force") && !params["Force"].is_boolean())
    {
        return CreateError(JSONRPCProtocol::INVALID_PARAMS,
                           "'Force' must be a boolean");
    }
    const bool force = params.value("Force", false);
    const nlohmann::json &zone_colors = params["color"];

    if (!zone_colors.is_array() || zone_colors.empty())
    {
        return CreateError(JSONRPCProtocol::INVALID_PARAMS,
                           "'color' must be a non-empty array of zone color arrays");
    }

    std::vector<std::vector<RGBColor> > parsed_zone_colors;
    parsed_zone_colors.reserve(zone_colors.size());
    for (const nlohmann::json &colors : zone_colors)
    {
        if (!colors.is_array() || (colors.empty() && !force))
        {
            return CreateError(JSONRPCProtocol::INVALID_PARAMS,
                               force
                                   ? "Each zone entry must be an array"
                                   : "Each zone must contain a non-empty array of RGB arrays");
        }

        std::vector<RGBColor> parsed_colors;
        parsed_colors.reserve(colors.size());
        for (const nlohmann::json &rgb : colors)
        {
            if (!rgb.is_array() || rgb.size() != 3
                || !rgb[0].is_number_unsigned() || !rgb[1].is_number_unsigned() || !rgb[2].is_number_unsigned()
                || rgb[0].get<unsigned int>() > 255 || rgb[1].get<unsigned int>() > 255 || rgb[2].get<unsigned int>() > 255)
            {
                return CreateError(JSONRPCProtocol::INVALID_PARAMS,
                                   "Each color must be an RGB array containing three integers from 0 to 255");
            }

            parsed_colors.push_back(ToRGBColor(rgb[0].get<unsigned int>(),
                                               rgb[1].get<unsigned int>(),
                                               rgb[2].get<unsigned int>()));
        }
        parsed_zone_colors.push_back(parsed_colors);
    }

    auto lock = LockControllerList();
    unsigned int device_idx = params["deviceIndex"];
    if (!ValidateDeviceIndex(device_idx))
    {
        return CreateError(JSONRPCProtocol::ERR_DEVICE_INDEX_OUT_OF_RANGE,
                           "Device index out of range");
    }

    RGBController *controller = controllers[device_idx];
    if (parsed_zone_colors.size() > controller->zones.size()
        || (!force && parsed_zone_colors.size() != controller->zones.size()))
    {
        return CreateError(JSONRPCProtocol::INVALID_PARAMS,
                           force
                               ? "The number of zone color arrays cannot exceed the device zone count"
                               : "The number of zone color arrays must match the device zone count");
    }

    if (!force)
    {
        for (std::size_t zone_idx = 0; zone_idx < parsed_zone_colors.size(); ++zone_idx)
        {
            const std::size_t led_count = parsed_zone_colors[zone_idx].size();
            const zone &target_zone = controller->zones[zone_idx];
            if (led_count != target_zone.leds_count
                && (led_count < target_zone.leds_min || led_count > target_zone.leds_max))
            {
                return CreateError(JSONRPCProtocol::INVALID_PARAMS,
                                   "Color count is outside a zone's supported LED count range");
            }
        }

        for (std::size_t zone_idx = 0; zone_idx < parsed_zone_colors.size(); ++zone_idx)
        {
            const std::size_t led_count = parsed_zone_colors[zone_idx].size();
            if (led_count != controller->zones[zone_idx].leds_count)
            {
                controller->ResizeZone(static_cast<unsigned int>(zone_idx), static_cast<int>(led_count));
                if (controller->zones[zone_idx].leds_count != led_count)
                {
                    return CreateError(JSONRPCProtocol::ERR_RESIZE_NOT_SUPPORTED,
                                       "Zone does not support the requested LED count");
                }
            }
        }
    }
    else
    {
        std::vector<std::vector<RGBColor> > original_zone_colors;
        original_zone_colors.reserve(controller->zones.size());
        for (std::size_t zone_idx = 0; zone_idx < controller->zones.size(); ++zone_idx)
        {
            const zone &original_zone = controller->zones[zone_idx];
            std::vector<RGBColor> saved_colors;
            saved_colors.reserve(original_zone.leds_count);
            for (std::size_t led_idx = 0; led_idx < original_zone.leds_count; ++led_idx)
            {
                saved_colors.push_back(original_zone.colors[led_idx]);
            }
            original_zone_colors.push_back(saved_colors);
        }

        for (std::size_t zone_idx = 0; zone_idx < parsed_zone_colors.size(); ++zone_idx)
        {
            if (parsed_zone_colors[zone_idx].empty())
            {
                continue;
            }
            controller->zones[zone_idx].leds_count = static_cast<unsigned int>(parsed_zone_colors[zone_idx].size());
            controller->zones[zone_idx].flags &= ~ZONE_FLAG_RESIZE_EFFECTS_ONLY;
        }

        std::size_t total_led_count = 0;
        for (std::size_t zone_idx = 0; zone_idx < controller->zones.size(); ++zone_idx)
        {
            total_led_count += controller->GetLEDsInZone(static_cast<unsigned int>(zone_idx));
        }
        controller->leds.resize(total_led_count);
        controller->SetupColors();

        for (std::size_t zone_idx = 0; zone_idx < controller->zones.size(); ++zone_idx)
        {
            if (zone_idx < parsed_zone_colors.size() && !parsed_zone_colors[zone_idx].empty())
            {
                continue;
            }

            zone &unchanged_zone = controller->zones[zone_idx];
            for (std::size_t led_idx = 0; led_idx < original_zone_colors[zone_idx].size(); ++led_idx)
            {
                unchanged_zone.colors[led_idx] = original_zone_colors[zone_idx][led_idx];
            }
        }
    }

    nlohmann::json led_counts = nlohmann::json::array();
    for (std::size_t zone_idx = 0; zone_idx < parsed_zone_colors.size(); ++zone_idx)
    {
        if (parsed_zone_colors[zone_idx].empty())
        {
            led_counts.push_back(nullptr);
            continue;
        }
        for (std::size_t led_idx = 0; led_idx < parsed_zone_colors[zone_idx].size(); ++led_idx)
        {
            controller->zones[zone_idx].colors[led_idx] = parsed_zone_colors[zone_idx][led_idx];
        }
        led_counts.push_back(parsed_zone_colors[zone_idx].size());
    }
    controller->UpdateLEDs();

    nlohmann::json result;
    result["success"] = true;
    result["zoneCount"] = parsed_zone_colors.size();
    result["ledCounts"] = led_counts;
    result["forced"] = force;
    return result;
}

nlohmann::json JSONRPCHandler::SetAllColors(const nlohmann::json &params)
{
    if (!params.contains("deviceIndex") || !params.contains("color"))
    {
        return CreateError(JSONRPCProtocol::INVALID_PARAMS,
                           "Missing required parameters: deviceIndex, color");
    }

    RGBColor color = ParseColor(params["color"]);

    auto lock = LockControllerList();

    unsigned int device_idx = params["deviceIndex"];

    if (!ValidateDeviceIndex(device_idx))
    {
        return CreateError(JSONRPCProtocol::ERR_DEVICE_INDEX_OUT_OF_RANGE,
                           "Device index out of range");
    }

    RGBController *controller = controllers[device_idx];

    controller->SetAllLEDs(color);
    controller->UpdateLEDs();

    nlohmann::json result;
    result["success"] = true;
    return result;
}

nlohmann::json JSONRPCHandler::SetMultipleColors(const nlohmann::json &params)
{
    if (!params.contains("deviceIndex") || !params.contains("colors"))
    {
        return CreateError(JSONRPCProtocol::INVALID_PARAMS,
                           "Missing required parameters: deviceIndex, colors");
    }

    const auto &colors = params["colors"];

    if (!colors.is_array())
    {
        return CreateError(JSONRPCProtocol::INVALID_PARAMS,
                           "'colors' must be an array");
    }

    auto lock = LockControllerList();

    unsigned int device_idx = params["deviceIndex"];

    if (!ValidateDeviceIndex(device_idx))
    {
        return CreateError(JSONRPCProtocol::ERR_DEVICE_INDEX_OUT_OF_RANGE,
                           "Device index out of range");
    }

    RGBController *controller = controllers[device_idx];

    for (const auto &color_item : colors)
    {
        if (!color_item.contains("ledIndex") || !color_item.contains("color"))
        {
            continue;
        }

        unsigned int led_idx = color_item["ledIndex"];
        if (led_idx < controller->leds.size())
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
| Set a single key color by key name                        |
|                                                           |
|   Looks up the first LED whose name matches "key" and     |
|   applies the color.  Useful for per-key keyboards where  |
|   the caller does not know the device-specific LED index. |
|   Comparison is exact and case-sensitive, matching the    |
|   names produced by KeyboardLayoutManager (e.g. "A",      |
|   "Escape", "Space").                                     |
\*---------------------------------------------------------*/
nlohmann::json JSONRPCHandler::SetKeyColor(const nlohmann::json &params)
{
    if (!params.contains("deviceIndex") || !params.contains("key") || !params.contains("color"))
    {
        return CreateError(JSONRPCProtocol::INVALID_PARAMS,
                           "Missing required parameters: deviceIndex, key, color");
    }

    std::string key_name = params["key"];
    RGBColor color = ParseColor(params["color"]);

    auto lock = LockControllerList();

    unsigned int device_idx = params["deviceIndex"];

    if (!ValidateDeviceIndex(device_idx))
    {
        return CreateError(JSONRPCProtocol::ERR_DEVICE_INDEX_OUT_OF_RANGE,
                           "Device index out of range");
    }

    RGBController *controller = controllers[device_idx];

    bool found = false;
    for (unsigned int led_idx = 0; led_idx < controller->leds.size(); led_idx++)
    {
        if (controller->leds[led_idx].name == key_name)
        {
            controller->SetLED(led_idx, color);
            found = true;
            break;
        }
    }

    if (!found)
    {
        return CreateError(JSONRPCProtocol::ERR_KEY_NAME_NOT_FOUND,
                           "Key name not found: " + key_name);
    }

    controller->UpdateLEDs();

    nlohmann::json result;
    result["success"] = true;
    return result;
}

/*---------------------------------------------------------*\
| Mode Control Methods                                      |
\*---------------------------------------------------------*/
nlohmann::json JSONRPCHandler::SetMode(const nlohmann::json &params)
{
    if (!params.contains("deviceIndex") || !params.contains("modeIndex"))
    {
        return CreateError(JSONRPCProtocol::INVALID_PARAMS,
                           "Missing required parameters: deviceIndex, modeIndex");
    }

    unsigned int mode_idx = params["modeIndex"];

    auto lock = LockControllerList();

    unsigned int device_idx = params["deviceIndex"];

    if (!ValidateDeviceIndex(device_idx))
    {
        return CreateError(JSONRPCProtocol::ERR_DEVICE_INDEX_OUT_OF_RANGE,
                           "Device index out of range");
    }

    RGBController *controller = controllers[device_idx];

    if (mode_idx >= controller->modes.size())
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

nlohmann::json JSONRPCHandler::SetCustomMode(const nlohmann::json &params)
{
    if (!params.contains("deviceIndex"))
    {
        return CreateError(JSONRPCProtocol::INVALID_PARAMS,
                           "Missing 'deviceIndex' parameter");
    }

    auto lock = LockControllerList();

    unsigned int device_idx = params["deviceIndex"];

    if (!ValidateDeviceIndex(device_idx))
    {
        return CreateError(JSONRPCProtocol::ERR_DEVICE_INDEX_OUT_OF_RANGE,
                           "Device index out of range");
    }

    RGBController *controller = controllers[device_idx];
    controller->SetCustomMode();
    controller->UpdateLEDs();

    nlohmann::json result;
    result["success"] = true;
    return result;
}

nlohmann::json JSONRPCHandler::UpdateMode(const nlohmann::json &params)
{
    if (!params.contains("deviceIndex") || !params.contains("mode"))
    {
        return CreateError(JSONRPCProtocol::INVALID_PARAMS,
                           "Missing required parameters: deviceIndex, mode");
    }

    const auto &mode_obj = params["mode"];

    if (!mode_obj.contains("index"))
    {
        return CreateError(JSONRPCProtocol::INVALID_PARAMS,
                           "Mode object missing 'index' field");
    }

    unsigned int mode_idx = mode_obj["index"];

    auto lock = LockControllerList();

    unsigned int device_idx = params["deviceIndex"];

    if (!ValidateDeviceIndex(device_idx))
    {
        return CreateError(JSONRPCProtocol::ERR_DEVICE_INDEX_OUT_OF_RANGE,
                           "Device index out of range");
    }

    RGBController *controller = controllers[device_idx];

    if (mode_idx >= controller->modes.size())
    {
        return CreateError(JSONRPCProtocol::ERR_MODE_INDEX_OUT_OF_RANGE,
                           "Mode index out of range");
    }

    // Update mode parameters if provided
    if (mode_obj.contains("speed") || mode_obj.contains("direction") ||
        mode_obj.contains("colors") || mode_obj.contains("value"))
    {
        if (mode_obj.contains("speed"))
        {
            controller->modes[mode_idx].speed = mode_obj["speed"];
        }
        if (mode_obj.contains("direction"))
        {
            controller->modes[mode_idx].direction = mode_obj["direction"];
        }
        if (mode_obj.contains("colors"))
        {
            const auto &colors = mode_obj["colors"];
            if (colors.is_array())
            {
                controller->modes[mode_idx].colors.clear();
                for (const auto &color : colors)
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
nlohmann::json JSONRPCHandler::GetZones(const nlohmann::json &params)
{
    if (!params.contains("deviceIndex"))
    {
        return CreateError(JSONRPCProtocol::INVALID_PARAMS,
                           "Missing 'deviceIndex' parameter");
    }

    auto lock = LockControllerList();

    unsigned int device_idx = params["deviceIndex"];

    if (!ValidateDeviceIndex(device_idx))
    {
        return CreateError(JSONRPCProtocol::ERR_DEVICE_INDEX_OUT_OF_RANGE,
                           "Device index out of range");
    }

    nlohmann::json result;
    nlohmann::json zones_array = nlohmann::json::array();
    RGBController *controller = controllers[device_idx];

    for (unsigned int i = 0; i < controller->zones.size(); i++)
    {
        zones_array.push_back(ZoneToJSON(controller, i));
    }

    result["zones"] = zones_array;
    return result;
}

nlohmann::json JSONRPCHandler::ResizeZone(const nlohmann::json &params)
{
    if (!params.contains("deviceIndex") || !params.contains("zoneIndex") || !params.contains("newSize"))
    {
        return CreateError(JSONRPCProtocol::INVALID_PARAMS,
                           "Missing required parameters: deviceIndex, zoneIndex, newSize");
    }

    unsigned int zone_idx = params["zoneIndex"];
    int new_size = params["newSize"];

    auto lock = LockControllerList();

    unsigned int device_idx = params["deviceIndex"];

    if (!ValidateDeviceIndex(device_idx))
    {
        return CreateError(JSONRPCProtocol::ERR_DEVICE_INDEX_OUT_OF_RANGE,
                           "Device index out of range");
    }

    RGBController *controller = controllers[device_idx];

    if (zone_idx >= controller->zones.size())
    {
        return CreateError(JSONRPCProtocol::ERR_ZONE_INDEX_OUT_OF_RANGE,
                           "Zone index out of range");
    }

    // Check if zone is resizable by comparing leds_min and leds_max
    if (controller->zones[zone_idx].leds_min == controller->zones[zone_idx].leds_max)
    {
        return CreateError(JSONRPCProtocol::ERR_RESIZE_NOT_SUPPORTED,
                           "Zone is not resizeable");
    }

    controller->ResizeZone(zone_idx, new_size);

    nlohmann::json result;
    result["success"] = true;
    return result;
}

nlohmann::json JSONRPCHandler::ClearSegments(const nlohmann::json &params)
{
    if (!params.contains("deviceIndex") || !params.contains("zoneIndex"))
    {
        return CreateError(JSONRPCProtocol::INVALID_PARAMS,
                           "Missing required parameters: deviceIndex, zoneIndex");
    }

    unsigned int zone_idx = params["zoneIndex"];

    auto lock = LockControllerList();

    unsigned int device_idx = params["deviceIndex"];

    if (!ValidateDeviceIndex(device_idx))
    {
        return CreateError(JSONRPCProtocol::ERR_DEVICE_INDEX_OUT_OF_RANGE,
                           "Device index out of range");
    }

    RGBController *controller = controllers[device_idx];

    if (zone_idx >= controller->zones.size())
    {
        return CreateError(JSONRPCProtocol::ERR_ZONE_INDEX_OUT_OF_RANGE,
                           "Zone index out of range");
    }

    controller->ClearSegments(zone_idx);

    nlohmann::json result;
    result["success"] = true;
    return result;
}

nlohmann::json JSONRPCHandler::AddSegment(const nlohmann::json &params)
{
    if (!params.contains("deviceIndex") || !params.contains("zoneIndex") || !params.contains("segment"))
    {
        return CreateError(JSONRPCProtocol::INVALID_PARAMS,
                           "Missing required parameters: deviceIndex, zoneIndex, segment");
    }

    unsigned int zone_idx = params["zoneIndex"];
    const auto &segment_obj = params["segment"];

    unsigned int start_idx = segment_obj.value("start", 0);
    unsigned int leds_count = segment_obj.value("length", 0);
    std::string name = segment_obj.value("name", "");

    segment new_seg;
    new_seg.name = name;
    new_seg.start_idx = start_idx;
    new_seg.leds_count = leds_count;

    auto lock = LockControllerList();

    unsigned int device_idx = params["deviceIndex"];

    if (!ValidateDeviceIndex(device_idx))
    {
        return CreateError(JSONRPCProtocol::ERR_DEVICE_INDEX_OUT_OF_RANGE,
                           "Device index out of range");
    }

    RGBController *controller = controllers[device_idx];

    if (zone_idx >= controller->zones.size())
    {
        return CreateError(JSONRPCProtocol::ERR_ZONE_INDEX_OUT_OF_RANGE,
                           "Zone index out of range");
    }

    controller->AddSegment(zone_idx, new_seg);

    nlohmann::json result;
    result["success"] = true;
    return result;
}

/*---------------------------------------------------------*\
| Profile Management Methods                                |
\*---------------------------------------------------------*/
nlohmann::json JSONRPCHandler::GetProfiles(const nlohmann::json &params)
{
    nlohmann::json result;
    nlohmann::json profiles_array = nlohmann::json::array();

    if (profile_manager)
    {
        for (const auto &name : profile_manager->profile_list)
        {
            profiles_array.push_back(name);
        }
    }

    result["profiles"] = profiles_array;
    return result;
}

nlohmann::json JSONRPCHandler::SaveProfile(const nlohmann::json &params)
{
    if (!params.contains("profileName"))
    {
        return CreateError(JSONRPCProtocol::INVALID_PARAMS,
                           "Missing 'profileName' parameter");
    }

    std::string profile_name = params["profileName"];
    bool include_sizes = params.value("includeSizes", true);

    if (!profile_manager)
    {
        return CreateError(JSONRPCProtocol::ERR_PROFILE_SAVE_FAILED,
                           "Profile manager not initialized");
    }

    profile_manager->SaveProfile(profile_name, include_sizes);

    /*---------------------------------------------------------*\
    | Notify WebSocket clients that a profile was saved.        |
    \*---------------------------------------------------------*/
    if(resource_manager)
    {
        if(WebSocketServer * ws_server = resource_manager->GetWebSocketServer())
        {
            ws_server->ProfileSaved(profile_name);
        }
    }

    nlohmann::json result;
    result["success"] = true;
    return result;
}

nlohmann::json JSONRPCHandler::LoadProfile(const nlohmann::json &params)
{
    if (!params.contains("profileName"))
    {
        return CreateError(JSONRPCProtocol::INVALID_PARAMS,
                           "Missing 'profileName' parameter");
    }

    std::string profile_name = params["profileName"];

    if (!profile_manager)
    {
        return CreateError(JSONRPCProtocol::ERR_PROFILE_NOT_FOUND,
                           "Profile manager not initialized");
    }

    std::vector<std::string> profile_names = profile_manager->profile_list;

    if (std::find(profile_names.begin(), profile_names.end(), profile_name) == profile_names.end())
    {
        return CreateError(JSONRPCProtocol::ERR_PROFILE_NOT_FOUND,
                           "Profile not found: " + profile_name);
    }

    profile_manager->LoadProfile(profile_name);
    profile_manager->LoadSizeFromProfile(profile_name);

    /*---------------------------------------------------------*\
    | Notify WebSocket clients that a profile was loaded.       |
    \*---------------------------------------------------------*/
    if(resource_manager)
    {
        if(WebSocketServer * ws_server = resource_manager->GetWebSocketServer())
        {
            ws_server->ProfileLoaded(profile_name);
        }
    }

    nlohmann::json result;
    result["success"] = true;
    return result;
}

nlohmann::json JSONRPCHandler::DeleteProfile(const nlohmann::json &params)
{
    if (!params.contains("profileName"))
    {
        return CreateError(JSONRPCProtocol::INVALID_PARAMS,
                           "Missing 'profileName' parameter");
    }

    std::string profile_name = params["profileName"];

    if (!profile_manager)
    {
        return CreateError(JSONRPCProtocol::ERR_PROFILE_NOT_FOUND,
                           "Profile manager not initialized");
    }

    std::vector<std::string> profile_names = profile_manager->profile_list;

    if (std::find(profile_names.begin(), profile_names.end(), profile_name) == profile_names.end())
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
nlohmann::json JSONRPCHandler::GetProtocolVersion(const nlohmann::json &params)
{
    nlohmann::json result;
    result["protocolVersion"] = JSONRPCProtocol::PROTOCOL_VERSION;
    result["jsonrpcVersion"] = JSONRPCProtocol::JSON_RPC_VERSION;
    return result;
}

nlohmann::json JSONRPCHandler::GetServerInfo(const nlohmann::json &params)
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

nlohmann::json JSONRPCHandler::GetClients(const nlohmann::json &params)
{
    nlohmann::json result;
    result["clients"] = nlohmann::json::array();
    // This will be implemented by WebSocketServer
    return result;
}

nlohmann::json JSONRPCHandler::ShutdownServer(const nlohmann::json &params,
                                              bool client_is_loopback)
{
    if(!client_is_loopback)
    {
        return CreateError(JSONRPCProtocol::ERR_OPERATION_NOT_PERMITTED,
                           "Shutdown is only permitted from loopback clients");
    }

    shutdown_requested = true;

    nlohmann::json result;
    result["success"] = true;
    result["message"] = "Shutdown scheduled";
    return result;
}

/*---------------------------------------------------------*\
| Plugin Methods                                            |
\*---------------------------------------------------------*/
nlohmann::json JSONRPCHandler::GetPlugins(const nlohmann::json &params)
{
    nlohmann::json result;
    result["plugins"] = nlohmann::json::array();
    // Plugin support will be added later
    return result;
}

nlohmann::json JSONRPCHandler::CallPlugin(const nlohmann::json &params)
{
    if (!params.contains("pluginName") || !params.contains("method") || !params.contains("params"))
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
nlohmann::json JSONRPCHandler::CreateError(int code, const std::string &message,
                                           const nlohmann::json &data)
{
    nlohmann::json error;
    error["code"] = code;
    error["message"] = message;

    if (!data.is_null())
    {
        error["data"] = data;
    }

    nlohmann::json result;
    result["error"] = error;
    return result;
}

nlohmann::json JSONRPCHandler::CreateResult(const nlohmann::json &result_data, int id)
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

std::unique_lock<std::mutex> JSONRPCHandler::LockControllerList()
{
    if (resource_manager)
    {
        return std::unique_lock<std::mutex>(resource_manager->GetDeviceListChangeMutex());
    }
    // No resource manager (defensive): return a non-owning, unlocked guard.
    return std::unique_lock<std::mutex>();
}

bool JSONRPCHandler::ValidateZoneIndex(unsigned int device_idx, unsigned int zone_idx)
{
    if (!ValidateDeviceIndex(device_idx))
    {
        return false;
    }

    return zone_idx < controllers[device_idx]->zones.size();
}

RGBColor JSONRPCHandler::ParseColor(const nlohmann::json &color_obj)
{
    RGBColor color = 0;

    if (color_obj.contains("red"))
    {
        unsigned int red = color_obj["red"];
        unsigned int green = color_obj.value("green", 0);
        unsigned int blue = color_obj.value("blue", 0);

        color = ToRGBColor(red, green, blue);
    }
    else if (color_obj.is_string())
    {
        std::string color_str = color_obj;
        if (color_str.size() == 7 && color_str[0] == '#')
        {
            std::stringstream ss;
            ss << std::hex << color_str.substr(1);
            unsigned int color_val;
            ss >> color_val;
            color = color_val;
        }
    }
    else if (color_obj.is_number())
    {
        color = color_obj;
    }

    return color;
}

nlohmann::json JSONRPCHandler::ControllerToJSON(RGBController *controller)
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

    // Add device state
    json_obj["activeMode"] = controller->active_mode;
    // Add current LED colors
    nlohmann::json colors_array = nlohmann::json::array();
    for (unsigned int i = 0; i < controller->colors.size(); i++)
    {
        RGBColor color = controller->colors[i];
        nlohmann::json color_obj;
        color_obj["red"] = RGBGetRValue(color);
        color_obj["green"] = RGBGetGValue(color);
        color_obj["blue"] = RGBGetBValue(color);
        colors_array.push_back(color_obj);
    }
    json_obj["colors"] = colors_array;

    // Add LEDs
    nlohmann::json leds_array = nlohmann::json::array();
    for (unsigned int i = 0; i < controller->leds.size(); i++)
    {
        leds_array.push_back(LEDToJSON(controller, i));
    }
    json_obj["leds"] = leds_array;

    // Add modes
    nlohmann::json modes_array = nlohmann::json::array();
    for (unsigned int i = 0; i < controller->modes.size(); i++)
    {
        modes_array.push_back(ModeToJSON(controller, i));
    }
    json_obj["modes"] = modes_array;
    json_obj["activeModeName"] = modes_array[controller->active_mode]["name"];

    return json_obj;
}

nlohmann::json JSONRPCHandler::ControllerToScanCompleteJSON(RGBController *controller)
{
    return ControllerToJSON(controller);
}

nlohmann::json JSONRPCHandler::ZoneToJSON(RGBController *controller, int zone_idx)
{
    nlohmann::json json_obj;
    const zone &z = controller->zones[zone_idx];

    json_obj["name"] = z.name;
    json_obj["type"] = z.type;
    json_obj["ledsMin"] = z.leds_min;
    json_obj["ledsMax"] = z.leds_max;
    json_obj["ledsCount"] = z.leds_count;

    // Convert matrix_map to JSON if it exists
    if (z.matrix_map)
    {
        nlohmann::json matrix_obj;
        matrix_obj["height"] = z.matrix_map->height;
        matrix_obj["width"] = z.matrix_map->width;

        nlohmann::json map_array = nlohmann::json::array();
        if (z.matrix_map->map)
        {
            for (unsigned int i = 0; i < (z.matrix_map->height * z.matrix_map->width); i++)
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
    for (unsigned int i = 0; i < z.segments.size(); i++)
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

nlohmann::json JSONRPCHandler::ModeToJSON(RGBController *controller, int mode_idx)
{
    nlohmann::json json_obj;
    const mode &m = controller->modes[mode_idx];

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
    for (unsigned int i = 0; i < m.colors.size(); i++)
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

nlohmann::json JSONRPCHandler::LEDToScanCompleteJSON(RGBController *controller, unsigned int led_idx,
                                                     unsigned int x, unsigned int y)
{
    nlohmann::json json_obj;
    const led &l = controller->leds[led_idx];

    json_obj["name"]            = l.name;
    json_obj["position"]        = nlohmann::json::array({x, y});
    json_obj["protocolAddress"] = led_idx;

    return json_obj;
}

nlohmann::json JSONRPCHandler::MatrixLEDsToScanCompleteJSON(RGBController *controller, const zone& matrix_zone)
{
    nlohmann::json leds_array = nlohmann::json::array();
    const matrix_map_type *matrix_map = matrix_zone.matrix_map;

    for (unsigned int y = 0; y < matrix_map->height; y++)
    {
        for (unsigned int x = 0; x < matrix_map->width; x++)
        {
            unsigned int zone_led_idx = matrix_map->map[(y * matrix_map->width) + x];

            if (zone_led_idx == 0xFFFFFFFF || zone_led_idx >= matrix_zone.leds_count)
            {
                continue;
            }

            unsigned int led_idx = matrix_zone.start_idx + zone_led_idx;
            if (led_idx >= controller->leds.size())
            {
                continue;
            }

            leds_array.push_back(LEDToScanCompleteJSON(controller, led_idx, x, y));
        }
    }

    return leds_array;
}

nlohmann::json JSONRPCHandler::LEDToJSON(RGBController *controller, int led_idx)
{
    nlohmann::json json_obj;
    const led &l = controller->leds[led_idx];

    json_obj["name"]            = l.name;
    json_obj["protocolAddress"] = led_idx;

    for(const zone& z : controller->zones)
    {
        if(z.type != ZONE_TYPE_MATRIX || z.matrix_map == NULL || z.matrix_map->map == NULL)
        {
            continue;
        }

        if((unsigned int)led_idx < z.start_idx || (unsigned int)led_idx >= (z.start_idx + z.leds_count))
        {
            continue;
        }

        unsigned int zone_led_idx = led_idx - z.start_idx;

        for(unsigned int y = 0; y < z.matrix_map->height; y++)
        {
            for(unsigned int x = 0; x < z.matrix_map->width; x++)
            {
                if(z.matrix_map->map[(y * z.matrix_map->width) + x] == zone_led_idx)
                {
                    json_obj["position"] = nlohmann::json::array({x, y});
                    return json_obj;
                }
            }
        }
    }

    return json_obj;
}
