"""Holds stream info and parses Scream headers."""
import traceback
from typing import Union
import numpy
from configuration.type_verification import verify_bit_depth, verify_channel_layout, verify_channels
from configuration.type_verification import verify_sample_rate, CHANNEL_LAYOUT_TABLE

class StreamInfo():
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
        sample_rate_multiplier: int = numpy.packbits(sample_rate_bits,bitorder='little')[0]
        if sample_rate_multiplier < 1:
            sample_rate_multiplier = 1
        self.sample_rate: int = sample_rate_base * sample_rate_multiplier
        """Sample rate in Hz"""
        self.bit_depth: int = scream_header_array[1]  # One byte for bit depth
        """Bit depth"""
        self.channels: int = scream_header_array[2]  # One byte for channel count
        """Channel count"""
        self.channel_mask: bytearray = scream_header_array[3:]  # Two bytes for channel_mask
        """Channel Mask"""
        self.channel_layout: str = self.__parse_channel_mask()
        """Holds the channel layout"""
        self.header: bytearray = scream_header_array
        """Holds the raw header bytes"""

    def __parse_channel_mask(self):
        """Converts the channel mask to a string to be fed to ffmpeg"""
        try:
            return CHANNEL_LAYOUT_TABLE[(self.channel_mask[0], self.channel_mask[1])]
        except KeyError:
            print("".join(["Unknown speaker configuration:",
                           f"{self.channel_mask[0]} {self.channel_mask[1]}",
                           f"({self.channels} channels), defaulting to stereo"]))
            traceback.format_exc()
            return "stereo"

    def __eq__(self, _other):
        """Returns if two ScreamStreamInfos equal"""
        other: StreamInfo = _other
        result: bool = self.sample_rate == other.sample_rate
        result = result and (self.bit_depth == other.bit_depth)
        result = result and (self.channels == other.channels)
        result = result and (self.channel_mask == other.channel_mask)
        return result

def create_stream_info(bit_depth: int,
                       sample_rate: int,
                       channels: int,
                       channel_layout: str) -> StreamInfo:
    """Returns a header with the specified properties"""
    verify_bit_depth(bit_depth)
    verify_sample_rate(sample_rate)
    verify_channels(channels)
    verify_channel_layout(channel_layout)
    header: bytearray = bytearray([0, 0, 0, 0, 0])
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
    return StreamInfo(header)
