/* main.c - LaTeX to RTF conversion program

Copyright (C) 1995-2002 The Free Software Foundation

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

This file is available from http://sourceforge.net/projects/latex2rtf/
 
Authors:
	1995	  Fernando Dorner, Andreas Granzer, Freidrich Polzer, Gerhard Trisko
	1995-1997 Ralf Schlatterbeck
	1998-2000 Georg Lehner
	2001-2002 Scott Prahl
*/

#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <errno.h>
#include "main.h"
#include "mygetopt.h"
#include "convert.h"
#include "commands.h"
#include "chars.h"
#include "l2r_fonts.h"
#include "stack.h"
#include "direct.h"
#include "ignore.h"
#include "version.h"
#include "funct1.h"
#include "cfg.h"
#include "encode.h"
#include "util.h"
#include "parser.h"
#include "lengths.h"
#include "counters.h"
#include "preamble.h"
#include "xref.h"

FILE		   *fRtf = NULL;			/* file pointer to RTF file */
char		   *g_tex_name = NULL;
char		   *g_rtf_name = NULL;
char		   *g_aux_name = NULL;
char		   *g_bbl_name = NULL;
char		   *g_home_dir = NULL;

char		   *progname;				/* name of the executable file */
bool			GermanMode = FALSE;		/* support germanstyle */
bool			FrenchMode = FALSE;		/* support frenchstyle */
bool			RussianMode = FALSE;	/* support russianstyle */
bool			CzechMode = FALSE;		/* support czech */

char			g_charset_encoding_name[20] = "cp1252";
int				g_fcharset_number = 0;

bool			twoside = FALSE;
int				g_verbosity_level = WARNING;
bool			g_little_endian = FALSE;  /* set properly in main() */
int				g_dots_per_inch = 300;

bool			pagenumbering = TRUE;	/* by default use plain style */
int				headings = FALSE;

bool			g_processing_preamble = TRUE;	/* flag set until \begin{document} */
bool			g_processing_figure = FALSE;	/* flag, set for figures and not tables */
bool			g_processing_eqnarray = FALSE;	/* flag set when in an eqnarry */
int				g_processing_arrays = 0;
int				g_processing_fields = 0;

bool			g_show_equation_number = FALSE;
int				g_enumerate_depth = 0;
bool			g_suppress_equation_number = FALSE;
bool			g_aux_file_missing = FALSE; /* assume that it exists */

int				g_safety_braces = 0;
bool			g_processing_equation = FALSE;
bool			g_document_type = FORMAT_ARTICLE;
bool			g_RTF_warnings = FALSE;
char		   *g_config_path = NULL;
char		   *g_tmp_path = NULL;
char		   *g_preamble = NULL;
char			g_field_separator = ',';
bool 			g_escape_parent= TRUE;

bool			g_equation_display_rtf	  = TRUE;
bool			g_equation_inline_rtf	  = TRUE;
bool			g_equation_inline_bitmap  = FALSE;
bool			g_equation_display_bitmap = FALSE;
bool			g_equation_comment		  = FALSE;

double			g_png_equation_scale=1.22; 
double			g_png_figure_scale=1.35;
bool			g_latex_figures = FALSE;

int				indent = 0;
char			alignment = JUSTIFIED;	/* default for justified: */

int				RecursionLevel = 0;
bool			twocolumn = FALSE;
bool			titlepage = FALSE;

static void		OpenRtfFile(char *filename, FILE ** f);
static void		CloseRtf(FILE ** f);
static void		ConvertLatexPreamble(void);
static void		InitializeLatexLengths(void);

static void		SetEndianness(void);
static void		ConvertWholeDocument(void);
static void		print_usage(void);
static void		print_version(void);

extern char *optarg;
extern int	 optind;

