/* 
 * This program reads unpacked data from an ASCII or EBCDIC tape.
 *
 * The program autodetects several formating options including IBM standard
 * labels, EBCDIC text tapes and IBM variable block size tapes.
 * The program will try to intuit the record size for EBCDIC tapes but the
 * information is not available in some formats so needs to be supplied on
 * the command line.
 *
 * $ read_tape [options] /dev/rmtX [output_file]
 *
 * Options:
 *   	-d	Debugging output.
 *	-c	Clip trailing spaces in records.
 *	-r	Define tape record size.
 *	-a	Use a generic EBCDIC->ISO-8859-1 character conversion.
 *	-h	Ascii hex dump of tape.
 *	-H	EBCDIC hex dump of tape.
 */

#include <stdio.h>
#include <fcntl.h>

char blockbuf[65537];
int  block_len = -1;
int  block_number = 0;

int  tape_fd = -1;
int  tapemarks = 2;

int  user_record_size = 0;  /* -1 => none, >0 Fixed, 0= unspecified */
int  is_ebcdic  = 0;
int  use_iso    = 0;
int  ibm_volume = 0;
int  reading_header = 0;

int  file_record_size  = -1;
int  block_record_size = -1;

char * tapename = 0;
int  debug = 0;

int record_offset = 0;
int pending_spaces = 0;
int trim_spaces = 0;

int hexdump = 0;

int exit_code = 0;

FILE * ofd = stdout;

extern char to_iso8859_1[];
extern char to_ascii[];

main(argc, argv)
int argc;
char ** argv;
{
   int ar;
   int done_file = 0;

   for(ar=1; ar<argc; ar++)
   {
      if( argv[ar][0] == '-' ) switch(argv[ar][1])
      {
      case 'd': debug = 1; break;
      case 'r': user_record_size = atoi(argv[ar]+2);
		if( user_record_size <= 0 )
		   user_record_size= -1;
		break;
      case 'a': use_iso = 1; break;
      case 'c': trim_spaces = 1; break;

      case 'h': hexdump=1; break;
      case 'H': hexdump=2; break;

      default:  Usage();
      }
      else
      {
	 tapename = argv[ar];

	 if( argv[ar+1] )
	 {
	    ofd = fopen(argv[ar+1], "w");
	    if( ofd == 0 )
            { perror("Cannot open output file."); exit(1); }
         }

	 do_tape();
	 done_file = 1;
	 break;
      }
   }

   if( !done_file ) Usage();

   if( ofd != stdout ) fclose(ofd);

   exit(exit_code);
}

Usage()
{
   fprintf(stderr, "Usage: read_tape [options] /dev/rmtX [output_file]\n");
   fprintf(stderr, " Options:\n");
   fprintf(stderr, "   	-d	Debugging output.\n");
   fprintf(stderr, "	-c	Clip trailing spaces in records.\n");
   fprintf(stderr, "	-r	Define tape record size.\n");
   fprintf(stderr, "	-a	Use a generic EBCDIC->ISO-8859-1 character conversion.\n");
   fprintf(stderr, "	-h	Ascii hex dump of tape.\n");
   fprintf(stderr, "	-H	EBCDIC hex dump of tape.\n");

   exit(99);
}

do_tape()
{
   block_number = 0;

   open_tape();

   while( read_block() > 0 )
   {
      if( hexdump )
         dump_block();
      else
      {
         if( block_number == 0 ) auto_detect();
         if( block_len > 0 )     write_block();
      }
      block_number++;
   }
   if( hexdump )
   {
      hex_output(EOF);
      fprintf(ofd, "** TAPEMARK\n");
   }

   close_tape();
}

open_tape()
{
   tape_fd = open(tapename, 0);

   if(tape_fd<0) { perror("Cannot open tape"); exit(1); }

   tapemarks = 0;
}

read_block()
{
   int cc, eof=0;

   if( tapemarks >= 2 )
      return (block_len = 0);

   cc = read(tape_fd, blockbuf, sizeof(blockbuf));

   if( cc == sizeof(blockbuf) )
   {
      fprintf(stderr, "ERROR: Block size too large, must use raw tape device");
      tapemarks = 3;
      return (block_len = 0);
   }

   if( cc <= 0 ) tapemarks++; else tapemarks=0;

   if( cc == 0 && !reading_header )
   {
      if( debug )
	 fprintf(stderr, "TAPE: Tapemark found.\n");

      end_of_tapefile();
      if( tapemarks >= 2 )
         return (block_len = 0);

      cc = read(tape_fd, blockbuf, sizeof(blockbuf));

      if( cc <= 0 ) tapemarks++; else tapemarks=0;
   }
      
   if( cc < 0 )
   {
      perror("Tape read error");
      exit(1);
   }

   if( debug )
   {
      if( cc == 0 )
         fprintf(stderr, "TAPE: Tapemark found.\n");
      else
         fprintf(stderr, "TAPE: Block of %d bytes found.\n", cc);
   }

   return (block_len = cc);
}

