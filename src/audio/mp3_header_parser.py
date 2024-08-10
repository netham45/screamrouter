"""Holds the MP3Header class that parses MP3 headers"""
import numpy

from src.screamrouter_logger.screamrouter_logger import get_logger

logger = get_logger(__name__)

class InvalidHeaderException(Exception):
    """Called when a header fails to parse"""
    def __init__(self, message: str):
        super().__init__(message)

class MP3Header:
    """Parses an MP3 header"""
    def __init__(self, header: bytes):
        self.mpeg_version: int = 0
        self.layer_description: int = 0
        self.protected: bool = False
        self.bitrate_index: int = 0
        self.bitrate: int = 0
        self.samplerate_index: int = 0
        self.samplerate: int = 0
        self.padding: bool = False
        self.private: int = 0
        self.channelmode: int = 0
        self.modeextension: int = 0
        self.copyright: int = 0
        self.original: bool = False
        self.emphasis: int = 0
        self.slotsize: int = 0
        self.samplecount: int = 0
        self.framelength: int = 0
        self.__mp3_process_header(header)

    def __mp3_parse_bitrate(self, index: int, mpeg_version: int, layer_description: int) -> int:
        """Looks up the bitrate from a table"""
        bitrate_table = {
            0: {1: [-1, -1, -1], 2: [-1, -1, -1]},
            1: {1: [32, 32, 32], 2: [32, 8, 8]},
            2: {1: [64, 48, 40], 2: [48, 16, 16]},
            3: {1: [96, 56, 48], 2: [56, 24, 24]},
            4: {1: [128, 64, 56], 2: [64, 32, 32]},
            5: {1: [160, 80, 64], 2: [80, 40, 40]},
            6: {1: [192, 96, 80], 2: [96, 48, 48]},
            7: {1: [224, 112, 96], 2: [112, 56, 56]},
            8: {1: [256, 128, 112], 2: [128, 64, 64]},
            9: {1: [288, 160, 128], 2: [144, 80, 80]},
            10: {1: [320, 192, 160], 2: [160, 96, 16]},
            11: {1: [352, 224, 192], 2: [176, 112, 112]},
            12: {1: [384, 256, 224], 2: [192, 128, 128]},
            13: {1: [416, 320, 256], 2: [224, 144, 144]},
            14: {1: [448, 384, 320], 2: [256, 160, 256]},
            15: {1: [-2, -2, -2], 2: [-2, -2, -2]},
        }
        return bitrate_table[index][mpeg_version][layer_description - 1]

    def __mp3_parse_frames(self, mpeg_version: int, layer_description: int) -> int:
        """Looks up the frame count from a table"""
        framecounts = {1: [384, 1152, 1152], 2: [384, 1152, 576]}
        return framecounts[mpeg_version][layer_description - 1]

    def __mp3_process_samplerate(self, mpeg_version: int, samplerate: int) -> int:
        """Looks up the samplerate from a table"""
        version = 4 - mpeg_version
        samplerates = {1: [44100, 48000, 32000], 2: [22050, 24000, 16000], 3: [11025, 12000, 8000]}
        return samplerates[version][samplerate]

    def __mp3_process_header(self, header_data: bytes) -> None:
        """Processes an MP3 header"""
        if len(header_data) < 4:
            raise InvalidHeaderException("Invalid MP3 Header (Too short)")

        bytes_data = [numpy.unpackbits(numpy.array([header_data[i]],
                                                   dtype=numpy.uint8),
                                                   bitorder='little') for i in range(4)]

        if (not all(bytes_data[0][i] == 1 for i in range(7)) or
            not all(bytes_data[1][i] == 1 for i in range(5, 7))):
            raise InvalidHeaderException("Invalid MP3 Header (Invalid marker)")
        try:
            self.mpeg_version = int(numpy.packbits(bytes_data[1][3:5], bitorder='little')[0])
            self.layer_description = int(numpy.packbits(bytes_data[1][1:2], bitorder='little')[0])
            self.protected = bytes_data[1][0] == 1
            self.bitrate_index = int(numpy.packbits(bytes_data[2][4:8], bitorder='little')[0])
            self.samplerate_index = int(numpy.packbits(bytes_data[2][2:3], bitorder='little')[0])
            self.padding = bytes_data[2][1]
            self.private = bytes_data[2][0] == 1
            self.channelmode = numpy.packbits(bytes_data[3][6:8], bitorder='little')[0]
            self.modeextension = numpy.packbits(bytes_data[3][4:6], bitorder='little')[0]
            self.copyright = bytes_data[3][3] == 1
            self.original = bytes_data[3][2] == 1
            self.emphasis = numpy.packbits(bytes_data[3][0:2], bitorder='little')[0]
            self.samplerate = int(self.__mp3_process_samplerate(self.mpeg_version,
                                                                self.samplerate_index))
            self.bitrate = self.__mp3_parse_bitrate(self.bitrate_index, 1 if
                                                    self.mpeg_version == 3 else
                                                    2, (3 - self.layer_description) + 1)
            self.slotsize = 4 if (3 - self.layer_description == 1) else 1
            self.samplecount = self.__mp3_parse_frames(1 if self.mpeg_version == 3 else
                                                       2, (3 - self.layer_description) + 1)
            self.framelength = int((self.bitrate * 1000 / 8 * self.samplecount / self.samplerate +
                                   (self.padding * self.slotsize)) - 4)
        except Exception as exc:
            raise InvalidHeaderException("Invalid MP3 Header (Failed to parse)") from exc
