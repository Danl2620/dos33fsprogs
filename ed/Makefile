include ../Makefile.inc

DOS33 = ../dos33fs-utils/dos33
MKDOS33FS = ../dos33fs-utils/mkdos33fs
TXT2BAS = ../asoft_basic-utils/tokenize_asoft

all:	ed.dsk

ED:	ED.bas
	$(TXT2BAS) < $< > $@

PROG.EXE : hello.o
##$(CC65BIN)/ld65 -o $@ $< -C ../linker_scripts/apple2_4000.inc $(CC65DIR)/lib/apple2.lib
	$(CC65BIN)/ld65 -o $@ -S 0x4000 -D __EXEHDR__=0 -t apple2 $< $(CC65DIR)/lib/apple2.lib

%.o : %.s
	$(CC65BIN)/ca65 -o $@ $<

ed.dsk:	ED PROG.EXE
	$(MKDOS33FS) -d ../dos33fs-utils/tests/empty.dsk $@
	$(DOS33) -y $@ SAVE A $<
	$(DOS33) -y $@ HELLO ED
	$(DOS33) -y -a 0x4000 $@ BSAVE PROG.EXE 

clean:	
	rm -f *~ *.BAS *.SHAPE *.lst

