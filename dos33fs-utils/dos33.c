#include <stdio.h>
#include <stdlib.h>   /* exit()    */
#include <string.h>   /* strncpy() */
#include <sys/stat.h> /* struct stat */
#include <fcntl.h>    /* O_RDONLY */
#include <unistd.h>   /* lseek() */
#include <ctype.h>    /* toupper() */

#include "version.h"

#include "dos33.h"

static int debug=1;

static unsigned char sector_buffer[BYTES_PER_SECTOR];

static int ones_lookup[16]={
	/* 0x0 = 0000 */ 0,
	/* 0x1 = 0001 */ 1,
	/* 0x2 = 0010 */ 1,
	/* 0x3 = 0011 */ 2,
	/* 0x4 = 0100 */ 1,
	/* 0x5 = 0101 */ 2,
	/* 0x6 = 0110 */ 2,
	/* 0x7 = 0111 */ 3,
	/* 0x8 = 1000 */ 1,
	/* 0x9 = 1001 */ 2,
	/* 0xA = 1010 */ 2,
	/* 0xB = 1011 */ 3,
	/* 0xC = 1100 */ 2,
	/* 0xd = 1101 */ 3,
	/* 0xe = 1110 */ 3,
	/* 0xf = 1111 */ 4,
};

static unsigned char get_high_byte(int value) {
	return (value>>8)&0xff;
}

static unsigned char get_low_byte(int value) {
	return (value&0xff);
}


#define FILE_NORMAL 0
#define FILE_DELETED 1

static unsigned char dos33_file_type(int value) {

	unsigned char result;

	switch(value&0x7f) {
		case 0x0: result='T'; break;
		case 0x1: result='I'; break;
		case 0x2: result='A'; break;
		case 0x4: result='B'; break;
		case 0x8: result='S'; break;
		case 0x10: result='R'; break;
		case 0x20: result='N'; break;
		case 0x40: result='L'; break;
		default: result='?'; break;
	}
	return result;
}


static unsigned char dos33_char_to_type(char type,int lock) {

	unsigned char result,temp_type;

	temp_type=type;
	/* Covert to upper case */
	if (temp_type>='a') temp_type=temp_type-0x20;

	switch(temp_type) {
		case 'T': result=0x0; break;
		case 'I': result=0x1; break;
		case 'A': result=0x2; break;
		case 'B': result=0x4; break;
		case 'S': result=0x8; break;
		case 'R': result=0x10; break;
		case 'N': result=0x20; break;
		case 'L': result=0x40; break;
		default: result=0x0;
	}
	if (lock) result|=0x80;
	return result;
}

	/* dos33 filenames have top bit set on ascii chars */
	/* and are padded with spaces */
static char *dos33_filename_to_ascii(char *dest,unsigned char *src,int len) {

	int i=0,last_nonspace=0;

	for(i=0;i<len;i++) if (src[i]!=0xA0) last_nonspace=i;

	for(i=0;i<last_nonspace+1;i++) {
		dest[i]=src[i]^0x80; /* toggle top bit */
	}

	dest[i]='\0';
	return dest;
}

    /* Read VTOC into a buffer */
static int dos33_read_vtoc(int fd) {

	int result;

	/* Seek to VTOC */
	lseek(fd,DISK_OFFSET(VTOC_TRACK,VTOC_SECTOR),SEEK_SET);

	/* read in VTOC */
	result=read(fd,&sector_buffer,BYTES_PER_SECTOR);

	if (result<0) fprintf(stderr,"Error on I/O\n");

	return 0;
}

    /* Calculate available freespace */
static int dos33_free_space(int fd) {

	unsigned char bitmap[4];
	int i,sectors_free=0;

	/* Read Vtoc */
	dos33_read_vtoc(fd);

	for(i=0;i<TRACKS_PER_DISK;i++) {
		bitmap[0]=sector_buffer[VTOC_FREE_BITMAPS+(i*4)];
		bitmap[1]=sector_buffer[VTOC_FREE_BITMAPS+(i*4)+1];

		sectors_free+=ones_lookup[bitmap[0]&0xf];
		sectors_free+=ones_lookup[(bitmap[0]>>4)&0xf];
		sectors_free+=ones_lookup[bitmap[1]&0xf];
		sectors_free+=ones_lookup[(bitmap[1]>>4)&0xf];
	}

	return sectors_free*BYTES_PER_SECTOR;
}


	/* Get a T/S value from a Catalog Sector */
static int dos33_get_catalog_ts(int fd) {

	dos33_read_vtoc(fd);

	return TS_TO_INT(sector_buffer[VTOC_CATALOG_T],
			sector_buffer[VTOC_CATALOG_S]);
}

	/* returns the next valid catalog entry */
	/* after the one passed in */
static int dos33_find_next_file(int fd,int catalog_tsf) {

	int catalog_track,catalog_sector,catalog_file;
	int file_track,i;
	int result;

	catalog_file=catalog_tsf>>16;
	catalog_track=(catalog_tsf>>8)&0xff;
	catalog_sector=(catalog_tsf&0xff);

catalog_loop:

	/* Read in Catalog Sector */
	lseek(fd,DISK_OFFSET(catalog_track,catalog_sector),SEEK_SET);
	result=read(fd,sector_buffer,BYTES_PER_SECTOR);

	i=catalog_file;
	while(i<7) {

		file_track=sector_buffer[CATALOG_FILE_LIST+
						(i*CATALOG_ENTRY_SIZE)];
		/* 0xff means file deleted */
		/* 0x0 means empty */
		if ((file_track!=0xff) && (file_track!=0x0)) {
			return ((i<<16)+(catalog_track<<8)+catalog_sector);
		}
		i++;
	}
	catalog_track=sector_buffer[CATALOG_NEXT_T];
	catalog_sector=sector_buffer[CATALOG_NEXT_S];
	if (catalog_sector!=0) {
		catalog_file=0;
		goto catalog_loop;
	}

	if (result<0) fprintf(stderr,"Error on I/O\n");

	return -1;
}

static int dos33_print_file_info(int fd,int catalog_tsf) {

	int catalog_track,catalog_sector,catalog_file,i;
	char temp_string[BUFSIZ];
	int result;

	catalog_file=catalog_tsf>>16;
	catalog_track=(catalog_tsf>>8)&0xff;
	catalog_sector=(catalog_tsf&0xff);

	/* Read in Catalog Sector */
	lseek(fd,DISK_OFFSET(catalog_track,catalog_sector),SEEK_SET);
	result=read(fd,sector_buffer,BYTES_PER_SECTOR);

	if (sector_buffer[CATALOG_FILE_LIST+(catalog_file*CATALOG_ENTRY_SIZE)+FILE_TYPE]>0x7f) {
		printf("*");
	}
	else {
		printf(" ");
	}

	printf("%c",dos33_file_type(sector_buffer[CATALOG_FILE_LIST+(catalog_file*CATALOG_ENTRY_SIZE)+FILE_TYPE]));
	printf(" ");
	printf("%.3i ",sector_buffer[CATALOG_FILE_LIST+(catalog_file*CATALOG_ENTRY_SIZE+FILE_SIZE_L)]+
		(sector_buffer[CATALOG_FILE_LIST+(catalog_file*CATALOG_ENTRY_SIZE+FILE_SIZE_H)]<<8));

	dos33_filename_to_ascii(temp_string,sector_buffer+(CATALOG_FILE_LIST+
			(catalog_file*CATALOG_ENTRY_SIZE+FILE_NAME)),30);

	for(i=0;i<strlen(temp_string);i++) {
		if (temp_string[i]<0x20) {
			printf("^%c",temp_string[i]+0x40);
		}
		else {
			printf("%c",temp_string[i]);
		}
	}

	printf("\n");

	if (result<0) fprintf(stderr,"Error on I/O\n");

	return 0;
}


	/* Checks if "filename" exists */
	/* returns entry/track/sector  */
static int dos33_check_file_exists(int fd,
					char *filename,
					int file_deleted) {

	int catalog_track,catalog_sector;
	int i,file_track;
	char file_name[31];
	int result;

	/* read the VTOC into buffer */
	dos33_read_vtoc(fd);

	/* get the catalog track and sector from the VTOC */
	catalog_track=sector_buffer[VTOC_CATALOG_T];
	catalog_sector=sector_buffer[VTOC_CATALOG_S];

repeat_catalog:

	/* Read in Catalog Sector */
	lseek(fd,DISK_OFFSET(catalog_track,catalog_sector),SEEK_SET);
	result=read(fd,sector_buffer,BYTES_PER_SECTOR);

	/* scan all file entries in catalog sector */
	for(i=0;i<7;i++) {
		file_track=sector_buffer[CATALOG_FILE_LIST+(i*CATALOG_ENTRY_SIZE)];
		/* 0xff means file deleted */
		/* 0x0 means empty */
		if (file_track!=0x0) {

			if (file_track==0xff) {
				dos33_filename_to_ascii(file_name,
					sector_buffer+(CATALOG_FILE_LIST+(i*CATALOG_ENTRY_SIZE+FILE_NAME)),29);

				if (file_deleted) {
					/* return if we found the file */
					if (!strncmp(filename,file_name,29)) {
						return ((i<<16)+(catalog_track<<8)+catalog_sector);
					}
				}
			}
			else {
				dos33_filename_to_ascii(file_name,
					sector_buffer+(CATALOG_FILE_LIST+(i*CATALOG_ENTRY_SIZE+FILE_NAME)),30);
				/* return if we found the file */
				if (!strncmp(filename,file_name,30)) {
					return ((i<<16)+(catalog_track<<8)+catalog_sector);
				}
			}
		}
	}

	/* point to next catalog track/sector */
	catalog_track=sector_buffer[CATALOG_NEXT_T];
	catalog_sector=sector_buffer[CATALOG_NEXT_S];

	if (catalog_sector!=0) goto repeat_catalog;

	if (result<0) fprintf(stderr,"Error on I/O\n");

	return -1;
}

   /* could be replaced by "find leading 1" instruction */
   /* if available                                      */
