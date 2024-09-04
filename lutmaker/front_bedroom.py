from lutmaker import Channel, ChannelType, LED, process, Relay

FUDGE = 1.0 # analogue dimming is wobbly and we need a bit of help getting a smooth fade

CHANNELS = {
    "dalia": Channel(
        friendly_name="5000k Thrive",
        points=[(0, 4.0), (0.12, 4.0), (0.5, 0.4), (1.0, 0)],
        type=ChannelType.INDEPENDENT,
        led=LED(imax=600, vf=33, eff=136 * FUDGE),
        group=0,
        night_only=True,
        requires_relay=Relay.RELAY1,
    ),
    "zeroten2": Channel(
        friendly_name="F90 Quiet",
        points=[(0, 0.0), (0.12, 0.0),(0.12001, 0.8), (0.25, 2.5),(0.70, 1.0), (1.0, 1.0)],
        type=ChannelType.INDEPENDENT,
        led=LED(imax=800, vf=50, eff=185),
        group=0,
        driver_min=0.01,
        requires_relay=Relay.RELAY2,
    ),
    "zeroten1": Channel(
        friendly_name="F90 Hissy",
        points=[(0, 0.0), (0.3, 0),(0.38, 0.0),(0.3801, 0.2), (0.70, 1.0), (1.0, 1.0)],
        type=ChannelType.INDEPENDENT,
        led=LED(imax=800, vf=50, eff=185),
        group=0,
        driver_min=0.01,
        requires_relay=Relay.RELAY2,
    ),
}

process(channels=CHANNELS, filename = "../spiffs/levelluts5.csv",
        rgb_nightlight=True, minimum_dim=0.00020 * FUDGE, refine_groups=[0])