int main(int argc, char **argv)
{
	int				c,x;
	char		   *basename = NULL;
	double			xx;
		
	SetEndianness();
	progname = argv[0];

	while ((c = my_getopt(argc, argv, "lhpvFSWZ:o:a:b:d:i:s:C:D:M:P:T:")) != EOF) {
		switch (c) {
		case 'a':
			g_aux_name = optarg;
			break;
		case 'b':
			g_bbl_name = optarg;
			break;
		case 'd':
			g_verbosity_level = *optarg - '0';
			if (g_verbosity_level < 0 || g_verbosity_level > 7) {
				diagnostics(WARNING, "debug level (-d# option) must be 0-7");
				print_usage();
			}
			break;
		case 'i':
			setPackageBabel(optarg);
			break;
		case 'l':
			setPackageBabel("latin1");
			break;
		case 'o':
			g_rtf_name = strdup(optarg);
			break;
		case 'p':
		  	g_escape_parent= FALSE;
		  	break;
		case 'v':
			print_version();
			return (0);
		case 'C':
			setPackageInputenc(optarg);
			break;
		case 'D':
			sscanf(optarg, "%d", &g_dots_per_inch);
			if (g_dots_per_inch < 25 || g_dots_per_inch >600)
				fprintf(stderr, "Dots per inch must be between 25 and 600 dpi\n");
			break;
		case 'F':
			g_latex_figures = TRUE;
			break;
		case 'M':
			sscanf(optarg, "%d", &x);
			diagnostics(WARNING, "Math option = %s x=%d",optarg,x);
			g_equation_display_rtf	  = (x & 1) ? TRUE : FALSE;
			g_equation_inline_rtf	  = (x & 2) ? TRUE : FALSE;
			g_equation_display_bitmap = (x & 4) ? TRUE : FALSE;
			g_equation_inline_bitmap  = (x & 8) ? TRUE : FALSE;
			g_equation_comment		  = (x & 16)? TRUE : FALSE;
			diagnostics(WARNING, "Math option g_equation_display_rtf   = %d",g_equation_display_rtf);
			diagnostics(WARNING, "Math option g_equation_inline_rtf	   = %d",g_equation_inline_rtf);
			diagnostics(WARNING, "Math option g_equation_display_bitmap= %d",g_equation_display_bitmap);
			diagnostics(WARNING, "Math option g_equation_inline_bitmap = %d",g_equation_inline_bitmap);
			diagnostics(WARNING, "Math option g_equation_comment	   = %d",g_equation_comment);
			if (!g_equation_comment && !g_equation_inline_rtf && !g_equation_inline_bitmap)
				g_equation_inline_rtf=TRUE;
			if (!g_equation_comment && !g_equation_display_rtf && !g_equation_display_bitmap)
				g_equation_display_rtf=TRUE;
			break;
		case 'P':
			g_config_path = strdup(optarg);
			break;
		case 's': 
			if (optarg && optarg[0]=='e') {
				if(sscanf(optarg,"e%lf",&xx)==1 && xx>0)
					g_png_equation_scale=xx;
				else 
					diagnostics(WARNING, "Equation scale (-se #) is not positive, ignoring");
			} 
			if (optarg && optarg[0]=='f') {
				if(sscanf(optarg,"f%lf",&xx)==1 && xx>0)
					g_png_figure_scale=xx;
				else 
					diagnostics(WARNING, "Figure scale (-sf #) is not positive, ignoring");
			}
			break;
		case 'S':
			g_field_separator = ';';
			break;
		case 'T':
			g_tmp_path = strdup(optarg);
			break;
		case 'W':
			g_RTF_warnings = TRUE;
			break;
		case 'Z':
			g_safety_braces = FALSE;
			g_safety_braces = *optarg - '0';
			if (g_safety_braces < 0 || g_safety_braces > 9) {
				diagnostics(WARNING, "Number of safety braces (-Z#) must be 0-9");
				print_usage();
			}
			break;
			
		case 'h':
		case '?':
		default:
			print_usage();
		}
	}

	argc -= optind;
	argv += optind;
	
	if (argc > 1) {
		diagnostics(WARNING, "Only a single file can be processed at a time");
		diagnostics(ERROR,	 " Type \"latex2rtf -h\" for help");
	}
	
/* Parse filename.	Extract directory if possible.	Beware of stdin cases */
	
	if (argc == 1 && strcmp(*argv,"-")!=0) {	/* filename exists and != "-" */
		char		   *s, *t;

		basename = strdup(*argv);				/* parse filename			 */
		s = strrchr(basename, PATHSEP);
		if (s != NULL) {						
			g_home_dir = strdup(basename);		/* parse /tmp/file.tex		 */
			t = strdup(s+1);
			free(basename);
			basename = t;						/* basename = file.tex		 */ 
			s = strrchr(g_home_dir, PATHSEP);
			*(s+1) = '\0';						/* g_home_dir = /tmp/		 */
		}

		t = strstr(basename, ".tex");			/* remove .tex if present	 */
		if (t != NULL) 
			*t = '\0';
		
		g_tex_name = strdup_together(basename, ".tex");
		
		if (g_rtf_name==NULL)
			g_rtf_name = strdup_together(basename, ".rtf");
	} 
	
	if (g_aux_name == NULL && basename != NULL) 
		g_aux_name = strdup_together(basename, ".aux");

	if (g_bbl_name == NULL && basename != NULL)
		g_bbl_name = strdup_together(basename, ".bbl");

	if (basename) {
		diagnostics(3, "latex filename is <%s>", g_tex_name);
		diagnostics(3, "  rtf filename is <%s>", g_rtf_name);
		diagnostics(3, "  aux filename is <%s>", g_aux_name);
		diagnostics(3, "  bbl filename is <%s>", g_bbl_name);
		diagnostics(3, "home directory is <%s>", g_home_dir);
	}
	
	ReadCfg();

	if (PushSource(g_tex_name, NULL)==0) {
		OpenRtfFile(g_rtf_name, &fRtf);
		
		InitializeStack();
		InitializeLatexLengths();
		InitializeDocumentFont(TexFontNumber("Roman"), 20, F_SHAPE_UPRIGHT, F_SERIES_MEDIUM);
		PushTrackLineNumber(TRUE);

		ConvertWholeDocument(); 
		PopSource();
		CloseRtf(&fRtf);
		printf("\n");
/*		debug_malloc();*/

		return 0;
	} else {
		printf("\n");	
		return 1;
	}
}

