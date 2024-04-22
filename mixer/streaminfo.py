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
        self.map: bytearray = scream_header[3:]  # Two bytes for WAVEFORMATEXTENSIBLE
        """WAVEFORMATEXTENSIBLE"""

    def __eq__(self, _other):
        """Returns if two ScreamStreamInfos equal, minus the WAVEFORMATEXTENSIBLE part"""
        other: StreamInfo = _other
        return (self.sample_rate == other.sample_rate) and (self.bit_depth == other.bit_depth) and (self.channels == other.channels)