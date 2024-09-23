from lutmaker import Channel, ChannelType, LED, process


LIVING_ROOM_CHANNELS = {
    "dalia": Channel(
        friendly_name="hexagons",
        type=ChannelType.INDEPENDENT,
        points=[
            (0.0, 0.0),
            (0.02, 0.0),
            (0.16, 1.85),
            (0.3, 1.2),
            (0.7, 0.48),
            (1.0, 0.83),
        ],
        led=LED(vf=17.5, imax=600 * 5, eff=120),
        group=0,
    ),
    "espnow": Channel(
        friendly_name="piano",
        type=ChannelType.INDEPENDENT,
        points=[(0.0, 4.3), (0.015, 2.8), (0.05, 2.4), (0.16, 1.5), (1.0, 0.5)],
        led=LED(vf=40, imax=800, eff=135),
        group=0,
    ),
    "dalib": Channel(
        friendly_name="tv 5000k",
        type=ChannelType.INDEPENDENT,
        points=[
            (0.0, 0.0),
            (0.02, 0.0),
            (0.16, 1.0),
            (0.23, 0.7),
            (0.45, 0.55),
            (0.6, 0.46),
            (1.0, 0.28),
        ],
        led=LED(imax=480, vf=40, eff=123),
        group=0,
    ),
    "dalic": Channel(
        friendly_name="tv 6500k",
        type=ChannelType.INDEPENDENT,
        points=[(0.0, 2.2), (0.01, 2.0), (0.04, 1.2), (0.16, 0.0), (1.0, 0.0)],
        led=LED(imax=650, vf=33, eff=130),
        group=0,
        night_only=True,
    ),
    "dalid": Channel(
        friendly_name="corner 6500k",
        type=ChannelType.INDEPENDENT,
        points=[(0.0, 1.75), (0.02, 1.75),(0.08, 0.3), (0.16, 0.0), (1.0, 0.0)],
        led=LED(imax=650, vf=33, eff=130),
        group=0,
        night_only=True,
    ),
    "dalie": Channel(
        friendly_name="F90L",
        points=[(0, 0.0), (0.08, 0.0), (0.16, 0.4), (0.3, 0.4), (0.5, 1.2), (0.7, 2), (1.0, 2.0)],
        type=ChannelType.INDEPENDENT,
        led=LED(imax=600, vf=50, eff=185),
        group=0,
    ),
    "dalif": Channel(
        friendly_name="F90R",
        points=[(0, 0.0), (0.08, 0.0), (0.16, 0.2), (0.3, 0.2), (0.5, 0.6), (0.7, 0.6), (1.0, 1.0)],
        type=ChannelType.INDEPENDENT,
        led=LED(imax=500, vf=50, eff=185),
        group=0,
    ),
}

process(channels=LIVING_ROOM_CHANNELS,
        minimum_dim = 0.0005,
        filename = "../spiffs/levelluts2.csv",
        refine_groups=[0])