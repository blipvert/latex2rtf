
/*
 * xrefs.c - commands for LaTeX cross references
 * 
 * Copyright (C) 2001-2002 The Free Software Foundation
 * 
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 * 
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 * 
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59
 * Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 * 
 * This file is available from http://sourceforge.net/projects/latex2rtf/
 * 
 * Authors: 2001-2002 Scott Prahl
 */

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "main.h"
#include "utils.h"
#include "convert.h"
#include "funct1.h"
#include "commands.h"
#include "cfg.h"
#include "xrefs.h"
#include "parser.h"
#include "preamble.h"
#include "lengths.h"
#include "fonts.h"
#include "styles.h"
#include "definitions.h"
#include "equations.h"
#include "vertical.h"
#include "fields.h"
#include "counters.h"

char *g_figure_label = NULL;
char *g_table_label = NULL;
char *g_equation_label = NULL;
char *g_section_label = NULL;
int g_suppress_name = FALSE;
static int g_warned_once = FALSE;

#define MAX_LABELS 200
#define MAX_CITATIONS 1000
#define BIB_STYLE_ALPHA  0
#define BIB_STYLE_SUPER  1
#define BIB_STYLE_NUMBER 2
#define MAX_AUTHOR_SIZE  201
#define MAX_YEAR_SIZE     51

char *BIB_DASH_MARKER="%dash%";

char *g_label_list[MAX_LABELS];
int g_label_list_number = -1;

typedef struct citekey_type {
	char *key;
	int number;
} citekey_type;


static char *g_all_citations[MAX_CITATIONS];
static int g_last_citation = 0;
static int g_current_cite_type = 0;
static int g_current_cite_seen = 0;
static int g_current_cite_paren = 0;
static char g_last_author_cited[MAX_AUTHOR_SIZE];
static char g_last_year_cited[MAX_YEAR_SIZE];
static int g_citation_longnamesfirst = 0;
static int g_current_cite_item = 0;
static int g_sorted_citations = FALSE;
static int g_compressed_citations = FALSE;

static char *g_bibpunct_open = NULL;
static char *g_bibpunct_close = NULL;
static char *g_bibpunct_cite_sep = NULL;
static char *g_bibpunct_author_date_sep = NULL;
static char *g_bibpunct_numbers_sep = NULL;
static char *g_bibpunct_postnote_sep = NULL;
static int g_bibpunct_cite_sep_touched = FALSE;
static int g_bibpunct_style_paren_touched = FALSE;
static int   g_bibpunct_style = BIB_STYLE_ALPHA;
static int g_in_bibliography = FALSE;

void InitializeBibliography(void)
{
	g_bibpunct_open = strdup("(");
	g_bibpunct_close = strdup(")");
	g_bibpunct_cite_sep = strdup(",");
	g_bibpunct_author_date_sep = strdup(",");
	g_bibpunct_numbers_sep = strdup(",");
    g_bibpunct_postnote_sep = strdup(", ");
    g_bibpunct_cite_sep_touched = FALSE;
    g_bibpunct_style_paren_touched = FALSE;
    g_bibpunct_style = BIB_STYLE_ALPHA;
}

void set_longnamesfirst(void)
{
    g_citation_longnamesfirst = TRUE;
}

void set_bibpunct_style_super(void)
{
    g_bibpunct_style = BIB_STYLE_SUPER;
}

void set_bibpunct_style_number(void)
{
    g_bibpunct_style = BIB_STYLE_NUMBER;
}

void set_bibpunct_style_separator(char *s)
{
    g_bibpunct_cite_sep_touched = TRUE;
	g_bibpunct_cite_sep=strdup(s);
}

void set_bibpunct_style_paren(char *open, char *close)
{
    g_bibpunct_style_paren_touched = TRUE;
	g_bibpunct_open = strdup(open);
	g_bibpunct_close = strdup(close);
}

void set_sorted_citations(void)
{
	g_sorted_citations = TRUE;
}

void set_compressed_citations(void)
{
	g_compressed_citations = TRUE;
}

/*************************************************************************
 * return 1 if citation used otherwise return 0 and add citation to list
 ************************************************************************/
static int citation_used(char *citation)
{
    int i;

    for (i = 0; i < g_last_citation; i++) {
        if (strcmp(citation, g_all_citations[i]) == 0)
            return 1;
    }

    if (g_last_citation > MAX_CITATIONS - 1) {
        diagnostics(WARNING, "Too many citations ... increase MAX_CITATIONS");
    } else {
        g_all_citations[g_last_citation] = strdup(citation);
        g_last_citation++;
    }

    return 0;
}

#define CR (char) 0x0d
#define LF (char) 0x0a

static int my_fgetc(FILE *f)
{
	int c;
	
	c = fgetc(f);
	if (feof(f)) return '\0';
		
	if (c==CR) {
		c = fgetc(f);
		if (c == LF) return '\n';
		ungetc(c,f);
		return '\n';
	} 
	
	if (c == LF ) return '\n';
	if (c =='\t') return ' ';
	
	return c;
}

/* written to avoid '\\''\n' being seen as the end of a line */
static char * my_fgets(FILE *f)
{
    char AuxLine[2048];
	int i;
	int cLast = '\0';
	
	if (f == NULL || feof(f)) return NULL;
	
	for(i=0; i<2048; i++) {
		AuxLine[i] = my_fgetc(f);
	
		if (feof(f)) break;
		
		if (AuxLine[i] == '\n') {
			if (cLast == '\\')
				AuxLine[i] = ' ';  /* replace backslash-newline with backslash-space */
			else 
				break;
		} 
		cLast = AuxLine[i];
	}
		
	AuxLine[i] = '\0';
	return strdup(AuxLine);
}

/*************************************************************************
purpose: obtains a reference from .aux file
    code==0 means \token{reference}{number}       -> "number"
    code==1 means \token{reference}{{sect}{line}} -> "sect"
    code==2 means \token{reference}{a}{b}{c}      -> "{a}{b}{c}"
    code==3 means \token{reference}[options]{a}   -> "{a}{b}{c}"
 ************************************************************************/
static char *ScanAux(char *token, char *reference, int code, char *aux_name)
{
    static FILE *fAux = NULL;
    static int warned_once = FALSE;
    char target[512];
    char *s, *t, *AuxLine;
    int braces;
    static int once = 0;
    
    once++;

    if (g_aux_file_missing || strlen(token) == 0) {
        return NULL;
    }
    diagnostics(4, "seeking '%s' in '%s' calls:%d ", reference, aux_name, once);
	
    snprintf(target, 512, "\\%s{%s}", token, reference);

    if (fAux == NULL)
    	fAux = my_fopen(aux_name, "rb"); /* WH: changed to binary 2007-10-30 */
    else
    	rewind(fAux);
    	
    if (fAux == NULL && !warned_once) {
        diagnostics(WARNING, "No .aux file.  Run LaTeX to create one.");
        g_aux_file_missing = TRUE;
        warned_once = TRUE;
        return NULL;
    }

	AuxLine = my_fgets(fAux);
	
    while (AuxLine != NULL) {

		s = strstr(AuxLine, "\\@input{");
		if (s) {
			char *ret_val, *filename;
			FILE *old_fAux = fAux;
			
			t = strchr(s, '}');
			filename = my_strndup(s+8,t-s-8);
			
			diagnostics(4, "In ScanAux, handling \\@input{%s}", filename);
			fAux=NULL;
			ret_val = ScanAux(token,reference,code,filename);
			free(filename);
			fclose(fAux);
			fAux = old_fAux;
			if (ret_val != NULL) {
				free(AuxLine);
				return ret_val;
			}
		}

		s = strstr(AuxLine, target);
		if (s) {
			s += strlen(target);    /* move to \token{reference}{ */
			
			if (code == 2) {
				char *ss;
				diagnostics(4, "found <%s>", s);
				ss = strdup_noendblanks(s);
				free(AuxLine);
				return ss;
			}
				
			if (code == 1)
				s++;            /* move to \token{reference}{{ */

			t = s;
			braces = 1;
			while (braces > 1 || (code!=3 && braces >0)) {   /* skip matched braces */
				t++;
				if (*t == '{')
					braces++;
				if (*t == '}')
					braces--;
				if (*t == '\0') {
					free(AuxLine);
					return NULL;
				}
			}

			*t = '\0';
			diagnostics(4, "found <%s>", s + 1);
			t = strdup(s+1);
			free(AuxLine);
			return t;
		}
		free(AuxLine);
		AuxLine = my_fgets(fAux);
    }
    return NULL;
}

/*************************************************************************
purpose: obtains a \bibentry{reference} from the .bbl file
         this consists of all lines after \bibentry{reference} until two
         newlines in a row are found.  
         Finally, remove a '.' if at the end 
 ************************************************************************/
