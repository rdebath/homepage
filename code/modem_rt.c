/*
 * This program resets a serial port and attached modem.
 *
 * DO NOT run this program if anything important is using the line as
 * connections will be broken and the modem will be disconnected.
 */

/*
 * TODO:
 *
 * Mode 1: Reset modem port, no locks.
 *
 * Mode 2: Daemon mode:
 *          1) Lock and Reset port.
 *          2) Release lock, wait for DTR, RING etc transition.
 *          3) Repeat.
 *
 * Mode 3: Answer mode:
 *          1) Lock and Reset port.
 *          2) Release lock, wait for DTR, RING etc transition.
 *          3) If RING fork job to port.
 *          4) Repeat.
 */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <signal.h>
#include <string.h>
#include <termios.h>
#include <wait.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <linux/serial.h>

#ifndef LOCK_DIR
#define LOCK_DIR "/var/lock"
#endif

#define atcmd(str)	longatcmd(str, 3)

void  do_connect(char *filename, char **argv);
void  do_reset(char *filename);
void  vhangup_dev(char *dev, int fork);
int   longatcmd(char *str, int timeout);
int   lock_tty(char *tty, int lpid);
void  unlock_tty(char *tty, int lpid);
void  sigdie(int signo);
void  fatal(char *str);

int   line_fd = -1;
int   locked = 0;
int   portio = 0;
int   verbose = 0;
int   nomodem = 0;
int   nolockcheck = 0;
struct termios port_ts;

int   ring_timeout = 60;

char *progname = "";
char *device = 0;

void
main(int argc, char **argv)
{
   int opt;

   progname = argv[0];

   opterr = 0;
#ifdef __GNU_LIBRARY__
   while((opt=getopt(argc, argv, "+vlMt:")) != EOF )
#else
   while((opt=getopt(argc, argv, "vlMt:")) != EOF )
#endif
      switch(opt)
      {
      case 'v': verbose++; break;
      case 'M': nomodem++; break;
      case 'l': nolockcheck++; break;
      case 't': ring_timeout = atoi(optarg); break;
      default: optind=argc; break;
      }

   if (optind >= argc)
   {
      fprintf(stderr, "Usage: %s [-vlM] [-t ringtimout] device [connect command]\n", progname);
      fprintf(stderr, "This program resets the serial device and the modem\n"
	      "that is attached to it. All programs connected to the\n"
	      "port will be disconnected, data transfers, SLIP/PPP\n"
	      "links and modem connections will be terminated.\n\n"
	      "If more than one argument is given the program will\n"
	      "wait 60 seconds for a ring then answer the phone and\n"
	      "run the command connected to the serial line.\n"
	      "\nEg: %s /dev/ttyS0 /bin/login\n",
	      progname);
      exit(1);
   }

   signal(SIGHUP, SIG_IGN);
   signal(SIGINT, sigdie);

   device = argv[optind];
   if (argc > 2 && lock_tty(device, getpid()) < 0)
   {
      if ( ring_timeout <= 0 )
      {
         if (verbose)
	    fprintf(stderr, "Device locked by someone else, waiting ... \n");

         while (lock_tty(device, getpid()) < 0)
	 {
	    if (errno == EEXIST)
	       sleep(10);
	    else
               fatal("Cannot lock modem device");
	 }
      }
      else
         fatal("Cannot lock modem device");
   }
   locked = 1;

   do_reset(device);
   if (argc == optind+1 )
      exit(0);

   do_connect(device, argv + optind + 1);
   do_reset(device);
   unlock_tty(device, getpid());
}

/*****************************************************************************
 *
 */

