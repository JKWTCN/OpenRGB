/*---------------------------------------------------------*\
| startup.h                                                 |
|                                                           |
|   Startup for the OpenRGB application                     |
|                                                           |
|   This file is part of the OpenRGB project                |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#pragma once

int startup(int argc, char* argv[], unsigned int ret_flags);
void startup_set_service_mode(bool service_mode);
void startup_set_service_started_callback(void (*callback)(void));
void startup_request_shutdown();
bool startup_shutdown_requested();
