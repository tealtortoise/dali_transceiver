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
    name: str
    type: ChannelType
    points: list[tuple[float, float]] | None
    led: LED
    group: int = 0
    night_only: bool = False
    driver_min: float = 0.001
    requires_relay: Relay = Relay.NO_RELAY

    @property
    def is_proportioned(self):
        return self.type == ChannelType.DIRECTED or self.type == ChannelType.RESIDUAL


inrange = np.arange(0, 255, 1)

columns = [
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

minimum_dim = 0.0003

iterations = 400

living_room_channels = {
    "dalie": Channel(
        name="f90",
        points=[(0, 0.0), (0.3, 0.0), (0.7, 1.45), (1.0, 1.45)],
        type=ChannelType.INDEPENDENT,
        led=LED(imax=450 * 2, vf=50, eff=185),
        group=0,
    ),
    "dalia": Channel(
        name="hexagons",
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
        name="piano",
        type=ChannelType.INDEPENDENT,
        points=[(0.0, 1.3), (0.05, 1.1), (0.16, 0.9), (1.0, 0.5)],
        led=LED(vf=46, imax=650, eff=135),
        group=2,
    ),
    "dalib": Channel(
        name="tv 5000k",
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
        name="tv 6500k",
        type=ChannelType.INDEPENDENT,
        points=[(0.0, 1.2), (0.01, 1.0), (0.02, 0.8), (0.16, 0.0), (1.0, 0.0)],
        led=LED(imax=650, vf=33, eff=130),
        group=1,
        night_only=True,
    ),
    "dalid": Channel(
        name="corner 6500k",
        type=ChannelType.INDEPENDENT,
        points=[(0.0, 1.75), (0.02, 1.75), (0.16, 0.0), (1.0, 0.0)],
        led=LED(imax=650, vf=33, eff=130),
        group=0,
        night_only=True,
    ),
}

f90_prop = 0.5
fade = 1.1

bedroom_fullthrive_channels = {
    "dalia": Channel(
        name="5700k Thrive",
        type=ChannelType.INDEPENDENT,
        points=[(0.0, 4), (1.0, 4)],
        led=LED(vf=34, imax=2800, eff=125),
        group=0,
        driver_min=0.001,
        requires_relay=Relay.RELAY1
    ),
    "dalib": Channel(
        name="5000k F90",
        type=ChannelType.INDEPENDENT,
        points=[(0.0, 0.0), (0.28 / fade, 0.0), (0.28 * fade**3, f90_prop),(1.0, f90_prop)],
        led=LED(imax=950, vf=51, eff=177),
        group=0,
        driver_min=0.01,
        requires_relay=Relay.RELAY2
    ),
    "dalic": Channel(
        name="5000k F90 2",
        type=ChannelType.INDEPENDENT,
        points=[(0.0, 0.0), (0.44 / fade, 0.0), (0.44 * fade, f90_prop), (1.0, f90_prop)],
        led=LED(imax=950, vf=51, eff=177),
        group=0,
        driver_min=0.01,
        requires_relay=Relay.RELAY2
    ),
    "dalid": Channel(
        name="5000k F90 3",
        type=ChannelType.INDEPENDENT,
        points=[(0.0, 0.0), (0.58 / fade, 0.0), (0.58 * fade, f90_prop), (1.0, f90_prop)],
        led=LED(imax=950, vf=51, eff=177),
        group=0,
        driver_min=0.01,
        requires_relay=Relay.RELAY2
    ),
    "dalie": Channel(
        name="5000k F90 4",
        type=ChannelType.INDEPENDENT,
        points=[(0.0, 0.0), (0.73 / fade, 0.0), (0.73 * fade, f90_prop), (1.0, f90_prop)],
        led=LED(imax=950, vf=51, eff=177),
        group=0,
        driver_min=0.01,
        requires_relay=Relay.RELAY2
    ),
}

f90_prop = 3.0
fade = 0.08
bedroom_lessthrive_channels = {
    "dalia": Channel(
        name="5700k Thrive",
        type=ChannelType.INDEPENDENT,
        points=[(0.0, 4), (1.0, 4)],
        led=LED(vf=34, imax=2800, eff=125),
        group=0,
        driver_min=0.001,
        requires_relay=Relay.RELAY1
    ),
    "dalib": Channel(
        name="5000k F90",
        type=ChannelType.INDEPENDENT,
        points=[(0.0, 0.0), (0.27 - fade, 0.0), (0.27 + fade, f90_prop),(1.0, f90_prop)],
        led=LED(imax=950, vf=51, eff=177),
        group=0,
        driver_min=0.01,
        requires_relay=Relay.RELAY2
    ),
    "dalic": Channel(
        name="5000k F90 2",
        type=ChannelType.INDEPENDENT,
        points=[(0.0, 0.0), (0.41 - fade, 0.0), (0.41 + fade, f90_prop), (1.0, f90_prop)],
        led=LED(imax=950, vf=51, eff=177),
        group=0,
        driver_min=0.01,
        requires_relay=Relay.RELAY2
    ),
    "dalid": Channel(
        name="5000k F90 3",
        type=ChannelType.INDEPENDENT,
        points=[(0.0, 0.0), (0.58 - fade, 0.0), (0.58 + fade, f90_prop), (1.0, f90_prop)],
        led=LED(imax=950, vf=51, eff=177),
        group=0,
        driver_min=0.01,
        requires_relay=Relay.RELAY2
    ),
    "dalie": Channel(
        name="5000k F90 4",
        type=ChannelType.INDEPENDENT,
        points=[(0.0, 0.0), (0.73 - fade, 0.0), (0.73 + fade, f90_prop), (1.0, f90_prop)],
        led=LED(imax=950, vf=51, eff=177),
        group=0,
        driver_min=0.01,
        requires_relay=Relay.RELAY2
    ),
}

    
if "fadetest" and 0:
    channels = {
        "a": Channel(
            name="a",
            type=ChannelType.INDEPENDENT,
            points=[(0.0, 1.0), (1.0, 0.0)],
            led=LED(1000, 34, 120),
        ),
        "b": Channel(
            name="b",
            type=ChannelType.INDEPENDENT,
            points=[(0.0, 0.0), (1.0, 1.0)],
            led=LED(1000, 34, 120),
        ),
    }
    columns = ["a", "b"]


# channels = living_room_channels
channels = bedroom_lessthrive_channels
if 0 and "No custom channels":
    highest_flux = 1
    channels = {}

highest_flux = max((curve.led.lumens for curve in channels.values()))


max_group = max((channel.group for channel in channels.values()))

total_prop = [0.0] * (max_group + 1)
print(total_prop)
if 1 and "print proportions":
    for key, channel in channels.items():
        print(
            f"Channel {key}: '{channel.name}': {channel.led.power}W {channel.led.lumens} ({(channel.led.lumens / highest_flux)})"
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


# def to_log_custom(inp: np.ndarray, minimum_level: float=minimum_dim) -> np.ndarray:
#     divider = -math.log10(minimum_level)
#     return np.maximum(np.log10(inp / minimum_level) * 253.0 / divider + 1.0, 0)


def to_log(inp: np.ndarray) -> np.ndarray:
    # return to_log_custom(inp)
    return np.maximum(np.log10(inp * 1000) * 253.0 / 3.0 + 1.0, 0)


# native_flux_proportions: dict[str, float] = {}
# for key, value in curves.items():
# native_flux_proportions[key] = value.native_lumens / total_proportioned_flux_available

# print("native flux proportions", native_flux_proportions)

# desired_flux_ary = to_linear_custom(inrange) * desired_total_proportioned_flux

# print("desire flux array", desired_flux_ary)

lin_flux = to_linear_custom(inrange, minimum_dim)
lin_flux[0] = 0.0


def main():
    warnings = []
    dalivals = {}
    lumens = [{} for _ in range(max_group + 1)]

    groupcolumns = [[], [], [], [], []]

    group_offsets = [np.zeros(inrange.size) for _ in range(max_group + 1)]

    for iteration in range(iterations):
        saturated = {key: np.zeros(inrange.size) for key in channels}
        lumensum = [np.zeros(inrange.size) for _ in range(max_group +1)]
        powersum = [np.zeros(inrange.size) for _ in range(max_group +1)]

        for key in columns:
            if key not in channels:
                dalivals[key] = inrange
                continue

            # interpolate curves
            channel = channels[key]
            x, y = zip(*channel.points)
            # print("Processing curve ", key, x, y)
            flux_points = np.array(x, dtype=float)
            prop_points = np.array(y, dtype=float)
            lamp_lumen_ratio = highest_flux / channel.led.lumens
            print(f"Lamp lumen ratio {lamp_lumen_ratio}")
            interpolated_curve = np.interp(
                lin_flux, flux_points, prop_points * lamp_lumen_ratio
            )
            raw_dalivals = (to_log(interpolated_curve * lin_flux) + 0.0).astype(int)
            # lumensum += interpolated_curve * channel;
            # for i, element in enumerate(raw_dalivals):
            # if element > 254:
            # print(f"WARNING! Not enough lumens available at {i}")
            offsets = group_offsets[channel.group].copy()

            # we don't want to turn on lights without a better option
            if iteration <= ((iterations * 2) // 4):
                offsets[raw_dalivals == 0] = 0
            else:
                # we don't want to turn on lights without a better option
                all_saturated = np.all(list(saturated.values()), 0)
                offsets[
                    np.all((raw_dalivals == 0, np.logical_not(all_saturated)), 0)
                ] = 0

            if 0 and iteration > (iterations - 2):
                ## try and smooth things out on last go
                # smoothed_offsets = np.convolve(offsets, np.ones(5) / 5, mode="same")
                # offsets[np.any([raw_dalivals == 0,raw_dalivals == 254], 0)] = smoothed_offsets
                # offsets = smoothed_offsets
                dalivals[key] = np.clip(
                    np.convolve(
                        raw_dalivals + offsets, np.ones(5) / 5, mode="same"),
                    0,
                    254,
                ).astype(int)
            else:
                dalivals[key] = np.clip(raw_dalivals + offsets, 0, 254).astype(int)
            channel_lumens = to_linear_with_minimum(dalivals[key], minimum=channel.driver_min) * channel.led.lumens
            lumens[channel.group][channel.name] = channel_lumens
            lumensum[channel.group] += channel_lumens
            powersum[channel.group] += channel_lumens / channel.led.eff
            if iteration == 0:
                groupcolumns[channel.group].append(channel.name)

        for key in columns:
            if key not in channels:
                dalivals[key] = inrange
                continue
            saturated[key] = np.any((dalivals[key] == 254, dalivals[key] == 0), 0)

        for group in range(max_group + 1):
            if len(lumens[group]) == 0:
                continue
            lumens[group]["sum"] = lumensum[group]
            lumens[group]["ideal"] = lin_flux * highest_flux * total_prop[group]
            group_offsets[group][lumens[group]["sum"] > lumens[group]["ideal"]] -= 0.5
            group_offsets[group][lumens[group]["sum"] < lumens[group]["ideal"]] += 0.5
            
            if iteration == ((iterations * 2) // 4) or iteration == 0:
                lumen_df = pd.DataFrame(
                    lumens[group],
                    columns=groupcolumns[group] + ["sum", "ideal"],
                    index=lin_flux,
                )
                lumen_df.plot(loglog=True, title=f"Lumens Group {group}")
                plt.show()
                lumen_df.plot(loglog=False, title=f"Lumens Group {group}")
                plt.show()

    for key, channel in channels.items():
        print(key, dalivals[key], dalivals[key].size)

    # plt.plot(inrange, lumensum);
    # plt.show();

    name_columns = []
    for name in columns:
        if name in channels:
            name_columns.append(channels[name].name)
            dalivals[channels[name].name] = dalivals[name]
            if name in lumens:
                lumendict = lumens[channels[name].group]
                lumendict[channels[name].name] = lumendict[name]
        else:
            name_columns.append(name)

    # smoothing

    for group in (0,):
        if len(lumens[group]) == 0:
            continue
        lumens[group]["sum"] = lumensum[group]
        lumens[group]["power"] = lumensum[group] / powersum[group] * 100
        lumens[group]["ideal"] = lin_flux * highest_flux * total_prop[group]
        lumen_df = pd.DataFrame(
            lumens[group],
            columns=groupcolumns[group] + ["sum", "ideal", "power"],
            index=lin_flux,
        )
        lumen_df.plot(loglog=True, title=f"Lumens Group {group}")
        plt.show()
        lumen_df.plot(loglog=False, title=f"Lumens Group {group}")
        plt.show()
        print(group_offsets[group])
        plt.plot(inrange, group_offsets[group])
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

    lin_r = np.interp(inrange, colour_reflevels, colour_r)
    lin_g = np.interp(inrange, colour_reflevels, colour_g)
    lin_b = np.interp(inrange, colour_reflevels, colour_b)

    out_r = np.zeros(inrange.size)
    out_g = np.zeros(inrange.size)
    out_b = np.zeros(inrange.size)

    for idx in inrange:
        r = lin_r[idx]
        g = lin_g[idx]
        b = lin_b[idx]
        out = np.matmul(np.array([r, g, b]), sRGB_to_prophoto)
        out_r[idx] = out[0]
        out_g[idx] = out[1]
        out_b[idx] = out[2]

    dalivals["r"] = np.maximum(np.floor(out_r * white[0] * 255 - 0.5).astype(int), 0)
    dalivals["g"] = np.maximum(np.floor(out_g * white[1] * 255 - 0.5).astype(int), 0)
    dalivals["b"] = np.maximum(np.floor(out_b * white[2] * 255 - 0.5).astype(int), 0)

    # build relay arrays
    relay1_needed = np.zeros(inrange.size, dtype=int)
    relay2_needed = np.zeros(inrange.size, dtype=int)

    for name, dalival in dalivals.items():
        selector = dalival > 0
        if name not in channels:
            continue
        if channels[name].requires_relay == Relay.RELAY1:
            relay1_needed[selector] = 1
        if channels[name].requires_relay == Relay.RELAY2:
            relay2_needed[selector] = 1

    dalivals["relay1"] = relay1_needed
    dalivals["relay2"] = relay2_needed

    df = pd.DataFrame(dalivals, columns=name_columns)
    df.plot(title="DALI Values")
    plt.show()
    print(df)
    df.to_csv("spiffs/levelluts4.csv", index=False)
    # print("hello")
    # print(to_log(to_linear(inrange)))


if __name__ == "__main__":
    main()
