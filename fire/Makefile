include ../Makefile.inc

DOS33 = ../dos33fs-utils/dos33
PNG_TO_RLE = ../gr-utils/png2rle

all:	fire.dsk

fire.dsk:	FIRE FIRE_TINY FIRE_FIRMWARE FIRE_EXTREME FIRE_QKUMBA HELLO COOL_EFFECT RASTER
	$(DOS33) -y fire.dsk SAVE A HELLO
	$(DOS33) -y -a 0x1000 fire.dsk BSAVE FIRE
	$(DOS33) -y -a 0x70 fire.dsk BSAVE FIRE_TINY
	$(DOS33) -y -a 0x70 fire.dsk BSAVE FIRE_FIRMWARE
	$(DOS33) -y -a 0x70 fire.dsk BSAVE FIRE_EXTREME
	$(DOS33) -y -a 0x70 fire.dsk BSAVE FIRE_QKUMBA
	$(DOS33) -y -a 0x70 fire.dsk BSAVE RASTER
	$(DOS33) -y -a 0x70 fire.dsk BSAVE COOL_EFFECT

####

FIRE:	fire.o
	$(CC65BIN)/ld65 -o fire fire.o -C ../linker_scripts/apple2_1000.inc

fire.o:	fire.s gr_fast_clear.s vapor_lock.s \
	delay_a.s
	$(CC65BIN)/ca65 -o fire.o fire.s -l fire.lst

####

FIRE_TINY:	fire_tiny.o
	$(CC65BIN)/ld65 -o fire_TINY fire_tiny.o -C ../linker_scripts/apple2_70.inc

fire_tiny.o:	fire_tiny.s
	$(CC65BIN)/ca65 -o fire_tiny.o fire_tiny.s -l fire_tiny.lst

####

FIRE_FIRMWARE:	fire_firmware.o
	$(CC65BIN)/ld65 -o fire_FIRMWARE fire_firmware.o -C ../linker_scripts/apple2_70.inc

fire_firmware.o:	fire_firmware.s
	$(CC65BIN)/ca65 -o fire_firmware.o fire_firmware.s -l fire_firmware.lst

####

FIRE_EXTREME:	fire_extreme.o
	$(CC65BIN)/ld65 -o fire_EXTREME fire_extreme.o -C ../linker_scripts/apple2_70.inc

fire_extreme.o:	fire_extreme.s
	$(CC65BIN)/ca65 -o fire_extreme.o fire_extreme.s -l fire_extreme.lst

####

FIRE_QKUMBA:	fire_qkumba.o
	$(CC65BIN)/ld65 -o fire_QKUMBA fire_qkumba.o -C ../linker_scripts/apple2_70.inc

fire_qkumba.o:	fire_qkumba.s
	$(CC65BIN)/ca65 -o fire_qkumba.o fire_qkumba.s -l fire_qkumba.lst



####

COOL_EFFECT:	cool_effect.o
	$(CC65BIN)/ld65 -o COOL_EFFECT cool_effect.o -C ../linker_scripts/apple2_70.inc

cool_effect.o:	cool_effect.s
	$(CC65BIN)/ca65 -o cool_effect.o cool_effect.s -l cool_effect.lst


####


RASTER:	raster.o
	$(CC65BIN)/ld65 -o RASTER raster.o -C ../linker_scripts/apple2_70.inc

raster.o:	raster.s
	$(CC65BIN)/ca65 -o raster.o raster.s -l raster.lst

####

HELLO:  hello.bas
	../asoft_basic-utils/tokenize_asoft < hello.bas > HELLO


#####

clean:		
		rm -f *~ *.o *.lst fire FIRE_TINY FIRE_FIRMWARE COOL_EFFECT HELLO FIRE_EXTREME FIRE_QKUMBA RASTER



