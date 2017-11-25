/*
 * uudecode [input]
 *
 * Create the specified file, decoding as you go, used with uuencode.
 */
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <pwd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <locale.h>

#define LINE_DATA	0x00	/* Generic data line, see later for specific */
#define LINE_BEGIN	0x10	/* Start of data marker */
#define LINE_EOD	0x20	/* End of data but possibly not file. */
#define LINE_END	0x30	/* 'end' command */
#define LINE_EMPTY	0x40	/* Blank line */
#define LINE_TABLE	0x50	/* End of data section -- like EOF. */
#define LINE_EOF	0x60	/* End of data section -- like EOF. */
#define LINE_SECTION	0x70	/* Section information */
#define LINE_SUBJECT	0x80	/* Usenet or Email subject line */
#define LINE_CONTENT	0x90	/* Content-Type: line */
#define LINE_MBEGIN	0xA0	/* begin-base64 line */
#define LINE_YBEGIN	0xB0	/* =ybegin line */
#define LINE_JUNK	0xC0	/* Nothing seen */

#define LINE_UUDATA	0x01	/* uuencode data line */
#define LINE_XXDATA	0x02	/* xxencode data line */
#define LINE_CDATA	0x03	/* uuencode like data using a custom table */
#define LINE_MMDATA	0x04	/* Base64 data line */
#define LINE_YDATA	0x05	/* yenc line - only used for state. */
#define LINE_ALLDATA	0x0F

#define TEMP_PREFIX	"/var/tmp/"	/* Parts stored here */
#define FNAMESIZ	512
#define LINELEN		1024		/* Adjust to taste */

void do_file(FILE * in, char * fname);
void do_end_of_file(void);
int identify_line(char * buf, int data_type);
int decode_uue_line(char * buf, int data_type);
void open_temp_file(void);
void close_temp_file(void);
void convert_filename(char * source, char * dest, int destlen);
void expand_filename(char * source, char * dest, int destlen);
void decode_subject_line(char * buf);
void decode_section_line(char * buf);
void decode_content_line(char * buf);
void save_temp_file(int suffix, int save_name);
void recover_temp_file(void);
void name_temp_file(char * fname, int mode);
void copy_file(FILE * ofd, char * filename);
void move_file(char * oldname, char * newname, int delete, int dispname);
void set_tempdir(char * name);
char *lgets(FILE * in);
char *lpeeks(FILE * in, int offset);

int yEnc_header(char * buf);
int yEnc_data(char * buf);

char   section_dir[FNAMESIZ];
char   temp_dir[FNAMESIZ];
char   temp_name[FNAMESIZ];
int    temp_open;
FILE * temp_file;

char  dest[FNAMESIZ];
char  backup_dest[FNAMESIZ];
int   mode;

int   decode_ancient_style = 0;	/* Decode using dumb decoder ? */
int   decode_single = 0;	/* Ignore multi-part markers ? */
int   overwrite = 0;		/* Force overwrite existing files */
int   unmodified_filename = 0;	/* Use real filenames with any characters. */
int   verbose = 0;		/* Chatter. */
int   quiet = 0;		/* Don't whinge */
int   pipe_decode = 0;		/* Ignore the 'begin' filename - pipe it */
int   call_munpack = 0;		/* MIME passed to /usr/bin/munpack */

char cv_buf[LINELEN];
int  cv_len;

char section_name[FNAMESIZ];
int  section_parts;
int  section_partno = 0;

char subject_saved[FNAMESIZ];
char old_subject[FNAMESIZ];

int exit_status = 0;
int mime_message = 0;

char * current_file = 0;

#define	MAXLA	4
char lb_buffer[MAXLA][LINELEN];
int  lb_filled = 0, lb_state = 0;
char *lb_ref[MAXLA];

char custom_dec[256];		/* Custom decode table from 'table' lines */
unsigned char custom_enc[65];	/* and the matching encode table. */
int  enable_custom = 0;		/* Enable custom table ? */

int corruption = 0;
int found_begin = 0;

/* Single-character decode for uuencode. */
#define DEC(c)	(((c) - ' ') & 077)

/* Single-character decode for xxencode  */
char xxencode_dec[] = {
    '\177', '\177', '\177', '\177', '\177', '\177', '\177', '\177', /*000-007*/
    '\177', '\177', '\177', '\177', '\177', '\177', '\177', '\177', /*010-017*/
    '\177', '\177', '\177', '\177', '\177', '\177', '\177', '\177', /*020-027*/
    '\177', '\177', '\177', '\177', '\177', '\177', '\177', '\177', /*030-037*/
    '\177', '\177', '\177', '\177', '\177', '\177', '\177', '\177', /*040-047*/
    '\177', '\177', '\177', '\000', '\177', '\001', '\177', '\177', /*050-057*/
    '\002', '\003', '\004', '\005', '\006', '\007', '\010', '\011', /*060-067*/
    '\012', '\013', '\177', '\177', '\177', '\177', '\177', '\177', /*070-077*/
    '\177', '\014', '\015', '\016', '\017', '\020', '\021', '\022', /*100-107*/
    '\023', '\024', '\025', '\026', '\027', '\030', '\031', '\032', /*110-117*/
    '\033', '\034', '\035', '\036', '\037', '\040', '\041', '\042', /*120-127*/
    '\043', '\044', '\045', '\177', '\177', '\177', '\177', '\177', /*130-137*/
    '\177', '\046', '\047', '\050', '\051', '\052', '\053', '\054', /*140-147*/
    '\055', '\056', '\057', '\060', '\061', '\062', '\063', '\064', /*150-157*/
    '\065', '\066', '\067', '\070', '\071', '\072', '\073', '\074', /*160-167*/
    '\075', '\076', '\077', '\177', '\177', '\177', '\177', '\177', /*170-177*/
    '\177', '\177', '\177', '\177', '\177', '\177', '\177', '\177', /*200-207*/
    '\177', '\177', '\177', '\177', '\177', '\177', '\177', '\177', /*210-217*/
    '\177', '\177', '\177', '\177', '\177', '\177', '\177', '\177', /*220-227*/
    '\177', '\177', '\177', '\177', '\177', '\177', '\177', '\177', /*230-237*/
    '\177', '\177', '\177', '\177', '\177', '\177', '\177', '\177', /*240-247*/
    '\177', '\177', '\177', '\177', '\177', '\177', '\177', '\177', /*250-257*/
    '\177', '\177', '\177', '\177', '\177', '\177', '\177', '\177', /*260-267*/
    '\177', '\177', '\177', '\177', '\177', '\177', '\177', '\177', /*270-277*/
    '\177', '\177', '\177', '\177', '\177', '\177', '\177', '\177', /*300-307*/
    '\177', '\177', '\177', '\177', '\177', '\177', '\177', '\177', /*310-317*/
    '\177', '\177', '\177', '\177', '\177', '\177', '\177', '\177', /*320-327*/
    '\177', '\177', '\177', '\177', '\177', '\177', '\177', '\177', /*330-337*/
    '\177', '\177', '\177', '\177', '\177', '\177', '\177', '\177', /*340-347*/
    '\177', '\177', '\177', '\177', '\177', '\177', '\177', '\177', /*350-357*/
    '\177', '\177', '\177', '\177', '\177', '\177', '\177', '\177', /*360-367*/
    '\177', '\177', '\177', '\177', '\177', '\177', '\177', '\177', /*370-377*/
};