static int find_first_one(unsigned char byte) {

	int i=0;

	if (byte==0) return -1;

	while((byte& (0x1<<i))==0) {
		i++;
	}
	return i;
}


static int dos33_free_sector(int fd,int track,int sector) {

	unsigned char vtoc[BYTES_PER_SECTOR];
	int result;

	/* Seek to VTOC */
	lseek(fd,DISK_OFFSET(VTOC_TRACK,VTOC_SECTOR),SEEK_SET);
	/* read in VTOC */
	result=read(fd,&vtoc,BYTES_PER_SECTOR);

	/* each bitmap is 32 bits.  With 16-sector tracks only first 16 used */
	/* 1 indicates free, 0 indicates used */
	if (sector<8) {
		vtoc[VTOC_FREE_BITMAPS+(track*4)+1]|=(0x1<<sector);
	}
	else {
		vtoc[VTOC_FREE_BITMAPS+(track*4)]|=(0x1<<(sector-8));
	}

	/* write modified VTOC back out */
	lseek(fd,DISK_OFFSET(VTOC_TRACK,VTOC_SECTOR),SEEK_SET);
	result=write(fd,&vtoc,BYTES_PER_SECTOR);

	if (result<0) fprintf(stderr,"Error on I/O\n");

	return 0;
}

static int dos33_allocate_sector(int fd) {

	int found_track=0,found_sector=0;
	unsigned char bitmap[4];
	int i,start_track,track_dir,byte;
	int result;

	dos33_read_vtoc(fd);

	/* Originally used to keep things near center of disk for speed */
	/* We can use to avoid fragmentation possibly */
	start_track=sector_buffer[VTOC_LAST_ALLOC_T]%TRACKS_PER_DISK;
	track_dir=sector_buffer[VTOC_ALLOC_DIRECT];

	if (track_dir==255) track_dir=-1;

	if ((track_dir!=1) && (track_dir!=-1)) {
		fprintf(stderr,"ERROR!  Invalid track dir %i\n",track_dir);
	}

	if (((start_track>VTOC_TRACK) && (track_dir!=1)) ||
		((start_track<VTOC_TRACK) && (track_dir!=-1))) {
		fprintf(stderr,"Warning! Non-optimal values for track dir t=%i d=%i!\n",
			start_track,track_dir);
	}

	i=start_track;

	do {
		for(byte=1;byte>-1;byte--) {
			bitmap[byte]=sector_buffer[VTOC_FREE_BITMAPS+(i*4)+byte];
			if (bitmap[byte]!=0x00) {
				found_sector=find_first_one(bitmap[byte]);
				found_track=i;
				/* clear bit indicating in use */
				sector_buffer[VTOC_FREE_BITMAPS+(i*4)+byte]&=~(0x1<<found_sector);
				found_sector+=(8*(1-byte));
				goto found_one;
			}
		}

		/* Move to next track, handling overflows */
		i+=track_dir;
		if (i<0) {
			i=VTOC_TRACK;
			track_dir=1;
		}

		if (i>=TRACKS_PER_DISK) {
			i=VTOC_TRACK;
			track_dir=-1;
		}
	} while (i!=start_track);

	fprintf(stderr,"No room left!\n");
	return -1;

found_one:
	/* store new track/direction info */
	sector_buffer[VTOC_LAST_ALLOC_T]=found_track;

	if (found_track>VTOC_TRACK) sector_buffer[VTOC_ALLOC_DIRECT]=1;
	else sector_buffer[VTOC_ALLOC_DIRECT]=-1;

	/* Seek to VTOC */
	lseek(fd,DISK_OFFSET(VTOC_TRACK,VTOC_SECTOR),SEEK_SET);

	/* Write out VTOC */
	result=write(fd,&sector_buffer,BYTES_PER_SECTOR);

	if (result<0) fprintf(stderr,"Error on I/O\n");

	return ((found_track<<8)+found_sector);
}

static int track=0,sector=0;

/* FIXME: currently assume sector is going to be 0 */
static int dos33_force_allocate_sector(int fd) {

	int found_track=0,found_sector=0;
	//unsigned char bitmap[4];
	int i,start_track;//,track_dir,byte;
	int result,so;

	dos33_read_vtoc(fd);

	/* Originally used to keep things near center of disk for speed */
	/* We can use to avoid fragmentation possibly */
//	start_track=sector_buffer[VTOC_LAST_ALLOC_T]%TRACKS_PER_DISK;
//	track_dir=sector_buffer[VTOC_ALLOC_DIRECT];

	start_track=track;

	i=start_track;
	so=!(sector/8);
	found_sector=sector%8;

	// FIXME: check if free
	//bitmap[so]=sector_buffer[VTOC_FREE_BITMAPS+(i*4)+so];
	/* clear bit indicating in use */
	sector_buffer[VTOC_FREE_BITMAPS+(i*4)+so]&=~(0x1<<found_sector);
	found_sector+=(8*(1-so));
	found_track=i;

	if (debug) {
		printf("VMW: want %d/%d, found %d/%d\n",
			track,sector,found_track,found_sector);
	}

	sector++;
	if (sector>15) {
		sector=0;
		track++;
	}

//	fprintf(stderr,"No room for raw-write!\n");
//	return -1;

//found_one:
	/* store new track/direction info */
	//sector_buffer[VTOC_LAST_ALLOC_T]=found_track;
//
//	sector_buffer[VTOC_ALLOC_DIRECT]=1;
//	else sector_buffer[VTOC_ALLOC_DIRECT]=-1;

	/* Seek to VTOC */
	lseek(fd,DISK_OFFSET(VTOC_TRACK,VTOC_SECTOR),SEEK_SET);

	/* Write out VTOC */
	result=write(fd,&sector_buffer,BYTES_PER_SECTOR);

	if (result<0) fprintf(stderr,"Error on I/O\n");

	if (debug) printf("raw: T=%d S=%d\n",found_track,found_sector);

	return ((found_track<<8)+found_sector);
}


#define ERROR_INVALID_FILENAME	1
#define ERROR_FILE_NOT_FOUND	2
#define ERROR_NO_SPACE		3
#define ERROR_IMAGE_NOT_FOUND	4
#define ERROR_CATALOG_FULL	5

#define ADD_RAW		0
#define ADD_BINARY	1

	/* creates file apple_filename on the image from local file filename */
	/* returns ?? */
