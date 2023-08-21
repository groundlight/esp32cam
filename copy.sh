cp ./.pio/build/m5stack-timer-cam/firmware.bin ./firmware/m5_firmware.bin
cp ./.pio/build/m5stack-timer-cam/bootloader.bin ./firmware/m5_bootloader.bin
cp ./.pio/build/m5stack-timer-cam/partitions.bin ./firmware/m5_partitions.bin
cp /Users/maxmckelvey/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin ./firmware/boot_app0.bin

cp .pio/build/esp32cam/firmware.bin ./firmware/cam_firmware.bin
cp .pio/build/esp32cam/bootloader.bin ./firmware/cam_bootloader.bin
cp .pio/build/esp32cam/partitions.bin ./firmware/cam_partitions.bin

cp .pio/build/esp32cam/firmware.bin ./firmware/s3_firmware.bin
cp .pio/build/esp32cam/bootloader.bin ./firmware/s3_bootloader.bin
cp .pio/build/esp32cam/partitions.bin ./firmware/s3_partitions.bin