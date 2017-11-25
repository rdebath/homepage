/* (C) 1989-2000 Robert de Bath, Released under the GPL Version 2.
 *
 *
 * usage: term [-s speed] [-l line] [-e escape-char] [-c callscript]
 *      defaults:
 *              speed:  Higest of 115200, 38400 and 2400
 *              line :  /dev/ttyS0
 *              escape: ~
 *                      escape char may be specified as three octal digits
 *
 *      term escapes:
 *              escape-. or escape-CTRLD        terminate
 *              escape-!                        escape to shell
 *              escape-#                        send break
 *              escape-escape                   send escape char
 *
 *
 * To compile this on linux you just do:
 *    cc -o miterm -O2 -s term.c
 * or
 *    cc -o miterm -O2 -s -DTINY term.c
 * if you want it a couple of K smaller.
 *
 * It has a weird scripting thing that you can enable with -DSCRIPT, it's
 * sort of expect-send with bells on.  The scripting also controls a logging
 * facility (compiled in with -DLOGFILE) but you probably want neither.
 *
 * The bits the -DTINY removes are:
 *     locking the tty line
 *     the option of a 7-bit and 'DUMB' terminal types
 *     and accepting zmodem downloads by calling 'rz'.
 *
 * The last one can be removed on it's own by -DNO_ZMODEM
 */

#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <fcntl.h>
#include <ctype.h>
#include <errno.h>
#include <malloc.h>
#include <setjmp.h>

#ifdef V7
# include <sgtty.h>
#else
# if defined(__linux__) || defined(HAVE_TERMIOS)
#  include <termios.h>
#  ifndef HAVE_TERMIOS
#   define HAVE_TERMIOS
#  endif
# else
#  include <termio.h>
# endif
# ifndef SYSV
#  define SYSV
# endif
#endif


#define XSCRIPT
#define XLOGFILE
#define XCHARXLATE

#ifdef TINY
#define NO_ZMODEM
#define NOLOCK
#define RAWTERM
#endif

#ifdef B115200
#define SPEED B115200		/* default speed */
#else
#ifdef B38400
#define SPEED B38400		/* default speed */
#else
#define SPEED B2400		/* default speed */
#endif
#endif

#ifdef M_XENIX
#define usleep(x) nap((int)(x/1000));
#endif

#define LINE    "/dev/ttyS0"    /* the default line  */
#define ESC     '~'             /* default esc char  */
#define EOT     '\004'          /* ^D */

#ifndef R_OK            /* access() modes */
#define R_OK 4
#define W_OK 2
#endif

char esc[2] = { ESC, '\0' };

extern void s_line();      /* set & save line characteristics   */
extern void r_line();      /* reset saved  line characteristics */

int wr_pid = -1;           /* writer process id */
int line_fd = -1;          /* comm line file desc        */
int tty_fd = 0;            /* console   file desc        */
char line_device[64];

#ifdef __STDC__
#define sigtype void
#else
#define sigtype int
#endif

sigtype cleanup();
sigtype open_timeout();

#ifndef RAWTERM
#ifdef CHARXLATE
int Termtype = 2;
#else
int Termtype = 0;
#endif
int Bit_mask = 0xFF;	   /* Strip high bit from line */
int Parity = 0;		   /* Add parity to line */
#endif

#ifdef LOGFILE
FILE * lfile = 0;
char   logfname[64] = "tty/ttylog%d";
char   ltbuf[10240];
char * ltptr = 0;
#endif

#ifdef SCRIPT
int in_script = 0;
char script_name[128];
char script_args[128];
struct script_sv
{
   struct script_sv * next;
   FILE * scrfd;
   char script_args[128];
}
   * saved_sc = 0;

int verbose = 0;
#endif

sigtype cleanup()
{
   int status;
   if( wr_pid>1 ) { kill(wr_pid, SIGTERM); wait(&status); }

   if( wr_pid ) r_line();
   if( line_fd > 2 ) close(line_fd);
   line_fd = -1;

   if( wr_pid>0 ) fprintf(stderr, "Disconnected\r\n");

#ifndef NOLOCK
   uucp_unlock(line_device);
#endif
#ifdef LOGFILE
   if( lfile ) fclose(lfile);
   lfile = 0;
#endif
   exit(0);
}

