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
class Channel(object):
    type: ChannelType
    points: list[tuple[float, float]] | None
    native_lumens: float

    @property
    def is_proportioned(self):
        return self.type == ChannelType.DIRECTED or self.type == ChannelType.RESIDUAL


inrange = np.arange(0, 255, 1)

columns = [
    "level",
    "zeroten1",
    "zeroten2",
    "dali1",
    "dali2",
    "dali3",
    "dali4",
    "espnow",
    "relay1",
    "relay2",
]

# native_lumens = {"zeroten1": 8500, "dali1": 2300}

minimum_dim = 0.001

curves = {
    "zeroten1": Channel(
        points=[(0, 0.0), (0.22, 0.0), (0.38, 1.0), (1.0, 1.0)],
        type=ChannelType.DIRECTED,
        native_lumens=8500,
    ),
    "dali1": Channel(type=ChannelType.RESIDUAL, points=None, native_lumens=2300),
    "espnow": Channel(type=ChannelType.INDEPENDENT, points=[(0.0, 0.0), (1.0, 1.3)], native_lumens=0),
    "dali2": Channel(type=ChannelType.INDEPENDENT, points=[(0.0, 0.0), (1.0, 1.3)], native_lumens=0)
}

total_proportioned_flux_available = sum(curve.native_lumens for curve in curves.values() if curve.is_proportioned);
desired_total_proportioned_flux = total_proportioned_flux_available;

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


native_flux_proportions: dict[str, float] = {}
for key, value in curves.items():
    native_flux_proportions[key] = value.native_lumens / total_proportioned_flux_available

print("native flux proportions", native_flux_proportions)

desired_flux_ary = to_linear_custom(inrange) * desired_total_proportioned_flux

print("desire flux array", desired_flux_ary)


def main():
    linear_luts = {}
    props = {}
    fluxes = {}

    sum = np.zeros(inrange.size)
    for (key, value) in curves.items():
        if value.type == ChannelType.RESIDUAL:
            continue
        x, y = zip(*value.points)
        print(key, x, y)
        flux_points = np.array(x, dtype=float)
        prop_points = np.array(y, dtype=float)
        props[key] = np.interp(to_linear(inrange), flux_points, prop_points)

        sum += props[key]
        print(props[key])


    fluxsum = np.zeros(inrange.size)
    directly_assigned_sum = 0
    for key in props:
        if curves[key].type == ChannelType.DIRECTED:
            fluxes[key] = np.minimum(props[key] * desired_flux_ary, curves[key].native_lumens)
            fluxsum += fluxes[key]
            print(key, fluxes[key])
            directly_assigned_sum += native_flux_proportions[key]

    remaining_prop = 1.0 - sum
    remaining_flux = desired_flux_ary - fluxsum
    print("remaining prop", remaining_prop)
    print("remaining flux", remaining_flux)
    print("directly proportioned sum", directly_assigned_sum)
    remaining_proportion = 1.0 - directly_assigned_sum

    # apportion remaining flux that hasn't been directly assigned
    for key in native_flux_proportions:
        if curves[key].type == ChannelType.RESIDUAL:
            fluxes[key] = (
                remaining_flux * native_flux_proportions[key] / remaining_proportion
            )
            print(key, fluxes[key])

    # convert fluxes to dali values

    warnings = []
    dalivals = {}
    for key in columns:
        if key in fluxes:
            flux = fluxes[key]
            raw_dalivals = (to_log(flux / curves[key].native_lumens) + 0.0).astype(int)
            for i, element in enumerate(raw_dalivals):
                if element > 254:
                    print(f"WARNING! Not enough lumens available at {i}")
            dalivals[key] = np.clip(raw_dalivals, 0, 254)
        elif key not in curves:
            dalivals[key] = inrange
        elif curves[key].type == ChannelType.INDEPENDENT:
            raw_dalivals = (to_log(props[key]) + 0.0).astype(int)
            for i, element in enumerate(raw_dalivals):
                if element > 254:
                    print(f"WARNING! Not enough lumens available at {i}")
            dalivals[key] = np.clip(raw_dalivals, 0, 254)


    for key, channel in curves.items():
        print(key, dalivals[key], dalivals[key].size)


    df = pd.DataFrame(dalivals, columns=columns)
    print(df)
    df.to_csv("spiffs/levelluts2.csv", index=False)
    # print("hello")
    # print(to_log(to_linear(inrange)))


if __name__ == "__main__":
    main()