static int dos33_add_file(int fd, char dos_type,
		int file_type, int address, int length,
		char *filename, char *apple_filename) {

	int free_space,file_size,needed_sectors;
	struct stat file_info;
	int size_in_sectors=0;
	int initial_ts_list=0,ts_list=0,i,data_ts,x,bytes_read=0,old_ts_list;
	int catalog_track,catalog_sector,sectors_used=0;
	int input_fd;
	int result;
	int first_write=1;

	if (apple_filename[0]<64) {
		fprintf(stderr,"Error!  First char of filename "
				"must be ASCII 64 or above!\n");
		return ERROR_INVALID_FILENAME;
	}

	/* Check for comma in filename */
	for(i=0;i<strlen(apple_filename);i++) {
		if (apple_filename[i]==',') {
			fprintf(stderr,"Error!  "
				"Cannot have , in a filename!\n");
			return ERROR_INVALID_FILENAME;
		}
	}

	/* FIXME */
	/* check type */
	/* and sanity check a/b filesize is set properly */

	/* Determine size of file to upload */
	if (stat(filename,&file_info)<0) {
		fprintf(stderr,"Error!  %s not found!\n",filename);
		return ERROR_FILE_NOT_FOUND;
	}

	file_size=(int)file_info.st_size;

	if (debug) printf("Filesize: %d\n",file_size);

	if (file_type==ADD_BINARY) {
		if (debug) printf("Adding 4 bytes for size/offset\n");
		if (length==0) length=file_size;
		file_size+=4;
	}

	/* We need to round up to nearest sector size */
	/* Add an extra sector for the T/S list */
	/* Then add extra sector for a T/S list every 122*256 bytes (~31k) */
	needed_sectors=(file_size/BYTES_PER_SECTOR)+ /* round sectors */
			((file_size%BYTES_PER_SECTOR)!=0)+/* tail if needed */
			1+/* first T/S list */
			(file_size/(122*BYTES_PER_SECTOR)); /* extra t/s lists */

	/* Get free space on device */
	free_space=dos33_free_space(fd);

	/* Check for free space */
	if (needed_sectors*BYTES_PER_SECTOR>free_space) {
		fprintf(stderr,"Error!  Not enough free space "
				"on disk image (need %d have %d)\n",
				needed_sectors*BYTES_PER_SECTOR,free_space);
		return ERROR_NO_SPACE;
	}

	/* plus one because we need a sector for the tail */
	size_in_sectors=(file_size/BYTES_PER_SECTOR)+
		((file_size%BYTES_PER_SECTOR)!=0);
	if (debug) printf("Need to allocate %i data sectors\n",size_in_sectors);
	if (debug) printf("Need to allocate %i total sectors\n",needed_sectors);

	/* Open the local file */
	input_fd=open(filename,O_RDONLY);
	if (input_fd<0) {
		fprintf(stderr,"Error! could not open %s\n",filename);
		return ERROR_IMAGE_NOT_FOUND;
	}

	i=0;
	while (i<size_in_sectors) {

		/* Create new T/S list if necessary */
		if (i%TSL_MAX_NUMBER==0) {
			old_ts_list=ts_list;

			/* allocate a sector for the new list */
			ts_list=dos33_allocate_sector(fd);
			sectors_used++;
			if (ts_list<0) return -1;

			/* clear the t/s sector */
			for(x=0;x<BYTES_PER_SECTOR;x++) {
				sector_buffer[x]=0;
			}
			lseek(fd,DISK_OFFSET((ts_list>>8)&0xff,ts_list&0xff),SEEK_SET);
			result=write(fd,sector_buffer,BYTES_PER_SECTOR);

			if (i==0) {
				initial_ts_list=ts_list;
			}
			else {
				/* we aren't the first t/s list so do special stuff */

				/* load in the old t/s list */
				lseek(fd,
					DISK_OFFSET(get_high_byte(old_ts_list),
					get_low_byte(old_ts_list)),
					SEEK_SET);

				result=read(fd,&sector_buffer,BYTES_PER_SECTOR);

				/* point from old ts list to new one we just made */
				sector_buffer[TSL_NEXT_TRACK]=get_high_byte(ts_list);
				sector_buffer[TSL_NEXT_SECTOR]=get_low_byte(ts_list);

				/* set offset into file */
				sector_buffer[TSL_OFFSET_H]=get_high_byte((i-122)*256);
				sector_buffer[TSL_OFFSET_L]=get_low_byte((i-122)*256);

				/* write out the old t/s list with updated info */
				lseek(fd,
					DISK_OFFSET(get_high_byte(old_ts_list),
					get_low_byte(old_ts_list)),
					SEEK_SET);

				result=write(fd,sector_buffer,BYTES_PER_SECTOR);
			}
		}

		/* allocate a sector */
		data_ts=dos33_allocate_sector(fd);
		sectors_used++;

		if (data_ts<0) return -1;

		/* clear sector */
		for(x=0;x<BYTES_PER_SECTOR;x++) sector_buffer[x]=0;

		/* read from input */
		if ((first_write) && (file_type==ADD_BINARY)) {
			first_write=0;
			sector_buffer[0]=address&0xff;
			sector_buffer[1]=(address>>8)&0xff;
			sector_buffer[2]=(length)&0xff;
			sector_buffer[3]=((length)>>8)&0xff;
			bytes_read=read(input_fd,sector_buffer+4,
					BYTES_PER_SECTOR-4);
			bytes_read+=4;
		}
		else {
			bytes_read=read(input_fd,sector_buffer,
					BYTES_PER_SECTOR);
		}
		first_write=0;

		if (bytes_read<0) fprintf(stderr,"Error reading bytes!\n");

		/* write to disk image */
		lseek(fd,DISK_OFFSET((data_ts>>8)&0xff,data_ts&0xff),SEEK_SET);
		result=write(fd,sector_buffer,BYTES_PER_SECTOR);

		if (debug) printf("Writing %i bytes to %i/%i\n",
				bytes_read,(data_ts>>8)&0xff,data_ts&0xff);

		/* add to T/s table */

		/* read in t/s list */
		lseek(fd,DISK_OFFSET((ts_list>>8)&0xff,ts_list&0xff),SEEK_SET);
		result=read(fd,sector_buffer,BYTES_PER_SECTOR);

		/* point to new data sector */
		sector_buffer[((i%TSL_MAX_NUMBER)*2)+TSL_LIST]=(data_ts>>8)&0xff;
		sector_buffer[((i%TSL_MAX_NUMBER)*2)+TSL_LIST+1]=(data_ts&0xff);

		/* write t/s list back out */
		lseek(fd,DISK_OFFSET((ts_list>>8)&0xff,ts_list&0xff),SEEK_SET);
		result=write(fd,sector_buffer,BYTES_PER_SECTOR);

		i++;
	}

	/* Add new file to Catalog */

	/* read in vtoc */
	dos33_read_vtoc(fd);

	catalog_track=sector_buffer[VTOC_CATALOG_T];
	catalog_sector=sector_buffer[VTOC_CATALOG_S];

continue_parsing_catalog:

          /* Read in Catalog Sector */
	lseek(fd,DISK_OFFSET(catalog_track,catalog_sector),SEEK_SET);
	result=read(fd,sector_buffer,BYTES_PER_SECTOR);
	if (result!=BYTES_PER_SECTOR) {
		fprintf(stderr,"Error reading at $%02X:$%02X\n",
			catalog_track,catalog_sector);
		return ERROR_NO_SPACE;
	}

	/* Find empty directory entry */
	i=0;
	while(i<7) {
		/* for undelete purposes might want to skip 0xff */
		/* (deleted) files first and only use if no room */

		if ((sector_buffer[CATALOG_FILE_LIST+
				(i*CATALOG_ENTRY_SIZE)]==0xff) ||
			(sector_buffer[CATALOG_FILE_LIST+
				(i*CATALOG_ENTRY_SIZE)]==0x00)) {
			goto got_a_dentry;
		}
		i++;
	}

	if ((catalog_track=0x11) && (catalog_sector==1)) {
		/* in theory can only have 105 files */
		/* if full, we have no recourse!     */
		/* can we allocate new catalog sectors */
		/* and point to them?? */
		fprintf(stderr,"Error!  No more room for files!\n");
		return ERROR_CATALOG_FULL;
	}

	catalog_track=sector_buffer[CATALOG_NEXT_T];
	catalog_sector=sector_buffer[CATALOG_NEXT_S];

	goto continue_parsing_catalog;

got_a_dentry:
//	printf("Adding file at entry %i of catalog 0x%x:0x%x\n",
//		i,catalog_track,catalog_sector);

	/* Point entry to initial t/s list */
	sector_buffer[CATALOG_FILE_LIST+(i*CATALOG_ENTRY_SIZE)]=(initial_ts_list>>8)&0xff;
	sector_buffer[CATALOG_FILE_LIST+(i*CATALOG_ENTRY_SIZE)+1]=(initial_ts_list&0xff);
	/* set file type */
	sector_buffer[CATALOG_FILE_LIST+(i*CATALOG_ENTRY_SIZE)+FILE_TYPE]=
		dos33_char_to_type(dos_type,0);

//	printf("Pointing T/S to %x/%x\n",(initial_ts_list>>8)&0xff,initial_ts_list&0xff);

	/* copy over filename */
	for(x=0;x<strlen(apple_filename);x++) {
		sector_buffer[CATALOG_FILE_LIST+(i*CATALOG_ENTRY_SIZE)+FILE_NAME+x]=
		apple_filename[x]^0x80;
	}

	/* pad out the filename with spaces */
	for(x=strlen(apple_filename);x<FILE_NAME_SIZE;x++) {
		sector_buffer[CATALOG_FILE_LIST+(i*CATALOG_ENTRY_SIZE)+FILE_NAME+x]=' '^0x80;
	}

	/* fill in filesize in sectors */
	sector_buffer[CATALOG_FILE_LIST+(i*CATALOG_ENTRY_SIZE)+FILE_SIZE_L]=
		sectors_used&0xff;
	sector_buffer[CATALOG_FILE_LIST+(i*CATALOG_ENTRY_SIZE)+FILE_SIZE_H]=
		(sectors_used>>8)&0xff;

	/* write out catalog sector */
	lseek(fd,DISK_OFFSET(catalog_track,catalog_sector),SEEK_SET);
	result=write(fd,sector_buffer,BYTES_PER_SECTOR);

	if (result<0) fprintf(stderr,"Error on I/O\n");

	return 0;
}



	/* Create raw file on disk starting at track/sector */
	/* returns ?? */