void main(argc, argv)
int argc;
char **argv;
{
   int done, status;
   int speed = SPEED;
   extern char *optarg;
   extern int optind;
   extern int getopt(), conv_speed(), conv_esc(), access();

   strcpy(line_device, LINE);
   while((status = getopt(argc, argv,"s:l:e:rdm7pv")) != EOF)
   {
      switch(status)
      {
      case 's':
         if((speed = conv_speed(optarg)) == EOF)
         {
            fprintf(stderr,"%s: invalid speed\n", optarg);
            exit(1);
         }
         break;
      case 'l':
         strncpy(line_device, optarg, sizeof(line_device));
         if(access(line_device, R_OK|W_OK) == -1)
         {
            perror(optarg);
            exit(2);
         }
         break;

      case 'e': esc[0] = conv_esc(optarg); break;
#ifndef RAWTERM
      case 'p': Parity = 1; /* FALL */
      case '7': Bit_mask = 0x7F; break;
      case 'd': Termtype = 1; break;
#ifdef CHARXLATE
      case 'r': Termtype = 0; break;
#endif
#endif
#ifdef SCRIPT
      case 'v': verbose++; break;
#endif
      default:
         fprintf(stderr,
             "usage: %s [-s speed] [-l line] [-e escape-char]\n", *argv);
         exit(3);
      }
   }
   if( optind < argc )
   {
#ifdef SCRIPT
      strcpy(script_name, argv[optind]);
      strcat(script_name, ".sct");
      if(access(script_name, R_OK) == -1)
      {
         strcpy(script_name, "sct/");
         strcat(script_name, argv[optind]);
         strcat(script_name, ".sct");
         if(access(script_name, R_OK) == -1)
         {
	    fprintf(stderr, "Cannot open '%s.sct'\n", argv[optind]);
	    exit(4);
	 }
      }
      strcpy(script_name, argv[optind]);
      in_script = 1;
      for(optind++, script_args[0] = '\0'; optind<argc; optind++)
      {
	 if(*script_args) strcat(script_args, " ");
	 strcat(script_args, argv[optind]);
      }
#else
      fprintf(stderr,
             "usage: %s [-s speed] [-l line] [-e escape-char]\n", *argv);
      exit(3);
#endif
   }

#ifndef NOLOCK
   if( !uucp_check(line_device) )
   {
      fprintf(stderr, "Device in use\n");
      exit(1);
   }
#endif

   signal(SIGALRM, open_timeout);
   alarm(5);

#ifdef O_NONBLOCK
   if((line_fd = open(line_device, O_RDWR|O_NONBLOCK)) < 0)
#else
   if((line_fd = open(line_device, O_RDWR)) < 0)
#endif
   {
      perror(line_device);
      exit(4);
   }

#ifdef O_NONBLOCK
   s_line(speed, 1);
   close(line_fd);
   usleep(20000L);	/* Linux needs this time to cleanup */

   if((line_fd = open(line_device, O_RDWR)) < 0)
   {
      perror(line_device);
      exit(4);
   }
#endif
   s_line(speed, 0);	/* And turn CLOCAL back off so we can
			   trap when the DCD toggles */
   alarm(0);
   signal(SIGALRM, SIG_DFL);

   tty_fd = fileno(stdin);

   signal(SIGINT, SIG_IGN);
   signal(SIGQUIT, SIG_IGN);
   signal(SIGTERM, cleanup);

   set_mode(2);

#ifdef SCRIPT
   if( in_script )
      fprintf(stderr, "Running script\r\n");
   else
#endif
      fprintf(stderr, "Connected\r\n");

   signal(SIGCHLD, cleanup);
   fork_writer(); /* launch writer process */
   read_tty();
   cleanup();
}

sigtype open_timeout()
{
   fprintf(stderr, "Cannot open port -- No carrier\n");
   cleanup();
}

#ifdef LOGFILE
open_logfile()
{
   char file[150];
   char file2[160];
   char file3[160];
   char *p;
   int  lnumber;
   FILE * fd;
   if( lfile ) return;
   if( strchr(logfname, '%') == 0 )
   {
      strcpy(file, logfname);
      lfile = fopen(file, "a");
      if( lfile == 0 ) return;
   }
   else
   {
      strcpy(file, logfname);
      p = strchr(file, '%');
      while(*p) { if(*p == 'd') { *p='s'; break; } else p++; }
      sprintf(file3, file, "cnt");
      lnumber=1;
      fd = fopen(file3, "r");
      if(fd) { fscanf(fd, "%d", &lnumber); fclose(fd); }
      for(;;lnumber++)
      {
         sprintf(file, logfname, lnumber);
         strcpy(file2, file);
         strcat(file2, ".Z");
         if( access(file, 0) != -1 || access(file2, 0) != -1 )
         {
            if( lnumber>=1000 )
            {
               lfile = 0;
               break;;
            }
            continue;
         }
         lfile = fopen(file, "w");
         fd = fopen(file3, "w");
         if(fd) { fprintf(fd, "%d\n", lnumber+1); fclose(fd); }
         break;
      }
   }
   if( lfile )
      fprintf(stderr, "Logfile %s\r\n", file);
   else
      fprintf(stderr, "Cannot open Logfile %s\r\n", file);
   if( ltptr && ltbuf != ltptr )
   {
      fwrite(ltbuf, 1, ltptr-ltbuf, lfile);
      ltptr = 0;
   }
}
#endif

typedef struct
{
   char *str;
   int  spd;
} vspeeds;

static vspeeds valid_speeds[] = 
{
   { "300",  B300  },
   { "1200", B1200 },
   { "2400", B2400 },
   { "4800", B4800 },
   { "9600", B9600 },
#ifdef B19200
   { "19200", B19200 },
#endif
#ifdef B38400
   { "38400", B38400 },
#endif
#ifdef B57600
   { "57600", B57600 },
#endif
#ifdef B115200
   { "115200", B115200 },
#endif
   { (char *)0, 0 }
};

/*
 * given a desired speed string, if valid return its Bspeed value
 * else EOF
 */
int conv_speed(s)
register char *s;
{
   register vspeeds *p = &valid_speeds[0];

   if(*s == 0) return EOF;

   for(; p->str != (char *)0; p++)
      if(strncmp(s, p->str, strlen(s)) == 0)
         return p->spd;
   return EOF;
}

/*
 * convert given string to char
 *      string maybe a char, or octal digits
 */
