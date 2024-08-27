#!/usr/bin/python
import math
from dataclasses import dataclass
from enum import Enum
import numpy as np
import pandas as pd
from matplotlib import pyplot as plt


class ChannelType(Enum):
    DIRECTED = 0
    RESIDUAL = 1
    INDEPENDENT = 2


class Relay(Enum):
    NO_RELAY = 0
    RELAY1 = 1
    RELAY2 = 2


@dataclass
class LED(object):
    imax: float
    vf: float
    eff: float

    @property
    def lumens(self):
        return self.power * self.eff

    @property
    def power(self):
        return self.imax * 0.001 * self.vf


@dataclass
class Channel(object):
    friendly_name: str
    type: ChannelType
    points: list[tuple[float, float]] | None
    led: LED
    group: int = 0
    night_only: bool = False
    driver_min: float = 0.001
    requires_relay: Relay = Relay.NO_RELAY
    priority: int = 0

    @property
    def is_proportioned(self):
        return self.type == ChannelType.DIRECTED or self.type == ChannelType.RESIDUAL


INRANGE = np.arange(0, 255, 1)

COLUMNS = [
    "level",
    "zeroten1",  # F90s
    "zeroten2",
    "dalia",  # hexagons 3
    "dalib",  # tv5000 1
    "dalic",  # tv6500 0
    "dalid",  # back 6500 2
    "dalie",
    "dalif",
    "espnow",  # piano
    "relay1",
    "relay2",
    "r",
    "g",
    "b",
]

MINIMUM_DIM = 0.001

FILENAME = "spiffs/levelluts5.csv"

RGB_NIGHTLIGHT = True

ITERATIONS = 400
REVERSE_PRIORITY = False

LIVING_ROOM_CHANNELS = {
    "dalie": Channel(
        friendly_name="f90",
        points=[(0, 0.0), (0.3, 0.0), (0.7, 1.45), (1.0, 1.45)],
        type=ChannelType.INDEPENDENT,
        led=LED(imax=450 * 2, vf=50, eff=185),
        group=0,
    ),
    "dalia": Channel(
        friendly_name="hexagons",
        type=ChannelType.INDEPENDENT,
        points=[
            (0.0, 0.0),
            (0.02, 0.0),
            (0.16, 1.85),
            (0.3, 1.75),
            (0.7, 0.38),
            (1.0, 0.83),
        ],
        led=LED(vf=17, imax=650 * 5, eff=120),
        group=0,
    ),
    "espnow": Channel(
        friendly_name="piano",
        type=ChannelType.INDEPENDENT,
        points=[(0.0, 1.3), (0.05, 1.1), (0.16, 0.9), (1.0, 0.5)],
        led=LED(vf=46, imax=650, eff=135),
        group=2,
    ),
    "dalib": Channel(
        friendly_name="tv 5000k",
        type=ChannelType.INDEPENDENT,
        points=[
            (0.0, 0.0),
            (0.02, 0.0),
            (0.16, 0.8),
            (0.23, 0.7),
            (0.45, 0.55),
            (0.6, 0.46),
            (1.0, 0.28),
        ],
        led=LED(imax=480, vf=40, eff=123),
        group=1,
    ),
    "dalic": Channel(
        friendly_name="tv 6500k",
        type=ChannelType.INDEPENDENT,
        points=[(0.0, 1.2), (0.01, 1.0), (0.02, 0.8), (0.16, 0.0), (1.0, 0.0)],
        led=LED(imax=650, vf=33, eff=130),
        group=1,
        night_only=True,
    ),
    "dalid": Channel(
        friendly_name="corner 6500k",
        type=ChannelType.INDEPENDENT,
        points=[(0.0, 1.75), (0.02, 1.75), (0.16, 0.0), (1.0, 0.0)],
        led=LED(imax=650, vf=33, eff=130),
        group=0,
        night_only=True,
    ),
}

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


if "fadetest" and 0:
    CHANNELS = {
        "a": Channel(
            friendly_name="a",
            type=ChannelType.INDEPENDENT,
            points=[(0.0, 1.0), (1.0, 0.0)],
            led=LED(1000, 34, 120),
        ),
        "b": Channel(
            friendly_name="b",
            type=ChannelType.INDEPENDENT,
            points=[(0.0, 0.0), (1.0, 1.0)],
            led=LED(1000, 34, 120),
        ),
    }
    COLUMNS = ["a", "b"]