static int dos33_raw_file(int fd, char dos_type,
		int track, int sector,
		char *filename, char *apple_filename) {


	int free_space,file_size,needed_sectors;
	struct stat file_info;
	int size_in_sectors=0;
	int initial_ts_list=0,ts_list=0,i,data_ts,x,bytes_read=0,old_ts_list;
	int catalog_track,catalog_sector,sectors_used=0;
	int input_fd;
	int result;

	if (apple_filename[0]<64) {
		fprintf(stderr,"Error!  First char of filename "
				"must be ASCII 64 or above!\n");
		return ERROR_INVALID_FILENAME;
	}

	/* Check for comma in filename */
	for(i=0;i<strlen(apple_filename);i++) {
		if (apple_filename[i]==',') {
			fprintf(stderr,"Error!  "
				"Cannot have , in a filename!\n");
			return ERROR_INVALID_FILENAME;
		}
	}

	/* FIXME */
	/* check type */
	/* and sanity check a/b filesize is set properly */

	/* Determine size of file to upload */
	if (stat(filename,&file_info)<0) {
		fprintf(stderr,"Error!  %s not found!\n",filename);
		return ERROR_FILE_NOT_FOUND;
	}

	file_size=(int)file_info.st_size;

	if (debug) printf("Filesize: %d\n",file_size);

	/* We need to round up to nearest sector size */
	/* Add an extra sector for the T/S list */
	/* Then add extra sector for a T/S list every 122*256 bytes (~31k) */
	needed_sectors=(file_size/BYTES_PER_SECTOR)+ /* round sectors */
			((file_size%BYTES_PER_SECTOR)!=0)+/* tail if needed */
			1+/* first T/S list */
			(file_size/(122*BYTES_PER_SECTOR)); /* extra t/s lists */

	/* Get free space on device */
	free_space=dos33_free_space(fd);

	/* Check for free space */
	if (needed_sectors*BYTES_PER_SECTOR>free_space) {
		fprintf(stderr,"Error!  Not enough free space "
				"on disk image (need %d have %d)\n",
				needed_sectors*BYTES_PER_SECTOR,free_space);
		return ERROR_NO_SPACE;
	}

	/* plus one because we need a sector for the tail */
	size_in_sectors=(file_size/BYTES_PER_SECTOR)+
		((file_size%BYTES_PER_SECTOR)!=0);
	if (debug) printf("Need to allocate %i data sectors\n",size_in_sectors);
	if (debug) printf("Need to allocate %i total sectors\n",needed_sectors);

	/* Open the local file */
	input_fd=open(filename,O_RDONLY);
	if (input_fd<0) {
		fprintf(stderr,"Error! could not open %s\n",filename);
		return ERROR_IMAGE_NOT_FOUND;
	}

	i=0;
	while (i<size_in_sectors) {

		/* Create new T/S list if necessary */
		if (i%TSL_MAX_NUMBER==0) {
			old_ts_list=ts_list;

			/* allocate a sector for the new list */
			ts_list=dos33_allocate_sector(fd);
			sectors_used++;
			if (ts_list<0) return -1;

			/* clear the t/s sector */
			for(x=0;x<BYTES_PER_SECTOR;x++) {
				sector_buffer[x]=0;
			}
			lseek(fd,DISK_OFFSET((ts_list>>8)&0xff,ts_list&0xff),SEEK_SET);
			result=write(fd,sector_buffer,BYTES_PER_SECTOR);

			if (i==0) {
				initial_ts_list=ts_list;
			}
			else {
				/* we aren't the first t/s list so do special stuff */

				/* load in the old t/s list */
				lseek(fd,
					DISK_OFFSET(get_high_byte(old_ts_list),
					get_low_byte(old_ts_list)),
					SEEK_SET);

				result=read(fd,&sector_buffer,BYTES_PER_SECTOR);

				/* point from old ts list to new one we just made */
				sector_buffer[TSL_NEXT_TRACK]=get_high_byte(ts_list);
				sector_buffer[TSL_NEXT_SECTOR]=get_low_byte(ts_list);

				/* set offset into file */
				sector_buffer[TSL_OFFSET_H]=get_high_byte((i-122)*256);
				sector_buffer[TSL_OFFSET_L]=get_low_byte((i-122)*256);

				/* write out the old t/s list with updated info */
				lseek(fd,
					DISK_OFFSET(get_high_byte(old_ts_list),
					get_low_byte(old_ts_list)),
					SEEK_SET);

				result=write(fd,sector_buffer,BYTES_PER_SECTOR);
			}
		}

		/* force-allocate a sector */
// VMW
		data_ts=dos33_force_allocate_sector(fd);
		sectors_used++;

		if (data_ts<0) return -1;

		/* clear sector */
		for(x=0;x<BYTES_PER_SECTOR;x++) sector_buffer[x]=0;


		bytes_read=read(input_fd,sector_buffer,BYTES_PER_SECTOR);

		if (bytes_read<0) fprintf(stderr,"Error reading bytes!\n");

		/* write to disk image */
		lseek(fd,DISK_OFFSET((data_ts>>8)&0xff,data_ts&0xff),SEEK_SET);
		result=write(fd,sector_buffer,BYTES_PER_SECTOR);

		if (debug) printf("Writing %i bytes to %i/%i\n",
				bytes_read,(data_ts>>8)&0xff,data_ts&0xff);

		/* add to T/s table */

		/* read in t/s list */
		lseek(fd,DISK_OFFSET((ts_list>>8)&0xff,ts_list&0xff),SEEK_SET);
		result=read(fd,sector_buffer,BYTES_PER_SECTOR);

		/* point to new data sector */
		sector_buffer[((i%TSL_MAX_NUMBER)*2)+TSL_LIST]=(data_ts>>8)&0xff;
		sector_buffer[((i%TSL_MAX_NUMBER)*2)+TSL_LIST+1]=(data_ts&0xff);

		/* write t/s list back out */
		lseek(fd,DISK_OFFSET((ts_list>>8)&0xff,ts_list&0xff),SEEK_SET);
		result=write(fd,sector_buffer,BYTES_PER_SECTOR);

		i++;
	}

	/* Add new file to Catalog */

	/* read in vtoc */
	dos33_read_vtoc(fd);

	catalog_track=sector_buffer[VTOC_CATALOG_T];
	catalog_sector=sector_buffer[VTOC_CATALOG_S];

continue_parsing_catalog:

          /* Read in Catalog Sector */
	lseek(fd,DISK_OFFSET(catalog_track,catalog_sector),SEEK_SET);
	result=read(fd,sector_buffer,BYTES_PER_SECTOR);

	/* Find empty directory entry */
	i=0;
	while(i<7) {
		/* for undelete purposes might want to skip 0xff */
		/* (deleted) files first and only use if no room */

		if ((sector_buffer[CATALOG_FILE_LIST+
				(i*CATALOG_ENTRY_SIZE)]==0xff) ||
			(sector_buffer[CATALOG_FILE_LIST+
				(i*CATALOG_ENTRY_SIZE)]==0x00)) {
			goto got_a_dentry;
		}
		i++;
	}

	if ((catalog_track=0x11) && (catalog_sector==1)) {
		/* in theory can only have 105 files */
		/* if full, we have no recourse!     */
		/* can we allocate new catalog sectors */
		/* and point to them?? */
		fprintf(stderr,"Error!  No more room for files!\n");
		return ERROR_CATALOG_FULL;
	}

	catalog_track=sector_buffer[CATALOG_NEXT_T];
	catalog_sector=sector_buffer[CATALOG_NEXT_S];

	goto continue_parsing_catalog;

got_a_dentry:
//	printf("Adding file at entry %i of catalog 0x%x:0x%x\n",
//		i,catalog_track,catalog_sector);

	/* Point entry to initial t/s list */
	sector_buffer[CATALOG_FILE_LIST+(i*CATALOG_ENTRY_SIZE)]=(initial_ts_list>>8)&0xff;
	sector_buffer[CATALOG_FILE_LIST+(i*CATALOG_ENTRY_SIZE)+1]=(initial_ts_list&0xff);
	/* set file type */
	sector_buffer[CATALOG_FILE_LIST+(i*CATALOG_ENTRY_SIZE)+FILE_TYPE]=
		dos33_char_to_type(dos_type,0);

//	printf("Pointing T/S to %x/%x\n",(initial_ts_list>>8)&0xff,initial_ts_list&0xff);

	/* copy over filename */
	for(x=0;x<strlen(apple_filename);x++) {
		sector_buffer[CATALOG_FILE_LIST+(i*CATALOG_ENTRY_SIZE)+FILE_NAME+x]=
		apple_filename[x]^0x80;
	}

	/* pad out the filename with spaces */
	for(x=strlen(apple_filename);x<FILE_NAME_SIZE;x++) {
		sector_buffer[CATALOG_FILE_LIST+(i*CATALOG_ENTRY_SIZE)+FILE_NAME+x]=' '^0x80;
	}

	/* fill in filesize in sectors */
	sector_buffer[CATALOG_FILE_LIST+(i*CATALOG_ENTRY_SIZE)+FILE_SIZE_L]=
		sectors_used&0xff;
	sector_buffer[CATALOG_FILE_LIST+(i*CATALOG_ENTRY_SIZE)+FILE_SIZE_H]=
		(sectors_used>>8)&0xff;

	/* write out catalog sector */
	lseek(fd,DISK_OFFSET(catalog_track,catalog_sector),SEEK_SET);
	result=write(fd,sector_buffer,BYTES_PER_SECTOR);

	if (result<0) fprintf(stderr,"Error on I/O\n");

	return 0;
}



    /* load a file.  fts=entry/track/sector */
static int dos33_load_file(int fd,int fts,char *filename) {

	int output_fd;
	int catalog_file,catalog_track,catalog_sector;
	int file_type,file_size=-1,tsl_track,tsl_sector,data_t,data_s;
	unsigned char data_sector[BYTES_PER_SECTOR];
	int tsl_pointer=0,output_pointer=0;
	int result;

	/* FIXME!  Warn if overwriting file! */
	output_fd=open(filename,O_WRONLY|O_CREAT|O_TRUNC,0666);
	if (output_fd<0) {
		fprintf(stderr,"Error! could not open %s for local save\n",
			filename);
		return -1;
	}

	catalog_file=fts>>16;
	catalog_track=(fts>>8)&0xff;
	catalog_sector=(fts&0xff);


	/* Read in Catalog Sector */
	lseek(fd,DISK_OFFSET(catalog_track,catalog_sector),SEEK_SET);
	result=read(fd,sector_buffer,BYTES_PER_SECTOR);

	tsl_track=sector_buffer[CATALOG_FILE_LIST+
			(catalog_file*CATALOG_ENTRY_SIZE)+FILE_TS_LIST_T];
	tsl_sector=sector_buffer[CATALOG_FILE_LIST+
			(catalog_file*CATALOG_ENTRY_SIZE)+FILE_TS_LIST_S];
	file_type=dos33_file_type(sector_buffer[CATALOG_FILE_LIST+
			(catalog_file*CATALOG_ENTRY_SIZE)+FILE_TYPE]);

//	printf("file_type: %c\n",file_type);

keep_saving:
	/* Read in TSL Sector */
	lseek(fd,DISK_OFFSET(tsl_track,tsl_sector),SEEK_SET);
	result=read(fd,sector_buffer,BYTES_PER_SECTOR);
	tsl_pointer=0;

	/* check each track/sector pair in the list */
	while(tsl_pointer<TSL_MAX_NUMBER) {

		/* get the t/s value */
		data_t=sector_buffer[TSL_LIST+(tsl_pointer*TSL_ENTRY_SIZE)];
		data_s=sector_buffer[TSL_LIST+(tsl_pointer*TSL_ENTRY_SIZE)+1];

		if ((data_s==0) && (data_t==0)) {
			/* empty */
		}
		else {
			lseek(fd,DISK_OFFSET(data_t,data_s),SEEK_SET);
			result=read(fd,&data_sector,BYTES_PER_SECTOR);

			/* some file formats have the size in the first sector */
			/* so cheat and get real file size from file itself    */
			if (output_pointer==0) {
				switch(file_type) {
				case 'A':
				case 'I':
					file_size=data_sector[0]+(data_sector[1]<<8)+2;
					break;
				case 'B':
					file_size=data_sector[2]+(data_sector[3]<<8)+4;
					break;
				default:
					file_size=-1;
				}
			}

			/* write the block read in out to the output file */
			lseek(output_fd,output_pointer*BYTES_PER_SECTOR,SEEK_SET);
			result=write(output_fd,&data_sector,BYTES_PER_SECTOR);
		}
		output_pointer++;
		tsl_pointer++;
	}

	/* finished with TSL sector, see if we have another */
	tsl_track=sector_buffer[TSL_NEXT_TRACK];
	tsl_sector=sector_buffer[TSL_NEXT_SECTOR];

//	printf("Next track/sector=%d/%d op=%d\n",tsl_track,tsl_sector,
//		output_pointer*BYTES_PER_SECTOR);

	if ((tsl_track==0) && (tsl_sector==0)) {
	}
	else goto keep_saving;

	/* Correct the file size */
	if (file_size>=0) {
//		printf("Truncating file size to %d\n",file_size);
		result=ftruncate(output_fd,file_size);
	}

	if (result<0) fprintf(stderr,"Error on I/O\n");

	return 0;

}

    /* lock a file.  fts=entry/track/sector */