void
do_connect(char *filename, char **argv)
{
   int   i;
   struct serial_icounter_struct c;
   int   rc, dcdc;
   int   modem_flags;
   int   pid;

   if ((line_fd = open(filename, O_RDWR | O_NONBLOCK | O_NOCTTY)) == -1)
      fatal("Cannot open port for connect");

   if (verbose)
      fprintf(stderr, "Clear DTR\n");
   i = TIOCM_DTR;
   if (ioctl(line_fd, TIOCMBIC, &i) < 0)
      fatal("Cannot clear DTR");

   if (verbose)
      fprintf(stderr, "Waiting for a RING\n");

   /* Wait for ring, do it the boring way */
   if (ioctl(line_fd, TIOCGICOUNT, &c) != 0)
      fatal("Cannot get ring count");
   rc = c.rng;
   dcdc = c.dcd;

   if ( ring_timeout<=0 )
   {
      unlock_tty(device, getpid());
      locked = 0;
   }

   for (i = 0; ring_timeout<=0 || i<ring_timeout; i+=(ring_timeout>0))
   {
      if (ioctl(line_fd, TIOCGICOUNT, &c) < 0)
      {
	 if (errno != EIO)
	    fatal("Failed to reget ring count");
	 close(line_fd);

	 if (verbose)
	    fprintf(stderr, "Reopening modem\n");
	 usleep(20000L);

	 if ((line_fd = open(filename, O_RDWR | O_NONBLOCK | O_NOCTTY)) == -1)
	    fatal("Cannot open port for connect");
	 continue;
      }

      if (rc != c.rng)
	 break;
      if (dcdc != c.dcd)
      {
	 /* Have we had a +ve DCD ? */
	 if (ioctl(line_fd, TIOCMGET, &modem_flags) >= 0)
	 {
	    if (TIOCM_CAR & modem_flags)
	       break;
	 }
	 else
	    fatal("Cannot read modem flags");

	 dcdc = c.dcd;
      }
      sleep(1);
   }

   /* Didn't get a ring ... oh well */
   if (c.rng == rc && dcdc == c.dcd)
   {
      close(line_fd);
      line_fd = -1;
      fatal("The modem didn't RING!");
   }

   if ( ring_timeout<=0 && lock_tty(device, getpid()) < 0)
   {
      if (verbose)
	 fprintf(stderr, "RING! But somebody else has the line! Waiting...\n");
      while (lock_tty(device, getpid()) < 0 && errno == EEXIST)
	 sleep(10);

      locked = 1;
      fatal("Somebody else had the modem when it rang");
   }

   if (dcdc != c.dcd || longatcmd("A", 45) == 3)
   {
      if (verbose)
	 fprintf(stderr, "Set DTR & RTS\n");
      i = TIOCM_DTR + TIOCM_RTS;
      ioctl(line_fd, TIOCMBIS, &i);

      if (verbose)
	 fprintf(stderr, "Spawning program\n");

      if ((pid = fork()) == 0)
      {
	 struct termios getty_ts;

	 close(line_fd);
	 line_fd = -1;
	 vhangup_dev(filename, 0);

	 signal(SIGHUP, SIG_DFL);

	 line_fd = open(filename, O_RDWR);
	 if (line_fd < 0)
	    perror("Cannot reopen stdin");	/* Hmm, vhangup() */

	 ioctl(line_fd, TIOCSCTTY, (void *) 1);
	 dup2(line_fd, 0);
	 dup2(line_fd, 1);
	 dup2(line_fd, 2);
	 close(line_fd);
	 line_fd = -1;

	 tcflush(0, TCIOFLUSH);

	 /* Set line to cooked */
	 if (tcgetattr(0, &getty_ts) != 0)
	    perror("Cannot tcgetattr");
	 getty_ts.c_lflag |= (ICANON | ISIG | ECHO);
	 getty_ts.c_oflag |= (OPOST);
	 getty_ts.c_cc[VEOF] = '\004';
	 getty_ts.c_cc[VERASE] = '\010';
	 getty_ts.c_cc[VINTR] = '\003';
	 getty_ts.c_cc[VKILL] = '\025';
	 getty_ts.c_cc[VQUIT] = '\034';
	 getty_ts.c_cc[VSUSP] = '\032';
	 if (tcsetattr(0, TCSANOW, &getty_ts) != 0)
	    perror("Cannot setup the line");

	 execvp(argv[0], argv);
	 exit(255);
      }
      if (pid < 0)
	 fatal("Forking problem");

      wait(&i);
   }
   else
      fatal("The modem didn't CONNECT");

   close(line_fd);
   line_fd = -1;			       /* vhangup will kill this */
}

/*****************************************************************************
 *
 */

