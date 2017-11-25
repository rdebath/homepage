
#include <stdio.h>
#include <ctype.h>
#include <string.h>

FILE * ifd = stdin;

char buf[2000];
char * bufptr = buf;

int litcharcnt = 0;
int vflag = 1;
int outoff = 0;

main(argc, argv)
int argc;
char ** argv;
{
   if( argc < 2 ) hplj_splt();
   else
   {
      ifd = fopen(argv[1], "r");
      if( ifd )
      {
	 hplj_splt();
	 fclose(ifd);
      }
      else
	 printf("Cannot open '%s'\n", argv[1]);
   }
   if(vflag) printf("\n");
   printf("Litchars: %d\n", litcharcnt);
}

hplj_splt()
{
static char seq1[]   = "$%&()*";

   int state = 0;
   int litmode = 0;
   int skipcnt = 0;
   int litc = 0;
   int ch;
   int pos = 0;

   char bl[4];
   char arg[32];
   int argoff=0;

   hex_output(EOF);

   while((ch=getc(ifd)) != EOF)
   {
      if( skipcnt > 0 )
      {
	 if(vflag) hex_output(ch);
	 skipcnt--;
	 if( skipcnt == 0 ) hex_output(EOF);
	 continue;
      }
      pos = 0;
      if( ch == '\033' && state != 0 )
      {
	 printf("\t\tESC found - Aborted continuation");
	 state=0;
      }
      if( state ) output(ch);
      switch(state)
      {
      case 0: if( ch != '\033' )
	      {
                 if( litmode == 1 )
                 {
                    decode_hpgl(ch);
                    break;
                 }
		 litcharcnt++,litc++;
		 if(vflag) hex_output(ch);
	      }
	      else
	      {
                 if( litmode == 1 )
                    decode_hpgl(EOF);
		 else if( litc )
		 {
		    hex_output(EOF);
		    if(vflag) printf("\nLits %d", litc); litc=0;
		 }
		 state = 1;
		 output(ch);
	      }
	      break;
      case 1:
	      memset(bl, '\0', sizeof(bl)); *arg = '\0';
	      bl[0] = ch;
	      if( strchr(seq1, ch) ) state=2;
	      else
	      {
		 if( vflag ) translate(bl, arg);
	         state = 0;
	      }
	      break;
      case 2: argoff=0;
              if( islower(ch) ) { bl[1] = ch; state = 3; break; }

      case 3: if( ch == '.' || ch == '+' || ch == '-' || (ch >= '0' && ch <= '9'))
	      {
		 if( argoff != 0 && ( ch == '+' || ch == '-' ))
		 {
		    if(vflag) printf("\\!");
		    state = 0;
		 }
		 else
		 {
                    arg[argoff++] = ch;
		    if( argoff >= sizeof(arg) ) argoff = sizeof(arg)-1;
		    state = 3;
		 }
                 break;
	      }
	      if( isalpha(ch) || ch == '@' )
	      {
		 bl[2] = toupper(ch);
                 arg[argoff] = '\0';
		 if( strcmp(bl, ")sW") == 0
		  || strcmp(bl, "(sW") == 0
		  || strcmp(bl, "(fW") == 0
		  || strcmp(bl, "*bW") == 0
		  || strcmp(bl, "*cW") == 0
		  || strcmp(bl, "&pX") == 0)
		 {
		    skipcnt = atoi(arg);
		 }
		 if( vflag ) translate(bl, arg);

                 if( memcmp(bl, "%\0X", 3) == 0 ) litmode=2;
                 if( memcmp(bl, "%\0A", 3) == 0 ) litmode=0;
                 if( memcmp(bl, "%\0B", 3) == 0 ) litmode=1;

	         if( islower(ch) ) { state = 3; argoff = 0; }
	         else state= 0;

		 if(vflag && state) outoff = printf("\n...   ") - 1;
	      }
	      else
	      {
		 if(vflag) printf("\\!");
		 state = 0;
	      }
	      break;
      }
   }
   hex_output(EOF);
   if( litc ) { if(vflag) printf("\nLits %d", litc); litc=0; }
}

output(ch)
{
   if(!vflag) return;
   if( ch == '\033' ) { putchar('\n'); outoff=0; }
   if( ch >= ' ' && ch <= '~' && ch != '\\')
   {
      putchar(ch); outoff++;
   }
   else
   {
      outoff += printf("\\%03o", ch);
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
	 printf("\n: %.66s", linebuf);
      memset(linebuf, ' ', sizeof(linebuf));
      pos = 0;
   }
   else
   {
      if(!vflag) return;

      sprintf(buf, "%02x", ch&0xFF);
      memcpy(linebuf+pos*3, buf, 2);
      if( ch > ' ' && ch <= '~' ) linebuf[50+pos] = ch;
      else  linebuf[50+pos] = '.';
      pos = ((pos+1) & 0xF);
      if( pos == 0 )
      {
         printf("\n: %.66s", linebuf);
         memset(linebuf, ' ', sizeof(linebuf));
      }
   }
}

