/* vertical.c - routines that handle vertical space issues

Copyright (C) 2002 The Free Software Foundation

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

TeX has six modes:
	
	MODE_VERTICAL              Building the main vertical list, from which the 
	                           pages of output are derived
	              
	MODE_INTERNAL_VERTICAL     Building a vertical list from a vbox
	
	MODE_HORIZONTAL            Building a horizontal list for a paragraph
	
	MODE_RESTICTED_HORIZONTAL  Building a horizontal list for an hbox
	
	MODE_MATH                  Building a mathematical formula to be placed in a 
	                           horizontal list
	                           
	MODE_DISPLAYMATH           Building a mathematical formula to be placed on a
	                           line by itself, temporarily interrupting the current paragraph
	                           
LaTeX has three modes: paragraph mode, math mode, or left-to-right mode.
This is not a particularly useful, since paragraph mode is a combination of
vertical and horizontal modes. 
                         
Why bother keeping track of modes?  Mostly so that paragraph indentation gets handled
correctly, as well as vertical and horizontal space.

The tricky part is that latex2rtf is a one-pass converter.  The right thing
must be done with each character.  Consider the sequence 

           \vspace{1cm}\noindent New paragraph.

When latex2rtf reaches the 'N', then a new paragraph should be started.  We
know it is a new paragraph because \vspace should have put the converter 
into a MODE_VERTICAL and characters are only emitted in MODE_HORIZONTAL.  

RTF needs to set up the entire paragraph at one time and the 

    * text alignment
    * line spacing
	* vertical space above
	* left margin
	* right margin
	* paragraph indentation 
	
must all be emitted at this time.  This file contains routines
that affect these quantities
 ******************************************************************************/

#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <ctype.h>
#include "main.h"
#include "funct1.h"
#include "cfg.h"
#include "utils.h"
#include "parser.h"
#include "lengths.h"
#include "vertical.h"
#include "convert.h"
#include "commands.h"

static int g_TeX_mode = MODE_VERTICAL;
static int g_line_spacing = 240;
static int g_paragraph_no_indent = FALSE;
static int g_paragraph_inhibit_indent = FALSE;
static int g_vertical_space_to_add = 0;
static int g_right_margin_indent;
static int g_left_margin_indent;
static int g_page_new = FALSE;
static int g_column_new = FALSE;
static int g_alignment = JUSTIFIED;

char TexModeName[7][25] = { "bad", "internal vertical", "horizontal",
    "restricted horizontal", "math", "displaymath", "vertical"
};

void setLeftMarginIndent(int indent)
{
	g_left_margin_indent = indent;
}
void setRightMarginIndent(int indent)
{
	g_right_margin_indent = indent;
}
int getLeftMarginIndent(void)
{
	return g_left_margin_indent;
}
int getRightMarginIndent(void)
{
	return g_right_margin_indent;
}

void setAlignment(int align)
{
	g_alignment = align;
}

int getAlignment(void)
{
	return g_alignment;
}

void SetTexMode(int mode, int just_set_it)
{

    if (just_set_it) {
        diagnostics(5, "Forcing mode change from [%s] -> [%s]", TexModeName[g_TeX_mode], TexModeName[mode]);
        g_TeX_mode = mode;
        return;
    } else
        diagnostics(5, "TeX mode changing from [%s] -> [%s]", TexModeName[g_TeX_mode], TexModeName[mode]);


    if (g_TeX_mode == MODE_VERTICAL && mode == MODE_HORIZONTAL)
        startParagraph("body", ANY_INDENT);

    if (g_TeX_mode == MODE_HORIZONTAL && mode == MODE_VERTICAL)
        CmdEndParagraph(0);

    g_TeX_mode = mode;
}

int GetTexMode(void)
{
    return g_TeX_mode;
}

void startParagraph(const char *style, int indenting)

