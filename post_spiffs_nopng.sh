#!/bin/sh

for file in spiffs/*
do
    case $file in
        *.png) continue;;
        *.ods) continue;;
        *.ico) continue;;
    esac
    curl -X POST --data-binary @$file http://192.168.1.$1/$file
    # echo "$file"
    echo ""
done
