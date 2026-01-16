#!/usr/bin/env python3
"""
Convert original Atari ROM binary files to C header files for embedding.

Usage:
    python3 convert_roms.py ATARIXL.ROM ATARIBAS.ROM

This will create:
    - original_os_xl.h (16KB OS ROM)
    - original_basic.h (8KB BASIC ROM)

The ROMs should be:
    - ATARIXL.ROM: 16384 bytes (Atari 800XL OS ROM)
    - ATARIBAS.ROM: 8192 bytes (Atari BASIC ROM)
"""

import sys
import os

def bin_to_header(input_file, output_file, array_name, expected_size=None, have_macro=None):
    """Convert binary file to C header with uint8_t array."""

    with open(input_file, 'rb') as f:
        data = f.read()

    actual_size = len(data)
    print(f"  {input_file}: {actual_size} bytes")

    if expected_size and actual_size != expected_size:
        print(f"  WARNING: Expected {expected_size} bytes, got {actual_size}")

    # Analyze ROM - check reset vector (last 2 bytes at $FFFC-$FFFD)
    if actual_size >= 16384:
        # OS ROM - check vectors at offset $3FFC and $3FFE
        reset_lo = data[0x3FFC]
        reset_hi = data[0x3FFD]
        reset_vec = (reset_hi << 8) | reset_lo
        irq_lo = data[0x3FFE]
        irq_hi = data[0x3FFF]
        irq_vec = (irq_hi << 8) | irq_lo
        print(f"  Reset vector: ${reset_vec:04X}")
        print(f"  IRQ vector: ${irq_vec:04X}")

    # Generate header
    lines = []
    lines.append(f"// Auto-generated from {os.path.basename(input_file)}")
    lines.append(f"// Size: {actual_size} bytes")
    lines.append(f"#ifndef {array_name.upper()}_H")
    lines.append(f"#define {array_name.upper()}_H")
    lines.append("")
    lines.append("#include <cstdint>")
    lines.append("")
    # Add the HAVE_ORIGINAL_*_ROM macro
    if have_macro:
        lines.append(f"// Enable original ROM support")
        lines.append(f"#define {have_macro} 1")
        lines.append("")
    lines.append(f"alignas(4) static const uint8_t {array_name}[{actual_size}] = {{")

    # Output data 16 bytes per line
    for i in range(0, actual_size, 16):
        chunk = data[i:i+16]
        hex_vals = ", ".join(f"0x{b:02X}" for b in chunk)
        if i + 16 < actual_size:
            lines.append(f"    {hex_vals},")
        else:
            lines.append(f"    {hex_vals}")

    lines.append("};")
    lines.append("")
    lines.append(f"#endif // {array_name.upper()}_H")
    lines.append("")

    with open(output_file, 'w') as f:
        f.write('\n'.join(lines))

    print(f"  -> Created {output_file}")

def main():
    if len(sys.argv) < 2:
        print(__doc__)
        print("\nSearching for ROM files in current directory...")

        # Look for common ROM file names
        os_candidates = ['ATARIXL.ROM', 'atarixl.rom', 'ATARIOSB.ROM', 'atariosb.rom']
        basic_candidates = ['ATARIBAS.ROM', 'ataribas.rom', 'REVC.ROM', 'revc.rom',
                          'REVB.ROM', 'revb.rom', 'REVA.ROM', 'reva.rom']

        os_rom = None
        basic_rom = None

        for f in os.listdir('.'):
            if f.upper() in [c.upper() for c in os_candidates]:
                os_rom = f
            elif f.upper() in [c.upper() for c in basic_candidates]:
                basic_rom = f

        if os_rom:
            print(f"Found OS ROM: {os_rom}")
        if basic_rom:
            print(f"Found BASIC ROM: {basic_rom}")

        if not os_rom and not basic_rom:
            print("No ROM files found. Please copy ROMs to this directory.")
            return 1

        args = []
        if os_rom:
            args.append(os_rom)
        if basic_rom:
            args.append(basic_rom)
    else:
        args = sys.argv[1:]

    print("\nConverting ROM files...")

    for rom_file in args:
        if not os.path.exists(rom_file):
            print(f"Error: {rom_file} not found")
            continue

        size = os.path.getsize(rom_file)
        name_upper = rom_file.upper()

        if size == 16384 or 'XL' in name_upper or 'OSB' in name_upper or 'OSA' in name_upper:
            # OS ROM
            print(f"\nProcessing OS ROM:")
            bin_to_header(rom_file, 'original_os_xl.h', 'original_os_xl', 16384,
                         have_macro='HAVE_ORIGINAL_OS_ROM')
        elif size == 8192 or 'BAS' in name_upper or 'REV' in name_upper:
            # BASIC ROM
            print(f"\nProcessing BASIC ROM:")
            bin_to_header(rom_file, 'original_basic.h', 'original_basic', 8192,
                         have_macro='HAVE_ORIGINAL_BASIC_ROM')
        elif size == 10240:
            # Atari 800 OS (not XL) - 10KB
            print(f"\nNote: {rom_file} appears to be Atari 800 OS (10KB)")
            print("  This emulator is designed for Atari 800XL (16KB OS)")
            print("  Skipping this file...")
        else:
            print(f"\nUnknown ROM type: {rom_file} ({size} bytes)")
            print("  Expected: 16384 bytes (OS) or 8192 bytes (BASIC)")

    print("\nDone! Copy the generated .h files to this directory.")
    print("Then rebuild the emulator project.")
    return 0

if __name__ == '__main__':
    sys.exit(main())
