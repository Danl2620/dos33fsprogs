DOS33 = ../dos33fs-utils/dos33
MKDOS33FS = ../dos33fs-utils/mkdos33fs
TXT2BAS = ../asoft_basic-utils/tokenize_asoft

all:	ed.dsk

ED:	ED.bas
	$(TXT2BAS) < $< > $@

ed.dsk:	ED
	$(MKDOS33FS) -d ../dos33fs-utils/tests/empty.dsk $@
	$(DOS33) -y $@ SAVE A $<

clean:	
	rm -f *~ *.BAS *.SHAPE *.lst

