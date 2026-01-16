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
    // Generate Atari PAL or NTSC palette
    // Color format: HHHHLLLL where H=hue (0-15), L=luminance (0-15)
    //
    // PAL:  Phase offset ~+30 degrees, more saturated colors
    // NTSC: Phase offset ~-58 degrees, slightly desaturated

    // Phase offset and saturation depend on system type
    float phaseOffset = isPAL ? 30.0f : -58.0f;
    float saturation = isPAL ? 0.38f : 0.32f;

    for (int color = 0; color < 256; color++) {
      int hue = (color >> 4) & 0x0F;
      int lum = color & 0x0F;

      // Convert to RGB
      float y = lum / 15.0f;  // Luminance 0-1
      float r, g, b;

      if (hue == 0) {
        // Grayscale
        r = g = b = y;
      } else {
        // Atari color phase calculation
        // ~25.7 degrees per hue step (360/14 for 14 active hues)
        float angle = ((hue - 1) * 25.7f + phaseOffset) * 3.14159f / 180.0f;

        // YIQ to RGB conversion
        float i = saturation * cosf(angle);
        float q = saturation * sinf(angle);

        r = y + 0.956f * i + 0.621f * q;
        g = y - 0.272f * i - 0.647f * q;
        b = y - 1.105f * i + 1.702f * q;

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