static char *ScanBbl(char *reference)
{
    static FILE *f_bbl = NULL;
    char *buffer, *target;
    char *s=NULL;
	char last_c;
	int  i=1;
	
    if (g_bbl_file_missing || strlen(reference) == 0) {
        return NULL;
    }
    target = strdup_together3("{", reference,"}");
    diagnostics(4, "seeking '%s' in .bbl", target);
	
    if (f_bbl == NULL && (f_bbl = my_fopen(g_bbl_name, "rb")) == NULL) {
        diagnostics(WARNING, "No .bbl file.  Run LaTeX to create one.");
        g_bbl_file_missing = TRUE;
        return NULL;
    }
    rewind(f_bbl);

	/* scan each line for \bibentry{reference} */
	while ((buffer = my_fgets(f_bbl)) != NULL) {
		s = strstr(buffer,target);
		if (s) break;
		free(buffer);
	}
	
	free(target);
	if (s == NULL) return NULL;
	buffer = (char *) malloc(4096);
	
	/* scan bbl file until we encounter \n\n */
	last_c = '\0';
	for (i=0; i<4096; i++) {		
		buffer[i] = my_fgetc(f_bbl);
		if (feof(f_bbl)) break;
		if (buffer[i] == '\n' && last_c == '\n') break;		
		last_c = buffer[i];
	}
		
	/* strip trailing . and any spaces at the end */
	while (buffer[i] ==' ' || buffer[i] == '\n') i--;
	if (buffer[i] == '.') i--;
	
	buffer[i+1] = '\0';
	s = strdup(buffer);
	free(buffer);
	return s;
}

/******************************************************************************
 purpose: creates RTF so that endnotes will be emitted at this point
 ******************************************************************************/
void CmdTheEndNotes(int code)
{
    diagnostics(4, "Entering CmdTheEndNotes");

    CmdVspace(VSPACE_BIG_SKIP);
    startParagraph("bibliography", SECTION_TITLE_PARAGRAPH);
    fprintRTF("{\\sect ");
    InsertStyle("section");
    fprintRTF(" Notes");
    CmdEndParagraph(0);

    fprintRTF("\\endnhere}");
}

/******************************************************************************
 purpose: converts footnotes and endnotes from LaTeX to Rtf
 params : code specifies whether it is a footnote or a thanks-mark
 ******************************************************************************/
void CmdFootNote(int code)
{
    char *number, *text;
    static int thankno = 0; /*WH 2009-11-10: changed from 1 to 0*/

    diagnostics(4, "Entering ConvertFootNote");
    number = getBracketParam(); /* ignored by automatic footnumber * generation */
    text = getBraceParam();

    switch (code) {
        case FOOTNOTE_THANKS:
            thankno++;
            fprintRTF("{");
            InsertStyle("footnote reference");
            fprintRTF(" %d}\n", thankno);
            fprintRTF("{\\*\\footnote\\pard ");
            InsertStyle("footnote text");
            fprintRTF("{");
            InsertStyle("footnote reference");
            fprintRTF("%d} ", thankno);
           break;

        case FOOTNOTE:
            fprintRTF("{");
            InsertStyle("footnote reference");
            fprintRTF("\\chftn}\n");
            fprintRTF("{\\*\\footnote\\pard ");
            InsertStyle("footnote text");
            fprintRTF("{");
            InsertStyle("footnote reference");
            fprintRTF("\\chftn} ");
            break;

        case FOOTNOTE_TEXT:
            fprintRTF("{\\*\\footnote\\pard ");
            InsertStyle("footnote text");
            break;

        case ENDNOTE:
            fprintRTF("{");
            InsertStyle("endnote reference");
            fprintRTF("\\chftn}\n");
            fprintRTF("{\\*\\footnote\\ftnalt\\pard ");
            InsertStyle("endnote text");
            fprintRTF("{");
            InsertStyle("endnote reference");
            fprintRTF("\\chftn} ");
            break;

        case ENDNOTE_TEXT:
            fprintRTF("{\\*\\footnote\\ftnalt\\pard ");
            InsertStyle("endnote text");
            break;

    }

    ConvertString(text);
    fprintRTF("}\n");
    diagnostics(4, "Exiting CmdFootNote");
    free(text);
    if (number) free(number);
}

/******************************************************************************
 purpose: handle the \nocite{tag}
 ******************************************************************************/
void CmdNoCite(int code)
{
    free(getBraceParam());      /* just skip the parameter */
}

/******************************************************************************
 purpose: handle the \bibliographystyle
 ******************************************************************************/
void CmdBibliographyStyle(int code)
{
    char *s = getBraceParam();  /* throw away widest_label */

    free(s);
}

/******************************************************************************
 purpose: handle the \bibliography
 ******************************************************************************/
void CmdBibliography(int code)
{
    int err;
    char *s;

    s = getBraceParam();        /* throw away bibliography name */
    free(s);

    err = PushSource(g_bbl_name, NULL);
	g_in_bibliography = TRUE;
	
    if (!err) {
        diagnostics(4, "CmdBibliography ... begin Convert()");
        Convert();
        diagnostics(4, "CmdBibliography ... done Convert()");
        PopSource();
    } else
        diagnostics(WARNING, "Cannot open bibliography file.  Create %s using BibTeX", g_bbl_name);

	g_in_bibliography = FALSE;
}

/******************************************************************************
 purpose: handle the \thebibliography
 ******************************************************************************/
void CmdThebibliography(int code)
{
    int amount = 450;
    int i;

    if (code & ON) {
        char *s = getBraceParam();  /* throw away widest_label */

        free(s);

        CmdEndParagraph(0);
        CmdVspace(VSPACE_MEDIUM_SKIP);

        if (g_document_type == FORMAT_APA) {
            ConvertString("\\begin{center}{\\bf");
        } else {
        	startParagraph("bibliography", SECTION_TITLE_PARAGRAPH);
        	fprintRTF("{\\plain\\b\\fs32 ");
        }
        i = existsDefinition("refname");    /* see if refname has * been redefined */
        if (i > -1) {
            char *str = expandDefinition(i);

            ConvertString(str);
            free(str);
        } else {
            if (g_document_type == FORMAT_ARTICLE || g_document_type == FORMAT_APA)
                ConvertBabelName("REFNAME");
            else
                ConvertBabelName("BIBNAME");
        }

        if (g_document_type == FORMAT_APA) {
            ConvertString("}\\end{center}");
        } else {
        	fprintRTF("}");
        	CmdEndParagraph(0);
        }
        CmdVspace(VSPACE_SMALL_SKIP);

        PushEnvironment(BIBLIOGRAPHY_MODE);
        setLength("parindent", -amount);
        setLeftMarginIndent(getLeftMarginIndent() + amount);
    } else {
        CmdEndParagraph(0);
        CmdVspace(VSPACE_SMALL_SKIP);
        PopEnvironment();
        g_processing_list_environment = FALSE;
    }
}

/******************************************************************************
 purpose: handle the \bibitem
 ******************************************************************************/
void CmdBibitem(int code)
{
    char *label, *key, *signet, *s, c;

    g_processing_list_environment = TRUE;
    CmdEndParagraph(0);
    startParagraph("bibitem", FIRST_PARAGRAPH);

    label = getBracketParam();
    key = getBraceParam();
    signet = strdup_nobadchars(key);
    s = ScanAux("bibcite", key, 0, g_aux_name);

    if (label && !s) {          /* happens when file needs to be latex'ed again */
    	if (!g_warned_once){
        diagnostics(WARNING, "Cannot locate \\bibcite{%s} in .aux file",key);
        diagnostics(WARNING, "**** The .tex file probably needs to be LaTeXed again ***");
        g_warned_once = TRUE;
        }
        fprintRTF("[");
        ConvertString(label);
        fprintRTF("]");
    } else {
        diagnostics(4, "CmdBibitem <%s>", s);
        if (g_document_bibstyle == BIBSTYLE_STANDARD ||
            (g_document_bibstyle == BIBSTYLE_NATBIB && g_bibpunct_style != BIB_STYLE_ALPHA)) {
            fprintRTF("[");
            fprintRTF("{\\v\\*\\bkmkstart BIB_%s}", signet);
            ConvertString(s);
            fprintRTF("{\\*\\bkmkend BIB_%s}", signet);
            fprintRTF("]");
            fprintRTF("\\tab\n");
        }
        /* else emit nothing for APALIKE */
    }

    if (s)
        free(s);
    if (label)
        free(label);
    free(signet);
    free(key);

    c = getNonBlank();
    ungetTexChar(c);
}

/******************************************************************************
 purpose: handle the \bibentry
 ******************************************************************************/
void CmdBibEntry(int code)
{
    char *key, *s;

    key = getBraceParam();
    s = ScanBbl(key);
    if (s) {
		ConvertString(s);
        free(s);
    }
    free(key);
}

void CmdNewblock(int code)
{
    /* 
     * if openbib chosen then start a paragraph with 1.5em indent
     * otherwise do nothing
     */
}

/******************************************************************************
purpose: convert \index{classe!article@\textit{article}!section}
              to {\xe\v "classe:{\i article}:section"}
******************************************************************************/
void CmdIndex(int code)
{
    char *text, *r, *s, *t;

    getNonBlank();
    text = getDelimitedText('{', '}', TRUE);
    diagnostics(4, "CmdIndex \\index{%s}", text);
    fprintRTF("{\\xe{\\v ");

    t = text;
    while (t) {
        s = t;
        t = strchr(s, '!');
        if (t)
            *t = '\0';
        r = strchr(s, '@');
        if (r)
            s = r + 1;
        ConvertString(s);
        /* while (*s && *s != '@') fprintRTF("%c",*s++); */
        if (t) {
            fprintRTF("\\:");
            t++;
        }
    }

    fprintRTF("}}");
    diagnostics(4, "leaving CmdIndex");
    free(text);
}

