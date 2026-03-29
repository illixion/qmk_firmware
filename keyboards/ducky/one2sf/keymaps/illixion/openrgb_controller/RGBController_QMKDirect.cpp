/*---------------------------------------------------------*\
| RGBController_QMKDirect.cpp                               |
|                                                           |
|   Generic RGBController for QMK keyboards using the       |
|   QMK Direct Protocol (QMKD)                             |
|                                                           |
|   Layout is built entirely from firmware-reported data.   |
|   No per-keyboard hardcoding needed — any QMK board       |
|   with qmk_openrgb_direct.h just works.                  |
|                                                           |
|   illixion                                     2026       |
|                                                           |
|   This file is part of the OpenRGB project                |
|   SPDX-License-Identifier: GPL-2.0-or-later              |
\*---------------------------------------------------------*/

#include "RGBControllerKeyNames.h"
#include "RGBController_QMKDirect.h"

#define NA  0xFFFFFFFF

/*---------------------------------------------------------*\
| QMK basic keycodes -> OpenRGB key name constants          |
|                                                           |
| QMK basic keycodes 0x00-0xFF follow USB HID usage table.  |
| We only map the printable/common keys here; anything      |
| unmapped gets a generated name like "Key 0x1A".           |
\*---------------------------------------------------------*/
static const struct
{
    unsigned short  keycode;
    const char*     name;
} qmk_keycode_map[] =
{
    /* Letters */
    { 0x04, KEY_EN_A },
    { 0x05, KEY_EN_B },
    { 0x06, KEY_EN_C },
    { 0x07, KEY_EN_D },
    { 0x08, KEY_EN_E },
    { 0x09, KEY_EN_F },
    { 0x0A, KEY_EN_G },
    { 0x0B, KEY_EN_H },
    { 0x0C, KEY_EN_I },
    { 0x0D, KEY_EN_J },
    { 0x0E, KEY_EN_K },
    { 0x0F, KEY_EN_L },
    { 0x10, KEY_EN_M },
    { 0x11, KEY_EN_N },
    { 0x12, KEY_EN_O },
    { 0x13, KEY_EN_P },
    { 0x14, KEY_EN_Q },
    { 0x15, KEY_EN_R },
    { 0x16, KEY_EN_S },
    { 0x17, KEY_EN_T },
    { 0x18, KEY_EN_U },
    { 0x19, KEY_EN_V },
    { 0x1A, KEY_EN_W },
    { 0x1B, KEY_EN_X },
    { 0x1C, KEY_EN_Y },
    { 0x1D, KEY_EN_Z },

    /* Number row */
    { 0x1E, KEY_EN_1 },
    { 0x1F, KEY_EN_2 },
    { 0x20, KEY_EN_3 },
    { 0x21, KEY_EN_4 },
    { 0x22, KEY_EN_5 },
    { 0x23, KEY_EN_6 },
    { 0x24, KEY_EN_7 },
    { 0x25, KEY_EN_8 },
    { 0x26, KEY_EN_9 },
    { 0x27, KEY_EN_0 },

    /* Control keys */
    { 0x28, KEY_EN_ANSI_ENTER },
    { 0x29, KEY_EN_ESCAPE },
    { 0x2A, KEY_EN_BACKSPACE },
    { 0x2B, KEY_EN_TAB },
    { 0x2C, KEY_EN_SPACE },
    { 0x2D, KEY_EN_MINUS },
    { 0x2E, KEY_EN_EQUALS },
    { 0x2F, KEY_EN_LEFT_BRACKET },
    { 0x30, KEY_EN_RIGHT_BRACKET },
    { 0x31, KEY_EN_ANSI_BACK_SLASH },
    { 0x33, KEY_EN_SEMICOLON },
    { 0x34, KEY_EN_QUOTE },
    { 0x35, KEY_EN_BACK_TICK },
    { 0x36, KEY_EN_COMMA },
    { 0x37, KEY_EN_PERIOD },
    { 0x38, KEY_EN_FORWARD_SLASH },
    { 0x39, KEY_EN_CAPS_LOCK },

    /* Function keys */
    { 0x3A, KEY_EN_F1 },
    { 0x3B, KEY_EN_F2 },
    { 0x3C, KEY_EN_F3 },
    { 0x3D, KEY_EN_F4 },
    { 0x3E, KEY_EN_F5 },
    { 0x3F, KEY_EN_F6 },
    { 0x40, KEY_EN_F7 },
    { 0x41, KEY_EN_F8 },
    { 0x42, KEY_EN_F9 },
    { 0x43, KEY_EN_F10 },
    { 0x44, KEY_EN_F11 },
    { 0x45, KEY_EN_F12 },

    /* Navigation */
    { 0x46, KEY_EN_PRINT_SCREEN },
    { 0x47, KEY_EN_SCROLL_LOCK },
    { 0x48, KEY_EN_PAUSE_BREAK },
    { 0x49, KEY_EN_INSERT },
    { 0x4A, KEY_EN_HOME },
    { 0x4B, KEY_EN_PAGE_UP },
    { 0x4C, KEY_EN_DELETE },
    { 0x4D, KEY_EN_END },
    { 0x4E, KEY_EN_PAGE_DOWN },
    { 0x4F, KEY_EN_RIGHT_ARROW },
    { 0x50, KEY_EN_LEFT_ARROW },
    { 0x51, KEY_EN_DOWN_ARROW },
    { 0x52, KEY_EN_UP_ARROW },

    /* Numpad */
    { 0x53, KEY_EN_NUMPAD_LOCK },
    { 0x54, KEY_EN_NUMPAD_DIVIDE },
    { 0x55, KEY_EN_NUMPAD_TIMES },
    { 0x56, KEY_EN_NUMPAD_MINUS },
    { 0x57, KEY_EN_NUMPAD_PLUS },
    { 0x58, KEY_EN_NUMPAD_ENTER },
    { 0x59, KEY_EN_NUMPAD_1 },
    { 0x5A, KEY_EN_NUMPAD_2 },
    { 0x5B, KEY_EN_NUMPAD_3 },
    { 0x5C, KEY_EN_NUMPAD_4 },
    { 0x5D, KEY_EN_NUMPAD_5 },
    { 0x5E, KEY_EN_NUMPAD_6 },
    { 0x5F, KEY_EN_NUMPAD_7 },
    { 0x60, KEY_EN_NUMPAD_8 },
    { 0x61, KEY_EN_NUMPAD_9 },
    { 0x62, KEY_EN_NUMPAD_0 },
    { 0x63, KEY_EN_NUMPAD_PERIOD },

    /* Modifiers */
    { 0xE0, KEY_EN_LEFT_CONTROL },
    { 0xE1, KEY_EN_LEFT_SHIFT },
    { 0xE2, KEY_EN_LEFT_ALT },
    { 0xE3, KEY_EN_LEFT_WINDOWS },
    { 0xE4, KEY_EN_RIGHT_CONTROL },
    { 0xE5, KEY_EN_RIGHT_SHIFT },
    { 0xE6, KEY_EN_RIGHT_ALT },
    { 0xE7, KEY_EN_RIGHT_WINDOWS },

    { 0, nullptr }   /* sentinel */
};

