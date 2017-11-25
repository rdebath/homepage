/* (C) 1989-2000 Robert de Bath, Released under the GPL Version 2.
 *
 *
 * usage: term [-s speed] [-l line] [-e escape-char] [-c callscript]
 *      defaults:
 *              speed:  Highest available known speed.
 *              line :  /dev/ttyS0
 *              escape: ~
 *                      escape char may be specified as three octal digits
 *                      If it's not a control character it must be preceded
 *                      by a CR to be active.
 *
 *      term escapes:
 *              escape-.			terminate
 *              escape-!                        escape to shell
 *              escape-#                        send break
 *              escape-7                        Flip 7 bit flag
 *              escape-escape                   send escape char
 *
 * TODO:
 *       Script command to SLIPify link.
 *       Script commands to do other things.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/time.h>
#include <signal.h>
#include <fcntl.h>
#include <ctype.h>
#include <errno.h>
#include <malloc.h>
#include <setjmp.h>
#include <termios.h>
#include <getopt.h>
#ifdef __linux__
#include <sys/ioctl.h>
#include <linux/serial.h>
#endif

#define LINE    "/dev/ttyS0"    /* the default line  */
#define ESC     '~'             /* default esc char  */

#define LOCKDIR	"/var/lock"	/* Directory for lock files. */

#define SCRIPT
#define LOGFILE
#define CHARXLATE
#define XZMODEM
#define XNOLOCK

int  verbose = 0;
int  line_fd = -1;         /* comm line file desc        */
int  tty_ifd = 0;           /* console   file desc        */
int  tty_ofd = 0;           /* console   file desc        */
char line_device[64];
int  line_speed = B9600;
int  custom_speed = 0;	   /* speed for linux */
int  termtype = 0;
int  sevenbit = 0;
int  script_ticks = 20;

int  disable_rts = 0;	   /* Disable RTS/CTS flow control */
int  disable_dtr = 0;	   /* Disable DTR -> disconnect */
int  disable_shell = 0;	   /* Disable shell access */
int  modem_flags = 0;

char esc[] = { ESC, 0 };
char * progname = "term";

char ttl_buf[2048];		/* Buffer for line output */
int  ttl_start = 0;
int  ttl_end = 0;

char ltt_buf[2048];		/* Buffer for terminal output */
int  ltt_start = 0;
int  ltt_end = 0;

#ifdef LOGFILE
FILE * lfile = 0;
char   logfname[256] = "";
#endif

typedef void (*sig_type)(int); 
void open_timeout(int sig);
void killed(int sig);

void s_line(int dolocal); 	/* set & save termios */
void r_line(void); 		/* reset to saved termios */
int conv_speed(char *s);
int conv_esc(char *s);

void cleanup(void);
void fatal(char * str1, char * str2);
void fatalsys(char * str1, char * str2);

void write_to_line(int ch);
void check_zmodem(void);
void script_command(int argc, char ** argv);
void script_timeout(void);
void script_scanner(char * buf, int len);
void Usage(void);

#ifdef CHARXLATE
void converted_terminal(char * buf, int len);
#endif