void CmdPrintIndex(int code)
{
    CmdEndParagraph(0);
    fprintRTF("\\page ");
    fprintRTF("{\\field{\\*\\fldinst{INDEX \\\\c 2}}{\\fldrslt{}}}");
}

static int ExistsBookmark(char *s)
{
    int i;

    if (!s)
        return FALSE;
    for (i = 0; i <= g_label_list_number; i++) {
        if (strcmp(s, g_label_list[i]) == 0)
            return TRUE;
    }
    return FALSE;
}

static void RecordBookmark(char *s)
{
    if (!s)
        return;
    if (g_label_list_number >= MAX_LABELS)
        diagnostics(WARNING, "Too many labels...some cross-references will fail");
    else {
        g_label_list_number++;
        g_label_list[g_label_list_number] = strdup(s);
    }
}

void InsertBookmark(char *name, char *text)
{
    char *signet;

    if (!name) {
		if (getTexMode() == MODE_VERTICAL)
			changeTexMode(MODE_HORIZONTAL);
        fprintRTF("%s", text);
        return;
    }
    signet = strdup_nobadchars(name);

    if (ExistsBookmark(signet)) {
        diagnostics(4, "bookmark %s already exists", signet);

    } else {
        diagnostics(4, "bookmark %s being inserted around <%s>", signet, text);
        RecordBookmark(signet);
        if (fields_use_REF())
            fprintRTF("{\\*\\bkmkstart BM%s}", signet);
        fprintRTF("%s", text);
        if (fields_use_REF())
            fprintRTF("{\\*\\bkmkend BM%s}", signet);
    }

    free(signet);
}

void InsertContentMark(char marker, char *s1, char *s2, char *s3)
{
    fprintRTF("{\\field{\\*\\fldinst TC \"");
	ConvertString(s1);
	ConvertString(s2);
	ConvertString(s3);
    fprintRTF("\" \\\\f %c}{\\fldrslt }}", marker);
}

/******************************************************************************
purpose: handles \label \ref \pageref \cite
******************************************************************************/
void CmdLabel(int code)
{
	int brace;
    char *text, *signet, *s, *t, *p;
    char *option = NULL;
    int mode = getTexMode();
	
    option = getBracketParam();
    text = getBraceParam();
    if (strlen(text) == 0) {
        free(text);
        return;
    }
    switch (code) {
        case LABEL_LABEL:
            if (g_processing_figure || g_processing_table)
                break;
            if (mode == MODE_DISPLAYMATH) {
                g_equation_label = strdup_nobadchars(text);
                diagnostics(4, "equation label is <%s>", text);
            } else
                InsertBookmark(text, "");
            break;

        case LABEL_HYPERREF:
        case LABEL_REF:
        case LABEL_EQREF:
        case LABEL_VREF:
            signet = strdup_nobadchars(text);
            s = ScanAux("newlabel", text, 1, g_aux_name);
            if (code == LABEL_EQREF)
                fprintRTF("(");
                
            if (fields_use_REF()) {
                fprintRTF("{\\field{\\*\\fldinst{\\lang1024 REF BM%s \\\\* MERGEFORMAT }}", signet);
                fprintRTF("{\\fldrslt{");
            }
            
            if (s)
                ConvertString(s);
            else
                fprintRTF("?");
                
            if (fields_use_REF())
                fprintRTF("}}}");
                
            if (code == LABEL_EQREF)
                fprintRTF(")");

            if (code == LABEL_VREF) {
            	fprintRTF(" ");
				if (fields_use_REF()) {
					fprintRTF("{\\field{\\*\\fldinst{\\lang1024 PAGEREF BM%s \\\\p }}", signet);
					fprintRTF("{\\fldrslt{");
				}
				fprintRTF("%s", signet);
				if (fields_use_REF())
					fprintRTF("}}}");
            }

            free(signet);
            if (s) free(s);
                            
            break;

        case LABEL_HYPERPAGEREF:
        case LABEL_PAGEREF:
            signet = strdup_nobadchars(text);
            if (fields_use_REF()) {
                fprintRTF("{\\field{\\*\\fldinst{\\lang1024 PAGEREF BM%s \\\\* MERGEFORMAT }}", signet);
                fprintRTF("{\\fldrslt{");
            }
            fprintRTF("%s", signet);
            if (fields_use_REF())
                fprintRTF("}}}");
            free(signet);
            break;

		case LABEL_NAMEREF:
            signet = strdup_nobadchars(text);
            s = ScanAux("newlabel", text, 0, g_aux_name);
            if (s) {
				/* s should look like {2}{1}{Random Stuff\relax }{section.2}{} */
				t = strchr(s,'{');
				if (t) t=strchr(t+1,'{');
				if (t) t=strchr(t+1,'{');
				if (t) {
					t++;
					p=t;
					brace = 1;
					/* find end of string ... counting braces */
					while (p && *p) {
						if (*p=='{') brace++;
						if (*p=='}') {
							brace--;
							if (brace == 0) break;
						}
						p++;
					}
					if (p) *p='\0';
					ConvertString(t);
				}
		    }
		    
		    free(signet);
            if (s) free(s);
		    break;
    }

    free(text);
    if (option)
        free(option);
}

/*
 * given s="name1,name2,name3" returns "name2,name3" and makes s="name1" no
 * memory is allocated, commas are replaced by '\0'
 */
static char *popCommaName(char *s)
{
    char *t;

    if (s == NULL || *s == '\0')
        return NULL;

    t = strchr(s, ',');
    if (!t)
        return NULL;

    *t = '\0';                  /* replace ',' with '\0' */
    return t + 1;               /* next string starts after ',' */
}

/******************************************************************************
  purpose: return bracketed parameter

  \item<1>  --->  "1"        \item<>   --->  ""        \item the  --->  NULL
       ^                           ^                         ^
  \item <1>  --->  "1"        \item <>  --->  ""        \item  the --->  NULL
       ^                           ^                         ^
 ******************************************************************************/
static char *getAngleParam(void)
{
    char c, *text;

    c = getNonBlank();

    if (c == '<') {
        text = getDelimitedText('<', '>', TRUE);
        diagnostics(5, "getAngleParam [%s]", text);

    } else {
        ungetTexChar(c);
        text = NULL;
        diagnostics(5, "getAngleParam []");
    }

    return text;
}

static int isEmptyName(char *s)
{
    if (s == NULL)
        return 1;
    if (s[0] == '\0')
        return 1;
    if (s[0] == '{' && s[1] == '}')
        return 1;
    return 0;
}