static int dos33_lock_file(int fd,int fts,int lock) {
 
    int catalog_file,catalog_track,catalog_sector;
    int file_type,result;
   
    catalog_file=fts>>16;
    catalog_track=(fts>>8)&0xff;
    catalog_sector=(fts&0xff);


       /* Read in Catalog Sector */
    lseek(fd,DISK_OFFSET(catalog_track,catalog_sector),SEEK_SET);
    result=read(fd,sector_buffer,BYTES_PER_SECTOR);

    file_type=sector_buffer[CATALOG_FILE_LIST+
			    (catalog_file*CATALOG_ENTRY_SIZE)
                                            +FILE_TYPE];
   
    if (lock) file_type|=0x80;
    else file_type&=0x7f;
   
    sector_buffer[CATALOG_FILE_LIST+
		  (catalog_file*CATALOG_ENTRY_SIZE)
                                            +FILE_TYPE]=file_type;

      /* write back modified catalog sector */
    lseek(fd,DISK_OFFSET(catalog_track,catalog_sector),SEEK_SET);
    result=write(fd,sector_buffer,BYTES_PER_SECTOR);

    if (result<0) fprintf(stderr,"Error on I/O\n");
   
    return 0;
   
}

    /* rename a file.  fts=entry/track/sector */
    /* FIXME: can we rename a locked file?    */
    /* FIXME: validate the new filename is valid */
static int dos33_rename_file(int fd,int fts,char *new_name) {
 
    int catalog_file,catalog_track,catalog_sector;
    int x,result;
   
    catalog_file=fts>>16;
    catalog_track=(fts>>8)&0xff;
    catalog_sector=(fts&0xff);

       /* Read in Catalog Sector */
    lseek(fd,DISK_OFFSET(catalog_track,catalog_sector),SEEK_SET);
    result=read(fd,sector_buffer,BYTES_PER_SECTOR);

          /* copy over filename */
    for(x=0;x<strlen(new_name);x++) 
       sector_buffer[CATALOG_FILE_LIST+(catalog_file*CATALOG_ENTRY_SIZE)+
		     FILE_NAME+x]=new_name[x]^0x80;
   
       /* pad out the filename with spaces */
    for(x=strlen(new_name);x<FILE_NAME_SIZE;x++)
        sector_buffer[CATALOG_FILE_LIST+(catalog_file*CATALOG_ENTRY_SIZE)+
		      FILE_NAME+x]=' '^0x80;
      
      /* write back modified catalog sector */
    lseek(fd,DISK_OFFSET(catalog_track,catalog_sector),SEEK_SET);
    result=write(fd,sector_buffer,BYTES_PER_SECTOR);

    if (result<0) fprintf(stderr,"Error on I/O\n");
   
    return 0;
   
}

    /* undelete a file.  fts=entry/track/sector */
    /* FIXME: validate the new filename is valid */
static int dos33_undelete_file(int fd,int fts,char *new_name) {
 
    int catalog_file,catalog_track,catalog_sector;
    char replacement_char;
    int result;

    catalog_file=fts>>16;
    catalog_track=(fts>>8)&0xff;
    catalog_sector=(fts&0xff);

       /* Read in Catalog Sector */
    lseek(fd,DISK_OFFSET(catalog_track,catalog_sector),SEEK_SET);
    result=read(fd,sector_buffer,BYTES_PER_SECTOR);

       /* get the stored track value, and put it back  */
       /* FIXME: should walk file to see if T/s valild */
       /* by setting the track value to FF which indicates deleted file */
    sector_buffer[CATALOG_FILE_LIST+(catalog_file*CATALOG_ENTRY_SIZE)]=
        sector_buffer[CATALOG_FILE_LIST+(catalog_file*CATALOG_ENTRY_SIZE)+
		      FILE_NAME+29];

       /* restore file name if possible */

    replacement_char=0xa0;
    if (strlen(new_name)>29) replacement_char=new_name[29]^0x80;
   
    sector_buffer[CATALOG_FILE_LIST+(catalog_file*CATALOG_ENTRY_SIZE)+
		      FILE_NAME+29]=replacement_char;
      
      /* write back modified catalog sector */
    lseek(fd,DISK_OFFSET(catalog_track,catalog_sector),SEEK_SET);
    result=write(fd,sector_buffer,BYTES_PER_SECTOR);

    if (result<0) fprintf(stderr,"Error on I/O\n");
   
    return 0;
   
}


static int dos33_delete_file(int fd,int fsl) {
   
    int i;
    int catalog_track,catalog_sector,catalog_entry;
    int ts_track,ts_sector;
    char file_type;
    int result;

       /* unpack file/track/sector info */
    catalog_entry=fsl>>16;
    catalog_track=(fsl>>8)&0xff;
    catalog_sector=(fsl&0xff);

       /* Load in the catalog table for the file */
    lseek(fd,DISK_OFFSET(catalog_track,catalog_sector),SEEK_SET);
    result=read(fd,sector_buffer,BYTES_PER_SECTOR);

    file_type=sector_buffer[CATALOG_FILE_LIST+
			    (catalog_entry*CATALOG_ENTRY_SIZE)
                                            +FILE_TYPE];
    if (file_type&0x80) {
       fprintf(stderr,"File is locked!  Unlock before deleting!\n");
       exit(1);
    }
   
   
       /* get pointer to t/s list */
    ts_track=sector_buffer[CATALOG_FILE_LIST+catalog_entry*CATALOG_ENTRY_SIZE+
		    FILE_TS_LIST_T];
    ts_sector=sector_buffer[CATALOG_FILE_LIST+catalog_entry*CATALOG_ENTRY_SIZE+
		     FILE_TS_LIST_S];
   
keep_deleting:
   

   
       /* load in the t/s list info */
    lseek(fd,DISK_OFFSET(ts_track,ts_sector),SEEK_SET);
    result=read(fd,sector_buffer,BYTES_PER_SECTOR);
      
      /* Free each sector listed by t/s list */
    for(i=0;i<TSL_MAX_NUMBER;i++) {
          /* If t/s = 0/0 then no need to clear */
       if ((sector_buffer[TSL_LIST+2*i]==0) && (sector_buffer[TSL_LIST+2*i+1]==0)) {
       }
       else {
          dos33_free_sector(fd,sector_buffer[TSL_LIST+2*i],sector_buffer[TSL_LIST+2*i+1]);
       }
    }

          /* free the t/s list */
    dos33_free_sector(fd,ts_track,ts_sector);
   
       /* Point to next t/s list */
    ts_track=sector_buffer[TSL_NEXT_TRACK];
    ts_sector=sector_buffer[TSL_NEXT_SECTOR];
   
       /* If more tsl lists, keep looping */
    if ((ts_track==0x0) && (ts_sector==0x0)) {
    }
    else {
       goto keep_deleting;        
    }
   
       /* Erase file from catalog entry */
   
       /* First reload proper catalog sector */
   lseek(fd,DISK_OFFSET(catalog_track,catalog_sector),SEEK_SET);
   result=read(fd,sector_buffer,BYTES_PER_SECTOR);
      
      /* save track as last char of name, for undelete purposes */
   sector_buffer[CATALOG_FILE_LIST+(catalog_entry*CATALOG_ENTRY_SIZE)+
	  (FILE_NAME+FILE_NAME_SIZE-1)]=
     sector_buffer[CATALOG_FILE_LIST+(catalog_entry*CATALOG_ENTRY_SIZE)];
   
       /* Actually delete the file */
       /* by setting the track value to FF which indicates deleted file */
   sector_buffer[CATALOG_FILE_LIST+(catalog_entry*CATALOG_ENTRY_SIZE)]=0xff;
   
       /* re seek to catalog position and write out changes */
   lseek(fd,DISK_OFFSET(catalog_track,catalog_sector),SEEK_SET);
   result=write(fd,sector_buffer,BYTES_PER_SECTOR);

    if (result<0) fprintf(stderr,"Error on I/O\n");
   
   return 0;
}

static int dump_sector(void) {

	int i,j;

	for(i=0;i<16;i++) {
		printf("$%02X : ",i*16);
		for(j=0;j<16;j++) {
			printf("%02X ",sector_buffer[i*16+j]);
		}
		printf("\n");
	}
	return 0;
}


