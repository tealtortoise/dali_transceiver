#!/bin/sh

for file in spiffs/*
do
  curl -X POST --data-binary @${file} http://192.168.1.$1/${file}
  echo ""
done