close_tape()
{
   close(tape_fd);
}

auto_detect()
{
   int i;

   /* Huristic to determine if block is in ebcdic */
   char * ptr = blockbuf;
   int len    = block_len;

   if( len < 10 )  is_ebcdic = 0;
   else
   {
      int err = 0;

      if( len > 256 ) len=256;

      for(i=0; i<len; i++)
      {
         int ch = to_ascii[ptr[i]];
   
         if( !isascii(ch) || ( !isprint(ch) && !isspace(ch) ) )
            err++;
      }
   
      if( err * 20 <= len )
      {
	 is_ebcdic = 1;
         fprintf(stderr, "EBCDIC tape detected.\n");
      }
   }

   if( !is_ebcdic )
      fprintf(stderr, "Tape appears to be ASCII or Binary data.\n");

   /* Could be an IBM labeled tape ? */
   if( is_ebcdic && block_len == 80 )
   {
      char lbuf[81];
      for(i=0; i<80; i++) lbuf[i] = to_ascii[blockbuf[i]];
      lbuf[80] = 0;
      for(i=79; i>1 && lbuf[i] == ' '; --i) lbuf[i] = 0;

      if( memcmp(lbuf, "VOL1", 4) == 0 )
      {
	 fprintf(stderr, "Tape has ibm volume header.\n");
	 fprintf(stderr, "%s\n", lbuf);

	 ibm_volume = 1;

	 read_ibmheader(lbuf);
	 return;
      }
   }
   if( is_ebcdic && block_len > 80 )
   {
      len = (blockbuf[0] & 0xFF)*256 + (blockbuf[1] & 0xFF);

      if( len == block_len )
      {
	 file_record_size = 0;
         fprintf(stderr, "Tape appears to be in IBM variable record format.\n");
      }
   }
}

read_ibmheader(lbuf)
char * lbuf;
{
   int cc,i;
   int got_utl2 = 0;

   char nbuf[10];

   reading_header = 1;
   while( (cc = read_block()) != 0 )
   {
      if( cc != 80 )
      {
         fprintf(stderr, "ERROR: IBM header has illegal block length!");
         reading_header = 0;
	 exit_code++;
         return;
      }

      for(i=0; i<80; i++) lbuf[i] = to_ascii[blockbuf[i]];
      lbuf[80] = 0;
      for(i=79; i>1 && lbuf[i] == ' '; --i) lbuf[i] = 0;

      fprintf(stderr, "%s\n", lbuf);

      if( memcmp(lbuf, "HDR", 3) == 0 )
      {
	 if( lbuf[3] == '2' )
	 {
	    got_utl2 = 1;

            fprintf(stderr, "Tape volume header has a");
	    switch( lbuf[4] )
	    {
	    case 'F': memcpy(nbuf, lbuf+10, 5);
	              nbuf[5] = 0;
	              file_record_size = atoi(nbuf);
                      fprintf(stderr, " record size of %d bytes.\n",
			      file_record_size);
		      break;
	    case 'V': file_record_size = 0;
                      fprintf(stderr, " variable record size.\n");
		      break;
	    default:  file_record_size = -1;
                      fprintf(stderr, "n unknown record size.\n");
		      break;
	    }
	 }
      }
      else if( memcmp(lbuf, "UHL", 3) != 0 )
      {
         fprintf(stderr, "ERROR: IBM header has unknown block type!");
         reading_header = 0;
	 exit_code++;
         return;
      }
   }

   if( !got_utl2 )
   {
      fprintf(stderr, "ERROR: IBM header doesn't have a UTL2 header!");
      exit_code++;
   }

   reading_header = 0;
   block_len = 0;
}

end_of_tapefile() {

   if( !ibm_volume )
   {
      if( hexdump )
      {
	 hex_output(EOF);
	 fprintf(ofd, "** TAPEMARK\n");
	 return;
      }
      fprintf(stderr, "End of file marker found.\n");
      block_number = 0;
      return;
   }

   read_ibmfooter();
}

read_ibmfooter()
{
   char lbuf[80];
   int cc, i;

   reading_header = 1;
   while( (cc = read_block()) != 0 )
   {
      if( cc != 80 )
      {
         fprintf(stderr, "ERROR: IBM footer has illegal block length!");
         reading_header = 0;
	 exit_code++;
         break;
      }

      for(i=0; i<80; i++) lbuf[i] = to_ascii[blockbuf[i]];
      lbuf[80] = 0;
      for(i=79; i>1 && lbuf[i] == ' '; --i) lbuf[i] = 0;

      fprintf(stderr, "%s\n", lbuf);

      if( memcmp(lbuf, "EOF", 3) != 0 && memcmp(lbuf, "UTL", 3) != 0 )
      {
         fprintf(stderr, "ERROR: IBM footer has unsupported block type!");
	 exit_code++;
	 tapemarks = 3;
      }
   }

   if( debug ) fprintf(stderr, "End of IBM volume dataset\n");

   block_len = 0;
   reading_header = 0;
   block_number = 0;
}

