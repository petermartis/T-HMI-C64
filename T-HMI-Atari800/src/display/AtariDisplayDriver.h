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
#ifndef ATARIDISPLAYDRIVER_H
#define ATARIDISPLAYDRIVER_H

#include "DisplayDriver.h"
#include <cmath>

/**
 * @brief Atari palette helper for display drivers.
 *
 * Provides Atari-specific 256-color palette support. The Atari 800 uses
 * a 256-color palette with 16 hues x 16 luminances.
 *
 * This class does NOT inherit from DisplayDriver - instead it provides
 * palette conversion utilities that can be used alongside any DisplayDriver.
 */
class AtariPalette {
private:
  // Atari 800 NTSC palette (256 colors in RGB565 format)
  uint16_t atariColors[256];
  bool initialized;

  void generatePalette(bool isPAL = true) {
    // Generate Atari PAL or NTSC palette using HSL color model
    // Color format: HHHHLLLL where H=hue (0-15), L=luminance (0-15)
    //
    // The Atari color wheel maps hues as follows:
    // 0=gray, 1-2=orange/gold, 3-4=red, 5-6=pink/magenta, 7-8=purple/violet,
    // 9-10=blue, 11-12=cyan/turquoise, 13-14=green, 15=yellow-green

    for (int color = 0; color < 256; color++) {
      int hue = (color >> 4) & 0x0F;
      int lum = color & 0x0F;

      float r, g, b;
      float lightness = lum / 15.0f;

      if (hue == 0) {
        // Grayscale
        r = g = b = lightness;
      } else {
        // Map Atari hue (1-15) to HSL hue angle (0-360)
        // Atari hue 1 = orange (~30°), going through spectrum
        // Hue 9 should be blue (~240°)
        // Formula: starting at orange (30°), each step adds ~24° (360/15)
        float hslHue;
        if (isPAL) {
          // PAL: Hue 9 = blue, so we need offset to make that happen
          // (9-1) * 24 + offset = 240 => 192 + offset = 240 => offset = 48
          hslHue = (hue - 1) * 24.0f + 48.0f;
        } else {
          // NTSC: slightly different color wheel
          hslHue = (hue - 1) * 24.0f + 30.0f;
        }
        if (hslHue >= 360.0f) hslHue -= 360.0f;

        // HSL to RGB conversion
        float h = hslHue / 360.0f;
        float s = 0.7f;  // Saturation
        float l = 0.15f + lightness * 0.7f;  // Map luminance 0-15 to 0.15-0.85

        float c = (1.0f - fabsf(2.0f * l - 1.0f)) * s;
        float x = c * (1.0f - fabsf(fmodf(h * 6.0f, 2.0f) - 1.0f));
        float m = l - c / 2.0f;

        int hueSegment = (int)(h * 6.0f) % 6;
        switch (hueSegment) {
          case 0: r = c; g = x; b = 0; break;
          case 1: r = x; g = c; b = 0; break;
          case 2: r = 0; g = c; b = x; break;
          case 3: r = 0; g = x; b = c; break;
          case 4: r = x; g = 0; b = c; break;
          default: r = c; g = 0; b = x; break;
        }
        r += m; g += m; b += m;

        // Clamp values
        if (r < 0.0f) r = 0.0f; if (r > 1.0f) r = 1.0f;
        if (g < 0.0f) g = 0.0f; if (g > 1.0f) g = 1.0f;
        if (b < 0.0f) b = 0.0f; if (b > 1.0f) b = 1.0f;
      }

      // Convert to RGB565
      uint8_t r5 = (uint8_t)(r * 31.0f);
      uint8_t g6 = (uint8_t)(g * 63.0f);
      uint8_t b5 = (uint8_t)(b * 31.0f);

      atariColors[color] = (r5 << 11) | (g6 << 5) | b5;
    }
    initialized = true;
  }

public:
  AtariPalette() : initialized(false) {
    // Don't generate palette in constructor - defer to init()
    // This avoids floating point math during static initialization
    for (int i = 0; i < 256; i++) {
      atariColors[i] = 0;
    }
  }

  /**
   * @brief Initialize the palette. Must be called before use.
   */
  void init() {
    if (!initialized) {
      generatePalette();
    }
  }

  /**
   * @brief Provides access to the Atari palette in RGB565 format.
   */
  const uint16_t *getAtariColors() const { return atariColors; }

  /**
   * @brief Convert Atari color index to RGB565.
   */
  uint16_t colorToRGB565(uint8_t colorIndex) const {
    return atariColors[colorIndex];
  }
};

// For backward compatibility, provide AtariDisplayDriver as an alias
// Note: Display drivers (ST7789V, etc.) inherit from DisplayDriver directly
using AtariDisplayDriver = DisplayDriver;

#endif // ATARIDISPLAYDRIVER_H