# CHANNELS = LIVING_ROOM_CHANNELS
CHANNELS = FULLTHRIVE_CHANNELS
if 1 and "No custom channels":
    highest_flux = 1
    max_group = 0
    CHANNELS = {}
else:
    highest_flux = max((curve.led.lumens for curve in CHANNELS.values()))
    max_group = max((channel.group for channel in CHANNELS.values()))
    max_priority = max(channel.priority for channel in CHANNELS.values())

REFINE_GROUPS = (0,)


total_prop = [0.0] * (max_group + 1)
print(total_prop)
if 1 and "print proportions":
    for key, channel in CHANNELS.items():
        print(
            f"Channel {key}: '{channel.friendly_name}': {channel.led.power}W {channel.led.lumens} ({(channel.led.lumens / highest_flux)})"
        )
        if not channel.night_only:
            total_prop[channel.group] += channel.led.lumens / highest_flux

for group in range(max_group):
    print(f"Total group {group} daytime proportion {total_prop[group]}")


# exit()


def to_linear_custom(inp: np.ndarray, minimum_level: float = 0.001) -> np.ndarray:
    divider = -math.log10(minimum_level)
    ary = 10 ** ((inp - 1) / 253.0 * divider) * minimum_level
    ary[inp == 0] = 0
    # for i in range(len(inp)):
    #     if inp[i] == 0:
    #         ary[i] = 0;
    return ary


def to_linear(inp: np.ndarray) -> np.ndarray:
    return to_linear_custom(inp)


def to_linear_with_minimum(inp: np.ndarray, minimum: float) -> np.ndarray:
    lin = to_linear_custom(inp)
    lin[lin < minimum] = minimum
    lin[inp == 0] = 0
    return lin
    

def to_log(inp: np.ndarray) -> np.ndarray:
    # return to_log_custom(inp)
    return np.maximum(np.log10(inp * 1000) * 253.0 / 3.0 + 1.0, 0.0)

LIN_FLUX_TARGET = to_linear_custom(INRANGE, MINIMUM_DIM)
LIN_FLUX_TARGET[0] = 0.0

