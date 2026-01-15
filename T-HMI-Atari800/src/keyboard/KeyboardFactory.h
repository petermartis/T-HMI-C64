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
#ifndef KEYBOARDFACTORY_H
#define KEYBOARDFACTORY_H

#include "../Config.h"
#include "KeyboardDriver.h"

// Temporarily disable WiFi keyboard to test display
#define SKIP_WIFI_KEYBOARD 1

#if defined(SKIP_WIFI_KEYBOARD)
// Minimal stub keyboard - no WiFi blocking
class NoKeyboard : public KeyboardDriver {
public:
  void init() override {}
  void scanKeyboard() override {}
  uint8_t getKBCodeDC01() override { return 0xFF; }
  uint8_t getKBCodeDC00() override { return 0xFF; }
  uint8_t getShiftctrlcode() override { return 0; }
  uint8_t getKBJoyValue() override { return 0xFF; }
  uint8_t *getExtCmdData() override { return nullptr; }
  void sendExtCmdNotification(uint8_t *data, size_t size) override {}
  void setDetectReleasekey(bool detectreleasekey) override {}
};
#elif defined(USE_BLE_KEYBOARD)
#include "BLEKB.h"
#elif defined(USE_SDL_KEYBOARD)
#include "SDLKB.h"
#elif defined(USE_WEB_KEYBOARD)
#include "WebKB.h"
#else
#error "no valid keyboard driver defined"
#endif

namespace Keyboard {
KeyboardDriver *create() {
#if defined(SKIP_WIFI_KEYBOARD)
  return new NoKeyboard();
#elif defined(USE_BLE_KEYBOARD)
  return new BLEKB();
#elif defined(USE_SDL_KEYBOARD)
  return new SDLKB();
#elif defined(USE_WEB_KEYBOARD)
  return new WebKB(80);
#endif
}
} // namespace Keyboard

#endif // KEYBOARDFACTORY_H
