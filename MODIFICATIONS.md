# OpenRGB Custom Build - Modifications

This document describes the modifications made to the original OpenRGB project in this custom build.

## Base Version

- **Repository**: https://gitlab.com/CalcProgrammer1/OpenRGB
- **Branch**: dev
- **Custom Repository**: https://github.com/JKWTCN/OpenRGB
- **Branch**: dev

## License

This custom build is licensed under the same GPL v2 license as the original OpenRGB project.
See the [LICENSE](LICENSE) file for details.

## Modifications Summary

### 1. Device Settings Preservation During Hot-Plug (2026-04-01)

**Files Modified**:
- `RGBController/RGBController.cpp`
- `ResourceManager.cpp`
- `ResourceManager.h`
- `qt/OpenRGBDevicePage/OpenRGBDevicePage.cpp`

**Changes**:

#### a) Fixed RGBController SignalUpdate() Race Condition
- **Problem**: `SignalUpdate()` was executing callbacks while holding the mutex, causing potential deadlocks
- **Solution**: Copy callbacks under lock, then execute without holding the mutex
- **Impact**: Improved thread safety and reduced potential for deadlocks

#### b) Implemented Controller State Restore in ResourceManager
- **Problem**: Device settings (modes, colors, zones) were lost when devices were hot-plugged
- **Solution**: Detect matching controllers during rediscovery and copy runtime state into the newly opened controller
  - Compares device name, serial, and location to identify the same physical device
  - Preserves mode and color settings when a device is re-detected
  - Keeps the newly detected controller object so USB/HID handles are refreshed after hot-plug
  - Only creates new controllers for genuinely new devices
- **Impact**: Device settings are now preserved across hot-plug events (USB reconnect, driver reload, etc.)

#### c) Fixed OpenRGBDevicePage Dangling Pointer Issue
- **Problem**: Static `UpdateCallback()` could access destroyed `OpenRGBDevicePage` objects
- **Solution**: Added global map `g_valid_pages` to track valid instances
  - Pages register themselves on construction
  - Pages unregister on destruction
  - Callback checks validity before accessing page objects
- **Impact**: Eliminated crashes when device pages are destroyed while callbacks are pending

### 2. Async Device Rescan and Enhanced Scan Events (2026-04-02)

**Files Modified**:
- `JSONRPCHandler.cpp`
- `JSONRPCHandler.h`
- `WebSocketServer.cpp`

**Changes**:

#### a) Added Async Device Rescan Support
- **Feature**: `RescanDevices()` now executes asynchronously
- **Implementation**:
  - Uses `std::async` to run device detection in background
  - Returns immediately to caller without blocking
  - Manages rescan state with mutex to prevent concurrent rescans
- **Impact**: Non-blocking device rescan for better UI responsiveness

#### b) Enhanced Scan Completion Event Broadcasting
- **Feature**: WebSocket clients receive enhanced scan completion events
- **Implementation**:
  - Added scan completion event to WebSocket server
  - Broadcasts event to all connected clients when scan finishes
  - Includes scan results in event payload
- **Impact**: Better real-time updates for SDK clients and plugins

## Technical Details

### Thread Safety Improvements

All modifications maintain or improve thread safety:
- Mutex-protected callback lists
- Lock-based state management for async operations
- Safe destruction patterns for UI objects

### Backward Compatibility

- All changes are backward compatible with existing OpenRGB installations
- No changes to network protocol (additions only)
- No changes to profile format
- No changes to plugin API

## Testing

The modifications have been tested with:
- Windows 11 Pro
- Multiple RGB devices from different manufacturers
- Hot-plug scenarios (USB disconnect/reconnect)
- Concurrent WebSocket/JSON-RPC connections
- Extended runtime testing

## Credits

- **Original OpenRGB**: Adam Honse (CalcProgrammer1) and contributors
- **Modifications**: JKWTCN <jkwtcn@icloud.com>

## Support

For issues specific to these modifications, please report them at:
https://github.com/JKWTCN/OpenRGB/issues

For general OpenRGB issues, please report to the official repository:
https://gitlab.com/CalcProgrammer1/OpenRGB/-/issues