static void		
SetEndianness(void) 
/*
purpose : Figure out endianness of machine.	 Needed for graphics support
*/
{
	unsigned int	endian_test = 0xaabbccdd;
	unsigned char	endian_test_char = *(unsigned char *)&endian_test;

	if (endian_test_char == 0xdd)
		g_little_endian = TRUE;
}	


static void
ConvertWholeDocument(void)
{
	char * body, *sec_head, *sec_head2, *label;
	char t[] = "\\begin{document}";

	PushEnvironment(PREAMBLE);
	SetTexMode(MODE_VERTICAL);
	ConvertLatexPreamble(); 
	WriteRtfHeader();
	ConvertString(t);

	g_processing_preamble = FALSE;
	getSection(&body,&sec_head,&label);
	
	diagnostics(2,"*******************\nbody=%s",body);
	diagnostics(2,"*******************\nsec_head=%s",sec_head);
	diagnostics(2,"*******************\nlabel=%s",g_section_label);
	ConvertString(body);
	free(body);
	if (label) free(label);
	
	while(sec_head) {
		getSection(&body,&sec_head2,&g_section_label);
		label = ExtractLabelTag(sec_head);
		if (label) {
			if (g_section_label) free(g_section_label);
			g_section_label = label;
		} 
		diagnostics(2,"\n========this section head==========\n%s",sec_head);
		diagnostics(2,"\n============ label ================\nlabel=%s",g_section_label);
		diagnostics(2,"\n==============body=================\n%s\n=========end	body=================",body);
		diagnostics(2,"\n========next section head==========\n%s",sec_head2);
		ConvertString(sec_head);
		ConvertString(body);
		free(body);
		free(sec_head);
		sec_head = sec_head2;
	}
}

static void
print_version(void)
{
	fprintf(stderr, "%s %s\n\n", progname, Version);
	fprintf(stderr, "Copyright (C) 2002 Free Software Foundation, Inc.\n\n");
	fprintf(stderr, "This is free software; see the source for copying conditions.  There is NO\n");
	fprintf(stderr, "warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.\n\n");
	fprintf(stderr, "Written by Prahl, Lehner, Granzer, Dorner, Polzer, Trisko, Schlatterbeck.\n");

/*		fprintf(stderr, "RTFPATH = '%s'\n", getenv("RTFPATH"));*/
}

