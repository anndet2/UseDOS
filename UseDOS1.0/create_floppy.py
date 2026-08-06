# Create an 8MB floppy disk image
# 8MB = 16384 sectors * 512 bytes = 8388608 bytes
#
# This script also patches the bootloader with the correct kernel sector count

import os
import sys

# Read the bootloader (with kernel embedded) binary
with open('boot_with_kernel.bin', 'rb') as f:
    data = bytearray(f.read())

# Calculate kernel size (everything after the 512-byte boot sector)
boot_sector_size = 512
kernel_size = len(data) - boot_sector_size
kernel_sectors = (kernel_size + 511) // 512  # Round up

print(f"Boot sector size: {boot_sector_size} bytes")
print(f"Kernel size: {kernel_size} bytes")
print(f"Kernel sectors needed: {kernel_sectors}")

# Patch the bootloader at offset 0x16 with the sector count
# The instruction is: mov al, 0x00  ->  B0 00
# Opcode B0 is at 0x15, immediate operand is at 0x16
patch_offset = 0x16
if kernel_sectors <= 0 or kernel_sectors > 255:
    print(f"ERROR: Invalid sector count: {kernel_sectors}")
    sys.exit(1)

data[patch_offset] = kernel_sectors
print(f"Patched bootloader at offset 0x{patch_offset:02X} with sector count: {kernel_sectors}")

# Create 8MB floppy image
floppy_size = 8 * 1024 * 1024  # 8MB = 8388608 bytes

# Write data to floppy image, padding with zeros
image = data + b'\x00' * (floppy_size - len(data))

# Write the floppy image
with open('os-image.bin', 'wb') as f:
    f.write(image)

print(f"Created 8MB floppy image: os-image.bin ({len(image)} bytes)")
print(f"Boot+kernel size: {len(data)} bytes")
print(f"Remaining space: {floppy_size - len(data)} bytes ({(floppy_size - len(data)) // 1024} KB)")
