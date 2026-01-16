/*
 Copyright (C) 2024-2025 retroelec <retroelec42@gmail.com>

 This program is free software; you can redistribute it and/or modify it
 under the terms of the GNU General Public License as published by the
 Free Software Foundation; either version 3 of the License, or (at your
 option) any later version.

 Atari BASIC ROM
 ===============
 This file supports both original Atari ROMs and Altirra replacements.

 To use original Atari ROMs:
 1. Place ATARIBAS.ROM in src/roms/original/
 2. Run: python3 convert_roms.py ATARIBAS.ROM
 3. Rebuild the project

 Altirra BASIC is used as fallback when original ROMs are not available.
 Altirra 8K BASIC 1.58
 Copyright (C) 2008-2018 Avery Lee

 BASIC ROM resides at $A000-$BFFF (8KB)
*/
#include "atari_basic.h"
#include "altirra_basic.h"
#include "original/original_basic.h"

const uint8_t* getAtariBasicRom() {
    // DEBUG: Force Altirra BASIC to test if emulation works
    // TODO: Remove this after debugging
#define FORCE_ALTIRRA_BASIC 1
#if FORCE_ALTIRRA_BASIC
    return altirra_basic;
#elif HAVE_ORIGINAL_BASIC_ROM
    // Use original Atari BASIC ROM when available
    return original_basic;
#else
    // Fall back to Altirra BASIC ROM
    return altirra_basic;
#endif
}

// For backward compatibility, provide a const reference
// Note: This is zero-initialized; use getAtariBasicRom() for actual ROM data
alignas(4) const uint8_t atari_basic_rom[ATARI_BASIC_SIZE] = {0};