int conv_esc(s)
register char *s;
{
   register int val = 0;

   if(!isdigit(*s))
      return *s; /* ! octal */

   /* convert octal digs */
   do
   {
      if(*s > '7' || *s < '0')
      {
         fprintf(stderr,"escape char must be character/octal digits\n");
         exit(4);
      }
      val = (val << 3) + (*s++ - '0');
   } while(isdigit(*s));

   return val;
}

/*-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-*/
#ifdef V7
/* This is Version 7 unix code it should work on any unix machine */
/* If you believe that you'll believe anything :-) */

struct sgttyb tty_save, line_save, tty;
int tty_mode = -1;
int got_line = 0;

void s_line(speed, dolocal)
{
   if(!got_line)
   {
      got_line = 1;
      ioctl(line_fd, TIOCGETP, &line_save);
   }
   tty = line_save;
   tty.sg_ospeed  = speed;      /* Set new speed */
   tty.sg_ispeed  = speed;      /* Set new speed */
   tty.sg_flags  |= RAW;
   tty.sg_flags  &= ~ECHO;
   /* Parity doesn't appear to work with Xenix tty.sg_flags |= EVENP; */
   /* So xmit parity is done in software */
   ioctl(line_fd, TIOCSETP, &tty);
}

void r_line()
{
   set_mode(0);
   if(!got_line) return;
   ioctl(line_fd, TIOCSETP, &line_save);
}

set_mode(which)
int which;
{
    if( tty_mode == -1 )
    {
       if(ioctl(tty_fd, TIOCGETP, &tty))
       {
	   perror("Unable to sense terminal");
	   exit(1);
       }
       tty_save = tty;
       tty_mode = 0;
    }

    tty = tty_save;
    switch(which)
    {
    default: /* Normal cooked */
	    which = 0;
	    break;
    case 1: /* CBREAK */
            tty.sg_flags &= ~ECHO;
            tty.sg_flags |= CBREAK; /* Allow break & cr/nl mapping */
	    break;
    case 2: /* RAW */
            tty.sg_flags &= ~ECHO;
            tty.sg_flags |= RAW;
            break;
    }
    if(ioctl(tty_fd, TIOCSETP, &tty))
    {
	perror("Unable to change terminal mode");
    }
    else tty_mode = which;
}
#endif
/*-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-*/
#ifdef SYSV
/* This is System V unix code */

#ifdef HAVE_TERMIOS
struct termios tty_save, line_save, tty;
#else
struct termio tty_save, line_save, tty;
#endif
static int tty_mode = -1;
int got_line = 0;

void s_line(speed, dolocal)
int speed;
{
   if(!got_line)
   {
#ifndef HAVE_TERMIOS
      ioctl(line_fd, TCGETA, &line_save);
#else
      if( tcgetattr(line_fd, &line_save) < 0 )
      {
         perror("Getting line state");
         cleanup();
      }
#endif
      got_line = 1;
   }
   tty = line_save;

#ifdef HAVE_TERMIOS
   cfmakeraw(&tty);
#else
   tty.c_iflag &= ~(IXON|IXOFF|ISTRIP|INLCR|ICRNL|IGNCR|IUCLC);
   tty.c_oflag &= ~(OLCUC|ONLCR|OCRNL|ONOCR|ONLRET|OPOST);
   tty.c_lflag &= ~(ICANON|ECHO|ECHOE|ECHONL|ISIG);
   tty.c_cflag &= ~(PARENB);
#endif

#ifdef CBAUDEX
   tty.c_cflag &= ~(CBAUD|CBAUDEX);
#else
   tty.c_cflag &= ~(CBAUD);
#endif

#ifdef CRTSCTS
   tty.c_cflag |= CRTSCTS;
#endif

   tty.c_iflag |= (IXON|IXOFF);	/* X/Y modem will clear this */
   tty.c_cflag |= HUPCL|(speed);

   if( dolocal )
      tty.c_cflag |= CLOCAL;

   tty.c_cc[VTIME] = 0;
   tty.c_cc[VMIN]  = 1;

#ifndef HAVE_TERMIOS
   ioctl(line_fd, TCSETA, &tty);
#else
   if( tcsetattr(line_fd, TCSANOW, &tty) < 0 )
   {
      perror("Setting line");
      cleanup();
   }
#endif
}

void r_line()
{
   int fd;

   set_mode(0);
   if(!got_line) return;
#ifndef HAVE_TERMIOS
   ioctl(line_fd, TCSETA, &line_save);
#else

   tcflush(line_fd, TCIOFLUSH);

#ifdef O_NONBLOCK
   if( fcntl(line_fd, F_SETFL, O_NONBLOCK) < 0 )
      perror("fnctl");
#endif

   if( tcsetattr(line_fd, TCSANOW, &line_save) < 0 )
   {
#ifdef O_NONBLOCK
      close(line_fd);

      if((line_fd = open(line_device, O_RDWR|O_NONBLOCK)) >= 0 )
      {
         if( tcsetattr(line_fd, TCSANOW, &line_save) < 0 )
            perror("Resetting attrs");

         tcflush(line_fd, TCIOFLUSH);
      }
#else
      perror("Resetting attrs");
#endif
   }

#endif
}

