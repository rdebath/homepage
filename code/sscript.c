
#include <stdio.h>

extern int verbose;
extern int script_ticks;

void write_to_line(int ch);

void script_command(int argc, char ** argv);
void script_timeout(void);
void script_scanner(char * buf, int len);

static void fetch_arg(int matchstring);
static void decode_arg(char *p);
static void do_send(void);

int argc;
char ** argv;

int sstate = 0;

char match_buf[256];
int  match_len;

char * sub_string;
int ticker;

char script_buf[256];
int  script_off;

void script_command(int p_argc, char ** p_argv)
{
   argc = p_argc;
   argv = p_argv;

   sstate = 1;
   fetch_arg(1);
}

void script_scanner(char * buf, int len)
{
   int i;
   if (sstate != 1) return;

   for(i=0; i<len; i++) {
      script_buf[script_off++] = buf[i];

      for(;script_off>0;)
      {
         if (memcmp(script_buf, match_buf, script_off) != 0)
         {
	    if ( --script_off )
               memcpy(script_buf, script_buf+1, script_off);
         }
	 else break;
      }
      if (script_off != match_len) continue;

      /* We have a match */
      fetch_arg(0);
      do_send();
      fetch_arg(1);
   }
}

void script_timeout(void)
{
   if (sstate != 1) return;
   if (match_len == 0)
   {
      /* We have an empty match */
      fetch_arg(0);
      do_send();
      fetch_arg(1);
      return;
   }
   if (sub_string == 0) return;

   ticker++;
   if (ticker < script_ticks) return;

   decode_arg(sub_string);
   do_send();
   decode_arg(sub_string);
   ticker = 0;
}

void do_send(void) 
{
   if (!match_len) return;

   if (*match_buf != '-')
   {
      int j;
      for(j=0; j<match_len; j++)
	 write_to_line(match_buf[j]);
   }
   else switch(match_buf[1])
   {
   case 't': script_ticks = 10*atof(match_buf+2); break;
   }
}

static void fetch_arg(int matchstring)
{
   match_len = 0;
   sub_string = 0;
   ticker = 0;

   if (argc == 0) {
      sstate = 0;
      return;
   }

   decode_arg(argv[0]);
   argc--; argv++;
}

static void decode_arg(char *p)
{
   int state = 0;
   match_len = 0;
   sub_string = 0;
   ticker = 0;

   for(; p && *p; p++) 
   {
      if (state)
      {
         if ((*p >='@' && *p<='_') || *p == '?' )
	    match_buf[match_len++] = ('@'^*p);
	 else switch(*p) 
	 {
	 case '~': match_buf[match_len++] = '^'; break;
	 case '-': match_buf[match_len++] = '-'; break;
	 default:
            if (islower(*p))
	       match_buf[match_len++] = *p - '`';
	    break;
	 }
	 state = 0;
      }
      else if (*p == '^') {
         state = 1;
      }
      else if (*p == '-' && match_len) {
         sub_string = p+1;
	 break;
      }
      else
	 match_buf[match_len++] = *p;
   }
   return;
}
