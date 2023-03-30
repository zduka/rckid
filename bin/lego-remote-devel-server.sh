#!/bin/bash
echo "Syncing source code..."
rsync -r -ssh lego-remote include bin peta@10.0.0.38:/home/peta/devel/rckid --delete
echo "Buiding lego-remote"
ssh peta@10.0.0.38 "cd ~/devel/rckid/lego-remote && pio run --target upload"