set_mode(which)
int which;
{
    if( tty_mode == -1 )
    {
#ifndef HAVE_TERMIOS
       if(ioctl(tty_fd, TCGETA, &tty))
#else
       if(tcgetattr(tty_fd, &tty))
#endif
       {
	   perror("Unable to sense terminal");
	   exit(1);
       }
       tty_save = tty;
       tty_mode = 0;
    }

    tty = tty_save;
    switch(which)
    {
    case 0: /* Normal cooked */
	    break;
    case 1: /* CBREAK */
	    tty.c_lflag &= ~(ICANON|ECHO|ECHOE|ECHONL);
	    tty.c_lflag |= ICRNL|ISIG;
	    tty.c_cc[VTIME] = 0;
	    tty.c_cc[VMIN]  = 1;
	    break;
    case 2: /* RAW */
	    tty.c_oflag &= ~OPOST;
	    tty.c_iflag &= ~(ICRNL|IGNCR|INLCR);
	    tty.c_lflag &= ~(ICANON|ECHO|ECHOE|ECHONL|ICRNL|ISIG);
	    tty.c_cc[VTIME] = 0;
	    tty.c_cc[VMIN]  = 1;
            break;
    }

#ifndef HAVE_TERMIOS
    if(ioctl(tty_fd, TCSETA, &tty))
#else
    if(tcsetattr(tty_fd, TCSANOW, &tty))
#endif
    {
	perror("Unable to change terminal mode");
    }
    else tty_mode = which;
}
#endif
/*-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-*/

fork_writer()
{
#ifdef LOGFILE
   if( lfile ) fflush(lfile);
#endif
   if (wr_pid = fork())
   {
      if (wr_pid == -1)
      {
         r_line();
         perror("Cannot fork writer");
         exit(0);
      }
      return;
   }
#ifdef LOGFILE
   if(lfile) fclose(lfile);
   lfile = 0;
#endif
   signal(SIGCHLD, SIG_DFL);
   write_tty();
   cleanup();
}

write_tty()
{
   char ch;
   char was_newline = 1;

   signal(SIGINT,  SIG_IGN);
   signal(SIGQUIT, SIG_IGN);
   signal(SIGTERM, cleanup);

   while (1) {
      read(0, &ch, 1);
      ch &= (char)0177;

      if (was_newline) {
         if (ch == esc[0]) {
            read(0, &ch, 1);
            ch &= (char)0177;

            switch (ch) {

            case EOT:
            case '.':
            case '>':
               read(0, &ch, 1);
               exit(0);

            case '!':
               /* tell reader to pause */
               kill(getppid(), SIGUSR1);
               do_shell();
               /* wake him up again */
               if( kill(getppid(), SIGUSR2) < 0 ) exit(0);
               ch = '\377';
               break;

            default:
               if(ch != esc[0])
               {
                  fprintf(stderr, "invalid command--use\
 \"~~\" to start a line with \"~\"\r\n");
               }

            }
         }
      }

      if (ch == '\r' || ch == '\004' || ch == '\003' || ch == '\177')
         was_newline = 1;
      else
         was_newline = 0;

      if( ch != '\377' )
      {
#ifndef RAWTERM
	 int i;
	 if( Parity )
	 {
	    ch &= 0x7F;
	    for(i=0; i<7; i++) if( ch & (1<<i) ) ch ^= 0x80;
	 }
#endif
         write(line_fd, &ch, 1);
      }
   }
}

/*-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-*/

do_shell()
{
   extern char *getenv();
   extern char ** environ;
   register int pid, wpid;
   char *shellp = (char *)0;
   int status;
   sigtype (*oqsig)(), (*oisig)(), (*otsig)();

   if (shellp == (char *)0)
   {
      shellp = getenv("SHELL");
      if (shellp == (char *)0)
         shellp = "/bin/sh";
   }

   oqsig = signal(SIGQUIT, SIG_IGN);
   oisig = signal(SIGINT,  SIG_IGN);
   otsig = signal(SIGTERM,  SIG_IGN);

   set_mode(0);
   if ((pid=fork()) < 0)
   {
      (void) signal(SIGQUIT, oqsig);
      (void) signal(SIGINT,  oisig);
      (void) signal(SIGTERM, otsig);
      set_mode(2);
      fprintf(stderr, "Failed to create process\r\n");
      return;
   }

   if (pid == 0)
   {
      signal(SIGQUIT, SIG_DFL);
      signal(SIGINT,  SIG_DFL);
      signal(SIGTERM,  SIG_DFL);
      execle(shellp, "sh", "-i", (char *)0, environ);
      /* SHELL env is buggered try default */
      execle("/bin/sh", "sh", "-i", (char *)0, environ);
      exit(0);                /* Should do better!    */
   }
   while ( ( (wpid=wait(&status))>=0 || errno == EINTR) && wpid!=pid)
      ;

   set_mode(2);
   (void) signal(SIGQUIT, oqsig);
   (void) signal(SIGINT,  oisig);
   (void) signal(SIGTERM, otsig);
}

/*-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-*/

#define NCHRS 4096
static char chrs[NCHRS];

int alarm_delay = 0;
jmp_buf timebuf;
int is_in    = 0;

sigtype alarm_int() { if( is_in ) longjmp(timebuf, -1); }

#ifndef NO_ZMODEM
char zmodem_buf[20];
char *zptr=zmodem_buf;

char z_request[]   =  "*\030B00";
char z_challange[] = "**\030B0e313233341821\r\n";
char z_charesp[]   =  "*\030B0331323334395b";

