/*
Copyright 2019 /u/KeepItUnder

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#pragma once

#define GPIO_INPUT_PIN_DELAY (NUC123_HCLK / 6 / 1000000L)

// Wear-leveling EEPROM backed by on-chip APROM flash (64 KB with default 4 KB data-flash).
// 2 KB backing store → 1 KB logical EEPROM, uses the last 4 sectors of APROM.
#define WEAR_LEVELING_EFL_FLASH_SIZE 0x10000
// NUC123 flash write granularity is 4 bytes (from hal_efl_lld.c NUC123_PAGE_SIZE).
// QMK's auto-detection checks QMK_MCU_FAMILY_NUC123 but the build system generates
// QMK_MCU_FAMILY_NUMICRO, so we set it explicitly.
#define BACKING_STORE_WRITE_SIZE 4
