from lutmaker import Channel, ChannelType, LED, process, Relay

F90_PROP = 0.5
FADE = 1.1

FULLTHRIVE_CHANNELS = {
    "dalia": Channel(
        friendly_name="5700k Thrive",
        type=ChannelType.INDEPENDENT,
        points=[(0.0, 4), (1.0, 4)],
        led=LED(vf=34, imax=2800, eff=125),
        group=0,
        driver_min=0.001,
        requires_relay=Relay.RELAY1,
        priority=0,
    ),
    "dalib": Channel(
        friendly_name="5000k F90",
        type=ChannelType.INDEPENDENT,
        points=[
            (0.0, 0.0),
            (0.28 / FADE, 0.0),
            (0.28 * FADE**3, F90_PROP),
            (1.0, F90_PROP),
        ],
        led=LED(imax=950, vf=51, eff=177),
        group=0,
        driver_min=0.01,
        requires_relay=Relay.RELAY2,
        priority=1,
    ),
    "dalic": Channel(
        friendly_name="5000k F90 2",
        type=ChannelType.INDEPENDENT,
        points=[
            (0.0, 0.0),
            (0.44 / FADE, 0.0),
            (0.44 * FADE, F90_PROP),
            (1.0, F90_PROP),
        ],
        led=LED(imax=950, vf=51, eff=177),
        group=0,
        driver_min=0.01,
        requires_relay=Relay.RELAY2,
        priority=1,
    ),
    "dalid": Channel(
        friendly_name="5000k F90 3",
        type=ChannelType.INDEPENDENT,
        points=[
            (0.0, 0.0),
            (0.58 / FADE, 0.0),
            (0.58 * FADE, F90_PROP),
            (1.0, F90_PROP),
        ],
        led=LED(imax=950, vf=51, eff=177),
        group=0,
        driver_min=0.01,
        requires_relay=Relay.RELAY2,
        priority=1,
    ),
    "dalie": Channel(
        friendly_name="5000k F90 4",
        type=ChannelType.INDEPENDENT,
        points=[
            (0.0, 0.0),
            (0.73 / FADE, 0.0),
            (0.73 * FADE, F90_PROP),
            (1.0, F90_PROP),
        ],
        led=LED(imax=950, vf=51, eff=177),
        group=0,
        driver_min=0.01,
        requires_relay=Relay.RELAY2,
        priority=1,
    ),
}

F90_PROP = 3.0
FADE = 0.04

LESSTHRIVE_CHANNELS = {
    "dalia": Channel(
        friendly_name="5700k Thrive",
        type=ChannelType.INDEPENDENT,
        points=[(0.0, 4), (1.0, 4)],
        led=LED(vf=34, imax=2800, eff=125),
        group=0,
        driver_min=0.001,
        requires_relay=Relay.RELAY1,
        priority=0,
    ),
    "dalib": Channel(
        friendly_name="5000k F90",
        type=ChannelType.INDEPENDENT,
        points=[
            (0.0, 0.0),
            (0.27 - FADE, 0.0),
            (0.27 + FADE, F90_PROP),
            (1.0, F90_PROP),
        ],
        led=LED(imax=950, vf=51, eff=177),
        group=0,
        driver_min=0.01,
        requires_relay=Relay.RELAY2,
        priority=1,
    ),
    "dalic": Channel(
        friendly_name="5000k F90 2",
        type=ChannelType.INDEPENDENT,
        points=[
            (0.0, 0.0),
            (0.41 - FADE, 0.0),
            (0.41 + FADE, F90_PROP),
            (1.0, F90_PROP),
        ],
        led=LED(imax=950, vf=51, eff=177),
        group=0,
        driver_min=0.01,
        requires_relay=Relay.RELAY2,
        priority=1,
    ),
    "dalid": Channel(
        friendly_name="5000k F90 3",
        type=ChannelType.INDEPENDENT,
        points=[
            (0.0, 0.0),
            (0.58 - FADE, 0.0),
            (0.58 + FADE, F90_PROP),
            (1.0, F90_PROP),
        ],
        led=LED(imax=950, vf=51, eff=177),
        group=0,
        driver_min=0.01,
        requires_relay=Relay.RELAY2,
        priority=1,
    ),
    "dalie": Channel(
        friendly_name="5000k F90 4",
        type=ChannelType.INDEPENDENT,
        points=[
            (0.0, 0.0),
            (0.73 - FADE, 0.0),
            (0.73 + FADE, F90_PROP),
            (1.0, F90_PROP),
        ],
        led=LED(imax=950, vf=51, eff=177),
        group=0,
        driver_min=0.01,
        requires_relay=Relay.RELAY2,
        priority=1,
    ),
}

process(channels=FULLTHRIVE_CHANNELS,
        minimum_dim = 0.0003,
        filename = "../spiffs/levelluts4.csv",
        refine_groups=[0])