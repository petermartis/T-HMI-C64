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
#include "Atari800Emu.h"
#include "board/BoardFactory.h"
#include "fs/FileFactory.h"
#include "joystick/JoystickFactory.h"
#include "keyboard/KeyboardFactory.h"
#include "platform/PlatformFactory.h"
#include "platform/PlatformManager.h"
#include "roms/atarixl_os.h"
#include "roms/atari_basic.h"
#include <cstring>
#include <esp_log.h>
#include <Arduino.h>
#include <esp_heap_caps.h>

static const char *TAG = "Atari800Emu";

// Memory size for Atari 800 XL (64KB)
constexpr uint32_t RAM_SIZE = 64 * 1024;

Atari800Emu::Atari800Emu() : ram(nullptr), board(nullptr) {
  cntSecondsForBatteryCheck = 0;
}

Atari800Emu::~Atari800Emu() {
  if (ram) {
    delete[] ram;
    ram = nullptr;
  }
}

void Atari800Emu::intervalTimerScanKeyboardFunc() {
  sys.scanKeyboard();
}

void Atari800Emu::intervalTimerProfilingBatteryCheckFunc() {
  // Update profiling info
  if (showperfvalues.load()) {
    numofcyclespersecond.store(sys.numofcyclespersecond.load());
  }

  // Battery check every 60 seconds
  cntSecondsForBatteryCheck++;
  if (cntSecondsForBatteryCheck >= 60) {
    cntSecondsForBatteryCheck = 0;
    if (board) {
      uint16_t voltage = board->getBatteryVoltage();
      if (voltage > 0 && voltage < 3300) {
        // Low battery warning - could display overlay
        PlatformManager::getInstance().log(LOG_WARN, TAG, "Low battery: %dmV", voltage);
      }
    }
  }
}

void Atari800Emu::cpuCode(void *parameter) {
  PlatformManager::getInstance().log(LOG_INFO, TAG, "cpuCode starting, PC=%04X", sys.getPC());
  vTaskDelay(pdMS_TO_TICKS(50)); // flush log before entering run loop
  sys.run();
  PlatformManager::getInstance().log(LOG_ERROR, TAG, "CPU task ended unexpectedly!");
}

