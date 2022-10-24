#!/bin/bash
echo "Syncing source code..."
rsync -r -ssh rcboy include pi@10.0.0.39:/home/pi/rcboy --delete
echo "Buiding avr-src"
ssh pi@10.0.0.39 "cd ~/rcboy/rcboy/avr && PATH=\$PATH:~/.local/bin ~/.platformio/penv/bin/pio run --target upload"
