
CFLAGS = --std=c11 -Wall

INC    = -I/opt/homebrew/Cellar/ffmpeg/7.1.1_1/include
INC   += -I/Users/stoth/GIT/ltntstools-build-environment/target-root/usr/include
INC   += -I/Users/stoth/GIT/ltntstools-build-environment/ffmpeg/libavcodec
INC   += -I/Users/stoth/GIT/ltntstools-build-environment/ffmpeg

LIBS   = -L/opt/homebrew/Cellar/ffmpeg/7.1.1_1/lib -lavformat -lavutil
LIBS  += -L/Users/stoth/GIT/ltntstools-build-environment/target-root/usr/lib -lltntstools -ldvbpsi

all:	probe_uc_01

clean:
	rm -f probe_uc_01

probe_uc_01:	probe_uc_01.c misc.c bitreader.c nal_h264.h nal_h264.c
	gcc $(CFLAGS) $(@).c -o $(@) $(INC) $(LIBS)
