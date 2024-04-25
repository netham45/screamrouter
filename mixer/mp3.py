import numpy

class MP3header():
    def __init__(self, header: bytes):
        self.mpeg_version = 0
        self.layer_description = 0
        self.protected = 0
        self.bitrate_raw = 0
        self.samplerate_raw = 0
        self.padding = 0
        self.private = 0
        self.channelmode = 0
        self.modeextension = 0
        self.copyright = 0
        self.original = 0
        self.emphasis = 0
        self.samplerate = 0
        self.bitrate = 0
        self.slotsize = 0
        self.framecount = 0
        self.framelength = 0
        self.__mp3_process_header(header)

    def __mp3_parse_bitrate(self, index: int, mpeg_version: int, layer_description: int):
        if index == 0:
            return -1
        elif index == 1:
            if mpeg_version == 1 and layer_description == 1:
                return 32
            elif mpeg_version == 1 and layer_description == 2:
                return 32
            elif mpeg_version == 1 and layer_description == 3:
                return 32
            elif mpeg_version == 2 and layer_description == 1:
                return 32
            elif mpeg_version == 2 and layer_description > 1:
                return 8
        elif index == 2:
            if mpeg_version == 1 and layer_description == 1:
                return 64
            elif mpeg_version == 1 and layer_description == 2:
                return 48
            elif mpeg_version == 1 and layer_description == 3:
                return 40
            elif mpeg_version == 2 and layer_description == 1:
                return 48
            elif mpeg_version == 2 and layer_description > 1:
                return 16
        elif index == 3:
            if mpeg_version == 1 and layer_description == 1:
                return 96
            elif mpeg_version == 1 and layer_description == 2:
                return 56
            elif mpeg_version == 1 and layer_description == 3:
                return 48
            elif mpeg_version == 2 and layer_description == 1:
                return 56
            elif mpeg_version == 2 and layer_description > 1:
                return 24
        elif index == 4:
            if mpeg_version == 1 and layer_description == 1:
                return 128
            elif mpeg_version == 1 and layer_description == 2:
                return 64
            elif mpeg_version == 1 and layer_description == 3:
                return 56
            elif mpeg_version == 2 and layer_description == 1:
                return 64
            elif mpeg_version == 2 and layer_description > 1:
                return 32
        elif index == 5:
            if mpeg_version == 1 and layer_description == 1:
                return 160
            elif mpeg_version == 1 and layer_description == 2:
                return 80
            elif mpeg_version == 1 and layer_description == 3:
                return 64
            elif mpeg_version == 2 and layer_description == 1:
                return 80
            elif mpeg_version == 2 and layer_description > 1:
                return 40
        elif index == 6:
            if mpeg_version == 1 and layer_description == 1:
                return 192
            elif mpeg_version == 1 and layer_description == 2:
                return 96
            elif mpeg_version == 1 and layer_description == 3:
                return 80
            elif mpeg_version == 2 and layer_description == 1:
                return 96
            elif mpeg_version == 2 and layer_description > 1:
                return 48
        elif index == 7:
            if mpeg_version == 1 and layer_description == 1:
                return 224
            elif mpeg_version == 1 and layer_description == 2:
                return 112
            elif mpeg_version == 1 and layer_description == 3:
                return 96
            elif mpeg_version == 2 and layer_description == 1:
                return 112
            elif mpeg_version == 2 and layer_description > 1:
                return 56
        elif index == 8:
            if mpeg_version == 1 and layer_description == 1:
                return 256
            elif mpeg_version == 1 and layer_description == 2:
                return 128
            elif mpeg_version == 1 and layer_description == 3:
                return 112
            elif mpeg_version == 2 and layer_description == 1:
                return 128
            elif mpeg_version == 2 and layer_description > 1:
                return 64
        elif index == 9:
            if mpeg_version == 1 and layer_description == 1:
                return 288
            elif mpeg_version == 1 and layer_description == 2:
                return 160
            elif mpeg_version == 1 and layer_description == 3:
                return 128
            elif mpeg_version == 2 and layer_description == 1:
                return 144
            elif mpeg_version == 2 and layer_description > 1:
                return 80
        elif index == 10:
            if mpeg_version == 1 and layer_description == 1:
                return 320
            elif mpeg_version == 1 and layer_description == 2:
                return 192
            elif mpeg_version == 1 and layer_description == 3:
                return 160
            elif mpeg_version == 2 and layer_description == 1:
                return 160
            elif mpeg_version == 2 and layer_description > 1:
                return 96
        elif index == 11:
            if mpeg_version == 1 and layer_description == 1:
                return 352
            elif mpeg_version == 1 and layer_description == 2:
                return 224
            elif mpeg_version == 1 and layer_description == 3:
                return 192
            elif mpeg_version == 2 and layer_description == 1:
                return 176
            elif mpeg_version == 2 and layer_description > 1:
                return 112
        elif index == 12:
            if mpeg_version == 1 and layer_description == 1:
                return 384
            elif mpeg_version == 1 and layer_description == 2:
                return 256
            elif mpeg_version == 1 and layer_description == 3:
                return 224
            elif mpeg_version == 2 and layer_description == 1:
                return 192
            elif mpeg_version == 2 and layer_description > 1:
                return 128
        elif index == 13:
            if mpeg_version == 1 and layer_description == 1:
                return 416
            elif mpeg_version == 1 and layer_description == 2:
                return 320
            elif mpeg_version == 1 and layer_description == 3:
                return 256
            elif mpeg_version == 2 and layer_description == 1:
                return 224
            elif mpeg_version == 2 and layer_description > 1:
                return 144
        elif index == 14:
            if mpeg_version == 1 and layer_description == 1:
                return 448
            elif mpeg_version == 1 and layer_description == 2:
                return 384
            elif mpeg_version == 1 and layer_description == 3:
                return 320
            elif mpeg_version == 2 and layer_description == 1:
                return 256
            elif mpeg_version == 2 and layer_description > 1:
                return 150
        elif index == 15:
            return -2
        return -1

    def __mp3_parse_frames(self, mpeg_version: int, layer_description: int):
        if mpeg_version == 1:
            if layer_description == 1:
                return 384
            elif layer_description > 1:
                return 1152
        if mpeg_version > 1:
            if layer_description == 1:
                return 384
            elif layer_description == 2:
                return 1152
            elif layer_description == 3:
                return 576
        return -1
    
    def __mp3_process_samplerate(self, samplerate, mpeg_version):
        version = 4 - mpeg_version
        if version == 1:
            if samplerate == 0:
                return 44100
            elif samplerate == 1:
                return 48000
            elif samplerate == 2:
                return 32000
        elif version == 2:
            if samplerate == 0:
                return 22050
            elif samplerate == 1:
                return 24000
            elif samplerate == 2:
                return 16000
        elif version == 3:
            if samplerate == 0:
                return 11025
            elif samplerate == 1:
                return 12000
            elif samplerate == 2:
                return 8000
        return -1

    def __mp3_process_header(self, headerData: bytes):
        data1: numpy.ndarray = numpy.unpackbits(numpy.array([headerData[0]], dtype=numpy.uint8), bitorder='little') 
        data2: numpy.ndarray = numpy.unpackbits(numpy.array([headerData[1]], dtype=numpy.uint8), bitorder='little') 
        data3: numpy.ndarray = numpy.unpackbits(numpy.array([headerData[2]], dtype=numpy.uint8), bitorder='little') 
        data4: numpy.ndarray = numpy.unpackbits(numpy.array([headerData[3]], dtype=numpy.uint8), bitorder='little')
        for i in range(0, 7):
            if data1[i] != 1:
                print("Invalid MP3 Header (data1)")
                return
            
        for i in range(5, 7):
            if data2[i] != 1:
                print("Invalid MP3 Header (data2)")
                return
        self.mpeg_version = numpy.packbits(data2[3:5], bitorder='little')[0]
        self.layer_description = numpy.packbits(data2[1:2], bitorder='little')[0]
        self.protected = data2[0] == 1
        self.bitrate_raw = numpy.packbits(data3[4:8], bitorder='little')[0]
        self.samplerate_raw = numpy.packbits(data3[2:3], bitorder='little')[0]
        self.padding = data3[1]
        self.private = data3[0] == 1
        self.channelmode = numpy.packbits(data4[6:8], bitorder='little')[0]
        self.modeextension = numpy.packbits(data4[4:6], bitorder='little')[0]
        self.copyright = data4[3] == 1
        self.original = data4[2] == 1
        self.emphasis = numpy.packbits(data4[0:2], bitorder='little')[0]
        self.samplerate = self.__mp3_process_samplerate(self.samplerate_raw, self.mpeg_version)
        self.bitrate = self.__mp3_parse_bitrate(self.bitrate_raw, 1 if self.mpeg_version == 3 else 2, (3 - self.layer_description) + 1)
        self.slotsize = 4 if (3 - self.layer_description == 1) else 1
        self.framecount = self.__mp3_parse_frames(1 if self.mpeg_version == 3 else 2, (3 - self.layer_description) + 1)
        self.framelength = (self.bitrate * 1000/8 * self.framecount / self.samplerate + (self.padding * self.slotsize)) - 4