int main(argc, argv)
int argc;
char **argv;
{
   int done, status, val;
   int temp_fd;
   char * p;

   strcpy(line_device, LINE);
   line_speed = conv_speed(0);
   progname = argv[0];

   tty_ifd = fileno(stdin);
   tty_ofd = fileno(stdout);

   if ( argv[0][0] == '-' ){
      disable_shell = 1;
      // Use environment 
      p = getenv("TERM_DEV");
      if (p && *p) strncpy(line_device, p, sizeof(line_device)-1);
      p = getenv("TERM_SPEED");
      if (p && *p) {
	 val = conv_speed(p);
	 if (val != EOF)
	    line_speed = val;
      }
#ifdef LOGFILE
      p = getenv("TERM_LOG");
      if (p && *p) strncpy(logfname, p, sizeof(logfname)-1);
#endif
   } else {
      while((status = getopt(argc, argv,"s:l:e:vd7RLSraT:t:")) != EOF)
      {
	 switch(status)
	 {
	 case 's':
	    if((line_speed = conv_speed(optarg)) == EOF)
	       fatal("Invalid speed", optarg);
	    break;
	 case 'l':
	    strncpy(line_device, optarg, sizeof(line_device)-1);
	    if(access(line_device, R_OK|W_OK) == -1)
	       fatalsys("Cannot access", optarg);
	    break;

	 case 'e': esc[0] = conv_esc(optarg); break;
	 case 'v': verbose++; break;
	 case '7': sevenbit = 1; break;
	 case 'R': disable_rts = 1; break;
	 case 'L': disable_dtr = 1; break;
	 case 'S': disable_shell = 1; break;

#ifdef CHARXLATE
	 case 'r': termtype = 0; break;
	 case 'd': termtype = 1; break;
	 case 'a': termtype = 2; break;
#endif

#ifdef SCRIPT
	 case 'T': script_ticks = 10*atof(optarg); break;
#endif
#ifdef SCRIPT
	 case 't': strncpy(logfname, optarg, sizeof(logfname)-1);
		   break;
#endif

	 default:
	    Usage();
	 }
      }
#ifdef SCRIPT
      if( optind < argc )
	 script_command(argc-optind, argv+optind);
#else
      if( optind < argc ) Usage();
#endif
   }

   set_mode(0);

   if( !uucp_check(line_device) )
   {
      fprintf(stderr, "Device %s has been locked by another program\n", line_device);
      exit(1);
   }

#ifdef LOGFILE
   if (*logfname) {
      lfile = fopen(logfname, "a");
      if (lfile == 0) {
	 fprintf(stderr, "Cannot open logfile %s\n", logfname);
	 exit(1);
      }
   }
#endif

   signal(SIGALRM, open_timeout);	/* Incase something doesn't follow */
   alarm(5);				/* the rules. */

   if((line_fd = open(line_device, O_RDWR|O_NONBLOCK|O_NOCTTY)) < 0)
   {
      perror(line_device);
      exit(4);
   }
   temp_fd = dup(line_fd);

   s_line(1);
   close(line_fd);
   usleep(20000L);	/* Linux needs this time to cleanup */

   if((line_fd = open(line_device, O_RDWR|O_NOCTTY)) < 0)
   {
      perror(line_device);
      exit(4);
   }
   close(temp_fd);

   if (!disable_dtr)
   {
#ifdef __linux__
      if (ioctl(line_fd, TIOCMGET, &modem_flags) >= 0)
      {
	 if ( !(modem_flags & TIOCM_CAR) )
	    s_line(0);	/*  And turn CLOCAL back off so we can
				       trap when the DCD drops. */
	 else
	    fprintf(stderr, "WARNING: Modem carrier detect disabled.\n");
      }
      else
#endif
	 s_line(0);	/* And turn CLOCAL back off so we can
			      trap when the DCD drops. */
   }

   alarm(0);
   signal(SIGALRM, SIG_DFL);

   signal(SIGINT,  killed);
   signal(SIGQUIT, killed);
   signal(SIGTERM, killed);

   fprintf(stderr, "Connected\n");

   signal(SIGHUP, SIG_IGN);
   set_mode(2);
   while (do_terminal()>=0);

#ifdef LOGFILE
   if( lfile ) fclose(lfile);
#endif
   cleanup();
}

void Usage()
{
#ifdef CHARXLATE
   fprintf(stderr,
	   "usage: %s [-v7RLSrad] [-s speed] [-l line] [-e escape-char]\n", 
	   progname);
#else
   fprintf(stderr,
	   "usage: %s [-v7RLS] [-s speed] [-l line] [-e escape-char]\n", 
	   progname);
#endif
   exit(3);
}

void open_timeout(int signo)
{
   fprintf(stderr, "Cannot open port -- No carrier?\r\n");
   cleanup();
}

void killed(int signo)
{
   fprintf(stderr, "Emulator killed by signal\r\n");
   cleanup();
}

typedef struct
{
   char *str;
   int  spd;
} vspeeds;

