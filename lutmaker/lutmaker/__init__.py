#!/usr/bin/python
import math
import typing
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
    scale_max: float = 1.0

    @property
    def usable_lumens(self):
        return self.power * self.eff
    
    @property
    def lumens_at_254(self):
        return self.power * self.eff / self.scale_max

    @property
    def power(self):
        return self.imax * 0.001 * self.vf * self.scale_max


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
    def is_proportioned(self) -> bool:
        return self.type == ChannelType.DIRECTED or self.type == ChannelType.RESIDUAL

    @property
    def contributes_to_full(self) -> bool:
        if not self.points:
            return True
        return self.points[-1][1] > 0

COLUMNS = [
    "level",
    "zeroten1",
    "zeroten2",
    "dalia",
    "dalib",
    "dalic",
    "dalid",
    "dalie",
    "dalif",
    "dalig",
    "dalih",
    "espnow",
    "relay1",
    "relay2",
    "r",
    "g",
    "b",
    "power",
]

# Create colour LUTs
DEFAULT_COLOUR_POINTS = [
    (0, (194, 45, 23)),
    (25, (203, 118, 33)),
    (48, (150, 160, 00)),
    (86, (41, 172, 61)),
    (207, (23, 143, 202)),
    (239, (149, 110, 201)),
    (254, (186, 57, 149)),
]

INRANGE = np.arange(0, 255, 1)


