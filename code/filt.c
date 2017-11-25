/*
 * ANSI, AVATAR and PC-graphic Filter program (c) Robert de Bath 1991
 *
 * I wrote this program when I couldn't find a terminal emulator that
 * was able to deal properly with logging those nice ANSI screens
 * that all BBS's seem to have.
 *
 * Features:
 *    1) Is able to remove all ANSI sequences from the input file, simple
 *       cursor movements are interpreted and replaced by spaces and overtyping
 *    2) Can translate the 8-bit PC-Graphic set into an approximate
 *       ASCII version (Useful if you have a different printer)
 *    3) Can re-insert the control sequences that deal with colour
 *       (because occasionally I want to keep the colour)
 *
 * Limitations:
 *    If the BBS is continually jumping up and down the screen this program
 *    may not react much better that a normal ANSI stripper as it only buffers
 *    the current line of text.
 *    If the BBS knows (or assumes) how many lines your screen has and uses
 *    absolute cursor positioning after scrolling the screen this program
 *    has to know how many lines the BBS assumed.
 */
#include <stdio.h>
#include <ctype.h>
#include <string.h>

#define AVATAR
#define COLOUR
#define KEEPMOVE

/* Alternate translation table if you don't like the corners */

char charalt[] = "CueaaaaceeeiiiAAE  ooouuyOUc$YPsaiounNao?++  !<>###||||++||++++++--|-+||++--|-+----++++++++##||#aBTPEsyt      EN=+><++-=... n2  ";

char chartab[] = "CueaaaaceeeiiiAAE**ooouuyOUc$YPsaiounNao?++**!<>###||||..||.'''.`--|-+||`.--|-+----``..++'.#****aBTPEsyt******EN=+><++-=...*n2* ";

int esc_found = 0;
char  escbuf[30];
char * escptr;
#define BIGBUF 2048

int lineno = 1;
int colno = 0;
int maxcol = 0;

int savcol = 0;
int savline = 1;

int pglen = 0;
int vflag = 0;
int bits8 = 0;
int nflag = 0;
int rflag = 0;
int crflag = 0;
int wflag = 0;
int lflag = 0;
#ifdef COLOUR
int cflag = 0;
int bflag = 0;
int sflag = 0;
int ccol = 7;
#endif
#ifdef KEEPMOVE
int kflag = 0;
#endif
int length = 80;

#ifdef AVATAR
int avcnt = 0;
#endif

main(argc, argv)
char ** argv;
int argc;
{
   int ch;
   int ar;
   int done_file=0;
   FILE * fd;

   for(ar=1; ar<argc; ar++) if( argv[ar][0] == '-' )
   {
      char * p = argv[ar]+1;
      while( *p ) switch( *p++ )
      {
      case 'T': bits8 = 1; wflag = 1; pglen=24;
#ifdef COLOUR
                cflag = 1;
#endif
		break;
      case 'v': vflag=1; break;
      case 'r': rflag=1; break;
      case 'R': rflag=1; crflag=1; break;
      case 'n': nflag=1; break;
      case '8': bits8=1; break;
      case 'B': length=BIGBUF; break;
      case 'w': wflag=1; break;
      case 'l': lflag=1; break;
      case 'a': strcpy(chartab, charalt); break;
      case 'h': pglen=atoi(p);
		while( *p >= '0' && *p <= '9' ) p++;
		break;
#ifdef COLOUR
      case 'c': cflag = 1; break;
      case 'm': cflag = 2; break;
      case 'b': bflag = 1; break;
      case 's': sflag = 1; break;
#endif
#ifdef KEEPMOVE
      case 'k': kflag = 1; break;
#endif
      default: Usage();
      }
      if( nflag && wflag ) wflag = 0;
   }
   else
   {
      lineno = 1;
      done_file=1;
#ifdef MSDOS
      if( (fd=fopen(argv[ar], "rb")) == 0 )
#else
      if( (fd=fopen(argv[ar], "r")) == 0 )
#endif
      {
	 fprintf(stderr, "Cannot open '%s'\n", argv[ar]);
         continue;
      }
#ifdef COLOUR
      ccol = 7; /* current colour = white */
#endif
      while((ch=getc(fd))!=EOF)
      {
         outchar(ch);
      }
      esc_found = 0;
      outchar('\n');
      if( rflag )
      {
         phy_move(lineno, colno);
      }
      fclose(fd);
   }
   if( !done_file )
   {
#ifdef MSDOS
      Usage();
#else
      ub_copy();
/*
      while((ch=getchar())!=EOF)
      {
         outchar(ch);
      }
*/
      esc_found = 0;
      outchar('\n');
#endif
   }
   update();
}