void
do_reset(char *filename)
{
   int   line_disc;
   int   modem_ok;
   int   i;

   portio = 0;

   if (verbose)
      fprintf(stderr, "Opening port\n");
   /* Open communication file device. */
   if ((line_fd = open(filename, O_RDWR | O_NONBLOCK | O_NOCTTY)) == -1)
      fatal("Cannot open port");
   if (flock(line_fd, LOCK_EX | LOCK_NB) != 0)
      if (!nolockcheck)
         fatal("Cannot lock device");

   /* This checks we have a real serial port, actually a hard reset will work
    * on any tty device, but the modem stuff obviously can't
    */
   if (ioctl(line_fd, TIOCMGET, &i) < 0 )
   {
      if (errno == EINVAL)
      {
	 errno = 0;
	 fatal("Device is not a serial port");
      }
      else
         fatal("Cannot access modem control information");
   }

   close(line_fd);
   line_fd = -1;

   if (verbose)
      fprintf(stderr, "Doing vhangup\n");
   vhangup_dev(filename, 1);
   sleep(1);

   if (verbose)
      fprintf(stderr, "Opening port\n");
   /* Open communication file device. */
   if ((line_fd = open(filename, O_RDWR | O_NONBLOCK | O_NOCTTY)) == -1)
      fatal("Cannot open port");
   if (!nolockcheck)
      if (flock(line_fd, LOCK_EX | LOCK_NB) != 0)
         fatal("Cannot lock device");

   /* Clear the port if it's in slip/ppp mode */
#ifdef TIOCSETD
   line_disc = -1;
   ioctl(line_fd, TIOCGETD, &line_disc);
   if (line_disc != N_TTY)
   {
      if (verbose)
	 fprintf(stderr, "Cleared PPP/SLIP mode\n");
      line_disc = N_TTY;
      ioctl(line_fd, TIOCSETD, &line_disc);
   }
#endif

   if (verbose)
      fprintf(stderr, "Putting port into raw mode\n");
   if (tcgetattr(line_fd, &port_ts) != 0)
      fatal("Cannot tcgetattr");

   cfmakeraw(&port_ts);

   /* These may interfere with correct operation */
   port_ts.c_iflag &= ~(INPCK | IXON | IXOFF);
   port_ts.c_cflag &= ~(PARODD | CSTOPB | HUPCL | CRTSCTS);
   port_ts.c_cflag |= (CREAD | CLOCAL);

   /* This is what we want for now. */
   port_ts.c_cc[VTIME] = 1;
   port_ts.c_cc[VMIN] = 0;

   if (tcsetattr(line_fd, TCSANOW, &port_ts) != 0)
      fatal("Cannot tcsetattr");
   close(line_fd);
   line_fd = -1;

   if (verbose)
      fprintf(stderr, "Re-opening port\n");
   if ((line_fd = open(filename, O_RDWR | O_NONBLOCK | O_NOCTTY)) == -1)
      fatal("Cannot reopen port");
   if (!nolockcheck)
      if (flock(line_fd, LOCK_EX | LOCK_NB) != 0)
         fatal("Cannot lock device");

   if (verbose)
      fprintf(stderr, "Doing DTR hangup\n");
   {				       /* Drop DTR for a second using the
				        * standard method. Not Linux
				        * ioctls. */
      struct termios hupem = port_ts;

      hupem.c_cflag &= ~(CBAUD | CLOCAL);
      hupem.c_cflag |= HUPCL;
      hupem.c_cflag |= B0;
      if (tcsetattr(line_fd, TCSANOW, &hupem) != 0)
	 fatal("Cannot hup the line");
   }
   close(line_fd);
   line_fd = -1;
   sleep(1);

   if (verbose)
      fprintf(stderr, "Re-opening port\n");
   if ((line_fd = open(filename, O_RDWR | O_NONBLOCK | O_NOCTTY)) == -1)
      fatal("Cannot reopen port");
   if (flock(line_fd, LOCK_EX | LOCK_NB) != 0)
      fatal("Cannot lock port, somebody else got there first");

   if (tcsetattr(line_fd, TCSANOW, &port_ts) != 0)
      fatal("Cannot clear hangup");

   /* Okay, the port should now be cleared. Time to do the modem. */
   /* It should be offline but ... */

   if (!nomodem)
   {
      if (verbose)
	 fprintf(stderr, "Making sure modem is in command mode\n");

      write(line_fd, "+++", 3);
      sleep(1);
   }

   /*
    * Ok now the modem will respond to speed changes so we can reset the
    * speed of the serial port
    */
   if (verbose)
      fprintf(stderr, "Speed to 115k2\n");
   port_ts.c_cflag &= ~(CBAUD | CBAUDEX);
   port_ts.c_cflag |= B115200;
   if (tcsetattr(line_fd, TCSANOW, &port_ts) != 0)
      fatal("Cannot reset speed");

   /*
    * Now have at the modem, factory reset it and toggle DCD to make sure
    * the kernel serial stuff is all reset too.
    */

   if (nomodem)
      modem_ok = 1;
   else
   {
      if (verbose)
	 fprintf(stderr, "Resetting modem\n");
      for (modem_ok = 0, i = 0; i < 3; i++)
      {
	 /* First time try a quick reset */
	 if (i == 0)
	    modem_ok = atcmd("&F E0V1Q0");
	 else if (verbose)
	    fprintf(stderr, "Modem reset failed ... retrying\n");

	 if (!modem_ok)
	 {
	    longatcmd("&F", 8);	       /* May not respond with OK */
	    modem_ok = atcmd("E0V1Q0");/* But this should */
	 }

	 if (modem_ok)
	 {
	    atcmd("H");		       /* Some modems the connection
				        * survives even a factory reset */
	    if (atcmd("&C0"))	       /* Set the DCD line */
	       sleep(1);
	    atcmd("&C1&D2");	       /* And clear */
	    modem_ok = atcmd("");      /* Still happy ? */
	 }
	 if (modem_ok)
	    break;		       /* Ahhhh */
      }

      /* Put the modem into user defined mode; this may not give an OK */
      if (modem_ok)
      {
	 atcmd("Z");
	 atcmd("E0");	/* Echo is _BAAAD_ for unix connected modems */
      }
   }

   /* And finally make the port standard */
   fcntl(line_fd, F_SETFL, O_NONBLOCK);

   if (verbose)
      fprintf(stderr, "Putting serial port into safe state\n");

   port_ts.c_cc[VTIME] = 0;
   port_ts.c_cc[VMIN] = 1;
   port_ts.c_cflag &= ~CLOCAL;
   port_ts.c_cflag |= (HUPCL | CRTSCTS);
   if (tcsetattr(line_fd, TCSANOW, &port_ts) != 0)
      fatal("Cannot do final set");

   /* Clear any junk */
   tcflush(line_fd, TCIOFLUSH);
   close(line_fd);
   line_fd = -1;

   if (!modem_ok)
      fatal("Modem didn't respond properly");

   if (verbose)
      fprintf(stderr, "Reset done\n");
   portio = 1;
}

