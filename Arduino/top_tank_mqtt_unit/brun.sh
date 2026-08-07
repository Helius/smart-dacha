arduino-cli compile --fqbn esp32:esp32:esp32c3:CDCOnBoot=cdc --build-path ./build
arduino-cli upload -p /dev/ttyACM0 --fqbn esp32:esp32:esp32c3 --input-dir ./build
arduino-cli monitor -p /dev/ttyACM0