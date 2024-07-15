#!/usr/bin/bash

python ~/esp/v5.2.1/esp-idf/components/spiffs/spiffsgen.py 1048576 spiffs/ spiffspart.bin
parttool.py write_partition --partition-name=spiffs --input=spiffspart.bin