/* Single-character decode for base64 */
char base64_dec[] = {
    '\177', '\177', '\177', '\177', '\177', '\177', '\177', '\177', /*000-007*/
    '\177', '\177', '\177', '\177', '\177', '\177', '\177', '\177', /*010-017*/
    '\177', '\177', '\177', '\177', '\177', '\177', '\177', '\177', /*020-027*/
    '\177', '\177', '\177', '\177', '\177', '\177', '\177', '\177', /*030-037*/
    '\177', '\177', '\177', '\177', '\177', '\177', '\177', '\177', /*040-047*/
    '\177', '\177', '\177', '\76',  '\177', '\177', '\177', '\77',  /*050-057*/
    '\64',  '\65',  '\66',  '\67',  '\70',  '\71',  '\72',  '\73',  /*060-067*/
    '\74',  '\75',  '\177', '\177', '\177', '\0',   '\177', '\177', /*070-077*/
    '\177', '\0',   '\1',   '\2',   '\3',   '\4',   '\5',   '\6',   /*100-107*/
    '\7',   '\10',  '\11',  '\12',  '\13',  '\14',  '\15',  '\16',  /*110-117*/
    '\17',  '\20',  '\21',  '\22',  '\23',  '\24',  '\25',  '\26',  /*120-127*/
    '\27',  '\30',  '\31',  '\177', '\177', '\177', '\177', '\177', /*130-137*/
    '\177', '\32',  '\33',  '\34',  '\35',  '\36',  '\37',  '\40',  /*140-147*/
    '\41',  '\42',  '\43',  '\44',  '\45',  '\46',  '\47',  '\50',  /*150-157*/
    '\51',  '\52',  '\53',  '\54',  '\55',  '\56',  '\57',  '\60',  /*160-167*/
    '\61',  '\62',  '\63',  '\177', '\177', '\177', '\177', '\177', /*170-177*/
    '\177', '\177', '\177', '\177', '\177', '\177', '\177', '\177', /*200-207*/
    '\177', '\177', '\177', '\177', '\177', '\177', '\177', '\177', /*210-217*/
    '\177', '\177', '\177', '\177', '\177', '\177', '\177', '\177', /*220-227*/
    '\177', '\177', '\177', '\177', '\177', '\177', '\177', '\177', /*230-237*/
    '\177', '\177', '\177', '\177', '\177', '\177', '\177', '\177', /*240-247*/
    '\177', '\177', '\177', '\177', '\177', '\177', '\177', '\177', /*250-257*/
    '\177', '\177', '\177', '\177', '\177', '\177', '\177', '\177', /*260-267*/
    '\177', '\177', '\177', '\177', '\177', '\177', '\177', '\177', /*270-277*/
    '\177', '\177', '\177', '\177', '\177', '\177', '\177', '\177', /*300-307*/
    '\177', '\177', '\177', '\177', '\177', '\177', '\177', '\177', /*310-317*/
    '\177', '\177', '\177', '\177', '\177', '\177', '\177', '\177', /*320-327*/
    '\177', '\177', '\177', '\177', '\177', '\177', '\177', '\177', /*330-337*/
    '\177', '\177', '\177', '\177', '\177', '\177', '\177', '\177', /*340-347*/
    '\177', '\177', '\177', '\177', '\177', '\177', '\177', '\177', /*350-357*/
    '\177', '\177', '\177', '\177', '\177', '\177', '\177', '\177', /*360-367*/
    '\177', '\177', '\177', '\177', '\177', '\177', '\177', '\177', /*370-377*/
};

/*
 * Copyright (C) 1986 Gary S. Brown. CRC-32 tables and macro.
 */

/* First, the polynomial itself and its table of feedback terms.  The  */
/* polynomial is                                                       */
/* X^32+X^26+X^23+X^22+X^16+X^12+X^11+X^10+X^8+X^7+X^5+X^4+X^2+X^1+X^0 */
/* Note that we take it "backwards" and put the highest-order term in  */
/* the lowest-order bit.  The X^32 term is "implied"; the LSB is the   */
/* X^31 term, etc.  The X^0 term (usually shown as "+1") results in    */
/* the MSB being 1.                                                    */

/* Note that the usual hardware shift register implementation, which   */
/* is what we're using (we're merely optimizing it by doing eight-bit  */
/* chunks at a time) shifts bits into the lowest-order term.  In our   */
/* implementation, that means shifting towards the right.  Why do we   */
/* do it this way?  Because the calculated CRC must be transmitted in  */
/* order from highest-order term to lowest-order term.  UARTs transmit */
/* characters in order from LSB to MSB.  By storing the CRC this way,  */
/* we hand it to the UART in the order low-byte to high-byte; the UART */
/* sends each low-bit to hight-bit; and the result is transmission bit */
/* by bit from highest- to lowest-order term without requiring any bit */
/* shuffling on our part.  Reception works similarly.                  */

/* The feedback terms table consists of 256, 32-bit entries.  Notes:   */
/*                                                                     */
/*     The table can be generated at runtime if desired; code to do so */
/*     is shown later.  It might not be obvious, but the feedback      */
/*     terms simply represent the results of eight shift/xor opera-    */
/*     tions for all combinations of data and CRC register values.     */
/*                                                                     */
/*     The values must be right-shifted by eight bits by the "updcrc"  */
/*     logic; the shift must be unsigned (bring in zeroes).  On some   */
/*     hardware you could probably optimize the shift in assembler by  */
/*     using byte-swap instructions.                                   */

static long cr3tab[] = { /* CRC polynomial 0xedb88320 */
0x00000000, 0x77073096, 0xee0e612c, 0x990951ba, 0x076dc419, 0x706af48f, 0xe963a535, 0x9e6495a3,
0x0edb8832, 0x79dcb8a4, 0xe0d5e91e, 0x97d2d988, 0x09b64c2b, 0x7eb17cbd, 0xe7b82d07, 0x90bf1d91,
0x1db71064, 0x6ab020f2, 0xf3b97148, 0x84be41de, 0x1adad47d, 0x6ddde4eb, 0xf4d4b551, 0x83d385c7,
0x136c9856, 0x646ba8c0, 0xfd62f97a, 0x8a65c9ec, 0x14015c4f, 0x63066cd9, 0xfa0f3d63, 0x8d080df5,
0x3b6e20c8, 0x4c69105e, 0xd56041e4, 0xa2677172, 0x3c03e4d1, 0x4b04d447, 0xd20d85fd, 0xa50ab56b,
0x35b5a8fa, 0x42b2986c, 0xdbbbc9d6, 0xacbcf940, 0x32d86ce3, 0x45df5c75, 0xdcd60dcf, 0xabd13d59,
0x26d930ac, 0x51de003a, 0xc8d75180, 0xbfd06116, 0x21b4f4b5, 0x56b3c423, 0xcfba9599, 0xb8bda50f,
0x2802b89e, 0x5f058808, 0xc60cd9b2, 0xb10be924, 0x2f6f7c87, 0x58684c11, 0xc1611dab, 0xb6662d3d,
0x76dc4190, 0x01db7106, 0x98d220bc, 0xefd5102a, 0x71b18589, 0x06b6b51f, 0x9fbfe4a5, 0xe8b8d433,
0x7807c9a2, 0x0f00f934, 0x9609a88e, 0xe10e9818, 0x7f6a0dbb, 0x086d3d2d, 0x91646c97, 0xe6635c01,
0x6b6b51f4, 0x1c6c6162, 0x856530d8, 0xf262004e, 0x6c0695ed, 0x1b01a57b, 0x8208f4c1, 0xf50fc457,
0x65b0d9c6, 0x12b7e950, 0x8bbeb8ea, 0xfcb9887c, 0x62dd1ddf, 0x15da2d49, 0x8cd37cf3, 0xfbd44c65,
0x4db26158, 0x3ab551ce, 0xa3bc0074, 0xd4bb30e2, 0x4adfa541, 0x3dd895d7, 0xa4d1c46d, 0xd3d6f4fb,
0x4369e96a, 0x346ed9fc, 0xad678846, 0xda60b8d0, 0x44042d73, 0x33031de5, 0xaa0a4c5f, 0xdd0d7cc9,
0x5005713c, 0x270241aa, 0xbe0b1010, 0xc90c2086, 0x5768b525, 0x206f85b3, 0xb966d409, 0xce61e49f,
0x5edef90e, 0x29d9c998, 0xb0d09822, 0xc7d7a8b4, 0x59b33d17, 0x2eb40d81, 0xb7bd5c3b, 0xc0ba6cad,
0xedb88320, 0x9abfb3b6, 0x03b6e20c, 0x74b1d29a, 0xead54739, 0x9dd277af, 0x04db2615, 0x73dc1683,
0xe3630b12, 0x94643b84, 0x0d6d6a3e, 0x7a6a5aa8, 0xe40ecf0b, 0x9309ff9d, 0x0a00ae27, 0x7d079eb1,
0xf00f9344, 0x8708a3d2, 0x1e01f268, 0x6906c2fe, 0xf762575d, 0x806567cb, 0x196c3671, 0x6e6b06e7,
0xfed41b76, 0x89d32be0, 0x10da7a5a, 0x67dd4acc, 0xf9b9df6f, 0x8ebeeff9, 0x17b7be43, 0x60b08ed5,
0xd6d6a3e8, 0xa1d1937e, 0x38d8c2c4, 0x4fdff252, 0xd1bb67f1, 0xa6bc5767, 0x3fb506dd, 0x48b2364b,
0xd80d2bda, 0xaf0a1b4c, 0x36034af6, 0x41047a60, 0xdf60efc3, 0xa867df55, 0x316e8eef, 0x4669be79,
0xcb61b38c, 0xbc66831a, 0x256fd2a0, 0x5268e236, 0xcc0c7795, 0xbb0b4703, 0x220216b9, 0x5505262f,
0xc5ba3bbe, 0xb2bd0b28, 0x2bb45a92, 0x5cb36a04, 0xc2d7ffa7, 0xb5d0cf31, 0x2cd99e8b, 0x5bdeae1d,
0x9b64c2b0, 0xec63f226, 0x756aa39c, 0x026d930a, 0x9c0906a9, 0xeb0e363f, 0x72076785, 0x05005713,
0x95bf4a82, 0xe2b87a14, 0x7bb12bae, 0x0cb61b38, 0x92d28e9b, 0xe5d5be0d, 0x7cdcefb7, 0x0bdbdf21,
0x86d3d2d4, 0xf1d4e242, 0x68ddb3f8, 0x1fda836e, 0x81be16cd, 0xf6b9265b, 0x6fb077e1, 0x18b74777,
0x88085ae6, 0xff0f6a70, 0x66063bca, 0x11010b5c, 0x8f659eff, 0xf862ae69, 0x616bffd3, 0x166ccf45,
0xa00ae278, 0xd70dd2ee, 0x4e048354, 0x3903b3c2, 0xa7672661, 0xd06016f7, 0x4969474d, 0x3e6e77db,
0xaed16a4a, 0xd9d65adc, 0x40df0b66, 0x37d83bf0, 0xa9bcae53, 0xdebb9ec5, 0x47b2cf7f, 0x30b5ffe9,
0xbdbdf21c, 0xcabac28a, 0x53b39330, 0x24b4a3a6, 0xbad03605, 0xcdd70693, 0x54de5729, 0x23d967bf,
0xb3667a2e, 0xc4614ab8, 0x5d681b02, 0x2a6f2b94, 0xb40bbe37, 0xc30c8ea1, 0x5a05df1b, 0x2d02ef8d
};

