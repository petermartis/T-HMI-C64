/*
 * original_os_xl.h - Original Atari 800XL OS ROM
 *
 * PLACEHOLDER FILE - Replace with converted ROM data
 *
 * To use original Atari ROMs:
 * 1. Place your ATARIXL.ROM (16384 bytes) in this directory
 * 2. Run: python3 convert_roms.py ATARIXL.ROM
 * 3. This will overwrite this file with the actual ROM data
 * 4. Rebuild the project
 *
 * The original Atari 800XL OS ROM has:
 * - Reset vector: $C2AA
 * - Size: 16384 bytes (16KB)
 * - Memory range: $C000-$FFFF
 */
#ifndef ORIGINAL_OS_XL_H
#define ORIGINAL_OS_XL_H

#include <cstdint>

// Set to 1 when original ROM data is present
#define HAVE_ORIGINAL_OS_ROM 0

#if HAVE_ORIGINAL_OS_ROM
alignas(4) static const uint8_t original_os_xl[16384] = {
    // ROM data will be inserted here by convert_roms.py
    0
};
#endif

#endif // ORIGINAL_OS_XL_H