static int dos33_dump(int fd) {

	int num_tracks,catalog_t,catalog_s,file,ts_t,ts_s,ts_total;
	int track,sector;
	int i,j;
	int deleted=0;
	char temp_string[BUFSIZ];
	unsigned char tslist[BYTES_PER_SECTOR];
	int result;

	/* Read Track 1 Sector 9 */
	lseek(fd,DISK_OFFSET(1,9),SEEK_SET);
	result=read(fd,sector_buffer,BYTES_PER_SECTOR);

	printf("Finding name of startup file, Track 1 Sector 9 offset $75\n");
	dump_sector();

	printf("Startup Filename: ");
	for(i=0;i<30;i++) {
		printf("%c",sector_buffer[0x75+i]&0x7f);
	}
	printf("\n");

	dos33_read_vtoc(fd);

	printf("\nVTOC Sector:\n");
	dump_sector();

	printf("\n\n");
	printf("VTOC INFORMATION:\n");
	catalog_t=sector_buffer[VTOC_CATALOG_T];
	catalog_s=sector_buffer[VTOC_CATALOG_S];
	printf("\tFirst Catalog = %02X/%02X\n",catalog_t,catalog_s);
	printf("\tDOS RELEASE = 3.%i\n",sector_buffer[VTOC_DOS_RELEASE]);
	printf("\tDISK VOLUME = %i\n",sector_buffer[VTOC_DISK_VOLUME]);
	ts_total=sector_buffer[VTOC_MAX_TS_PAIRS];
	printf("\tT/S pairs that will fit in T/S List = %i\n",ts_total);

	printf("\tLast track where sectors were allocated = $%02X\n",
		sector_buffer[VTOC_LAST_ALLOC_T]);
	printf("\tDirection of track allocation = %i\n",
		sector_buffer[VTOC_ALLOC_DIRECT]);

	num_tracks=sector_buffer[VTOC_NUM_TRACKS];
	printf("\tNumber of tracks per disk = %i\n",num_tracks);
	printf("\tNumber of sectors per track = %i\n",
		sector_buffer[VTOC_S_PER_TRACK]);
	printf("\tNumber of bytes per sector = %i\n",
		(sector_buffer[VTOC_BYTES_PER_SH]<<8)+
		sector_buffer[VTOC_BYTES_PER_SL]);

	printf("\nFree sector bitmap:\n");
	printf("\tTrack  FEDCBA98 76543210\n");
	for(i=0;i<num_tracks;i++) {
		printf("\t $%02X:  ",i);
		for(j=0;j<8;j++) {
			if ((sector_buffer[VTOC_FREE_BITMAPS+(i*4)]<<j)&0x80) {
				printf(".");
			}
			else {
				printf("U");
			}
		}
		printf(" ");
		for(j=0;j<8;j++) {
			if ((sector_buffer[VTOC_FREE_BITMAPS+(i*4)+1]<<j)&0x80) {
				printf(".");
			}
			else {
				printf("U");
			}
		}
		printf("\n");
	}

repeat_catalog:

	printf("\nCatalog Sector $%02X/$%02x\n",catalog_t,catalog_s);
	lseek(fd,DISK_OFFSET(catalog_t,catalog_s),SEEK_SET);
	result=read(fd,sector_buffer,BYTES_PER_SECTOR);

	dump_sector();

	for(file=0;file<7;file++) {
		printf("\n\n");

		ts_t=sector_buffer[(CATALOG_FILE_LIST+(file*CATALOG_ENTRY_SIZE+FILE_TS_LIST_T))];
		ts_s=sector_buffer[(CATALOG_FILE_LIST+(file*CATALOG_ENTRY_SIZE+FILE_TS_LIST_S))];

		printf("%i+$%02X/$%02X - ",file,catalog_t,catalog_s);
		deleted=0;

		if (ts_t==0xff) {
			printf("**DELETED** ");
			deleted=1;
			ts_t=sector_buffer[(CATALOG_FILE_LIST+(file*CATALOG_ENTRY_SIZE+FILE_NAME+0x1e))];
		}

		if (ts_t==0x00) {
			printf("UNUSED!\n");
			goto continue_dump;
		}

		dos33_filename_to_ascii(temp_string,
			sector_buffer+(CATALOG_FILE_LIST+(file*CATALOG_ENTRY_SIZE+FILE_NAME)),30);

		for(i=0;i<strlen(temp_string);i++) {
			if (temp_string[i]<0x20) {
				printf("^%c",temp_string[i]+0x40);
			}
			else {
				printf("%c",temp_string[i]);
			}
		}
		printf("\n");
		printf("\tLocked = %s\n",
			sector_buffer[CATALOG_FILE_LIST+(file*CATALOG_ENTRY_SIZE)+FILE_TYPE]>0x7f?
			"YES":"NO");
		printf("\tType = %c\n",
			dos33_file_type(sector_buffer[CATALOG_FILE_LIST+(file*CATALOG_ENTRY_SIZE)+FILE_TYPE]));
		printf("\tSize in sectors = %i\n",
			sector_buffer[CATALOG_FILE_LIST+(file*CATALOG_ENTRY_SIZE+FILE_SIZE_L)]+
			(sector_buffer[CATALOG_FILE_LIST+(file*CATALOG_ENTRY_SIZE+FILE_SIZE_H)]<<8));

repeat_tsl:
		printf("\tT/S List $%02X/$%02X:\n",ts_t,ts_s);
		if (deleted) goto continue_dump;
		lseek(fd,DISK_OFFSET(ts_t,ts_s),SEEK_SET);
		result=read(fd,&tslist,BYTES_PER_SECTOR);

		for(i=0;i<ts_total;i++) {
			track=tslist[TSL_LIST+(i*TSL_ENTRY_SIZE)];
			sector=tslist[TSL_LIST+(i*TSL_ENTRY_SIZE)+1];
			if ((track==0) && (sector==0)) printf(".");
			else printf("\n\t\t%02X/%02X",track,sector);
		}
		ts_t=tslist[TSL_NEXT_TRACK];
		ts_s=tslist[TSL_NEXT_SECTOR];

		if (!((ts_s==0) && (ts_t==0))) goto repeat_tsl;
continue_dump:;
	}

	catalog_t=sector_buffer[CATALOG_NEXT_T];
	catalog_s=sector_buffer[CATALOG_NEXT_S];

	if (catalog_s!=0) {
		file=0;
		goto repeat_catalog;
	}

	printf("\n");

	if (result<0) fprintf(stderr,"Error on I/O\n");

	return 0;
}

static int dos33_showfree(int fd) {

	int num_tracks,catalog_t,catalog_s,file,ts_t,ts_s,ts_total;
	int track,sector;
	int i,j;
	int deleted=0;
	char temp_string[BUFSIZ];
	unsigned char tslist[BYTES_PER_SECTOR];
	int result;

	int sectors_per_track;
	int catalog_used;
	int next_letter='a';
	struct file_key_type {
		int ch;
		char *filename;
	} file_key[100];
	int num_files=0;


	unsigned char usage[35][16];

	for(i=0;i<35;i++) for(j=0;j<16;j++) usage[i][j]=0;

	/* Read Track 1 Sector 9 */
	lseek(fd,DISK_OFFSET(1,9),SEEK_SET);
	result=read(fd,sector_buffer,BYTES_PER_SECTOR);

	printf("Finding name of startup file, Track 1 Sector 9 offset $75\n");
	printf("Startup Filename: ");
	for(i=0;i<30;i++) {
		printf("%c",sector_buffer[0x75+i]&0x7f);
	}
	printf("\n");

	dos33_read_vtoc(fd);

	printf("\n");
	printf("VTOC INFORMATION:\n");
	catalog_t=sector_buffer[VTOC_CATALOG_T];
	catalog_s=sector_buffer[VTOC_CATALOG_S];
	printf("\tFirst Catalog = %02X/%02X\n",catalog_t,catalog_s);
	printf("\tDOS RELEASE = 3.%i\n",sector_buffer[VTOC_DOS_RELEASE]);
	printf("\tDISK VOLUME = %i\n",sector_buffer[VTOC_DISK_VOLUME]);
	ts_total=sector_buffer[VTOC_MAX_TS_PAIRS];
	printf("\tT/S pairs that will fit in T/S List = %i\n",ts_total);

	printf("\tLast track where sectors were allocated = $%02X\n",
		sector_buffer[VTOC_LAST_ALLOC_T]);
	printf("\tDirection of track allocation = %i\n",
		sector_buffer[VTOC_ALLOC_DIRECT]);

	num_tracks=sector_buffer[VTOC_NUM_TRACKS];
	printf("\tNumber of tracks per disk = %i\n",num_tracks);
	printf("\tNumber of sectors per track = %i\n",
		sector_buffer[VTOC_S_PER_TRACK]);
	sectors_per_track=sector_buffer[VTOC_S_PER_TRACK];

	printf("\tNumber of bytes per sector = %i\n",
		(sector_buffer[VTOC_BYTES_PER_SH]<<8)+
		sector_buffer[VTOC_BYTES_PER_SL]);

	printf("\nFree sector bitmap:\n\n");
	printf("\t                1111111111111111222\n");
	printf("\t0123456789ABCDEF0123456789ABCDEF012\n");

	for(j=0;j<sectors_per_track;j++) {
		printf("$%01X:\t",j);
		for(i=0;i<num_tracks;i++) {

			if (j<8) {
				if ((sector_buffer[VTOC_FREE_BITMAPS+(i*4)]<<j)&0x80) {
					printf(".");
				}
				else {
					printf("U");
				}
			}
			else {
				if ((sector_buffer[VTOC_FREE_BITMAPS+(i*4)+1]<<j)&0x80) {
					printf(".");
				}
				else {
					printf("U");
				}
			}
		}
		printf("\n");

	}

	printf("Key: U=used, .=free\n\n");

	/* Reserve DOS */
	for(i=0;i<3;i++) for(j=0;j<16;j++) usage[i][j]='$';

	/* Reserve CATALOG (not all used?) */
	i=0x11;
	for(j=0;j<16;j++) usage[i][j]='#';


repeat_catalog:

	catalog_used=0;

//	printf("\nCatalog Sector $%02X/$%02x\n",catalog_t,catalog_s);
	lseek(fd,DISK_OFFSET(catalog_t,catalog_s),SEEK_SET);
	result=read(fd,sector_buffer,BYTES_PER_SECTOR);


//	dump_sector();

	for(file=0;file<7;file++) {
//		printf("\n\n");

		ts_t=sector_buffer[(CATALOG_FILE_LIST+(file*CATALOG_ENTRY_SIZE+FILE_TS_LIST_T))];
		ts_s=sector_buffer[(CATALOG_FILE_LIST+(file*CATALOG_ENTRY_SIZE+FILE_TS_LIST_S))];

//		printf("%i+$%02X/$%02X - ",file,catalog_t,catalog_s);
		deleted=0;

		if (ts_t==0xff) {
			printf("**DELETED** ");
			deleted=1;
			ts_t=sector_buffer[(CATALOG_FILE_LIST+(file*CATALOG_ENTRY_SIZE+FILE_NAME+0x1e))];
		}

		if (ts_t==0x00) {
//			printf("UNUSED!\n");
			goto continue_dump;
		}

		dos33_filename_to_ascii(temp_string,
			sector_buffer+(CATALOG_FILE_LIST+(file*CATALOG_ENTRY_SIZE+FILE_NAME)),
			30);

		for(i=0;i<strlen(temp_string);i++) {
			if (temp_string[i]<0x20) {
				printf("^%c",temp_string[i]+0x40);
			}
			else {
				printf("%c",temp_string[i]);
			}
		}
		printf("\n");
//		printf("\tLocked = %s\n",
//			sector_buffer[CATALOG_FILE_LIST+(file*CATALOG_ENTRY_SIZE)+FILE_TYPE]>0x7f?
//			"YES":"NO");
//		printf("\tType = %c\n",
//			dos33_file_type(sector_buffer[CATALOG_FILE_LIST+(file*CATALOG_ENTRY_SIZE)+FILE_TYPE]));
//		printf("\tSize in sectors = %i\n",
//			sector_buffer[CATALOG_FILE_LIST+(file*CATALOG_ENTRY_SIZE+FILE_SIZE_L)]+
//			(sector_buffer[CATALOG_FILE_LIST+(file*CATALOG_ENTRY_SIZE+FILE_SIZE_H)]<<8));

		if (!deleted) {
			catalog_used++;
			usage[catalog_t][catalog_s]='@';
		}

repeat_tsl:
//		printf("\tT/S List $%02X/$%02X:\n",ts_t,ts_s);
		if (deleted) goto continue_dump;

		usage[ts_t][ts_s]=next_letter;
		file_key[num_files].ch=next_letter;
		file_key[num_files].filename=strdup(temp_string);

		num_files++;


		lseek(fd,DISK_OFFSET(ts_t,ts_s),SEEK_SET);
		result=read(fd,&tslist,BYTES_PER_SECTOR);

		for(i=0;i<ts_total;i++) {
			track=tslist[TSL_LIST+(i*TSL_ENTRY_SIZE)];
			sector=tslist[TSL_LIST+(i*TSL_ENTRY_SIZE)+1];
			if ((track==0) && (sector==0)) {
				//printf(".");
			}
			else {
//				printf("\n\t\t%02X/%02X",track,sector);
				usage[track][sector]=toupper(next_letter);
			}
		}
		ts_t=tslist[TSL_NEXT_TRACK];
		ts_s=tslist[TSL_NEXT_SECTOR];

		if (!((ts_s==0) && (ts_t==0))) goto repeat_tsl;
continue_dump:;

		next_letter++;
	}

	catalog_t=sector_buffer[CATALOG_NEXT_T];
	catalog_s=sector_buffer[CATALOG_NEXT_S];

	if (catalog_s!=0) {
		file=0;
		goto repeat_catalog;
	}

	printf("\n");

	if (result<0) fprintf(stderr,"Error on I/O\n");

	printf("\nDetailed sector bitmap:\n\n");
	printf("\t                1111111111111111222\n");
	printf("\t0123456789ABCDEF0123456789ABCDEF012\n");

	for(j=0;j<sectors_per_track;j++) {
		printf("$%01X:\t",j);
		for(i=0;i<num_tracks;i++) {
			if (usage[i][j]==0) printf(".");
			else printf("%c",usage[i][j]);
		}
		printf("\n");

	}

	printf("Key: $=DOS, @=catalog used, #=catalog reserved, .=free\n\n");
	for(i=0;i<num_files;i++) {
		printf("\t%c %s\n",file_key[i].ch,file_key[i].filename);
	}

	return 0;
}

