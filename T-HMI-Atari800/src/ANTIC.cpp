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
*/
#include "ANTIC.h"
#include "GTIA.h"
#include "display/DisplayFactory.h"
#include "platform/PlatformManager.h"
#include <cstring>
#include <esp_log.h>
#include <Arduino.h>
#include <esp_heap_caps.h>

static const char* ATAG = "ANTIC";

// Mode line parameters: scanlines, bytes per line, characters/pixels
static const struct {
  uint8_t scanlines;
  uint8_t bytesPerLine;
  bool isChar;
} modeParams[16] = {
    {1, 0, false},   // 0: blank line instructions handled separately
    {1, 0, false},   // 1: jump instruction
    {8, 40, true},   // 2: 40 chars, 8 scanlines (GR.0)
    {10, 40, true},  // 3: 40 chars, 10 scanlines
    {8, 40, true},   // 4: 40 chars, 8 scanlines, multicolor
    {16, 40, true},  // 5: 40 chars, 16 scanlines, multicolor
    {8, 20, true},   // 6: 20 chars, 8 scanlines, 5 colors
    {16, 20, true},  // 7: 20 chars, 16 scanlines, 5 colors
    {8, 10, false},  // 8: 40 pixels, 8 scanlines (GR.3)
    {4, 10, false},  // 9: 80 pixels, 4 scanlines (GR.4)
    {4, 20, false},  // A: 80 pixels, 4 scanlines, 4 colors (GR.5)
    {2, 20, false},  // B: 160 pixels, 2 scanlines (GR.6)
    {1, 20, false},  // C: 160 pixels, 1 scanline
    {2, 40, false},  // D: 160 pixels, 2 scanlines, 4 colors (GR.7)
    {1, 40, false},  // E: 160 pixels, 1 scanline, 4 colors (GR.15)
    {1, 40, false},  // F: 320 pixels, 1 scanline, hires (GR.8)
};

ANTIC::ANTIC() : ram(nullptr), osRom(nullptr), selfTestEnabled(nullptr), bitmap(nullptr), display(nullptr), gtia(nullptr) {
  cntRefreshs = 0;
  reset();
}