/* With this you become a SZModem ver 1.x
char z_challange[] = "**\030B0e534d4201053c\r\n";
char z_charesp[]   =  "*\030B03424d5301"; 
/* */

#endif

int paused = 0;

sigtype unpause_reader()
{
   paused = 0;
}

sigtype pause_reader()
{
   alarm(0); is_in = 0;

   write(2, "Paused\r\n", 8);
   signal(SIGUSR1, SIG_IGN);
   signal(SIGUSR2, unpause_reader);
#ifdef LOGFILE
   if( lfile ) fflush(lfile);
#endif
   paused = 1;
   while(paused) pause();
   signal(SIGUSR2, SIG_IGN);
   signal(SIGUSR1, pause_reader);
   write(2, "Continue\r\n", 10);
}

read_tty()
{
   register int i, n;
   register char *p;

   /* Child process */
   signal(SIGINT,  SIG_IGN);
   signal(SIGQUIT, SIG_IGN);
   signal(SIGTERM, cleanup);
   signal(SIGUSR1, pause_reader);

#ifdef SCRIPT
   if( in_script )
   {
      open_script(script_name);
      signal(SIGINT, cleanup);
      set_mode(1);
      alarm_delay = 2;
   }
#else
#ifdef LOGFILE
   open_logfile();
#endif
#endif

   for(;;)
   {
      if( alarm_delay )
      {
         signal(SIGALRM, alarm_int);
         alarm(2);
         if( setjmp(timebuf) == 0 )
         {
            is_in = 1;
            n = read(line_fd, chrs, (int)NCHRS);
            is_in = 0;
         }
         else
         {
            chrs[0] = 0xFF;
            n = 1;
         }
         alarm(0);
         signal(SIGALRM, SIG_DFL);
      }
      else
         n = read(line_fd, chrs, (int)NCHRS);
      if( n == 0 )
      {
         fprintf(stderr, "Carrier Lost\r\n");
         break;
      }
      if( n > 0 )
      {
         for(i=0;i<n;i++)
	 {
#ifdef SCRIPT
            if( !in_script || chrs[i] != '\377' )
	    {
#endif
#ifdef LOGFILE
               if( lfile ) fputc(chrs[i], lfile);
               else if( ltptr && ltptr < ltbuf+sizeof(ltbuf) )
		  *ltptr++ = chrs[i];
#endif
#ifdef RAWTERM
               putchar(chrs[i]);
#else
               switch(Termtype)
	       {
	       case 1:  dumbch(chrs[i]&Bit_mask); break;
#ifdef CHARXLATE
	       case 2:  outchar(chrs[i]&Bit_mask); break;
#endif
               default: putchar(chrs[i]&Bit_mask); break;
	       }
#endif
#ifdef SCRIPT
	    }
	    if( in_script )
	    {
	       in_script = out_schar(chrs[i]);
	       if( !in_script )
	       {
                  close_script();
		  printf("\r\nScript completed\r\n");
		  alarm_delay = 0;
		  set_mode(2);
                  signal(SIGINT, SIG_IGN);
#ifdef LOGFILE
                  if( lfile == 0 ) open_logfile();
#endif
	       }
	    }
#endif
	 }

         fflush(stdout);
#ifndef NO_ZMODEM
	 /* NOTE Zmodem needs an 8 bit channel this check is 8-bit */
         check_zmodem(chrs, n);
#endif
      }
      if( n < 0 )
      {
	 perror("write_tty");
	 break;
      }
   }
#ifdef LOGFILE
   if( lfile ) fclose(lfile);
   lfile = 0;
#endif
}

#ifndef NO_ZMODEM
check_zmodem(chrs, n)
char * chrs;
int n;
{
   int i;
   for(i=0;i<n;i++)
   {
      if(chrs[i] == '*')
      {
	 zptr = zmodem_buf;
	 memset(zmodem_buf, '\0', sizeof(zmodem_buf));
      }
      if( zptr != zmodem_buf+sizeof(zmodem_buf))
	 *zptr++ = chrs[i];
   }
   if( strncmp(zmodem_buf, z_request, sizeof(z_request)-1) == 0 )
   {
      write(line_fd, z_challange, sizeof(z_challange)-1);
      memset(zmodem_buf, '\0', sizeof(zmodem_buf));
   }
   else if( strncmp(zmodem_buf, z_charesp, sizeof(z_charesp)-1) == 0 )
   {
      call_zmodem();
      memset(zmodem_buf, '\0', sizeof(zmodem_buf));
   }
}

call_zmodem()
{
   int pid, wpid;
   int status;
   sigtype (*oqsig)(), (*oisig)(), (*otsig)();

   printf("\r\nAuto-Zmodem\r\n");
   fflush(stdout);
#ifdef LOGFILE
   if( lfile ) fflush(lfile);
#endif
   oqsig = signal(SIGQUIT, SIG_IGN);
   oisig = signal(SIGINT,  SIG_IGN);
   otsig = signal(SIGTERM,  SIG_IGN);
   signal(SIGCHLD, SIG_DFL);
   set_mode(1);
   if ((pid = fork()) == -1 )
   {
      perror("Cannot zmodem");
      set_mode(2);
      return;
   }
   if( pid == 0 )
   {
      signal(SIGINT, SIG_DFL);
      signal(SIGQUIT, SIG_DFL);
      signal(SIGTERM, SIG_DFL);
      close(0);
      dup(line_fd);
      close(1);
      dup(line_fd);
      execlp("rz", "rz", (char *)0);
      perror("Cannot Zmodem");
      exit(1);
   }

   while ( ( (wpid=wait(&status))>=0 || errno == EINTR) && wpid!=pid)
      if( wpid == wr_pid )
	 cleanup();

   signal(SIGCHLD, cleanup);

   set_mode(2);
   (void) signal(SIGQUIT, oqsig);
   (void) signal(SIGINT,  oisig);
   (void) signal(SIGTERM, otsig);
}
#endif

