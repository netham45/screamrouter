import numpy

class StreamInfo():
    """Parses Scream headers to get sample rate, bit depth, and channels"""
    def __init__(self, scream_header):
        """Parses the first five bytes of a Scream header to get the stream attributes"""
        sample_rate_bits: numpy.ndarray = numpy.unpackbits(numpy.array([scream_header[0]], dtype=numpy.uint8), bitorder='little')  # Unpack the first byte into 8 bits
        sample_rate_base: int = 44100 if sample_rate_bits[7] == 1 else 48000  # If the uppermost bit is set then the base is 44100, if it's not set the base is 48000
        sample_rate_bits = numpy.delete(sample_rate_bits, 7)  # Remove the uppermost bit
        sample_rate_multiplier: int = numpy.packbits(sample_rate_bits,bitorder='little')[0]  # Convert it back into a number without the top bit, this is the multiplier to multiply the base by
        if sample_rate_multiplier < 1:
            sample_rate_multiplier = 1
        self.sample_rate: int = sample_rate_base * sample_rate_multiplier
        """Sample rate in Hz"""
        self.bit_depth: int = scream_header[1]  # One byte for bit depth
        """Bit depth"""
        self.channels: int = scream_header[2]  # One byte for channel count
        """Channel count"""
        self.channel_mask: bytearray = scream_header[3:]  # Two bytes for channel_mask
        """Channel Mask"""

        self.channel_layout: str = "stereo"
        """Holds the channel layout"""
        self.__parse_channel_mask()

    def __parse_channel_mask(self):
        """Converts the channel mask to a string to be fed to ffmpeg"""
        self.channel_layout: str = "stereo"
        #print(f"Got mask {self.channel_mask[0]} {self.channel_mask[1]}")
        if   self.channel_mask[1] == 0x00 and self.channel_mask[0] == 0x04:  # KSAUDIO_SPEAKER_MONO
            self.channel_layout = "mono"
        elif self.channel_mask[1] == 0x00 and self.channel_mask[0] == 0x03:  # KSAUDIO_SPEAKER_STEREO
            self.channel_layout = "stereo"
        elif self.channel_mask[1] == 0x00 and self.channel_mask[0] == 0x33:  # KSAUDIO_SPEAKER_QUAD
            self.channel_layout = "quad"
        elif self.channel_mask[1] == 0x01 and self.channel_mask[0] == 0x34:  # KSAUDIO_SPEAKER_SURROUND
            self.channel_layout = "4.0"
        elif self.channel_mask[1] == 0x00 and self.channel_mask[0] == 0x3F:  # KSAUDIO_SPEAKER_5POINT1
            self.channel_layout = "5.1"
        elif self.channel_mask[1] == 0x00 and self.channel_mask[0] == 0xFF:  # KSAUDIO_SPEAKER_7POINT1
            self.channel_layout = "7.1"
        elif self.channel_mask[1] == 0x06 and self.channel_mask[0] == 0x0F:  # KSAUDIO_SPEAKER_5POINT1_SURROUND
            self.channel_layout = "5.1"
        elif self.channel_mask[1] == 0x06 and self.channel_mask[0] == 0x3F:  # KSAUDIO_SPEAKER_7POINT1_SURROUND
            self.channel_layout = "7.1"

    def __eq__(self, _other):
        """Returns if two ScreamStreamInfos equal"""
        other: StreamInfo = _other
        return (self.sample_rate == other.sample_rate) and (self.bit_depth == other.bit_depth) and (self.channels == other.channels) and (self.channel_mask == other.channel_mask)