void ANTIC::init(uint8_t *ram, const uint8_t *osRom, GTIA *gtia, const bool *selfTestEnabled) {
  PlatformManager::getInstance().log(LOG_INFO, ATAG, "init() starting");
  this->ram = ram;
  this->osRom = osRom;
  this->gtia = gtia;
  this->selfTestEnabled = selfTestEnabled;

  // Initialize palette (deferred from constructor to avoid FP during static init)
  palette.init();
  PlatformManager::getInstance().log(LOG_INFO, ATAG, "palette initialized");

  // Allocate bitmap in PSRAM for ATARI_WIDTH x ATARI_HEIGHT pixels (16-bit RGB565)
  // Using PSRAM (external RAM) to save internal RAM for task stacks
  size_t bitmapSize = ATARI_WIDTH * ATARI_HEIGHT * sizeof(uint16_t);
  PlatformManager::getInstance().log(LOG_INFO, ATAG, "Allocating bitmap (%u bytes), trying PSRAM...", bitmapSize);
  bitmap = (uint16_t*)ps_malloc(bitmapSize);
  if (!bitmap) {
    PlatformManager::getInstance().log(LOG_WARN, ATAG, "ps_malloc failed, trying heap_caps...");
    bitmap = (uint16_t*)heap_caps_malloc(bitmapSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  }
  if (!bitmap) {
    // Fallback to internal RAM if PSRAM not available
    PlatformManager::getInstance().log(LOG_WARN, ATAG, "PSRAM not available, using internal RAM");
    bitmap = new uint16_t[ATARI_WIDTH * ATARI_HEIGHT];
  } else {
    PlatformManager::getInstance().log(LOG_INFO, ATAG, "Bitmap allocated in PSRAM");
  }
  memset(bitmap, 0, bitmapSize);
  PlatformManager::getInstance().log(LOG_INFO, ATAG, "bitmap at %p (%dx%d)", (void*)bitmap, ATARI_WIDTH, ATARI_HEIGHT);

  // Create display driver
  display = Display::create();
  PlatformManager::getInstance().log(LOG_INFO, ATAG, "display created: %p", (void*)display);
  if (display) {
    display->init();
    PlatformManager::getInstance().log(LOG_INFO, ATAG, "display initialized");
  } else {
    PlatformManager::getInstance().log(LOG_ERROR, ATAG, "display is NULL!");
  }

  reset();
  PlatformManager::getInstance().log(LOG_INFO, ATAG, "init() complete");
}

void ANTIC::reset() {
  dmactl = 0;
  chactl = 0;
  dlist = 0;
  hscrol = 0;
  vscrol = 0;
  pmbase = 0;
  chbase = 0;
  nmien = 0;
  nmist = 0x1F;  // No NMIs pending

  scanline = 0;
  displayListPC = 0;
  memScan = 0;
  modeLineCount = 0;
  currentMode = 0;
  inDisplayList = false;
  dliPending = false;
  vbiPending = false;
  wsyncHalt = false;

  rowInMode = 0;
  scanLinesPerMode = 0;
  isCharMode = false;
  bytesPerLine = 0;
  charsPerLine = 0;
  memScanOffset = 0;
  xOffset = 0;
  pixelsPerByte = 1;

  hscrolEnabled = false;
  vscrolEnabled = false;
  vscrolLines = 0;

  dmaCycles = 0;
}

uint8_t ANTIC::read(uint8_t addr) {
  addr &= 0x0F;

  switch (addr) {
  case VCOUNT_R:
    // Return scanline / 2 (0-155 for NTSC, 0-155 for PAL visible)
    return (scanline >> 1) & 0xFF;

  case PENH:
    return 0x00; // Light pen horizontal (not implemented)

  case PENV:
    return 0x00; // Light pen vertical (not implemented)

  case NMIST:
    return nmist;

  default:
    return 0xFF;
  }
}

void ANTIC::write(uint8_t addr, uint8_t val) {
  addr &= 0x0F;

  switch (addr) {
  case DMACTL:
    dmactl = val;
    break;

  case CHACTL:
    chactl = val;
    break;

  case DLISTL:
    dlist = (dlist & 0xFF00) | val;
    break;

  case DLISTH:
    dlist = (dlist & 0x00FF) | (val << 8);
    break;

  case HSCROL:
    hscrol = val & 0x0F;
    break;

  case VSCROL:
    vscrol = val & 0x0F;
    break;

  case PMBASE:
    pmbase = val;
    break;

  case CHBASE:
    chbase = val;
    break;

  case WSYNC:
    // Halt CPU until end of scanline
    wsyncHalt = true;
    break;

  case NMIEN:
    nmien = val;
    break;

  case NMIRES:
    // Clear NMI status bits
    nmist = 0x1F;
    dliPending = false;
    vbiPending = false;
    break;
  }
}

uint8_t ANTIC::fetchDisplayListByte() {
  if (!(dmactl & DMACTL_DL)) {
    return 0;
  }
  // Use ROM-aware read for display list (self-test has DL in ROM)
  uint8_t byte = readMemWithROM(displayListPC & 0xFFFF);
  displayListPC++;
  dmaCycles++;
  return byte;
}

// Read byte from memory, using OS ROM for character set and self-test areas
// This is used for character set access and display list reads
inline uint8_t ANTIC::readMemWithROM(uint16_t addr) {
  // Self-test ROM at $5000-$57FF (when enabled via PORTB bit 7 = 0)
  if (selfTestEnabled && *selfTestEnabled && addr >= 0x5000 && addr < 0x5800 && osRom != nullptr) {
    // Self-test is at $D000-$D7FF in OS ROM file (offset 0x1000 from start)
    return osRom[addr - 0x5000 + 0x1000];
  }
  // OS ROM covers $C000-$FFFF when enabled
  // Character set is typically at $E000 (chbase=$E0)
  if (addr >= 0xC000 && osRom != nullptr) {
    // Read from OS ROM (offset from $C000)
    return osRom[addr - 0xC000];
  }
  return ram[addr];
}

void ANTIC::setModeLineParams(uint8_t mode) {
  uint8_t modeIdx = mode & 0x0F;
  scanLinesPerMode = modeParams[modeIdx].scanlines;
  uint8_t standardBytes = modeParams[modeIdx].bytesPerLine;
  isCharMode = modeParams[modeIdx].isChar;
  rowInMode = 0;

  // Determine pixels per byte based on mode
  // Modes 6,7: 20-column modes with 16-pixel wide characters (double-width)
  // Modes 2,3,4,5: 40-column modes with 8-pixel wide characters
  // Bitmap modes: 8 pixels per byte for hires, 4 for multicolor (doubled to 8)
  uint8_t pixelsPerByte;
  if (modeIdx == 6 || modeIdx == 7) {
    pixelsPerByte = 16;  // 20-column double-width character modes
  } else {
    pixelsPerByte = 8;   // Standard 40-column or bitmap modes
  }

  // Playfield width determines how many characters are DISPLAYED
  // But screen memory may still be organized at standard width (especially in ROM)
  // charsPerLine = what we render, bytesPerLine = how much to advance memScan
  uint8_t playfieldWidth = dmactl & DMACTL_PLAYFIELD;
  switch (playfieldWidth) {
    case DMACTL_NARROW:  // 0x01 - Narrow playfield
      charsPerLine = (standardBytes * 4) / 5;  // 80% (32 for mode 2, 16 for mode 7)
      bytesPerLine = standardBytes;  // Memory still organized at standard width
      memScanOffset = 0;
      // Center the display on screen
      xOffset = (ATARI_WIDTH - charsPerLine * pixelsPerByte) / 2;
      break;
    case DMACTL_STANDARD:  // 0x02 - Standard playfield
      bytesPerLine = standardBytes;
      charsPerLine = standardBytes;
      memScanOffset = 0;
      xOffset = 0;
      break;
    case DMACTL_WIDE:  // 0x03 - Wide playfield (clips at edges)
      bytesPerLine = (standardBytes * 6) / 5;  // 120%
      charsPerLine = bytesPerLine;
      memScanOffset = 0;
      xOffset = 0;  // Wide extends past edges, no offset
      break;
    default:  // 0x00 - No playfield
      bytesPerLine = 0;
      charsPerLine = 0;
      memScanOffset = 0;
      xOffset = 0;
      break;
  }
}

void ANTIC::processDisplayList() {
  if (!(dmactl & DMACTL_DL)) {
    return;
  }

  // Only fetch new instruction at start of mode line
  if (modeLineCount == 0) {
    uint8_t instruction = fetchDisplayListByte();

    // Check for DLI on this instruction
    if (instruction & MODE_DLI) {
      if (nmien & NMI_DLI) {
        dliPending = true;
        nmist &= ~NMI_DLI;
      }
    }

    hscrolEnabled = (instruction & MODE_HSCROL) != 0;
    vscrolEnabled = (instruction & MODE_VSCROL) != 0;

    uint8_t mode = instruction & 0x0F;

    // Handle blank lines (modes 0x00, 0x10, 0x20, etc.)
    if ((instruction & 0x0F) == 0 && instruction != 0) {
      // Blank line instruction
      modeLineCount = ((instruction >> 4) & 0x07) + 1;
      currentMode = 0;
      return;
    }

    // Handle jump instructions
    if (mode == 0x01) {
      // JMP or JVB
      uint8_t lo = fetchDisplayListByte();
      uint8_t hi = fetchDisplayListByte();
      displayListPC = lo | (hi << 8);

      if (instruction == MODE_JVB) {
        // Jump and wait for vertical blank
        inDisplayList = false;
        // Trigger VBI if enabled
        if (nmien & NMI_VBI) {
          vbiPending = true;
          nmist &= ~NMI_VBI;
        }
      }
      return;
    }

    // Set up mode line parameters
    setModeLineParams(mode);
    currentMode = mode;
    modeLineCount = scanLinesPerMode;

    // Check for LMS (Load Memory Scan)
    if (instruction & MODE_LMS) {
      uint8_t lo = fetchDisplayListByte();
      uint8_t hi = fetchDisplayListByte();
      memScan = lo | (hi << 8);
    }
  }
}

// First scanline of visible area in bitmap
// Standard Atari display: ~24 blank scanlines before text area
// Increasing this value shifts the captured area down (shows earlier scanlines)
static constexpr int FIRST_VISIBLE_SCANLINE = 32;

void ANTIC::drawBlankLine() {
  // Only draw to bitmap if within visible area
  int bitmapLine = scanline - FIRST_VISIBLE_SCANLINE;
  if (bitmapLine < 0 || bitmapLine >= ATARI_HEIGHT) {
    return;  // Outside bitmap bounds - don't write
  }

  // Fill scanline with background color from GTIA
  uint16_t bgColor = palette.colorToRGB565(gtia->getBackgroundColor());
  uint16_t *line = &bitmap[bitmapLine * ATARI_WIDTH];
  for (int x = 0; x < ATARI_WIDTH; x++) {
    line[x] = bgColor;
  }
}

void ANTIC::drawCharacterMode2() {
  // ANTIC Mode 2: 40 characters, 8 scanlines per char, 2 colors
  // This is BASIC GR.0 (standard text mode)
  int bitmapLine = scanline - FIRST_VISIBLE_SCANLINE;
  if (bitmapLine < 0 || bitmapLine >= ATARI_HEIGHT) {
    return;  // Outside bitmap bounds
  }

  const uint16_t *colors = palette.getAtariColors();
  // Mode 2 uses COLPF2 for background, COLPF1 luminance + COLPF2 hue for foreground
  uint8_t bgColor = gtia->getPlayfieldColor(2);  // COLPF2
  uint8_t fgColor = (gtia->getPlayfieldColor(2) & 0xF0) | (gtia->getPlayfieldColor(1) & 0x0F);

  uint16_t bgRGB = colors[bgColor];
  uint16_t fgRGB = colors[fgColor];

  uint16_t charBase = chbase << 8;
  uint16_t *line = &bitmap[bitmapLine * ATARI_WIDTH];

  // Fill entire line with background first (for narrow playfield borders)
  for (int x = 0; x < ATARI_WIDTH; x++) {
    line[x] = bgRGB;
  }

  // Scale rowInMode to character ROM row (0-7) based on mode height
  // Mode 2 = 8 scanlines, Mode 3 = 10 scanlines
  uint8_t modeHeight = modeParams[currentMode].scanlines;
  uint8_t charRow = (modeHeight > 8) ? (rowInMode * 8 / modeHeight) : rowInMode;

  // Apply character control
  bool invert = (chactl & CHACTL_INVERT) != 0;
  bool reflect = (chactl & CHACTL_REFLECT) != 0;

  if (reflect) {
    charRow = 7 - charRow;
  }

  // Start at xOffset for playfield centering (narrow playfield)
  int xpos = xOffset;
  for (int col = 0; col < charsPerLine && xpos < ATARI_WIDTH; col++) {
    // Use ROM-aware read for screen data (self-test has screen in ROM)
    uint8_t charCode = readMemWithROM((memScan + memScanOffset + col) & 0xFFFF);
    bool charInvert = (charCode & 0x80) != 0;
    charCode &= 0x7F;

    // Read character data from ROM (character set at $E000 in OS ROM)
    uint8_t charData = readMemWithROM((charBase + charCode * 8 + charRow) & 0xFFFF);

    // Apply inversion: CHACTL bit 1 enables inversion for characters with bit 7 set
    // When both conditions are true, the character displays inverted (e.g., cursor)
    if (invert && charInvert) {
      charData ^= 0xFF;
    }

    // Draw 8 pixels
    for (int bit = 7; bit >= 0 && xpos < ATARI_WIDTH; bit--) {
      if (charData & (1 << bit)) {
        line[xpos] = fgRGB;
      } else {
        line[xpos] = bgRGB;
      }
      xpos++;
    }
  }
}

void ANTIC::drawCharacterMode4() {
  // ANTIC Mode 4: 40 characters, 8 scanlines, 4 colors
  int bitmapLine = scanline - FIRST_VISIBLE_SCANLINE;
  if (bitmapLine < 0 || bitmapLine >= ATARI_HEIGHT) {
    return;  // Outside bitmap bounds
  }

  const uint16_t *colors = palette.getAtariColors();
  uint8_t colorRegs[4] = {
      gtia->getBackgroundColor(),
      gtia->getPlayfieldColor(0),
      gtia->getPlayfieldColor(1),
      gtia->getPlayfieldColor(2)};

  uint16_t charBase = chbase << 8;
  uint16_t *line = &bitmap[bitmapLine * ATARI_WIDTH];

  // Fill entire line with background first (for narrow playfield borders)
  uint16_t bgRGB = colors[colorRegs[0]];
  for (int x = 0; x < ATARI_WIDTH; x++) {
    line[x] = bgRGB;
  }

  // Scale rowInMode to character ROM row (0-7) based on mode height
  // Mode 4 = 8 scanlines, Mode 5 = 16 scanlines
  uint8_t modeHeight = modeParams[currentMode].scanlines;
  uint8_t charRow = (modeHeight > 8) ? (rowInMode * 8 / modeHeight) : rowInMode;

  // Start at xOffset for playfield centering (narrow playfield)
  int xpos = xOffset;
  for (int col = 0; col < charsPerLine && xpos < ATARI_WIDTH; col++) {
    // Use ROM-aware read for screen data (self-test has screen in ROM)
    uint8_t charCode = readMemWithROM((memScan + memScanOffset + col) & 0xFFFF);
    // Read character data from ROM
    uint8_t charData = readMemWithROM((charBase + (charCode & 0x7F) * 8 + charRow) & 0xFFFF);

    // Color selection based on high bits of character code
    uint8_t colorPair = (charCode >> 6) & 0x03;

    // Draw 8 pixels (4 color clocks, 2 bits each)
    for (int i = 0; i < 4 && xpos < ATARI_WIDTH; i++) {
      uint8_t pixelBits = (charData >> (6 - i * 2)) & 0x03;
      uint16_t color = colors[colorRegs[pixelBits]];
      line[xpos++] = color;
      line[xpos++] = color;
    }
  }
}

void ANTIC::drawCharacterMode6() {
  // ANTIC Mode 6: 20 characters, 8 scanlines, 5 colors
  int bitmapLine = scanline - FIRST_VISIBLE_SCANLINE;
  if (bitmapLine < 0 || bitmapLine >= ATARI_HEIGHT) {
    return;  // Outside bitmap bounds
  }

  const uint16_t *colors = palette.getAtariColors();

  uint16_t charBase = chbase << 8;
  uint16_t *line = &bitmap[bitmapLine * ATARI_WIDTH];

  // Fill entire line with background first (for narrow playfield borders)
  uint16_t bgRGB = colors[gtia->getBackgroundColor()];
  for (int x = 0; x < ATARI_WIDTH; x++) {
    line[x] = bgRGB;
  }

  // Scale rowInMode to character ROM row (0-7) based on mode height
  // Mode 6 = 8 scanlines, Mode 7 = 16 scanlines
  uint8_t modeHeight = modeParams[currentMode].scanlines;
  uint8_t charRow = (modeHeight > 8) ? (rowInMode * 8 / modeHeight) : rowInMode;

  // Start at xOffset for playfield centering (narrow playfield)
  int xpos = xOffset;
  for (int col = 0; col < charsPerLine && xpos < ATARI_WIDTH; col++) {
    // Use ROM-aware read for screen data (self-test has screen in ROM)
    uint8_t charCode = readMemWithROM((memScan + memScanOffset + col) & 0xFFFF);

    // Mode 6/7 uses 6-bit character codes (0-63)
    // The Altirra character set has uppercase letters at positions 0x21-0x3A,
    // which is directly accessible by mode 6/7's 6-bit codes
    uint8_t charIdx = charCode & 0x3F;

    // Read character data from ROM
    uint8_t charData = readMemWithROM((charBase + charIdx * 8 + charRow) & 0xFFFF);

    // Color selection based on bits 6-7
    uint8_t colorSelect = (charCode >> 6) & 0x03;
    uint8_t fgColor;
    switch (colorSelect) {
    case 0:
      fgColor = gtia->getPlayfieldColor(0);
      break;
    case 1:
      fgColor = gtia->getPlayfieldColor(1);
      break;
    case 2:
      fgColor = gtia->getPlayfieldColor(2);
      break;
    default:
      fgColor = gtia->getPlayfieldColor(3);
      break;
    }

    uint16_t bgRGB = colors[gtia->getBackgroundColor()];
    uint16_t fgRGB = colors[fgColor];

    // Draw 16 pixels (each character is double-width)
    for (int bit = 7; bit >= 0 && xpos < ATARI_WIDTH; bit--) {
      uint16_t pixel = (charData & (1 << bit)) ? fgRGB : bgRGB;
      line[xpos++] = pixel;
      line[xpos++] = pixel;
    }
  }
}

void ANTIC::drawBitmapModeD() {
  // ANTIC Mode D: 160x2, 4 colors (GR.7)
  int bitmapLine = scanline - FIRST_VISIBLE_SCANLINE;
  if (bitmapLine < 0 || bitmapLine >= ATARI_HEIGHT) {
    return;  // Outside bitmap bounds
  }

  const uint16_t *colors = palette.getAtariColors();
  uint8_t colorRegs[4] = {
      gtia->getBackgroundColor(),
      gtia->getPlayfieldColor(0),
      gtia->getPlayfieldColor(1),
      gtia->getPlayfieldColor(2)};

  uint16_t *line = &bitmap[bitmapLine * ATARI_WIDTH];

  // Fill entire line with background first (for narrow playfield borders)
  uint16_t bgRGB = colors[colorRegs[0]];
  for (int x = 0; x < ATARI_WIDTH; x++) {
    line[x] = bgRGB;
  }

  // Start at xOffset for playfield centering (narrow playfield)
  // Use charsPerLine for rendering (playfield width), not bytesPerLine (memory width)
  int xpos = xOffset;
  for (int byte = 0; byte < charsPerLine && xpos < ATARI_WIDTH; byte++) {
    uint8_t data = readMemWithROM((memScan + memScanOffset + byte) & 0xFFFF);

    // Each byte contains 4 pixels (2 bits each)
    for (int pixel = 0; pixel < 4 && xpos < ATARI_WIDTH; pixel++) {
      uint8_t colorIdx = (data >> (6 - pixel * 2)) & 0x03;
      uint16_t color = colors[colorRegs[colorIdx]];
      // Double-width pixels
      line[xpos++] = color;
      line[xpos++] = color;
    }
  }
}

void ANTIC::drawBitmapModeE() {
  // ANTIC Mode E: 160x1, 4 colors (GR.15)
  int bitmapLine = scanline - FIRST_VISIBLE_SCANLINE;
  if (bitmapLine < 0 || bitmapLine >= ATARI_HEIGHT) {
    return;  // Outside bitmap bounds
  }

  const uint16_t *colors = palette.getAtariColors();
  uint8_t colorRegs[4] = {
      gtia->getBackgroundColor(),
      gtia->getPlayfieldColor(0),
      gtia->getPlayfieldColor(1),
      gtia->getPlayfieldColor(2)};

  uint16_t *line = &bitmap[bitmapLine * ATARI_WIDTH];

  // Fill entire line with background first (for narrow playfield borders)
  uint16_t bgRGB = colors[colorRegs[0]];
  for (int x = 0; x < ATARI_WIDTH; x++) {
    line[x] = bgRGB;
  }

  // Start at xOffset for playfield centering (narrow playfield)
  // Use charsPerLine for rendering (playfield width), not bytesPerLine (memory width)
  int xpos = xOffset;
  for (int byte = 0; byte < charsPerLine && xpos < ATARI_WIDTH; byte++) {
    uint8_t data = readMemWithROM((memScan + memScanOffset + byte) & 0xFFFF);

    // Each byte contains 4 pixels (2 bits each)
    for (int pixel = 0; pixel < 4 && xpos < ATARI_WIDTH; pixel++) {
      uint8_t colorIdx = (data >> (6 - pixel * 2)) & 0x03;
      uint16_t color = colors[colorRegs[colorIdx]];
      // Double-width pixels
      line[xpos++] = color;
      line[xpos++] = color;
    }
  }
}

void ANTIC::drawBitmapModeF() {
  // ANTIC Mode F: 320x1, 2 colors (GR.8 hires)
  int bitmapLine = scanline - FIRST_VISIBLE_SCANLINE;
  if (bitmapLine < 0 || bitmapLine >= ATARI_HEIGHT) {
    return;  // Outside bitmap bounds
  }

  const uint16_t *colors = palette.getAtariColors();
  uint16_t bgRGB = colors[gtia->getBackgroundColor()];
  uint16_t fgRGB = colors[gtia->getPlayfieldColor(0) | (gtia->getPlayfieldColor(0) & 0x0F)];

  uint16_t *line = &bitmap[bitmapLine * ATARI_WIDTH];

  // Fill entire line with background first (for narrow playfield borders)
  for (int x = 0; x < ATARI_WIDTH; x++) {
    line[x] = bgRGB;
  }

  // Start at xOffset for playfield centering (narrow playfield)
  // Use charsPerLine for rendering (playfield width), not bytesPerLine (memory width)
  int xpos = xOffset;
  for (int byte = 0; byte < charsPerLine && xpos < ATARI_WIDTH; byte++) {
    uint8_t data = readMemWithROM((memScan + memScanOffset + byte) & 0xFFFF);

    // Each byte contains 8 pixels
    for (int bit = 7; bit >= 0 && xpos < ATARI_WIDTH; bit--) {
      line[xpos++] = (data & (1 << bit)) ? fgRGB : bgRGB;
    }
  }
}

void ANTIC::drawModeLine() {
  switch (currentMode) {
  case 0:
    drawBlankLine();
    break;
  case 2:
    drawCharacterMode2();
    break;
  case 3:
    drawCharacterMode2(); // Similar to mode 2 but taller
    break;
  case 4:
    drawCharacterMode4();
    break;
  case 5:
    drawCharacterMode4(); // Similar to mode 4 but taller
    break;
  case 6:
    drawCharacterMode6();
    break;
  case 7:
    drawCharacterMode6(); // Similar to mode 6 but taller
    break;
  case 8:
  case 9:
  case 0x0A:
  case 0x0B:
  case 0x0C:
    drawBitmapModeD(); // Simplified - use mode D rendering
    break;
  case 0x0D:
    drawBitmapModeD();
    break;
  case 0x0E:
    drawBitmapModeE();
    break;
  case 0x0F:
    drawBitmapModeF();
    break;
  default:
    drawBlankLine();
    break;
  }
}

void ANTIC::drawScanline() {
  if (scanline < 8 || scanline >= VBLANK_START) {
    // Vertical blank area - draw blank
    drawBlankLine();
    return;
  }

  if (!(dmactl & DMACTL_PLAYFIELD)) {
    // DMA disabled - blank line
    drawBlankLine();
    return;
  }

  // Process display list if needed
  if (inDisplayList) {
    processDisplayList();
  }

  // Draw the current mode line
  if (modeLineCount > 0) {
    drawModeLine();

    rowInMode++;
    modeLineCount--;

    // Advance memory scan at end of character row (for char modes)
    if (isCharMode && rowInMode >= scanLinesPerMode) {
      memScan += bytesPerLine;
    }
    // For bitmap modes, advance memory scan every line
    if (!isCharMode) {
      memScan += bytesPerLine;
    }
  } else {
    drawBlankLine();
  }
}

uint8_t ANTIC::nextScanline() {
  dmaCycles = 0;

  scanline++;

  if (scanline >= SCANLINES_PAL) {
    // End of frame
    scanline = 0;
    displayListPC = dlist;
    inDisplayList = true;
    modeLineCount = 0;
    rowInMode = 0;

    // Trigger VBI at start of vertical blank
    if (nmien & NMI_VBI) {
      vbiPending = true;
      nmist &= ~NMI_VBI;
    }
  }

  // Release WSYNC halt at end of scanline
  wsyncHalt = false;

  return dmaCycles;
}

void ANTIC::refresh() {
  static uint8_t dbgCount = 0;
  if (display) {
    display->drawBitmap(bitmap);
    // Draw border with background color (convert Atari color to RGB565)
    uint16_t borderColor = palette.colorToRGB565(gtia->getBackgroundColor());
    display->drawFrame(borderColor);

    // Debug: log ANTIC state every 50 frames (~1 second)
    if (++dbgCount >= 50) {
      dbgCount = 0;
      PlatformManager::getInstance().log(LOG_INFO, ATAG,
          "dmactl=%02X dlist=%04X chbase=%02X bg=%02X",
          dmactl, dlist, chbase, gtia->getBackgroundColor());
      PlatformManager::getInstance().log(LOG_INFO, ATAG,
          "COLPF0=%02X COLPF1=%02X COLPF2=%02X COLPF3=%02X",
          gtia->getPlayfieldColor(0), gtia->getPlayfieldColor(1),
          gtia->getPlayfieldColor(2), gtia->getPlayfieldColor(3));
      // Dump more display list bytes to see mode 7 LMS instructions
      PlatformManager::getInstance().log(LOG_INFO, ATAG,
          "DL@%04X: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
          dlist,
          readMemWithROM(dlist), readMemWithROM(dlist+1), readMemWithROM(dlist+2), readMemWithROM(dlist+3),
          readMemWithROM(dlist+4), readMemWithROM(dlist+5), readMemWithROM(dlist+6), readMemWithROM(dlist+7),
          readMemWithROM(dlist+8), readMemWithROM(dlist+9), readMemWithROM(dlist+10), readMemWithROM(dlist+11),
          readMemWithROM(dlist+12), readMemWithROM(dlist+13), readMemWithROM(dlist+14), readMemWithROM(dlist+15));
      PlatformManager::getInstance().log(LOG_INFO, ATAG,
          "DL+16: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
          readMemWithROM(dlist+16), readMemWithROM(dlist+17), readMemWithROM(dlist+18), readMemWithROM(dlist+19),
          readMemWithROM(dlist+20), readMemWithROM(dlist+21), readMemWithROM(dlist+22), readMemWithROM(dlist+23),
          readMemWithROM(dlist+24), readMemWithROM(dlist+25), readMemWithROM(dlist+26), readMemWithROM(dlist+27),
          readMemWithROM(dlist+28), readMemWithROM(dlist+29), readMemWithROM(dlist+30), readMemWithROM(dlist+31));
      // Also show memScan and chactl
      PlatformManager::getInstance().log(LOG_INFO, ATAG,
          "memScan=%04X chactl=%02X", memScan, chactl);
    }
  }
  cntRefreshs++;
}

bool ANTIC::checkDLI() {
  if (dliPending) {
    dliPending = false;
    return true;
  }
  return false;
}

bool ANTIC::checkVBI() {
  if (vbiPending) {
    vbiPending = false;
    return true;
  }
  return false;
}

void ANTIC::clearNMI(uint8_t mask) { nmist |= mask; }
