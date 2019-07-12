include ../Makefile.inc

DOS33 = ../dos33fs-utils/dos33
MKDOS33FS = ../dos33fs-utils/mkdos33fs
TXT2BAS = ../asoft_basic-utils/tokenize_asoft

all:	ed.dsk

ED:	ED.bas
	$(TXT2BAS) < $< > $@

PROG.EXE : hello.c.o
	$(CC65BIN)/ld65 -o $@ -S 0x4000 -D __EXEHDR__=0 -t apple2 $< $(CC65DIR)/lib/apple2.lib

%.c.s : %.c
	$(CC65BIN)/cc65 -t apple2 -o $@ $<

%.c.o : %.c.s
	$(CC65BIN)/ca65 -o $@ $<

ed.dsk:	ED PROG.EXE
	$(MKDOS33FS) -d ../dos33fs-utils/tests/empty.dsk $@
	$(DOS33) -y $@ SAVE A $<
	$(DOS33) -y $@ HELLO ED
	$(DOS33) -y -a 0x4000 $@ BSAVE PROG.EXE 

clean:	
	rm -f *~ *.BAS *.SHAPE *.lst

