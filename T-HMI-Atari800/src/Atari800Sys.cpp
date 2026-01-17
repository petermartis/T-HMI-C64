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
#include "Atari800Sys.h"
#include "platform/PlatformManager.h"
#include "roms/atarixl_os.h"
#include <cstring>

// Cycles per scanline (PAL: 114 cycles at 1.77MHz)
constexpr int32_t CYCLES_PER_SCANLINE = 114;
constexpr int32_t SCANLINES_PER_FRAME = 312;  // PAL

Atari800Sys::Atari800Sys()
    : ram(nullptr), osRom(nullptr), basicRom(nullptr), charRom(nullptr),
      joystick(nullptr), keyboard(nullptr) {
  osRomEnabled = true;
  basicRomEnabled = true;
  selfTestEnabled = false;
  nmiActive = false;
  lastIRQ = 0;
  cyclesThisScanline = 0;
  cyclesPerScanline = CYCLES_PER_SCANLINE;
  numofcyclespersecond = 0;
  perf = false;
}

Atari800Sys::~Atari800Sys() {
  // RAM is managed externally
}

void Atari800Sys::init(uint8_t *ram, const uint8_t *osRom, const uint8_t *basicRom) {
  this->ram = ram;
  this->osRom = osRom;
  this->basicRom = basicRom;

  // Character ROM is built into OS ROM at offset $E000 (relative to $C000)
  // In the actual OS ROM it's at $E000-$E3FF
  this->charRom = osRom + 0x2000;  // $E000 - $C000 = $2000

  // Copy display list to RAM at $0600 (where the boot code expects it)
  size_t dlSize = 0;
  const uint8_t* displayList = getDisplayList(&dlSize);
  if (displayList && dlSize > 0) {
    memcpy(ram + 0x0600, displayList, dlSize);
  }

  // Copy screen text to RAM at $0640 (where the display list points)
  size_t txtSize = 0;
  const uint8_t* screenText = getScreenText(&txtSize);
  if (screenText && txtSize > 0) {
    memcpy(ram + 0x0640, screenText, txtSize);
  }

  // Copy character ROM to RAM at $E000 (ANTIC reads character data from RAM)
  // Boot code sets CHBASE=$E0 which means character base at $E000
  size_t charSize = 0;
  const uint8_t* charRom = getCharacterRom(&charSize);
  if (charRom && charSize > 0) {
    memcpy(ram + 0xE000, charRom, charSize);
  }

  // Initialize chips
  antic.init(ram, osRom, &gtia);
  pokey.init();
  gtia.reset();
  pia.reset();

  reset();
}

void Atari800Sys::reset() {
  // Reset CPU
  cpuhalted = false;
  numofcycles = 0;

  // Initialize flags
  cflag = false;
  zflag = false;
  dflag = false;
  bflag = false;
  vflag = false;
  nflag = false;
  iflag = true;  // Interrupts disabled on reset

  // Initialize registers
  a = 0;
  x = 0;
  y = 0;
  sp = 0xFF;

  // Reset chips
  antic.reset();
  gtia.reset();
  pokey.reset();
  pia.reset();

  // Sync banking state with PIA's initial PORTB value
  // PIA resets with portb = 0xFC (OS and BASIC enabled, self-test disabled)
  updateBanking();

  // Read reset vector from OS ROM
  pc = osRom[0x3FFC - 0x0000] | (osRom[0x3FFD - 0x0000] << 8);

  nmiActive = false;
  cyclesThisScanline = 0;
}

void Atari800Sys::updateBanking() {
  uint8_t portb = pia.getPortB();
  bool wasOsEnabled = osRomEnabled;
  bool wasBasicEnabled = basicRomEnabled;
  osRomEnabled = (portb & PORTB_OS_ROM) == 0;
  basicRomEnabled = (portb & PORTB_BASIC) == 0;
  selfTestEnabled = (portb & PORTB_SELFTEST) == 0;

  // Update TRIG3 to reflect BASIC/cartridge state
  // On XL/XE, TRIG3=0 means cartridge/BASIC present, TRIG3=1 means not present
  gtia.setCartridgePresent(basicRomEnabled);

  // Debug: log when banking changes
  if (wasOsEnabled != osRomEnabled || wasBasicEnabled != basicRomEnabled) {
    static const char* TAG = "BANK";
    PlatformManager::getInstance().log(LOG_INFO, TAG, "PORTB=%02X osRom=%d basic=%d->%d self=%d TRIG3=%d",
        portb, osRomEnabled, wasBasicEnabled, basicRomEnabled, selfTestEnabled, basicRomEnabled ? 0 : 1);
  }
}