void Atari800Emu::setup() {
  ESP_LOGI(TAG, "DBG: setup() entry");

  // Initialize platform first
  ESP_LOGI(TAG, "DBG: creating platform");
  PlatformManager::initialize(PlatformNS::create());
  ESP_LOGI(TAG, "DBG: platform created");

  ESP_LOGI(TAG, "Atari 800 XL Emulator starting...");
  ESP_LOGI(TAG, "DBG: after first log");

  // Initialize board driver
  ESP_LOGI(TAG, "DBG: about to create board");
  board = Board::create();
  ESP_LOGI(TAG, "DBG: board created");
  if (board) {
    ESP_LOGI(TAG, "DBG: calling board->init()");
    board->init();
    ESP_LOGI(TAG, "DBG: board->init() done");
  }
  ESP_LOGI(TAG, "Board initialized");

  // Allocate RAM in PSRAM (external memory) to save internal RAM for task stacks
  // Try ps_malloc first (Arduino's PSRAM allocator), fallback to heap_caps, then internal
  PlatformManager::getInstance().log(LOG_INFO, TAG, "Allocating RAM (64KB), trying PSRAM...");
  ram = (uint8_t*)ps_malloc(RAM_SIZE);
  if (!ram) {
    PlatformManager::getInstance().log(LOG_WARN, TAG, "ps_malloc failed, trying heap_caps...");
    ram = (uint8_t*)heap_caps_malloc(RAM_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  }
  if (!ram) {
    // Fallback to internal RAM if PSRAM not available
    PlatformManager::getInstance().log(LOG_WARN, TAG, "PSRAM not available, using internal RAM");
    ram = new uint8_t[RAM_SIZE];
  } else {
    PlatformManager::getInstance().log(LOG_INFO, TAG, "RAM allocated in PSRAM");
  }
  PlatformManager::getInstance().log(LOG_INFO, TAG, "RAM at %p", (void*)ram);
  memset(ram, 0, RAM_SIZE);

  // Initialize system with ROMs (use getters to get initialized ROM data)
  PlatformManager::getInstance().log(LOG_INFO, TAG, "Getting OS ROM...");
  const uint8_t* osRom = getAtariOSRom();
  PlatformManager::getInstance().log(LOG_INFO, TAG, "OS ROM at %p", (void*)osRom);
  const uint8_t* basicRom = getAtariBasicRom();
  PlatformManager::getInstance().log(LOG_INFO, TAG, "BASIC ROM at %p", (void*)basicRom);
  PlatformManager::getInstance().log(LOG_INFO, TAG, "Calling sys.init()...");
  sys.init(ram, osRom, basicRom);
  PlatformManager::getInstance().log(LOG_INFO, TAG, "System initialized, PC=%04X", sys.getPC());

  // Create keyboard driver
  PlatformManager::getInstance().log(LOG_INFO, TAG, "Creating keyboard...");
  sys.keyboard = Keyboard::create();
  if (sys.keyboard) {
    sys.keyboard->init();
  }
  PlatformManager::getInstance().log(LOG_INFO, TAG, "Keyboard done");

  // Create joystick driver
  PlatformManager::getInstance().log(LOG_INFO, TAG, "Creating joystick...");
  JoystickDriver *joystick = Joystick::create();
  if (joystick) {
    joystick->init();
    sys.setJoystick(joystick);
  }
  PlatformManager::getInstance().log(LOG_INFO, TAG, "Joystick done");

  // Initialize file system and loader
  PlatformManager::getInstance().log(LOG_INFO, TAG, "Creating file system...");
  fs = FileSys::create();
  if (fs) {
    fs->init();
    loader = std::make_unique<AtariLoader>(ram, fs.get());
    PlatformManager::getInstance().log(LOG_INFO, TAG, "File system and loader initialized");
  } else {
    PlatformManager::getInstance().log(LOG_WARN, TAG, "No file system available");
  }

  // Start CPU task on core 1
  PlatformManager::getInstance().log(LOG_INFO, TAG, "Starting CPU task on core 1...");
  PlatformManager::getInstance().startTask(
      [this](void *param) { this->cpuCode(param); },
      1,  // Core 1
      5   // Priority
  );
  PlatformManager::getInstance().log(LOG_INFO, TAG, "CPU task created");
  vTaskDelay(pdMS_TO_TICKS(100)); // give CPU task time to start

  // Start keyboard scanner timer (every 8ms)
  ESP_LOGI(TAG, "Starting keyboard timer...");
  PlatformManager::getInstance().startIntervalTimer(
      [this]() { this->intervalTimerScanKeyboardFunc(); },
      8000  // 8ms
  );

  // Start profiling/battery timer (every 1 second)
  ESP_LOGI(TAG, "Starting battery timer...");
  PlatformManager::getInstance().startIntervalTimer(
      [this]() { this->intervalTimerProfilingBatteryCheckFunc(); },
      1000000  // 1 second
  );

  PlatformManager::getInstance().log(LOG_INFO, TAG, "Setup complete");
}

void Atari800Emu::loop() {
  static uint32_t loopCount = 0;

  // Debug: log loop entry every 50 calls
  if (++loopCount % 50 == 0) {
    PlatformManager::getInstance().log(LOG_INFO, TAG, "loop() #%lu, refreshs=%d", loopCount, sys.antic.cntRefreshs.load());
  }

  // Handle pending file load requests
  if (loadFileRequested.load()) {
    loadFileRequested.store(false);
    loadFile(pendingLoadFile);
  }

  // Handle external commands from keyboard
  handleExternalCommands();

  // Main loop - refresh display
  sys.antic.refresh();

  // Feed watchdog
  PlatformManager::getInstance().feedWDT();

  // Small delay
  PlatformManager::getInstance().waitMS(Config::REFRESHDELAY);

  // Update refresh counter
  cntRefreshs.store(sys.antic.cntRefreshs.load());
}

void Atari800Emu::handleExternalCommands() {
  if (!sys.keyboard) return;

  uint8_t *extCmd = sys.keyboard->getExtCmdData();
  if (!extCmd) return;

  ExtCmd cmd = static_cast<ExtCmd>(extCmd[0]);

  switch (cmd) {
  case ExtCmd::LOAD:
    // Show file list and load first found XEX file (simplified)
    {
      auto files = listFiles();
      if (!files.empty()) {
        PlatformManager::getInstance().log(LOG_INFO, TAG, "Found %zu files, loading first", files.size());
        loadFile(files[0]);
      } else {
        PlatformManager::getInstance().log(LOG_WARN, TAG, "No Atari files found on SD card");
      }
    }
    break;

  case ExtCmd::RESET:
    sys.reset();
    break;

  default:
    // Other commands not handled yet
    break;
  }
}

bool Atari800Emu::loadFile(const std::string &filename) {
  if (!loader) {
    PlatformManager::getInstance().log(LOG_ERROR, TAG, "No loader available");
    return false;
  }

  PlatformManager::getInstance().log(LOG_INFO, TAG, "Loading file: %s", filename.c_str());

  AtariLoader::LoadResult result = loader->loadExecutable(filename);

  if (!result.success) {
    PlatformManager::getInstance().log(LOG_ERROR, TAG, "Load failed: %s", result.errorMessage.c_str());
    return false;
  }

  // If we have a run address, set PC to it
  if (result.runAddress != 0) {
    PlatformManager::getInstance().log(LOG_INFO, TAG, "Setting PC to run address $%04X", result.runAddress);
    sys.setPC(result.runAddress);
  }

  PlatformManager::getInstance().log(LOG_INFO, TAG, "Load complete");
  return true;
}

bool Atari800Emu::mountATR(const std::string &filename) {
  if (!loader) return false;
  return loader->mountATR(filename);
}

void Atari800Emu::unmountATR() {
  if (loader) loader->unmountATR();
}

std::vector<std::string> Atari800Emu::listFiles() {
  if (!loader) return {};
  return loader->listFiles();
}

void Atari800Emu::requestLoadFile(const std::string &filename) {
  pendingLoadFile = filename;
  loadFileRequested.store(true);
}
