/* $Id: commands.h,v 1.14 2001/08/22 05:50:23 prahl Exp $ */

#define PREAMBLE 1
#define DOCUMENT 2
#define ITEMIZE 3
#define ENUMERATE 4
#define DESCRIPTION 5
#define TABBING 6
#define GERMANMODE 7
#define LETTER 8
#define IGN_ENV_CMD 9
#define HYPERLATEX 10
#define FIGURE_ENV 11

#define ON 0x4000
#define OFF 0x0000

void            PushEnvironment(int code);
void            PopEnvironment();
void            ClearEnvironment();
bool            CallCommandFunc(char *cCommand);
bool            CallParamFunc(char *cCommand, int AddParam);
int             CurrentEnvironmentCount(void);
