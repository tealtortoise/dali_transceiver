#!/bin/sh

# for file in spiffs/*
#do
curl -X POST --data-binary @$2 http://192.168.1.$1/$2
echo ""
#done