/* ??? */
static int dos33_rename_hello(int fd, char *new_name) {

	char buffer[BYTES_PER_SECTOR];
	int i;

	lseek(fd,DISK_OFFSET(1,9),SEEK_SET);
	read(fd,buffer,BYTES_PER_SECTOR);

	for(i=0;i<30;i++) {
		if (i<strlen(new_name)) {
			buffer[0x75+i]=new_name[i]|0x80;
		}
		else {
			buffer[0x75+i]=' '|0x80;
		}
	}

	lseek(fd,DISK_OFFSET(1,9),SEEK_SET);
	write(fd,buffer,BYTES_PER_SECTOR);

	return 0;
}

static void display_help(char *name, int version_only) {
	printf("\ndos33 version %s\n",VERSION);
	printf("by Vince Weaver <vince@deater.net>\n");
	printf("\n");

	if (version_only) return;

	printf("Usage: %s [-h] [-y] disk_image COMMAND [options]\n",name);
	printf("\t-h : this help message\n");
	printf("\t-y : always answer yes for anying warning questions\n");
	printf("\n");
	printf("  Where disk_image is a valid dos3.3 disk image\n"
		"  and COMMAND is one of the following:\n");
	printf("\tCATALOG\n");
	printf("\tLOAD     apple_file <local_file>\n");
	printf("\tSAVE     type local_file <apple_file>\n");
	printf("\tBSAVE    [-a addr] [-l len] local_file <apple_file>\n");
	printf("\tDELETE   apple_file\n");
	printf("\tLOCK     apple_file\n");
	printf("\tUNLOCK   apple_file\n");
	printf("\tRENAME   apple_file_old apple_file_new\n");
	printf("\tUNDELETE apple_file\n");
	printf("\tDUMP\n");
	printf("\tHELLO    apple_file\n");
#if 0
	printf("\tINIT\n");
	printf("\tCOPY\n");
#endif
	printf("\n");
	return;
}

#define COMMAND_LOAD	 0
#define COMMAND_SAVE	 1
#define COMMAND_CATALOG	 2
#define COMMAND_DELETE	 3
#define COMMAND_UNDELETE 4
#define COMMAND_LOCK	 5
#define COMMAND_UNLOCK	 6
#define COMMAND_INIT	 7
#define COMMAND_RENAME	 8
#define COMMAND_COPY	 9
#define COMMAND_DUMP	10
#define COMMAND_HELLO	11
#define COMMAND_BSAVE	12
#define COMMAND_BLOAD	13
#define COMMAND_SHOWFREE	14
#define COMMAND_RAW_WRITE	15

#define MAX_COMMAND	15
#define COMMAND_UNKNOWN	255

static struct command_type {
	int type;
	char name[32];
} commands[MAX_COMMAND] = {
	{COMMAND_LOAD,"LOAD"},
	{COMMAND_SAVE,"SAVE"},
	{COMMAND_CATALOG,"CATALOG"},
	{COMMAND_DELETE,"DELETE"},
	{COMMAND_UNDELETE,"UNDELETE"},
	{COMMAND_LOCK,"LOCK"},
	{COMMAND_UNLOCK,"UNLOCK"},
	{COMMAND_INIT,"INIT"},
	{COMMAND_RENAME,"RENAME"},
	{COMMAND_COPY,"COPY"},
	{COMMAND_DUMP,"DUMP"},
	{COMMAND_HELLO,"HELLO"},
	{COMMAND_BSAVE,"BSAVE"},
	{COMMAND_SHOWFREE,"SHOWFREE"},
	{COMMAND_RAW_WRITE,"RAWWRITE"},
};

static int lookup_command(char *name) {

	int which=COMMAND_UNKNOWN,i;

	for(i=0;i<MAX_COMMAND;i++) {
		if(!strncmp(name,commands[i].name,strlen(commands[i].name))) {
			which=commands[i].type;
			break;
		}
	}
	return which;

}

static int truncate_filename(char *out, char *in) {

	int truncated=0;

	/* Truncate filename if too long */
	if (strlen(in)>30) {
		fprintf(stderr,"Warning!  Truncating %s to 30 chars\n",in);
		truncated=1;
	}
	strncpy(out,in,30);
	out[30]='\0';

	return truncated;
}