ub_copy()
{
   char buf[2000];
   int cc;
   int i;

   while( (cc=read(0, buf, sizeof(buf))) > 0 )
   {
      for(i=0; i<cc; i++)
	 outchar(buf[i]);
      fflush(stdout);
   }
}

Usage()
{
   fprintf(stderr, "usage: filt [-T] [-vnrl8aw] [-hNN] filename\n");
   fprintf(stderr, " -v    Verbose list unknown ansi codes\n");
   fprintf(stderr, " -n    Print line numbers\n");
   fprintf(stderr, " -r    Raw output .. use BS and CR to overtype\n");
   fprintf(stderr, " -l    Don't auto-cr on input lf\n");
   fprintf(stderr, " -8    Pass 8-bit characters\n");
   fprintf(stderr, " -a    Use an alternate map for 8-bit chars\n");
   fprintf(stderr, " -w    Assume output device wraps at col 80\n");
   fprintf(stderr, " -hNN  Assume screen height of NN lines\n");
#ifndef COLOUR
   fprintf(stderr, " -T    PC screen mode (Like -w8h24)\n");
#else
   fprintf(stderr, " -T    PC screen mode (Like -w8ch24)\n");
   fprintf(stderr, " -c    Pass ansi colour information\n");
   fprintf(stderr, " -m    Pass ansi mono information\n");
#endif
   /* fprintf(stderr, " -m    Display --More-- at end of page\n"); */
   exit(1);
}

outchar(ch)
int ch;
{
#ifdef AVATAR
   if( avcnt > 0 || ( avcnt == 0 && ( ch == '\026' || ch == '\031')))
   {
      do_avatar(ch);
      return;
   }
#endif
   if( isprint(ch) || (ch&0x80) )
   {
      if(!bits8 && (ch & 0x80) )
         ch = chartab[ch&0x7F];

      if( esc_found )
      {
	 if (escptr < escbuf+sizeof(escbuf)-1)
	    *escptr++ = ch;
         if( ch >= '@' && ch <= '~' && (ch != '[' || escptr != escbuf+1) )
	 {
            *escptr = '\0';
            if( escbuf[0] == '[' )
	       do_ansi(ch);
            else if( vflag )
	    {
	       char *p = escbuf;
               print_ch('^');
               print_ch('[');
	       while( *p )
		  print_ch(*p++);
            }
            esc_found = 0;
         }
      }
      else
         print_ch(ch);
   }
   else if( ch == '\033' )
   {
      esc_found = 1;
      escptr = escbuf;
   }
   else if( ch == '\r' )
      move_to(lineno, 0);
   else if( ch == '\b' )
      move_to(lineno, colno-1);
   else if( ch == '\t' )
      move_to(lineno, ((colno+8)/8)*8);
   else if( ch == '\n' )
   {
      if( lflag )
         move_to(lineno+1, colno);
      else
         move_to(lineno+1, 0);
      check_pglen();
   }
   else if( ch == '\f' )
      do_cls();
   else if( (ch&0xE0) == 0 )
   {
      if( vflag )
      {
         print_ch('^');
         print_ch('@'+ch);
      }
      else if( ch != '\007' && ch != '\0' )
      {
	 print_ch('.');
      }
   }
}

do_ansi(ch)
int ch;
{
   int cnt;

   switch( ch )
   {
   case 's' : savcol = colno; savline = lineno; break;
   case 'u' : move_to(savline, savcol);
	      break;
   case 'A': 
      cnt = atoi(escbuf+1);
      if( cnt < 1 ) cnt = 1;
      move_to(lineno - cnt, colno);
      break;
   case 'B': 
      cnt = atoi(escbuf+1);
      if( cnt < 1 ) cnt = 1;
      move_to(lineno + cnt, colno);
      break;
   case 'C':
      cnt = atoi(escbuf+1);
      if( cnt < 1 ) cnt = 1;
      move_to(lineno, colno + cnt);
      break;
   case 'D':
      cnt = atoi(escbuf+1);
      if( cnt < 1 ) cnt = 1;
      move_to(lineno, colno - cnt);
      break;
   case 'f':
   case 'H':
      cnt = atoi(escbuf+1);
      move_to(cnt, 0);
      escptr = strchr(escbuf, ';');
      if( escptr )
      {
         cnt = atoi(escptr+1);
	 if(cnt < 1 ) cnt=1;
	 move_to(lineno, cnt-1);
      }
      break;
   case 'J': /* Note only ^[2J is simulated */
      do_cls();
      break;
   case 'K':
      cnt = colno;
      while( colno < maxcol ) print_ch(' ');
      move_to(lineno, cnt);
      break;
   case 'M': cnt = atoi(escbuf+1); if(cnt<1) cnt=1;
	     scr_scroll(-cnt,lineno);
	     break;
   case 'm':
#ifdef COLOUR
      if( !cflag ) break;
      escptr = escbuf;
      while( escptr )
      {
         cnt = atoi(escptr+1);
	 switch(cnt)
	 {
	 case 0: ccol  = 0x07; break;
	 case 1: ccol |= 0x08; break;
	 case 2: ccol &= 0xF7; break;
	 case 5: if( bflag ) ccol |= 0x80; break;
	 case 7: ccol  = 0x70; break;
	 case 30: case 31: case 32: case 33:
	 case 34: case 35: case 36: case 37:
		 if( cflag != 2 ) ccol = ((ccol&0xF8)|(cnt-30));
		 break;
	 case 40: case 41: case 42: case 43:
	 case 44: case 45: case 46: case 47:
		 if( cflag != 2 ) ccol = ((ccol&0x8F)|((cnt-40)<<4));
		 break;
	 }
         escptr = strchr(escptr+1, ';');
      }
#endif
      break;
   default:
      if( vflag )
      {
         char *p = escbuf;
         print_ch('^');
         print_ch('[');
	 while( *p )
	    print_ch(*p++);
      }
      break;
   }
}