char *xlate[] =
{
   "%%-12345X",	"PCL Escape to HP-JCL",
   "%%%gA",	"Enter PCL mode (%s) from HPGL",
   "%%%gB",	"Enter HPGL mode (%s) from PCL",

   "9",		"Clear margins",
   "E",		"Reset printer",
   "z",		"Self Test",
   "=",		"Half line feed",
   "Y",		"Turn display functions mode on",
   "Z",		"Turn display functions mode off (default)",

   "&l1A",	"Page size Executive",
   "&l2A",	"Page size Letter",
   "&l3A",	"Page size Legal",
   "&l26A",	"Page size A4",
   "&l80A",	"Page size Envelope Monarch",
   "&l81A",	"Page size Envelope COM 10",
   "&l90A",	"Page size Envelope DL",
   "&l91A",	"Page size Envelope C5",
   "&l%gA",	"Page size type %s",

   "&l%gC",	"Set vertical motion index to '%s' 1/48\" increments",
   "&l%gD",	"Set lines per inch to '%s'",
   "&l%gE",	"Set top margin to '%s' lines",
   "&l%gF",	"Set text length to '%s' lines",
   "&l1G",	"Output paper to upper bin",
   "&l2G",	"Output paper to back",
   "&l0H",	"Eject page",
   "&l1H",	"Feed paper from upper tray",
   "&l2H",	"Feed paper manually",
   "&l3H",	"Feed manual envelope",
   "&l4H",	"Feed paper from 4Si lower tray",
   "&l5H",	"Feed from automatic envelope feeder",
   "&l0L",	"Disable perforation skip",
   "&l1L",	"Enable perforation skip",
   "&l0O",	"Portrait orientation",
   "&l1O",	"Landscape orientation",
   "&l2O",	"Reversed portrait orientation",
   "&l3O",	"Reversed landscape orientation",
   "&l%gO",	"Orientation %s",
   "&l%gP",	"Set page length to '%s' lines",
   "&l0T",	"Default stacking position",
   "&l1T",	"Togglestacking position",
   "&l%gX",	"Select '%s' number of copies",
   "&l%gZ",	"Top offset of %s decipoints",
   "&l%gU",	"Left offset of %s decipoints",

   "&l1T",	"Job separator",

   "&l0S",	"Simplex printing",
   "&l1S",	"Dulpex with long edge binding",
   "&l2S",	"Dulpex with short edge binding",

   "&u%gD",	"Set unit of measure to %s",

   "(0U",	"Select US-ASCII symbol set",
   "(0N",	"Select ISO 8859-1 symbol set",
   "(8U",	"Select Roman 8 symbol set",
   "(9U",	"Select Windows symbol set",
   "(10U",	"Select PC-8 symbol set",
   "(12U",	"Select IBM 850 symbol set",

   "(0D",	"Select ISO 60 Denmark/Norway symbol set",
   "(1E",	"Select ISO 4  United Kingdom symbol set",
   "(1F",	"Select ISO 69 French symbol set",
   "(1G",	"Select ISO 21 German symbol set",
   "(0I",	"Select ISO 15 Italian symbol set",
   "(6J",	"Select Microsoft publishing symbol set",
   "(7J",	"Select Desktop symbol set",
   "(10J",	"Select Postscript symbol set",
   "(13J",	"Select Ventura Intl symbol set",
   "(14J",	"Select Ventura US symbol set",
   "(9L",	"Select Ventura Zapf dingbats symbol set",

   "(8K",	"Select Kana 8 symbol set",
   "(8M",	"Select Math 8 symbol set",
   "(0B",	"Select Line Draw symbol set",
   "(0A",	"Select Math symbol set",
   "(0M",	"Select Math 7 symbol set",
   "(0Q",	"Select Math 8a symbol set",
   "(1Q",	"Select Math 8b symbol set",
   "(1U",	"Select US Legal symbol set",
   "(0E",	"Select Roman Extension symbol set",
   "(0F",	"Select ISO France symbol set",
   "(0G",	"Select ISO German symbol set",
   "(0S",	"Select ISO Sweden/Finland symbol set",
   "(1S",	"Select ISO Spain symbol set",
   "(2Q",	"Select PiFonta symbol set",
   "(15U",	"Select PiFont symbol set",
   "(19U",	"Windows 3.1 'Latin 1' symbol set",
   "(19M",	"Select 'Symbol' symbol set",
   "(579L",	"Select Windings symbol set",

   "(s0P",	"Select fixed space font",
   "(s1P",	"Select proportional font",
   "(s0S",	"Print style Upright",
   "(s1S",	"Print style Italic",
   "(s4S",	"Print style Condensed",
   "(s5S",	"Print style Condensed Italic",
   "(s8S",	"Print style Compressed",
   "(s24S",	"Print style Expanded",
   "(s32S",	"Print style Outline",
   "(s64S",	"Print style Inline",
   "(s128S",	"Print style Shadowed",
   "(s160S",	"Print style Outline Shadowed",
   "(s%gS",	"Print style to number %s",

   "(s%gB",	"Set stroke weight to '%s', (7=bold, -7=light)",

   "(s0T",	"Select Line Printer font",
   "(s1T",	"Select Pica font",
   "(s2T",	"Select Elite font",
   "(s3T",	"Select Courier font",
   "(s4T",	"Select Cartridge Helvetica font",
   "(s5T",	"Select Cartridge Times Roman (TMS RMN) font",
   "(s6T",	"Select Cartridge Gothic font",
   "(s7T",	"Select Cartridge Script font",
   "(s8T",	"Select Cartridge Prestige font",

   "(s4099T",	"Select LJ4 font Courier",
   "(s4101T",	"Select LJ3 font CG Times",
   "(s4102T",	"Select LJ4 font Letter Gothic",
   "(s4113T",	"Select LJ4 font CG Omega",
   "(s4116T",	"Select LJ4 font Coronet",
   "(s4140T",	"Select LJ4 font Clarendon Condensed",
   "(s4148T",	"Select LJ3 font Univers",
   "(s4168T",	"Select LJ4 font Antique Olive",
   "(s4197T",	"Select LJ4 font Garamond",
   "(s4297T",	"Select LJ4 font Marigold",
   "(s4362T",	"Select LJ4 font Albertus",
   "(s16602T",	"Select LJ4 font Arial",
   "(s16901T",	"Select LJ4 font Times New",
   "(s16686T",	"Select LJ4 font Symbol",
   "(s31402T",	"Select LJ4 font Wingdings",

   "(s%gT",	"Select font number %s",

   ")s%gW",	"Create font header",
   "(s%gW",	"Download character",
   "(%gX",	"Designate downloaded font %s as primary",
   ")%gX",	"Designate downloaded font %s as secondary",
   "(%g@",	"Primary font default",
   ")%g@",	"Secondary font default",

   "(s16.6H",	"Set 16.66 pitch",
   "(s%gH",	"Set %s pitch",
   "(s%gV",	"Set point size to %s",

   "*b0M",	"Graphic data is uncoded",
   "*b1M",	"Graphic data is RLE encoded",
   "*b2M",	"Graphic data is TIFF encoded",
   "*b3M",	"Graphic data is Delta row encoded",
   "*b4M",	"Graphic data is Adaptive compressed",
   "*b%gM",	"Graphic data is in encoding mode '%s'",
   "*b%gY",	"Raster Y offset to '%s'",
   "*b%gW",	"Transfer '%s' byte raster image",

   "*c%gD",	"Specify font ID '%s'",
   "*c%gE",	"Specify character code '%s'",
   "*c0F",	"Delete all fonts, including permanent",
   "*c1F",	"Delete all temporary fonts",
   "*c2F",	"Delete last font ID specified",
   "*c3F",	"Delete last character code and font ID specified",
   "*c4F",	"Make last font ID temporary",
   "*c5F",	"Make last font ID permanent",
   "*c6F",	"Copy or assign last font ID specified",
   "*c7F",	"Reestablish ROM",
   "*c8F",	"Set primary font",
   "*c9F",	"Set secondary font",
   "*c10F",	"Set primary and secondary font default",

   "*v0T",	"Select pattern solid black as current",
   "*v1T",	"Select pattern solid white as current",
   "*v2T",	"Select HP-shading pattern as current",
   "*v3T",	"Select HP-cross pattern hatch as current",
   "*v4T",	"Select user defined pattern as current",
   "*v%gT",	"Select pattern %s as current",

   "&a%gL",	"Set left margin to column '%s'",
   "&a%gM",	"Set right margin to column '%s'",
   "&a%gR",	"Move to row '%s'",
   "&a%gC",	"Move to col '%s'",
   "&a%gH",	"Move to horizontal position '%s' decipoints",
   "&a%gV",	"Move to vertical position '%s' decipoints",

   "&a0G",	"Print on next side",
   "&a1G",	"Print on front side",
   "&a2G",	"Print on back side",
   "&a%gG",	"Print offside (%s)",

   "&a%gP", 	"Print direction %s degrees",

   "*p%gX",	"Move to horizontal position '%s' dots",
   "*p%gY",	"Move to vertical position '%s' dots",

   "*t%gR",	"Select %s dots per inch graphics mode",

   "*r0A",	"Start graphics at left most position",
   "*r1A",	"Start graphics at current cursor",
   "*r%gA",	"Start graphics at position %s",
   "*r0B",	"End graphics",
   "*r%gF",	"Rotate graphics image to posn %s",
   "*r%gS",	"Raster width = %s pixels",
   "*r%gT",	"Raster height = %s pixels",

   "*c%gA",	"Set horizontal rectangle size '%s' dots",
   "*c%gH",	"Set horizontal rectangle size '%s' decipoints",
   "*c%gB",	"Set vertical rectangle size '%s' dots",
   "*c%gV",	"Set vertical rectangle size '%s' decipoints",
   "*c0P",	"Fill black rectangle",
   "*c1P",	"Fill white rectangle",
   "*c2P",	"Fill gray scale pattern",
   "*c3P",	"Fill Cross-hatch pattern",
   "*c4P",	"Fill User defined pattern",
   "*c5P",	"Fill current pattern",
   "*c1G",	"Vertical lines pattern",
   "*c2G",	"Horizontal lines pattern",
   "*c3G",	"Diagonal lines pattern (upward left to right)",
   "*c4G",	"Diagonal lines pattern (downward left to right)",
   "*c5G",	"Horizontal/vertical grid lines pattern",
   "*c6G",	"Diagonal grid pattern",
   "*c%gG",	"Set grey scale pattern, of %s%% grey",

   "*c%gK",	"HP-GL horizontal size = %s inches",
   "*c%gL",	"HP-GL vertical size = %s inches",
   "*c0T",	"HP-GL anchor at cursor pos",
   "*c%gX",	"HP-GL horizontal size = %s decipoints",
   "*c%gY",	"HP-GL vertical size = %s decipoints",

   "&f0S",	"Push cursor position",
   "&f1S",	"Pop cursor position",
   "&f%gY",	"Select macro with ID '%s'",
   "&f0X",	"Start macro definition",
   "&f1X",	"Stop macro definition",
   "&f2X",	"Execute current macro",
   "&f3X",	"Call current macro",
   "&f4X",	"Enable auto macro overlay",
   "&f5X",	"Disable auto macro overlay",
   "&f6X",	"Delete all macros",
   "&f7X",	"Delete all temporary macros",
   "&f8X",	"Delete macro ID",
   "&f9X",	"Make macro temporary",
   "&f10X",	"Make macro permanent",

   "&d0D",	"Set underline on",
   "&d3D",	"Set floating underline on",
   "&d0@",	"Set underline off",

   "&p%gX",	"Disable command interpretation for the '%s' bytes following this command",


   "&k0G",	"Set line terminators to CR=CR, LF=LF, FF=FF",
   "&k1G",	"Set line terminators to CR=CR+LF, LF=LF, FF=FF",
   "&k2G",	"Set line terminators to CR=CR, LF=CR+LF, FF=CR+FF",
   "&k3G",	"Set line terminators to CR=CR+LF, LF=CR+LF, FF=CR+FF",
   "&k%gH",	"Set horizontal motion index to (120.0/%s) cpi",

   "&k0S",	"Pitch mode normal",
   "&k2S",	"Pitch mode Compressed (16.5-16.7)",
   "&k4S",	"Pitch mode Elite (12.0)",

   "&s0C",	"Enable end of line wrap",
   "&s1C",	"Disable end of line wrap",

   ")%gA",	"Select symbol set for secondary font",
   ")%gB",	"Select symbol set for secondary font",
   ")%gD",	"Select symbol set for secondary font",
   ")%gE",	"Select symbol set for secondary font",
   ")%gF",	"Select symbol set for secondary font",
   ")%gI",	"Select symbol set for secondary font",
   ")%gJ",	"Select symbol set for secondary font",
   ")%gK",	"Select symbol set for secondary font",
   ")%gL",	"Select symbol set for secondary font",
   ")%gM",	"Select symbol set for secondary font",
   ")%gN",	"Select symbol set for secondary font",
   ")%gU",	"Select symbol set for secondary font",
   ")%gQ",	"Select symbol set for secondary font",
   ")s%gP",	"Select spacing for secondary font",
   ")s%gS",	"Select style for secondary font",
   ")s%gB",	"Select boldness for secondary font",
   ")s%gT",	"Select typeface for secondary font",

   0
};