static void ConvertNatbib(char *s, int code, char *pre, char *post, int first, int last)
{
    char *n, *year, *abbv, *full, *v;
    int author_repeated, year_repeated;

    PushSource(NULL, s);
    n = getBraceParam();
    year = getBraceParam();
    abbv = getBraceParam();
    full = getBraceParam();
    PopSource();
    diagnostics(4, "natbib pre=[%s] post=<%s> n=<%s> year=<%s> abbv=<%s> full=<%s>", pre, post, n, year, abbv, full);
    author_repeated = FALSE;
    year_repeated = FALSE;

	/* for numbers just write and then exit */
	if (g_bibpunct_style != BIB_STYLE_ALPHA){
		if (!first) {
			ConvertString(g_bibpunct_cite_sep);
			if (g_bibpunct_style == BIB_STYLE_NUMBER)
	            fprintRTF(" ");
	    }

		ConvertString(n);
		free(n);
		free(year);
		free(abbv);
		free(full);
		return;
    }
    
    switch (code) {

        case CITE_CITE:			
            v = abbv;
            if (g_citation_longnamesfirst && !isEmptyName(full)) 
            	v = full;
            	
			if (isEmptyName(v))
				v = n;
				
            if (strcmp(v, g_last_author_cited) == 0)
                author_repeated = TRUE;

	    	if (strncmp(year, g_last_year_cited, 4) == 0)   /* over simplistic test * ... */
				year_repeated = TRUE;

            if (!first && !author_repeated) {
            	ConvertString(g_bibpunct_cite_sep);
                fprintRTF(" ");
            }
            ConvertString(v);
			fprintRTF(" ");
			ConvertString(g_bibpunct_open);
			ConvertString(year);
			ConvertString(g_bibpunct_close);
            break;

        case CITE_T:
        case CITE_T_STAR:
        case CITE_T_CAP:
            v = abbv;
            if (CITE_T == code && g_citation_longnamesfirst && !g_current_cite_seen)
                if (!isEmptyName(full))
                    v = full;
            if (CITE_T_STAR == code)
                if (!isEmptyName(full))
                    v = full;

            if (strcmp(v, g_last_author_cited) == 0)
                author_repeated = TRUE;

            if (!first && !author_repeated) {
				ConvertString(g_bibpunct_close);
            	ConvertString(g_bibpunct_cite_sep);
                fprintRTF(" ");
            }
	    
			if (CITE_T_CAP == code) {
				v[1]=toupper(v[1]);
			}
	
			if (!author_repeated) { /* suppress repeated names */
				ConvertString(v);
				my_strlcpy(g_last_author_cited, v, MAX_AUTHOR_SIZE);
				my_strlcpy(g_last_year_cited, year, MAX_YEAR_SIZE);
				if (g_bibpunct_style == BIB_STYLE_ALPHA) {
					fprintRTF(" ");
					ConvertString(g_bibpunct_open);
					if (pre) {
						ConvertString(pre);
						fprintRTF(" ");
					}
					ConvertString(year);
				}
			} else if (g_bibpunct_style == BIB_STYLE_ALPHA) {
				if (!year_repeated) {
					ConvertString(g_bibpunct_numbers_sep);
					fprintRTF(" ");
					ConvertString(year);
				} else {
					char *ss = strdup(year + 4);
					ConvertString(g_bibpunct_numbers_sep);
					ConvertString(ss);
					free(ss);
				}
			}
			
			if (g_bibpunct_style == BIB_STYLE_ALPHA) {
				if (last && post && !isEmptyName(post)) {
					ConvertString(g_bibpunct_postnote_sep);
					ConvertString(post);
				}
				if (last)
					ConvertString(g_bibpunct_close);
			}
			break;

        case CITE_ALT:
        case CITE_ALT_STAR:
        case CITE_ALT_CAP:
            v = abbv;

            if (strcmp(v, g_last_author_cited) == 0)
                author_repeated = TRUE;

			if (strncmp(year, g_last_year_cited, 4) == 0)   /* over simplistic test * ... */
				year_repeated = TRUE;
	
			if (!first && !author_repeated) {
				ConvertString(g_bibpunct_cite_sep);
				fprintRTF(" ");
			}
	
			if (CITE_ALT_CAP == code) {
				v[1]=toupper(v[1]);
			}
	
			if (!author_repeated) { /* suppress repeated names */
				ConvertString(v);
				my_strlcpy(g_last_author_cited, v, MAX_AUTHOR_SIZE);
				my_strlcpy(g_last_year_cited, year, MAX_YEAR_SIZE);
				fprintRTF(" ");
				if (pre) {
					ConvertString(pre);
					fprintRTF(" ");
				}
				ConvertString(year);
			} else {
				if (!year_repeated) {
					ConvertString(g_bibpunct_numbers_sep);
					fprintRTF(" ");
					ConvertString(year);
				} else {
					char *ss = strdup(year + 4);
					ConvertString(g_bibpunct_numbers_sep);
					ConvertString(ss);
					free(ss);
				}
			}
			if (last && post && !isEmptyName(post)) {
			    ConvertString(g_bibpunct_postnote_sep);
				ConvertString(post);
			}
            break;
            
        case CITE_P:
        case CITE_P_CAP:
            v = abbv;

            if (strcmp(v, g_last_author_cited) == 0)
                author_repeated = TRUE;

            if (strncmp(year, g_last_year_cited, 4) == 0)   /* over simplistic test * ... */
                year_repeated = TRUE;

            if (!first && !author_repeated) {
            	ConvertString(g_bibpunct_cite_sep);
                fprintRTF(" ");
            }

			if (pre && g_current_cite_item == 1) {
					ConvertString(pre);
					fprintRTF(" ");
				}

			if (CITE_P_CAP == code) {
				v[1]=toupper(v[1]);
			}

            if (!author_repeated) { /* suppress repeated names */
                ConvertString(v);
                my_strlcpy(g_last_author_cited, v, MAX_AUTHOR_SIZE);
                my_strlcpy(g_last_year_cited, year, MAX_YEAR_SIZE);
				ConvertString(g_bibpunct_author_date_sep);
				fprintRTF(" ");			
			} else {			
                ConvertString(g_bibpunct_numbers_sep);
				fprintRTF(" ");
			}
			
			ConvertString(year);

			if (last && post && !isEmptyName(post)) {
				ConvertString(g_bibpunct_postnote_sep);
				ConvertString(post);
			}
            break;

        case CITE_P_STAR:
        case CITE_ALP:
        case CITE_ALP_STAR:
        case CITE_ALP_CAP:
            v = abbv;
            if (CITE_P == code && g_citation_longnamesfirst && !g_current_cite_seen)
                if (!isEmptyName(full))
                    v = full;
            if (CITE_P_STAR == code)
                if (!isEmptyName(full))
                    v = full;

            if (strcmp(v, g_last_author_cited) == 0)
                author_repeated = TRUE;

            if (strncmp(year, g_last_year_cited, 4) == 0)   /* over simplistic test * ... */
                year_repeated = TRUE;

            if (pre && g_current_cite_item == 1) {
				ConvertString(pre);
                 fprintRTF(" ");
            }

            if (!first && !author_repeated) {
            	ConvertString(g_bibpunct_cite_sep);
                fprintRTF(" ");
            }

			if (CITE_ALP_CAP == code) {
				v[1]=toupper(v[1]);
			}

            if (!author_repeated) { /* suppress repeated names */
                ConvertString(v);
                my_strlcpy(g_last_author_cited, v, MAX_AUTHOR_SIZE);
                my_strlcpy(g_last_year_cited, year, MAX_YEAR_SIZE);
				ConvertString(g_bibpunct_author_date_sep);
				fprintRTF(" ");
				ConvertString(year);
            } else {
                if (!year_repeated) {
             		ConvertString(g_bibpunct_numbers_sep);
                    fprintRTF(" ");
                    ConvertString(year);
                } else {
                    char *ss = strdup(year + 4);
                    ConvertString(g_bibpunct_numbers_sep);
                    ConvertString(ss);
                    free(ss);
                }
            }

            if (last && post && !isEmptyName(post)) {
             	ConvertString(g_bibpunct_postnote_sep);
                ConvertString(post);
            }
            break;

        case CITE_AUTHOR:
        case CITE_AUTHOR_STAR:
        case CITE_AUTHOR_CAP:
            v = abbv;
            if (!first) {
            	ConvertString(g_bibpunct_cite_sep);
                fprintRTF(" ");
            }
            if (CITE_AUTHOR == code && g_citation_longnamesfirst && !g_current_cite_seen)
	      		if (!isEmptyName(full))
                	v = full;

			if (CITE_AUTHOR_CAP == code) {
				v[1]=toupper(v[1]);
			}

            if (CITE_AUTHOR_STAR == code)
                if (!isEmptyName(full))
                    v = full;
                    
            ConvertString(v);
            if (last && post && !isEmptyName(post)) {
             	ConvertString(g_bibpunct_postnote_sep);
                ConvertString(post);
            }
            break;

        case CITE_YEAR:
        case CITE_YEAR_P:
            if (!first) {
            	ConvertString(g_bibpunct_cite_sep);
                fprintRTF(" ");
            }

            if (CITE_YEAR != code && pre && g_current_cite_item == 1) {
                ConvertString(pre);
                fprintRTF(" ");
            }
            ConvertString(year);

            if (last && post && !isEmptyName(post)) {
             	ConvertString(g_bibpunct_postnote_sep);
                ConvertString(post);
            }
            break;
    }
    free(n);
    free(year);
    free(abbv);
    free(full);
}

static void ConvertHarvard(char *s, int code, char *pre, char *post, int first)
{
    char *year, *abbv, *full;
    int author_repeated, year_repeated;

    PushSource(NULL, s);
    full = getBraceParam();
    abbv = getBraceParam();
    year = getBraceParam();
    PopSource();
    diagnostics(4, "harvard pre=[%s] post=<%s> full=<%s> abbv=<%s> year=<%s>", pre, post, full, abbv, year);
    author_repeated = FALSE;
    year_repeated = FALSE;
    switch (code) {
        case CITE_AFFIXED:
        	if (first && pre) {
        		ConvertString(pre);
        		fprintRTF(" ");
        	}
            ConvertString(full);
            fprintRTF(" ");
            ConvertString(year);
            break;

        case CITE_CITE:
            ConvertString(full);
            fprintRTF(" ");
            ConvertString(year);
            break;

        case CITE_YEAR:
        case CITE_YEAR_STAR:
             ConvertString(year);
             break;
             
        case CITE_NAME:
             ConvertString(full);
             break;

        case CITE_AS_NOUN:
             ConvertString(full);
             fprintRTF(" (");
             ConvertString(year);
             fprintRTF(")");
             break;
             
        case CITE_POSSESSIVE:
             ConvertString(full);
             fprintRTF("\\rquote s (");
             ConvertString(year);
             fprintRTF(")");
             break;
    }
    free(year);
    free(abbv);
    free(full);
}

void CmdNatexlab(int code) 
{
	char *s = getBracketParam();
	if (!BIB_STYLE_NUMBER) 
		ConvertString(s);
	free(s);
}

/******************************************************************************
 Use \bibpunct (in the preamble only) with 6 mandatory arguments:
    1. opening bracket for citation
    2. closing bracket
    3. citation separator (for multiple citations in one \cite)
    4. the letter n for numerical styles, s for superscripts
        else anything for author-year
    5. punctuation between authors and date
    6. punctuation between years (or numbers) when common authors missing

One optional argument is the character coming before post-notes. It 
appears in square braces before all other arguments. May be left off.
Example (and default) 
           \bibpunct[, ]{(}{)}{;}{a}{,}{,}
******************************************************************************/