#define UPDC32(b, c) (cr3tab[((int)c ^ b) & 0xff] ^ ((c >> 8) & 0x00FFFFFF))

int main(int argc, char **argv) 
{
   int   ar;
   FILE *in;
   int done_file = 0;
   char * p;
   int rv = 0;

   setlocale(LC_CTYPE, "");

   if ((p = getenv("TMPDIR")) != 0) set_tempdir(p);
   else {
      strcpy(section_dir, TEMP_PREFIX);
      *temp_dir = 0;
   }

   /*
    * optional input args
    */
   if (argc > 1) {
      for (ar=1; ar<argc; ar++) if (argv[ar][0] == '-' && argv[ar][1]) {
	 for (p=argv[ar]+1; *p; p++)
	    switch(*p) {
	    default:
	       fprintf(stderr, "Usage: uudecode [-h][-vqasopCM] [infile]\n");
	       exit(2);
	    case 'h': case ':': 
printf("Usage: uudecode [-h][-vqasopCM] [infile]\n");
printf(" Decodes multipart uuencode, xxencode, base64 and yEnc files.\n");
printf("   -v	Verbose\n");
printf("   -q	Quiet - don't complain about files without a 'begin'.\n");
printf("   -a	Use ancient style uudecoder for poor uue files.\n");
printf("   -s	Decode as singlton message; no multipart processing.\n");
printf("   -o	Overwrite files.\n");
printf("   -f	Use full unmodified filename from begin line.\n");
printf("   -p	Pipe decode; send all data (and junk) to stdout.\n");
printf("   -C	Use current directory for temporary files.\n");
printf("   -M	Give a file to munpack if it looks like a MIME multipart.\n");
	       exit(0);
	    case 'v': verbose++;
		      break;
	    case 'q': quiet++;
		      break;
	    case 'a': decode_ancient_style = 1; decode_single = 1;
		      break;
	    case 's': decode_single = 1;
		      break;
	    case 'o': overwrite = 1;
		      break;
	    case 'f': unmodified_filename = 1;
		      break;
	    case 'C': set_tempdir("");
		      break;
	    case 'p': pipe_decode = 1;
		      break;
	    case 'M': call_munpack = 1;
		      break;
	    }
      } else {
	 done_file = 1;
	 exit_status = 0;
	 if ((in = fopen(argv[ar], "r")) == NULL) {
	    perror(argv[ar]);
	    rv |= 2;
	 } else {
	    mime_message = 0;
	    do_file(in, argv[ar]);
	    fclose(in);
	    rv |= exit_status;

	    if (exit_status > 3) break;
	    if (call_munpack && mime_message>0 && exit_status) {
	       char buf[FNAMESIZ];
	       sprintf(buf, "munpack %s", argv[ar]);
	       system(buf);
	    }
	 }
      }
   }
   if (!done_file) {
      if (call_munpack)
	 fprintf(stderr, "uudecode: Cannot call munpack with standard input\n");
      in = stdin;
      do_file(in, "Standard input");
      rv |= exit_status;
   }
   exit(rv);
}

void set_tempdir(char * name)
{
   *section_dir = 0;
   strncpy(temp_dir, name, sizeof(temp_dir));
   temp_dir[sizeof(temp_dir)-4] = 0;
   if (!*temp_dir) return;
   if (temp_dir[strlen(temp_dir)-1] != '/')
      strcat(temp_dir, "/");

   strcpy(section_dir, temp_dir);
}

