/*---------------------------------------------------------*\
| QMKDirectControllerDetect.cpp                             |
|                                                           |
|   Detector for QMK keyboards using the QMK Direct         |
|   Protocol (QMKD)                                        |
|                                                           |
|   Each device is detected by VID/PID and then probed      |
|   with GET_PROTOCOL to confirm QMKD compatibility.        |
|   To add a new keyboard, simply add its VID/PID below.    |
|                                                           |
|   illixion                                     2026       |
|                                                           |
|   This file is part of the OpenRGB project                |
|   SPDX-License-Identifier: GPL-2.0-or-later              |
\*---------------------------------------------------------*/

#include "Detector.h"
#include "QMKDirectController.h"
#include "RGBController_QMKDirect.h"
#include <hidapi.h>

/*---------------------------------------------------------*\
| QMK raw HID interface identifiers                         |
\*---------------------------------------------------------*/
#define QMK_USAGE_PAGE      0xFF60
#define QMK_USAGE           0x61

/*---------------------------------------------------------*\
| Supported devices                                         |
|                                                           |
| To add your QMK keyboard:                                 |
|   1. Flash firmware with qmk_openrgb_direct.h included    |
|   2. Add VID/PID below                                    |
|   3. Rebuild OpenRGB                                       |
|                                                           |
| The detector probes each device with GET_PROTOCOL to      |
| confirm compatibility, so false positives from matching    |
| VID/PID alone are safely filtered out.                    |
\*---------------------------------------------------------*/

/*---------------------------------------------------------*\
| Ducky                                                     |
\*---------------------------------------------------------*/
#define DUCKY_VID                   0x445B
#define DUCKY_ONE2SF_1967ST_PID     0x07AF

/*---------------------------------------------------------*\
| Add more vendors / PIDs here as needed:                   |
|                                                           |
| #define MYVENDOR_VID              0x1234                   |
| #define MYVENDOR_MYKB_PID        0x5678                   |
\*---------------------------------------------------------*/

/******************************************************************************************\
*                                                                                          *
*   DetectQMKDirectControllers                                                             *
*                                                                                          *
*       Opens the HID device, probes for QMKD protocol, and registers it if compatible.   *
*                                                                                          *
\******************************************************************************************/

void DetectQMKDirectControllers(hid_device_info* info, const std::string&)
{
    hid_device* dev = hid_open_path(info->path);

    if(dev)
    {
        QMKDirectController* controller = new QMKDirectController(dev, info->path);

        /*---------------------------------------------------------*\
        | Probe: verify the device speaks QMK Direct Protocol       |
        \*---------------------------------------------------------*/
        unsigned char major = 0;
        unsigned char minor = 0;

        if(controller->ProbeProtocol(major, minor))
        {
            RGBController_QMKDirect* rgb_controller = new RGBController_QMKDirect(controller);
            ResourceManager::get()->RegisterRGBController(rgb_controller);
        }
        else
        {
            /*-----------------------------------------------------*\
            | Device doesn't speak QMKD — clean up                  |
            \*-----------------------------------------------------*/
            delete controller;
        }
    }
}

/*---------------------------------------------------------*\
| Register detectors                                        |
|                                                           |
| Each REGISTER_HID_DETECTOR_PU line adds detection for     |
| one VID/PID combo. The detector function probes for QMKD  |
| so only compatible devices are registered.                 |
|                                                           |
| Add one line per keyboard model.                          |
\*---------------------------------------------------------*/

REGISTER_HID_DETECTOR_PU("Ducky One 2 SF (QMK Direct)",
                          DetectQMKDirectControllers,
                          DUCKY_VID,
                          DUCKY_ONE2SF_1967ST_PID,
                          QMK_USAGE_PAGE,
                          QMK_USAGE);

/*---------------------------------------------------------*\
| Example: adding another keyboard                          |
|                                                           |
| REGISTER_HID_DETECTOR_PU("My Keyboard (QMK Direct)",     |
|                           DetectQMKDirectControllers,     |
|                           MYVENDOR_VID,                   |
|                           MYVENDOR_MYKB_PID,             |
|                           QMK_USAGE_PAGE,                 |
|                           QMK_USAGE);                     |
\*---------------------------------------------------------*/