/*---------------------------------------------------------*\
| Look up OpenRGB key name from QMK keycode                 |
\*---------------------------------------------------------*/
const char* RGBController_QMKDirect::KeycodeToName(unsigned short keycode)
{
    /* Basic keycodes (USB HID range) */
    for(int i = 0; qmk_keycode_map[i].name != nullptr; i++)
    {
        if(qmk_keycode_map[i].keycode == keycode)
        {
            return qmk_keycode_map[i].name;
        }
    }

    /*---------------------------------------------------------*\
    | QMK layer/function keycodes (MO, LT, etc.)               |
    | These don't map to physical key labels — use Fn           |
    \*---------------------------------------------------------*/
    if(keycode >= 0x5100 && keycode <= 0x51FF)
    {
        /* MO(layer) — momentary layer switch */
        return KEY_EN_RIGHT_FUNCTION;
    }

    return nullptr;
}

/**------------------------------------------------------------------*\
    @name QMK Direct Protocol Keyboard
    @category Keyboard
    @type USB
    @save :x:
    @direct :white_check_mark:
    @effects :x:
    @detectors DetectQMKDirectControllers
    @comment Supports any QMK keyboard running firmware with
        qmk_openrgb_direct.h (QMKD protocol v1.0+).
\*-------------------------------------------------------------------*/

