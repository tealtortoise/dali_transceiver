#!/usr/bin/python3

import argparse
import sys
import requests
import urllib

DEFAULT_SETTINGS_FILENAME = "spiffs/default_settings.csv"

def read_nvs(file, uri, relaxed):
    with open(DEFAULT_SETTINGS_FILENAME, "r") as defaultfile:
        for line in defaultfile.readlines():
            setting_name = line.split(",")[0]

            response = requests.get(f"{uri}/nvs/{setting_name}")
            if response.status_code != 200:
                errstr = f"Unable to read value for '{setting_name}' -- response body: '{response.text}')"
                if relaxed:
                    print(errstr, file=sys.stderr)
                raise Exception(errstr)
            # print(f"Found key '{setting_name}': {value}")
            try:
                value = int(response.text)
            except ValueError:
                raise ValueError(f"Unable to recognise '{response.text}' as an int")
            file.write(f"{setting_name},{value}\n")

def write_nvs(file, uri, relaxed):
    valid_keys = set()
    with open(DEFAULT_SETTINGS_FILENAME, "r") as defaultfile:
        for line in defaultfile.readlines():
            setting_name = line.split(",")[0]
            valid_keys.add(setting_name)

        for line in file.readlines():
            setting_name = line.split(",")[0]
            value = line.split(",")[1].strip()

            if setting_name not in valid_keys:
                errstr = f"Unknown setting '{setting_name}"
                if relaxed:
                    print(errstr, file=sys.stderr)
                else:
                    raise ValueError(errstr)
            print(f"Setting key '{setting_name}' to {value}")
            requests.put(f"{uri}/nvs/{setting_name}", value)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
            prog='NVS Manager',
            description='Loads and saves NVS settings from MulberryLight over http')
    parser.add_argument("-b", "--backup", type=argparse.FileType('w'))
    parser.add_argument("-r", "--restore", type=argparse.FileType('r'))
    parser.add_argument("-x", "--relaxed")
    parser.add_argument("uri")
    args = parser.parse_args()

    if args.uri[-1] == '/':
        uri_noslash = args.uri[:-1]
    else:
        uri_noslash = args.uri
    if args.backup:
        if args.restore:
            raise argparse.ArgumentError("Cannot both backup and restore")
        read_nvs(args.backup, args.uri, bool(args.relaxed))
    if args.restore:
        write_nvs(args.restore, args.uri, bool(args.relaxed))