/******************************************************************************
	RTF codes to create a new paragraph.  If the paragraph should
	not be indented then emit \fi0 otherwise use the current value
	of \parindent as the indentation of the first line.
	
	style describes the type of paragraph ... 
	  "body"
	  "caption"
	  "author"
	  "bibitem"
	  "section"
	  etc.
	  
	indenting describes how this paragraph and (perhaps) the following
	paragraph should be indented
	
	  TITLE_INDENT  (do not indent this paragraph or the next)
	  FIRST_INDENT  (do not indent this paragraph but indent the next)
	  ANY_INDENT    (indent as needed)
	
	Sometimes it is necessary to know what the next paragraph will
	be before it has been parsed.  For example, a section command
	should create a paragraph for the section title and then the
	next paragraph encountered should be handled like as a first 
	paragraph.  
	
	For FIRST_INDENT, then it is the first paragraph in a section.
	Usually the first paragraph is not indented.  However, when the
	document is being typeset in french it should have normal indentation.
	Another special case occurs when the paragraph being typeset is
	in a list environment.  In this case, we need to indent according
	to the current parindent to obtain the proper hanging indentation
	
	The default is to indent according to
	the current parindent.  However, if the g_paragraph_inhibit_indent
	flag or the g_paragraph_no_indent flag is TRUE, then do not indent
	the next line.  Typically these flags are set just after a figure
	or equation or table.
	
 ******************************************************************************/
{
    int parindent;
	static int status = 0;
	
    parindent = getLength("parindent");

    if (indenting == TITLE_INDENT) {      /* titles are never indented */
        parindent = 0;
        status = 1;
    	diagnostics(5, "TITLE_INDENT");
    }
    else if (indenting == FIRST_INDENT) { /* French indents the first paragraph */
    	diagnostics(5, "FIRST_INDENT");
    	status = 1;
    	if (!FrenchMode && !g_processing_list_environment)
        	parindent = 0;
	} else {                              /* Worry about not indenting */
    	diagnostics(5, "ANY_INDENT");
	    if (g_paragraph_no_indent || g_paragraph_inhibit_indent)
        	parindent = 0;
        else if (status > 0) 
        	parindent = 0;
        status--;
	}
	

    diagnostics(5, "startParagraph mode = %s", TexModeName[GetTexMode()]);
    diagnostics(5, "Noindent is         %s", (g_paragraph_no_indent) ? "TRUE" : "FALSE");
    diagnostics(5, "Inhibit is          %s", (g_paragraph_inhibit_indent) ? "TRUE" : "FALSE");
    diagnostics(5, "indent is           %d", g_left_margin_indent);
    diagnostics(5, "right indent is     %d", g_right_margin_indent);
    diagnostics(5, "current parindent   %d", getLength("parindent"));
    diagnostics(5, "paragraph indent is %d", parindent);

    if (g_page_new) {
        fprintRTF("\\page{} ");   /* causes new page */
        g_page_new = FALSE;
        g_column_new = FALSE;
    }

    if (g_column_new) {
        fprintRTF("\\column "); /* causes new page */
        g_column_new = FALSE;
    }

    fprintRTF("\\pard\\q%c\\sl%i\\slmult1 ", getAlignment(), g_line_spacing);

    if (g_vertical_space_to_add > 0)
        fprintRTF("\\sb%d ", g_vertical_space_to_add);
    g_vertical_space_to_add = 0;

    if (g_left_margin_indent != 0)
        fprintRTF("\\li%d", g_left_margin_indent);

    if (g_right_margin_indent != 0)
        fprintRTF("\\ri%d", g_right_margin_indent);

    fprintRTF("\\fi%d ", parindent);

    SetTexMode(MODE_HORIZONTAL,TRUE); 

    if (!g_processing_list_environment) {
        g_paragraph_no_indent = FALSE;
        if (indenting == TITLE_INDENT)
        	g_paragraph_inhibit_indent = TRUE;
        else
        	g_paragraph_inhibit_indent = FALSE;
    }
}

void CmdEndParagraph(int code)

/******************************************************************************
     purpose : ends the current paragraph and return to MODE_VERTICAL.
 ******************************************************************************/
{
    int mode = GetTexMode();

    diagnostics(5, "CmdEndParagraph mode = %s", TexModeName[mode]);
    if (mode != MODE_VERTICAL  && g_processing_fields == 0) {
        fprintRTF("\\par\n");
        SetTexMode(MODE_VERTICAL,TRUE); /* TRUE value avoids calling CmdEndParagraph! */
    }

    g_paragraph_inhibit_indent = FALSE;
}

void SetVspaceDirectly(int vspace)
{
    g_vertical_space_to_add = vspace;
}

void CmdVspace(int code)

/******************************************************************************
     purpose : vspace, vspace*, and vskip
     		   code ==  0 if vspace or vspace*
     		   code == -1 if vskip
     		   code ==  1 if \smallskip
     		   code ==  2 if \medskip
     		   code ==  3 if \bigskip
 ******************************************************************************/
{
    int vspace=0;
    char c;

    switch (code) {
        case VSPACE_VSPACE:
            vspace = getDimension();
            break;

        case VSPACE_VSKIP:
            while ((c = getTexChar()) && c != '{') {
            }
            vspace = getDimension();
            parseBrace();
            break;

        case VSPACE_SMALL_SKIP:
            vspace = getLength("smallskipamount");
            break;

        case VSPACE_MEDIUM_SKIP:
            vspace = getLength("medskipamount");
            break;

        case VSPACE_BIG_SKIP:
            vspace = getLength("bigskipamount");
            break;
    }

	SetTexMode(MODE_VERTICAL,TRUE);
    SetVspaceDirectly(vspace);
}