RGBController_QMKDirect::RGBController_QMKDirect(QMKDirectController* controller_ptr)
{
    controller        = controller_ptr;
    heartbeat_counter = 0;

    /*---------------------------------------------------------*\
    | Query the device to learn its capabilities                |
    \*---------------------------------------------------------*/
    QueryDevice();

    name        = controller->GetDeviceName() + " (QMK)";
    vendor      = "QMK";
    type        = DEVICE_TYPE_KEYBOARD;
    description = "QMK keyboard with Direct Protocol (QMKD)";
    location    = controller->GetDeviceLocation();
    serial      = controller->GetSerialString();

    mode Direct;
    Direct.name       = "Direct";
    Direct.value      = 0;
    Direct.flags      = MODE_FLAG_HAS_PER_LED_COLOR;
    Direct.color_mode = MODE_COLORS_PER_LED;
    modes.push_back(Direct);

    SetupZones();

    /*---------------------------------------------------------*\
    | Enable host-controlled mode on the keyboard               |
    \*---------------------------------------------------------*/
    controller->EnableDirect();
}

RGBController_QMKDirect::~RGBController_QMKDirect()
{
    delete controller;
}

/*---------------------------------------------------------*\
| QueryDevice                                               |
|                                                           |
|   Query the firmware for LED count, matrix dimensions,    |
|   and per-LED layout info. Called once at startup.         |
\*---------------------------------------------------------*/
void RGBController_QMKDirect::QueryDevice()
{
    /*---------------------------------------------------------*\
    | Device info                                               |
    \*---------------------------------------------------------*/
    led_count       = 0;
    matrix_rows     = 0;
    matrix_cols     = 0;
    max_leds_per_pkt = 9;
    timeout_seconds = 5;

    controller->QueryDeviceInfo(led_count, matrix_rows, matrix_cols,
                                max_leds_per_pkt, timeout_seconds);

    /*---------------------------------------------------------*\
    | Calculate heartbeat interval: send heartbeat often enough  |
    | to stay well within the firmware timeout                  |
    \*---------------------------------------------------------*/
    if(timeout_seconds > 0)
    {
        /* Assume ~50 LED updates/sec; heartbeat at half the timeout */
        heartbeat_interval = (timeout_seconds * 50) / 2;
        if(heartbeat_interval < 10) heartbeat_interval = 10;
    }
    else
    {
        heartbeat_interval = 100;
    }

    /*---------------------------------------------------------*\
    | LED map — query in batches of 4 until we have them all    |
    \*---------------------------------------------------------*/
    led_info.clear();
    for(unsigned char start = 0; start < led_count; )
    {
        std::size_t before = led_info.size();
        if(!controller->QueryLEDMap(start, led_info))
        {
            break;
        }
        start += (unsigned char)(led_info.size() - before);
    }
}

