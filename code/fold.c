#include <stdio.h>

char buf[500];

FILE * ifd;
int refold = 0;

#define MAXCOL 256
int cols = 76;
int indent = 0;

main(argc, argv)
int argc;
char ** argv;
{
   int ar;
   int count = 0;
   ifd = stdin;

   for(ar=1; ar<argc; ar++) if( argv[ar][0] == '-' )
   switch(argv[ar][1])
   {
   case 'r': refold = 1; break;
   case 'w': cols= atoi(argv[ar]+2); 
	     if( cols > MAXCOL ) cols = MAXCOL;
	     break;
   case 'i': indent= atoi(argv[ar]+2); 
	     break;
   default:  fprintf(stderr, "Unknown flag %s\n", argv[ar]);
	     fprintf(stderr, "Fold -refold -w99\n");
	     break;
   }
   else
   {
      count++;
      if( indent >= cols ) indent = 0;
      ifd = fopen(argv[ar], "r");
      if( ifd == 0 )
      {
	 fprintf(stderr, "Cannot open %s\n", argv[ar]);
      }
      else
      {
	 do_file();
	 fclose(ifd);
      }
   }
   if( count == 0 ) do_file();
}

do_file()
{
   int cr=0, nl=0;
   int prev = ' ';
   int indented = 0, previndent = 0;

   int ch, i;
   int ccount=0;

   while((ch = fgetc(ifd)) != EOF)
   {
      if( ch == '\r' ) { cr++; continue; }
      if( ch == '\n' ) { nl++; continue; }
      if( nl == 0 ) nl=cr;

      // if( refold && nl == 1 ) { nl = cr = 0; ungetc(ch, ifd); ch = ' '; }

      if( refold && nl )
      {
	 if (ch == ' ')  { indented++; continue; }
	 if (ch == '\t') { indented = (indented+8)/8*8; continue; }

      	 if (indented <= previndent && nl == 1) {
	    nl = cr = 0; ungetc(ch, ifd); ch = ' '; 
	 }
	 else if (nl == 1)
	    nl++;

	 previndent = indented; indented = 0;

	 if (nl>2) nl=2;
      }

      if( nl )
      {
	 buf[ccount] = '\0';
	 printf("%s\n", buf);
	 ccount = 0;
	 for(nl--; nl>0; nl--) printf("\n");
	 cr=nl= 0;
	 prev = ' ';
      }

      if( refold )
      {
	 if( ch == '\t' ) ch = ' ';
	 if( ch == ' ' && prev == ' ' ) continue;
         prev = ch;
      }

      if( ch == '\t' )
      {
	while((ccount&7) != 7 ) buf[ccount++] = ' ';
	ch = ' ';
      }
      if( ch >= ' ' && ch != '\177' )
      {
	 buf[ccount++] = ch;
	 if( ccount > cols )
	 {
	    buf[ccount] = '\0';
	    for(i=ccount-1; i>indent ; i--)
	    {
	       if( buf[i] == ' ' )
	       {
		  buf[i] = '\0';
		  printf("%s\n", buf);
		  i++; ccount = 0;
		  while( ccount <indent ) buf[ccount++] = ' ';
		  while( buf[ccount++] = buf[i++] ) ;
		  ccount--;

		  break;
	       }
	    }
	 }
      }
   }
   if (ccount) {
      buf[ccount] = '\0';
      printf("%s\n", buf);
   }
}