/*****************************************************************************
 *
 */

int
longatcmd(char *str, int timeout)
{
   char  ibuf[256];
   int   tcount = 0;
   int   offt = 0;
   int   rv = 0;
   time_t start, now;

   /* Clear any pending junk */
   tcflush(line_fd, TCIOFLUSH);

   if (nomodem)
      return 1;

   if (verbose)
      fprintf(stderr, "Send: AT%s\\r --> ", str);
   sprintf(ibuf, "AT%s\r", str);
   write(line_fd, ibuf, strlen(ibuf));

   time(&start);
   errno = 0;

   while (offt < sizeof(ibuf) - 1 && tcount < 1000)
   {
      int   cc;
      tcount++;
      cc = read(line_fd, ibuf + offt, sizeof(ibuf) - offt);
      if (cc > 0)
      {
	 while (cc > 0)
	 {
	    if (verbose)
	    {
	       if (ibuf[offt] >= 127 || ibuf[offt] < 0 ||
		   ibuf[offt] == '\\' || ibuf[offt] == '^')
		  fprintf(stderr, "\\%03o", ibuf[offt] & 0xFF);
	       else if (ibuf[offt] < ' ')
		  fprintf(stderr, "^%c", ibuf[offt] + '@');
	       else
		  fprintf(stderr, "%c", ibuf[offt]);
	    }
	    if (ibuf[offt] == '\0')
	       ibuf[offt] = ' ';
	    offt++;
	    cc--;
	 }
	 ibuf[offt] = 0;
	 if (strstr(ibuf, "OK\r"))
	    rv = 1;
	 if (strstr(ibuf, "NO CARRIER\r"))
	    rv = 2;
	 if (strstr(ibuf, "CONNECT"))
	    rv = 3;
	 if (rv)
	 {
	    if (verbose)
	       fprintf(stderr, "--> %d\n", rv);
	    return rv;
	 }
      }
      else if (cc < 0 && errno == EWOULDBLOCK)
      {
	 errno = 0;
	 usleep(100000);
      }
      else if (cc < 0)
	 fatal("Cannot read AT response");
      time(&now);
      if (now > start + timeout)
      {
	 if (verbose)
	    fprintf(stderr, "--> 0\n");
	 return 0;
      }
   }
   if (verbose)
      fprintf(stderr, "--> JUNK!\n");
   return 0;
}