void do_file(FILE * in, char * fname)
{
   char *s, *linep;
   int ntype, ltype, rtype, last_ltype, last_rtype;
   int lt0, lt1, lt2, lt3, i;

   last_ltype = last_rtype = LINE_JUNK;
   current_file = fname;
   sprintf(temp_name, "%s_uue.XXXXXX", temp_dir);
   mktemp(temp_name);

   found_begin = 0;
   corruption = 0; *dest = 0; *backup_dest = 0; *section_name = 0;

   for(;(linep=lgets(in)); last_ltype = ltype, last_rtype = rtype) {

      rtype = identify_line(linep, last_rtype);

      if (verbose>3) {
	 char * str = "JUNK ";
	 switch(rtype)
	 {
	 case LINE_DATA:   str = "DATA "; break;
	 case LINE_UUDATA: str = "UDATA"; break;
	 case LINE_XXDATA: str = "XDATA"; break;
	 case LINE_CDATA:  str = "CDATA"; break;
	 case LINE_MMDATA: str = "MDATA"; break;
	 case LINE_YDATA:  str = "YDATA"; break;
	 case LINE_BEGIN:  str = "BEGIN"; break;
	 case LINE_EOD:    str = "EOD  "; break;
	 case LINE_END:    str = "END  "; break;
	 case LINE_TABLE:  str = "TABLE"; break;
	 case LINE_EMPTY:  str = "EMPTY"; break;
	 case LINE_EOF:    str = "EOF  "; break;
	 case LINE_SECTION:str = "SECT "; break;
	 case LINE_SUBJECT:str = "SUB  "; break;
	 case LINE_CONTENT:str = "CONT "; break;
	 case LINE_MBEGIN: str = "MBEG "; break;
	 case LINE_YBEGIN: str = "YENC "; break;
	 }
	 fprintf(stderr, "%s: %s\n", str, linep);
      }

      if (rtype == LINE_JUNK && last_rtype == LINE_UUDATA) {
	 /* Some uuencoders cannot cope properly with the last line.
	  * If it looks like we have one of these then adjust the 
	  * count on the last data line.
	  */

	 ntype = identify_line(lpeeks(in,1), LINE_DATA);
	 if (ntype == LINE_EOD)
	    ntype = identify_line(lpeeks(in,2), LINE_DATA);
	 if (ntype == LINE_END) {
	    fprintf(stderr, "uudecode: assuming corrupt line is data: %s\n",
			    linep);
	    corruption = 1;
	    linep[0] = ' '+((strlen(linep)-1)*6)/8;
	    (void) decode_uue_line(linep, LINE_DATA);
	    rtype = LINE_UUDATA;
	 }
      }

      if (rtype < LINE_ALLDATA) 
	 ltype = LINE_DATA;
      else if (rtype == LINE_MBEGIN || rtype == LINE_YBEGIN)
	 ltype = LINE_BEGIN;
      else
	 ltype = rtype;

      if (pipe_decode) {
	 if (ltype == LINE_DATA && cv_len>0)
	    fwrite(cv_buf, 1, cv_len, stdout);
	 else if (ltype!=LINE_BEGIN && ltype!=LINE_EOD && ltype!=LINE_END)
	    printf("%s\n", linep);
	 continue;
      }

      /* If we're in the middle of decoding some data */
      if (last_ltype == LINE_DATA || last_ltype == LINE_BEGIN ||
	    last_ltype == LINE_EOD) {

	 switch(ltype) {
	 case LINE_DATA:
	    if (cv_len>0) {
	       if (!temp_open) open_temp_file();
	       fwrite(cv_buf, 1, cv_len, temp_file);
	    }
	    continue;

	 case LINE_EOD:
	    ntype = identify_line(lpeeks(in,1), rtype);
	    if (ntype < LINE_ALLDATA || ntype == LINE_END)
		  continue;
	    fprintf(stderr, "uudecode: No end line: %s\n", current_file);
	    /*FALLTHROUGH*/

	 case LINE_END:
	    rtype = ltype = LINE_END;

	    if (*dest) {
	       if(corruption) {
		  fprintf(stderr, "uudecode: corrupt file: %s in %s\n",
				   dest, current_file);
		  exit_status |= 2;
	       } else if (!temp_open) {
		  fprintf(stderr, "uudecode: no data found for %s in %s\n",
				   dest, current_file);
		  exit_status |= 2;
	       }
	       name_temp_file(dest, mode);
	    } else if (*section_name) {
	       /* This looks like a partial end file */
	       save_temp_file('e', 0);
	    } else if (temp_open) {
	       fprintf(stderr, "uudecode: missing begin: %s\n", current_file);
	       close_temp_file();
	       exit_status |= 2;
	    }
	    corruption = 0;
	    *dest = 0;
	    enable_custom = 0;
	    continue;

	 case LINE_EOF:
	    do_end_of_file();
	    continue;

	 case LINE_EMPTY:
	    /* If it's an empty line be a little forgiving and check for a
	     * following end or more data.
	     */
	    ntype = identify_line(lpeeks(in,1), last_rtype);
	    if (ntype == LINE_END || ntype < LINE_ALLDATA) {
	       rtype = last_rtype;
	       ltype = last_ltype;
	       continue;
	    }
	    /*FALLTHROUGH*/

	 default: /* JUNK etc */
	    lt1 = identify_line(lpeeks(in,1), LINE_JUNK);
	    lt2 = identify_line(lpeeks(in,2), LINE_JUNK);
	    lt3 = identify_line(lpeeks(in,3), LINE_JUNK);

	    if (lt1 == LINE_EOF || lt2 == LINE_EOF || lt3 == LINE_EOF)
	       ;
	    else if (lt1 < LINE_ALLDATA ||  /* 1-3 corrupt lines + data? */
		     lt2 < LINE_ALLDATA ||
		     lt3 < LINE_ALLDATA) {
	       corruption = 1;

	       /* Allow for possibility of using rar recovery records */
	       memset(cv_buf, 0, 45);
	       if(temp_open)
		  fwrite(cv_buf, 1, 45, temp_file);
	       continue;
	    }
	    do_end_of_file();
	    break;
	 }
      }

      /* If it looks like a mime multipart message dropout early. */
      if (call_munpack && mime_message>0) {
	 if (mime_message == 2) {
	    close_temp_file();
	    exit_status |= 1;
	    return;
	 }
	 /* First empty line is end of header -- munpack only works 
	  * with single messages in a file.
	  */
	 if (ltype == LINE_EMPTY)
	    mime_message = -1;
      }

      switch(ltype) {
      case LINE_BEGIN:
	 found_begin = 1;
	 close_temp_file();

	 if (enable_custom) rtype = LINE_CDATA; /* Force custom table. */

	 mode = 0644;
	 s = strstr(linep, "begin");
	 if (rtype == LINE_YBEGIN) { /* yEnc different again */
	    s = strstr(linep, " name=");
	    if (s==0) s=linep;
	    else s += 6;
	 }
	 else if (s==0) s=linep;
	 else {
	    while(*s && !isspace(*s)) s++;
	    (void) sscanf(s, "%o", &mode);

	    /* Trim off that number. */
	    while(*s && isspace(*s)) s++;
	    while(*s && isdigit(*s)) s++;
	    while(*s && isspace(*s)) s++;
	 }

	 if (unmodified_filename)
	    expand_filename(s, dest, sizeof(dest));
	 else
	    convert_filename(s, dest, sizeof(dest));
	 break;

      case LINE_TABLE:
	 lt1 = identify_line(lpeeks(in,1), LINE_JUNK);
	 lt2 = identify_line(lpeeks(in,2), LINE_JUNK);
	 lt3 = identify_line(lpeeks(in,3), LINE_JUNK);
	 lt0 = identify_line(linep, LINE_JUNK);

	 if (lt3 != LINE_BEGIN) break;
	 if (strlen(lpeeks(in,1)) != 32 || strlen(lpeeks(in,2)) != 32) 
	    break;

	 /* Okay looks like a table ... */
	 strcpy(custom_enc, lgets(in));
	 strcat(custom_enc, lgets(in));
	 enable_custom = 1;

	 memset(custom_dec, '\177', 256);
	 for(i=0; i<64; i++) {
	    if (custom_dec[custom_enc[i]] != '\177' ) {
	       fprintf(stderr, 
		       "uudecode: WARNING: Corrupt custom table found: %s\n",
		       current_file);
	       enable_custom = 0;
	       break;
	    }
	    custom_dec[custom_enc[i]] = i;
	 }

	 if (enable_custom) {
	    if (verbose>1) 
	       fprintf(stderr, "uudecode: uutab %s\n", custom_enc);
	 }
	 break;

      case LINE_DATA:
	 /* We weren't expecting data so we'll be a little picky and 
	  * check that the next few lines are data too. (or reasonable)
	  */
	 {
	    lt1 = identify_line(lpeeks(in,1), LINE_DATA);
	    lt2 = identify_line(lpeeks(in,2), LINE_DATA);
	    lt3 = identify_line(lpeeks(in,3), LINE_DATA);
	    lt0 = identify_line(linep, LINE_DATA);

	    if ((lt1==lt0 && lt2==lt0 && lt3==lt0) ||
	        (lt1==lt0 && lt2==lt0 && lt3==LINE_EOD) ||
	        (lt1==lt0 && lt2==lt0 && lt3==LINE_END) ||
	        (lt1==lt0 && lt2==LINE_EOD && lt3==LINE_END) ||
	        (lt1==lt0 && lt2==LINE_END) ||
	        (lt1==LINE_EOD && lt2==LINE_END)) {

	       if (cv_len>0) {
		  if (!temp_open) open_temp_file();
		  fwrite(cv_buf, 1, cv_len, temp_file);
	       }
	       found_begin = 1;
	       continue;
	    }
	 }

	 rtype = ltype = LINE_JUNK;
	 break;

      case LINE_SUBJECT: 
	 while(isspace(lpeeks(in, 1)[0]) && 
	       strlen(subject_saved) < sizeof(subject_saved)-1) {
	    linep = lgets(in);
	    if (verbose>3)
	       fprintf(stderr, "LINESUB+: %s", linep);

	    strncat(subject_saved, linep, 
		    sizeof(subject_saved)-strlen(subject_saved) -1);
	 }
	 decode_subject_line(subject_saved);
	 break;

      case LINE_SECTION: 
	 decode_section_line(linep);
	 break;

      case LINE_CONTENT: 
	 decode_content_line(linep);
	 break;

      default:
	 rtype = ltype = LINE_JUNK;
	 break;
      }
   }
   if (pipe_decode) return;

   if (last_ltype == LINE_DATA || last_ltype == LINE_BEGIN)
      do_end_of_file();

   if (!found_begin) {
      if (!quiet)
	 fprintf(stderr, "uudecode: No begin line: %s\n", current_file);
      exit_status |= 1;
   }
}

char * lgets(FILE * in)
{
   int i;
   char * p;

   if (lb_ref[0] == 0) for(i=0; i<MAXLA; i++) lb_ref[i] = lb_buffer[i];

   p = lb_ref[0];
   for(i=1; i<MAXLA; i++) lb_ref[i-1] = lb_ref[i];
   lb_ref[MAXLA-1] = p;
   if(lb_filled>0) lb_filled --;

   if (lb_filled > 0) return lb_ref[0];
   lb_filled++;
   lb_ref[0][0] = 0;
   if (feof(in)) return 0;
   else if (fgets(lb_ref[0], LINELEN, in) == 0) return 0;

   return lb_ref[0];
}

char * lpeeks(FILE * in, int offset)
{
   if (offset < 0 || offset >= MAXLA) return "";
   while (lb_filled <= offset) {
      lb_ref[lb_filled][0] = 0;
      if (feof(in))
	 return "";
      else if (fgets(lb_ref[lb_filled], LINELEN, in) == 0)
	 return "";
      lb_filled++;
   }

   if(verbose>3) {
      char buf[LINELEN];
      int len;
      strcpy(buf, lb_ref[offset]);
      for(len=strlen(buf); len>0 && (buf[len-1]=='\r' || buf[len-1]=='\n') ;) 
	 buf[--len] = 0;
      fprintf(stderr, "LA(%d): %s\n", offset, buf);
   }
   return lb_ref[offset];
}

void do_end_of_file(void)
{
   if (*section_name == 0) {
      if (*dest) {
	 fprintf(stderr, "uudecode: Short file: %s\n", current_file);
	 exit_status |= 2;
      } else if (!corruption) {
	 if (!*backup_dest) {
	    if(verbose>0)
	       fprintf(stderr, "uudecode: unnamed section: %s\n", current_file);

	    if (strncasecmp(old_subject, "subject:", 8) == 0)
	       convert_filename(old_subject+8, dest, sizeof(dest));
	    else
	       convert_filename(old_subject, dest, sizeof(dest));
	    name_temp_file(dest, 0600);
	 } else
	    name_temp_file(backup_dest, 0600);
      } else {
	 fprintf(stderr, "uudecode: corrupt unnamed section: %s\n",
	       current_file);
	 exit_status |= 2;
      }
   } else {
      if (*dest) {
	 /* This is a begin section, save both the section and the name. */
	 save_temp_file('s', 1);
      } else if (temp_open) {
	 /* This is a middle data section save it */
	 save_temp_file('d', 0);
      }
   }

   close_temp_file();
   corruption = 0; *dest = 0;
}

void open_temp_file(void) 
{
   if (temp_open) close_temp_file();

   /* Create temporary output file */
#ifdef MSDOS
   temp_file = fopen(temp_name, "wb");	       /* Binary file */
#else
   temp_file = fopen(temp_name, "w");
#endif
   if (temp_file == NULL) {
      perror(temp_name);
      exit(99);
   }
   temp_open = 1; 
}

