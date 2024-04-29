"""Holds verification functions"""
import ipaddress
import traceback
from typing import Tuple

CHANNEL_LAYOUT_TABLE: dict[Tuple[int,int], str] = {(0x00, 0x00): "stereo", # No layout
                                                   (0x04, 0x00): "mono",
                                                   (0x03, 0x00): "stereo",
                                                   (0x33, 0x00): "quad",
                                                   (0x34, 0x01): "surround",
                                                   (0x0F, 0x06): "5.1",  # Surround
                                                   (0x3F, 0x06): "7.1",  # Surround
                                                   (0x3F, 0x00): "5.1",  # Deprecated
                                                   (0xFF, 0x00): "7.1"}   # Deprecated

def verify_ip(ip: str) -> None:
    """Verifies an ip address can be parsed correctly"""
    try:
        ipaddress.ip_address(ip)  # Verify IP address is formatted right
    except ValueError as exc:
        raise ValueError("Invalid IP address {ip}") from exc

def verify_port(port: int) -> None:
    """Verifies a port is between 1 and 65535"""
    if port < 1 or port > 65535:
        raise ValueError(f"Invalid port {port}")

def verify_name(name: str) -> None:
    """Verifies a name is non-blank"""
    if len(name) == 0:
        raise ValueError("Invalid name (Blank)")

def verify_volume(volume: float) -> None:
    """Verifies a volume is between 0 and 1"""
    if volume < 0 or volume > 1:
        raise ValueError(f"Invalid Volume {volume} is not between 0 and 1")

def verify_sample_rate(sample_rate: int) -> None:
    """Verifies a sample rate has a base of 44.1 or 48 kHz"""
    if sample_rate % 44100 != 0 and sample_rate % 48000 != 0:
        raise ValueError("Unknown sample rate base")

def verify_bit_depth(bit_depth: int) -> None:
    """Verifies bit depth is 16, 24, or 32"""
    if bit_depth == 24:
        print("WARNING: Using 24-bit depth is not recommended.")
        print(traceback.format_exc())
    if bit_depth != 16 and bit_depth != 24 and bit_depth != 32:
        raise ValueError("Invalid Bit Depth")

def verify_channels(channels: int) -> None:
    """Verifies the channel count is between 1 and 8"""
    if channels < 1 or channels > 8:
        raise ValueError("Invalid Channel Count")

def verify_channel_layout(channel_layout: str) -> None:
    """Verifies the channel layout string is known"""
    for layout in CHANNEL_LAYOUT_TABLE.values():
        if layout == channel_layout:
            return
    raise ValueError("".join([f"Invalid Channel Layout {channel_layout}",
                              f"Valid channel layouts: {CHANNEL_LAYOUT_TABLE.values()}"]))
