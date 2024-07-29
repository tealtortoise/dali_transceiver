#!/usr/bin/python
import math
import numpy as np
import pandas as pd

inrange = np.arange(255)

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
    "relay2"
]

native_lumens = {"zeroten1": 8500, "dali1": 1600}

minimum_dim = 0.0003

desired_max_total_flux = sum(native_lumens.values())

REMAINING = 1

curves = {
    "zeroten1": [(0, 0.0), (0.15, 0.0), (0.38, 1.0), (1.0, 1.0)],
    "dali1": REMAINING,
}


def to_linear_custom(inp: np.ndarray, minimum_level: float=minimum_dim) -> np.ndarray:
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
total_flux_available = sum(native_lumens.values())
for key, value in native_lumens.items():
    native_flux_proportions[key] = value / total_flux_available

print("native flux proportions", native_flux_proportions)

desired_flux_ary = to_linear_custom(inrange) * desired_max_total_flux

print("desire flux array", desired_flux_ary)


def main():
    linear_luts = {}
    props = {}
    fluxes = {}

    sum = np.zeros(255)
    for key, value in curves.items():
        if value == REMAINING:
            continue
        x, y = zip(*value)
        print(key)
        print(x, y)
        flux_points = np.array(x, dtype=float)
        prop_points = np.array(y, dtype=float)
        props[key] = np.interp(to_linear(inrange), flux_points, prop_points)

        sum += props[key]

    fluxsum = np.zeros(255)
    directly_assigned_sum = 0
    for key in props:
        if curves[key] == REMAINING:
            continue
        fluxes[key] = np.minimum(props[key] * desired_flux_ary, native_lumens[key])
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
        if curves[key] != REMAINING:
            continue
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
            raw_dalivals = (to_log(flux / native_lumens[key])+ 0.0).astype(int)
            for i, element in enumerate(raw_dalivals):
                if element > 254:
                    print(f"WARNING! Not enough lumens available at {i}")
            dalivals[key] = np.clip(raw_dalivals, 0, 254)
            continue
        dalivals[key] = np.arange(255)

    df = pd.DataFrame(dalivals, columns=columns);
    print(df)
    df.to_csv("spiffs/levelluts.csv", index=False)
    # print("hello")
    # print(to_log(to_linear(inrange)))


if __name__ == "__main__":
    main()
