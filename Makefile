ifdef $(GENDEV)
ROOTDIR = $(GENDEV)
else
ROOTDIR = /opt/toolchains/sega
endif

LDSCRIPTSDIR = $(ROOTDIR)/ldscripts

LIBPATH = -L$(ROOTDIR)/sh-elf/lib -L$(ROOTDIR)/sh-elf/lib/gcc/sh-elf/4.6.2 -L$(ROOTDIR)/sh-elf/sh-elf/lib
INCPATH = -I. -I$(ROOTDIR)/sh-elf/include -I$(ROOTDIR)/sh-elf/sh-elf/include

CCFLAGS = -m2 -mb -Wall -c -fomit-frame-pointer -fno-builtin  -ffunction-sections -fdata-sections -g
CCFLAGS += -fno-align-loops -fno-align-functions -fno-align-jumps -fno-align-labels
CCFLAGS += -D__32X__ -DMARS

HWFLAGS := $(CCFLAGS)
HWFLAGS += -O1 -fno-lto

CCFLAGS += -O2 -funroll-loops -funsafe-loop-optimizations -fno-align-loops -fno-align-functions -fno-align-jumps -fno-align-labels -lto

LDFLAGS = -T $(LDSCRIPTSDIR)/mars.ld -Wl,-Map=output.map -nostdlib -Wl,--gc-sections --specs=nosys.specs -flto
ASFLAGS = --big

PREFIX = $(ROOTDIR)/sh-elf/bin/sh-elf-
CC = $(PREFIX)gcc
AS = $(PREFIX)as
LD = $(PREFIX)ld
OBJC = $(PREFIX)objcopy

DD = dd
RM = rm -f

TARGET = roq32X
LIBS = $(LIBPATH) -lc -lgcc -lgcc-Os-4-200 -lnosys
OBJS = \
	crt0.o \
	main.o \
	sound.o \
	hw_32x.o \
	font.o \
	roq_read.o \
	roqbase.o \
	blit.o

all: m68k.bin $(TARGET).bin

m68k.bin:
	make -C src-md

$(TARGET).bin: $(TARGET).elf
	$(OBJC) -O binary $< temp.bin
	$(DD) if=temp.bin of=$@ bs=128K conv=sync

$(TARGET).elf: $(OBJS)
	$(CC) $(LDFLAGS) $(OBJS) $(LIBS) -o $(TARGET).elf

crt0.o: | m68k.bin

hw_32x.o: hw_32x.c
	$(CC) $(HWFLAGS) $(INCPATH) $< -o $@

%.o: %.c
	$(CC) $(CCFLAGS) $(INCPATH) $< -o $@

%.o: %.s
	$(AS) $(ASFLAGS) $(INCPATH) $< -o $@

clean:
	make clean -C src-md
	$(RM) *.o *.bin *.elf output.map
