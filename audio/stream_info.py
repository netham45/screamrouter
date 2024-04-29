"""Holds stream info and parses Scream headers."""
from typing import Union
import numpy

class StreamInfo():
    """Parses Scream headers to get sample rate, bit depth, and channels"""
    CHANNEL_LAYOUT_TABLE: dict = {(0x04, 0x00): "mono",
                                  (0x03, 0x00): "stereo",
                                  (0x33, 0x00): "quad",
                                  (0x34, 0x01): "surround",
                                  (0x0F, 0x06): "5.1",  # Surround
                                  (0x3F, 0x06): "7.1",  # Surround
                                  (0x3F, 0x00): "5.1",  # Deprecated
                                  (0xFF, 0x00): "7.1"   # Deprecated
                                  }

    def __init__(self, scream_header: Union[bytearray, bytes]):
        scream_header_array: bytearray = bytearray(scream_header)
        """Parses the first five bytes of a Scream header to get the stream attributes"""
        sample_rate_bits: numpy.ndarray = numpy.unpackbits(numpy.array([scream_header_array[0]], dtype=numpy.uint8), bitorder='little')  # Unpack the first byte into 8 bits
        sample_rate_base: int = 44100 if sample_rate_bits[7] == 1 else 48000  # If the uppermost bit is set then the base is 44100, if it's not set the base is 48000
        sample_rate_bits = numpy.delete(sample_rate_bits, 7)  # Remove the uppermost bit
        sample_rate_multiplier: int = numpy.packbits(sample_rate_bits,bitorder='little')[0]  # Convert it back into a number without the top bit, this is the multiplier to multiply the base by
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
            return self.CHANNEL_LAYOUT_TABLE[(self.channel_mask[0], self.channel_mask[1])]
        except KeyError:
            return "stereo"

    def __eq__(self, _other):
        """Returns if two ScreamStreamInfos equal"""
        other: StreamInfo = _other
        return (self.sample_rate == other.sample_rate) and (self.bit_depth == other.bit_depth) and (self.channels == other.channels) and (self.channel_mask == other.channel_mask)

def create_stream_info(bit_depth: int, sample_rate: int, channels: int, channel_layout: str) -> StreamInfo:
    """Returns a header with the specified properties"""
    header: bytearray = bytearray([0, 0, 0, 0, 0])
    is_441khz: bool = sample_rate % 44100 == 0
    samplerate_multiplier: int = int(sample_rate / (44100 if is_441khz else 48000))
    sample_rate_bits: numpy.ndarray = numpy.unpackbits(numpy.array([samplerate_multiplier], dtype=numpy.uint8), bitorder='little')  # Unpack the first byte into 8 bits
    sample_rate_bits[7] = 1 if is_441khz else 0
    sample_rate_packed: int = int(numpy.packbits(sample_rate_bits, bitorder='little')[0])
    header[0] = sample_rate_packed
    header[1] = bit_depth
    header[2] = channels
    for key, value in StreamInfo.CHANNEL_LAYOUT_TABLE:
        if value == channel_layout:
            header[3], header[4] = key
    return StreamInfo(header)