static void
print_usage(void)
{
	fprintf(stderr, "latex2rtf converts text files in LaTeX format to rich text format (RTF).\n\n");
	fprintf(stderr, "Usage:  %s [options] input[.tex]\n\n", progname);
	fprintf(stderr, "Options:\n");
	fprintf(stderr, "  -a auxfile       use LaTeX auxfile rather than input.aux\n");
	fprintf(stderr, "  -b bblfile       use BibTex bblfile rather than input.bbl)\n");
	fprintf(stderr, "  -C codepage      input encoding (latin1, cp850, raw, etc.)\n");
	fprintf(stderr, "  -d#              debug level (# is 0-6)\n");
	fprintf(stderr, "  -D#              dots per inch for bitmaps\n");
	fprintf(stderr, "  -h               display this help\n");
	fprintf(stderr, "  -i language      babel idiom (german, french)\n");
	fprintf(stderr, "  -l               use latin1 encoding (default)\n");
	fprintf(stderr, "  -M#              math equation handling\n");
	fprintf(stderr, "       -M1          displayed equations to RTF\n");
	fprintf(stderr, "       -M2          inline equations to RTF\n");
	fprintf(stderr, "       -M3          inline and displayed equations to RTF (default)\n");
	fprintf(stderr, "       -M4          displayed equations to bitmap\n");
	fprintf(stderr, "       -M6          inline equations to RTF and displayed equations to bitmaps\n");
	fprintf(stderr, "       -M8          inline equations to bitmap\n");
	fprintf(stderr, "       -M12         inline and displayed equations to bitmaps\n");
	fprintf(stderr, "       -M16         insert Word comment field that the original equation text\n");
	fprintf(stderr, "  -o outputfile    RTF output other than input.rtf\n");
	fprintf(stderr, "  -P /path/to/cfg  directory containing .cfg files\n");
	fprintf(stderr, "  -S               use ';' to separate args in RTF fields\n");
	fprintf(stderr, "  -se#             scale factor for bitmap equations\n");
	fprintf(stderr, "  -sf#             scale factor for bitmap figures\n");
	fprintf(stderr, "  -T /path/to/tmp  temporary directory\n");
	fprintf(stderr, "  -v               version information\n");
	fprintf(stderr, "  -V               version information\n");
	fprintf(stderr, "  -W               include warnings in RTF\n");
	fprintf(stderr, "  -Z#              add # of '}'s at end of rtf file (# is 0-9)\n\n");
	fprintf(stderr, "Examples:\n");
	fprintf(stderr, "  latex2rtf foo              convert foo.tex to foo.rtf\n");
	fprintf(stderr, "  latex2rtf <foo >foo.RTF    convert foo to foo.RTF\n");
	fprintf(stderr, "  latex2rtf -M12 foo         replace equations with bitmaps\n");
	fprintf(stderr, "  latex2rtf -i russian foo   assume russian tex conventions\n");
	fprintf(stderr, "  latex2rtf -C raw foo       retain font encoding in rtf file\n");
	fprintf(stderr, "  latex2rtf -d4 foo          lots of debugging information\n\n");
	fprintf(stderr, "Report bugs to <latex2rtf-developers@lists.sourceforge.net>\n\n");
	fprintf(stderr, "RTFPATH designates the directory for configuration files (*.cfg)\n");
	fprintf(stderr, "  RTFPATH = '%s'\n", getenv("RTFPATH"));
	fprintf(stderr, "  CFGDIR  = '%s'\n\n", CFGDIR);
	exit(1);
}

