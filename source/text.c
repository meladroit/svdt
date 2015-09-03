// some text routines from 3dsfindskitten
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "text.h"

void gotoxy(int x, int y)
{
    // in other words, move to row y, column x
    // (both zero-indexed!)
    printf("\x1b[0m");
    printf("\x1b[%d;%dH",y,x);
}

void textcolour(enum colour c)
{
    printf("\x1b[0m");
    if(c>>3) // i.e. if normal intensity
    {
        printf("\x1b[3%d;1m",c%8);
    }
    else
    {
        printf("\x1b[3%dm",c%8);
    }
}

int lastSpace(char *tocut)
{
    char *lastSpace;
    lastSpace = strrchr(tocut,' ');
    if(lastSpace)
    {
        return (int)(lastSpace-tocut);
    } else { return -1; }
}

void wordwrap(char *towrap, int width)
{
    int idx = 0;
    int i, lastSpc;
    char* buffer = (char*)malloc(width+1);
    memset(buffer,0,width+1);
    do
    {
        strncpy(buffer,towrap+idx,width);
        if(strlen(buffer)<width)
        {
            printf(buffer);
            putchar(' ');
            break;
        }
        if(towrap[idx+width]==' ')
        {
            printf(buffer);
            idx = idx + width;
        } else {
            lastSpc = lastSpace(buffer);
            if (lastSpc>0)
            {
                for(i=0;i<lastSpc;i++)
                {
                    putchar(buffer[i]);
                    idx++;
                }
                putchar('\n');
            } else {
                for(i=0;i<width-1;i++)
                {
                    putchar(buffer[i]);
                }
                printf("- ");
                idx = idx + width - 1;
            }
        }
    }while (strlen(buffer)==width);
}