translate(bl, arg)
char * bl, *arg;
{
   char buf1[20];
   char buf2[20];
   char argx[20];
   char **ptr;
   double argn = 0.0;
   extern double atof();


   buf1[0] = bl[0];
   buf1[1] = bl[1];
   buf1[2] = '\0';

   argn = atof(arg);
   if( arg[0] == '+' )
      sprintf(argx, "+%g", argn);
   else
      sprintf(argx, "%g", argn);

   if( bl[1] || *arg )
      sprintf(buf1+strlen(buf1), "%g%c", argn, bl[2]);
   else
      sprintf(buf1+strlen(buf1), "%c", bl[2]);

   while(outoff<15) output(' '); output(' ');

   for(ptr=xlate; *ptr; ptr+=2)
   {
      if( bl[0] == **ptr )
      {
         sprintf(buf2, *ptr, argn, argn);
         if( strcmp(buf1, buf2) == 0 )
	 {
	    printf(ptr[1], argx, argx);
	    return;
	 }
      }
   }
   printf("Unknown PCL sequence");
}

char *hpgl[] = 
{
   "DT", "Define Label Terminator",
   "BL", "Buffer label",
   "LB", "Label",
   "WD", "Write to Display",

   "PA", "Plot Absolute",
   "PR", "Plot Relative",
   "PD", "Pen Down",
   "PU", "Pen Up",

   "SP", "Select Pen",
   "PT", "Pen Thickness",

   "AA", "Arc Absolute",
   "AF", "Advance page",
   "AH", "Advance page",
   "AP", "Automatic Pen Operations",
   "AR", "Arc Relative",
   "AS", "Acceleration Select",
   "BF", "Buffer Plot",
   "CA", "Select Alternative Charset",
   "CC", "Character chord angle",
   "CI", "Circle",
   "CM", "Character Selection Mode",
   "CP", "Character Plot",
   "CS", "Select Standard Charset",
   "CT", "Chord Tolerance",
   "CV", "Curved line generator",
   "CD", "Digitize Clear",
   "DF", "Default",
   "DI", "Absolute Direction",
   "DL", "Define Download character",
   "DP", "Digitze Point",
   "DR", "Relative Direction",
   "DS", "Designate Charset",
   "EA", "Edge Rectangle Absolute",
   "EP", "Edge Polygon",
   "ER", "Edge Rectangle Relative",
   "ES", "Extra Space",
   "EW", "Edge Wedge",
   "FP", "Fill Polygon",
   "FS", "Force Select",
   "FT", "Fill Type",
   "GC", "Group Count",
   "GM", "Graphics Memory",
   "GP", "Group Pen",
   "IM", "Input Mask",
   "IN", "Initialize",
   "IP", "Input p1 and p2",
   "IV", "Invoke Character Slot",
   "IW", "Input Window",
   "KY", "Define Key",
   "LO", "Label Origin",
   "LT", "Line Type",
   "NR", "Not Ready",
   "PB", "Print Buffered Label",
   "PG", "Page Feed",
   "PM", "Polygon Mode",
   "RA", "Fill Rectangle Absolute",
   "RO", "Rotate Coordinate System",
   "RP", "Replot",
   "RR", "Fill Rectangle Relative",
   "SA", "Select Alternative Charset",
   "SC", "Scale",
   "SG", "Select Pen Group",
   "SI", "Absolute Character Size",
   "SL", "Character Slant",
   "SM", "Symbol Mode",
   "SR", "Relative Character Size",
   "SS", "Select Standard Charset",
   "TL", "Tick Length",
   "UC", "User Defined Character",
   "UF", "User Defined Fill",
   "VS", "Velocity Select",
   "WG", "Fill Wedge",
   "XT", "X Tick",
   "YT", "Y Tick",
   "OA", "Output Actual Position",
   "OC", "Output Position",
   "OD", "Output Digitised Point",
   "OE", "Output Error",
   "OF", "Output Factors",
   "OG", "Output Group Count",
   "OH", "Output HardClip Limits",
   "OI", "Output Identification",
   "OK", "Output Key",
   "OL", "Output Label Length",
   "OO", "Output Options",
   "OP", "Output p1 and p2",
   "OS", "Output Status",
   "OT", "Output Carousel Type",
   "OW", "Output Window",
   0
};
decode_hpgl(ch)
int ch;
{
}