void
diagnostics(int level, char *format,...)
/*
purpose : Writes the message to stderr depending on debugging level
*/
{
	char		   buffer[512], *buff_ptr;
	va_list		   apf;
	int			   i,linenumber, iEnvCount;
	char		  *input;
	
	buff_ptr = buffer;

	va_start(apf, format);

	if (level <= g_verbosity_level) {

		linenumber = CurrentLineNumber();
		input	   = CurrentFileName();
		iEnvCount  = CurrentEnvironmentCount();
		
		switch (level) {
		case 0:
			fprintf(stderr, "\nError! line=%d ", linenumber);
			break;
		case 1:
			fprintf(stderr, "\nWarning line=%d ", linenumber);
			if (g_RTF_warnings) {
				vsprintf(buffer, format, apf);
				fprintRTF("{\\plain\\cf2 [latex2rtf:");
				while (*buff_ptr){putRtfChar(*buff_ptr);buff_ptr++;}
				fprintRTF("]}");
			}
			break;
		case 2:
		case 3:
		case 4:
		case 5:
		case 6:
			fprintf(stderr, "\n%s %4d rec=%d ", input, linenumber, RecursionLevel);
			for (i=0; i<BraceLevel; i++)
				fprintf(stderr, "{");
			for (i=8; i>BraceLevel; i--)
				fprintf(stderr, " ");
			
			for (i=0; i<RecursionLevel; i++)
				fprintf(stderr, "  ");
			break;
		default:
			fprintf(stderr, "\nline=%d ", linenumber);
			break;
		}
		vfprintf(stderr, format, apf);
	}
	va_end(apf);

	if (level == 0) {
		fprintf(stderr, "\n");
		fflush(stderr);
		if (fRtf) {
			fflush(fRtf);
			CloseRtf(&fRtf);
		}
		exit(EXIT_FAILURE);
	}
}

static void
InitializeLatexLengths(void)
{
	/* Default Page Sizes */
	setLength("pageheight",	 795*20);
	setLength("hoffset",	   0*20);
	setLength("oddsidemargin",62*20);
	setLength("headheight",	  12*20);
	setLength("textheight",	 550*20);
	setLength("footskip",	  30*20);
	setLength("marginparpush", 5*20);

	setLength("pagewidth",	 614*20);
	setLength("voffset",	   0*20);
	setLength("topmargin",	  18*20);
	setLength("headsep",	  25*20);	
	setLength("textwidth",	 345*20);
	setLength("marginparsep", 10*20);
	setLength("columnsep",	  10*20);

	setLength("smallskipamount", 3*20);
	setLength("medskipamount", 6*20);
	setLength("bigskipamount", 12*20);
	setLength("evensidemargin",11*20);

	/* Default Paragraph Sizes */
	setLength("baselineskip",12*20);
	setLength("parindent",	 15*20);
	setLength("parskip",	  0*20);
				
	setCounter("page",			0);
	setCounter("chapter",		0);
	setCounter("section",		0);
	setCounter("subsection",	0);
	setCounter("subsubsection", 0);
	setCounter("paragraph",		0);
	setCounter("subparagraph",	0);
	setCounter("figure",		0);
	setCounter("table",			0);
	setCounter("equation",		0);
	setCounter("footnote",		0);
	setCounter("mpfootnote",	0);
	setCounter("secnumdepth",	2);
	
 }


static void 
ConvertLatexPreamble(void)
/****************************************************************************
purpose: reads the LaTeX preamble (to \begin{document} ) for the file
 ****************************************************************************/
{
	FILE * hidden;
	char t[] = "\\begin{document}";
	
	diagnostics(4, "Reading LaTeX Preamble");
	hidden = fRtf;
	fRtf = stderr;
	 
	g_preamble = getTexUntil(t,1);
	
	diagnostics(4, "Entering ConvertString() from ConvertLatexPreamble <%s>",g_preamble);
	ConvertString(g_preamble);
	diagnostics(4, "Exiting ConvertString() from ConvertLatexPreamble");
	
	fRtf = hidden;
}


void 
OpenRtfFile(char *filename, FILE ** f)
/****************************************************************************
purpose: creates output file and writes RTF-header.
params: filename - name of outputfile, possibly NULL for already open file
	f - pointer to filepointer to store file ID
 ****************************************************************************/
{
	char * name;
	
	if (filename == NULL) {
		diagnostics(4, "Writing RTF to stdout");
		*f = stdout;

	} else {
		
		if (g_home_dir)
			name = strdup_together(g_home_dir,filename);
		else
			name = strdup(filename);
			
		diagnostics(3, "Opening RTF file <%s>", name);
		*f = fopen(name, "w");

		if (*f == NULL) 
			diagnostics(ERROR,	"Error opening RTF file <%s>\n", name);
		
		free(name);
	}
}

