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

    @property
    def is_proportioned(self):
        return self.type == ChannelType.DIRECTED or self.type == ChannelType.RESIDUAL


inrange = np.arange(0, 255, 1)

columns = [
    "level",
    "zeroten1", #F90s
    "zeroten2",
    "dalia", #hexagons 3
    "dalib", #tv5000 1
    "dalic", # tv6500 0
    "dalid", # back 6500 2
    "dalie",
    "dalif",
    "espnow", # piano
    "relay1",
    "relay2",
]

minimum_dim = 0.001

channels = {
    "dalie": Channel(name="f90",
        points=[(0, 0.0), (0.3, 0.0), (0.7, 1.45), (1.0, 1.45)],
        type=ChannelType.INDEPENDENT,
        led=LED(imax=450 * 2, vf=50, eff=185),
        group=0,
    ),
    'dalia': Channel(name="hexagons",
        type=ChannelType.INDEPENDENT,
                     points=[(0.0, 0.0), (0.02, 0.0), (0.16, 1.85), (0.3, 1.75), (0.7, 0.38), (1.0, 0.83)],
                     led=LED(vf=17, imax=650*5, eff=120), group=0),
    "espnow": Channel(name="piano",
                      type=ChannelType.INDEPENDENT,
                      points=[(0.0, 1.3), (0.05, 1.1) ,(0.16, 0.9),(1.0, 0.5)],
                      led=LED(vf=46, imax=650, eff=135), group=2),
    "dalib": Channel(name="tv 5000k",
                     type=ChannelType.INDEPENDENT,
                     points=[(0.0, 0.0),(0.02, 0.0) ,(0.16, 0.8),(0.23, 0.7),(0.45, 0.58), (0.6, 0.48), (1.0, 0.31)],
                     led=LED(imax=650, vf=33, eff=120), group=1),
    "dalic": Channel(name="tv 6500k",type=ChannelType.INDEPENDENT,
                     points=[(0.0, 1.2),(0.01, 1.0),  (0.02, 0.8), (0.16, 0.0), (1.0, 0.0)],
                     led=LED(imax=650, vf=33, eff=130), group=1, night_only=True),
    "dalid": Channel(name="corner 6500k",
                     type=ChannelType.INDEPENDENT,
                     points=[(0.0, 1.75), (0.02, 1.75), (0.16, 0.0), (1.0, 0.0)],
                     led=LED(imax=650, vf=33, eff=130), group=0, night_only=True),
}

if "fadetest" and 0:
    channels = {
        "a":Channel(name="a", type=ChannelType.INDEPENDENT, points=[(0.0,1.0), (1.0, 0.0)], led=LED(1000, 34, 120)),
        "b":Channel(name="b", type=ChannelType.INDEPENDENT, points=[(0.0,0.0), (1.0, 1.0)], led=LED(1000, 34, 120)),
    }
    columns = ["a", "b"]

highest_flux = max((curve.led.lumens for curve in channels.values()))

total_prop = [0.0, 0.0, 0.0, 0.0, 0.0,]
if 1 and "print proportions":
    for key, channel in channels.items():
        print(f"Channel {key}: '{channel.name}': {channel.led.power}W {channel.led.lumens} ({(channel.led.lumens / highest_flux)})");
        if not channel.night_only:
            total_prop[channel.group] += channel.led.lumens / highest_flux

for group in range(5):
    print(f"Total group {group} daytime proportion {total_prop[group]}")


# exit()

def to_linear_custom(inp: np.ndarray, minimum_level: float = minimum_dim) -> np.ndarray:
    divider = -math.log10(minimum_level)
    ary = 10 ** ((inp - 1) / 253.0 * divider) * minimum_level
    ary[inp == 0] = 0;
    # for i in range(len(inp)):
    #     if inp[i] == 0:
    #         ary[i] = 0;
    return ary


def to_linear(inp: np.ndarray) -> np.ndarray:
    return to_linear_custom(inp)


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

lin_flux = to_linear(inrange);
lin_flux[0] = 0.0;

def main():
    warnings = []
    dalivals = {}
    lumens = [{}, {}, {}, {}, {}]
    
    groupcolumns = [[], [], [], [], []]


    lumensum = [np.zeros(inrange.size), np.zeros(inrange.size), np.zeros(inrange.size), np.zeros(inrange.size), np.zeros(inrange.size)]

    for key in columns:
        if key not in channels:
            dalivals[key] = inrange
            continue

        # interpolate curves
        channel = channels[key]
        x, y = zip(*channel.points)
        print("Processing curve ", key, x, y)
        flux_points = np.array(x, dtype=float)
        prop_points = np.array(y, dtype=float)
        lamp_lumen_ratio = highest_flux / channel.led.lumens
        print(f"Lamp lumen ratio {lamp_lumen_ratio}")
        interpolated_curve = np.interp(lin_flux, flux_points, prop_points * lamp_lumen_ratio);
        raw_dalivals = (to_log(interpolated_curve * lin_flux) + 0.0).astype(int)
        # lumensum += interpolated_curve * channel;
        for i, element in enumerate(raw_dalivals):
            if element > 254:
                print(f"WARNING! Not enough lumens available at {i}")
        dalivals[key] = np.clip(raw_dalivals, 0, 254)
        channel_lumens = to_linear(dalivals[key]) * channel.led.lumens
        lumens[channel.group][channel.name] = channel_lumens
        lumensum[channel.group] += channel_lumens
        groupcolumns[channel.group].append(channel.name)

        # if key in fluxes:
        #     flux = fluxes[key]
        #     raw_dalivals = (to_log(flux / curves[key].native_lumens) + 0.0).astype(int)
        #     for i, element in enumerate(raw_dalivals):
        #         if element > 254:
        #             print(f"WARNING! Not enough lumens available at {i}")
        #     dalivals[key] = np.clip(raw_dalivals, 0, 254)
        # elif key not in curves:
        #     dalivals[key] = inrange
        # elif curves[key].type == ChannelType.INDEPENDENT:
        #     raw_dalivals = (to_log(props[key] * to_linear(inrange)) + 0.0).astype(int)
        #     for i, element in enumerate(raw_dalivals):
        #         if element > 254:
        #             print(f"WARNING! Not enough lumens available at {i}")
        #     dalivals[key] = np.clip(raw_dalivals, 0, 254)


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

    for group in (2,):
        if len(lumens[group]) == 0: continue
        lumens[group]['sum'] = lumensum[group];
        lumens[group]['ideal'] = lin_flux * highest_flux * total_prop[group];
        lumen_df = pd.DataFrame(lumens[group], columns=groupcolumns[group] + ['sum', 'ideal'], index=lin_flux);
        lumen_df.plot(loglog=True, title=f"Lumens Group {group}")
        plt.show()
        lumen_df.plot(loglog=False, title=f"Lumens Group {group}")
        plt.show()
    # exit()

    df_plot_only = pd.DataFrame({key: value for key, value in dalivals.items() if value is not inrange}, columns=name_columns)
    df_plot_only.plot(title="DALI Values");
    plt.show();

    df = pd.DataFrame(dalivals, columns=name_columns)
    # df.plot(title="DALI Values");
    print(df)
    df.to_csv("spiffs/levelluts2.csv", index=False)
    # print("hello")
    # print(to_log(to_linear(inrange)))


if __name__ == "__main__":
    main()
