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

F90_PROP = 1.0
F90_PROP_START = 0.2
FADE = 0.02
F90_PRIORITY = 1

LESSTHRIVE_CHANNELS = {
    "dalia": Channel(
        friendly_name="5700k Thrive",
        type=ChannelType.INDEPENDENT,
        points=[(0.0, 4),(0.3, 4.0), (0.5, 18.0), (1.0, 18.0)],
        # led=LED(vf=34, imax=2800, eff=125, scale_max=1),
        led=LED(vf=34, imax=3750, eff=133, scale_max=0.78),
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
            (0.25 - FADE, 0.0),
            (0.25001 - FADE, F90_PROP_START),
            (0.25 + FADE, F90_PROP),
            (1.0, F90_PROP),
        ],
        led=LED(imax=950, vf=51, eff=189),
        group=0,
        driver_min=0.01,
        requires_relay=Relay.RELAY2,
        priority=F90_PRIORITY,
    ),
    "dalic": Channel(
        friendly_name="5000k F90 2",
        type=ChannelType.INDEPENDENT,
        points=[
            (0.0, 0.0),
            (0.43 - FADE, 0.0),
            (0.43001 - FADE, F90_PROP_START),
            (0.43 + FADE, F90_PROP),
            (1.0, F90_PROP),
        ],
        led=LED(imax=950, vf=51, eff=189),
        group=0,
        driver_min=0.01,
        requires_relay=Relay.RELAY2,
        priority=F90_PRIORITY,
    ),
    "dalid": Channel(
        friendly_name="5000k F90 3",
        type=ChannelType.INDEPENDENT,
        points=[
            (0.0, 0.0),
            (0.60 - FADE, 0.0),
            (0.60001 - FADE, F90_PROP_START),
            (0.60 + FADE, F90_PROP),
            (1.0, F90_PROP),
        ],
        led=LED(imax=950, vf=51, eff=189),
        group=0,
        driver_min=0.01,
        requires_relay=Relay.RELAY2,
        priority=F90_PRIORITY,
    ),
    "dalie": Channel(
        friendly_name="5000k F90 4",
        type=ChannelType.INDEPENDENT,
        points=[
            (0.0, 0.0),
            (0.77 - FADE, 0.0),
            (0.77001 - FADE, F90_PROP_START),
            (0.77 + FADE, F90_PROP),
            (1.0, F90_PROP),
        ],
        led=LED(imax=950, vf=51, eff=189),
        group=0,
        driver_min=0.01,
        requires_relay=Relay.RELAY2,
        priority=F90_PRIORITY,
    ),
}

F90_PROP = 1.0
F90_PROP_START = 0.2
FADE = 0.04
F90_PRIORITY = 1
F90_START = 0.30
F90_INC = 0.12
F90_INC2 = 0.30

F90_TWOSTART_CHANNELS = {
    "dalia": Channel(
        friendly_name="5700k Thrive",
        type=ChannelType.INDEPENDENT,
        points=[(0.0, 3.0),(0.3, 3.0), (0.5, 3.0), (1.0, 3.0)],
        # led=LED(vf=34, imax=2800, eff=125, scale_max=1),
        led=LED(vf=33, imax=3750, eff=133, scale_max=0.84),
        group=0,
        driver_min=0.001,
        requires_relay=Relay.RELAY1,
        priority=0,
    ),
    "dalib": Channel(
        friendly_name="5000k F90 1",
        type=ChannelType.INDEPENDENT,
        points=[
            (0.0, 0.0),
            (F90_START, 0.0),
            (F90_START + 0.001, F90_PROP_START),
            (F90_START + FADE, F90_PROP),
            (1.0, F90_PROP),
        ],
        led=LED(imax=1050, vf=49, eff=195),
        group=0,
        driver_min=0.001,
        requires_relay=Relay.RELAY2,
        priority=F90_PRIORITY,
    ),
    "dalic": Channel(
        friendly_name="5000k F90 2",
        type=ChannelType.INDEPENDENT,
        points=[
            (0.0, 0.0),
            (F90_START + F90_INC, 0.0),
            (F90_START + F90_INC + 0.001, F90_PROP_START * 0.23),
            (F90_START + F90_INC + FADE, F90_PROP),
            (1.0, F90_PROP),
        ],
        led=LED(imax=1050, vf=49, eff=195),
        group=0,
        driver_min=0.001,
        requires_relay=Relay.RELAY2,
        priority=F90_PRIORITY,
    ),
    "zeroten1": Channel(
        friendly_name="5000k F90 3",
        type=ChannelType.INDEPENDENT,
        # points=[
        #     (0.0, 0.0),
        #     (0.60 - FADE, 0.0),
        #     (0.60001 - FADE, F90_PROP_START),
        #     (0.60 + FADE, F90_PROP),
        #     (1.0, F90_PROP),
        # ],
        points=[
            (0.0, 0.0),
            (F90_START + F90_INC2, 0.0),
            (F90_START + F90_INC2 + 0.001, F90_PROP_START * 0.15),
            (F90_START + F90_INC2 + FADE, F90_PROP),
            (1.0, F90_PROP),
        ],
        led=LED(imax=1050, vf=49, eff=195),
        group=0,
        driver_min=0.01,
        requires_relay=Relay.RELAY2,
        priority=F90_PRIORITY,
    ),
    # "dalie": Channel(
    #     friendly_name="5000k F90 4",
    #     type=ChannelType.INDEPENDENT,
    #     points=[
    #         (0.0, 0.0),
    #         (F90_START - FADE, 0.0),
    #         (F90_START + 0.001 - FADE, F90_PROP_START),
    #         (F90_START + FADE, F90_PROP),
    #         (1.0, F90_PROP),
    #     ],
        # points=[
        #     (0.0, 0.0),
        #     (0.57 - FADE, 0.0),
        #     (0.57001 - FADE, F90_PROP_START),
        #     (0.57 + FADE, F90_PROP),
        #     (1.0, F90_PROP),
        # ],
    #     led=LED(imax=950, vf=51, eff=189),
    #     group=0,
    #     driver_min=0.01,
    #     requires_relay=Relay.RELAY2,
    #     priority=F90_PRIORITY,
    # ),
}
process(channels=F90_TWOSTART_CHANNELS,
        minimum_dim = 0.0004,
        filename = "../spiffs/levelluts4.csv",
        refine_groups=[0])