uint8_t Atari800Sys::getMem(uint16_t addr) {
  // RAM area ($0000-$BFFF with holes for ROM)
  if (addr < 0xA000) {
    // Check for self-test ROM area ($5000-$57FF)
    if (selfTestEnabled && addr >= 0x5000 && addr < 0x5800) {
      return osRom[addr - 0x5000 + 0x1000];  // Self-test at $D000 in OS ROM
    }
    return ram[addr];
  }

  // BASIC ROM area ($A000-$BFFF)
  if (addr < 0xC000) {
    if (basicRomEnabled && basicRom) {
      uint8_t val = basicRom[addr - 0xA000];

      // Patch BASIC ROM cartridge header for proper XL boot
      // The ROM has FLAGS=$00 and RUN=$0500 which doesn't work
      if (addr == 0xBFFA) {
        // FLAGS byte: bit 2 set = cartridge wants to run
        // Original BASIC has FLAGS=$00, but XL OS needs bit 2 set
        if (val == 0x00) {
          static bool flagsLogged = false;
          if (!flagsLogged) {
            static const char* TAG = "BASIC";
            PlatformManager::getInstance().log(LOG_WARN, TAG,
                "Patching FLAGS: $00 -> $04 (enable cartridge run)");
            flagsLogged = true;
          }
          val = 0x04;  // Set bit 2: cartridge wants to run
        }
      }

      // Patch RUN vector to $A000 if it's wrong
      // Some ROM dumps have RUN=$0500 instead of $A000
      if (addr == 0xBFFD) {
        uint8_t runHi = basicRom[0x1FFD];
        if (runHi != 0xA0) {
          static bool patchLogged = false;
          if (!patchLogged) {
            static const char* TAG = "BASIC";
            PlatformManager::getInstance().log(LOG_WARN, TAG,
                "Patching RUN vector: $%02X00 -> $A000", runHi);
            patchLogged = true;
          }
          val = 0xA0;  // Patch RUN_HI for BASIC cold start
        }
      }

      // Debug: log reads of cartridge vectors ($BFFA-$BFFF)
      static uint8_t cartVecReadCount = 0;
      if (addr >= 0xBFFA && cartVecReadCount < 30) {
        static const char* TAG = "CARTVEC";
        const char* vecName = "";
        switch (addr) {
          case 0xBFFA: vecName = "FLAGS"; break;
          case 0xBFFB: vecName = "RESERVED"; break;
          case 0xBFFC: vecName = "RUN_LO"; break;
          case 0xBFFD: vecName = "RUN_HI"; break;
          case 0xBFFE: vecName = "INIT_LO"; break;
          case 0xBFFF: vecName = "INIT_HI"; break;
        }
        PlatformManager::getInstance().log(LOG_INFO, TAG, "Read $%04X (%s) = $%02X from PC=$%04X",
            addr, vecName, val, pc);
        cartVecReadCount++;
      }
      return val;
    }
    // BASIC disabled - reading from RAM
    static uint8_t basicDisabledReadCount = 0;
    if (addr >= 0xBFFA && basicDisabledReadCount < 10) {
      static const char* TAG = "CARTVEC";
      PlatformManager::getInstance().log(LOG_INFO, TAG, "Read $%04X from RAM (BASIC disabled) = $%02X", addr, ram[addr]);
      basicDisabledReadCount++;
    }
    return ram[addr];
  }

  // Hardware I/O area ($D000-$D7FF)
  if (addr >= 0xD000 && addr < 0xD800) {
    return readIO(addr);
  }

  // OS ROM area ($C000-$CFFF, $D800-$FFFF)
  if (osRomEnabled && osRom) {
    if (addr >= 0xC000 && addr < 0xD000) {
      return osRom[addr - 0xC000];
    }
    if (addr >= 0xD800) {
      return osRom[addr - 0xC000];
    }
  }

  // Fall back to RAM
  return ram[addr];
}