reset_output()
{
   if( record_offset != 0)
      if( putc('\n', ofd) == EOF )
      {
	 perror("ERROR writing to output file");
	 exit(1);
      }
   pending_spaces = record_offset = 0;
}

write_block()
{
   int i;

   if( block_number == 0 ) reset_output();

   if( user_record_size != 0 )
      block_record_size = user_record_size;
   else
      block_record_size = file_record_size;

   if( is_ebcdic && block_record_size == 0 )
   {
      if( block_number == 0 )
         fprintf(stderr, "Writing variable blocks.\n");
      write_varblock();
   }
   else
   {
      if( block_number == 0 )
      {
	 if( block_record_size > 0 )
	    fprintf(stderr, "Writing record of %d bytes.\n", block_record_size);
	 else if( !is_ebcdic )
	    fprintf(stderr, "Writing binary data.\n");
	 else
	    fprintf(stderr, "Writing record of %d bytes.\n", block_len);
      }
      for(i=0; i<block_len; i++)
         write_char(blockbuf[i]);
   }

   if( block_record_size == -1 && is_ebcdic && record_offset )
   {
      pending_spaces = record_offset = 0;
      if( putc('\n', ofd) == EOF )
      {
	 perror("ERROR writing to output file");
	 exit(1);
      }
   }
}

write_char(ch)
int ch;
{
   if( is_ebcdic &&  use_iso ) ch = to_iso8859_1[ch & 0xFF];
   if( is_ebcdic && !use_iso ) ch = to_ascii[ch & 0xFF];

   if( trim_spaces && ch == ' ' )
      pending_spaces++;
   else
   {
      while(pending_spaces>0)
      {
         if( putc(' ', ofd) == EOF )
         {
	    perror("ERROR writing to output file");
	    exit(1);
         }
	 pending_spaces--;
      }
      if( putc(ch, ofd) == EOF )
      {
	 perror("ERROR writing to output file");
	 exit(1);
      }
   }

   record_offset++;
   if( record_offset >= block_record_size && block_record_size > 0 )
      reset_output();
}

write_varblock()
{
   int len;
   int i;
   int off=4;

   if( block_len < 4 ) return ;

   len = (blockbuf[0] & 0xFF)*256 + (blockbuf[1] & 0xFF);

   if( len != block_len )
   {
      fprintf(stderr, "ERROR: Block %d is incorrect length!\n", block_len);
      exit_code = 1;
   }

   while(off < block_len)
   {
      len = (blockbuf[off] & 0xFF)*256 + (blockbuf[off+1] & 0xFF);

      if( debug )
	 fprintf(stderr, "VARR: Record length %d\n", len);

      for(i=4; i<len && off+i < block_len; i++)
	 write_char(blockbuf[off+i]);

      reset_output();
      off +=len;
   }
   if( off != block_len )
   {
      fprintf(stderr, "ERROR: record offset wrong! (%d,%d)\n", off, block_len);
      exit_code = 1;
   }
}

/* This is Unix 'dd' conv=ascii translation */
char to_ascii[] = {
0x00,0x01,0x02,0x03,0x9c,0x09,0x86,0x7f,0x97,0x8d,0x8e,0x0b,0x0c,0x0d,0x0e,0x0f,
0x10,0x11,0x12,0x13,0x9d,0x85,0x08,0x87,0x18,0x19,0x92,0x8f,0x1c,0x1d,0x1e,0x1f,
0x80,0x81,0x82,0x83,0x84,0x0a,0x17,0x1b,0x88,0x89,0x8a,0x8b,0x8c,0x05,0x06,0x07,
0x90,0x91,0x16,0x93,0x94,0x95,0x96,0x04,0x98,0x99,0x9a,0x9b,0x14,0x15,0x9e,0x1a,
 ' ',0xa0,0xa1,0xa2,0xa3,0xa4,0xa5,0xa6,0xa7,0xa8,0xd5, '.', '<', '(', '+', '|',
 '&',0xa9,0xaa,0xab,0xac,0xad,0xae,0xaf,0xb0,0xb1, '!', '$', '*', ')', ';', '~',
 '-', '/',0xb2,0xb3,0xb4,0xb5,0xb6,0xb7,0xb8,0xb9,0xcb, ',', '%', '_', '>', '?',
0xba,0xbb,0xbc,0xbd,0xbe,0xbf,0xc0,0xc1,0xc2, '`', ':', '#', '@',0x27, '=', '"',
0xc3, 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i',0xc4,0xc5,0xc6,0xc7,0xc8,0xc9,
0xca, 'j', 'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', '^',0xcc,0xcd,0xce,0xcf,0xd0,
0xd1,0xe5, 's', 't', 'u', 'v', 'w', 'x', 'y', 'z',0xd2,0xd3,0xd4, '[',0xd6,0xd7,
0xd8,0xd9,0xda,0xdb,0xdc,0xdd,0xde,0xdf,0xe0,0xe1,0xe2,0xe3,0xe4, ']',0xe6,0xe7,
 '{', 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I',0xe8,0xe9,0xea,0xeb,0xec,0xed,
 '}', 'J', 'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R',0xee,0xef,0xf0,0xf1,0xf2,0xf3,
0x5c,0x9f, 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z',0xf4,0xf5,0xf6,0xf7,0xf8,0xf9,
 '0', '1', '2', '3', '4', '5', '6', '7', '8', '9',0xfa,0xfb,0xfc,0xfd,0xfe,0xff
};