void CmdBibpunct(int code) 
{
	char *s = NULL;
	
	s = getBracketParam();
	if (s) {
		if (g_bibpunct_postnote_sep)
			free(g_bibpunct_postnote_sep);
		g_bibpunct_postnote_sep = strdup(s);
		free(s);
	}
	
	free(g_bibpunct_open);
	g_bibpunct_open=getBraceParam();

	free(g_bibpunct_close);
	g_bibpunct_close=getBraceParam();

	free(g_bibpunct_cite_sep);
	g_bibpunct_cite_sep=getBraceParam();

    /* not implemented */
	s=getBraceParam();
	if (*s == 's')
		g_bibpunct_style = BIB_STYLE_SUPER;
	if (*s == 'n')
		g_bibpunct_style = BIB_STYLE_NUMBER;
	if (*s == 'a')
		g_bibpunct_style = BIB_STYLE_ALPHA;
	free(s);

	free(g_bibpunct_author_date_sep);
	g_bibpunct_author_date_sep=getBraceParam();

	free(g_bibpunct_numbers_sep);
	g_bibpunct_numbers_sep=getBraceParam();

	g_bibpunct_cite_sep_touched = TRUE;
	g_bibpunct_style_paren_touched = TRUE;
}

static int CmpFunc( const void * _a, const void * _b)
{ 
	citekey_type * aa = (citekey_type *) _a;
	citekey_type * bb = (citekey_type *) _b;
	int a = (*aa).number;
	int b = (*bb).number;

 	if (a > b) return 1;
	if (a ==  b) return 0;
	return -1;
}

static char * reorder_citations(char *keys, int scan_aux_code)
{
    char *key, *remaining_keys,*ordered_keys,*a,*b;
    int n,i; 
    int dash;
    citekey_type names[100];

	diagnostics(4,"original list <%s> scan aux code=%d",keys,scan_aux_code);
	
    /* gather citekeys and numbers into list */
    key = keys;
    remaining_keys = popCommaName(key);
	n=0;
    while (key  && n < 100) {
		char *s = ScanAux("bibcite", key, scan_aux_code, g_aux_name); 
		if (s) {
			int number;
			sscanf(s,"%d",&number);
			free(s);
   			names[n].key = key;
    		names[n].number = number;
    		n++;
		}
        key = remaining_keys;
        remaining_keys = popCommaName(key);
    }
 
 	/* if there is no .aux file or only one key return original list */
 	if (n<=1) {
 		ordered_keys = strdup(keys);
 		return ordered_keys;
 	}
 	
    /* sort list according to the numbers */
 	qsort(names, n, sizeof(citekey_type), CmpFunc);

    /* write the sorted list of keys into a string */
	ordered_keys=strdup(names[0].key);
	dash = FALSE;

	for (i=1; i<n; i++) {
		if (g_compressed_citations && dash && i!=n-1 && names[i].number+1==names[i+1].number) 
			continue;		/* skip intermediate numbers */

		a = strdup_together(ordered_keys, ",");
		
		if (g_compressed_citations && !dash && i!=n-1 && names[i-1].number+2==names[i+1].number) {
			/* insert dash */
			dash = TRUE;
			b = strdup_together(a, BIB_DASH_MARKER);
		} else {
			/* normal case */
			dash = FALSE;
			b = strdup_together(a, names[i].key);
		}
		free(a);
		free(ordered_keys);
		ordered_keys=b;
	}
	
	diagnostics(4,"compressed list <%s>",ordered_keys);
	return ordered_keys;    
}

/******************************************************************************
purpose: handles \cite
******************************************************************************/
void CmdCite(int code)
{
    char *text, *str1;
    char *keys, *key, *next_keys;
    char *option = NULL;
    char *pretext = NULL;
    int first_key = TRUE;

    /* Setup punctuation and read options before citation */
    g_current_cite_paren = TRUE;
    g_last_author_cited[0] = '\0';
    g_last_year_cited[0] = '\0';

    if (g_document_bibstyle == BIBSTYLE_STANDARD) {
		free(g_bibpunct_open);
		free(g_bibpunct_close);
		g_bibpunct_open = strdup("[");
	    g_bibpunct_close = strdup("]");
        option = getBracketParam();
    }
    if (g_document_bibstyle == BIBSTYLE_APALIKE) {
        option = getBracketParam();
    }
    if (g_document_bibstyle == BIBSTYLE_AUTHORDATE) {
        option = getBracketParam();
    }

    if (g_document_bibstyle == BIBSTYLE_APACITE) {
        pretext = getAngleParam();
        option = getBracketParam();
        if (code != CITE_CITE && code != CITE_FULL && code != CITE_SHORT && code != CITE_YEAR)
            g_current_cite_paren = FALSE;
        g_current_cite_type = code;
    }
    
    text = getBraceParam();
    str1 = strdup_nocomments(text);
    free(text);
    text = str1;
        
    if (strlen(text) == 0) {
        free(text);
        if (pretext)
            free(pretext);
        if (option)
            free(option);
        return;
    }
    /* output text before citation */
    if (g_current_cite_paren) {
        fprintRTF("\n"); 
        ConvertString(g_bibpunct_open);
    }

    if (pretext && g_document_bibstyle == BIBSTYLE_APACITE) {
        ConvertString(pretext);
        fprintRTF(" ");
    }

    /* clean-up keys and sort if necessary */
    keys = strdup_noblanks(text);
    free(text);
    if (g_sorted_citations){
    	text = reorder_citations(keys,0);
    	free(keys);
    	keys = text;
    }

    /* now start processing keys */
    key = keys;
    next_keys = popCommaName(key);

    g_current_cite_item = 0;
    while (key) {
        char *s, *t;

        g_current_cite_item++;

		if (strcmp(key,BIB_DASH_MARKER)==0) {
			fprintRTF("-");
        	first_key = TRUE;				/* inhibit comma after dash */
        	key = next_keys;
        	next_keys = popCommaName(key);  /* key modified to be a * single key */
			continue;
		}
		
		s = ScanAux("bibcite", key, 0, g_aux_name); /* look up bibliographic * reference */
            
        if (g_document_bibstyle == BIBSTYLE_APALIKE) {  /* can't use Word refs for APALIKE or APACITE */
            t = s ? s : key;
            if (!first_key) {
            	ConvertString(g_bibpunct_cite_sep);
                fprintRTF(" ");
            }
            ConvertString(t);
        }
        if (g_document_bibstyle == BIBSTYLE_AUTHORDATE) {
            if (!first_key) {
            	ConvertString(g_bibpunct_cite_sep);
                fprintRTF(" ");
            }
            t = s ? s : key;
            if (code == CITE_SHORT)
                g_suppress_name = TRUE;
            ConvertString(t);
            if (code == CITE_SHORT)
                g_suppress_name = FALSE;
        }
        if (g_document_bibstyle == BIBSTYLE_APACITE) {
            if (!first_key) {
            	ConvertString(g_bibpunct_cite_sep);
                fprintRTF(" ");
            }
            t = s ? s : key;
            g_current_cite_seen = citation_used(key);
            ConvertString(t);
        }
        
        if (g_document_bibstyle == BIBSTYLE_STANDARD) {
            char *signet = strdup_nobadchars(key);

            if (!first_key) {
            	ConvertString(g_bibpunct_cite_sep);
                fprintRTF(" ");
            }
            t = s ? s : signet; /* if .aux is missing or * incomplete use original * citation */
            if (fields_use_REF()) {
                fprintRTF("{\\field{\\*\\fldinst{\\lang1024 REF BIB_%s \\\\* MERGEFORMAT }}", signet);
                fprintRTF("{\\fldrslt{");
            }
            ConvertString(t);
            if (fields_use_REF())
                fprintRTF("}}}");
            if (signet)
                free(signet);
        }
        
        first_key = FALSE;
        key = next_keys;
        next_keys = popCommaName(key);  /* key modified to be a * single key */
        if (s)
            free(s);
    }

    /* final text after citation */
    if (option) {
   		ConvertString(g_bibpunct_postnote_sep);
        ConvertString(option);
    }

    if (g_current_cite_paren) {
        fprintRTF("\n"); 
        ConvertString(g_bibpunct_close);
    }

    if (keys)
        free(keys);
    if (option)
        free(option);
    if (pretext)
        free(pretext);
}

