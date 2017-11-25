/*
 * xxencode [input] output
 * xxdecode [input]
 *
 * Encode/decode a file so it can be mailed to a remote system.
 *
 * This program will produce:
 *    uuencode    - Normal form uuencode with backquotes.
 *                - Or old style form with spaces.
 *    xxencode    - Uses nice safe character set.
 *    mime-base64 - GNU/Posix style using MIME base64.
 *    zzencode    - uuencode with some other character set,
 *                  check the zzset table.
 *
 * This program will decode:
 *    uudecode    - With backquote or space.
 *    xxdecode    - uuencode with nice safe character set.
 *    table       - any uuencode like form with valid 'table' lines.
 *    mime-base64 - GNU/Posix style.
 *
 * The decoding uses a traditional style algorithm so multi-part is not 
 * supported and a corrupt input will probably produce a corrupt output
 * without any warnings.
 *
 * The encoder can add a line by line checksum but the decoder doesn't
 * check it.
 */

#ifdef __STDC__
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#endif
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <pwd.h>

/* ENC is the basic 1 character encoding function to make a char printing */
#define ENC(c) ( set[ (c) & 077 ] )
static char uuset[] =
	"`!\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_"
#ifdef __STDC__
	" abcdefghijklmnopqrstuvwxyz{|}~"
#endif
	;
static char oldset[] =
	" !\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_";
static char xxset[] =
	"+-0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";

static char mmset[] =
	"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static char Mmset[] =
	"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

	/* This one is for customisation. */
static char zzset[70] =
	"`abcdefghijklmnopqrstuvwxyz;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_";

char * set = uuset;

/* single character decode */
#define DEC(c)	( table[ (c) & 0xFF ] )

static int table[256];		/* Decode table */
static int add_csum = 0;	/* Add line checksums */
static int dflag = 0;		/* Do decoding. */
static int hflag = 0;		/* Disable headers and footers. */
static int wflag = 0;		/* Wide encoding 57 bytes/line */
static int tflag = 0;		/* Force a table to be output. */
static int pflag = 0;		/* Remove count byte on in and out. */

#ifdef __STDC__
void Usage(void);
void encode(FILE *in);
int do_decode(char *filename);
#else
#define void
#endif

void Usage() {
	printf("Usage:\txxencode [-uxmMO] [-z table] [-hcw] [infile] remotefile\n");
	printf("Or:\txxencode [-uxmMO] -d [infile]\n");
	printf("Or:\txxdecode [-uxmMO] [infile]\n");
	printf("Or:\tuuencode [-hcw] [infile] remotefile\n");
	printf("  -d\tdecode\n");
	printf("  -u\tUse normal uuencode character set\n");
	printf("  -O\tUse old uuencode character set (with spaces)\n");
	printf("  -x\tUse xxencode character set\n");
	printf("  -M\tUse MIME base64 character set\n");
	printf("  -m\tEncode using POSIX base64 method\n");
	printf("  -h\tDon't output headers or footers of encoding\n");
	printf("  -t\tAdd a table section for normal uuencode.\n");
	printf("  -c\tAdd a checksum on the end of each line\n");
	printf("  -p\tRemove the Prefix length bytes\n");
	printf("  -w\tUse longer lines - 57 characters not 45\n");
	exit(2);
}

