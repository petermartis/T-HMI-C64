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
#ifndef ATARILOADER_H
#define ATARILOADER_H

#include "fs/FileDriver.h"
#include <cstdint>
#include <string>
#include <vector>

// Atari OS addresses for executable loading
constexpr uint16_t RUNAD = 0x02E0;   // Run address (2 bytes)
constexpr uint16_t INITAD = 0x02E2; // Init address (2 bytes)
constexpr uint16_t MEMLO = 0x02E7;  // Low memory boundary

// ATR disk image constants
constexpr uint16_t ATR_SIGNATURE = 0x0296;  // "NICKATARI" signature
constexpr uint16_t ATR_SECTOR_SIZE = 128;   // Standard single density
constexpr uint16_t ATR_DD_SECTOR_SIZE = 256; // Double density

/**
 * @brief Atari executable and disk image loader
 *
 * Supports loading:
 * - XEX files (Atari executable format)
 * - BIN/COM files (raw binary or executable)
 * - ATR disk images (virtual disk mounting)
 */
class AtariLoader {
public:
  enum class FileType {
    UNKNOWN,
    XEX,       // Atari executable (DOS 2.x format)
    BINARY,    // Binary load (COM file)
    ATR,       // ATR disk image
    CAS        // Cassette image (future)
  };

  struct LoadResult {
    bool success;
    uint16_t runAddress;
    uint16_t initAddress;
    std::string errorMessage;
    std::vector<std::pair<uint16_t, uint16_t>> loadedSegments; // start, end pairs
  };

  AtariLoader(uint8_t *ram, FileDriver *fs);

  /**
   * @brief Detect file type from filename extension
   */
  FileType detectFileType(const std::string &filename);

  /**
   * @brief Load an executable file into RAM
   * @param filename Path to the file
   * @return LoadResult with success status and addresses
   */
  LoadResult loadExecutable(const std::string &filename);

  /**
   * @brief Load XEX/COM format executable
   */
  LoadResult loadXEX(const std::string &filename);

  /**
   * @brief Load raw binary at specified address
   * @param filename Path to the file
   * @param loadAddress Address to load the binary
   */
  LoadResult loadBinary(const std::string &filename, uint16_t loadAddress);

  /**
   * @brief Mount ATR disk image for virtual disk access
   * @param filename Path to ATR file
   * @return true if mounted successfully
   */
  bool mountATR(const std::string &filename);

  /**
   * @brief Unmount current ATR disk image
   */
  void unmountATR();

  /**
   * @brief Check if ATR is mounted
   */
  bool isATRMounted() const { return atrMounted; }

  /**
   * @brief Read sector from mounted ATR
   * @param sector Sector number (1-based)
   * @param buffer Output buffer
   * @return true if read successful
   */
  bool readATRSector(uint16_t sector, uint8_t *buffer);

  /**
   * @brief Write sector to mounted ATR
   * @param sector Sector number (1-based)
   * @param buffer Data to write
   * @return true if write successful
   */
  bool writeATRSector(uint16_t sector, const uint8_t *buffer);

  /**
   * @brief Get list of files from filesystem
   */
  std::vector<std::string> listFiles(const std::string &path = "");

private:
  uint8_t *ram;
  FileDriver *fs;

  // ATR state
  bool atrMounted;
  std::string atrFilename;
  uint16_t atrSectorSize;
  uint32_t atrSectorCount;
  uint16_t atrBootSectors; // First 3 sectors are always 128 bytes

  /**
   * @brief Read 16-bit little-endian value from file
   */
  uint16_t readWord();

  /**
   * @brief Check if filename has given extension (case-insensitive)
   */
  bool hasExtension(const std::string &filename, const std::string &ext);
};

#endif // ATARILOADER_H