void
CloseRtf(FILE ** f)
/****************************************************************************
purpose: closes output file.
params: f - pointer to filepointer to invalidate
globals: g_tex_name;
 ****************************************************************************/
{
	int i;
	CmdEndParagraph(0);
	if (BraceLevel>1) 
		diagnostics(WARNING,"Mismatched '{' in RTF file, Conversion may cause problems.");

	if (BraceLevel-1>g_safety_braces) 
		diagnostics(WARNING,"Try translating with 'latex2rtf -Z%d %s'", BraceLevel-1, g_tex_name);
	
	fprintf(*f, "}\n");
	for (i=0; i<g_safety_braces; i++)
		fprintf(*f, "}");
	if (*f != stdout) {
		if (fclose(*f) == EOF) {
			diagnostics(WARNING, "Error closing RTF-File");
		}
	}
	*f = NULL;
	diagnostics(4,"Closed RTF file");
}

void
putRtfChar(char cThis)
/****************************************************************************
purpose: output filter to escape characters written to an RTF file
		 all output to the RTF file passes through this routine or the one below
 ****************************************************************************/
{
	if (cThis == '\\')
		fprintf(fRtf, "\\\\");
	else if (cThis == '{')
		fprintf(fRtf, "\\{");
	else if (cThis == '}')
		fprintf(fRtf, "\\}");
	else if (cThis == '\n') 
		fprintf(fRtf, "\n\\par ");
	else
		fputc(cThis, fRtf);
}

void
fprintRTF(char *format, ...)
/****************************************************************************
purpose: output filter to track of brace depth and font settings
		 all output to the RTF file passes through this routine or the one above
 ****************************************************************************/
{
	char buffer[1024];
	char *text;
	va_list		  apf;

	va_start(apf, format);
	vsprintf(buffer, format, apf);
	va_end(apf);
	text = buffer;
	
	while ( *text ) {
	
		if ((unsigned char) *text > 127) {
			
			WriteEightBitChar(text[0]);
			
		} else {		
			fputc(*text, fRtf);
			
			if (*text == '{')
				PushFontSettings();
			
			if (*text == '}')
				PopFontSettings();
				
			if (*text == '\\')
				MonitorFontChanges(text);
		}
		text++;
	}			
}

char *
getTmpPath(void)
/****************************************************************************
purpose: return the directory to store temporary files
 ****************************************************************************/
{
#if defined(MSDOS) || defined(MACINTOSH) || defined(__MWERKS__)

	return strdup("");

#else

	char * t, *u;
	char pathsep_str[2]={PATHSEP,0};  /* for os2 or w32 "unix" compiler */
	
	/* first use any temporary directory specified as an option */
	if (g_tmp_path)
		t= strdup(g_tmp_path);

	/* next try the environment variable TMPDIR */
	else if ((u = getenv("TMPDIR"))!=NULL)
		t= strdup(u);
	
	/* finally just return "/tmp/" */
	else 
		t = strdup("/tmp/");
		
	/* append a final '/' if missing */
	if (*(t+strlen(t)-1) != PATHSEP) { 
		u = strdup_together(t,pathsep_str);
		free(t);
		return u;
	}
	
	return t;
#endif
}

char		   *
my_strdup(const char *str)
/****************************************************************************
purpose: duplicate string --- exists to ease porting
 ****************************************************************************/
{
	char		   *s=NULL;
	unsigned long  strsize;
	
	strsize = strlen(str);
	s = (char *) malloc(strsize+1);
	if (s == NULL) 
		diagnostics(ERROR,"Cannot allocate memory to duplicate string");
	strcpy(s, str);
/*	diagnostics(3,"ptr %x",(unsigned long)s);*/
	return s;
}

FILE *
my_fopen(char *path, char *mode)
{
	char *name;
	FILE *p;

	diagnostics(4,"Opening <%s>, mode=[%s]",path,mode);
	
	if (g_home_dir==NULL)
		name = strdup(path);
	else
		name = strdup_together(g_home_dir, path);

	diagnostics(4,"Opening <%s>",name);
	p = fopen(name, mode);

	if (p==NULL) {
	   diagnostics(WARNING, "Cannot open <%s>", name);
	   fflush(NULL);
	 }
	   
	free(name);
	return p;
}

void debug_malloc(void)
{
	char c;
	diagnostics(1,"Malloc Debugging --- press return to continue");
	fflush(NULL);
	fscanf(stdin,"%c",&c);
}