int
main(argc, argv)
char **argv;
{
	FILE *in;
	struct stat sbuf;
	int mode, ns;
	char * pgm = argv[0];
	char * arg;

	if (strncmp(pgm+strlen(pgm)-8, "uu", 2) == 0) set = uuset;
	if (strncmp(pgm+strlen(pgm)-8, "xx", 2) == 0) set = xxset;
	if (strncmp(pgm+strlen(pgm)-8, "mm", 2) == 0) set = mmset;
	if (strcmp(pgm+strlen(pgm)-6, "decode") == 0) dflag = 1;

	while(argc>1 && argv[1][0] == '-' && argv[1][1]) {
		char * p = argv[1]+1;
		while(*p) switch(*p++) {
		case 'u': set = uuset ; break;	/* Standard uuencode */
		case 'x': set = xxset ; break;	/* xxencode */
		case 'm': set = mmset ; break;	/* GNU/Posix base64 encode */
		case 'M': set = Mmset ; break;	/* base64 uuencode */
		case 'O': set = oldset ; break;	/* Old uuencode */
		case 'z': set = zzset ; 	/* uuencode, different cset */
			  if (p && *p) {
			     strncpy(zzset, p, sizeof(zzset)-1);
			     p="";
			  } else {
			     if (argc<2) Usage();
			     argc--; argv++;
			     strncpy(zzset, argv[1], sizeof(zzset)-1);
			  }
			  if (strlen(zzset) != 64) Usage();
			  break;

		case 'h': hflag=1; break;	/* Remove headers */
		case 'c': add_csum=1; break;	/* Add line checksums */
		case 'w': wflag=1; break;	/* Wide */
		case 't': tflag=1; break;	/* Table */
		case 'p': pflag=1; break;	/* Prefix count byte */

		case 'd': dflag=1; break;	/* Decode */
		default:  Usage();
		}
		argc--; argv++;
	}
	if(dflag && argc > 2) Usage();
	if(dflag) exit(do_decode(argv[1]));

	if (add_csum && set != uuset && set != oldset)
	   fprintf(stderr, 
		   "WARNING: -c option was only used with normal uuencode.\n");

	/* optional 1st argument */
	if (argc > 2 || (argc == 2 && hflag)) {
		if ((in = fopen(argv[1], "r")) == NULL) {
			perror(argv[1]);
			exit(1);
		}
		argv++; argc--;
	} else
		in = stdin;

	if (argc != 2 && hflag == 0) Usage();

	/* figure out the input file mode */
	fstat(fileno(in), &sbuf);
	mode = sbuf.st_mode & 0777;
	if (!hflag) {
		if (tflag || (set != uuset && set != oldset
			&& set != mmset
			)) {
		   	/* Note this use of table - for alternate character
			 * sets is _not_ what the table hack was designed for.
			 * It was designed as an error correction device
			 * however if the decoder understands tables it's
			 * better to avoid errors before they occur by using
			 * a safer character set.
			 */
			printf("table\n");
			printf("%.32s\n", set);
			printf("%.32s\n", set+32);
		}
		printf("begin%s %o ", 
			(set==mmset?"-base64":(pflag?"s":"")), mode);
		/* Careful here, don't make a broken file. */

		for (arg=argv[1], ns= -1; *arg; arg++) {
		   if (ns == -1 && *arg == ' ') continue;
		   if (*arg != ' ') {
		      while(ns>0) putchar(' '), ns--;
		      ns=0;
		      if (*arg < ' ' && *arg > 0)
			 putchar('?');
		      else
			 putchar(*arg);
		   }
		   else ns++;
		}
		if (ns == -1) printf("Space");
		putchar('\n');
	}
	encode(in);
	if (!hflag)
		printf("%s\n",(set!=mmset?"end":"===="));
	exit(0);
}

/*
 * copy from in to out, encoding as you go along.
 */
void encode(in)
FILE *in;
{
	char buf[80];
	char wbuf[80], *wptr;
	int i, n, csum;

	for (;;) {
		/* 1 (up to) 45 character line */
	        n = fread(buf, 1, wflag?57:45, in);
		wptr = wbuf;

		/* Make sure additional bits are always zero */
		if (n>0 && n < (wflag?57:45))
		   memset(buf+n, 0, (wflag?57:45) - n);

		/* GNU mmencode doesn't have line lengths */
		if(set != mmset && !pflag) 
			*wptr++ = ENC(n);
		if (n<=0 && ( set == mmset || hflag))
			break;

		csum = 0;
		for (i=0; i<n; i += 3)
		{
			unsigned char * p = buf + i;
			int v;

			*wptr++ = ENC(v=(*p >> 2));
			csum += v;
			*wptr++ = ENC(v=((*p << 4) & 060 | (p[1] >> 4) & 017));
			csum += v;
			*wptr++ = ENC(v=((p[1] << 2) & 074 | (p[2] >> 6) & 03));
			csum += v;
			*wptr++ = ENC(v=(p[2] & 077));
			csum += v;
		}
		if (set == mmset) {
			switch(n%3) {
			case 1: wptr[-2] = '=';
			case 2: wptr[-1] = '=';
			}
		} 
		if (pflag) {
			switch(n%3) {
			case 1: wptr[-2] = '\0';
			case 2: wptr[-1] = '\0';
			}
		} 
		if (add_csum) *wptr++ = ENC(csum);
		*wptr = 0;
		printf("%s\n", wbuf);
		if (n <= 0)
			break;
	}
}

/*------------------------------------------------------------------------
 * xxdecode [input]
 *
 * Create the specified file, decoding as you go.
 */