void close_temp_file(void)
{
   if (!temp_open) return;
   fclose(temp_file);
   unlink(temp_name);
   temp_open = 0;
}

void name_temp_file(char * fname, int mode)
{
   if (!temp_open) return;
   fclose(temp_file);
   temp_open = 0;

   move_file(temp_name, fname, overwrite, (verbose>0));
#ifdef UNIX
   /*
    * Only keep the execute bit, otherwise use umask(). 
    */
   if (mode & 0100) {
      mode = umask(077);
      umask(mode);
      mode = (0777 & ~mode);
      chmod(dest, mode);
   }
#endif
}

void convert_filename(char * source, char * dest, int destlen)
{
   unsigned char *s, *d;

   /* Select only known safe filenames. */
   s = source;
   for (d=dest; *s && (char*)d<dest + destlen - 1; s++) {
      int ch = *s;
      if (isalnum(ch))
	 *d++ = ch;
      else {
	 if (ch == ' ' || ch == '_') ch = '_';
	 else if (ch != '.') ch = '-';

	 if ((char*)d == dest)
	    ;
	 else if (isalnum(d[-1])) 
	    *d++ = ch;
	 else if (ch == '.')
	    d[-1] = ch;
	 else if (d[-1] == '_' && ch == '-')
	    d[-1] = ch;
      }
   }
   if ((char*)d>dest && (d[-1] == '_' || d[-1] == '-')) d--;

   *d = 0;
   if (*dest == 0) strcpy(dest, "Unknown_filename.bin");

   for(d=dest; *d; d++) if (islower(*d)) return;

   for(d=dest; *d; d++) if (isupper(*d)) *d = tolower(*d);
}

void expand_filename(char * source, char * dest, int destlen)
{
   int i;
   while (*source && isspace(*source)) source++;
   strcpy(dest, source);

   /* handle ~user/file format */
   if (source[0] == '~') {
      char *sl, *sp;
      struct passwd *user;

      sl = strchr(dest, '/');
      sp = strchr(source, '/');
      if (sl == NULL) {
         fprintf(stderr, "uudecode: Illegal ~user\n");
      } else {
	 *sl = 0;
	 user = getpwnam(dest+1);
	 if (user == NULL) {
	    fprintf(stderr, "uudecode: No such user as %s\n", dest);
	    strcpy(dest, source);
	 } else {
	    strcpy(dest, user->pw_dir);
	    strcat(dest, sp);
	 }
      }
   }

   i = strlen(dest);
   while(i>0 && (isspace(dest[i-1]) || iscntrl(dest[i-1]))) dest[--i] = 0;

   if (*dest == 0) strcpy(dest, "Unknown.bin");
}

void decode_subject_line(char * buf)
{
static char open_br[] = "([{<", close_br[] = ")]}>";

   char * s = buf+8;
   char * p;
   int i, k, n;

   /* Quick and nasty decoder to find part numbers in subject lines. */
   while(*s && isspace(*s)) s++;

   for(i=0; open_br[i]; i++) {
      for(p=buf+strlen(buf); p>=s; p--) {
	 if (*p == open_br[i]) {
	    if (sscanf(p+1, "%d/%d", &k, &n) == 2 && n > 1 && k <= n) {
	       char * t = p+1;
	       while(*t && isdigit(*t)) t++;
	       if (*t == '/') t++;
	       while(*t && isdigit(*t)) t++;
	       if (*t == close_br[i]) {

		  while(p<=t && *p) *p++ = ' ';
		  section_partno = k;
		  section_parts = n;
		  convert_filename(s, section_name, sizeof(section_name));
		  if (*section_name && verbose>1) 
		     fprintf(stderr, 
			     "uudecode: section header %d of %d in %s\n", 
			     section_partno, section_parts, section_name);
		  return;
	       }
	    }
	 }
      }
   }
}

void decode_section_line(char * buf)
{
   char * s;
   char * p;

   *section_name = 0;

   /* If we get here we know there's a digit in the line after the 
    * word 'section' - find it. */
   for(s=buf; *s && !isdigit(*s); s++);
   
   if (sscanf(s, "%d/%d", &section_partno, &section_parts) == 2 ||
       sscanf(s, "%d of %d", &section_partno, &section_parts) == 2) {

      /* Okay we've got the numbers ... now to find the name. */
      for(; *s && isdigit(*s); s++);
      for(; *s && !isdigit(*s); s++);
      for(; *s && isdigit(*s); s++);
      for(; *s && isspace(*s); s++);
      if (strncmp(s, "of file", 7) == 0) s += 7;
      else if (strncmp(s, "of ", 3) == 0) s += 3;
      else if (strncmp(s, "file:", 5) == 0)   s += 5;

      /* s should now point to the start of the name. Strip off some junk. */
      for (p=s; *p; p++) if (strncmp(p, "  <",3) == 0) {*p=0; break;}

      convert_filename(s, section_name, sizeof(section_name));
   }

   if (*section_name && verbose>1) 
      fprintf(stderr, "uudecode: section header %d of %d in %s\n", 
		       section_partno, section_parts, section_name);
}

void decode_content_line(char * buf)
{
   char *p, *s;
   if (section_parts > 1) return;

   if ( (p = strstr(buf, "name=")) != 0 && p > buf && !isalpha(p[-1])) {
      while(*p && *p != '"') p++;
      if (*p) {
	 s = ++p;
	 while(*p && *p != '"') p++;
      }
      if (*p) {
	 *p = 0;
	 convert_filename(s, backup_dest, sizeof(backup_dest));
      }
   }
}

