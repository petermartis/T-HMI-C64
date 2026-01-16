/*
 * original_basic.h - Original Atari BASIC ROM
 *
 * PLACEHOLDER FILE - Replace with converted ROM data
 *
 * To use original Atari ROMs:
 * 1. Place your ATARIBAS.ROM (8192 bytes) in this directory
 * 2. Run: python3 convert_roms.py ATARIBAS.ROM
 * 3. This will overwrite this file with the actual ROM data
 * 4. Rebuild the project
 *
 * The original Atari BASIC ROM has:
 * - Size: 8192 bytes (8KB)
 * - Memory range: $A000-$BFFF
 * - Revision C is recommended for best compatibility
 */
#ifndef ORIGINAL_BASIC_H
#define ORIGINAL_BASIC_H

#include <cstdint>

// Set to 1 when original ROM data is present
#define HAVE_ORIGINAL_BASIC_ROM 0

#if HAVE_ORIGINAL_BASIC_ROM
alignas(4) static const uint8_t original_basic[8192] = {
    // ROM data will be inserted here by convert_roms.py
    0
};
#endif

#endif // ORIGINAL_BASIC_H