int do_decode(filename)
char * filename;
{
	FILE *in, *out;
	int mode;
	char dest[128];
	char buf[512];
	static char tableset[80];
	char wbuf[80];
	char *wptr;
	unsigned char *bp;
	int n;
	int to_stdout = 0;
	char bb[4];
	int  bc = 0;
	int  posix_enc = 0;

	/* optional input arg */
	if (filename) {
		if ((in = fopen(filename, "r")) == NULL) {
			perror(filename);
			exit(1);
		}
	} else	in = stdin;

	/* search for header line */
	for (;;) {
		if (fgets(buf, sizeof buf, in) == NULL) {
			fprintf(stderr, "No begin line\n");
			exit(3);
		}
		if (strncmp(buf, "begin ", 6) == 0) {
		   	pflag = 0;
			break;
		}
		if (strncmp(buf, "begins ", 7) == 0) {
		   	pflag = 1;
			break;
		}
		if (strncmp(buf, "begin-base64 ", 13) == 0) {
		   	posix_enc = 1;
			set = mmset;
			break;
		}
		/* table option allows substitute encodings. */
		if (strcmp(buf, "table\n") == 0) {
			 if (fgets(buf, sizeof buf, in) == NULL) continue;
			 memcpy(tableset, buf, 32);
			 if (fgets(buf, sizeof buf, in) == NULL) continue;
			 memcpy(tableset+32, buf, 32);
			 tableset[64] = 0;
			 set = tableset;
		}
	}
	for(;;) {
		sscanf(buf, "%*s %o %s", &mode, dest);

		/* handle ~user/file format */
		if (dest[0] == '~') {
			char *sl;
			struct passwd *getpwnam();
			char *index();
			struct passwd *user;
			char dnbuf[100];

			sl = index(dest, '/');
			if (sl == NULL) {
				fprintf(stderr, "Illegal ~user\n");
				exit(3);
			}
			*sl++ = 0;
			user = getpwnam(dest+1);
			if (user == NULL) {
				fprintf(stderr, "No such user as %s\n", dest);
				exit(4);
			}
			strcpy(dnbuf, user->pw_dir);
			strcat(dnbuf, "/");
			strcat(dnbuf, sl);
			strcpy(dest, dnbuf);
		}

		/* create output file */
		if (strcmp(dest, "-") == 0) {
			out = stdout;
			to_stdout = 1;
		} else {
			out = fopen(dest, "w");
			if (out == NULL) {
				perror(dest);
				exit(4);
			}
			chmod(dest, mode);
		}

		if (set == oldset) {
			/*
			 * If we use the very very old style then make
			 * the decode the same as Berkeley 5.1 (1983-07-02)
			 */
			for(n=0;n<256;n++) table[n] = (((n) - ' ') & 077);
		} else {
			if (posix_enc) {
			    memset(table, -1, sizeof(table));
			    table['='] = 0;
			} else
			    memset(table, 0, sizeof(table));
			for(n=0;set[n];n++) table[set[n] & 0xFF] = (n&077);
		}

		for (;;) {
			/* for each input line */
			if (fgets(buf, sizeof buf, in) == NULL) {
				printf("Short file\n");
				exit(10);
			}
			if (posix_enc) {
			    /* Standard base64 decode ignores illegal
			     * characters completely.
			     */
			    bp = buf; wptr = wbuf; n = 3;
			    while(*bp) {
				if (DEC(*bp) != -1)
				    bb[bc++] = *bp;
				bp++;
				if (bc==4) {
				    wptr[0] = DEC(*bb) << 2 | DEC(bb[1]) >> 4;
				    wptr[1] = DEC(bb[1]) << 4 | DEC(bb[2]) >> 2;
				    wptr[2] = DEC(bb[2]) << 6 | DEC(bb[3]);

				    if (bb[0] == '=')      n=0;
				    else if (bb[1] == '=') n=0;
				    else if (bb[2] == '=') n=1;
				    else if (bb[3] == '=') n=2;
				    else                   n=3;

				    wptr+=n;
				    bc=0;
				}
			    }

			    if (wptr != wbuf)
				fwrite(wbuf, 1, wptr-wbuf, out);
			    if (n==0) break;

			} else if (pflag) {
			    n = strlen(buf);
			    while (n>0 && buf[n-1]<=' ') n--;
			    if (n <= 0) break;

			    bp = buf; wptr = wbuf;
			    while (n > 0) {
			        if (n>1)
				   *wptr++ = DEC(*bp) << 2 | DEC(bp[1]) >> 4;
			        if (n>2)
				   *wptr++ = DEC(bp[1]) << 4 | DEC(bp[2]) >> 2;
			        if (n>3)
				   *wptr++ = DEC(bp[2]) << 6 | DEC(bp[3]);

				bp += 4;
				n -= 4;
			    }
			    fwrite(wbuf, 1, wptr-wbuf, out);

			} else {
			    n = DEC(buf[0]);
			    if (n <= 0) break;

			    bp = buf+1; wptr = wbuf;
			    while (n > 0) {
				*wptr++ = DEC(*bp) << 2 | DEC(bp[1]) >> 4;
				*wptr++ = DEC(bp[1]) << 4 | DEC(bp[2]) >> 2;
				*wptr++ = DEC(bp[2]) << 6 | DEC(bp[3]);
				bp += 4;
				n -= 3;
			    }
			    fwrite(wbuf, 1, DEC(buf[0]), out);
			}
		}

		if (!to_stdout)
			fclose(out);

		if (posix_enc) break;

		/* Occasionally instead of an 'end' line a file will have
		 * another begin line.
		 */
		if (fgets(buf, sizeof buf, in) == NULL ||
			 (strcmp(buf, "end\n") && strncmp(buf, "begin ", 6))) {
			fprintf(stderr, "No end line\n");
			exit(5);
		}
		if (strcmp(buf, "end\n") == 0)
			break;
	}
	return 0;
}