char * part_file_name(int part_no, char suffix)
{
static char filename[FNAMESIZ];

#ifndef NO_MKDIR
   if (suffix == 0) {
      sprintf(filename, "%s_uue.%.*s", section_dir,
	      (int)(sizeof(filename)-20-strlen(section_dir)),
	      section_name);
   } else if (suffix != 'l') {
      sprintf(filename, "%s_uue.%.*s/%04d-%04d.%c", section_dir,
	      (int)(sizeof(filename)-20-strlen(section_dir)),
	      section_name, part_no, section_parts, suffix);
#else
   if (suffix != 'l') {
      sprintf(filename, "%s_uue.%.*s.%04d-%04d.%c", section_dir,
	      (int)(sizeof(filename)-20-strlen(section_dir)),
	      section_name, part_no, section_parts, suffix);
#endif
   } else {
      sprintf(filename, "%s_uue.%.*s.%c", section_dir,
	      (int)(sizeof(filename)-20-strlen(section_dir)),
	      section_name, suffix);
   }
   return filename;
}

void save_temp_file(int suffix, int save_name)
{
   int lfd = -1, i;

   if(corruption) {
      if (section_parts)
	 fprintf(stderr, "uudecode: corrupt section: (%d/%d) %s\n",
	               section_partno, section_parts, section_name);
      else
	 fprintf(stderr, "uudecode: corrupt section: (%d/?) %s\n",
	               section_partno, section_name);
      corruption = 0;

      /* Do NOT save corrupt sections! */
      exit_status |= 2;
      return;
   }

   for(i=1; i<10; i++) {
      lfd = open(part_file_name(section_partno, 'l'), O_CREAT|O_EXCL|O_RDWR, 0666);
      if (lfd >=0) break;
      if (verbose>0)
	 fprintf(stderr, "uudecode: waiting for lock: (%d/%d) %s\n",
			  section_partno, section_parts, section_name);
      sleep(i);
   }

   if (lfd>=0) close(lfd);
   else {
      fprintf(stderr, "uudecode: LOCK FAILED: %s\n",
		       part_file_name(section_partno, 'l'));
      return;
   }

   if (verbose>0 && section_parts)
      fprintf(stderr, "uudecode: saving section: (%d/%d) %s\n",
	               section_partno, section_parts, section_name);
   if (verbose>0 && !section_parts)
      fprintf(stderr, "uudecode: saving section: (%d/?) %s\n",
	               section_partno, section_name);

   if (temp_open) {
      fclose(temp_file);
      temp_open = 0;
#ifndef NO_MKDIR
      mkdir(part_file_name(section_partno, 0), 0700);
#endif
      move_file(temp_name, part_file_name(section_partno, suffix), 1, 0);

      if (save_name) {
	 FILE * fd;
	 fd = fopen(part_file_name(section_partno, 'n'), "w");
	 fprintf(fd, "%s", dest);
	 fclose(fd);
      }
   }

   recover_temp_file();

   unlink(part_file_name(section_partno, 'l'));
}

void recover_temp_file(void)
{
   /* 
    * Look for a set of files to generate.
    */

   int starter, scanner;
   FILE * fd;
   char * filename;

   for (starter=section_partno; starter>=0; starter--) {
      if (access(part_file_name(starter, 's'), F_OK) == 0)
	 break;
   }

   if (starter<=0) return;	/* No starter found */

   for (scanner=starter+1; ;scanner++) {
      if (access(part_file_name(scanner, 'e'), F_OK) == 0)
	 break;

      if (access(part_file_name(scanner, 's'), F_OK) == 0)
	 return;

      if (access(part_file_name(scanner, 'd'), F_OK) != 0)
	 return;		/* Missing body part */
   }

   /* Got here ? Found a sequence starter..scanner */

   if (verbose>0)
      fprintf(stderr, "uudecode: joining sections (%d..%d) %s\n",
	               starter, scanner, section_name);

   filename = part_file_name(starter, 'n');

   fd = fopen(filename, "r");
   fgets(dest, sizeof(dest), fd);
   fclose(fd);
   unlink(filename);

   open_temp_file();

   filename = part_file_name(starter, 's');
   copy_file(temp_file, filename);
   unlink(filename);

   for (scanner=starter+1; ;scanner++) {
      filename = part_file_name(scanner, 'e');
      if (access(filename, F_OK) == 0) {
	 copy_file(temp_file, filename);
	 unlink(filename);
	 break;
      }

      filename = part_file_name(scanner, 'd');
      copy_file(temp_file, filename);
      unlink(filename);
   }

   name_temp_file(dest, 0644);
#ifndef NO_MKDIR
   rmdir(part_file_name(0, 0));
#endif
}

void copy_file(FILE * ofd, char * filename)
{
   FILE * fd;
   char buf[16384];
   int cc;

   fd = fopen(filename, "r");
   if (fd == 0) {
      fprintf(stderr, "uudecode: Unable to reopen temporary file '%s'\n",
	              filename);
   } else {
      while((cc=fread(buf, 1, sizeof(buf), fd)) > 0) {
	 if (fwrite(buf, 1, cc, ofd) != cc) {
	    fprintf(stderr, "uudecode: Unable write to file!\n");
	    exit(99);
	 }
      }
      fclose(fd);
   }
}

void move_file(char * oldname, char * newname, int delete, int dispname)
{
   FILE *ifd, *ofd;
   char buf[16384];
   char nbuf[FNAMESIZ];
   char * p;
   int cc, i, fd = -1;

   /* If delete remove files we can't overwrite. */
   if (delete) (void)unlink(newname);
   /* Use open to check for access so it's atomic. */
   if ((fd = open(newname, O_CREAT|O_EXCL|O_RDWR, 0666)) <0) {
      for(i=1; ; i++) {
	 strcpy(nbuf, newname);
	 p = strrchr(nbuf, '.');
	 if (p==0)
	    sprintf(nbuf, "%s.%d", newname, i);
	 else {
	    *p = 0;
	    p = strrchr(newname, '.');
	    sprintf(nbuf+strlen(nbuf), ".~%d%s", i, p);
	 }
         if ((fd = open(nbuf, O_CREAT|O_EXCL|O_RDWR, 0666)) >=0) {
	    newname = nbuf;
	    break;
	 }
      }
   }
   if (fd>=0) close(fd);

   if(dispname)
      fprintf(stderr, "uudecode: decoded file: %s\n", newname);

   if (rename(oldname, newname) >= 0) return;

   ifd = fopen(oldname, "r");
   if (ifd == 0) {
      fprintf(stderr, "uudecode: Unable to reopen file %s\n", oldname);
      unlink(oldname);
      exit_status |= 4;
      return;
   }

   ofd = fopen(newname, "w");
   if (ofd == 0) {
      fprintf(stderr, "uudecode: Unable to create file %s\n", newname);
      fclose(ifd);
      unlink(oldname);
      exit_status |= 2;
      return;
   }

   while((cc=fread(buf, 1, sizeof(buf), ifd)) > 0) {
      if (fwrite(buf, 1, cc, ofd) != cc) {
         fprintf(stderr, "uudecode: Error writing to %s\n", newname);
	 unlink(newname);
	 exit_status |= 4;
      }
   }
   fclose(ofd);
   fclose(ifd);
   unlink(oldname);
}

/*
 * Decode or identify a single line. If any data is found it's put into
 * the cv_buf array.
 *
 * This function uses data_type to ensure that data decoded this time
 * uses the same encoding scheme as the previous line.
 */
int identify_line(char * buf, int data_type)
{
   int len = 0, i;
   char lbuf[90];
   int llen = 0;

   if (decode_ancient_style)
      return decode_uue_line(buf, data_type);

   /* Note: the fast uudecoder doesn't need the line length so we don't have
    * to do the strlen()
    */
   if (data_type != LINE_UUDATA) {
      /* Remove EOL marker - any style. */
      for(len=strlen(buf); len>0 && (buf[len-1]=='\r' || buf[len-1]=='\n') ;) 
	 buf[--len] = 0;
   }

   /* Empty lines are ignored. */
   if (!*buf) return LINE_EMPTY;

   switch(data_type) {
   default:
      data_type = LINE_DATA;
   case LINE_UUDATA:

      /*
       * Might it be data ? This _should_be_ the most common line type so
       * we need to make it run as fast as we can. 
       *
       * First try a fast UUdecode. This only allows perfectly correct data
       * from a modern uuencoder.
       */

      if  (buf[0] > ' ' && buf[0] <= '`') {
	 int  i, ok = 1;
	 char * cv_ptr = cv_buf;
	 register unsigned char * p=buf;

	 cv_len = DEC(*p++);

	 /* Actually decode the uue data; ensure characters are in range. */
	 if (ok) for (i=0; i<cv_len; i+=3, p+=4) {
	    if ( (p[0]<=' ' || p[0]>'`') ||
		 (p[1]<=' ' || p[1]>'`') ||
		 (p[2]<=' ' || p[2]>'`') ||
		 (p[3]<=' ' || p[3]>'`') ) {
	       ok = 0;
	       break;
	    }
	    *cv_ptr++ = DEC(*p) << 2 | DEC(p[1]) >> 4;
	    *cv_ptr++ = DEC(p[1]) << 4 | DEC(p[2]) >> 2;
	    *cv_ptr++ = DEC(p[2]) << 6 | DEC(p[3]);
	 }
	 if (*p != '\r' && *p != '\n' && *p != '\0')
	       ok=0;
	 else  *p=0;

	 if (ok) return cv_len ? LINE_UUDATA : LINE_EOD;
      }

      if (data_type == LINE_UUDATA) {
	 /* Remove EOL marker - any style. */
         for(len=strlen(buf); len>0 && (buf[len-1]=='\r'||buf[len-1]=='\n') ;) 
	    buf[--len] = 0;
	 /* Empty lines are ignored. */
	 if (!*buf) return LINE_EMPTY;
      }

      /* A more normal uudecoder, it's stricter than most but allows some
       * varients. */
      if  (len<89 && buf[0] >= ' ' && buf[0] <= '`') {
	 int  expected, ok = 1;
	 char * cv_ptr = cv_buf;
	 register unsigned char * p=buf+1;

	 cv_len = DEC(buf[0]);

	 /* The expected length may be either enough for all the byte triples
	  * or just enough characters for all the bits.
	  */
	 expected = (((cv_len + 2) / 3) << 2) + 1;
	 if (ok && len != expected) {

	    /* If the line is one byte too long try for a checksum */
	    if (len == expected + 1 || len == (cv_len*8 + 5)/6 + 2) {
	       int csum = 0;
	       for(i=1; i<len-1; i++) csum += DEC(buf[i]);
	       if ((csum & 077) != DEC(buf[len-1])) ok = 0;
	       else len--;
	    }
	    /* Is the line the precise length needed for the bits ? */
	    else if (len != (cv_len*8 + 5)/6 + 1)
	       ok = 0;

	    if (ok) {
	       /* Pad to where we originally expected the data. */
	       strcpy(lbuf, buf); p = lbuf + 1;
	       llen = len;
	       while (llen < expected) lbuf[llen++] = '`'; lbuf[llen] = 0;
	    }
	 }

	 /* Actually decode the uue data; ensure characters are in range. */
	 if (ok) for (i=1; i<expected; i+=4, p+=4) {
	    if ( (p[0]<' ' || p[0]>'`') ||
		 (p[1]<' ' || p[1]>'`') ||
		 (p[2]<' ' || p[2]>'`') ||
		 (p[3]<' ' || p[3]>'`') ) {
	       ok = 0;
	       break;
	    }
	    *cv_ptr++ = DEC(*p) << 2 | DEC(p[1]) >> 4;
	    *cv_ptr++ = DEC(p[1]) << 4 | DEC(p[2]) >> 2;
	    *cv_ptr++ = DEC(p[2]) << 6 | DEC(p[3]);
	 }
	 if (ok) return cv_len ? LINE_UUDATA : LINE_EOD;
      }

      if (data_type != LINE_DATA) break;
   case LINE_MMDATA:
   case LINE_MBEGIN:

      /*
       * GNU style base64 encoding.
       *
       * Please note this doesn't strictly follow the POSIX standard about
       * embedded non-base64 characters. Rather than just throwing away the
       * bad characters it drops the whole line; the hope is to add a bit of
       * error detection and allow for multi-part instead.
       */
      if (len<89 && base64_dec[(unsigned char)buf[0]] != '\177') {
	 int expected, ok = 1;
	 char * cv_ptr = cv_buf;

	 expected = (((len+3)/4) << 2);
	 if (len != expected) ok = 0;

	 if (ok) {
	    register unsigned char * p=buf;
	    for (i=0; i<expected; i+=4) {
	       p=buf+i;
	       if (base64_dec[p[0]] == '\177' ||
		   base64_dec[p[1]] == '\177' ||
		   base64_dec[p[2]] == '\177' ||
		   base64_dec[p[3]] == '\177' ) {
		  ok = 0;
		  break;
	       }
	       *cv_ptr++ = base64_dec[p[0]] << 2 | base64_dec[p[1]] >> 4;
	       *cv_ptr++ = base64_dec[p[1]] << 4 | base64_dec[p[2]] >> 2;
	       *cv_ptr++ = base64_dec[p[2]] << 6 | base64_dec[p[3]];
	    }

	    /* Adjust for end of file */
	    if (p[0] == '=') {
	       cv_ptr -= 3;
	       if (cv_ptr != cv_buf) ok = 0;
	    }
	    else if (p[1] == '=') cv_ptr -= 3;
	    else if (p[2] == '=') cv_ptr -= 2;
	    else if (p[3] == '=') cv_ptr -= 1;

	    cv_len = cv_ptr - cv_buf;
	 }

	 if (ok) return cv_len?LINE_MMDATA:LINE_END;
      }

      if (data_type != LINE_DATA) break;
   case LINE_XXDATA:

      /* This is xxencoding ... */
      if (len<89 && xxencode_dec[(unsigned char)buf[0]] != '\177') {
	 int  expected, ok = 1;
	 char * cv_ptr = cv_buf;
	 register unsigned char * p=buf+1;

	 cv_len = xxencode_dec[(unsigned char)buf[0]];

	 /* The expected length may be either enough for all the byte triples
	  * or just enough characters for all the bits. We don't do checksums.
	  */

	 expected = (((cv_len + 2) / 3) << 2) + 1;
	 if (len != expected) {
	    /* Is the line the precise length needed for the bits ? */
	    if (len == (cv_len*8 + 5)/6 + 1) {
	       /* Pad to where we originally expected the data. */
	       strcpy(lbuf, buf); p = lbuf + 1;
	       llen = len;
	       while (llen < expected) lbuf[llen++] = '+'; lbuf[llen] = 0;
	    } else
	       ok = 0;
	 }

	 /* Actually decode the data; ensure characters are in range. */
	 if (ok) for (i=1; i<expected; i+=4, p+=4) {
	    if (xxencode_dec[p[0]] == '\177' ||
		xxencode_dec[p[1]] == '\177' ||
		xxencode_dec[p[2]] == '\177' ||
		xxencode_dec[p[3]] == '\177' ) {
	       ok = 0;
	       break;
	    }
	    *cv_ptr++ = xxencode_dec[p[0]] << 2 | xxencode_dec[p[1]] >> 4;
	    *cv_ptr++ = xxencode_dec[p[1]] << 4 | xxencode_dec[p[2]] >> 2;
	    *cv_ptr++ = xxencode_dec[p[2]] << 6 | xxencode_dec[p[3]];
	 }

	 if (ok) return LINE_XXDATA;
      }
      break;

      if (data_type != LINE_DATA) break;
   case LINE_CDATA:

      /* This is some custom decoding ... */
      if (enable_custom && 
	    len<89 && custom_dec[(unsigned char)buf[0]] != '\177') {
	 int  expected, ok = 1;
	 char * cv_ptr = cv_buf;
	 register unsigned char * p=buf+1;

	 cv_len = custom_dec[(unsigned char)buf[0]];

	 /* The expected length may be either enough for all the byte triples
	  * or just enough characters for all the bits. We don't do checksums.
	  */

	 expected = (((cv_len + 2) / 3) << 2) + 1;
	 if (len != expected) {
	    /* Is the line the precise length needed for the bits ? */
	    if (len == (cv_len*8 + 5)/6 + 1) {
	       /* Pad to where we originally expected the data. */
	       strcpy(lbuf, buf); p = lbuf + 1;
	       llen = len;
	       while (llen < expected) lbuf[llen++] = custom_enc[0];
	       lbuf[llen] = 0;
	    } else
	       ok = 0;
	 }

	 /* Actually decode the data; ensure characters are in range. */
	 if (ok) for (i=1; i<expected; i+=4, p+=4) {
	    if (custom_dec[p[0]] == '\177' ||
		custom_dec[p[1]] == '\177' ||
		custom_dec[p[2]] == '\177' ||
		custom_dec[p[3]] == '\177' ) {
	       ok = 0;
	       break;
	    }
	    *cv_ptr++ = custom_dec[p[0]] << 2 | custom_dec[p[1]] >> 4;
	    *cv_ptr++ = custom_dec[p[1]] << 4 | custom_dec[p[2]] >> 2;
	    *cv_ptr++ = custom_dec[p[2]] << 6 | custom_dec[p[3]];
	 }

	 if (ok) return LINE_CDATA;
      }
      break;

      /* yEnc encoding ... yuk! */
   case LINE_YBEGIN:
   case LINE_YDATA:
      {
	 int ltype = yEnc_data(buf);
	 if (ltype>0) return ltype;
      }
   }

   /* Well it wasn't data so check for headers. */

   /* yEnc encoding ... double yuk! */
   if (strncmp(buf, "=ybegin ", 8) == 0 ) return yEnc_header(buf);

   /* uuencode or xxencode */
   if (strncmp(buf, "begin ", 6) == 0) {
      if (buf[6] >= '0' && buf[6] <= '7') return LINE_BEGIN;
   }
   if (strcmp(buf, "end") == 0) return LINE_END;
   if (strcmp(buf, "table") == 0) return LINE_TABLE;

   /* posix uuencode */
   if (strncmp(buf, "begin-base64 ", 13) == 0) return LINE_MBEGIN;

   /* Now check for multi-part processing */
   if (!decode_single) {
      if (strncasecmp(buf, "subject:", 8) == 0) {
	 strncpy(subject_saved, buf, sizeof(subject_saved));
	 subject_saved[sizeof(subject_saved)-1] = 0;
	 return LINE_SUBJECT;
      }

      /* Look for the word 'section' followed by a number. */
      for(i=0; buf[i] && isspace(buf[i]); i++);
      if (buf[i] == '[') i++;
      for(; buf[i] && isspace(buf[i]); i++);
      if (strncasecmp(buf+i, "section", 7) == 0) {
	 i+=7;
	 if (buf[i] == ':') i++;
	 for(; buf[i] && isspace(buf[i]); i++);
	 if (isdigit(buf[i]))
	    return LINE_SECTION;
      }

      if (strcmp(buf, ".") == 0 || strncmp(buf, "--", 2) == 0)
	 return LINE_EOF;
   }

   /* Check if this may be a mime message. */
   if (mime_message >= 0) {
      if (strncasecmp(buf, "MIME-Version:", 13) == 0) 
	 mime_message = 1;
   }
   if (mime_message > 0) {
      if (strncasecmp(buf, "Content-Type: multipart", 23) == 0)
	 mime_message = 2;
      if (strncasecmp(buf, "Content-Type: message/p", 23) == 0)
	 mime_message = 2;

      if (strncasecmp(buf, "Content-Type:", 13) == 0)
	 return LINE_CONTENT;
   }

   /* At this point we can check for damaged uue lines. */

   /* eg: has the line been 'quoted' */
   if ( !isalnum(*buf) ) {
static int recurse = 0;
      if (recurse) return LINE_JUNK;

      recurse++; i = identify_line(buf+1, data_type); recurse--;
      if (i != LINE_JUNK && i != LINE_SECTION) return i;
      if ( (i = strspn(buf+1, "<> \t")) > 0 ) {
         recurse++; i = identify_line(buf+i+1, data_type); recurse--;
         if (i != LINE_JUNK && i != LINE_SECTION) return i;
      }
   }

   return LINE_JUNK;
}

/* 
 * This decodes any line as a data line (like an ancient decoder would)
 * it may be required for broken uuencoders.
 *
 * If the line is the wrong length it will still complain about corruption
 * but unlike the normal decoder it will still classify the line as data.
 *
 * Unlike the original BSD decoder if the line is too short it assumes
 * that trailing spaces have been clipped.
 *
 */
int decode_uue_line(char * buf, int data_type)
{
   int len,i;
   register unsigned char * p;

   /* Remove EOL marker - any style. */
   for(len=strlen(buf); len>0 && (buf[len-1]=='\r' || buf[len-1]=='\n') ;) 
      buf[--len] = 0;

   if (data_type == LINE_DATA || data_type == LINE_BEGIN) {
      char * cv_ptr = cv_buf;
      int expected;

      cv_len = buf[0] ? DEC(buf[0]) :0;

      /* Check for stripped trailing spaces. */
      expected = (((cv_len + 2) / 3) << 2) + 1;

      /* Warn about corruption */
      if (len != expected) corruption = 1;

      /* Add any stripped spaces */
      for (; len < expected; ) buf[len++] = ' '; buf[len] = 0;

      for (i=0,p=buf+1; i<cv_len; i+=3, p+=4) {
	 *cv_ptr++ = DEC(*p) << 2 | DEC(p[1]) >> 4;
	 *cv_ptr++ = DEC(p[1]) << 4 | DEC(p[2]) >> 2;
	 *cv_ptr++ = DEC(p[2]) << 6 | DEC(p[3]);
      }
      return cv_len?LINE_DATA:LINE_EOD;
   }
   if (strncmp(buf, "begin ", 6) == 0) return LINE_BEGIN;
   if (strcmp(buf, "end") == 0) return LINE_END;

   return LINE_JUNK;
}

/*************************************************************************
 *
 * yEnc decoder, this is a nasty and fragile encoding that really needs
 * the crc. So we must warn if a chunk or file doesn't have one.
 *
 * There are also issues with filenames, embedded spaces, no character
 * set identifier and so forth.
 *
 * This decoder only uses part numbers not part offsets so you cannot
 * use it to try the (very dubious) technique of merging parts from
 * different posts.
 *
 * If the total number of parts isn't in the header lines this will
 * try and take it from the subject line.
 *
 * The line length limitation isn't an error because frequently this
 * will still be able to correctly decode a file even if when the 
 * lines are too long.
 *
 * The program doesn't (currently) check the overall crc on a multi
 * part decode.
 */

struct yenc {
   /* Begin line */
   int line;
   int size;
   int part;
   int total;

   /* Part line */
   int partial;
   int begin;
   int end;

   /* End line */
   int esize;
   int has_ecrc32;
   int has_pcrc32;
   unsigned long ecrc32;
   unsigned long pcrc32;

   /* Data lines */
   int count;
   unsigned long crc32;

   char section_name[FNAMESIZ];
} yEnc;

int yEnc_header(char * buf)
{
   char * ptr = buf; 

   cv_len = 0;
   if (strncmp(buf, "=ybegin ", 8) == 0)
   {
      yEnc.crc32 = -1;
      yEnc.count = 0;

      yEnc.line = -1;
      yEnc.size = -1;
      yEnc.part = 0;
      yEnc.total = 0;
      yEnc.partial = 0;

      for(ptr = buf+7;;)
      {
	 while( *ptr == ' ' ) ptr++;
	 if( *ptr == 0 ) return LINE_JUNK;	/* If no name this is junk */

	 if (strncmp(ptr, "line=", 5) == 0) 
	    yEnc.line = strtol(ptr+5, &ptr, 10);
	 else if (strncmp(ptr, "size=", 5) == 0) 
	    yEnc.size = strtol(ptr+5, &ptr, 10);
	 else if (strncmp(ptr, "part=", 5) == 0) 
	    yEnc.part = strtol(ptr+5, &ptr, 10);
	 else if (strncmp(ptr, "total=", 6) == 0) 
	    yEnc.total = strtol(ptr+6, &ptr, 10);
	 else if (strncmp(ptr, "name=", 5) == 0) 
	    break;
	 else
	    while( *ptr != ' ' && ptr != 0 )	/* Allow for new keywords */
	       ptr++;
      }

      if (yEnc.line == -1 || yEnc.size == -1)
	 return LINE_JUNK;

      /* Long lines work _IFF_ no quoted character gets split by a forced EOL */
      if (yEnc.line > LINELEN-10) {
	 fprintf(stderr, "uudecode: WARNING yEnc lines too long, this may cause corruption\n");
      }

      if (yEnc.part > 0) {
	 /* NOTE: This does not support parts that overlap! */
	 yEnc.partial = 1;

	 if (yEnc.total == 0 && section_partno == yEnc.part)
	    yEnc.total = section_parts;

	 convert_filename(ptr+5, yEnc.section_name, sizeof(yEnc.section_name));

	 yEnc.begin = 0;
	 yEnc.end = -1;
      } else {
	 yEnc.begin = 1;
	 yEnc.end = yEnc.size;
      }

      return LINE_YBEGIN;
   }
   else if (strncmp(buf, "=yend ", 6) == 0)
   {
      yEnc.esize = -1;
      yEnc.has_ecrc32 = 0;
      yEnc.has_pcrc32 = 0;

      section_parts = yEnc.total;
      section_partno = yEnc.part;
      strcpy(section_name, yEnc.section_name);

      for(ptr = buf+5;;)
      {
	 while( *ptr == ' ' ) ptr++;
	 if( *ptr == 0 ) break;

	 if (strncmp(ptr, "size=", 5) == 0) 
	    yEnc.esize = strtol(ptr+5, &ptr, 10);
	 else if (strncmp(ptr, "crc32=", 6) == 0) {
	    yEnc.ecrc32 = strtoul(ptr+6, &ptr, 16);
	    yEnc.has_ecrc32 = 1;
	 }
	 else if (strncmp(ptr, "pcrc32=", 7) == 0) {
	    yEnc.pcrc32 = strtoul(ptr+7, &ptr, 16);
	    yEnc.has_pcrc32 = 1;
	 } else
	    while( *ptr != ' ' && ptr != 0 )	/* Allow for new keywords */
	       ptr++;
      }

      if (yEnc.begin == 0) {
	 fprintf(stderr, "uudecode: yEnc missing =ypart line: %s\n", 
	                 current_file);
	 corruption = 1;
      }
      else if (yEnc.esize != yEnc.count) {
	 fprintf(stderr, "uudecode: yEnc incorrect section byte count: %s\n",
	                 current_file);
	 corruption = 1;
      }
      else if (yEnc.esize != yEnc.end - yEnc.begin + 1) {
	 fprintf(stderr, "uuencode: WARNING yEnc header size mismatch: %s\n",
	                 current_file);
      }

      if (yEnc.partial) {
	 if (yEnc.has_pcrc32 && (yEnc.crc32 ^ 0xFFFFFFFF) != yEnc.pcrc32) {
	    fprintf(stderr, "uudecode: yEnc data part crc failure: %s\n",
		            current_file);
	    corruption = 1;
	 }
	 if (!yEnc.has_pcrc32)
	    fprintf(stderr,"uudecode: WARNING: yEnc data part has no crc: %s\n",
		            current_file);

	 /* Single part multi-part files ... hmmm */
	 if (yEnc.begin == 1 && yEnc.end == yEnc.size && 
	       yEnc.has_ecrc32 && (yEnc.crc32 ^ 0xFFFFFFFF) != yEnc.ecrc32) {
	    fprintf(stderr, "uudecode: yEnc data crc failure: %s\n",
		            current_file);
	    corruption = 1;
	 }

	 if (yEnc.begin != 1) {
	    /* This is not a starter so we don't need the name. */
	    *dest = 0;
	 }
	 if (yEnc.end != yEnc.size) {
	    /* This is not an ending so just finish. */
	    return LINE_EOF;
	 }

      } else {
	 if (yEnc.has_ecrc32 && (yEnc.crc32 ^ 0xFFFFFFFF) != yEnc.ecrc32) {
	    if (yEnc.ecrc32 == 1) {
	       fprintf(stderr, "uudecode: WARNING yEnc data crc failure, but CRC is dubious: %s\n",
		               current_file);
	    } else {
	       fprintf(stderr, "uudecode: yEnc data crc failure: %s\n",
		               current_file);
	       corruption = 1;
	    }
	 }
	 if (!yEnc.has_ecrc32)
	    fprintf(stderr,"uudecode: WARNING: yEnc data has no crc: %s\n",
		            current_file);
      }

      return LINE_END;
   }
   else if (strncmp(buf, "=ypart ", 7) == 0) {
      yEnc.begin = 1;
      yEnc.end = 0;

      for(ptr = buf+6;;) {
	 while( *ptr == ' ' ) ptr++;
	 if( *ptr == 0 ) break;

	 if (strncmp(ptr, "begin=", 6) == 0) 
	    yEnc.begin = strtol(ptr+6, &ptr, 10);
	 else if (strncmp(ptr, "end=", 4) == 0) 
	    yEnc.end = strtol(ptr+4, &ptr, 10);
	 else
	    while( *ptr != ' ' && ptr != 0 )	/* Allow for new keywords */
	       ptr++;
      }

      *section_name = 0;	/* We do the multi-part stuff */

      return LINE_YDATA;
   }

   /* Unknown header lines must be skipped (zero length data line) */
   fprintf(stderr,"uudecode: WARNING: Unknown yEnc control line in %s\n",
		   current_file);
   return LINE_YDATA;
}

int yEnc_data(char * buf)
{
   int i;

   if (buf[0] == '=' && buf[1] == 'y') 
      return yEnc_header(buf);
   cv_len = 0;

   for(i=0; buf[i]; i++) {
      unsigned char ch;
      if (buf[i] == '=') {
	 if (buf[i+1]) i++; else corruption = 1;
	 ch = buf[i] - 64 - 42;
      }
      else
	 ch = buf[i] - 42;

      yEnc.crc32 = UPDC32(ch, yEnc.crc32);
      cv_buf[cv_len++] = ch;
   }
   yEnc.count += cv_len;

   return LINE_YDATA;
}


