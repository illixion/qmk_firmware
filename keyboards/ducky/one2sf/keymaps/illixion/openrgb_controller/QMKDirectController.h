/*---------------------------------------------------------*\
| QMKDirectController.h                                     |
|                                                           |
|   Generic HID driver for QMK keyboards using the          |
|   QMK Direct Protocol (QMKD)                             |
|                                                           |
|   Supports any QMK keyboard that includes                 |
|   qmk_openrgb_direct.h in its firmware.                  |
|                                                           |
|   illixion                                     2026       |
|                                                           |
|   This file is part of the OpenRGB project                |
|   SPDX-License-Identifier: GPL-2.0-or-later              |
\*---------------------------------------------------------*/

#pragma once

#include <string>
#include <vector>
#include <hidapi.h>
#include "RGBController.h"

/*---------------------------------------------------------*\
| QMK Direct Protocol (QMKD) v1.0                          |
|                                                           |
| 32-byte packets via usage page 0xFF60, usage 0x61         |
|                                                           |
| Control commands:                                         |
|   0x01  SET_LEDS      [start, count(<=9), R,G,B, ...]    |
|   0x02  ENABLE        Enter host-controlled mode          |
|   0x03  DISABLE       Return to local effects             |
|   0x04  HEARTBEAT     Keep-alive                          |
|                                                           |
| Discovery commands:                                       |
|   0x05  GET_PROTOCOL  Magic "QMKD" + version             |
|   0x06  GET_DEVICE    LED count, matrix dims, caps        |
|   0x07  GET_LED_MAP   Per-LED x,y,flags,row,col,keycode  |
\*---------------------------------------------------------*/

#define QMKD_RAW_EPSIZE           32

#define QMKD_CMD_SET_LEDS         0x01
#define QMKD_CMD_ENABLE           0x02
#define QMKD_CMD_DISABLE          0x03
#define QMKD_CMD_HEARTBEAT        0x04
#define QMKD_CMD_GET_PROTOCOL     0x05
#define QMKD_CMD_GET_DEVICE       0x06
#define QMKD_CMD_GET_LED_MAP      0x07

#define QMKD_RSP_UNKNOWN          0xFF

/*---------------------------------------------------------*\
| Per-LED info reported by the firmware                     |
\*---------------------------------------------------------*/
struct QMKDirectLEDInfo
{
    unsigned char x;
    unsigned char y;
    unsigned char flags;
    unsigned char row;           /* 0xFF = not in key matrix  */
    unsigned char col;           /* 0xFF = not in key matrix  */
    unsigned short keycode;      /* QMK layer-0 keycode       */
};

class QMKDirectController
{
public:
    QMKDirectController(hid_device* dev_handle, const char* path);
    ~QMKDirectController();

    std::string     GetDeviceLocation();
    std::string     GetSerialString();
    std::string     GetDeviceName();

    /*-----------------------------------------------------*\
    | Discovery — called once during initialisation          |
    \*-----------------------------------------------------*/
    bool            ProbeProtocol(unsigned char &major, unsigned char &minor);
    bool            QueryDeviceInfo(unsigned char &led_count,
                                    unsigned char &rows,
                                    unsigned char &cols,
                                    unsigned char &max_leds_pkt,
                                    unsigned char &timeout_sec);
    bool            QueryLEDMap(unsigned char start,
                                std::vector<QMKDirectLEDInfo> &leds);

    /*-----------------------------------------------------*\
    | Runtime — LED control                                  |
    \*-----------------------------------------------------*/
    void            EnableDirect();
    void            DisableDirect();
    void            Heartbeat();
    void            SendColors(unsigned char* color_data,
                               unsigned int led_count,
                               unsigned int max_per_pkt);

private:
    hid_device*     dev;
    std::string     location;
    std::string     device_name;

    bool            SendPacket(unsigned char* data, unsigned int length,
                               unsigned char* response);
};
