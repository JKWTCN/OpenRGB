/*---------------------------------------------------------*\
| JSONRPCProtocol.h                                         |
|                                                           |
|   JSON-RPC 2.0 Protocol Constants and Error Codes        |
|                                                           |
|   This file is part of the OpenRGB project                |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#pragma once

#include <string>

namespace JSONRPCProtocol
{

/*---------------------------------------------------------*\
| Protocol Version                                          |
\*---------------------------------------------------------*/
const char* const PROTOCOL_VERSION         = "1.0";
const char* const JSON_RPC_VERSION         = "2.0";

/*---------------------------------------------------------*\
| Standard JSON-RPC 2.0 Error Codes                        |
\*---------------------------------------------------------*/
const int PARSE_ERROR                      = -32700;
const int INVALID_REQUEST                  = -32600;
const int METHOD_NOT_FOUND                 = -32601;
const int INVALID_PARAMS                   = -32602;
const int INTERNAL_ERROR                   = -32603;

/*---------------------------------------------------------*\
| Application-Specific Error Codes                          |
\*---------------------------------------------------------*/
const int ERR_DEVICE_INDEX_OUT_OF_RANGE    = -32001;
const int ERR_ZONE_INDEX_OUT_OF_RANGE      = -32002;
const int ERR_LED_INDEX_OUT_OF_RANGE       = -32003;
const int ERR_MODE_INDEX_OUT_OF_RANGE      = -32004;
const int ERR_PROFILE_NOT_FOUND            = -32005;
const int ERR_PROFILE_SAVE_FAILED          = -32006;
const int ERR_DEVICE_NOT_SUPPORTED         = -32007;
const int ERR_INVALID_COLOR_VALUE          = -32008;
const int ERR_INVALID_MODE_PARAMETERS      = -32009;
const int ERR_PLUGIN_NOT_FOUND             = -32010;
const int ERR_PLUGIN_METHOD_FAILED         = -32011;
const int ERR_OPERATION_NOT_PERMITTED      = -32012;
const int ERR_DEVICE_BUSY                  = -32013;
    const int ERR_RESIZE_NOT_SUPPORTED         = -32014;
    const int ERR_AUTHENTICATION_FAILED        = -32015;
    const int ERR_INVALID_TOKEN                = -32016;
    const int ERR_KEY_NAME_NOT_FOUND           = -32017;

/*---------------------------------------------------------*\
| Error Messages                                            |
\*---------------------------------------------------------*/
inline std::string GetErrorMessage(int error_code)
{
    switch(error_code)
    {
        case PARSE_ERROR:                     return "Parse error";
        case INVALID_REQUEST:                 return "Invalid Request";
        case METHOD_NOT_FOUND:                return "Method not found";
        case INVALID_PARAMS:                  return "Invalid params";
        case INTERNAL_ERROR:                  return "Internal error";
        case ERR_DEVICE_INDEX_OUT_OF_RANGE:   return "Device index out of range";
        case ERR_ZONE_INDEX_OUT_OF_RANGE:     return "Zone index out of range";
        case ERR_LED_INDEX_OUT_OF_RANGE:      return "LED index out of range";
        case ERR_MODE_INDEX_OUT_OF_RANGE:     return "Mode index out of range";
        case ERR_PROFILE_NOT_FOUND:           return "Profile not found";
        case ERR_PROFILE_SAVE_FAILED:         return "Profile save failed";
        case ERR_DEVICE_NOT_SUPPORTED:        return "Device not supported";
        case ERR_INVALID_COLOR_VALUE:         return "Invalid color value";
        case ERR_INVALID_MODE_PARAMETERS:     return "Invalid mode parameters";
        case ERR_PLUGIN_NOT_FOUND:            return "Plugin not found";
        case ERR_PLUGIN_METHOD_FAILED:        return "Plugin method failed";
        case ERR_OPERATION_NOT_PERMITTED:     return "Operation not permitted";
        case ERR_DEVICE_BUSY:                 return "Device busy";
        case ERR_RESIZE_NOT_SUPPORTED:        return "Resize not supported";
        case ERR_AUTHENTICATION_FAILED:       return "Authentication failed";
        case ERR_INVALID_TOKEN:               return "Invalid token";
        case ERR_KEY_NAME_NOT_FOUND:          return "Key name not found";
        default:                              return "Unknown error";
    }
}

/*---------------------------------------------------------*\
| Method Names                                              |
\*---------------------------------------------------------*/
namespace Methods
{
    // Device Management
    const char* const GET_CONTROLLERS           = "device.getControllers";
    const char* const GET_CONTROLLER_COUNT      = "device.getControllerCount";
    const char* const GET_CONTROLLER_DATA       = "device.getControllerData";
    const char* const GET_CONTROLLER_INFO       = "device.getControllerInfo";
    const char* const RESCAN_DEVICES            = "device.rescan";

    // Color Control
    const char* const SET_LED_COLOR             = "device.setLEDColor";
    const char* const SET_ZONE_COLOR            = "device.setZoneColor";
    const char* const SET_ZONE_MULTIPLE_LED     = "device.setZoneMultipleLed";
    const char* const SET_MULTIPLE_ZONE_MULTIPLE_LED = "device.setMultipleZoneMultipleLed";
    const char* const SET_ALL_COLORS            = "device.setAllColors";
    const char* const SET_MULTIPLE_COLORS       = "device.setMultipleColors";
    const char* const SET_KEY_COLOR             = "device.setKeyColor";

    // Mode Control
    const char* const SET_MODE                  = "device.setMode";
    const char* const SET_CUSTOM_MODE           = "device.setCustomMode";
    const char* const UPDATE_MODE               = "device.updateMode";

    // Zone Management
    const char* const GET_ZONES                 = "device.getZones";
    const char* const RESIZE_ZONE               = "device.resizeZone";
    const char* const CLEAR_SEGMENTS            = "zone.clearSegments";
    const char* const ADD_SEGMENT               = "zone.addSegment";

    // Profile Management
    const char* const GET_PROFILES              = "profile.getProfiles";
    const char* const SAVE_PROFILE              = "profile.saveProfile";
    const char* const LOAD_PROFILE              = "profile.loadProfile";
    const char* const DELETE_PROFILE            = "profile.deleteProfile";

    // Server Information
    const char* const GET_PROTOCOL_VERSION      = "server.getProtocolVersion";
    const char* const GET_SERVER_INFO           = "server.getServerInfo";
    const char* const GET_CLIENTS               = "server.getClients";
    const char* const SHUTDOWN                  = "server.shutdown";

    // Plugin Management
    const char* const GET_PLUGINS               = "plugin.getPlugins";
    const char* const CALL_PLUGIN               = "plugin.callPlugin";
}

/*---------------------------------------------------------*\
| Event Types                                               |
\*---------------------------------------------------------*/
namespace Events
{
    const char* const DEVICE_LIST_CHANGED       = "deviceListChanged";
    const char* const DEVICE_CONNECTED          = "deviceConnected";
    const char* const DEVICE_DISCONNECTED       = "deviceDisconnected";
    const char* const PROFILE_SAVED             = "profileSaved";
    const char* const PROFILE_LOADED            = "profileLoaded";
    const char* const CLIENT_CONNECTED          = "clientConnected";
    const char* const CLIENT_DISCONNECTED       = "clientDisconnected";
    const char* const SCAN_COMPLETE             = "scanComplete";
}

} // namespace JSONRPCProtocol