/*---------------------------------------------------------*\
| SetupZones                                                |
|                                                           |
|   Build OpenRGB zones, matrix map, and LED names          |
|   entirely from the firmware-reported LED info.           |
\*---------------------------------------------------------*/
void RGBController_QMKDirect::SetupZones()
{
    /*---------------------------------------------------------*\
    | Determine if we have matrix-mapped LEDs and/or            |
    | non-matrix LEDs (underglow, decorative, etc.)             |
    \*---------------------------------------------------------*/
    unsigned int matrix_led_count = 0;
    unsigned int extra_led_count  = 0;

    for(std::size_t i = 0; i < led_info.size(); i++)
    {
        if(led_info[i].row != 0xFF)
        {
            matrix_led_count++;
        }
        else
        {
            extra_led_count++;
        }
    }

    /*---------------------------------------------------------*\
    | Keyboard zone (matrix type)                               |
    \*---------------------------------------------------------*/
    if(matrix_led_count > 0 && matrix_rows > 0 && matrix_cols > 0)
    {
        zone kb_zone;
        kb_zone.name       = ZONE_EN_KEYBOARD;
        kb_zone.type       = ZONE_TYPE_MATRIX;
        kb_zone.leds_min   = led_count;
        kb_zone.leds_max   = led_count;
        kb_zone.leds_count = led_count;

        /*-----------------------------------------------------*\
        | Build matrix map from firmware-reported row/col         |
        \*-----------------------------------------------------*/
        kb_zone.matrix_map         = new matrix_map_type;
        kb_zone.matrix_map->height = matrix_rows;
        kb_zone.matrix_map->width  = matrix_cols;

        unsigned int map_size = matrix_rows * matrix_cols;
        unsigned int* map_data = new unsigned int[map_size];

        for(unsigned int i = 0; i < map_size; i++)
        {
            map_data[i] = NA;
        }

        for(std::size_t i = 0; i < led_info.size(); i++)
        {
            if(led_info[i].row != 0xFF && led_info[i].col != 0xFF)
            {
                unsigned int idx = led_info[i].row * matrix_cols + led_info[i].col;
                if(idx < map_size)
                {
                    map_data[idx] = (unsigned int)i;
                }
            }
        }

        kb_zone.matrix_map->map = map_data;
        zones.push_back(kb_zone);
    }

    /*---------------------------------------------------------*\
    | Create LED entries with names from keycodes               |
    \*---------------------------------------------------------*/
    for(std::size_t i = 0; i < led_info.size(); i++)
    {
        led new_led;

        const char* key_name = KeycodeToName(led_info[i].keycode);

        if(key_name != nullptr)
        {
            new_led.name = key_name;
        }
        else if(led_info[i].row != 0xFF)
        {
            /* Key is in the matrix but has no standard name */
            char buf[32];
            snprintf(buf, sizeof(buf), "Key [%d,%d]",
                     led_info[i].row, led_info[i].col);
            new_led.name = buf;
        }
        else
        {
            /* LED not in the key matrix (underglow, accent, etc.) */
            char buf[32];
            snprintf(buf, sizeof(buf), "LED %d", (int)i);
            new_led.name = buf;
        }

        new_led.value = (unsigned int)i;
        leds.push_back(new_led);
    }

    SetupColors();
}

void RGBController_QMKDirect::ResizeZone(int /*zone*/, int /*new_size*/)
{
    /*---------------------------------------------------------*\
    | This device does not support resizing zones               |
    \*---------------------------------------------------------*/
}

void RGBController_QMKDirect::DeviceUpdateLEDs()
{
    unsigned int count = (unsigned int)leds.size();
    unsigned char* frame_buf = new unsigned char[count * 3];

    for(std::size_t i = 0; i < count; i++)
    {
        frame_buf[(i * 3) + 0] = RGBGetRValue(colors[i]);
        frame_buf[(i * 3) + 1] = RGBGetGValue(colors[i]);
        frame_buf[(i * 3) + 2] = RGBGetBValue(colors[i]);
    }

    controller->SendColors(frame_buf, count, max_leds_per_pkt);
    delete[] frame_buf;

    /*---------------------------------------------------------*\
    | Periodic heartbeat to keep host-controlled mode alive     |
    \*---------------------------------------------------------*/
    heartbeat_counter++;
    if(heartbeat_counter >= heartbeat_interval)
    {
        controller->Heartbeat();
        heartbeat_counter = 0;
    }
}

void RGBController_QMKDirect::UpdateZoneLEDs(int /*zone*/)
{
    DeviceUpdateLEDs();
}

void RGBController_QMKDirect::UpdateSingleLED(int /*led*/)
{
    DeviceUpdateLEDs();
}

void RGBController_QMKDirect::DeviceUpdateMode()
{
    /*---------------------------------------------------------*\
    | Direct mode is enabled/disabled in constructor/destructor  |
    \*---------------------------------------------------------*/
}

void RGBController_QMKDirect::DeviceSaveMode()
{
    /*---------------------------------------------------------*\
    | This device does not support saving modes                 |
    \*---------------------------------------------------------*/
}