/*-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-*/

#ifndef RAWTERM
dumbch(ch)
int ch;
{
static int cr_pend = 0;

   if( cr_pend )
   {
      if( ch == '\n' ) putchar('\r');
      else { putchar('^'); putchar('M'); }
      cr_pend = 0;
   }
   if( ch == '\r' )
      cr_pend = 1;
   else
   {
      if( ch & 0x80 )
	 printf("M-");
      if( isprint(ch&0x7F) || ch == '\n' || ch == '\b' )
	 putchar(ch&0x7F);
      else
      {
	 putchar('^');
	 putchar((ch ^ '@')&0x7F);
      }
   }
}
#endif

/*-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-*/

#ifndef NOLOCK
uucp_check(device)
char * device;
{
   FILE * fd;
   char nbuf[128];
   char * p;
   int i;

   p = strrchr(device, '/');
   strcpy(nbuf, "/var/lock/LCK..");
   if( p ) strcat(nbuf, p+1);
   else    strcat(nbuf, device);
   for(p=nbuf+21; *p; p++) if( *p>='A' && *p<='Z' ) *p = *p-'A'+'a';
   fd = fopen(nbuf, "r");
   if( fd )
   {
      fscanf(fd, "%d", &i);
      fclose(fd);
      if( kill(i, 0) == 0 ) return 0;   /* Sorry */
      if( errno == EPERM ) return 0;
      if( unlink(nbuf) == -1 ) return 0;
   }
   fd = fopen(nbuf, "w"); /* Got it! */
   fprintf(fd, "%10d\n", getpid());
   fclose(fd);
   return 1;
}

uucp_unlock(device)
char * device;
{
   FILE * fd;
   char nbuf[128];
   char * p;
   int i;

   p = strrchr(device, '/');
   strcpy(nbuf, "/var/lock/LCK..");
   if( p ) strcat(nbuf, p+1);
   else    strcat(nbuf, device);
   for(p=nbuf+21; *p; p++) if( *p>='A' && *p<='Z' ) *p = *p-'A'+'a';
   fd = fopen(nbuf, "r");
   if( fd )
   {
      fscanf(fd, "%d", &i);
      fclose(fd);
      if( i == getpid() || kill(i, 0) != 0 )
         unlink(nbuf);
   }
}
#endif

/*-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-*/

#ifdef SCRIPT

#define MAXMATCH 20   /* Max number of match strings at a time */
#define MATCHLEN 20   /* Max size of a match string */
static int starcount = 50;

static FILE * script_file;
static char buffer[MATCHLEN*3];
static int offset = sizeof(buffer)/2;
static check_match();
static clear_matchtab();
static read_matchset();
static char * get_item();
static vdisp();
static script_com();

static int timeout  = 0;
static long last_ok = 0;

/*-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-*/

out_schar(ch)
int ch;
{
   long now;
   int ok = 1;

   if( ch != 0 ) /* Completely ignore nuls */
   {
      if( ch != '\377' ) buffer[offset++] = (ch&0x7F);
      else               buffer[offset++] = '\377';
      if( offset == sizeof(buffer) )
      {
         offset = sizeof(buffer)/2;
         memcpy(buffer, buffer+sizeof(buffer)/2, sizeof(buffer)/2);
      }
      buffer[offset] = '\0';

      ok = check_match();
   }
   time(&now);
   if( timeout && now > last_ok + timeout )
   {
      printf("\r\nTimeout ... Script aborted\r\n");
      cleanup();
   }
   return ok;
}

open_script(fname)
char * fname;
{
   char buf[200];
   if(script_file) close_script();
   strcpy(buf, fname);
   strcat(buf, ".sct");
   script_file = fopen(buf, "r");
   if( script_file == 0 )
   {
      strcpy(buf, "sct/");
      strcat(buf, fname);
      strcat(buf, ".sct");
      script_file = fopen(buf, "r");
   }
   if( script_file == 0 ) return 0;
   time(&last_ok);
   timeout = 10;
   read_matchset();
   check_match();
   return 1;
}

close_script()
{
   clear_matchtab();
   if(script_file)
      fclose(script_file);
   script_file = 0;
   timeout = 0;
}

#define VALIDREC	1	/* This record is available for matching */
#define ALTMATCH	2	/* Is an extra match record for prev line */
#define SCCOM		4	/* sendstr is a script command */
#define UXCOM		8	/* sendstr is a unix command */

static
struct matchrec
{
   char match[MATCHLEN];
   int  matchlen;
   int  maxcount;
   char * sendstr;
   int  flags;
}
   matchtab[MAXMATCH];

static int matcount;