static vspeeds valid_speeds[] = 
{
#ifdef B115200
   { "115200", B115200 },
#endif
#ifdef B57600
   { "57600", B57600 },
#endif
#ifdef B38400
   { "38400", B38400 },
#endif
#ifdef B19200
   { "19200", B19200 },
#endif
   { "9600", B9600 },
   { "4800", B4800 },
   { "2400", B2400 },
   { "1200", B1200 },
   { "300",  B300  },
   { (char *)0, 0 }
};

/*
 * given a desired speed string, if valid return its Bspeed value
 * else EOF
 */
int conv_speed(char *s)
{
   vspeeds *p = &valid_speeds[0];

   if(s == 0)  return p->spd;
   if(*s == 0) return EOF;

   for(; p->str != (char *)0; p++)
      if(strncmp(s, p->str, strlen(s)) == 0)
         return p->spd;

#ifdef __linux__
   /* Setserial can set _any_ speed */

   custom_speed = atoi(s);
   if (custom_speed < 50) return EOF;
   return EXTB;  
#endif
   return EOF;
}

/*
 * convert given string to char
 *      string maybe a char, or octal digits
 */
int conv_esc(char *s)
{
   int val = 0;

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

void cleanup()
{
   set_mode(0);

   if( line_fd > 2 )
      fprintf(stderr, "Disconnected\n");

   r_line();
   if( line_fd > 2 ) close(line_fd);
   line_fd = -1;

#ifndef NOLOCK
   uucp_unlock(line_device);
#endif

   exit(0);
}

void
fatal(char * str1, char * str2)
{
   static int doublefault = 0;
   if (doublefault) return;
   doublefault = 1;
   set_mode(0);

   fprintf(stderr, "%s: %s: %s\n", progname, str1, str2);

#ifndef NOLOCK
   uucp_unlock(line_device);
#endif
   exit(1);
}

void
fatalsys(char * str1, char * str2)
{
   char buf[256];
   static int doublefault = 0;
   if (doublefault) return;
   doublefault = 1;
   set_mode(0);

   sprintf(buf, "%s: %s: %s", progname, str1, str2);
   perror(buf);

#ifndef NOLOCK
   uucp_unlock(line_device);
#endif
   exit(1);
}

/*-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-*/

struct termios tty_save, line_save, tty;
static int tty_mode = -1;
int got_line = 0;

void s_line(dolocal)
{
   if(!got_line)
   {
      if( tcgetattr(line_fd, &line_save) < 0 )
         fatalsys("Cannot access line state", line_device);
      got_line = 1;
   }
   tty = line_save;

#ifdef __linux__
   if ( custom_speed )
   {
      struct serial_struct cspeed;
      int new_divisor;
      int got_speed;

      if( ioctl(line_fd, TIOCGSERIAL, &cspeed) < 0 )
         fatalsys("Cannot set custom speed", line_device);

      new_divisor = cspeed.baud_base/custom_speed;
      if( new_divisor < 1 ) fatal("Custom speed too high", line_device);
      if( new_divisor > 65535 ) fatal("Custom speed too low", line_device);
      if( new_divisor != cspeed.custom_divisor &&
          custom_speed != cspeed.baud_base/new_divisor )
	  fprintf(stderr, "Warning: Speed set is %d baud\n", cspeed.baud_base/new_divisor);
      cspeed.custom_divisor = new_divisor;

      cspeed.flags &= ~ASYNC_SPD_MASK;
      cspeed.flags |= ASYNC_SPD_CUST;
      
      if( ioctl(line_fd, TIOCSSERIAL, &cspeed) < 0 )
         fatalsys("Cannot set custom speed", line_device);
   }
#endif

   cfmakeraw(&tty);
#ifdef CBAUDEX
   tty.c_cflag &= ~(CBAUD|CBAUDEX);
#else
   tty.c_cflag &= ~(CBAUD);
#endif

#ifdef CRTSCTS
   if( disable_rts )
      tty.c_cflag &= ~CRTSCTS;
   else
      tty.c_cflag |= CRTSCTS;
#endif

   tty.c_iflag &= ~(IXON|IXOFF);	/* X/Y modem dont like this */
   tty.c_cflag |= HUPCL|(line_speed);

   if( dolocal )
      tty.c_cflag |= CLOCAL;
   else
      tty.c_cflag &= ~CLOCAL;

   tty.c_cc[VTIME] = 0;
   tty.c_cc[VMIN]  = 1;

   if( tcsetattr(line_fd, TCSANOW, &tty) < 0 )
      fatalsys("Cannot set line state", line_device);
}

void r_line()
{
   int fd;

   if(!got_line) return;

   tcflush(line_fd, TCIOFLUSH);

   if( fcntl(line_fd, F_SETFL, O_NONBLOCK) < 0 )
      perror("fnctl call on line");

   if( tcsetattr(line_fd, TCSANOW, &line_save) < 0 )
   {
      /* We may be closing due to an error on the port, so we should try
       * quite hard to reset it.
       */

      close(line_fd);

      if((line_fd = open(line_device, O_RDWR|O_NONBLOCK|O_NOCTTY)) >= 0 )
      {
         if( tcsetattr(line_fd, TCSANOW, &line_save) < 0 )
            perror("Resetting line state");

         tcflush(line_fd, TCIOFLUSH);
      }
   }

#ifdef __linux__
   if ( custom_speed )
   {
      struct serial_struct cspeed;
      if( ioctl(line_fd, TIOCGSERIAL, &cspeed) >= 0 )
      {
         cspeed.custom_divisor = 0;
         cspeed.flags &= ~ASYNC_SPD_MASK;
         ioctl(line_fd, TIOCSSERIAL, &cspeed);
      }
   }
#endif
}

set_mode(which)
int which;
{
    if( tty_mode == -1 )
    {
       if(tcgetattr(tty_ifd, &tty))
	   fatalsys("Unable read terminal state", "");
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

    if(tcsetattr(tty_ifd, TCSANOW, &tty))
	fatalsys("Unable to change terminal mode", "");
    else tty_mode = which;
}

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
   strcpy(nbuf, LOCKDIR "/LCK..");
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
   strcpy(nbuf, LOCKDIR "/LCK..");
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

int
do_terminal()
{
   fd_set rfds, wfds, efds;
   struct timeval tv;
   int rv;

   FD_ZERO(&rfds);
   FD_ZERO(&wfds);
   FD_ZERO(&efds);

   FD_SET(tty_ifd, &rfds);	/* Always want to know about the tty */
   FD_SET(line_fd, &efds);	/* Always want to know about problems */

   if( ttl_start != ttl_end ) FD_SET(line_fd, &wfds);
   if( ltt_start != ltt_end ) FD_SET(tty_ofd, &wfds);
   else		              FD_SET(line_fd, &rfds);

   tv.tv_sec = 0; tv.tv_usec = 100000;
   rv = select(line_fd+1, &rfds, &wfds, &efds, &tv);
   if (rv < 0)
      fatalsys("select syscall","");
#ifdef SCRIPT
   if (!rv) script_timeout();
#endif

#ifdef __linux__
   if ( !disable_dtr && (modem_flags & TIOCM_CAR) )
   {
      if (ioctl(line_fd, TIOCMGET, &modem_flags) >= 0)
      {
	 if ( !(modem_flags & TIOCM_CAR) )
	 {
	    s_line(0);	/*  And turn CLOCAL back off so we can
				    trap when the DCD drops. */
	    fprintf(stderr, "<Local: Modem carrier detect reenabled.>\r\n");
	 }
      }
   }
#endif

   if( FD_ISSET(line_fd, &efds) )
      return -1;
      
   if( FD_ISSET(tty_ifd, &rfds) && read_keys() < 0 ) return -1;

   if( FD_ISSET(line_fd, &wfds) )
   {
      rv = write(line_fd, ttl_buf+ttl_start, ttl_end-ttl_start);
      if( rv > 0 )
      {
         if( rv != ttl_end-ttl_start ) ttl_start += rv;
	 else                          ttl_start = ttl_end = 0;
      } else if (rv < 0)
	  fatalsys("write line_fd syscall","");
   }
   if( FD_ISSET(line_fd, &rfds) )
   {
      rv = read(line_fd, ltt_buf, sizeof(ltt_buf));
      if( rv > 0 )
      {
	 if (sevenbit)
	 {
	    int i;
	    for (i=0; i<rv; i++) ltt_buf[i] &= 0x7F;
	 }
         ltt_end = rv;
#ifdef LOGFILE
         if( lfile ) fwrite(ltt_buf, 1, rv, lfile);
#endif
#ifdef ZMODEM
	 check_zmodem();
#endif
#ifdef SCRIPT
	 script_scanner(ltt_buf, rv);
#endif
#ifdef CHARXLATE
	 if (termtype)
	 {
	    converted_terminal(ltt_buf, rv);
	    ltt_start = ltt_end = 0;
	 }
#endif
      } else if (rv < 0)
	  fatalsys("read line_fd syscall","");
      else if (rv == 0)
	 return -1;
   }
   if( FD_ISSET(tty_ofd, &wfds) )
   {
      rv = write(tty_ofd, ltt_buf+ltt_start, ltt_end-ltt_start);
      if( rv > 0 )
      {
         if( rv != ltt_end-ltt_start ) ltt_start += rv;
	 else                          ltt_start = ltt_end = 0;
      } else if (rv < 0)
	  fatalsys("write terminal syscall","");
   }

   return 0;
}

void write_to_line(int ch) {
   if( ttl_end < sizeof(ttl_buf) )
      ttl_buf[ttl_end++] = ch;
}

read_keys()
{
   char keybuf[256];
   int cc;
   int i;
   int rv = 0;
static int state = 0;

   cc = read(tty_ifd, keybuf, sizeof(keybuf));
   if( cc < 0 && ( errno == EINTR || errno == EWOULDBLOCK ) ) return 0;
   if( cc <= 0 ) { perror("read_keys"); return -1; }

   for(i=0; i<cc; i++)
   {
      int ch = keybuf[i];
#ifdef LOGFILE
      if( lfile ) fprintf(lfile, "%s%d", i?";":"\033[", (unsigned char)ch);
#endif
      switch(state)
      {
      case 0:
      case 1: if( ch == esc[0] && (state==0 || (esc[0]&0xE0) == 0 ) )
	      { 
		 state=2 ;
		 break;
	      }
              if( ch == '\r' ) state=0; else state=1;
	      write_to_line(ch);
	      break;

      case 2: state=0;
              switch(ch)
              {
	      case 'x':
	      case '.':
	      case '>': rv = -1; 
			break;

	      case '!': if (disable_shell)
			   fprintf(stderr, "<Local: shell disabled.> ");
			else {
			   do_shell();
			   fprintf(stderr, "Remote> ");
			}
			break;

	      case '#': fprintf(stderr, "<BREAK>");
			tcsendbreak(line_fd, 0);
			break;

	      case '7': sevenbit = !sevenbit;
			fprintf(stderr, "<Local: Seven bit mode %s>\r\n",
			       sevenbit?"on":"off");
			break;

	      default: if( ch == esc[0] )
	               {
			  write_to_line(ch);
		          state=1;
			  break;
		       }

		       /* ... something else */
		       break;
	      }
	      break;
      }
   }
#ifdef LOGFILE
   if( lfile && i ) fprintf(lfile, "k");
#endif
   return rv;
}

/*-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-*/

#ifdef ZMODEM
char zmodem_buf[20];
char *zptr=zmodem_buf;

char z_request[]   =  "*\030B00";
char z_challange[] = "**\030B0e313233341821\r\n";
char z_charesp[]   =  "*\030B0331323334395b";

/* With this you become a SZModem ver 1.x
 * char z_challange[] = "**\030B0e534d4201053c\r\n";
 * char z_charesp[]   =  "*\030B03424d5301";
 */

void
check_zmodem()
{
   int i;
   for(i=ltt_start;i<ltt_end;i++)
   {
      if(ltt_buf[i] == '*')
      {
	 zptr = zmodem_buf;
	 memset(zmodem_buf, '\0', sizeof(zmodem_buf));
      }
      if( zptr != zmodem_buf+sizeof(zmodem_buf))
	 *zptr++ = ltt_buf[i];
   }
   if( strncmp(zmodem_buf, z_request, sizeof(z_request)-1) == 0 )
   {
      if ( sizeof(z_challange) < sizeof(ttl_buf)-ttl_end )
      {
         memcpy(ttl_buf+ttl_end, z_challange, sizeof(z_challange)-1);
	 ttl_end += sizeof(z_challange)-1;
      }
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
   sig_type oqsig, oisig, otsig, ocsig;

   if( ltt_start!=ltt_end )
      write(tty_ofd, ltt_buf+ltt_start, ltt_end-ltt_start);
   ltt_start=ltt_end = 0;

   set_mode(1);
   fprintf(stderr, "\nAuto-Zmodem\n");
#ifdef LOGFILE
   if( lfile ) fflush(lfile);
#endif
   oqsig = signal(SIGQUIT, SIG_IGN);
   oisig = signal(SIGINT,  SIG_IGN);
   otsig = signal(SIGTERM,  SIG_IGN);
   ocsig = signal(SIGCHLD, SIG_DFL);
   fcntl(line_fd, F_SETFL, 0);

   if ((pid = fork()) == -1 )
   {
      perror("Cannot zmodem");
      set_mode(2);
      fcntl(line_fd, F_SETFL, O_NONBLOCK);
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
      ;

   fcntl(line_fd, F_SETFL, O_NONBLOCK);
   fprintf(stderr, "\nZmodem finished\n");
   set_mode(2);
   (void) signal(SIGQUIT, oqsig);
   (void) signal(SIGINT,  oisig);
   (void) signal(SIGTERM, otsig);
   (void) signal(SIGCHLD, ocsig);
}

#endif

/*-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-*/

do_shell()
{
extern char ** environ;
   int pid, wpid;
   char *shellp = (char *)0;
   int status;
   sig_type oqsig, oisig, otsig;

   if (shellp == (char *)0)
   {
      shellp = getenv("SHELL");
      if (shellp == (char *)0)
         shellp = "/bin/sh";
   }

   oqsig = signal(SIGQUIT, SIG_IGN);
   oisig = signal(SIGINT,  SIG_IGN);
   otsig = signal(SIGTERM,  SIG_IGN);

   fcntl(line_fd, F_SETFL, 0);
   set_mode(0);
   if ((pid=fork()) < 0)
   {
      (void) signal(SIGQUIT, oqsig);
      (void) signal(SIGINT,  oisig);
      (void) signal(SIGTERM, otsig);
      set_mode(2);
      fcntl(line_fd, F_SETFL, O_NONBLOCK);
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
   fcntl(line_fd, F_SETFL, O_NONBLOCK);
   (void) signal(SIGQUIT, oqsig);
   (void) signal(SIGINT,  oisig);
   (void) signal(SIGTERM, otsig);
}

/*-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-*/

#ifdef CHARXLATE
void
dumbch(ch)
int ch;
{
static int cr_pend = 0;

   if( cr_pend )
   {
      cr_pend = 0;
      if( ch == '\n' ) { putchar('\r'); putchar('\n'); return; }
      else { putchar('^'); putchar('M'); }
   }
   if( ch == '\r' )
      cr_pend = 1;
   else
   {
      if( ch & 0x80 )
	 printf("M-");
      if( isprint(ch&0x7F) || ch == '\b' || ch == '\t' )
	 putchar(ch&0x7F);
      else
      {
	 putchar('^');
	 putchar((ch ^ '@')&0x7F);
      }
   }
}

void 
converted_terminal(char * buf, int len)
{
   int i;
   switch(termtype) {
   default: for(i=0; i<len; i++) dumbch(buf[i]); fflush(stdout); break;
   case 2:  for(i=0; i<len; i++) outchar(buf[i]); fflush(stdout); break;
   }
}
#endif

/*-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-*/

