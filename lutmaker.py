#!/usr/bin/python
import math
from dataclasses import dataclass
from enum import Enum
import numpy as np
import pandas as pd


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
        return self.imax * 0.001 * self.vf * self.eff


@dataclass
class Channel(object):
    name: str
    type: ChannelType
    points: list[tuple[float, float]] | None
    led: LED

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
    "zeroten1": Channel(name="f90",
        points=[(0, 0.0), (0.2, 0.0), (0.5, 1.5), (1.0, 1.5)],
        type=ChannelType.INDEPENDENT,
        led=LED(imax=1000, vf=51, eff=180),
    ),
    'dalia': Channel(name="hexagons",
        type=ChannelType.INDEPENDENT,
                     points=[(0.0, 0.0), (0.01, 0.0), (0.05, 1.0), (0.1, 1.0), (0.5, 0.02), (1.0, 0.17)],
                     led=LED(vf=17, imax=800, eff=120)),
    "espnow": Channel(name="piano",
                      type=ChannelType.INDEPENDENT,
                      points=[(0.0, 0.3),(0.05, 0.3), (0.15, 1.0) ,(1.0, 1)],
                      led=LED(vf=34, imax=650, eff=120)),
    "dalib": Channel(name="tv 5000k",
                     type=ChannelType.INDEPENDENT,
                     points=[(0.0, 0.0),(0.01, 0.0) ,(0.1, 0.3), (1.0, 0.3)],
                     led=LED(imax=650, vf=33, eff=120)),
    "dalic": Channel(name="tv 6500k",type=ChannelType.INDEPENDENT,
                     points=[(0.0, 0.3), (0.01, 0.3), (0.1, 0.0), (1.0, 0.0)],
                     led=LED(imax=650, vf=33, eff=120)),
    "dalid": Channel(name="corner 6500k",
                     type=ChannelType.INDEPENDENT,
                     points=[(0.0, 1.0), (0.01, 1.0), (0.1, 0.0), (1.0, 0.0)],
                     led=LED(imax=650, vf=33, eff=120)),
}


highest_flux = max((curve.led.lumens for curve in channels.values()))

if 1 and "print proportions":
    for key, channel in channels.items():
        print(f"Channel {key}: '{channel.name}': {channel.led.lumens} ({(channel.led.lumens / highest_flux)})");


# exit()

def to_linear_custom(inp: np.ndarray, minimum_level: float = minimum_dim) -> np.ndarray:
    divider = -math.log10(minimum_level)
    ary = 10 ** ((inp - 1) / 253.0 * divider) * minimum_level
    ary[0] = 0
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

def main():
    warnings = []
    dalivals = {}


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

        for i, element in enumerate(raw_dalivals):
            if element > 254:
                print(f"WARNING! Not enough lumens available at {i}")
        dalivals[key] = np.clip(raw_dalivals, 0, 254)

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

    name_columns = []
    for name in columns:
        if name in channels:
            name_columns.append(channels[name].name)
            dalivals[channels[name].name] = dalivals[name]
        else:
            name_columns.append(name)
    df = pd.DataFrame(dalivals, columns=name_columns)
    print(df)
    df.to_csv("spiffs/levelluts2.csv", index=False)
    # print("hello")
    # print(to_log(to_linear(inrange)))


if __name__ == "__main__":
    main()
