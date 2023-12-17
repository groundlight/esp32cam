rm -rf ./firmware
mkdir ./firmware
cd .pio/build
for f in $(ls -d *)
do
    echo $f
    cp $f/firmware.bin ../../firmware/firmware_$f.bin
done
echo "Contents of firmware directory:"
ls -l ./firmware