from lutmaker import Channel, ChannelType, LED, process, Relay

FUDGE = 1.0 # analogue dimming is wobbly and we need a bit of help getting a smooth fade

CHANNELS = {
    "dalia": Channel(
        friendly_name="5000k Thrive",
        points=[(0, 1.0), (0.08, 1.0), (0.20, 0.8), (0.35, 0.0), (1.0, 0.0)],
        type=ChannelType.INDEPENDENT,
        led=LED(imax=600, vf=33, eff=136 * FUDGE),
        group=0,
        night_only=True,
        requires_relay=Relay.RELAY1,
        priority=0,
    ),
    "zeroten1": Channel(
        friendly_name="F90s",
        points=[(0, 0.0), (0.10, 0.0),(0.10001, 0.8), (0.25, 1.5),(0.46, 4.0),(0.60, 1.0), (1.0, 1.0)],
        type=ChannelType.INDEPENDENT,
        led=LED(imax=1600, vf=49, eff=185),
        group=0,
        driver_min=0.01,
        requires_relay=Relay.RELAY2,
        priority=1,
    ),
    "zeroten2": Channel(
        friendly_name="S90",
        points=[(0, 0.0), (0.3, 0),(0.46, 0.0),(0.4601, 0.16), (0.6, 0.6), (1.0, 0.6)],
        type=ChannelType.INDEPENDENT,
        led=LED(imax=1000, vf=49, eff=185),
        group=0,
        driver_min=0.01,
        requires_relay=Relay.RELAY2,
        
        priority=1,
    ),
}

process(channels=CHANNELS, filename = "../spiffs/levelluts5.csv",
        rgb_nightlight=True, minimum_dim=0.00018 * FUDGE, refine_groups=[0], reverse_priority=False)