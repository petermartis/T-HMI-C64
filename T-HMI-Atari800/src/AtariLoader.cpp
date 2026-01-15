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
#include "AtariLoader.h"
#include "platform/PlatformManager.h"
#include <algorithm>
#include <cctype>
#include <cstring>

static const char *TAG = "LOADER";

AtariLoader::AtariLoader(uint8_t *ram, FileDriver *fs)
    : ram(ram), fs(fs), atrMounted(false), atrSectorSize(ATR_SECTOR_SIZE),
      atrSectorCount(0), atrBootSectors(3) {}

bool AtariLoader::hasExtension(const std::string &filename, const std::string &ext) {
  if (filename.size() < ext.size() + 1) return false;
  std::string fileLower = filename;
  std::string extLower = ext;
  std::transform(fileLower.begin(), fileLower.end(), fileLower.begin(), ::tolower);
  std::transform(extLower.begin(), extLower.end(), extLower.begin(), ::tolower);
  return fileLower.size() >= extLower.size() &&
         fileLower.compare(fileLower.size() - extLower.size(), extLower.size(), extLower) == 0;
}

AtariLoader::FileType AtariLoader::detectFileType(const std::string &filename) {
  if (hasExtension(filename, ".xex") || hasExtension(filename, ".com")) {
    return FileType::XEX;
  }
  if (hasExtension(filename, ".bin")) {
    return FileType::BIN;
  }
  if (hasExtension(filename, ".atr")) {
    return FileType::ATR;
  }
  if (hasExtension(filename, ".cas")) {
    return FileType::CAS;
  }
  return FileType::UNKNOWN;
}

uint16_t AtariLoader::readWord() {
  uint8_t buf[2];
  if (fs->read(buf, 2) != 2) return 0;
  return buf[0] | (buf[1] << 8);
}

AtariLoader::LoadResult AtariLoader::loadExecutable(const std::string &filename) {
  FileType type = detectFileType(filename);

  switch (type) {
  case FileType::XEX:
    return loadXEX(filename);
  case FileType::BIN:
    // Default load address for raw BIN files - can be overridden
    return loadBinary(filename, 0x2000);
  default:
    return {false, 0, 0, "Unknown or unsupported file type", {}};
  }
}