/*****************************************************************************
 * Create a lockfile - remove invalid leave ours
 */

int
lock_tty(char *tty, int lpid)
{
   FILE *fd;
   char  pbuf[260], lbuf[260];
   int   pid = 0;
   char *p;

   if ((p = strrchr(tty, '/')))
      tty = p + 1;

   sprintf(pbuf, LOCK_DIR "/PID..%d", getpid());
   sprintf(lbuf, LOCK_DIR "/LCK..%s", tty);

   fd = fopen(pbuf, "w");
   if (fd == 0)
      return -1;
   fprintf(fd, "%10d\n", lpid);
   fclose(fd);

   if (link(pbuf, lbuf) < 0)
   {
      fd = fopen(lbuf, "r");
      if (fd && fscanf(fd, "%d", &pid) == 1 && pid > 0)
      {
	 fclose(fd);
	 if (pid == lpid)	       /* Ooops it's me */
	 {
	    unlink(pbuf);
	    return 0;
	 }
	 if (kill(pid, 0) != 0 && errno != EPERM)
	    unlink(lbuf);
      }
      else if (fd)
	 fclose(fd);

      if (link(pbuf, lbuf) < 0)
      {
	 unlink(pbuf);
	 return -1;
      }
   }
   unlink(pbuf);
   return 0;
}

/*****************************************************************************
 * Remove our lockfile (ONLY ours!)
 */

void
unlock_tty(char *tty, int lpid)
{
   FILE *fd;
   char  lbuf[260];
   int   pid = 0;
   char *p;

   if ((p = strrchr(tty, '/')))
      tty = p + 1;
   sprintf(lbuf, LOCK_DIR "/LCK..%s", tty);

   fd = fopen(lbuf, "r");
   if (fd && fscanf(fd, "%d", &pid) == 1 && pid == lpid)
   {
      fclose(fd);
      unlink(lbuf);
   }
   else if (fd)
      fclose(fd);
}

/*****************************************************************************
 * Do a vhangup on the line.
 */

void
vhangup_dev(char *portname, int do_fork)
{
   int   port_fd;

   /* If not root don't bother trying */
   if (geteuid()) return;

   if (do_fork && fork())
   {
      wait(&port_fd);
      return;
   }

   port_fd = open("/dev/tty", 0);
   if (port_fd >= 0)
   {
      ioctl(port_fd, TIOCNOTTY, 0);
      close(port_fd);
      port_fd = -1;
   }

   signal(SIGHUP, SIG_IGN);
   setsid();

   port_fd = open(portname, O_RDWR | O_NONBLOCK);
   if (port_fd >= 0)
   {
      ioctl(port_fd, TIOCSCTTY, (void *) 1);

      if (vhangup() < 0)
      {
	 perror("vhangup");
	 exit(1);
      }
      close(port_fd);
      port_fd = -1;
   }
   else
      perror("vhangup-open");

   if (do_fork)
      exit(0);
}

/*****************************************************************************
 * Fatal error -- die quickly.
 */

void
sigdie(int signo)
{
   fatal("Killed by signal");
}

void
fatal(char *str)
{
   char  ebuf[256];

   strcpy(ebuf, progname);
   strcat(ebuf, ": ");
   strcat(ebuf, str);
   if (device)
   {
      strcat(ebuf, ": ");
      strcat(ebuf, device);
   }
   if (errno == 0)
   {
      strcat(ebuf, "\n");
      write(2, ebuf, strlen(ebuf));
   }
   else
      perror(ebuf);

   if (line_fd > 0)
      close(line_fd);
   line_fd = -1;

   if (portio)
   {
      if ((line_fd = open(device, O_RDWR | O_NONBLOCK | O_NOCTTY)) != -1)
      {
	 (void) tcsetattr(line_fd, TCSANOW, &port_ts);
	 (void) tcflush(line_fd, TCIOFLUSH);
	 close(line_fd);
	 line_fd = -1;
      }
   }

   if (locked)
      unlock_tty(device, getpid());

   exit(1);
}
