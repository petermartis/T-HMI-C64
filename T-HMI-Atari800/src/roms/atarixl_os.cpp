/*
 Copyright (C) 2024-2025 retroelec <retroelec42@gmail.com>

 This program is free software; you can redistribute it and/or modify it
 under the terms of the GNU General Public License as published by the
 Free Software Foundation; either version 3 of the License, or (at your
 option) any later version.

 This program is distributed in the hope that it will be useful, but
 WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 for more details.

 For the complete text of the GNU General Public License see
 http://www.gnu.org/licenses/.

 Atari XL OS ROM - Using Altirra OS
 ==================================
 This file uses the Altirra XL OS ROM which is a legal, freely-distributable
 replacement for the original Atari XL OS ROM.

 Altirra - Atari 800/800XL emulator
 Kernel ROM replacement, version 3.11
 Copyright (C) 2008-2018 Avery Lee

 The Altirra OS is released under a permissive license:
 "Copying and distribution of this file, with or without modification,
 are permitted in any medium without royalty provided the copyright
 notice and this notice are preserved. This file is offered as-is,
 without any warranty."

 OS ROM resides at $C000-$FFFF (16KB)
*/
#include "atarixl_os.h"
#include "altirraos_xl.h"

// Placeholder for backward compatibility (zero-initialized)
alignas(4) const uint8_t atarixl_os_rom[ATARIXL_OS_SIZE] = {0};

const uint8_t* getAtariOSRom() {
    // Return the Altirra XL OS ROM directly
    return altirra_os_xl;
}

// Display list for boot screen (not needed with real OS - OS creates its own)
static const uint8_t empty_display_list[] = {0};
static const uint8_t empty_screen_text[] = {0};

// Get display list data and size for RAM initialization
// With real OS, this is not needed as the OS sets up its own display
const uint8_t* getDisplayList(size_t* size) {
    if (size) *size = 0;
    return empty_display_list;
}

// Get screen text data and size for RAM initialization
const uint8_t* getScreenText(size_t* size) {
    if (size) *size = 0;
    return empty_screen_text;
}

// Get character ROM data for ANTIC access (1KB at $E000)
// The Altirra OS includes the character set at offset $2000 (address $E000)
const uint8_t* getCharacterRom(size_t* size) {
    if (size) *size = 1024;  // Standard Atari character set is 1KB
    // Character set is at offset $2000 in the 16KB ROM ($E000 - $C000 = $2000)
    return &altirra_os_xl[0x2000];
}