void Atari800Sys::setMem(uint16_t addr, uint8_t val) {
  // RAM always writable in all regions
  // The OS can write to RAM under ROM at any time

  // Debug: Log writes to ROM-shadowed area
  static uint32_t writeCount = 0;
  if (addr >= 0xC000 && addr < 0xD000) {
    if (writeCount < 50) {
      static const char* TAG = "WMEM";
      PlatformManager::getInstance().log(LOG_INFO, TAG, "Write $%04X = $%02X (C-region)", addr, val);
      writeCount++;
    }
  }
  if (addr >= 0xD800) {
    if (writeCount < 50) {
      static const char* TAG = "WMEM";
      PlatformManager::getInstance().log(LOG_INFO, TAG, "Write $%04X = $%02X (D8-region)", addr, val);
      writeCount++;
    }
  }

  if (addr < 0xA000) {
    // Low RAM ($0000-$9FFF)

    // Debug: trace writes to DOSVEC ($000A-$000B) and DOSINI ($000C-$000D)
    static uint8_t vecWriteCount = 0;
    if (addr >= 0x000A && addr <= 0x000D && vecWriteCount < 40) {
      static const char* TAG = "DOSVEC";
      const char* vecName = "";
      switch (addr) {
        case 0x000A: vecName = "DOSVEC_LO"; break;
        case 0x000B: vecName = "DOSVEC_HI"; break;
        case 0x000C: vecName = "DOSINI_LO"; break;
        case 0x000D: vecName = "DOSINI_HI"; break;
      }
      PlatformManager::getInstance().log(LOG_INFO, TAG, "Write %s ($%04X) = $%02X from PC=$%04X",
          vecName, addr, val, pc);
      vecWriteCount++;
    }

    // Debug: trace writes to screen memory area (around $9C40)
    static uint32_t screenWriteCount = 0;
    if (addr >= 0x9C40 && addr < 0xA000 && screenWriteCount < 30) {
      static const char* TAG = "SCREEN";
      PlatformManager::getInstance().log(LOG_INFO, TAG, "Write screen $%04X = $%02X '%c'",
          addr, val, (val >= 0x20 && val < 0x7F) ? (val) : '.');
      screenWriteCount++;
    }
    ram[addr] = val;
    return;
  }

  if (addr < 0xC000) {
    // BASIC ROM area ($A000-$BFFF) - write to underlying RAM
    ram[addr] = val;
    return;
  }

  if (addr < 0xD000) {
    // OS ROM area ($C000-$CFFF) - write to underlying RAM
    ram[addr] = val;
    return;
  }

  if (addr < 0xD800) {
    // Hardware I/O area ($D000-$D7FF)
    writeIO(addr, val);
    return;
  }

  // OS ROM area ($D800-$FFFF) - write to underlying RAM
  ram[addr] = val;
}

uint8_t Atari800Sys::readIO(uint16_t addr) {
  uint8_t reg = addr & 0xFF;

  // GTIA: $D000-$D0FF (mirrored every 32 bytes)
  if (addr >= 0xD000 && addr < 0xD100) {
    return gtia.read(reg & 0x1F);
  }

  // POKEY: $D200-$D2FF (mirrored every 16 bytes)
  if (addr >= 0xD200 && addr < 0xD300) {
    return pokey.read(reg & 0x0F);
  }

  // PIA: $D300-$D3FF (mirrored every 4 bytes)
  if (addr >= 0xD300 && addr < 0xD400) {
    return pia.read(reg & 0x03);
  }

  // ANTIC: $D400-$D4FF (mirrored every 16 bytes)
  if (addr >= 0xD400 && addr < 0xD500) {
    return antic.read(reg & 0x0F);
  }

  // Unused/cartridge area
  return 0xFF;
}

void Atari800Sys::writeIO(uint16_t addr, uint8_t val) {
  uint8_t reg = addr & 0xFF;

  // GTIA: $D000-$D0FF
  if (addr >= 0xD000 && addr < 0xD100) {
    gtia.write(reg & 0x1F, val);
    return;
  }

  // POKEY: $D200-$D2FF
  if (addr >= 0xD200 && addr < 0xD300) {
    pokey.write(reg & 0x0F, val);
    return;
  }

  // PIA: $D300-$D3FF
  if (addr >= 0xD300 && addr < 0xD400) {
    pia.write(reg & 0x03, val);
    // Check for banking changes
    updateBanking();
    return;
  }

  // ANTIC: $D400-$D4FF
  if (addr >= 0xD400 && addr < 0xD500) {
    antic.write(reg & 0x0F, val);
    return;
  }
}