/******************************************************************************
purpose: handles \citations for natbib package
******************************************************************************/
void CmdNatbibCite(int code)
{
    char *text, *str1;
    char *keys, *key, *next_keys;
    char *option = NULL;
    char *pretext = NULL;
    int first_key = TRUE;
    int last_key = FALSE;

    /* Setup punctuation and read options before citation */
    g_current_cite_paren = TRUE;
    g_last_author_cited[0] = '\0';
    g_last_year_cited[0] = '\0';

	if (!g_bibpunct_cite_sep_touched) {
		free(g_bibpunct_cite_sep);
		g_bibpunct_cite_sep = strdup(";");
	}
	
	if (!g_bibpunct_style_paren_touched) {
		free(g_bibpunct_cite_sep);
		g_bibpunct_cite_sep = strdup(";");
	}

	pretext = getBracketParam();
	option = getBracketParam();
	if (!option) {
		option = pretext;
		pretext = '\0';
	}
	if (code != CITE_P && code != CITE_P_STAR && code != CITE_YEAR_P)
		g_current_cite_paren = FALSE;
    
    if (g_bibpunct_style == BIB_STYLE_SUPER)
    	g_current_cite_paren = FALSE;

    if (g_bibpunct_style == BIB_STYLE_NUMBER)
    	g_current_cite_paren = TRUE;
    	
    text = getBraceParam();
    str1 = strdup_nocomments(text);
    free(text);
    text = str1;
        
	/* no citation, just clean up and exit */
	if (strlen(text) == 0) {
        free(text);
        if (pretext)
            free(pretext);
        if (option)
            free(option);
        return;
    }

	/* superscript style */
	if (g_bibpunct_style == BIB_STYLE_SUPER)
        fprintRTF("{\\up%d\\fs%d ", script_shift(), script_size());
    
    /* write open parenthesis before citation starts */
    if (g_current_cite_paren) 
        ConvertString(g_bibpunct_open);

    /* clean-up keys and sort if necessary */
    keys = strdup_noblanks(text);
    free(text);
    if (g_sorted_citations){
    	text = reorder_citations(keys,1);
    	free(keys);
    	keys = text;
    }

    /* now process each citation key */
    g_current_cite_item = 0;
    key = keys;
    next_keys = popCommaName(key);
    last_key = !next_keys;
    
    while (key) {
        char *s;

        g_current_cite_item++;

		if (strcmp(key,BIB_DASH_MARKER)==0) { /* a citation, not a dash */
		
			fprintRTF("-");    /* just write a dash */
	        first_key = TRUE;  /* no comma after dash */
			
		} else {
		
			/* look up citation and write it to the RTF stream */
			s = ScanAux("bibcite", key, 0, g_aux_name);
				
			diagnostics(4, "natbib key=[%s] <%s> ", key, s);
			if (s) {
				g_current_cite_seen = citation_used(key);
				ConvertNatbib(s, code, pretext, option, first_key, last_key);
			} else {
				if (!first_key) {
					ConvertString(g_bibpunct_cite_sep);
					fprintRTF(" ");
				}
				ConvertString(key);
			}
        	if (s) free(s);
        	first_key = FALSE;
        }
        
        key = next_keys;
        next_keys = popCommaName(key);
		last_key = !next_keys;
    }

    if (g_current_cite_paren)
        ConvertString(g_bibpunct_close);

	if (g_bibpunct_style == BIB_STYLE_SUPER)
        fprintRTF("}"); 
	
	if (keys)
        free(keys);
    if (option)
        free(option);
    if (pretext)
        free(pretext);
}

/******************************************************************************
purpose: handles \citations for harvard.sty
******************************************************************************/
void CmdHarvardCite(int code)
{
    char *text, *s;
    char *keys, *key, *next_keys;
    char *posttext = NULL;
    char *pretext = NULL;
    int first_key = TRUE;

    /* Setup punctuation and read options before citation */
    g_current_cite_paren = TRUE;
    g_last_author_cited[0] = '\0';
    g_last_year_cited[0] = '\0';
	if (code == CITE_AS_NOUN || code == CITE_YEAR_STAR || 
		code == CITE_NAME || code == CITE_POSSESSIVE)
		g_current_cite_paren = FALSE;

	/* read citation entry */
	posttext = getBracketParam();    
    text = getBraceParam();
    if (code == CITE_AFFIXED) 
    	pretext = getBraceParam();
    s = strdup_nocomments(text);
    free(text);
    text = s;
        
    if (strlen(text) == 0) {
        free(text);
        if (pretext) free(pretext);
        if (posttext)free(posttext);
        return;
    }
    
    /* output text before citation */
    if (g_current_cite_paren) {
        fprintRTF("\n"); 
        ConvertString(g_bibpunct_open);
    }

    /* clean-up keys and sort if necessary */
    keys = strdup_noblanks(text);
    free(text);
    if (g_sorted_citations){
    	text = reorder_citations(keys,0);
    	free(keys);
    	keys = text;
    }

    /* now start processing keys */
    key = keys;
    next_keys = popCommaName(key);

    g_current_cite_item = 0;
    while (key) {
        char *ss;

        g_current_cite_item++;

		if (strcmp(key,BIB_DASH_MARKER)==0) {
			fprintRTF("-");
        	first_key = TRUE;
        	key = next_keys;
        	next_keys = popCommaName(key); 
			continue;
		}

        ss = ScanAux("harvardcite", key, 2, g_aux_name); /* look up bibliographic reference */
            
		diagnostics(4, "harvard key=[%s] <%s>", key, ss);
		
		if (!first_key) {
			ConvertString(g_bibpunct_cite_sep);
			fprintRTF(" ");
		}

		if (ss) {
			g_current_cite_seen = citation_used(key);
			ConvertHarvard(ss, code, pretext, NULL, first_key);
		} else 
			ConvertString(key);
        
        first_key = FALSE;
        key = next_keys;
        next_keys = popCommaName(key);
        if (ss)
            free(ss);
    }

    /* final text after citation */
    if (posttext) {
        fprintRTF("%s", g_bibpunct_postnote_sep);
        ConvertString(posttext);
    }
    
    if (g_current_cite_paren) {
        fprintRTF("\n"); 
        ConvertString(g_bibpunct_close);
    }

    if (keys)
        free(keys);
    if (posttext)
        free(posttext);
    if (pretext)
        free(pretext);
}

static void putHtmlRTF(const char *style)
{
	int n;
	
	if (style) {
		/*  possible styles are "tt", "rm", "sf", and "same" */
		if (strstr(style,"rm")) 
			CmdFontFamily(F_FAMILY_ROMAN);
		else if (strstr(style,"tt"))
			CmdFontFamily(F_FAMILY_TYPEWRITER);
		else if (strstr(style,"sf"))
			CmdFontFamily(F_FAMILY_SANSSERIF);
	} else
		CmdFontFamily(F_FAMILY_TYPEWRITER);
		
	n = existsDefinition("UrlFont");
	if (n != -1) ConvertString("\\UrlFont");

}
/******************************************************************************
purpose: just create a hyperlink using word fields
******************************************************************************/
static void InsertRtfHyperlink(const char *text,    const char *url, 
                               const char *baseurl, const char *style)
{
	
	char * fullurl = strdup_together(baseurl,url);
	fprintRTF("{");
	putHtmlRTF(style);
	fprintRTF("\\field{\\*\\fldinst{ HYPERLINK \"");
	putRtfStrEscaped(fullurl);
	fprintRTF("\" }{{}}}{\\fldrslt{");
	ConvertString(text);
	fprintRTF("}}}");
	free(fullurl);
}

/******************************************************************************
purpose: handles \htmladdnormallink{text}{link}
******************************************************************************/
void CmdHtml(int code)
{
    static char *baseurl = NULL;
    static char *urlstyle = NULL;
    char *text=NULL;
    char *url=NULL;
    char *s = NULL;

	switch (code) {
		case LABEL_HTMLADDNORMALREF:
			
			text = getBraceParam();
			url = getBraceParam();
	
			while ((s = strstr(text, "\\~{}")) != NULL) {
				*s = '~';
				my_strcpy(s + 1, s + 4);
			}
			while ((s = strstr(url, "\\~{}")) != NULL) {
				*s = '~';
				my_strcpy(s + 1, s + 4);
			}
			InsertRtfHyperlink(text, url, NULL, NULL);
			break;

        
		case LABEL_HTMLREF:
			text = getBraceParam();
			url = getBraceParam();
			ConvertString(text);
			break;
        
		case LABEL_HYPERREF:
			/* \hyperref[label]{text} or \hyperref{url}{category}{name}{text} */
			url = getBracketParam();
			if (!url) {
				char *a, *b, *category, *name;
				a = getBraceParam();
				category = getBraceParam();
				name = getBraceParam();
				b = strdup_together3(a,"#",category);
				url = strdup_together3(b,".",name);
				free(b);
				free(name);
				free(category);
				free(a);
			}
			text = getBraceParam();
			InsertRtfHyperlink(text, url, baseurl, urlstyle);
			break;

		case LABEL_HREF:
			url = getBraceParam();
			text = getBraceParam();
			InsertRtfHyperlink(text, url, baseurl, urlstyle);
			break;

		case LABEL_URL_HYPER:
			/* cannot use insertHyperlink because url has toxic characters */
	        url = getBraceRawParam();
			text = strdup_together(baseurl,url);
			fprintRTF("{");
			putHtmlRTF(urlstyle);
			fprintRTF("\\field{\\*\\fldinst{ HYPERLINK \"");
			putRtfStrEscaped(text);
			fprintRTF("\" }{{}}}{\\fldrslt{");
			putRtfStrEscaped(text);
			fprintRTF("}}}");
			break;

		case LABEL_URL:
		case LABEL_NO_LINK_URL:
	        url = getBraceRawParam();
	        text = strdup_together(baseurl,url);
			fprintRTF("{");
			putHtmlRTF(urlstyle);
    		putRtfStrEscaped(text);
			fprintRTF("}");
			break;

		case LABEL_BASE_URL:
    		if (baseurl) free(baseurl);
        	baseurl = getBraceRawParam();
			break;

		case LABEL_URLSTYLE:
    		if (urlstyle) free(urlstyle);
        	urlstyle = getBraceParam();
			break;
    }

	if (text) free(text);
	if (url) free(url);
}