AtariLoader::LoadResult AtariLoader::loadXEX(const std::string &filename) {
  LoadResult result = {false, 0, 0, "", {}};

  if (!fs->open(filename, "rb")) {
    result.errorMessage = "Failed to open file: " + filename;
    PlatformManager::getInstance().log(LOG_ERROR, TAG, "%s", result.errorMessage.c_str());
    return result;
  }

  PlatformManager::getInstance().log(LOG_INFO, TAG, "Loading XEX: %s", filename.c_str());

  // Reset RUNAD and INITAD in RAM
  ram[RUNAD] = 0;
  ram[RUNAD + 1] = 0;
  ram[INITAD] = 0;
  ram[INITAD + 1] = 0;

  // Read file header
  uint8_t header[2];
  if (fs->read(header, 2) != 2) {
    result.errorMessage = "Failed to read file header";
    fs->close();
    return result;
  }

  // Check for XEX signature (0xFF 0xFF)
  if (header[0] != 0xFF || header[1] != 0xFF) {
    result.errorMessage = "Invalid XEX file (missing 0xFF 0xFF header)";
    fs->close();
    return result;
  }

  // Load segments
  int segmentCount = 0;
  while (!fs->eof()) {
    // Read segment start address
    uint16_t startAddr = readWord();

    // Check for another 0xFF 0xFF header (optional between segments)
    if (startAddr == 0xFFFF) {
      startAddr = readWord();
    }

    // Check for EOF or invalid data
    if (fs->eof()) break;

    // Read segment end address
    uint16_t endAddr = readWord();
    if (fs->eof()) {
      result.errorMessage = "Unexpected end of file reading segment end address";
      fs->close();
      return result;
    }

    // Validate addresses
    if (endAddr < startAddr) {
      result.errorMessage = "Invalid segment: end < start";
      fs->close();
      return result;
    }

    uint16_t segmentSize = endAddr - startAddr + 1;

    PlatformManager::getInstance().log(LOG_INFO, TAG,
        "Segment %d: $%04X-$%04X (%d bytes)", segmentCount, startAddr, endAddr, segmentSize);

    // Read segment data into RAM
    size_t bytesRead = fs->read(&ram[startAddr], segmentSize);
    if (bytesRead != segmentSize) {
      result.errorMessage = "Failed to read segment data";
      fs->close();
      return result;
    }

    result.loadedSegments.push_back({startAddr, endAddr});
    segmentCount++;

    // Check for INITAD (initialization routine)
    // The OS would call this after each segment load
    if (startAddr <= INITAD && endAddr >= INITAD + 1) {
      uint16_t initAddr = ram[INITAD] | (ram[INITAD + 1] << 8);
      if (initAddr != 0) {
        result.initAddress = initAddr;
        PlatformManager::getInstance().log(LOG_INFO, TAG,
            "Init address set: $%04X", initAddr);
        // Clear INITAD after noting it (standard behavior)
        ram[INITAD] = 0;
        ram[INITAD + 1] = 0;
      }
    }

    // Check for RUNAD (run address)
    if (startAddr <= RUNAD && endAddr >= RUNAD + 1) {
      uint16_t runAddr = ram[RUNAD] | (ram[RUNAD + 1] << 8);
      if (runAddr != 0) {
        result.runAddress = runAddr;
        PlatformManager::getInstance().log(LOG_INFO, TAG,
            "Run address set: $%04X", runAddr);
      }
    }
  }

  fs->close();

  if (segmentCount == 0) {
    result.errorMessage = "No segments loaded";
    return result;
  }

  result.success = true;
  PlatformManager::getInstance().log(LOG_INFO, TAG,
      "Loaded %d segments, run=$%04X init=$%04X",
      segmentCount, result.runAddress, result.initAddress);

  return result;
}

AtariLoader::LoadResult AtariLoader::loadBinary(const std::string &filename, uint16_t loadAddress) {
  LoadResult result = {false, loadAddress, 0, "", {}};

  if (!fs->open(filename, "rb")) {
    result.errorMessage = "Failed to open file: " + filename;
    return result;
  }

  int64_t fileSize = fs->size();
  if (fileSize <= 0 || fileSize > 0xFFFF) {
    result.errorMessage = "Invalid file size";
    fs->close();
    return result;
  }

  // Ensure binary fits in memory
  if (loadAddress + fileSize > 0xFFFF) {
    result.errorMessage = "Binary too large to fit in memory at specified address";
    fs->close();
    return result;
  }

  PlatformManager::getInstance().log(LOG_INFO, TAG,
      "Loading BIN: %s at $%04X (%lld bytes)", filename.c_str(), loadAddress, fileSize);

  size_t bytesRead = fs->read(&ram[loadAddress], (size_t)fileSize);
  fs->close();

  if (bytesRead != (size_t)fileSize) {
    result.errorMessage = "Failed to read complete file";
    return result;
  }

  result.loadedSegments.push_back({loadAddress, (uint16_t)(loadAddress + fileSize - 1)});
  result.success = true;
  result.runAddress = loadAddress;  // Default to start of loaded data

  return result;
}