void Atari800Sys::checkInterrupts() {
  // Check for NMI (from ANTIC)
  if (antic.checkVBI() || antic.checkDLI()) {
    handleNMI();
  }

  // Check for IRQ (from POKEY)
  if (!iflag && pokey.checkIRQ()) {
    handleIRQ();
  }
}

bool Atari800Sys::handleNMI() {
  if (nmiActive) {
    return false;
  }

  nmiActive = true;

  // Push PC and status
  pushtostack(pc >> 8);
  pushtostack(pc & 0xFF);
  uint8_t status = 0x20;  // Unused bit always set
  if (nflag) status |= 0x80;
  if (vflag) status |= 0x40;
  if (bflag) status |= 0x10;
  if (dflag) status |= 0x08;
  if (iflag) status |= 0x04;
  if (zflag) status |= 0x02;
  if (cflag) status |= 0x01;
  pushtostack(status);

  // Read NMI vector
  pc = getMem(0xFFFA) | (getMem(0xFFFB) << 8);
  iflag = true;
  numofcycles += 7;

  return true;
}

bool Atari800Sys::handleIRQ() {
  if (iflag) {
    return false;
  }

  // Push PC and status
  pushtostack(pc >> 8);
  pushtostack(pc & 0xFF);
  uint8_t status = 0x20;
  if (nflag) status |= 0x80;
  if (vflag) status |= 0x40;
  // B flag clear for IRQ
  if (dflag) status |= 0x08;
  if (iflag) status |= 0x04;
  if (zflag) status |= 0x02;
  if (cflag) status |= 0x01;
  pushtostack(status);

  // Read IRQ vector
  pc = getMem(0xFFFE) | (getMem(0xFFFF) << 8);
  iflag = true;
  numofcycles += 7;

  return true;
}

void Atari800Sys::cmd6502brk() {
  // BRK instruction - software interrupt
  uint16_t brkPC = pc - 1;  // PC already incremented when we fetched the opcode
  pc++;  // Skip padding byte (BRK pushes PC+2)

  pushtostack(pc >> 8);
  pushtostack(pc & 0xFF);

  uint8_t status = 0x30;  // B flag set for BRK
  if (nflag) status |= 0x80;
  if (vflag) status |= 0x40;
  if (dflag) status |= 0x08;
  if (iflag) status |= 0x04;
  if (zflag) status |= 0x02;
  if (cflag) status |= 0x01;
  pushtostack(status);

  uint8_t vecLo = getMem(0xFFFE);
  uint8_t vecHi = getMem(0xFFFF);
  pc = vecLo | (vecHi << 8);

  // Debug: Log BRK vector read
  static const char* TAG = "BRK";
  PlatformManager::getInstance().log(LOG_INFO, TAG, "BRK at $%04X -> vec=$%02X%02X osRom=%d portb=%02X",
      brkPC, vecHi, vecLo, osRomEnabled, pia.getPortB());

  iflag = true;
  numofcycles = 7;
}

void Atari800Sys::logDebugInfo() {
  // Debug logging if enabled
}

void Atari800Sys::scanKeyboard() {
  if (keyboard) {
    // Get Atari key code from keyboard driver and send to POKEY
    uint8_t keyCode = keyboard->getAtariKeyCode();
    bool keyPressed = keyboard->isAtariKeyPressed();
    pokey.setKeyCode(keyCode, keyPressed);

    // Get console keys and send to GTIA
    uint8_t consoleState = keyboard->getConsoleKeys();
    gtia.setConsoleKey(0x01, (consoleState & 0x01) != 0); // START
    gtia.setConsoleKey(0x02, (consoleState & 0x02) != 0); // SELECT
    gtia.setConsoleKey(0x04, (consoleState & 0x04) != 0); // OPTION
  }

  // Update joystick
  if (joystick) {
    uint8_t joyVal = joystick->getValue();
    // Decode C64-style joystick bits (active-low: 0=pressed)
    bool up = (joyVal & 0x01) == 0;
    bool down = (joyVal & 0x02) == 0;
    bool left = (joyVal & 0x04) == 0;
    bool right = (joyVal & 0x08) == 0;
    bool fire = (joyVal & 0x10) == 0;
    pia.setJoystick1(up, down, left, right);
    gtia.setTrigger(0, fire);
  }
}