if "fadetest" and 0:
    channels = {
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

# if NO_CUSTOM_CHANNELS:
#     highest_flux = 1
#     max_group = 0
#     CHANNELS = {}
# else:
#     CHANNELS = FULLTHRIVE_CHANNELS


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


def to_log(inp: np.ndarray | float) -> np.ndarray:
    # return to_log_custom(inp)
    return np.maximum(np.log10(inp * 1000) * 253.0 / 3.0 + 1.0, 0.0)


def process(
    channels: typing.Dict[str, Channel] = {},
    minimum_dim: float = 0.001,
    rgb_nightlight: bool = False,
    rgb_nightlight_colour: typing.Tuple[float, float, float] = (254, 254, 254),
    reverse_priority: bool = False,
    filename: str | None = None,
    rgb_led_points: typing.List[
        typing.Tuple[int, typing.Tuple[int, int, int]]
    ] = DEFAULT_COLOUR_POINTS,
    refine_groups: typing.List[int] = [],
    iterations: int = 400,
):
    warnings = []
    dalivals_float = {}
    lin_flux_target = to_linear_custom(INRANGE, minimum_dim)
    lin_flux_target[0] = 0.0

    if channels:
        highest_flux = max((curve.led.usable_lumens for curve in channels.values()))
        max_group = max((channel.group for channel in channels.values()))
        max_priority = max(channel.priority for channel in channels.values())
    else:
        highest_flux = 1
        max_group = 0
        max_priority = 0

    total_prop = [0.0] * (max_group + 1)
    print(total_prop)
    total_power = 0.0
    if 1 and "print proportions":
        for key, channel in channels.items():
            total_power += channel.led.power
            if channel.led.scale_max == 1.0:
                print(
                    f"Channel {key}: '{channel.friendly_name}': {channel.led.power}W {channel.led.usable_lumens} ({(channel.led.usable_lumens / highest_flux)})"
                )
            else:
                dali_max = int(to_log(channel.led.scale_max))
                print(
                    f"Channel {key}: '{channel.friendly_name}': {channel.led.power}W {channel.led.usable_lumens} at DALI {dali_max} ({(channel.led.usable_lumens / highest_flux)})"
                )
            if not channel.night_only:
                total_prop[channel.group] += channel.led.usable_lumens / highest_flux

    for group in range(max_group):
        print(f"Total group {group} daytime proportion {total_prop[group]}")

    print(f"Total Available Power {total_power}W")
    lumens = [{} for _ in range(max_group + 1)]
    # exit()
    groupcolumns = [[], [], [], [], []]

    group_offsets = [np.zeros(INRANGE.size) for _ in range(max_group + 1)]
    channel_offsets = {key: np.zeros(INRANGE.size) for key in channels.keys()}

    for iteration in range(iterations):
        saturated = {key: np.zeros(INRANGE.size) for key in channels}
        lumensum = [np.zeros(INRANGE.size) for _ in range(max_group + 1)]
        lumensum_quantised = [np.zeros(INRANGE.size) for _ in range(max_group + 1)]
        powersum = [np.zeros(INRANGE.size) for _ in range(max_group + 1)]
        total_powersum = np.zeros(INRANGE.size)

        for key in COLUMNS:
            if key not in channels:
                dalivals_float[key] = INRANGE
                continue

            # interpolate curves
            channel = channels[key]
            ch_dali_max = int(to_log(channel.led.scale_max))
            # if channel.led.scale_max != 1.0:
                # ch_dali_max = 254;
            x, y = zip(*channel.points)
            # print("Processing curve ", key, x, y)
            flux_points = np.array(x, dtype=float)
            prop_points = np.array(y, dtype=float)
            lamp_lumen_ratio = highest_flux / channel.led.usable_lumens
            interpolated_curve = np.interp(
                lin_flux_target, flux_points, prop_points * lamp_lumen_ratio
            )
            raw_dali = to_log(np.clip(interpolated_curve * lin_flux_target, 0, channel.led.scale_max))
                # 0.0,
                # to_log(channel.scale_max_power),
            # )

            dalivals_float[key] = np.clip(raw_dali + channel_offsets[key], 0, ch_dali_max)
            channel_lumens = (
                to_linear_with_minimum(dalivals_float[key], minimum=channel.driver_min)
                * channel.led.lumens_at_254
            )
            channel_lumens_quantised = (
                to_linear_with_minimum(
                    (dalivals_float[key]).astype(int), minimum=channel.driver_min
                )
                * channel.led.lumens_at_254
            )
            lumens[channel.group][channel.friendly_name] = channel_lumens
            lumensum[channel.group] += channel_lumens
            lumensum_quantised[channel.group] += channel_lumens_quantised
            power = channel_lumens_quantised / channel.led.eff
            powersum[channel.group] += power
            total_powersum += power
            if iteration == 0:
                groupcolumns[channel.group].append(channel.friendly_name)

        for key in COLUMNS:
            if key not in channels:
                dalivals_float[key] = INRANGE
                continue
            saturated[key] = np.any(
                (dalivals_float[key] == ch_dali_max, dalivals_float[key] == 0), 0
            )

        for group in refine_groups:
            if len(lumens[group]) == 0:
                continue
            group_lumens = lumensum[group]
            target_lumens = lin_flux_target * highest_flux * total_prop[group] * 1.015
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
                if reverse_priority
                else reversed(range(max_priority + 1))
            ):
                channels_in_priority = []
                for ch_key, ch in reversed(channels.items()):
                    if ch.priority == priority:
                        channels_in_priority.append(ch_key)
                num_channels = len(channels_in_priority)

                # first pass we only change existing unsaturated lights
                prio_change = np.zeros(INRANGE.size)
                for ch_key in channels_in_priority:
                    # not_sat = np.logical_not(saturated[ch_key])
                    prio_ch_max = int(to_log(channels[ch_key].led.scale_max))
                    not_sat = dalivals_float[ch_key] > 1
                    old_vals = np.clip(
                        dalivals_float[ch_key] + channel_offsets[ch_key], 0, prio_ch_max
                    )
                    channel_offsets[ch_key][not_sat] += (change / num_channels)[not_sat]
                    diff = (
                        np.clip(
                            dalivals_float[ch_key] + channel_offsets[ch_key], 0, prio_ch_max
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
                    index=lin_flux_target,
                )
                lumen_df.plot(loglog=True, title=f"Lumens Group {group}")
                plt.show()
                lumen_df.plot(loglog=False, title=f"Lumens Group {group}")
                plt.show()

    for key, channel in channels.items():
        print(key, dalivals_float[key], dalivals_float[key].size)

    name_columns = []
    for name in COLUMNS:
        if name in channels:
            name_columns.append(channels[name].friendly_name)
            dalivals_float[channels[name].friendly_name] = dalivals_float[name]
            if name in lumens:
                lumendict = lumens[channels[name].group]
                lumendict[channels[name].friendly_name] = lumendict[name]
        else:
            name_columns.append(name)

    for group in (0,):
        if len(lumens[group]) == 0:
            continue
        lumens[group]["sum"] = lumensum_quantised[group]
        lumens[group]["power"] = lumensum_quantised[group] / powersum[group] * 100
        lumens[group]["ideal"] = lin_flux_target * highest_flux * total_prop[group]
        lumen_df = pd.DataFrame(
            lumens[group],
            columns=groupcolumns[group] + ["sum", "ideal", "power"],
            index=lin_flux_target,
        )
        lumen_df.plot(loglog=True, title=f"Lumens Group {group}")
        plt.show()
        lumen_df.plot(loglog=False, title=f"Lumens Group {group}")
        plt.show()
        # print(group_offsets[group])
        # plt.plot(INRANGE, group_offsets[group])
        # plt.show()
    # exit()

    # df_plot_only = pd.DataFrame({key: value for key, value in dalivals.items() if value is not inrange}, columns=name_columns)
    # df_plot_only.plot(title="DALI Values");
    # plt.show();

    # colourpoints = [(0, (255, 255, 255)),
    # (254, (255, 255, 255))]

    SRGB_TO_PROPHOTO = np.array(
        [
            [0.6274413721, 0.3292974595, 0.0433514584],
            [0.0690276171, 0.9195806669, 0.0113614226],
            [0.0163642351, 0.0880171625, 0.8955649727],
        ]
    ).T

    RGB_FULL_BRIGHTNESS = 0.3
    WHITE_POINT = (
        RGB_FULL_BRIGHTNESS * 1.0,
        RGB_FULL_BRIGHTNESS * 0.78,
        RGB_FULL_BRIGHTNESS * 0.36,
    )

    colour_reflevels = [point[0] for point in rgb_led_points]
    colour_r = [(point[1][0] / 255) ** 2.2 for point in rgb_led_points]
    colour_g = [(point[1][1] / 255) ** 2.2 for point in rgb_led_points]
    colour_b = [(point[1][2] / 255) ** 2.2 for point in rgb_led_points]

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
        out = np.matmul(np.array([r, g, b]), SRGB_TO_PROPHOTO)
        out_r[idx] = out[0]
        out_g[idx] = out[1]
        out_b[idx] = out[2]

    dalivals_float["r"] = np.maximum(
        np.floor(out_r * WHITE_POINT[0] * 255 - 0.5).astype(int), 0
    )
    dalivals_float["g"] = np.maximum(
        np.floor(out_g * WHITE_POINT[1] * 255 - 0.5).astype(int), 0
    )
    dalivals_float["b"] = np.maximum(
        np.floor(out_b * WHITE_POINT[2] * 255 - 0.5).astype(int), 0
    )
    if rgb_nightlight:
        for key in dalivals_float.keys():
            if key not in ("level", "r", "g", "b"):
                dalivals_float[key] = dalivals_float[key].copy()
                dalivals_float[key][1] = 0.0
        dalivals_float["r"][1] = rgb_nightlight_colour[0]
        dalivals_float["g"][1] = rgb_nightlight_colour[1]
        dalivals_float["b"][1] = rgb_nightlight_colour[2]

    # build relay arrays
    if len(channels) > 0:
        relay1_needed = np.zeros(INRANGE.size, dtype=int)
        relay2_needed = np.zeros(INRANGE.size, dtype=int)
        for name, dalival in dalivals_float.items():
            selector = dalival > 1
            if name not in channels:
                continue
            if channels[name].requires_relay == Relay.RELAY1:
                relay1_needed[selector] = 1
            if channels[name].requires_relay == Relay.RELAY2:
                relay2_needed[selector] = 1
    else:
        relay1_needed = np.ones(INRANGE.size, dtype=int)
        relay2_needed = np.ones(INRANGE.size, dtype=int)
        relay1_needed[0] = 0
        relay2_needed[0] = 0
        if rgb_nightlight:
            relay1_needed[1] = 0
            relay2_needed[1] = 0

    dalivals_float["relay1"] = relay1_needed
    dalivals_float["relay2"] = relay2_needed

    dalivals_float["power"] = total_powersum

    dalivals_final_dtype = {}
    for name, ary in dalivals_float.items():
        if name in ("power",):
            dalivals_final_dtype[name] = ary
        else:
            dalivals_final_dtype[name] = ary.astype(int)

    df = pd.DataFrame(dalivals_final_dtype, columns=name_columns)
    df.plot(title="DALI Values")
    plt.show()
    print(df)
    if filename:
        df.to_csv(filename, index=False, float_format="%.8g")