static
check_match()
{
   struct matchrec * sptr;
   struct matchrec * mptr;
   extern int line_fd;
   int i,j;
   int checked_one;

Restart:
   sptr = matchtab;
   mptr = matchtab;
   checked_one = 0;

   for(i=0; i<matcount; i++, mptr++)
   {
      if( (mptr->flags & ALTMATCH) == 0 ) sptr = mptr;

      if( (sptr->flags & VALIDREC) == 0 ) continue;
      checked_one = 1;
      if( (mptr->matchlen == 0 
           || ( buffer[offset-mptr->matchlen] == mptr->match[0]
              && strcmp(buffer+offset-mptr->matchlen, mptr->match) == 0 )
          )
        )
      {
         time(&last_ok);
         offset = sizeof(buffer)/2;
         memset(buffer, 0, sizeof(buffer));
         if( mptr->matchlen ) vdisp(0, "<Found>\r\n");
         if( sptr->flags & SCCOM )
	    switch(script_com(sptr->sendstr))
	    {
	    case 0: break;
	    case 1: close_script(); return 0;
	    case 2: goto Restart;
	    }
#if 0
         else if( sptr->flags & UXCOM )
#endif
         else
         {
            vdisp(0, "<Sending> '");
            if( line_fd )
	    {
	       /* Yes we want this ssslllooowww incase */
	       /* the other end is stupid */
	       /* Of course this isn't really that slow */
	       char ch[2];
	       ch[1] = '\0';
	       for(i=0; sptr->sendstr[i]; i++)
	       {
		  ch[0] = sptr->sendstr[i];
                  vdisp(1, ch);
	          if(*ch == '\377')
		     sleep(2);
                  else
		  {
                     write(line_fd, ch, 1);
                     usleep(20000L);
                  }
	       }
	    }
            vdisp(0, "'\r\n");
         }
         time(&last_ok);
         if( sptr->maxcount )
         {
            if( --(sptr->maxcount) == 0 )
            {
               sptr->flags &= ~VALIDREC;
            }
         }
         else
	 {
            read_matchset();
            goto Restart;   /* Incase this is a '- "Send"' line */
	 }
         break;
      }
   }
   return checked_one;
}

static
clear_matchtab()
{
   int i;
   for(i=0; i<matcount; i++)
      if( matchtab[i].sendstr ) free(matchtab[i].sendstr);
   memset(matchtab, 0, sizeof(matchtab));
   matcount = 0;
}

static
read_matchset()
{
   char lbuf[128];
   char sbuf[128];
   char * p;
   int i;
   int saveptr;
   int keepgoing = 1;
   struct matchrec new;
   int wflg = 0;

   clear_matchtab();
   memset(&new, 0, sizeof(new));
   if( script_file == 0 ) return;

   while( keepgoing )
   {
      if( fgets(lbuf, sizeof(lbuf), script_file) == 0 )
      {
	 struct script_sv *p = saved_sc;
	 if( p == 0 ) break;
	 fclose(script_file);
         script_file = p->scrfd;
	 strcpy(script_args, p->script_args);
	 saved_sc = p->next;
	 free(p);
	 continue;
      }
      p = lbuf;
      while( *p == ' ' || *p == '\t' ) p++;
      if( *p == '#' || *p == '\n' || *p == '\0' ) continue;
#ifdef VARS
      if( *p == '%' )
      if( *p == '$' )
#endif
      memset(&new, 0, sizeof(new));
      if( *p == '*' || *p == '?' )
      {
	 if( *p == '?' ) new.maxcount = 1;
	 else            new.maxcount = starcount;
	 p++; while( *p == ' ' || *p == '\t' ) p++;
      }
      else
         keepgoing = 0;
      saveptr = matcount;
      do
      {
         if( *p == ',' ) p++;
      
	 p = get_item(p, new.match, sizeof(new.match), 0);
	 if( p == 0 )
	 {
	    fprintf(stderr, "Syntax error:%s", lbuf);
	    goto next_line;
	 }
         new.matchlen = strlen(new.match);
	 if( new.matchlen )
	 {
            if( wflg == 0 ) { vdisp(0, "<Waiting>"); wflg = 1; }
            vdisp(0, " '");
            vdisp(1, new.match);
            vdisp(0, "'");
	 }
	 while( *p == ' ' || *p == '\t' ) p++;
         if( matcount >= MAXMATCH )
            fprintf(stderr, "Too many match strings\r\n");
         else
         {
            matchtab[matcount] = new;
            matcount++;
            new.flags |= ALTMATCH;
         }
      }
      while( *p == ',');

      p = get_item(p, sbuf, sizeof(sbuf), 1);
      if( p == 0 )
      {
         fprintf(stderr, "Syntax error:%s", lbuf);
         goto next_line;
      }
      if( *sbuf != '"' ) 
      {
         matchtab[saveptr].sendstr = malloc(strlen(sbuf)+1);
         strcpy(matchtab[saveptr].sendstr, sbuf);
         matchtab[saveptr].flags |= SCCOM;
         while( (p = get_item(p, sbuf, sizeof(sbuf), 1)) != 0 )
	 {
	    matchtab[saveptr].sendstr = realloc(matchtab[saveptr].sendstr,
	             strlen(matchtab[saveptr].sendstr) + strlen(sbuf)+2);
	    strcat(matchtab[saveptr].sendstr, " ");
	    strcat(matchtab[saveptr].sendstr, sbuf);
	 }
      }
      else
      {
         matchtab[saveptr].sendstr = malloc(strlen(sbuf));
         strcpy(matchtab[saveptr].sendstr, sbuf+1);
      }
      matchtab[saveptr].flags |= VALIDREC;
      
next_line: ;
   }
   if( wflg ) vdisp(0, "\r\n");
}