void CmdIndent(int code)

/******************************************************************************
 purpose : set flags so that startParagraph() does the right thing
     
     	   INDENT_INHIBIT allows the next paragraph to be indented if
     	   a paragraph break occurs before startParagraph() is called
     			     		
           INDENT_NONE tells startParagraph() to not indent the next paragraph
           
           INDENT_USUAL has startParagraph() use the value of \parindent
 ******************************************************************************/
{
    diagnostics(5, "CmdIndent mode = %d", GetTexMode());
    if (code == INDENT_NONE)
        g_paragraph_no_indent = TRUE;

    else if (code == INDENT_INHIBIT)
        g_paragraph_inhibit_indent = TRUE;

    else if (code == INDENT_USUAL) {
        g_paragraph_no_indent = FALSE;
        g_paragraph_inhibit_indent = FALSE;
    }
    diagnostics(5, "Noindent is %d", (int) g_paragraph_no_indent);
    diagnostics(5, "Inhibit  is %d", (int) g_paragraph_inhibit_indent);
}

void CmdNewPage(int code)

/******************************************************************************
  purpose: starts a new page
parameter: code: newpage or newcolumn-option
 globals: twocolumn: true if twocolumn-mode is set
 ******************************************************************************/
{
    switch (code) {
        case NewPage:
            g_page_new = TRUE;
            break;

        case NewColumn:
            g_column_new = TRUE;
            break;
    }
}

/******************************************************************************
  purpose: support for \doublespacing
 ******************************************************************************/
void CmdDoubleSpacing(int code)
{
	g_line_spacing = 480;
}

void CmdAlign(int code)

/*****************************************************************************
    purpose : sets the alignment for a paragraph
  parameter : code: alignment centered, justified, left or right
 ********************************************************************************/
{
    char *s;
    static char old_alignment_before_center = JUSTIFIED;
    static char old_alignment_before_right = JUSTIFIED;
    static char old_alignment_before_left = JUSTIFIED;
    static char old_alignment_before_centerline = JUSTIFIED;

    if (code == PAR_VCENTER) {
        s = getBraceParam();
        free(s);
        return;
    }

    CmdEndParagraph(0);
    switch (code) {
        case (PAR_CENTERLINE):
            old_alignment_before_centerline = getAlignment();
            setAlignment(CENTERED);
            fprintRTF("{");
            diagnostics(4, "Entering Convert from CmdAlign (centerline)");
            Convert();
            diagnostics(4, "Exiting Convert from CmdAlign (centerline)");
            setAlignment(old_alignment_before_centerline);
            CmdEndParagraph(0);
            fprintRTF("}");
            break;

        case (PAR_RAGGEDRIGHT):
            old_alignment_before_centerline = getAlignment();
            setAlignment(LEFT);

/*		fprintRTF("{"); */
            diagnostics(4, "Entering Convert from CmdAlign (centerline)");
            Convert();
            diagnostics(4, "Exiting Convert from CmdAlign (centerline)");
            setAlignment(old_alignment_before_centerline);
            CmdEndParagraph(0);

/*		fprintRTF("}");*/
            break;

        case (PAR_CENTER | ON):
            CmdIndent(INDENT_NONE);
            old_alignment_before_center = getAlignment();
            setAlignment(CENTERED);
            break;
        case (PAR_CENTER | OFF):
            setAlignment(old_alignment_before_center);
            CmdEndParagraph(0);
            CmdIndent(INDENT_INHIBIT);
            break;

        case (PAR_RIGHT | ON):
            old_alignment_before_right = getAlignment();
            setAlignment(RIGHT);
            CmdIndent(INDENT_NONE);
            break;
        case (PAR_RIGHT | OFF):
            setAlignment(old_alignment_before_right);
            CmdIndent(INDENT_INHIBIT);
            break;

        case (PAR_LEFT | ON):
            old_alignment_before_left = getAlignment();
			setAlignment(LEFT);
			CmdIndent(INDENT_NONE);
            break;
        case (PAR_LEFT | OFF):
            setAlignment(old_alignment_before_left);
            CmdIndent(INDENT_INHIBIT);
            break;
        case (PAR_CENTERING):
            CmdIndent(INDENT_NONE);
            old_alignment_before_center = getAlignment();
            setAlignment(CENTERED);
            break;
    }
}
