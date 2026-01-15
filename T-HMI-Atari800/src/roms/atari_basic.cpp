/*
 Copyright (C) 2024-2025 retroelec <retroelec42@gmail.com>

 This program is free software; you can redistribute it and/or modify it
 under the terms of the GNU General Public License as published by the
 Free Software Foundation; either version 3 of the License, or (at your
 option) any later version.

 Atari BASIC ROM - Using Altirra BASIC
 =====================================
 This file uses the Altirra BASIC ROM which is a legal, freely-distributable
 replacement for the original Atari BASIC ROM.

 Altirra 8K BASIC 1.58
 Copyright (C) 2008-2018 Avery Lee

 The Altirra BASIC is released under a permissive license:
 "Copying and distribution of this file, with or without modification,
 are permitted in any medium without royalty provided the copyright
 notice and this notice are preserved. This file is offered as-is,
 without any warranty."

 BASIC ROM resides at $A000-$BFFF (8KB)
*/
#include "atari_basic.h"
#include "altirra_basic.h"

const uint8_t* getAtariBasicRom() {
    // Return the Altirra BASIC ROM directly
    return altirra_basic;
}

// For backward compatibility, provide a const reference
// Note: This is zero-initialized; use getAtariBasicRom() for actual ROM data
alignas(4) const uint8_t atari_basic_rom[ATARI_BASIC_SIZE] = {0};
