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
#include "joystick/JoystickFactory.h"
#include "keyboard/KeyboardFactory.h"
#include "platform/PlatformFactory.h"
#include "platform/PlatformManager.h"
#include "roms/atarixl_os.h"
#include "roms/atari_basic.h"
#include <cstring>
#include <esp_log.h>
#include <Arduino.h>

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
  sys.run();
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

  // Allocate RAM
  ESP_LOGI(TAG, "Allocating RAM (64KB)...");
  ram = new uint8_t[RAM_SIZE];
  ESP_LOGI(TAG, "RAM allocated at %p", (void*)ram);
  memset(ram, 0, RAM_SIZE);
  ESP_LOGI(TAG, "RAM cleared");

  // Initialize system with ROMs (use getters to get initialized ROM data)
  PlatformManager::getInstance().log(LOG_INFO, TAG, "Getting OS ROM...");
  const uint8_t* osRom = getAtariOSRom();
  PlatformManager::getInstance().log(LOG_INFO, TAG, "OS ROM at %p", (void*)osRom);
  PlatformManager::getInstance().log(LOG_INFO, TAG, "Getting BASIC ROM...");
  const uint8_t* basicRom = getAtariBasicRom();
  PlatformManager::getInstance().log(LOG_INFO, TAG, "BASIC ROM at %p", (void*)basicRom);
  PlatformManager::getInstance().log(LOG_INFO, TAG, "Calling sys.init()...");
  sys.init(ram, osRom, basicRom);
  PlatformManager::getInstance().log(LOG_INFO, TAG, "System initialized");

  // Create keyboard driver
  ESP_LOGI(TAG, "Creating keyboard...");
  sys.keyboard = Keyboard::create();
  if (sys.keyboard) {
    ESP_LOGI(TAG, "Initializing keyboard...");
    sys.keyboard->init();
  }
  ESP_LOGI(TAG, "Keyboard done");

  // Create joystick driver
  ESP_LOGI(TAG, "Creating joystick...");
  JoystickDriver *joystick = Joystick::create();
  if (joystick) {
    ESP_LOGI(TAG, "Initializing joystick...");
    joystick->init();
    sys.setJoystick(joystick);
  }
  ESP_LOGI(TAG, "Joystick done");

  // Start CPU task on core 1
  ESP_LOGI(TAG, "Starting CPU task on core 1...");
  PlatformManager::getInstance().startTask(
      [this](void *param) { this->cpuCode(param); },
      1,  // Core 1
      5   // Priority
  );
  ESP_LOGI(TAG, "CPU task started");

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

  // Main loop - refresh display
  sys.antic.refresh();

  // Feed watchdog
  PlatformManager::getInstance().feedWDT();

  // Small delay
  PlatformManager::getInstance().waitMS(Config::REFRESHDELAY);

  // Update refresh counter
  cntRefreshs.store(sys.antic.cntRefreshs.load());
}
