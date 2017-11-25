#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>

static void usage(void);
static void do_file(FILE * in, FILE * out);

int force_skip = 0;

struct {
  unsigned char sync[12];
  unsigned char header[4];
  unsigned char subheader[8];
  unsigned char data[2324];
  unsigned char edc[4];
} sbuf;

int
main(int argc, char **argv)
{
   FILE *in = NULL, *out = NULL;

   if (argc>2 && strcmp(argv[1], "-s") == 0) {
      force_skip = atoi(argv[2]);
      argc -=2 ;
      argv +=2;
   }
   if (argc == 2 || argc == 3)
   {
      in = fopen(argv[1], "rb");
      if (!in)
      {
	 printf("fopen (): %s\n", strerror(errno));
	 exit(1);
      }
   }
   else
      usage();

   if (argc == 3)
   {
      out = fopen(argv[2], "wb");
      if (!out)
      {
	 printf("fopen (): %s\n", strerror(errno));
	 exit(1);
      }
   }

   do_file(in, out);

   if (in)
      fclose(in);

   if (out)
      fclose(out);

   return 0;

}

static void
usage(void)
{
   printf("usage: bin2mpg [-s sect_skip_count] infile [outfile]\n\n"
	  "description: \n"
	  "  Converts a Video CD bin file or a windows CDXA RIFF file\n"
	  "  to a plain mpeg data file.\n"
	  );

   exit(1);
}

void
do_file(FILE * in, FILE * out)
{
   int scount = 0;
   int dcount = 0;
   int last_err = 0;
   int err = 0, cc;

   fread(sbuf.sync, 1, sizeof(sbuf.sync), in);
   while (memcmp(sbuf.sync,
		 "\0\377\377\377\377\377\377\377\377\377\377\0", 12) != 0) 
   {
      if (err++ > 16000) {
	 printf("Cannot find start of data.\n");
	 return;
      }
      memcpy(sbuf.sync, sbuf.sync+1, 11);
      cc = fgetc(in);
      if (cc == EOF) {
	 printf("End of file searching for start of data\n");
	 return;
      }
      sbuf.sync[11] = cc;
   }

   if (err)
      printf("Skipped %d bytes\n", err);

   fread(sbuf.sync+sizeof(sbuf.sync), 1, sizeof(sbuf)-sizeof(sbuf.sync), in);

   do {
      if (memcmp(sbuf.sync, 
	         "\0\377\377\377\377\377\377\377\377\377\377\0", 12) != 0) {
	 printf("BAD Sync mark; aborting\n");
	 return;
      }
      err = 0;

      if (sbuf.header[3] == 1) err = 1;
      else if (sbuf.header[3] == 0) err = 2;
      else if (sbuf.header[3] != 2) err = 3;
      else if ((sbuf.subheader[2] & (1<<5)) == 0) err = 4;
      else if (sbuf.subheader[1] == 0) err = 5;
      else if (sbuf.subheader[3] == 0x1F) err = 6;

      if (err == 0 || err == 5 || err == 6) {
	 int i, empty=1;
	 for(i=0; empty && i<2324; i++) if (sbuf.data[i]) empty = 0;
	 if (empty) err = 7;
      }

      if (force_skip > 0) {
	 force_skip--;
	 err = 8;
      }

      if (last_err != err) {
	 if (scount) printf("%d sectors\n", scount); scount = 0;
	 last_err = err;
	 switch(err) {
	 case 0: printf("Found Mode 2/Form 2 DATA   : "); break;
	 case 1: printf("Found Mode 1               : "); break;
	 case 2: printf("Found Mode 0               : "); break;
	 case 3: printf("Found weird mode           : "); break;
	 case 4: printf("Found Mode 2/Form 1        : "); break;
	 case 5: printf("Found Mode 2/Form 2 CN_PAD : "); break;
	 case 6: printf("Found Mode 2/Form 2 CI_PAD : "); break;
	 case 7: printf("Found Mode 2/Form 2 Empty  : "); break;
	 case 8: printf("Ignored                    : "); break;
	 }
	 if (err == 0 || (dcount && (err == 5 || err == 6))) 
	    printf("keep ");
	 else
	    printf("skip ");
	 fflush(stdout);
      }
      scount++;

      /* Keep embedded padding sectors for timing purposes. */
      if (err == 0 || (dcount && (err == 5 || err == 6))) 
      {
	 if (out) if ( fwrite(sbuf.data, sizeof(sbuf.data), 1, out) != 1 ) {
	    printf("Error writing to output, abort.");
	    return;
	 }
	 dcount++;
      }
   }
   while( (cc = fread(&sbuf, 1, sizeof(sbuf), in)) == sizeof(sbuf) );

   if (scount) printf("%d sectors\n", scount); scount = 0;

   if (cc>0) printf("Incomplete sector at end of file, %d bytes\n", cc);

   if (!dcount) 
      printf("No data sectors found!\n");
   else if (out)
      printf("%d data sectors (%d bytes) written to output.\n",
	     dcount, dcount * 2324);
   else
      printf("%d data sectors (%d bytes), No output to write them to.\n",
	     dcount, dcount * 2324);
}