bool AtariLoader::mountATR(const std::string &filename) {
  if (atrMounted) {
    unmountATR();
  }

  if (!fs->open(filename, "rb")) {
    PlatformManager::getInstance().log(LOG_ERROR, TAG, "Failed to open ATR: %s", filename.c_str());
    return false;
  }

  // Read ATR header (16 bytes)
  uint8_t header[16];
  if (fs->read(header, 16) != 16) {
    PlatformManager::getInstance().log(LOG_ERROR, TAG, "Failed to read ATR header");
    fs->close();
    return false;
  }

  // Check signature (0x96 0x02 = NICKATARI)
  if (header[0] != 0x96 || header[1] != 0x02) {
    PlatformManager::getInstance().log(LOG_ERROR, TAG, "Invalid ATR signature");
    fs->close();
    return false;
  }

  // Parse header
  // Bytes 2-3: Image size in 16-byte paragraphs (low word)
  // Bytes 4-5: Sector size
  // Byte 6: High byte of image size
  uint32_t paragraphs = header[2] | (header[3] << 8) | ((uint32_t)header[6] << 16);
  uint32_t imageSize = paragraphs * 16;
  atrSectorSize = header[4] | (header[5] << 8);

  // Calculate sector count
  // First 3 sectors are always 128 bytes
  uint32_t bootSize = 3 * 128;
  if (imageSize > bootSize) {
    atrSectorCount = 3 + (imageSize - bootSize) / atrSectorSize;
  } else {
    atrSectorCount = imageSize / 128;
  }

  fs->close();

  atrFilename = filename;
  atrMounted = true;
  atrBootSectors = 3;

  PlatformManager::getInstance().log(LOG_INFO, TAG,
      "ATR mounted: %s, %d sectors, %d bytes/sector",
      filename.c_str(), atrSectorCount, atrSectorSize);

  return true;
}

void AtariLoader::unmountATR() {
  if (atrMounted) {
    PlatformManager::getInstance().log(LOG_INFO, TAG, "ATR unmounted: %s", atrFilename.c_str());
    atrMounted = false;
    atrFilename.clear();
    atrSectorCount = 0;
  }
}

bool AtariLoader::readATRSector(uint16_t sector, uint8_t *buffer) {
  if (!atrMounted || sector == 0 || sector > atrSectorCount) {
    return false;
  }

  if (!fs->open(atrFilename, "rb")) {
    return false;
  }

  // Calculate file offset
  // Header is 16 bytes
  // First 3 sectors are 128 bytes each
  uint32_t offset = 16;  // Skip header

  if (sector <= atrBootSectors) {
    // Boot sectors are always 128 bytes
    offset += (sector - 1) * 128;
  } else {
    // After boot sectors, use configured sector size
    offset += atrBootSectors * 128;
    offset += (sector - atrBootSectors - 1) * atrSectorSize;
  }

  uint16_t sectorSize = (sector <= atrBootSectors) ? 128 : atrSectorSize;

  if (!fs->seek(offset, SEEK_SET)) {
    fs->close();
    return false;
  }

  size_t bytesRead = fs->read(buffer, sectorSize);
  fs->close();

  // Pad with zeros if necessary
  if (bytesRead < sectorSize) {
    memset(buffer + bytesRead, 0, sectorSize - bytesRead);
  }

  return true;
}

bool AtariLoader::writeATRSector(uint16_t sector, const uint8_t *buffer) {
  if (!atrMounted || sector == 0 || sector > atrSectorCount) {
    return false;
  }

  // Open for read+write
  if (!fs->open(atrFilename, "r+b")) {
    return false;
  }

  // Calculate file offset (same as read)
  uint32_t offset = 16;

  if (sector <= atrBootSectors) {
    offset += (sector - 1) * 128;
  } else {
    offset += atrBootSectors * 128;
    offset += (sector - atrBootSectors - 1) * atrSectorSize;
  }

  uint16_t sectorSize = (sector <= atrBootSectors) ? 128 : atrSectorSize;

  if (!fs->seek(offset, SEEK_SET)) {
    fs->close();
    return false;
  }

  size_t bytesWritten = fs->write(buffer, sectorSize);
  fs->close();

  return bytesWritten == sectorSize;
}

std::vector<std::string> AtariLoader::listFiles(const std::string &path) {
  std::vector<std::string> files;
  std::string name;

  bool result = fs->listnextentry(name, true);  // Start listing
  while (result && !name.empty()) {
    // Filter for Atari-related files
    FileType type = detectFileType(name);
    if (type != FileType::UNKNOWN) {
      files.push_back(name);
    }
    result = fs->listnextentry(name, false);  // Continue listing
  }

  return files;
}
