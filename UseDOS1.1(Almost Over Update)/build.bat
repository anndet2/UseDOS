@echo off
echo === Building UseDOS ===

echo 1. Assembling kernel entry...
nasm ckernel\entry.asm -f win32 -o ckernel\entry.o

echo 2. Compiling user applications...
gcc -m32 -ffreestanding -nostdlib -fno-pic -fno-pie -c ckernel\freader.c -o ckernel\freader.o
gcc -m32 -ffreestanding -nostdlib -fno-pic -fno-pie -c ckernel\feditor.c -o ckernel\feditor.o
gcc -m32 -ffreestanding -nostdlib -fno-pic -fno-pie -c ckernel\nasmc.c -o ckernel\nasmc.o

echo 3. Compiling C kernel...
gcc -m32 -ffreestanding -nostdlib -fno-pic -fno-pie -c ckernel\kernel.c -o ckernel\kernel.o

echo 4. Linking kernel...
ld -T ckernel\linker.ld -m i386pe --oformat pei-i386 ckernel\entry.o ckernel\freader.o ckernel\feditor.o ckernel\nasmc.o ckernel\kernel.o -o ckernel\kernel.elf

echo 5. Creating binary kernel...
objcopy -O binary ckernel\kernel.elf ckernel\kernel.bin

echo 6. Creating bootloader with embedded kernel...
nasm boot.asm -f bin -o boot_with_kernel.bin

echo 7. Creating floppy disk image...
python create_floppy.py

echo 8. Creating ISO image...
python make_iso.py os-image.bin useDOS.iso

echo === Build completed successfully! ===
echo Floppy image: os-image.bin
echo ISO image: useDOS.iso