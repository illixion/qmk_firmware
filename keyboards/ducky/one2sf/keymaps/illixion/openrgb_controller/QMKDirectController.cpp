/*---------------------------------------------------------*\
| QMKDirectController.cpp                                   |
|                                                           |
|   Generic HID driver for QMK keyboards using the          |
|   QMK Direct Protocol (QMKD)                             |
|                                                           |
|   illixion                                     2026       |
|                                                           |
|   This file is part of the OpenRGB project                |
|   SPDX-License-Identifier: GPL-2.0-or-later              |
\*---------------------------------------------------------*/

#include <cstring>
#include "QMKDirectController.h"
#include "StringUtils.h"

QMKDirectController::QMKDirectController(hid_device* dev_handle, const char* path)
{
    dev      = dev_handle;
    location = path;

    /*---------------------------------------------------------*\
    | Read device name from USB product string                  |
    \*---------------------------------------------------------*/
    wchar_t product[128] = {0};
    if(hid_get_product_string(dev, product, 128) == 0)
    {
        device_name = StringUtils::wstring_to_string(product);
    }
    else
    {
        device_name = "QMK Keyboard";
    }
}

QMKDirectController::~QMKDirectController()
{
    DisableDirect();
    hid_close(dev);
}

std::string QMKDirectController::GetDeviceLocation()
{
    return("HID: " + location);
}

std::string QMKDirectController::GetSerialString()
{
    wchar_t serial_string[128];
    int ret = hid_get_serial_number_string(dev, serial_string, 128);

    if(ret != 0)
    {
        return("");
    }

    return(StringUtils::wstring_to_string(serial_string));
}

std::string QMKDirectController::GetDeviceName()
{
    return device_name;
}

/*---------------------------------------------------------*\
| SendPacket                                                |
|                                                           |
|   Sends a 32-byte raw HID packet and reads the response.  |
|   Returns true if a response was received.                |
\*---------------------------------------------------------*/
bool QMKDirectController::SendPacket(unsigned char* data, unsigned int length,
                                     unsigned char* response)
{
    unsigned char packet[QMKD_RAW_EPSIZE + 1];
    memset(packet, 0, sizeof(packet));

    /*---------------------------------------------------------*\
    | Byte 0 is HID report ID (0x00 for raw HID)               |
    \*---------------------------------------------------------*/
    packet[0] = 0x00;

    unsigned int copy_len = (length < QMKD_RAW_EPSIZE)
                          ? length : QMKD_RAW_EPSIZE;
    memcpy(&packet[1], data, copy_len);

    hid_write(dev, packet, sizeof(packet));

    /*---------------------------------------------------------*\
    | Read response with 200ms timeout                          |
    \*---------------------------------------------------------*/
    if(response != nullptr)
    {
        memset(response, 0, QMKD_RAW_EPSIZE);
        int bytes = hid_read_timeout(dev, response, QMKD_RAW_EPSIZE, 200);
        return(bytes > 0);
    }
    else
    {
        /*-----------------------------------------------------*\
        | Fire-and-forget: just drain the response so the HID   |
        | input buffer doesn't fill up. Short timeout since the |
        | firmware responds immediately.                        |
        \*-----------------------------------------------------*/
        unsigned char discard[QMKD_RAW_EPSIZE];
        hid_read_timeout(dev, discard, sizeof(discard), 10);
        return true;
    }
}

/*---------------------------------------------------------*\
| ProbeProtocol                                             |
|                                                           |
|   Send GET_PROTOCOL and verify the "QMKD" magic.         |
|   Returns true if this device speaks QMK Direct Protocol.  |
\*---------------------------------------------------------*/
bool QMKDirectController::ProbeProtocol(unsigned char &major, unsigned char &minor)
{
    unsigned char cmd[1] = { QMKD_CMD_GET_PROTOCOL };
    unsigned char rsp[QMKD_RAW_EPSIZE];

    if(!SendPacket(cmd, sizeof(cmd), rsp))
    {
        return false;
    }

    if(rsp[0] == QMKD_CMD_GET_PROTOCOL &&
       rsp[1] == 'Q' && rsp[2] == 'M' && rsp[3] == 'K' && rsp[4] == 'D')
    {
        major = rsp[5];
        minor = rsp[6];
        return true;
    }

    return false;
}

