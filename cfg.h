/* $Id: cfg.h,v 1.15 2003/01/06 01:26:06 prahl Exp $*/

#define DIRECT_A	0
#define FONT_A		1
#define IGNORE_A	2
#define LANGUAGE_A  3

#ifndef CFGDIR
#define CFGDIR ""
#endif

#ifndef TRUE
#define TRUE  1
#define FALSE 0
#endif

typedef int (*fptr) (const void*, const void*);

typedef struct ConfigEntryT
{
   const char  *TexCommand;
   const char  *RtfCommand;
} ConfigEntryT;

void 			ReadLanguage(char *lang);
void 			ConvertBabelName(char *name);

void 			ReadCfg (void);
int 			SearchRtfIndex (const char *theCommand, int WhichArray);
char   		   *SearchRtfCmd (const char *theCommand, int WhichArray);
ConfigEntryT  **CfgStartIterate (int WhichCfg);
ConfigEntryT  **CfgNext (int WhichCfg, ConfigEntryT **last);