def main():
    warnings = []
    dalivals_float = {}
    lumens = [{} for _ in range(max_group + 1)]

    groupcolumns = [[], [], [], [], []]

    group_offsets = [np.zeros(INRANGE.size) for _ in range(max_group + 1)]
    channel_offsets = {key: np.zeros(INRANGE.size) for key in CHANNELS.keys()}

    for iteration in range(ITERATIONS):
        saturated = {key: np.zeros(INRANGE.size) for key in CHANNELS}
        lumensum = [np.zeros(INRANGE.size) for _ in range(max_group + 1)]
        lumensum_quantised = [np.zeros(INRANGE.size) for _ in range(max_group + 1)]
        powersum = [np.zeros(INRANGE.size) for _ in range(max_group + 1)]

        for key in COLUMNS:
            if key not in CHANNELS:
                dalivals_float[key] = INRANGE
                continue

            # interpolate curves
            channel = CHANNELS[key]
            x, y = zip(*channel.points)
            # print("Processing curve ", key, x, y)
            flux_points = np.array(x, dtype=float)
            prop_points = np.array(y, dtype=float)
            lamp_lumen_ratio = highest_flux / channel.led.lumens
            interpolated_curve = np.interp(
                LIN_FLUX_TARGET, flux_points, prop_points * lamp_lumen_ratio
            )
            raw_dali = to_log(interpolated_curve * LIN_FLUX_TARGET)

            dalivals_float[key] = np.clip(raw_dali + channel_offsets[key], 0, 254)
            channel_lumens = (
                to_linear_with_minimum(
                    dalivals_float[key], minimum=channel.driver_min
                )
                * channel.led.lumens
            )
            channel_lumens_quantised = (
                to_linear_with_minimum(
                    (dalivals_float[key]).astype(int), minimum=channel.driver_min
                )
                * channel.led.lumens
            )
            lumens[channel.group][channel.friendly_name] = channel_lumens
            lumensum[channel.group] += channel_lumens
            lumensum_quantised[channel.group] += channel_lumens_quantised
            powersum[channel.group] += channel_lumens / channel.led.eff
            if iteration == 0:
                groupcolumns[channel.group].append(channel.friendly_name)

        for key in COLUMNS:
            if key not in CHANNELS:
                dalivals_float[key] = INRANGE
                continue
            saturated[key] = np.any(
                (dalivals_float[key] == 254, dalivals_float[key] == 0), 0
            )

        for group in REFINE_GROUPS:
            if len(lumens[group]) == 0:
                continue
            group_lumens = lumensum[group]
            target_lumens = LIN_FLUX_TARGET * highest_flux * total_prop[group]
            inc_needed = group_lumens < target_lumens
            dec_needed = group_lumens > target_lumens
            change_inc = (
                1.0
                if max(np.abs(group_lumens[1:] - target_lumens[1:]) / target_lumens[1:])
                > 0.04
                else 0.2
            )
            print(
                iteration,
                change_inc,
                max(np.abs(group_lumens[1:] - target_lumens[1:]) / target_lumens[1:]),
            )
            change = np.zeros(INRANGE.size)
            change[inc_needed] = change_inc
            change[dec_needed] = -change_inc

            for priority in (
                range(max_priority + 1)
                if REVERSE_PRIORITY
                else reversed(range(max_priority + 1))
            ):
                channels_in_priority = []
                for ch_key, ch in reversed(CHANNELS.items()):
                    if ch.priority == priority:
                        channels_in_priority.append(ch_key)
                num_channels = len(channels_in_priority)

                # first pass we only change existing unsaturated lights
                prio_change = np.zeros(INRANGE.size)
                for ch_key in channels_in_priority:
                    # not_sat = np.logical_not(saturated[ch_key])
                    not_sat = dalivals_float[ch_key] > 1
                    old_vals = np.clip(
                        dalivals_float[ch_key] + channel_offsets[ch_key], 0, 254
                    )
                    channel_offsets[ch_key][not_sat] += (change / num_channels)[
                        not_sat
                    ]
                    diff = (
                        np.clip(
                            dalivals_float[ch_key] + channel_offsets[ch_key], 0, 254
                        )
                        - old_vals
                    )
                    prio_change += diff
                change -= prio_change

                # now we can turn on lights
                # for ch_key in channels_in_priority:
                #     allow_expand = np.convolve(dalivals_float[ch_key] > 1, np.ones(3), "same") > 0
                #     old_vals = np.clip(dalivals_float[ch_key] + channel_offsets[ch_key], 0, 254)
                #     channel_offsets[ch_key][allow_expand] += change[allow_expand]
                #     diff = np.clip(dalivals_float[ch_key] + channel_offsets[ch_key], 0, 254) - old_vals
                #     change -= diff

            if iteration == 0:
                lumens[group]["sum"] = group_lumens
                lumens[group]["ideal"] = target_lumens
                lumen_df = pd.DataFrame(
                    lumens[group],
                    columns=groupcolumns[group] + ["sum", "ideal"],
                    index=LIN_FLUX_TARGET,
                )
                lumen_df.plot(loglog=True, title=f"Lumens Group {group}")
                plt.show()
                lumen_df.plot(loglog=False, title=f"Lumens Group {group}")
                plt.show()

    for key, channel in CHANNELS.items():
        print(key, dalivals_float[key], dalivals_float[key].size)

    name_columns = []
    for name in COLUMNS:
        if name in CHANNELS:
            name_columns.append(CHANNELS[name].friendly_name)
            dalivals_float[CHANNELS[name].friendly_name] = dalivals_float[name]
            if name in lumens:
                lumendict = lumens[CHANNELS[name].group]
                lumendict[CHANNELS[name].friendly_name] = lumendict[name]
        else:
            name_columns.append(name)

    for group in (0,):
        if len(lumens[group]) == 0:
            continue
        lumens[group]["sum"] = lumensum_quantised[group]
        lumens[group]["power"] = lumensum_quantised[group] / powersum[group] * 100
        lumens[group]["ideal"] = LIN_FLUX_TARGET * highest_flux * total_prop[group]
        lumen_df = pd.DataFrame(
            lumens[group],
            columns=groupcolumns[group] + ["sum", "ideal", "power"],
            index=LIN_FLUX_TARGET,
        )
        lumen_df.plot(loglog=True, title=f"Lumens Group {group}")
        plt.show()
        lumen_df.plot(loglog=False, title=f"Lumens Group {group}")
        plt.show()
        print(group_offsets[group])
        plt.plot(INRANGE, group_offsets[group])
        plt.show()
    # exit()

    # df_plot_only = pd.DataFrame({key: value for key, value in dalivals.items() if value is not inrange}, columns=name_columns)
    # df_plot_only.plot(title="DALI Values");
    # plt.show();

    # Create colour LUTs
    colourpoints = [
        (
            0,
            (
                194,
                45,
                23,
            ),
        ),
        (25, (203, 118, 33)),
        (48, (150, 160, 00)),
        (86, (41, 172, 61)),
        (207, (23, 143, 202)),
        (239, (149, 110, 201)),
        (254, (186, 57, 149)),
    ]

    # colourpoints = [(0, (255, 255, 255)),
    # (254, (255, 255, 255))]

    sRGB_to_prophoto = np.array(
        [
            [0.6274413721, 0.3292974595, 0.0433514584],
            [0.0690276171, 0.9195806669, 0.0113614226],
            [0.0163642351, 0.0880171625, 0.8955649727],
        ]
    ).T

    white = (0.3 * 1.0, 0.3 * 0.78, 0.3 * 0.36)

    colour_reflevels = [point[0] for point in colourpoints]
    colour_r = [(point[1][0] / 255) ** 2.2 for point in colourpoints]
    colour_g = [(point[1][1] / 255) ** 2.2 for point in colourpoints]
    colour_b = [(point[1][2] / 255) ** 2.2 for point in colourpoints]

    lin_r = np.interp(INRANGE, colour_reflevels, colour_r)
    lin_g = np.interp(INRANGE, colour_reflevels, colour_g)
    lin_b = np.interp(INRANGE, colour_reflevels, colour_b)

    out_r = np.zeros(INRANGE.size)
    out_g = np.zeros(INRANGE.size)
    out_b = np.zeros(INRANGE.size)

    for idx in INRANGE:
        r = lin_r[idx]
        g = lin_g[idx]
        b = lin_b[idx]
        out = np.matmul(np.array([r, g, b]), sRGB_to_prophoto)
        out_r[idx] = out[0]
        out_g[idx] = out[1]
        out_b[idx] = out[2]

    dalivals_float["r"] = np.maximum(
        np.floor(out_r * white[0] * 255 - 0.5).astype(int), 0
    )
    dalivals_float["g"] = np.maximum(
        np.floor(out_g * white[1] * 255 - 0.5).astype(int), 0
    )
    dalivals_float["b"] = np.maximum(
        np.floor(out_b * white[2] * 255 - 0.5).astype(int), 0
    )


    if RGB_NIGHTLIGHT:
        dalivals_float["r"][0] = 254;
        dalivals_float["g"][0] = 254;
        dalivals_float["b"][0] = 254;
    # build relay arrays

    if len(CHANNELS) > 0:
        relay1_needed = np.zeros(INRANGE.size, dtype=int)
        relay2_needed = np.zeros(INRANGE.size, dtype=int)
        for name, dalival in dalivals_float.items():
            selector = dalival > 1
            if name not in CHANNELS:
                continue
            if CHANNELS[name].requires_relay == Relay.RELAY1:
                relay1_needed[selector] = 1
            if CHANNELS[name].requires_relay == Relay.RELAY2:
                relay2_needed[selector] = 1
    else:
        relay1_needed = np.ones(INRANGE.size, dtype=int)
        relay2_needed = np.ones(INRANGE.size, dtype=int)
        relay1_needed[0] = 0
        relay2_needed[0] = 0


    dalivals_float["relay1"] = relay1_needed
    dalivals_float["relay2"] = relay2_needed

    df = pd.DataFrame(dalivals_float, columns=name_columns).astype(int)
    df.plot(title="DALI Values")
    plt.show()
    print(df)
    df.to_csv(FILENAME, index=False)
    # print("hello")
    # print(to_log(to_linear(inrange)))


if __name__ == "__main__":
    main()