int main(int argc, char **argv) {

	char image[BUFSIZ];
	unsigned char type='b';
	int dos_fd=0,i;

	int command,catalog_entry;
	char temp_string[BUFSIZ];
	char apple_filename[31],new_filename[31];
	char local_filename[BUFSIZ];
	char *result_string;
	int always_yes=0;
	char *temp,*endptr;
	int c;
	int address=0, length=0;


	/* Check command line arguments */
	while ((c = getopt (argc, argv,"a:l:t:s:hvy"))!=-1) {
		switch (c) {

		case 'a':
			address=strtol(optarg,&endptr,0);
			if (debug) printf("Address=%d\n",address);
			break;
		case 'l':
			length=strtol(optarg,&endptr,0);
			if (debug) printf("Length=%d\n",address);
			break;
		case 't':
			track=strtol(optarg,&endptr,0);
			if (debug) printf("Track=%d\n",address);
			break;
		case 's':
			sector=strtol(optarg,&endptr,0);
			if (debug) printf("Sector=%d\n",address);
			break;
		case 'v':
			display_help(argv[0],1);
			return 0;
		case 'h': display_help(argv[0],0);
			return 0;
		case 'y':
			always_yes=1;
			break;
		}
	}

	if (optind==argc) {
		fprintf(stderr,"ERROR!  Must specify disk image!\n\n");
		return -1;
	}

	/* get argument 1, which is image name */
	strncpy(image,argv[optind],BUFSIZ-1);
	dos_fd=open(image,O_RDWR);
	if (dos_fd<0) {
		fprintf(stderr,"Error opening disk_image: %s\n",image);
		return -1;
	}

	/* Move to next argument */
	optind++;

	if (optind==argc) {
		fprintf(stderr,"ERROR!  Must specify command!\n\n");
		return -2;
	}

	/* Grab command */
	strncpy(temp_string,argv[optind],BUFSIZ-1);

	/* Make command be uppercase */
	for(i=0;i<strlen(temp_string);i++) {
		temp_string[i]=toupper(temp_string[i]);
	}

	/* Move to next argument */
	optind++;

	command=lookup_command(temp_string);

	switch(command) {

	case COMMAND_UNKNOWN:
		fprintf(stderr,"ERROR!  Unknown command %s\n",temp_string);
		fprintf(stderr,"\tTry \"%s -h\" for help.\n\n",argv[0]);
		goto exit_and_close;
		break;

	/* Load a file from disk image to local machine */
	case COMMAND_LOAD:

		/* check and make sure we have apple_filename */
		if (argc==optind) {
			fprintf(stderr,"Error! Need apple file_name\n");
			fprintf(stderr,"%s %s LOAD apple_filename\n",
				argv[0],image);
			goto exit_and_close;
		}

		truncate_filename(apple_filename,argv[optind]);

		if (debug) printf("\tApple filename: %s\n",apple_filename);

		/* get output filename */
		optind++;
		if (argc>optind) {
			if (debug) printf("Using %s for filename\n",
						local_filename);
			strncpy(local_filename,argv[optind],BUFSIZ-1);
		}
		else {
			if (debug) printf("Using %s for filename\n",
						apple_filename);
			strncpy(local_filename,apple_filename,31);
		}

		if (debug) printf("\tOutput filename: %s\n",local_filename);


		/* get the entry/track/sector for file */
		catalog_entry=dos33_check_file_exists(dos_fd,
							apple_filename,
							FILE_NORMAL);
		if (catalog_entry<0) {
			fprintf(stderr,"Error!  %s not found!\n",
				apple_filename);
			goto exit_and_close;
		}

		dos33_load_file(dos_fd,catalog_entry,local_filename);

		break;

       case COMMAND_CATALOG:

		/* get first catalog */
		catalog_entry=dos33_get_catalog_ts(dos_fd);

		printf("\nDISK VOLUME %i\n\n",sector_buffer[VTOC_DISK_VOLUME]);
		while(catalog_entry>0) {
			catalog_entry=dos33_find_next_file(dos_fd,catalog_entry);
			if (catalog_entry>0) {
				dos33_print_file_info(dos_fd,catalog_entry);
				/* why 1<<16 ? */
				catalog_entry+=(1<<16);
				/* dos33_find_next_file() handles wrapping issues */
			}
		}
		printf("\n");
		break;

	case COMMAND_SAVE:
		/* argv3 == type == A,B,T,I,N,L etc */
		/* argv4 == name of local file */
		/* argv5 == optional name of file on disk image */

		if (argc==optind) {
			fprintf(stderr,"Error! Need type and file_name\n");
			fprintf(stderr,"%s %s SAVE type "
					"file_name apple_filename\n\n",
					argv[0],image);
			goto exit_and_close;
		}

		type=argv[optind][0];
		optind++;

	case COMMAND_BSAVE:

		if (debug) printf("\ttype=%c\n",type);

		if (argc==optind) {
			fprintf(stderr,"Error! Need file_name\n");

			if (command==COMMAND_BSAVE) {
				fprintf(stderr,"%s %s BSAVE "
						"file_name apple_filename\n\n",
						argv[0],image);

			}
			else {
				fprintf(stderr,"%s %s SAVE type "
						"file_name apple_filename\n\n",
						argv[0],image);
			}
			goto exit_and_close;
		}

		strncpy(local_filename,argv[optind],BUFSIZ-1);
		optind++;

		if (debug) printf("\tLocal filename: %s\n",local_filename);

		if (argc>optind) {
			/* apple filename specified */
			truncate_filename(apple_filename,argv[optind]);
		}
		else {
			/* If no filename specified for apple name    */
			/* Then use the input name.  Note, we strip   */
			/* everything up to the last slash so useless */
			/* path info isn't used                       */

			temp=local_filename+(strlen(local_filename)-1);

			while(temp!=local_filename) {
				temp--;
				if (*temp == '/') {
					temp++;
					break;
				}
			}

			truncate_filename(apple_filename,temp);
		}

		if (debug) printf("\tApple filename: %s\n",apple_filename);

		catalog_entry=dos33_check_file_exists(dos_fd,apple_filename,
							FILE_NORMAL);

		if (catalog_entry>=0) {
			fprintf(stderr,"Warning!  %s exists!\n",apple_filename);
			if (!always_yes) {
				printf("Over-write (y/n)?");
				result_string=fgets(temp_string,BUFSIZ,stdin);
				if ((result_string==NULL) || (temp_string[0]!='y')) {
					printf("Exiting early...\n");
					goto exit_and_close;
				}
			}
			fprintf(stderr,"Deleting previous version...\n");
			dos33_delete_file(dos_fd,catalog_entry);
		}
		if (command==COMMAND_SAVE) {
			dos33_add_file(dos_fd,type,
				ADD_RAW, address, length,
				local_filename,apple_filename);
		}
		else {
			dos33_add_file(dos_fd,type,
				ADD_BINARY, address, length,
				local_filename,apple_filename);
		}
		break;


	case COMMAND_RAW_WRITE:

		/* ??? */
		if (debug) printf("\ttype=%c\n",type);

		if (argc==optind) {
			fprintf(stderr,"Error! Need file_name\n");

			fprintf(stderr,"%s %s RAWWRITE "
					"file_name apple_filename\n\n",
					argv[0],image);
			goto exit_and_close;
		}

		strncpy(local_filename,argv[optind],BUFSIZ-1);
		optind++;

		if (debug) printf("\tLocal filename: %s\n",local_filename);

		if (argc>optind) {
			/* apple filename specified */
			truncate_filename(apple_filename,argv[optind]);
		}
		else {
			/* If no filename specified for apple name    */
			/* Then use the input name.  Note, we strip   */
			/* everything up to the last slash so useless */
			/* path info isn't used                       */

			temp=local_filename+(strlen(local_filename)-1);

			while(temp!=local_filename) {
				temp--;
				if (*temp == '/') {
					temp++;
					break;
				}
			}

			truncate_filename(apple_filename,temp);
		}

		if (debug) printf("\tApple filename: %s\n",apple_filename);

		catalog_entry=dos33_check_file_exists(dos_fd,apple_filename,
							FILE_NORMAL);

		if (catalog_entry>=0) {
			fprintf(stderr,"Warning!  %s exists!\n",apple_filename);
			if (!always_yes) {
				printf("Over-write (y/n)?");
				result_string=fgets(temp_string,BUFSIZ,stdin);
				if ((result_string==NULL) || (temp_string[0]!='y')) {
					printf("Exiting early...\n");
					goto exit_and_close;
				}
			}
			fprintf(stderr,"Deleting previous version...\n");
			dos33_delete_file(dos_fd,catalog_entry);
		}

		dos33_raw_file(dos_fd, type, track, sector,
				local_filename,apple_filename);

		break;

	case COMMAND_DELETE:

		if (argc==optind) {
			fprintf(stderr,"Error! Need file_name\n");
			fprintf(stderr,"%s %s DELETE apple_filename\n",
				argv[0],image);
			goto exit_and_close;
		}

		truncate_filename(apple_filename,argv[optind]);

		catalog_entry=dos33_check_file_exists(dos_fd,
						apple_filename,
						FILE_NORMAL);
		if (catalog_entry<0) {
			fprintf(stderr, "Error!  File %s does not exist\n",
					apple_filename);
			goto exit_and_close;
		}
		dos33_delete_file(dos_fd,catalog_entry);

		break;

	case COMMAND_DUMP:
		printf("Dumping %s!\n",image);
		dos33_dump(dos_fd);
		break;

	case COMMAND_SHOWFREE:
		printf("Showing Free %s!\n",image);
		dos33_showfree(dos_fd);
		break;

	case COMMAND_LOCK:
	case COMMAND_UNLOCK:
		/* check and make sure we have apple_filename */
		if (argc==optind) {
			fprintf(stderr,"Error! Need apple file_name\n");
			fprintf(stderr,"%s %s %s apple_filename\n",
				argv[0],image,temp_string);
			goto exit_and_close;
		}

		truncate_filename(apple_filename,argv[optind]);

		/* get the entry/track/sector for file */
		catalog_entry=dos33_check_file_exists(dos_fd,
							apple_filename,
							FILE_NORMAL);
		if (catalog_entry<0) {
			fprintf(stderr,"Error!  %s not found!\n",
				apple_filename);
			goto exit_and_close;
		}

		dos33_lock_file(dos_fd,catalog_entry,command==COMMAND_LOCK);

		break;

	case COMMAND_RENAME:
		/* check and make sure we have apple_filename */
		if (argc==optind) {
			fprintf(stderr,"Error! Need two filenames\n");
			fprintf(stderr,"%s %s LOCK apple_filename_old "
				"apple_filename_new\n",
				argv[0],image);
	     		goto exit_and_close;
		}

		/* Truncate filename if too long */
		truncate_filename(apple_filename,argv[optind]);
		optind++;

		if (argc==optind) {
			fprintf(stderr,"Error! Need two filenames\n");
			fprintf(stderr,"%s %s LOCK apple_filename_old "
				"apple_filename_new\n",
				argv[0],image);
	     		goto exit_and_close;
		}

		truncate_filename(new_filename,argv[optind]);

		/* get the entry/track/sector for file */
		catalog_entry=dos33_check_file_exists(dos_fd,
						apple_filename,
						FILE_NORMAL);
		if (catalog_entry<0) {
			fprintf(stderr,"Error!  %s not found!\n",
							apple_filename);
			goto exit_and_close;
		}

		dos33_rename_file(dos_fd,catalog_entry,new_filename);

		break;

	case COMMAND_UNDELETE:
		/* check and make sure we have apple_filename */
		if (argc==optind) {
			fprintf(stderr,"Error! Need apple file_name\n");
			fprintf(stderr,"%s %s UNDELETE apple_filename\n\n",
				argv[0],image);
			goto exit_and_close;
		}

		/* Truncate filename if too long */
		/* what to do about last char ? */

		truncate_filename(apple_filename,argv[optind]);

		/* get the entry/track/sector for file */
		catalog_entry=dos33_check_file_exists(dos_fd,
						apple_filename,
						FILE_DELETED);
		if (catalog_entry<0) {
			fprintf(stderr,"Error!  %s not found!\n",
				apple_filename);
			goto exit_and_close;
		}

		dos33_undelete_file(dos_fd,catalog_entry,apple_filename);

		break;

	case COMMAND_HELLO:
		if (argc==optind) {
			fprintf(stderr,"Error! Need file_name\n");
			fprintf(stderr,"%s %s HELLO apple_filename\n\n",
				argv[0],image);
			goto exit_and_close;
		}

		truncate_filename(apple_filename,argv[optind]);

		catalog_entry=dos33_check_file_exists(dos_fd,
						apple_filename,
						FILE_NORMAL);

		if (catalog_entry<0) {
			fprintf(stderr,
				"Warning!  File %s does not exist\n",
					apple_filename);
		}
		dos33_rename_hello(dos_fd,apple_filename);
		break;

	case COMMAND_INIT:
		/* use common code from mkdos33fs? */
	case COMMAND_COPY:
		/* use temp file?  Walking a sector at a time seems a pain */
	default:
		fprintf(stderr,"Sorry, unsupported command %s\n\n",temp_string);
		goto exit_and_close;
	}

exit_and_close:
	close(dos_fd);

	return 0;
}