static char *
get_item(p, buf, szbuf, itype)
char * p;
char * buf;
int szbuf;
int itype;
{
   char stc;
   while( *p == ' ' || *p == '\t' ) p++;
   if( *p == '-' )
   {
      if( itype == 0 ) buf[0] = '\0'; else buf[0] = '"';
      buf[1] = '\0';
      p++; return p;
   }
   else if( ( itype == 0 && *p != '"' ) || *p == '\0' )
   {
      buf[0] = '\0'; return 0;
   }
   if( itype == 0 ) stc = *p++;
   else { *buf++ = stc = *p++; szbuf--; }
   while( *p )
   {
      if( stc == '"' && *p == '"' ) break;
      if( stc != '"' && (*p == ' ' || *p == '\t' || *p == '\n') ) break;
      if( szbuf < 2+(itype&1) ) { p++; continue; ; }
      szbuf--;
      if( *p == '\\' && p[1] )
      {
	 switch(p[1])
	 {
	 case 'b': *buf++ = '\b'; break;
	 case 'c': itype &= ~1; break;
	 case 'd': *buf++ = '\377'; break;
	 case 'f': *buf++ = '\f'; break;
	 case 'n': *buf++ = '\n'; break;
	 case 'r': *buf++ = '\r'; break;
	 case 't': *buf++ = '\t'; break;
	 case 'a':
	    {
	       char * q = script_args;
	       while(*q) *buf++ = *q++;
	    }
	    break;
	 default:  *buf++ = p[1]; break;
	 }
	 p+=2;
      }
      else
	 *buf++ = *p++;
   }
   if( itype&1 && stc == '"' ) *buf++ = '\r';
   *buf = 0;
   if( *p ) p++;
   return p;
}

static
script_com(str)
char * str;
{
extern char logfname[];

   vdisp(0, "<Command> '");
   vdisp(1, str);
   vdisp(0, "'\r\n");
   if( strncmp("timeout=", str, 8) == 0 )
      timeout = atoi(str+8);
   else if( strncmp("starcount=", str, 10) == 0 )
      starcount = atoi(str+10);
   else if( strncmp("cd=", str, 3) == 0 )
      chdir(str+3);
#ifdef LOGFILE
   else if( strncmp("logfile=", str, 8) == 0 )
      strcpy(logfname, str+8);
   else if( strcmp("logstart", str) == 0 )
   {
      if( lfile == 0 ) { printf("\r\n"); open_logfile(); }
   }
   else if( strcmp("logbuf", str) == 0 )
      ltptr = ltbuf;
#endif
   else if( strncmp("con", str, 3) == 0 )
      return 1;
   else if( strcmp("abort", str) == 0 )
   {
      printf("\r\nAborted\r\n");
      cleanup();
   }
   else if( strcmp("end", str) == 0 )
   {
      printf("\r\nScript completed\r\n");
      cleanup();
   }
   else if( strncmp("call ", str, 5) == 0 )
   {
      struct script_sv * p;
      char * s;

      p = (struct script_sv * ) malloc(sizeof(struct script_sv));
      if( p == 0 ) { fprintf(stderr, "Out of memory\r\n"); return 0; }
      p->next = saved_sc;
      p->scrfd = script_file;
      strcpy(p->script_args, script_args);
      s = strchr(str+5, ' ');
      script_file = 0;
      if( s ) { *s = '\0'; strcpy(script_args, s+1); }
      else *script_args = '\0';

      if( open_script(str+5) )
      {
         saved_sc = p;
	 return 2;
      }
      else
      {
         fprintf(stderr, "Cannot open '%s.sct'\r\n", str+5);
	 script_file = p->scrfd;
         strcpy(script_args, p->script_args);
	 free(p);
      }
   }
   else if( strcmp("nobreak", str) == 0 )
   {
      set_mode(2);
      signal(SIGINT, SIG_IGN);
   }
   else if( strcmp("parity", str) == 0 )
   {
      Bit_mask = 0x7F; Parity=1;
   }
   else if( strcmp("sevenbit", str) == 0 )
   {
      Bit_mask = 0x7F;
   }
   else if( strcmp("eightbit", str) == 0 )
   {
      Bit_mask = 0xFF;
   }
   else if( strcmp("rawterm", str) == 0 )
   {
      Termtype = 0;
   }
   else if( strcmp("dumbterm", str) == 0 )
   {
      Termtype = 1;
   }
   else
      fprintf(stderr, "< Error > Command '%s' not found!\r\n", str);

   return 0;
}

static vdisp(flg, str)
int flg;
char * str;
{
   if( verbose != 1 ) return;
   fflush(stdout);
   if( !flg ) fprintf(stderr, "%s", str);
   else while( *str )
   {
      int ch = *str++;
      ch &= 0xFF;
      if( ch >= ' ' && ch != 0x9B && ch != '\\' && ch != 0xFF)
         fputc(ch, stderr);
      else if( ch < ' ' )
         fprintf(stderr, "^%c", ch+'@');
      else if( ch == 0xFF )
         fprintf(stderr, "\\d");
      else
         fprintf(stderr, "\\0x%02x", ch);
   }
}
#endif
