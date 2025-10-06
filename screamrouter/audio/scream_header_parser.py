"""Holds stream info and parses Scream headers."""
import traceback
from typing import Tuple, Union

import numpy

from screamrouter.screamrouter_logger.screamrouter_logger import get_logger
from screamrouter.screamrouter_types.annotations import (BitDepthType,
                                                ChannelLayoutType,
                                                ChannelsType, SampleRateType)

logger = get_logger(__name__)

CHANNEL_LAYOUT_TABLE: dict[Tuple[int,int], str] = {(0x00, 0x00): "stereo", # No layout
                                                   (0x04, 0x00): "mono",
                                                   (0x03, 0x00): "stereo",
                                                   (0x33, 0x00): "quad",
                                                   (0x34, 0x01): "surround",
                                                   (0x0F, 0x00): "3.1",
                                                   (0x07, 0x01): "4.0",
                                                   (0x0F, 0x06): "5.1(side)",  # 5.1 Side
                                                   (0x3F, 0x06): "7.1",
                                                   (0x3F, 0x00): "5.1"} # 5.1 rear

class ScreamHeader():
    """Parses Scream headers to get sample rate, bit depth, and channels"""

    def __init__(self, scream_header: Union[bytearray, bytes]):
        scream_header_array: bytearray = bytearray(scream_header)
        """Parses the first five bytes of a Scream header to get the stream attributes"""
        # Unpack the first byte into 8 bits
        sample_rate_bits: numpy.ndarray = numpy.unpackbits(numpy.array([scream_header_array[0]],
                                                                       dtype=numpy.uint8),
                                                                       bitorder='little')
        # If the uppermost bit is set then the base is 44100, if it's not set the base is 48000
        sample_rate_base: int = 44100 if sample_rate_bits[7] == 1 else 48000
        sample_rate_bits = numpy.delete(sample_rate_bits, 7)  # Remove the uppermost bit
        # Convert it back into a number without the top bit, this is the multiplier
        sample_rate_multiplier: int = int(numpy.packbits(sample_rate_bits,bitorder='little')[0])
        if sample_rate_multiplier < 1:
            sample_rate_multiplier = 1
        # Bypassing pydantic verification for these
        self.sample_rate: SampleRateType = sample_rate_base * sample_rate_multiplier # type: ignore
        """Sample rate in Hz"""
        self.bit_depth: BitDepthType = scream_header_array[1] # type: ignore
        """Bit depth"""
        self.channels: ChannelsType = scream_header_array[2] # type: ignore
        """Channel count"""
        self.channel_mask: bytes = scream_header_array[3:] # type: ignore
        """Channel Mask"""
        self.channel_layout: ChannelLayoutType
        self.channel_layout = self.__parse_channel_mask(
            bytes(scream_header_array[3:])) # type: ignore
        """Holds the channel layout"""
        self.header: bytes = scream_header_array
        """Holds the raw header bytes"""

    def __parse_channel_mask(self, channel_mask: bytes) -> str:
        """Converts the channel mask to a string for ffmpeg"""
        try:
            return CHANNEL_LAYOUT_TABLE[(channel_mask[0], channel_mask[1])]
        except KeyError as exc:
            logger.warning("Unknown speaker configuration bytes: %s, defaulting to stereo",
                           exc)
            traceback.format_exc()
            return "stereo"

    def __eq__(self, _other):
        """Returns if two ScreamStreamInfos equal"""
        other: ScreamHeader = _other
        result: bool = self.sample_rate == other.sample_rate
        result = result and (self.bit_depth == other.bit_depth)
        result = result and (self.channels == other.channels)
        result = result and (self.channel_mask == other.channel_mask)
        return result

def create_stream_info(bit_depth: BitDepthType,
                       sample_rate: SampleRateType,
                       channels: ChannelsType,
                       channel_layout: ChannelLayoutType) -> ScreamHeader:
    """Returns a header with the specified properties"""
    header: bytearray = bytearray([0, 32, 2, 0, 0])
    is_441khz: bool = sample_rate % 44100 == 0
    samplerate_multiplier: int = int(sample_rate / (44100 if is_441khz else 48000))
    sample_rate_bits: numpy.ndarray = numpy.unpackbits(numpy.array([samplerate_multiplier],
                                                                   dtype=numpy.uint8),
                                                                   bitorder='little')
    sample_rate_bits[7] = 1 if is_441khz else 0
    sample_rate_packed: int = int(numpy.packbits(sample_rate_bits, bitorder='little')[0])
    header[0] = sample_rate_packed
    header[1] = bit_depth
    header[2] = channels
    for key, value in CHANNEL_LAYOUT_TABLE.items():
        if value == channel_layout:
            header[3], header[4] = key
    return ScreamHeader(header)
