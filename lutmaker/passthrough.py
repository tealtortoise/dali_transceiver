from lutmaker import Channel, ChannelType, LED, process, Relay

process(filename = "../spiffs/levelluts0.csv", passthrough_nocurve=True)
process(filename = "../spiffs/levelluts3.csv", passthrough_nocurve=True, default_max_power=40.0)