/* This translation a best guess of generic EBCDIC mappings to iso8859-1 */
char to_iso8859_1[] = {
0x00,0x01,0x02,0x03,0x9c,0x09,0x86,0x7f,0x97,0x8d,0x8e,0x0b,0x0c,0x0d,0x0e,0x0f,
0x10,0x11,0x12,0x13,0x9d,0x85,0x08,0x87,0x18,0x19,0x92,0x8f,0x1c,0x1d,0x1e,0x1f,
0x80,0x81,0x82,0x83,0x84,0x0a,0x17,0x1b,0x88,0x89,0x8a,0x8b,0x8c,0x05,0x06,0x07,
0x90,0x91,0x16,0x93,0x94,0x95,0x96,0x04,0x98,0x99,0x9a,0x9b,0x14,0x15,0x9e,0x1a,
' ', 0xa0,0xe2,0xe4,0xe0,0xe1,0xe3,0xe5,0xe7,0xf1,'[', '.', '<', '(', '+', '!', 
'&', 0xe9,0xea,0xeb,0xe8,0xed,0xee,0xef,0xec,0xdf,']', '$', '*', ')', ';', '^', 
'-', '/', 0xc2,0xc4,0xc0,0xc1,0xc3,0xc5,0xc7,0xd1,0xa6,',', '%', '_', '>', '?', 
0xf8,0xc9,0xca,0xcb,0xc8,0xcd,0xce,0xcf,0xcc,'`', ':', '#', '@', 0x27,'=', '"', 
0xd8,'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 0xab,0xbb,0xf0,0xfd,0xfe,0xb1,
0xb0,'j', 'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 0xaa,0xba,0xe6,0xb8,0xc6,0xa4,
0xb5,'~', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z', 0xa1,0xbf,0xd0,0xdd,0xde,0xae,
0xa2,0xa3,0xa5,0xb7,0xa9,0xa7,0xb6,0xbc,0xbd,0xbe,0xac,'|', 0xaf,0xa8,0xb4,0xd7,
'{', 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 0xad,0xf4,0xf6,0xf2,0xf3,0xf5,
'}', 'J', 'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 0xb9,0xfb,0xfc,0xf9,0xfa,0xff,
0x5c,0xf7,'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z', 0xb2,0xd4,0xd6,0xd2,0xd3,0xd5,
'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 0xb3,0xdb,0xdc,0xd9,0xda,0x9f
};


dump_block()
{
   int i;

   for(i=0; i<block_len; i++)
      hex_output(blockbuf[i] & 0xFF);

   hex_output(EOF);
   fflush(ofd);
   putc('\n', ofd);

   if( ferror(ofd) )
   {
      perror("ERROR writing to output file");
      exit(1);
   }
}

hex_output(ch)
{
static char linebuf[80];
static char buf[20];
static int pos = 0;

   if( ch == EOF )
   {
      if(pos)
	 fprintf(ofd, ": %.66s\n", linebuf);
      pos = 0;
   }
   else
   {
      if(!pos)
         memset(linebuf, ' ', sizeof(linebuf));
      sprintf(buf, "%02x", ch&0xFF);
      memcpy(linebuf+pos*3+(pos>7), buf, 2);

      if( hexdump == 2 )
      {
	 if( use_iso ) ch = to_iso8859_1[ch];
	 else          ch = to_ascii[ch];
      }
      if( ( ch > ' ' && ch <= '~' ) || (use_iso && ch > 160) )
	    linebuf[50+pos] = ch;
      else  linebuf[50+pos] = '.';
      pos = ((pos+1) & 0xF);
      if( pos == 0 )
         fprintf(ofd, ": %.66s\n", linebuf);
   }
}
