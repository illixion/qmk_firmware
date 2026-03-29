/*---------------------------------------------------------*\
| RGBController_QMKDirect.h                                 |
|                                                           |
|   Generic RGBController for QMK keyboards using the       |
|   QMK Direct Protocol (QMKD)                             |
|                                                           |
|   Dynamically builds layout from firmware-reported data   |
|   — no hardcoded key maps needed.                         |
|                                                           |
|   illixion                                     2026       |
|                                                           |
|   This file is part of the OpenRGB project                |
|   SPDX-License-Identifier: GPL-2.0-or-later              |
\*---------------------------------------------------------*/

#pragma once

#include "RGBController.h"
#include "QMKDirectController.h"

class RGBController_QMKDirect : public RGBController
{
public:
    RGBController_QMKDirect(QMKDirectController* controller_ptr);
    ~RGBController_QMKDirect();

    void        SetupZones();
    void        ResizeZone(int zone, int new_size);

    void        DeviceUpdateLEDs();
    void        UpdateZoneLEDs(int zone);
    void        UpdateSingleLED(int led);

    void        DeviceUpdateMode();
    void        DeviceSaveMode();

private:
    QMKDirectController*    controller;

    unsigned char           led_count;
    unsigned char           matrix_rows;
    unsigned char           matrix_cols;
    unsigned char           max_leds_per_pkt;
    unsigned char           timeout_seconds;
    unsigned int            heartbeat_counter;
    unsigned int            heartbeat_interval;

    std::vector<QMKDirectLEDInfo>   led_info;

    void        QueryDevice();
    const char* KeycodeToName(unsigned short keycode);
};
