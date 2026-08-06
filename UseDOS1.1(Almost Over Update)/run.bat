@echo off

echo Starting build UseDOS...
call build.bat

echo Build completed successfully!

echo Starting UseDOS in QEMU...

if "%1"=="iso" (
    echo Booting from ISO...
    "C:\Program Files\qemu\qemu-system-i386.exe" -cdrom useDOS.iso -boot d
) else (
    echo Booting from floppy image...
    "C:\Program Files\qemu\qemu-system-i386.exe" -drive file=os-image.bin,format=raw -boot a
)