/******************************************************************************/

int last_row = 1;
int pend_cls = 0;
int last_col = 0;

#ifdef COLOUR
unsigned int buffer[BIGBUF];
int last_attr = 0x07;
#else
unsigned char buffer[BIGBUF];
#endif

move_to(row, col)
int row, col;
{
   if( row < 1 ) row = 1;
   if( col < 0 ) col = 0;
   if( col > 1000000 ) abort();

   lineno = row; colno = col;
}

do_cls()
{
   move_to(1, 0);
   pend_cls++;
   last_row += 50;
}

check_pglen()
{
   while( pglen && lineno > pglen )
   {
      lineno--; last_row--;  /* scroll screen */
   }
}

scr_scroll(lines, row)
int lines,row;
{
   while(lines)
   {
      if(lines > 0)
      {
         lines--;
	 if( last_row > row ) last_row++;
	 if( lineno > row ) lineno++;
      }
      else
      {
         lines++;
	 if( last_row > row ) last_row--;
	 if( lineno > row ) lineno--;
      }
   }
}

phy_move(row, col)
int row, col;
{
   int ch;
#ifdef KEEPMOVE
   int pending = 0;
#endif

   if( row < 1 ) row = 1;
   if( col < 0 ) col = 0;

   if( last_row != row )
   {
      memset(buffer, '\0', sizeof(buffer));
      if( crflag ) putchar('\r');
#ifdef COLOUR
      if( last_attr != 0x07 && !sflag )
      {
	 printf("\033[m");
         last_attr = 0x07;
      }
#endif
#ifdef KEEPMOVE
      if( kflag )
      {
         if( wflag && last_col == length )
         {
	    last_row++; last_col = 0;
         }
         if( last_row +1 == row )
	    putchar('\n');
         else
	    printf("\033[%dH", row);
      }
      else
#endif
      {
         if( !wflag || last_col != length )
	    putchar('\n');
         if( last_row + 1 != row ) putchar('\n');
      }
      pend_cls = 0;
      last_row = row;
      last_col = 0;
      maxcol = colno;
   }
   if( col == 0 && last_col != 0 )
   {
      putchar('\r');
      last_col = 0;
   }
   while( last_col > col )
   {
      putchar('\b');
      last_col--;
   }
   while( last_col < col )
   {
      if(last_col < length)
      {
#ifdef COLOUR
         int this_attr = ((buffer[last_col]>>8)&0xFF);
	 int fl = 0;
#define istr(n) ((n)?";":"\033[")
#endif
#ifdef KEEPMOVE
	 if( kflag && buffer[last_col] == 0 )
	    pending++;
         else
	 {
	    if( pending ) printf("\033[%dC", pending);
	    pending = 0;
#endif
	    ch = (buffer[last_col] & 0xFF);
            if( ch == '\0' ) ch = ' ';

#ifdef COLOUR
	    if( buffer[last_col] == 0 )
	       this_attr = 0x07;
	    if( this_attr != last_attr)
	    {
	       /* printf("\033"); */
	       if( ((last_attr & ~this_attr) &0x88) != 0 )
               {
	          printf("%s0", istr(fl++));
	          last_attr = 0x07;
	       }
               if( (last_attr & 0x08) == 0
                && (this_attr & 0x08) != 0
	        && ch != ' ')
               {
	          printf("%s1", istr(fl++));
	          last_attr |= 0x08;
	       }
	       if( (last_attr & 0x07) != (this_attr &0x07) && ch != ' ')
	       {
	          printf("%s3%c", istr(fl++), '0'+(this_attr&0x07));
	          last_attr = (last_attr&0xF8)+(this_attr&0x07);
	       }
	       if( (last_attr & 0x70) != (this_attr &0x70) )
	       {
	          printf("%s4%c", istr(fl++), '0'+ ((this_attr&0x70)>>4));
	          last_attr = (last_attr&0x8F)+(this_attr&0x70);
	       }
	       if( fl ) printf("m");
               if( (last_attr & 0x80) == 0
                && (this_attr & 0x80) != 0
	        && ch != ' ')
               {
	          printf("\033[5m");
	          last_attr |= 0x80;
	       }
	       /* last_attr = this_attr; */
	    }
#endif
            putchar(ch);
#ifdef KEEPMOVE
         }
#endif
      }
      last_col++;
   }
   if( pending ) printf("\033[%dC", pending);
   pending = 0;
}