/*---------------------------------------------------------*\
| QueryDeviceInfo                                           |
\*---------------------------------------------------------*/
bool QMKDirectController::QueryDeviceInfo(unsigned char &led_count,
                                          unsigned char &rows,
                                          unsigned char &cols,
                                          unsigned char &max_leds_pkt,
                                          unsigned char &timeout_sec)
{
    unsigned char cmd[1] = { QMKD_CMD_GET_DEVICE };
    unsigned char rsp[QMKD_RAW_EPSIZE];

    if(!SendPacket(cmd, sizeof(cmd), rsp))
    {
        return false;
    }

    if(rsp[0] != QMKD_CMD_GET_DEVICE)
    {
        return false;
    }

    led_count    = rsp[1];
    rows         = rsp[2];
    cols         = rsp[3];
    max_leds_pkt = rsp[4];
    timeout_sec  = rsp[5];
    return true;
}

/*---------------------------------------------------------*\
| QueryLEDMap                                               |
|                                                           |
|   Query one batch of LED info starting at `start`.        |
|   Appends results to the `leds` vector.                   |
|   Returns true if at least one LED was returned.          |
\*---------------------------------------------------------*/
bool QMKDirectController::QueryLEDMap(unsigned char start,
                                      std::vector<QMKDirectLEDInfo> &leds)
{
    unsigned char cmd[2] = { QMKD_CMD_GET_LED_MAP, start };
    unsigned char rsp[QMKD_RAW_EPSIZE];

    if(!SendPacket(cmd, sizeof(cmd), rsp))
    {
        return false;
    }

    if(rsp[0] != QMKD_CMD_GET_LED_MAP)
    {
        return false;
    }

    unsigned char count = rsp[2];
    if(count == 0)
    {
        return false;
    }

    for(unsigned char i = 0; i < count; i++)
    {
        unsigned char off = 3 + i * 7;
        QMKDirectLEDInfo info;
        info.x       = rsp[off + 0];
        info.y       = rsp[off + 1];
        info.flags   = rsp[off + 2];
        info.row     = rsp[off + 3];
        info.col     = rsp[off + 4];
        info.keycode = rsp[off + 5] | ((unsigned short)rsp[off + 6] << 8);
        leds.push_back(info);
    }

    return true;
}

/*---------------------------------------------------------*\
| Runtime control                                           |
\*---------------------------------------------------------*/
void QMKDirectController::EnableDirect()
{
    unsigned char cmd[1] = { QMKD_CMD_ENABLE };
    SendPacket(cmd, sizeof(cmd), nullptr);
}

void QMKDirectController::DisableDirect()
{
    unsigned char cmd[1] = { QMKD_CMD_DISABLE };
    SendPacket(cmd, sizeof(cmd), nullptr);
}

void QMKDirectController::Heartbeat()
{
    unsigned char cmd[1] = { QMKD_CMD_HEARTBEAT };
    SendPacket(cmd, sizeof(cmd), nullptr);
}

void QMKDirectController::SendColors(unsigned char* color_data,
                                     unsigned int led_count,
                                     unsigned int max_per_pkt)
{
    if(max_per_pkt == 0 || max_per_pkt > 9) max_per_pkt = 9;

    for(unsigned int start = 0; start < led_count; start += max_per_pkt)
    {
        unsigned int batch = led_count - start;
        if(batch > max_per_pkt) batch = max_per_pkt;

        unsigned char pkt[QMKD_RAW_EPSIZE];
        memset(pkt, 0, sizeof(pkt));

        pkt[0] = QMKD_CMD_SET_LEDS;
        pkt[1] = (unsigned char)start;
        pkt[2] = (unsigned char)batch;

        memcpy(&pkt[3], &color_data[start * 3], batch * 3);

        SendPacket(pkt, sizeof(pkt), nullptr);
    }
}
