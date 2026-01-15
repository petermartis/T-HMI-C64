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
#ifndef ATARI800EMU_H
#define ATARI800EMU_H

#include "Atari800Sys.h"
#include "AtariLoader.h"
#include "board/BoardDriver.h"
#include "fs/FileDriver.h"
#include <atomic>
#include <memory>
#include <string>
#include <vector>

/**
 * @brief Main Atari 800 Emulator class
 *
 * This class orchestrates the emulator:
 * - Initializes hardware drivers
 * - Allocates memory
 * - Sets up the emulation core
 * - Manages the main loop and display refresh
 * - Handles file loading from SD card
 */
class Atari800Emu {
private:
  uint8_t *ram;
  BoardDriver *board;
  std::unique_ptr<FileDriver> fs;
  std::unique_ptr<AtariLoader> loader;
  uint16_t cntSecondsForBatteryCheck;

  // Pending file to load (set from web interface, loaded in main loop)
  std::string pendingLoadFile;
  std::atomic<bool> loadFileRequested{false};

  void intervalTimerScanKeyboardFunc();
  void intervalTimerProfilingBatteryCheckFunc();
  void cpuCode(void *parameter);
  void handleExternalCommands();

public:
  Atari800Sys sys;
  std::atomic<bool> showperfvalues = false;
  std::atomic<uint8_t> cntRefreshs = 0;
  std::atomic<uint32_t> numofcyclespersecond = 0;

  Atari800Emu();
  ~Atari800Emu();

  void setup();
  void loop();

  // File loading interface
  bool loadFile(const std::string &filename);
  bool mountATR(const std::string &filename);
  void unmountATR();
  std::vector<std::string> listFiles();

  // Request file load from another thread (e.g., web interface)
  void requestLoadFile(const std::string &filename);

  // Access to loader for SIO emulation
  AtariLoader *getLoader() { return loader.get(); }
};

#endif // ATARI800EMU_H