print_ch(ch)
int ch;
{
   while( colno >= length )
   {
      move_to(lineno+1, colno-length);
   }

   if( rflag )
      phy_move(lineno, colno);
   else if( last_row != lineno )
      update();

#ifdef COLOUR
   if( ch )
      buffer[colno] = (ch&0xFF) | (ccol << 8);
   else
#endif
      buffer[colno] = ch;

   if( rflag )
      phy_move(lineno, colno+1);

   colno++;
   if( colno > maxcol ) maxcol = colno;
}

update()
{
   int maxcol, i;
   int row = lineno, col = colno;

   if( rflag )
   {
      phy_move(lineno, colno);
      return;
   }

   rflag++;
   for( maxcol=i=0; i<length; i++)
#ifdef COLOUR
      if( (buffer[i]&0xFF) > ' ' ) maxcol = i+1; 
      else if( ((buffer[i]>>8)&0x78) != 0 ) maxcol = i+1; 
#else
      if( buffer[i] > ' ' ) maxcol = i+1; 
#endif

   if( nflag && row != last_row )
      printf("%3d: ", last_row-pend_cls*50);
   phy_move(last_row, maxcol);
   phy_move(row, 0);
   rflag--;
}

/******************************************************************************/

#ifdef AVATAR
char avbuf[20];

/* Note: Avatar 0/+ codes are traped but only Avatar 0 are interpreted */
int avsizes[] = { 0, 3, 2, 2, 2, 2, 2, 2, 4, 2, 7, 7, 5, 6, 2 };
int colconv[] = { 0, 4, 2, 6, 1, 5, 3, 7 };

do_avatar(ch)
{
   char tbuf[30];
   int i, j;

   if( avcnt == 0 )
   {
      avbuf[0] = ch;
      avcnt=1;
      return;
   }
   else
   {
      avbuf[avcnt++] = ch;
      if( avbuf[0] == '\031' )
      {
	 if( avcnt < 3 ) return;
	 j = (avbuf[2]&0x7F);
	 avcnt = -1;
	 for(i=0; i<j; i++)
	    outchar(avbuf[1]);
	 avcnt = 0;
         return;
      }
      avbuf[1] &= 0x3F;
      if( avbuf[1] < 1 || avbuf[1] > 14 )
      {
	 avcnt = -1;
	 outchar('\026');
	 outchar(ch);
	 avcnt = 0;
	 return;
      }
      if( avcnt < avsizes[avbuf[1]]) return;
      avcnt = -1;
      tbuf[0] = '\0';
      switch( avbuf[1] )
      {
      case 1: sprintf(tbuf, "\033[0;3%d;4%dm", colconv[avbuf[2]&0x7], colconv[(avbuf[2]>>4)&0x7]);
	      if( avbuf[2]&0x08 ) sprintf(tbuf+strlen(tbuf), "\033[1m");
	      break;
      case 2: sprintf(tbuf, "\033[5m");
	      break;
      case 3: sprintf(tbuf, "\033[A");
	      break;
      case 4: sprintf(tbuf, "\033[B");
	      break;
      case 5: sprintf(tbuf, "\033[D");
	      break;
      case 6: sprintf(tbuf, "\033[C");
	      break;
      case 7: sprintf(tbuf, "\033[K");
	      break;
      case 8: sprintf(tbuf, "\033[%d;%dH", avbuf[2], avbuf[3]);
	      break;
      }
      for(i=0; tbuf[i]; i++)
	 outchar(tbuf[i]);
      avcnt = 0;
   }
}
#endif

/*
 * Changes:
 *   1992-07-08: Version 1.0 - stable.
 *   2002-09-22: Buffer overflow in escbuf, detected and blocked.
 *
 */