void CmdBCAY(int code)
{
    char *s=NULL, *t, *v, *year;

    s = getBraceParam();

    diagnostics(4, "Entering CmdBCAY", s);

    t = getBraceParam();
    year = getBraceParam();
    v = g_current_cite_seen ? t : s;

    diagnostics(4, "s    = <%s>", s);
    diagnostics(4, "t    = <%s>", t);
    diagnostics(4, "year = <%s>", year);
    diagnostics(4, "type = %d, seen = %d, item= %d", g_current_cite_type, g_current_cite_seen, g_current_cite_item);

    switch (g_current_cite_type) {

        case CITE_CITE:
        case CITE_CITE_NP:
        case CITE_CITE_A:
            if (strcmp(v, g_last_author_cited) != 0) {  /* suppress repeated names */
                ConvertString(v);
                my_strlcpy(g_last_author_cited, v, MAX_AUTHOR_SIZE);
                my_strlcpy(g_last_year_cited, year, MAX_YEAR_SIZE);

                if (g_current_cite_type == CITE_CITE_A)
                    fprintRTF(" (");
                else
                    fprintRTF(", ");
            }
            ConvertString(year);
            if (g_current_cite_type == CITE_CITE_A)
                fprintRTF(")");
            break;

        case CITE_CITE_AUTHOR:
            ConvertString(v);
            break;

        case CITE_FULL:
        case CITE_FULL_NP:
        case CITE_FULL_A:
            ConvertString(s);
            if (g_current_cite_type == CITE_FULL_A)
                fprintRTF(" (");
            else
                fprintRTF(", ");

            ConvertString(year);
            if (g_current_cite_type == CITE_FULL_A)
                fprintRTF(")");
            break;

        case CITE_FULL_AUTHOR:
            ConvertString(s);
            break;

        case CITE_SHORT:
        case CITE_SHORT_NP:
        case CITE_SHORT_A:
        case CITE_SHORT_AUTHOR:
            ConvertString(t);
            if (g_current_cite_type == CITE_SHORT_A)
                fprintRTF(" (");
            else
                fprintRTF(", ");

            ConvertString(year);
            if (g_current_cite_type == CITE_SHORT_A)
                fprintRTF(")");
            break;

        case CITE_YEAR:
        case CITE_YEAR_NP:
            ConvertString(year);
            break;

    }
    free(s);
    free(t);
    free(year);
}

static void ConvertBraceParam(char *pre, char *post)
{
	char *s;
	s = getBraceParam();
	if (s && strlen(s) > 0) {
		fprintRTF("%s",pre);
		ConvertString(s);
		fprintRTF("%s",post);
	}
		
	if (s) free(s);
}

static void DiscardBraceParam(void)
{
	char *s;
	s = getBraceParam();
	if (s) free(s);
}

