int     maybeDefinition(char * s, int n);
int     existsDefinition(char * s);
void	newDefinition(char *name, char *def, int params);
void	renewDefinition(char * name, char * def, int params);
char *	expandDefinition(int thedef);

int		existsEnvironment(char * s);
void	newEnvironment(char *name, char *begdef, char *enddef, int params);
void	renewEnvironment(char *name, char *begdef, char *enddef, int params);
void	expandEnvironment(int thedef, int starting);


