import numpy

class InvalidHeaderException(Exception):
    """Called when a header fails to parse"""
    def __init__(self, message: str):
        super().__init__(self, message)

class MP3Header():
    """Parses an MP3 header"""
    def __init__(self, header: bytes):
        self.mpeg_version = 0
        """MPEG Version"""
        self.layer_description = 0
        """MPEG Layer"""
        self.protected = 0
        """Is MP3 protected"""
        self.bitrate_index = 0
        """Bitrate index from header"""
        self.bitrate = 0
        """True bitrate"""
        self.samplerate_index = 0
        """Samplerate index from header"""
        self.samplerate = 0
        """True samplerate"""
        self.padding = 0
        """Rather there's a padding byte to discard or not"""
        self.private = 0
        """Private byte"""
        self.channelmode = 0
        """Channel mode (Mono/LR Stereo/MS Stereo)"""
        self.modeextension = 0
        """Only used for MS Stereo"""
        self.copyright = 0
        """Rather the MP3 is copyright prtoected"""
        self.original = 0
        """Rather the MP3 is an original"""
        self.emphasis = 0
        """Channel emphasis"""
        self.slotsize = 0
        """Slot Size"""
        self.samplecount = 0
        """Number of samples in this frame"""
        self.framelength = 0
        """Length of this frame"""
        self.__mp3_process_header(header)

    def __mp3_parse_bitrate(self, index: int, mpeg_version: int, layer_description: int):
        """Looks up the bitrate from a table"""
        bitrate_table = {
                        0:  # Index 0 (Free)
                            { 1:  # MPEG Version 1
                                [   # Layer Description
                                    -1,  # Layer 1
                                    -1,  # Layer 2
                                    -1   # Layer 3
                                ],
                                2:  # MPEG Version 2
                                [   # Layer Description
                                    -1,  # Layer 1
                                    -1,  # Layer 2
                                    -1   # Layer 3
                                ]
                            },
                        1:  # Index 1
                            { 1:  # MPEG Version 1
                                [   # Layer Description
                                    32,  # Layer 1
                                    32,  # Layer 2
                                    32   # Layer 3
                                ],
                                2:  # MPEG Version 2
                                [   # Layer Description
                                    32,  # Layer 1
                                    8,  # Layer 2
                                    8   # Layer 3
                                ]
                            },
                        2:  # Index 2
                            { 1:  # MPEG Version 1
                                [   # Layer Description
                                    64,  # Layer 1
                                    48,  # Layer 2
                                    40   # Layer 3
                                ],
                                2:  # MPEG Version 2
                                [   # Layer Description
                                    48,  # Layer 1
                                    16,  # Layer 2
                                    16   # Layer 3
                                ]
                            },
                        3:  # Index 3
                            { 1:  # MPEG Version 1
                                [   # Layer Description
                                    96,  # Layer 1
                                    56,  # Layer 2
                                    48   # Layer 3
                                ],
                                2:  # MPEG Version 2
                                [   # Layer Description
                                    56,  # Layer 1
                                    24,  # Layer 2
                                    24   # Layer 3
                                ]
                            },
                        4:  # Index 4
                            { 1:  # MPEG Version 1
                                [   # Layer Description
                                    128,  # Layer 1
                                    64,  # Layer 2
                                    56   # Layer 3
                                ],
                                2:  # MPEG Version 2
                                [   # Layer Description
                                    64,  # Layer 1
                                    32,  # Layer 2
                                    32   # Layer 3
                                ]
                            },
                        5:  # Index 5
                            { 1:  # MPEG Version 1
                                [   # Layer Description
                                    160,  # Layer 1
                                    80,  # Layer 2
                                    64   # Layer 3
                                ],
                                2:  # MPEG Version 2
                                [   # Layer Description
                                    80,  # Layer 1
                                    40,  # Layer 2
                                    40   # Layer 3
                                ]
                            },
                        6:  # Index 6
                            { 1:  # MPEG Version 1
                                [   # Layer Description
                                    192,  # Layer 1
                                    96,  # Layer 2
                                    80   # Layer 3
                                ],
                                2:  # MPEG Version 2
                                [   # Layer Description
                                    96,  # Layer 1
                                    48,  # Layer 2
                                    48   # Layer 3
                                ]
                            },
                        7:  # Index 7
                            { 1:  # MPEG Version 1
                                [   # Layer Description
                                    224,  # Layer 1
                                    112,  # Layer 2
                                    96   # Layer 3
                                ],
                                2:  # MPEG Version 2
                                [   # Layer Description
                                    112,  # Layer 1
                                    56,  # Layer 2
                                    56   # Layer 3
                                ]
                            },
                        8:  # Index 8
                            { 1:  # MPEG Version 1
                                [   # Layer Description
                                    256,  # Layer 1
                                    128,  # Layer 2
                                    112   # Layer 3
                                ],
                                2:  # MPEG Version 2
                                [   # Layer Description
                                    128,  # Layer 1
                                    64,  # Layer 2
                                    64   # Layer 3
                                ]
                            },
                        9:  # Index 9
                            { 1:  # MPEG Version 1
                                [   # Layer Description
                                    288,  # Layer 1
                                    160,  # Layer 2
                                    128   # Layer 3
                                ],
                                2:  # MPEG Version 2
                                [   # Layer Description
                                    144,  # Layer 1
                                    80,  # Layer 2
                                    80   # Layer 3
                                ]
                            },
                        10:  # Index 10
                            { 1:  # MPEG Version 1
                                [   # Layer Description
                                    320,  # Layer 1
                                    192,  # Layer 2
                                    160   # Layer 3
                                ],
                                2:  # MPEG Version 2
                                [   # Layer Description
                                    160,  # Layer 1
                                    96,  # Layer 2
                                    16   # Layer 3
                                ]
                            },
                        11:  # Index 11
                            { 1:  # MPEG Version 1
                                [   # Layer Description
                                    352,  # Layer 1
                                    224,  # Layer 2
                                    192   # Layer 3
                                ],
                                2:  # MPEG Version 2
                                [   # Layer Description
                                    176,  # Layer 1
                                    112,  # Layer 2
                                    112   # Layer 3
                                ]
                            },
                        12:  # Index 12
                            { 1:  # MPEG Version 1
                                [   # Layer Description
                                    384,  # Layer 1
                                    256,  # Layer 2
                                    224   # Layer 3
                                ],
                                2:  # MPEG Version 2
                                [   # Layer Description
                                    192,  # Layer 1
                                    128,  # Layer 2
                                    128   # Layer 3
                                ]
                            },
                        13:  # Index 13
                            { 1:  # MPEG Version 1
                                [   # Layer Description
                                    416,  # Layer 1
                                    320,  # Layer 2
                                    256   # Layer 3
                                ],
                                2:  # MPEG Version 2
                                [   # Layer Description
                                    224,  # Layer 1
                                    144,  # Layer 2
                                    144   # Layer 3
                                ]
                            },
                        14:  # Index 14
                            { 1:  # MPEG Version 1  
                                [   # Layer Description
                                    448,  # Layer 1
                                    384,  # Layer 2
                                    320   # Layer 3
                                ],
                                2:  # MPEG Version 2
                                [   # Layer Description
                                    256,  # Layer 1
                                    160,  # Layer 2
                                    256   # Layer 3
                                ]
                            },
                        15:  # Index 15
                            { 1:  # MPEG Version 1
                                [   # Layer Description
                                    -2,  # Layer 1
                                    -2,  # Layer 2
                                    -2   # Layer 3
                                ],
                                2:  # MPEG Version 2
                                [   # Layer Description
                                    -2,  # Layer 1
                                    -2,  # Layer 2
                                    -2   # Layer 3
                                ]
                            },
                        }
        return bitrate_table[index][mpeg_version][layer_description - 1]

    def __mp3_parse_frames(self, mpeg_version: int, layer_description: int):
        """Looks up the frame count from a table"""
        framecounts = {1: [384, 1152, 1152], 2: [384, 1152, 576]}
        return framecounts[mpeg_version][layer_description - 1]
    
    def __mp3_process_samplerate(self, mpeg_version: int, samplerate: int):
        """Looks up the samplerate from a table"""
        version = 4 - mpeg_version
        samplerates = {1: [44100, 48000, 32000], 2: [22050, 24000, 16000], 3: [11025, 12000, 8000]}
        return samplerates[version][samplerate]

    def __mp3_process_header(self, headerData: bytes):
        """Processes an MP3 header"""
        # See http://www.mp3-tech.org/programmer/frame_header.html
        if len(headerData) < 4:
            raise InvalidHeaderException("Invalid MP3 Header (Too short)")
        byte_1: numpy.ndarray = numpy.unpackbits(numpy.array([headerData[0]], dtype=numpy.uint8), bitorder='little') 
        byte_2: numpy.ndarray = numpy.unpackbits(numpy.array([headerData[1]], dtype=numpy.uint8), bitorder='little') 
        byte_3: numpy.ndarray = numpy.unpackbits(numpy.array([headerData[2]], dtype=numpy.uint8), bitorder='little') 
        byte_4: numpy.ndarray = numpy.unpackbits(numpy.array([headerData[3]], dtype=numpy.uint8), bitorder='little')
        for i in range(0, 7):
            if byte_1[i] != 1:
                raise InvalidHeaderException("Invalid MP3 Header (First byte has invalid marker)")
        for i in range(5, 7):
            if byte_2[i] != 1:
                raise InvalidHeaderException("Invalid MP3 Header (Second byte has invalid marker)")
        self.mpeg_version = numpy.packbits(byte_2[3:5], bitorder='little')[0]
        self.layer_description = numpy.packbits(byte_2[1:2], bitorder='little')[0]
        self.protected = byte_2[0] == 1
        self.bitrate_index = numpy.packbits(byte_3[4:8], bitorder='little')[0]
        self.samplerate_index = numpy.packbits(byte_3[2:3], bitorder='little')[0]
        self.padding = byte_3[1]
        self.private = byte_3[0] == 1
        self.channelmode = numpy.packbits(byte_4[6:8], bitorder='little')[0]
        self.modeextension = numpy.packbits(byte_4[4:6], bitorder='little')[0]
        self.copyright = byte_4[3] == 1
        self.original = byte_4[2] == 1
        self.emphasis = numpy.packbits(byte_4[0:2], bitorder='little')[0]
        self.samplerate = self.__mp3_process_samplerate(self.mpeg_version, self.samplerate_index)
        self.bitrate = self.__mp3_parse_bitrate(self.bitrate_index, 1 if self.mpeg_version == 3 else 2, (3 - self.layer_description) + 1)
        self.slotsize = 4 if (3 - self.layer_description == 1) else 1   
        self.samplecount = self.__mp3_parse_frames(1 if self.mpeg_version == 3 else 2, (3 - self.layer_description) + 1)
        self.framelength = int((self.bitrate * 1000 / 8 * self.samplecount / self.samplerate + (self.padding * self.slotsize)) - 4)