void Atari800Sys::run() {
  static const char* TAG = "CPU";
  PlatformManager::getInstance().log(LOG_INFO, TAG, "run() starting, PC=%04X cpuhalted=%d", pc, cpuhalted);

  // Debug: Show first few bytes at PC to diagnose boot issue
  PlatformManager::getInstance().log(LOG_INFO, TAG, "osRomEnabled=%d osRom=%p", osRomEnabled, (void*)osRom);
  uint8_t b0 = getMem(pc);
  uint8_t b1 = getMem(pc+1);
  uint8_t b2 = getMem(pc+2);
  uint8_t b3 = getMem(pc+3);
  PlatformManager::getInstance().log(LOG_INFO, TAG, "getMem(%04X)=%02X %02X %02X %02X", pc, b0, b1, b2, b3);
  // Also check raw ROM bytes
  if (osRom) {
    uint16_t offset = 0x2450;  // E450 - C000
    PlatformManager::getInstance().log(LOG_INFO, TAG, "osRom[%04X]=%02X %02X %02X %02X",
        offset, osRom[offset], osRom[offset+1], osRom[offset+2], osRom[offset+3]);
  }

  int64_t lastMeasuredTime = PlatformManager::getInstance().getTimeUS();
  uint32_t totalCycles = 0;
  uint32_t frameCount = 0;

  uint32_t instrCount = 0;
  while (!cpuhalted) {
    // Execute instructions for one scanline
    cyclesThisScanline = 0;
    int32_t targetCycles = cyclesPerScanline - antic.dmaCycles;

    while (cyclesThisScanline < targetCycles) {
      // Check for WSYNC halt
      if (antic.isWSYNCHalted()) {
        cyclesThisScanline = targetCycles;
        break;
      }

      // Execute one instruction
      numofcycles = 0;
      logDebugInfo();
      uint16_t instrPC = pc;
      uint8_t opcode = getMem(pc++);

      // Debug: Log first 20 instructions
      if (instrCount < 20) {
        PlatformManager::getInstance().log(LOG_INFO, TAG, "instr#%u: PC=%04X op=%02X", instrCount, instrPC, opcode);
      }

      // Debug: Trace CIOV calls (Central I/O) - increased limit to catch all calls
      static uint8_t cioCallCount = 0;
      if (instrPC == 0xE456 && cioCallCount < 50) {
        // CIO entry - log IOCB being used (X register / 16)
        PlatformManager::getInstance().log(LOG_INFO, TAG, "CIOV called! X=%02X (IOCB #%d) A=%02X", x, x >> 4, a);
        // Also log ICCOM (command) at $0342 + X
        uint8_t iccom = getMem(0x0342 + x);
        uint8_t icax1 = getMem(0x034A + x);
        uint16_t icbal = getMem(0x0344 + x) | (getMem(0x0345 + x) << 8);
        uint16_t icbll = getMem(0x0348 + x) | (getMem(0x0349 + x) << 8);
        PlatformManager::getInstance().log(LOG_INFO, TAG, "  ICCOM=$%02X ICAX1=$%02X ICBAL=$%04X ICBLL=$%04X",
            iccom, icax1, icbal, icbll);
        cioCallCount++;
      }

      // Debug: Trace execution in BASIC ROM range - increased limit
      static uint8_t basicExecCount = 0;
      if (instrPC >= 0xA000 && instrPC < 0xC000 && basicExecCount < 100) {
        PlatformManager::getInstance().log(LOG_INFO, TAG, "BASIC exec: PC=$%04X op=$%02X A=%02X X=%02X Y=%02X",
            instrPC, opcode, a, x, y);
        basicExecCount++;
      }

      // Debug: Trace JMP/JSR to BASIC area and cartridge-related jumps
      static uint8_t jmpTraceCount = 0;
      if (jmpTraceCount < 30) {
        // JMP absolute ($4C), JSR ($20)
        if ((opcode == 0x4C || opcode == 0x20) && instrPC >= 0xC000) {
          uint16_t target = getMem(instrPC + 1) | (getMem(instrPC + 2) << 8);
          // Trace jumps to BASIC area or low RAM (possible cartridge trampoline)
          if (target >= 0xA000 && target < 0xC000) {
            PlatformManager::getInstance().log(LOG_INFO, TAG, "%s to BASIC: PC=$%04X -> $%04X",
                opcode == 0x4C ? "JMP" : "JSR", instrPC, target);
            jmpTraceCount++;
          } else if (target < 0x0800) {
            // Low RAM jump - might be cartridge RUN trampoline
            PlatformManager::getInstance().log(LOG_INFO, TAG, "%s to low RAM: PC=$%04X -> $%04X",
                opcode == 0x4C ? "JMP" : "JSR", instrPC, target);
            jmpTraceCount++;
          }
        }
        // JMP indirect ($6C) - commonly used for cartridge/BASIC start
        if (opcode == 0x6C && instrPC >= 0xC000) {
          uint16_t ptr = getMem(instrPC + 1) | (getMem(instrPC + 2) << 8);
          uint16_t target = getMem(ptr) | (getMem(ptr + 1) << 8);
          PlatformManager::getInstance().log(LOG_INFO, TAG, "JMP ($%04X)=$%04X from OS: PC=$%04X",
              ptr, target, instrPC);
          jmpTraceCount++;
        }
      }

      // Track PC before execution
      static uint16_t prevPC = 0;
      static uint8_t prevOp = 0;
      static bool crashReported = false;

      execute(opcode);
      instrCount++;

      // Detect when PC becomes 0 (crash)
      if (pc == 0 && !crashReported) {
        PlatformManager::getInstance().log(LOG_ERROR, TAG, "CRASH to PC=0 at instr#%u!", instrCount-1);
        PlatformManager::getInstance().log(LOG_ERROR, TAG, "  Previous: PC=%04X op=%02X", prevPC, prevOp);
        PlatformManager::getInstance().log(LOG_ERROR, TAG, "  Current: PC=%04X op=%02X A=%02X X=%02X Y=%02X SP=%02X",
            instrPC, opcode, a, x, y, sp);
        // Log stack contents
        PlatformManager::getInstance().log(LOG_ERROR, TAG, "  Stack[FF-F8]: %02X %02X %02X %02X %02X %02X %02X %02X",
            ram[0x1FF], ram[0x1FE], ram[0x1FD], ram[0x1FC], ram[0x1FB], ram[0x1FA], ram[0x1F9], ram[0x1F8]);
        crashReported = true;
      }

      prevPC = instrPC;
      prevOp = opcode;

      // Check if we just halted
      if (cpuhalted) {
        PlatformManager::getInstance().log(LOG_ERROR, TAG, "HALT at instr#%u: PC=%04X op=%02X", instrCount-1, instrPC, opcode);
      }

      cyclesThisScanline += numofcycles;
      totalCycles += numofcycles;

      // Check interrupts
      checkInterrupts();
    }

    // Release WSYNC at end of scanline
    antic.releaseWSYNC();

    // Draw scanline
    antic.drawScanline();

    // Generate audio samples for this scanline
    pokey.fillBuffer(antic.getScanline());

    // Advance to next scanline
    antic.nextScanline();

    // End of frame handling
    if (antic.getScanline() == 0) {
      frameCount++;

      // Debug: log CPU state every 50 frames
      if (frameCount % 50 == 0) {
        PlatformManager::getInstance().log(LOG_INFO, TAG,
            "frame=%u PC=%04X A=%02X dmactl=%02X",
            frameCount, pc, a, antic.read(0x00));
      }

      // Play accumulated audio
      pokey.playAudio();

      // Reset NMI latch
      nmiActive = false;

      // Frame timing
      int64_t nominalFrameTime = lastMeasuredTime + (1000000 / 50);  // 50 Hz PAL
      int64_t now = PlatformManager::getInstance().getTimeUS();

      if (nominalFrameTime > now) {
        PlatformManager::getInstance().waitUS(nominalFrameTime - now);
      }

      lastMeasuredTime = PlatformManager::getInstance().getTimeUS();

      // Update profiling
      if (perf) {
        numofcyclespersecond = totalCycles * 50;
      }
      totalCycles = 0;
    }
  }
}