/******************************************************************************
purpose: handles apacite stuff
******************************************************************************/
void CmdApaCite(int code)
{
    int n;
    char *s;
	char * month[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
	switch (code) {
        case 0:
            fprintRTF(" (");
            break;              /* BBOP */
        case 1:
            fprintRTF("&");
            break;              /* BBAA */
        case 2:
            fprintRTF("and");
            break;              /* BBAB */
        case 3:
            fprintRTF(", ");
            break;              /* BBAY */ 
        case 4:
            fprintRTF("; ");
            break;              /* BBC */
        case 5:
            fprintRTF(", ");
            break;              /* BBN */
        case 6:
            fprintRTF(")");
            break;              /* BBCP */
        case 7:
            fprintRTF("");
            break;              /* BBOQ */
        case 8:
            fprintRTF("");
            break;              /* BBCQ */
        case 9:
            fprintRTF(",");
            break;              /* BCBT */
        case 10:
            fprintRTF(",");
            break;              /* BCBL */
        case 11:
        	s = getBraceParam();
            fprintRTF("et al.");
            free(s);
            break;              /* BOthers */
        case 12:
            fprintRTF("in press");
            break;              /* BIP */
        case 13:
            fprintRTF("and");
            break;              /* BAnd */
        case 14:
            fprintRTF("Ed.");
            break;              /* BED */
        case 15:
            fprintRTF("Eds.");
            break;              /* BEDS */
        case 16:
            fprintRTF("Trans.");
            break;              /* BTRANS */
        case 17:
            fprintRTF("Trans.");
            break;              /* BTRANSS */
        case 18:
            fprintRTF("Chair");
            break;              /* BCHAIR */
        case 19:
            fprintRTF("Chairs");
            break;              /* BCHAIRS */
        case 20:
            fprintRTF("Vol.");
            break;              /* BVOL */
        case 21:
            fprintRTF("Vols.");
            break;              /* BVOLS */
        case 22:
            fprintRTF("No.");
            break;              /* BNUM */
        case 23:
            fprintRTF("Nos.");
            break;              /* BNUMS */
        case 24:
            fprintRTF("ed.");
            break;              /* BEd */
        case 25:
            fprintRTF("p.");
            break;              /* BPG */
        case 26:
            fprintRTF("pp.");
            break;              /* BPGS */
        case 27:
            fprintRTF("Tech. Rep.");
            break;              /* BTR */
        case 28:
            fprintRTF("Doctoral dissertation");
            break;              /* BPhD */
        case 29:
            fprintRTF("Unpublished doctoral dissertation");
            break;              /* BUPhD */
        case 30:
            fprintRTF("Master's thesis");
            break;              /* BMTh */
        case 31:
            fprintRTF("Unpublished master's thesis");
            break;              /* BUMTh */
        case 32:
            fprintRTF("Original work published ");
            break;              /* BOWP */
        case 33:
            fprintRTF("Reprinted from ");
            break;              /* BREPR */
        case 34:
            s = getBraceParam();               /* BCnt {1} */
            if (sscanf(s, "%d", &n) == 1)
                fprintRTF("%c", (char) 'a' + n - 1);
            free(s);
            break;
        case 35:
            if (g_current_cite_paren || g_in_bibliography)
            	fprintRTF("&");
            else
            	fprintRTF("and");    /* BBA */
            break;
        case 36:
            DiscardBraceParam();    /* \AX{entry} */
            diagnostics(4, "Ignoring \\AX{blah blah}");
            break;
        case 37:
            fprintRTF(". ");
            break;              /* BPBI */
        case 38:
            fprintRTF("In");
            break;              /* BIn */

        case CITE_APA_CITE_METASTAR:
            ConvertString("$\\star");
            break;
       
        case CITE_APA_CITE_YEAR:
			ConvertBraceParam("","");     /* \APACyear{1991} */
            break;

        case CITE_APA_CITE_A_TITLE:
            ConvertBraceParam("\\ldblquote ","\\rdblquote ");
            break;

         case CITE_APA_CITE_B_TITLE:
            ConvertBraceParam("{\\i ","}");  /* \APACcitebtitle{title} */
            break;

        case CITE_APA_CITE_INSERT:
            DiscardBraceParam();    /* discard \APACinsertmetastar{art 1} ?? */
            break;

        case CITE_APA_YMD:
            fprintRTF("(");
            ConvertBraceParam("","");     /* \APACrefYearMonthDay{1991}{month}{day} */
            ConvertBraceParam(", ","");   /* month */
            ConvertBraceParam(" ","");    /* day */
            fprintRTF(")");
            break;

        case CITE_APA_REF_A_TITLE:
            DiscardBraceParam();    /* ignore first entry?? */
            ConvertBraceParam("","");   /* \APACrefatitle{title}{title} */
            break;
            
        case CITE_APA_REF_B_TITLE:
            DiscardBraceParam();    /* ignore first entry?? */
            ConvertBraceParam("{\\i ","}");    /* \APACrefbtitle{title}{title} */
            break;

        case CITE_APA_JVNP:
            ConvertBraceParam("{\\i ","}");    /*  \APACjournalVolNumPages{Journal of nothingness}{2}{}{1-2} */           
            ConvertBraceParam(", {\\i ","}");   /* volume */
			ConvertBraceParam("(",")"); /* number (10) */
			ConvertBraceParam(", ",""); /* pages */
            break;
        
        case CITE_APA_REF_YEAR:
            ConvertBraceParam("(",")");  /* \APACrefYear{1991} */
            break;
        
		case CITE_APA_ADD_PUB:
            ConvertBraceParam("",": ");    /* \APACaddressPublisher{Somewhere}{PublishCo} */
            ConvertBraceParam("","");  
            break;
	
		case CITE_PRINT_BACK_REFS:  /* ignore \PrintBackRefs{\CurrentBib} */
			DiscardBraceParam();			
			break;

		case CITE_PRINT_ORDINAL:  
		case CITE_PRINT_CARDINAL:  
			ConvertBraceParam("","");
			break;

		case CITE_APA_ADD_PUB_EQ_AUTHOR:  
      /* \APACaddressPublisherEqAuth{Washington, DC}{{American Psychiatric Association}} */
            ConvertBraceParam("",": Author");
			DiscardBraceParam();			
            break;

		case CITE_APA_REF_A_E_TITLE:   /* english translation of article */
			DiscardBraceParam();			
            ConvertBraceParam("[","]");
			break;
			
		case CITE_APA_REF_B_E_TITLE:   /* english translation of book */
			DiscardBraceParam();			
            ConvertBraceParam("[","]");
			break;

		case CITE_APA_MONTH:
			s = getBraceParam();
			if (s && *s) {
				sscanf(s, "%d", &n);
            	ConvertString(month[n-1]);
				free(s);
			}
			break;
			
		case CITE_APA_B_VOL_ED_TR:    /* \APACbVolEdTR{}{tech report}*/
			DiscardBraceParam();			
            ConvertBraceParam("(",")");
			break;
			
		case CITE_APA_B_VOL_ED_TR_PGS:    /* \APACbVolEdTRpgs{}{tech report}{}*/
            fprintRTF("(");
			DiscardBraceParam();			
            ConvertBraceParam("","");     /* \APACbVolEdTRpgs{}{tech report}{}*/
            ConvertBraceParam(", ","");   /* more info */
            fprintRTF(")");
			break;
			
		case CITE_APA_ADD_INST:   /* APACaddressInstitution{add}{inst} */
            ConvertBraceParam("","");
            ConvertBraceParam(": ","");   /* more info */
			break;
			
		case CITE_APA_HOW:
            ConvertBraceParam("","");
			break;
			
		case CITE_APA_ORIG_YEAR_NOTE:
            ConvertBraceParam("(Original work published ",")");
			DiscardBraceParam();			
			break;

		case CITE_APA_ORIG_JOUR:
			s = getBraceParam();   /* year */
            ConvertBraceParam("(Reprinted from {\\i  ","}"); /* article */
            if (s && *s) {
            	fprintRTF(", ");
            	ConvertString(s);
            	free(s);
            }
            
            ConvertBraceParam(", {\\i ","}");   /* volume */
			ConvertBraceParam("(",")"); /* number (10) */
			ConvertBraceParam(", ",""); /* pages */
            fprintRTF(")");
			break;

		case CITE_APA_REF_NOTE:
			ConvertBraceParam("(",")"); 
			break;

		case CITE_APA_UNSKIP:   /*do nothing! */
			break;
			
       default:;
    }
}

/******************************************************************************
purpose: handles \citename from authordate bib style
******************************************************************************/
void CmdCiteName(int code)
{
    char *s=NULL;

    s = getBraceParam();

    diagnostics(4, "Entering CmdCitename [%s]", (s) ? s : "");

    if (!g_suppress_name)
        ConvertString(s);

    free(s);

}

/******************************************************************************
purpose: handles \numberline{3.2.1}
******************************************************************************/
void CmdNumberLine(int code)
{
    char *number;

    number = getBraceParam();
    diagnostics(4, "Entering CmdNumberLine [%s]", number);
    ConvertString(number);
    fprintRTF("\\tab\n");
    free(number);
}

/******************************************************************************
purpose: handles \harvarditem{a}{b}{c} \harvardyearleft \harvardyearright
******************************************************************************/
void CmdHarvard(int code)
{
    char *s=NULL;

	if (code == CITE_HARVARD_ITEM) {
		s = getBracketParam();
		if (s) free(s);
		s = getBraceParam();
		free(s);
		s = getBraceParam();
		free(s);
		s = getBraceParam();
		free(s);
	}
	
	if (code == CITE_HARVARD_YEAR_LEFT) 
		fprintRTF("(");

	if (code == CITE_HARVARD_YEAR_RIGHT) 
		fprintRTF(")");
		
	if (code == CITE_HARVARD_AND)
		fprintRTF("&");
}

/******************************************************************************
purpose: handles \citename from authordate bib style
******************************************************************************/
void CmdContentsLine(int code)
{
    char *type, *text, *num, *contents_type;

    type = getBraceParam();
    text = getBraceParam();
    num = getBraceParam();

    diagnostics(4, "Entering CmdContentsLine %s [%s]", type, text);

    startParagraph("contents", SECTION_TITLE_PARAGRAPH);
    fprintRTF("{");
    contents_type = strdup_together("contents_", type);
    InsertStyle(contents_type);
    fprintRTF(" ");
    ConvertString(text);
    CmdEndParagraph(0);
    fprintRTF("}");

    free(type);
    free(text);
    free(num);
    free(contents_type);
}

/******************************************************************************
purpose: handles \listoffigures \listoftables
******************************************************************************/
void CmdListOf(int code)
{
	char c = ' ';
	
    diagnostics(4, "Entering CmdListOf");

	startParagraph("contents", SECTION_TITLE_PARAGRAPH);
	fprintRTF(" ");
	
	switch (code) {
	
		case LIST_OF_FIGURES:
			ConvertBabelName("LISTFIGURENAME");
			c = 'f';
			break;
			
		case LIST_OF_TABLES:
			ConvertBabelName("LISTTABLENAME");
			c = 't';
			break;
	
		case TABLE_OF_CONTENTS:
			ConvertBabelName("CONTENTSNAME");
			c = 'c';
			break;
	}

	CmdEndParagraph(0);

	startParagraph("Normal", GENERIC_PARAGRAPH);
	CmdVspace(VSPACE_SMALL_SKIP);
	g_tableofcontents = TRUE;
	fprintRTF("{\\field{\\*\\fldinst TOC \\\\f %c }{\\fldrslt }}\n",c);  
	CmdNewPage(NewPage);
	CmdEndParagraph(0);
}

static void GetAcronymFromAux(char *key, char **shortname, char **longname)
{
	char  *s, *t;
	*shortname = NULL;
	*longname = NULL;
	
	s = ScanAux("newacro", key, 3, g_aux_name); /* get short & long name */

	t=s;
	while (*t && *t != ']')
		t++;
	
	if (*t == ']') 
		*t = '\0';
	else
		return;
	
	*shortname = strdup(s);
	*longname = strdup(t+1);
	free(s);
}

static void incrementAcronymCounter(char *key)
{
	char *s = strdup_together("ACRO~",key);
	incrementCounter(s);
	free(s);
}

static int getAcronymCounter(char *key)
{
	int n;
	char *s = strdup_together("ACRO~",key);
	n=getCounter(s);
	free(s);
	return n;
}

/*
\ac{key}  	Expand and identify the acronym the first time; use only the acronym thereafter
\acf{key} 	Use the full name of the acronym.
\acs{key} 	Use the acronym, even before the first corresponding \ac command
\acl{key} 	Expand the acronym without using the acronym itself.

\acf{SW}  	Scientific Word (SW) documents are beautifully typeset.
\acs{SW} 	SW documents are beautifully typeset.
\acl{SW} 	Scientific Word documents are beautifully typeset.

\acp{label}
    plural form of acronym by adding an s. \acfp. \acsp, \aclp work as well.

\acfi{label}  Now like \acf{label}
*/
void CmdAcronymAc(int code)
{
	char      *key, *shortname, *longname;
	int        true_code = code & 0x0f;
	int        plural    = (true_code == 0x10);
	int        star      = (true_code == ACRONYM_STAR);
	int        mark_it_used = (true_code == ACRONYM_USED);
	
	key = getBraceParam();
	GetAcronymFromAux(key, &shortname, &longname);

	diagnostics(5,"CmdAcronymAc key='%s'",key);
	diagnostics(5,"code=%d, true_code=%d",code, true_code);
	diagnostics(5,"short='%s'",shortname);
	diagnostics(5,"long='%s'",longname);
	diagnostics(5,"plural=%d, star=%d, used=%d",plural, star, mark_it_used);	
	
	if (shortname == NULL) {
		diagnostics(WARNING, "Undefined acronym '%s' not defined in .aux file", key);
		free(key);
		return;
	}
		
	if (true_code == ACRONYM_AC) {
		if (getAcronymCounter(key)) 
			true_code = ACRONYM_ACS;
		else 
			true_code = ACRONYM_ACF;
	}
	
	if (true_code == ACRONYM_ACF) {
		ConvertString(longname);
		if (plural == TRUE)
			ConvertString("s (");
		else
			ConvertString(" (");
		
		ConvertString(shortname);
		if (plural == TRUE)
			ConvertString(")s");
		else
			ConvertString(")");

		mark_it_used |= (star == FALSE);
	}
	
	if (true_code == ACRONYM_ACS) {
		ConvertString(shortname);
		if (plural == TRUE)
			ConvertString("s");
		mark_it_used |= (star == FALSE);
	}
	
	if (true_code == ACRONYM_ACL) {
		ConvertString(longname);
		if (plural == TRUE)
			ConvertString("s");
		mark_it_used |= (star == FALSE);
	}

	if (true_code == ACRONYM_USED) 
		mark_it_used = 1;

	if (mark_it_used) 
		incrementAcronymCounter(key);

	free(key);
}


void CmdAcronymUsed(int code)
{
	char *key = getBraceParam();
	incrementAcronymCounter(key);
	free(key);
}

void CmdAcronymReset(int code)
{
	zeroKeyCounters("ACRO~");
}

void CmdAcronymDef(int code)
{
	CmdIgnoreParameter(21);
}

/*
static void CmdAcronym(int code)
{
if (code & ON) {
		PushEnvironment(ACRONYM_MODE);
		CmdVspace(VSPACE_BIG_SKIP);
		startParagraph("section", SECTION_TITLE_PARAGRAPH);
		fprintRTF("{");
		InsertStyle("section");
		ConvertString((SpanishMode) ? "Acr\\'{o}nimos" : "Acronyms");
		CmdEndParagraph(0);
		fprintRTF("}");
		CmdVspace(VSPACE_SMALL_SKIP);
	} else 
		PopEnvironment();
		
		
}
*/
void CmdAC(int code)
{
	char *shortAc, *key;
	int i;
	int n = (int) strlen("@hyperlink");
	
	/* skip '@hyperlink' */
	for (i=0; i<n; i++) 
		getTexChar();
		
	key = getBraceParam();
	shortAc = getBraceParam();
	ConvertString(shortAc);
	free(shortAc);
	free(key);
}

