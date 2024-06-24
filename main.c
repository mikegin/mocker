#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define MAX_CMD_LEN 100

int main(int argc, char ** args)
{
    char * first = args[0];

    char cmd[MAX_CMD_LEN] = "", **p;

    if (argc > 2)
    {
        char * second = args[1];
        if (strcmp(second, "run") != 0)
        {
            fprintf(stderr, "Unrecognized second argument.\nUsage: %s run <command> <args>\n", first);
            return EXIT_FAILURE;
        }
        
        strcat(cmd, args[2]);
        
        for(p = &args[3]; *p; p++)
        {
            strcat(cmd, " ");
            strcat(cmd, *p);
        }

        system(cmd);

        return 0;
    }
    else
    {
        fprintf(stderr, "Usage: %s run <command> <args>\n", first);
        return EXIT_FAILURE;
    }
}