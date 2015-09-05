#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <3ds.h>

#include "filesystem.h"
#include "text.h"
#include "utils.h"

#define MAX_PATH_LENGTH 1024
#define MAX_LS_LINES HEIGHT-4
#define CURSOR_WIDTH 3
#define LIST_WIDTH (TOP_WIDTH/2-CURSOR_WIDTH)
#define RES_OUT_OF_SPACE_CARD 0xc86044cd  // probably
#define RES_OUT_OF_SPACE_ESHOP 0xd8604664 // at least I think so
typedef struct lsLine
{
    char thisLine[MAX_PATH_LENGTH];
    u8 isDirectory;
    u64 fileSize;
    struct lsLine* nextLine;
} lsLine;

typedef struct lsDir
{
    char thisDir[MAX_PATH_LENGTH];
    int dirEntryCount;
    int lsOffset;
    struct lsLine* firstLine;
} lsDir;

void freeDir(lsDir* dir)
{
    if (!dir) return;
    // still totally cribbing from 3ds_hb_menu
    lsLine* line = dir->firstLine;
    lsLine* temp = NULL;
    while(line)
    {
        temp = line->nextLine;
        line->nextLine = NULL;
        free(line);
        line = temp;
    }
    dir->dirEntryCount = 0;
    dir->firstLine = NULL;
}

int canHasConsole = 0;

// hey look yet another 3ds_hb_menu derivative
void gotoParentDirectory(lsDir* dir)
{
    char* cwd = dir->thisDir;
    char *p = cwd + strlen(cwd)-2;
    while(p > cwd && *p != '/') *p-- = 0;
}

void gotoSubDirectory(lsDir* dir, char* basename)
{
    if (!dir || !basename) return;
    char* cwd = dir->thisDir;
    cwd = strcat(cwd,basename);
    if (cwd[strlen(cwd+1)] != '/')
    {
        //printf("\nappending slash");
        cwd = strcat(cwd,"/");
    }
    if (canHasConsole)
        printf("\nsubdir path: %s\n",cwd);
}

char* lsDirBasename(lsDir* dir)
{
    char* ret = NULL;
    // our convention is awkward enough to have to find the second-to-last slash in most cases
    char* baseidx = dir->thisDir;
    char* baseidx2 = strrchr(dir->thisDir,'/');
    while (baseidx!=baseidx2)
    {
        ret = baseidx+1;
        baseidx = strchr(baseidx+1,'/');
    }
    return ret;
}

void scanDir(lsDir* dir, FS_archive* archive, Handle* fsHandle)
{
    if (!dir) return;

    dir->firstLine = NULL;
    Handle dirHandle;
    Result res = FSUSER_OpenDirectory(fsHandle, &dirHandle, *archive, FS_makePath(PATH_CHAR, dir->thisDir));
    dir->dirEntryCount = 0;
    dir->lsOffset = 0;
    // cribbing from 3ds_hb_menu again
    u32 entriesRead;
    lsLine* lastLine = NULL;
    static char pathname[MAX_PATH_LENGTH];
    do
    {
        static FS_dirent entry;
        memset(&entry,0,sizeof(FS_dirent));
        entriesRead=0;
        res = FSDIR_Read(dirHandle, &entriesRead, 1, &entry);
        if (res)
        {
            if (canHasConsole)
            {
                printf("error reading directory\n");
                printf("result code %08x\n",(unsigned int)res);
            }
            return;
        }
        unicodeToChar(pathname,entry.name,MAX_PATH_LENGTH);
        if(entriesRead)
        {
            lsLine* tempLine = (lsLine*)malloc(sizeof(lsLine));
            strncpy(tempLine->thisLine,pathname,MAX_PATH_LENGTH);
            tempLine->isDirectory = entry.isDirectory;
            tempLine->fileSize = entry.fileSize;
            tempLine->nextLine = NULL;
            if(dir->firstLine)
            {
                lastLine->nextLine = tempLine;
            } else {
                dir->firstLine = tempLine;
            }
            lastLine = tempLine;
            dir->dirEntryCount++;
        }
    }while (entriesRead);
    FSDIR_Close(dirHandle); // oh god how did I forget this line
}

const char HOME[2] = "/";
const char SDMC_CURSOR[4] = ">>>";
const char SAVE_CURSOR[4] = ">>>";

enum state
{
    SELECT_SDMC,
    SELECT_SAVE,
    CONFIRM_DELETE,
    CONFIRM_OVERWRITE,
    SVDT_IS_KILL
};
enum state machine_state;
enum state previous_state;

PrintConsole titleBar, sdmcList, saveList, sdmcCursor, saveCursor;
PrintConsole statusBar;//, instructions;

void debugOut(char* garbled)
{
    consoleSelect(&statusBar);
    printf("\n");//consoleClear();
    textcolour(NEONGREEN);
    printf("%s\n",garbled);
}

int detectOverwrite(char* path, lsDir* destDir)
{   
    if (!destDir || !path) return -1;
    if (canHasConsole)
    {
        debugOut("checking for overwrite risk");
        printf("checking against %s",path);
    }
    lsLine* curLine = destDir->firstLine;
    if (canHasConsole)
        debugOut("starting check");
    while (curLine)
    {
        if (!(curLine->isDirectory))
            if (!strcmp(curLine->thisLine,path))
            {
                if (canHasConsole)
                    printf("found overwrite: %s\n",curLine->thisLine);
                return 1;
            }
        curLine = curLine->nextLine;
    }
    if (canHasConsole)
        printf("no overwrite\n");
    return 0;
}

void copyFile(lsDir* dir, char* path, u64 size, lsDir* destDir)
{
    if (!dir || !destDir || !path) return;
    Handle* curFsHandle = NULL;
    Handle* destFsHandle = NULL;
    FS_archive* curArchive = NULL;
    FS_archive* destArchive = NULL;
    switch(machine_state)
    {
        case SELECT_SAVE:
            curFsHandle = &saveGameFsHandle;
            curArchive = &saveGameArchive;
            destFsHandle = &sdmcFsHandle;
            destArchive = &sdmcArchive;
            break;
        case SELECT_SDMC:
            curFsHandle = &sdmcFsHandle;
            curArchive = &sdmcArchive;
            destFsHandle = &saveGameFsHandle;
            destArchive = &saveGameArchive;
            break;
        default:
            break;
    }
    
    char origpath[MAX_PATH_LENGTH] = {0};
    char destpath[MAX_PATH_LENGTH] = {0};
    strncpy(origpath,dir->thisDir,MAX_PATH_LENGTH);
    strcat(origpath,path);
    strncpy(destpath,destDir->thisDir,MAX_PATH_LENGTH);
    strcat(destpath,path);
    
    u8* data = (u8*) malloc(size);
    
    if (canHasConsole)
    {
        debugOut("reading original file ...");
        printf("original path %s\n",origpath);
    }
    Result res = loadFile(origpath,data,curArchive,curFsHandle,size);
    if(res)
    {
        if (canHasConsole)
        {
            debugOut("error reading file");
            printf("result code %08x",(unsigned int)res);
        }
        return;
    }
    if (canHasConsole)
    {
        debugOut("writing new file ...");
        printf("destination path %s\n",destpath);
    }
    res = writeFile(destpath,data,(u32)size,destArchive,destFsHandle);
    free(data);
    if(res)
    {
        if (canHasConsole)
        {
            debugOut("error writing file");
            printf("result code %08x\n",(unsigned int)res);
            if(res==RES_OUT_OF_SPACE_CARD || res==RES_OUT_OF_SPACE_ESHOP)
                printf("(you may be running out of save space!)\n");
        }
        machine_state = SVDT_IS_KILL;
        return;
    }
    if (canHasConsole)
        debugOut("success!");
}

void copyDir(lsDir* dir, char* path, lsDir* destDir, char* destName)
{
    if (!dir || !destDir) return;
    Handle* curFsHandle = NULL;
    Handle* destFsHandle = NULL;
    FS_archive* curArchive = NULL;
    FS_archive* destArchive = NULL;
    
    char origpath[MAX_PATH_LENGTH] = {0};
    char destpath[MAX_PATH_LENGTH] = {0};
    strncpy(origpath,dir->thisDir,MAX_PATH_LENGTH);
    if (path)
        strcat(origpath,path);
    if (canHasConsole)
        debugOut("constructing paths:");
    strncpy(destpath,destDir->thisDir,MAX_PATH_LENGTH);
    if (canHasConsole)
        printf("origpath %s\n",origpath);
    switch(machine_state)
    {
        case SELECT_SAVE:
            curFsHandle = &saveGameFsHandle;
            curArchive = &saveGameArchive;
            destFsHandle = &sdmcFsHandle;
            destArchive = &sdmcArchive;
            break;
        case SELECT_SDMC:
            curFsHandle = &sdmcFsHandle;
            curArchive = &sdmcArchive;
            destFsHandle = &saveGameFsHandle;
            destArchive = &saveGameArchive;
            break;
        default:
            break;
    }
    if (canHasConsole)
        debugOut("got handles");
    //debugOut("manipulating destDir->thisDir");
    //printf("currently %s\n",destDir->thisDir);
    if (destName)
    {
        strcat(destpath,destName);
        FSUSER_CreateDirectory(destFsHandle,*destArchive,FS_makePath(PATH_CHAR,destpath));
        gotoSubDirectory(destDir,destName);
    } else {
        if (path)
        {
            strcat(destpath,path);
            FSUSER_CreateDirectory(destFsHandle,*destArchive,FS_makePath(PATH_CHAR,destpath));
            gotoSubDirectory(destDir,path);
        }
    }
    if(destArchive==&saveGameArchive)
    {
        if (canHasConsole)
            printf("\ncalling ControlArchive\n");
        FSUSER_ControlArchive(saveGameFsHandle, saveGameArchive);
        // this is absolutely necessary
        // otherwise any changes we make don't stick!
    }
    if (path)
        gotoSubDirectory(dir,path);
    //printf("currently %s\n",destDir->thisDir);
    //printf("destpath %s",destpath);
    scanDir(dir,curArchive,curFsHandle);
    lsLine* curLine = dir->firstLine;
    while (curLine && (machine_state!=SVDT_IS_KILL))
    {
        if (curLine->isDirectory)
        {
            copyDir(dir,curLine->thisLine,destDir,NULL);
        }
        else
        {
            copyFile(dir,curLine->thisLine,curLine->fileSize,destDir);
        }    
        curLine = curLine->nextLine;
    }
    if (path)
        gotoParentDirectory(dir);
    gotoParentDirectory(destDir);
    scanDir(dir,curArchive,curFsHandle);
}

void printDir(lsDir* dir)
{
    switch(machine_state)
    {
        case SELECT_SAVE:
            consoleSelect(&saveList);
            break;
        case SELECT_SDMC:
            consoleSelect(&sdmcList);
            break;
        default:
            break;
    }
    consoleClear();
    int lines_available = MAX_LS_LINES;
    int i;
    char lineOut[LIST_WIDTH] = {0};
    textcolour(PURPLE);
    if(strlen(dir->thisDir)<LIST_WIDTH)
    {
        strncpy(lineOut,dir->thisDir,LIST_WIDTH-1);
    } else {
        strncpy(lineOut,lsDirBasename(dir),LIST_WIDTH-1);
        if(strlen(lsDirBasename(dir))>=LIST_WIDTH)
        {
            char x;
            for(i=0;i<5;i++)
            {
                switch(i)
                {
                    case 0:
                        x = ']';
                        break;
                    case 4:
                        x = '[';
                        break;
                    default:
                        x = '.';
                        break;
                }
                lineOut[LIST_WIDTH-2-i] = x;
            }
        }
    }
    printf("%s\n",lineOut);//lsDirBasename(dir));
    lines_available--;
    if (!dir->lsOffset)
    {
        if (strcmp("/",(const char*)dir->thisDir))
            printf("../\n");
        else
            printf("[is root]\n");
        lines_available--;
    }
    lsLine* currentLine = dir->firstLine;
    for (i=1;i<dir->lsOffset;i++)
    {
        if(currentLine)
            currentLine = currentLine->nextLine;
    }
    while (currentLine && lines_available)
    {
        if(currentLine->isDirectory)
            textcolour(MAGENTA);
        else
            textcolour(WHITE);
        strncpy(lineOut,currentLine->thisLine,LIST_WIDTH-1);
        if(strlen(currentLine->thisLine)>=LIST_WIDTH)
        {
            char x;
            for(i=0;i<5;i++)
            {
                switch(i)
                {
                    case 0:
                        x = ']';
                        break;
                    case 4:
                        x = '[';
                        break;
                    default:
                        x = '.';
                        break;
                }
                lineOut[LIST_WIDTH-2-i] = x;
            }
        }
        printf("%s\n",lineOut);
        lines_available--;
        //printf("[REDACTED]\n");
        currentLine = currentLine->nextLine;
    }
}

void redrawCursor(int* cursor_y, lsDir* dir)
{
    if (dir->lsOffset && (*cursor_y == 0))
    {
        dir->lsOffset--;
        *cursor_y = 1;
        //debugOut("reached top of listing");
        //printf("dir->lsOffset is %d",dir->lsOffset);
        printDir(dir);
    }
    if (*cursor_y<0)
        *cursor_y = 0;
    int cursor_y_bound = HEIGHT-5;
    if (dir->dirEntryCount+1<cursor_y_bound)
        cursor_y_bound = dir->dirEntryCount+1;
    if (*cursor_y>cursor_y_bound)
    {
        *cursor_y = cursor_y_bound;
        if (dir->dirEntryCount+2>MAX_LS_LINES+dir->lsOffset)
        {
            dir->lsOffset++;
            //debugOut("scrolling down listing");
            //printf("dir->lsOffset is %d",dir->lsOffset);
            
            printDir(dir);
        }
    }
    switch(machine_state)
    {
        case SELECT_SAVE:
            consoleSelect(&saveCursor);
            consoleClear();
            gotoxy(0,*cursor_y);
            printf(SAVE_CURSOR);
            break;
        case SELECT_SDMC:
            consoleSelect(&sdmcCursor);
            consoleClear();
            gotoxy(0,*cursor_y);
            printf(SDMC_CURSOR);
            break;
        default:
            break;
    }
}

void printInstructions()
{
    consoleSelect(&statusBar);
    consoleClear();
    textcolour(WHITE);
    wordwrap("svdt is tdvs, reversed and without vowels. Use it to transfer files between your SD card and your save data. (Directories marked in purple.) If you don't see any save data, restart until you can select a target app.\n",BOTTOM_WIDTH);
    wordwrap("> Press L/R to point at save/SD data, and up/down to point at a specific file or folder.\n",BOTTOM_WIDTH);
    wordwrap("> Press X to delete file or folder.\n",BOTTOM_WIDTH);
    wordwrap("> Press A to navigate inside a folder. Press B to return to the parent folder, if there is one.\n",BOTTOM_WIDTH);
    wordwrap("> Press Y to copy selected file/folder to the working directory of the other data. Use the topmost listing to dump working directory.\n",BOTTOM_WIDTH);
    wordwrap("> Press SELECT to reprint these instructions at any time.\n",BOTTOM_WIDTH);
    wordwrap("> Press START to stop while you're ahead.\n",BOTTOM_WIDTH);
}

int checkInjectDirectory(char* path, lsDir* dir)
{   
    lsLine* curLine = dir->firstLine;
    while (curLine)
    {
        if (curLine->isDirectory)
            if (!strcmp(curLine->thisLine,path))
                return 1;
        curLine = curLine->nextLine;
    }
    return 0;
}

int main()
{
	filesystemInit();
    lsDir cwd_sdmc, cwd_save;
    strncpy(cwd_sdmc.thisDir,HOME,MAX_PATH_LENGTH);
    strncpy(cwd_save.thisDir,HOME,MAX_PATH_LENGTH);
    scanDir(&cwd_sdmc,&sdmcArchive,&sdmcFsHandle);
    scanDir(&cwd_save,&saveGameArchive,&saveGameFsHandle);
    
    hidScanInput();
    
    if(hidKeysHeld() & KEY_L)
    {
        // super secret emergency save backup mode
        // for games that won't give away services so easily, like ACNL
        machine_state = SELECT_SAVE;
        char timeStr[16] = {0};                                    
        time_t temps = time(NULL);
        strftime(timeStr,16,"%Y%m%d_%H%M%S",gmtime(&temps));
        copyDir(&cwd_save,NULL,&cwd_sdmc,timeStr);
        scanDir(&cwd_sdmc,&sdmcArchive,&sdmcFsHandle);
        canHasConsole = 2;
    }
    
    if((hidKeysHeld() & KEY_R) && !canHasConsole)
    {
        // super secret emergency save write mode
        // again, for games like ACNL that won't play nice
        machine_state = SELECT_SDMC;
        if (!checkInjectDirectory("svdt_inject", &cwd_sdmc))
        {
            canHasConsole = -1;
        } else {
            gotoSubDirectory(&cwd_sdmc,"svdt_inject");
            freeDir(&cwd_sdmc);
            scanDir(&cwd_sdmc,&sdmcArchive,&sdmcFsHandle);
            copyDir(&cwd_sdmc,NULL,&cwd_save,NULL);
            scanDir(&cwd_save,&saveGameArchive,&saveGameFsHandle);
            gotoParentDirectory(&cwd_sdmc);
            scanDir(&cwd_sdmc,&sdmcArchive,&sdmcFsHandle);
            canHasConsole = 3;
        }
    }
    
    gfxInitDefault();
	gfxSet3D(false);

	consoleInit(GFX_TOP, &titleBar);
	consoleInit(GFX_TOP, &sdmcList);
	consoleInit(GFX_TOP, &saveList);
	consoleInit(GFX_TOP, &sdmcCursor);
	consoleInit(GFX_TOP, &saveCursor);
	consoleInit(GFX_BOTTOM, &statusBar);
	//consoleInit(GFX_BOTTOM, &instructions);

    consoleSetWindow(&titleBar,0,0,TOP_WIDTH,HEIGHT);
    consoleSetWindow(&saveCursor,0,3,3,HEIGHT-3);
    consoleSetWindow(&saveList,CURSOR_WIDTH,3,LIST_WIDTH,HEIGHT-3);
    consoleSetWindow(&sdmcCursor,TOP_WIDTH/2,3,CURSOR_WIDTH,HEIGHT-3);
    consoleSetWindow(&sdmcList,TOP_WIDTH/2+CURSOR_WIDTH,3,LIST_WIDTH,HEIGHT-3);
    consoleSetWindow(&statusBar,0,0,BOTTOM_WIDTH,HEIGHT);//8);
    //consoleSetWindow(&instructions,0,8,BOTTOM_WIDTH,HEIGHT-2);
    
    printInstructions();
    switch (canHasConsole)
    {
        case -1:
            debugOut("emergency inject invoked without directory");        
            break;
        case 2:
            debugOut("emergency dump to SD was invoked");
            break;
        case 3:
            debugOut("emergency savegame inject was invoked");
            break;
        default:
            debugOut("successful startup, I guess. Huh.");
            break;
    }
    canHasConsole = 1;
    
	consoleSelect(&titleBar);
    textcolour(SALMON);
    printf("svdt 0.1, meladroit/willidleaway\n");
    printf("a hacked-together save data explorer/manager\n");
    gotoxy(CURSOR_WIDTH,2);
    textcolour(GREY);
    printf("save data:");
    gotoxy(TOP_WIDTH/2,2);
    printf("SD data:");
    
    machine_state = SELECT_SAVE;
    printDir(&cwd_save);
    previous_state = machine_state;
    machine_state = SELECT_SDMC;
    printDir(&cwd_sdmc);
    
    consoleSelect(&sdmcCursor);
    consoleClear();
    gotoxy(0,0);
    printf(SDMC_CURSOR);
    int cursor_y = 0;
    char overwritePath[MAX_PATH_LENGTH] = {0};
    char deletePath[MAX_PATH_LENGTH] = {0};
    u64 overwriteSize = 0;
    lsDir* ccwd = &cwd_sdmc;
    lsDir* notccwd = &cwd_save;
    PrintConsole* curList = &sdmcList;
    Handle* curFsHandle = &sdmcFsHandle;
    FS_archive* curArchive = &sdmcArchive;
    
    u8 sdmcCurrent, sdmcPrevious;
    sdmcCurrent = 1;
            
	while (aptMainLoop())
	{
		hidScanInput();
		if(hidKeysDown() & KEY_START)break;
        if(machine_state == SVDT_IS_KILL)
        {
            if(previous_state != SVDT_IS_KILL)
            {
                consoleSelect(&statusBar);
                int i;
                for (i=0;i<5;i++)
                {                
                    textcolour((enum colour)((1+i*6)%16));
                    wordwrap("svdt has encountered an error, and has halted to protect your data. Press START to exit.\n", BOTTOM_WIDTH);
                }
                previous_state = SVDT_IS_KILL;
            }
            gspWaitForVBlank();
            // Flush and swap framebuffers
            gfxFlushBuffers();
            gfxSwapBuffers();
            continue;
        }
        int cwd_needs_update = 0;
        int notccwd_needs_update = 0;
        sdmcPrevious = sdmcCurrent; 
        FSUSER_IsSdmcDetected(NULL, &sdmcCurrent);
        if(sdmcCurrent != sdmcPrevious)
        {
            if(sdmcPrevious)
            {
                consoleSelect(&statusBar);
                consoleClear();
                textcolour(RED);
                wordwrap("svdt cannot detect your SD card. To continue using it, please eject and reinstall your SD card, or force-reboot your 3DS.", BOTTOM_WIDTH);
            } else {
                filesystemSoftReset();
                if(curList == &sdmcList)
                {
                    cwd_needs_update = 1;
                } else {
                    notccwd_needs_update = 1;
                }
            }
        }
        if(hidKeysDown() & KEY_SELECT)
        {
            printInstructions();
        }
        if(hidKeysDown() & KEY_X)
        {
            switch (machine_state)
            {
                case CONFIRM_DELETE:
                    debugOut("attempting to delete selection");
                    int i;
                    switch (cursor_y)
                    {
                        case 0:
                            break;
                        case 1:
                            if (!ccwd->lsOffset)
                                break;
                        default: ;
                            strncpy(deletePath,ccwd->thisDir,MAX_PATH_LENGTH);
                            lsLine* selection = ccwd->firstLine;
                            for (i=0;i<cursor_y+ccwd->lsOffset-2;i++)
                            {
                                selection = selection->nextLine;
                            }
                            debugOut(selection->thisLine);
                            printf(" isDir: %d",(int)selection->isDirectory);
                            strcat(deletePath,selection->thisLine);
                            if(selection->isDirectory)
                            {
                                FSUSER_DeleteDirectoryRecursively(curFsHandle,*curArchive,FS_makePath(PATH_CHAR,deletePath));
                                if(curArchive==&saveGameArchive)
                                {
                                    //printf("\ncalling ControlArchive\n");
                                    FSUSER_ControlArchive(saveGameFsHandle, saveGameArchive);
                                }
                            } else {
                                deleteFile(deletePath,curArchive,curFsHandle);
                            }
                            break;
                    }
                    machine_state = previous_state;
                    previous_state = CONFIRM_DELETE;
                    cwd_needs_update = 1;
                    break;
                default:
                    previous_state = machine_state;
                    machine_state = CONFIRM_DELETE;
                    debugOut("press X again to confirm delete!");
            }
        } else {
            if (hidKeysDown() && (machine_state == CONFIRM_DELETE))
            {
                machine_state = previous_state;
                previous_state = CONFIRM_DELETE;
                debugOut("delete unconfirmed");
            }
        }
        if(hidKeysDown() & KEY_Y)
        {
            if (machine_state == CONFIRM_OVERWRITE)
            {
                machine_state = previous_state;
                previous_state = CONFIRM_OVERWRITE;
                if (curArchive==&sdmcArchive)
                    deleteFile(deletePath,&saveGameArchive,&saveGameFsHandle);
                if (curArchive==&saveGameArchive)
                    deleteFile(deletePath,&sdmcArchive,&sdmcFsHandle);
                    
                copyFile(ccwd,overwritePath,overwriteSize,notccwd);
                notccwd_needs_update = 1;
            } else {
                debugOut("attempting to copy selection");
                int i;
                switch (cursor_y)
                {
                    case 0:
                        if (!strcmp("/",(const char*)ccwd->thisDir))
                        {
                            debugOut("dumping root");            
                            char timeStr[16] = {0};                                    
                            time_t temps = time(NULL);
                            strftime(timeStr,16,"%Y%m%d_%H%M%S",gmtime(&temps));
                            printf("using timestamp %s",timeStr);
                            copyDir(ccwd,NULL,notccwd,timeStr);
                        }
                        else
                        {
                            debugOut("dumping subdirectory");
                            printf("using basename %s",lsDirBasename(ccwd));
                            copyDir(ccwd,NULL,notccwd,lsDirBasename(ccwd));
                        }
                        notccwd_needs_update = 1;
                        break;
                    case 1:
                        if (!ccwd->lsOffset)
                            break;
                    default: ;
                        lsLine* selection = ccwd->firstLine;
                        for (i=0;i<cursor_y+ccwd->lsOffset-2;i++)
                        {
                            selection = selection->nextLine;
                        }
                        debugOut(selection->thisLine);
                        printf(" isDir: %d",(int)selection->isDirectory);
                        if(selection->isDirectory)
                        {
                            copyDir(ccwd,selection->thisLine,notccwd,NULL);
                            notccwd_needs_update = 1;
                        } else {
                            if (detectOverwrite(selection->thisLine,notccwd))
                            {
                                debugOut("possible overwrite detected\npress Y again to confirm!");
                                previous_state = machine_state;
                                machine_state = CONFIRM_OVERWRITE;
                                strncpy(overwritePath,selection->thisLine,MAX_PATH_LENGTH);
                                overwriteSize = selection->fileSize;
                            } else {
                                copyFile(ccwd,selection->thisLine,selection->fileSize,notccwd);
                                notccwd_needs_update = 1;
                            }
                        }
                        break;
                }
            }
        } else {
            if (hidKeysDown() && (machine_state == CONFIRM_OVERWRITE))
            {
                machine_state = previous_state;
                previous_state = CONFIRM_OVERWRITE;
                debugOut("overwrite unconfirmed");
            }
        }
        if(hidKeysDown() & (KEY_L | KEY_DLEFT))
        {
            consoleSelect(&sdmcCursor);
            consoleClear();
            previous_state = machine_state;
            machine_state = SELECT_SAVE;
            ccwd = &cwd_save;
            notccwd = &cwd_sdmc;
            curList = &saveList;
            curFsHandle = &saveGameFsHandle;
            curArchive = &saveGameArchive;
            redrawCursor(&cursor_y,ccwd);
        }
        if((hidKeysDown() & (KEY_R | KEY_DRIGHT)) && sdmcCurrent)
        {
            consoleSelect(&saveCursor);
            consoleClear();
            machine_state = SELECT_SDMC;
            ccwd = &cwd_sdmc;
            notccwd = &cwd_save;
            curList = &sdmcList;
            curFsHandle = &sdmcFsHandle;
            curArchive = &sdmcArchive;
            redrawCursor(&cursor_y,ccwd);
        }
        if(hidKeysDown() & (KEY_UP))
        {
            cursor_y--;
            redrawCursor(&cursor_y,ccwd);
        }
        if(hidKeysDown() & (KEY_DOWN))
        {
            cursor_y++;
            redrawCursor(&cursor_y,ccwd);
        }
        if(hidKeysDown() & KEY_B)
        {
            if (strcmp("/",(const char*)ccwd->thisDir))
            {
                gotoParentDirectory(ccwd);
                debugOut("navigating to parent directory");
                cwd_needs_update = 1;
            }
        }
        if(hidKeysDown() & KEY_A)
        {
            switch (cursor_y)
            {
                case 0:
                    debugOut("refreshing current directory");
                    cwd_needs_update = 1;
                    break;
                case 1:
                    if (strcmp("/",(const char*)ccwd->thisDir))
                    {
                        gotoParentDirectory(ccwd);
                        debugOut("navigating to parent directory");
                        cwd_needs_update = 1;
                    }
                    break;
                default: ;
                    lsLine* selection = ccwd->firstLine;
                    int i;
                    for (i=0;i<cursor_y+ccwd->lsOffset-2;i++)
                    {
                        selection = selection->nextLine;
                    }
                    debugOut(selection->thisLine);
                    printf(" isDir: %d",(int)selection->isDirectory);
                    if (selection->isDirectory)
                    {
                        cwd_needs_update = 1;
                        //consoleSelect(&statusBar);
                        //textcolour(NEONGREEN);
                        //printf("isDirectory is %d",(int)(selection->isDirectory));
                        //debugOut("moving to initDir");
                        //temp_ccwd = (lsDir*)malloc(sizeof(lsDir));
                        //memset(temp_ccwd,0,sizeof(lsDir));
                        gotoSubDirectory(ccwd,selection->thisLine);
                        debugOut("navigating to subdirectory");
                    }
                    break;
            }
        }
        if(machine_state==SVDT_IS_KILL)
            continue;
        if(cwd_needs_update)
        {
            freeDir(ccwd);
            scanDir(ccwd,curArchive,curFsHandle);
            debugOut("scanned current directory");
            printf("%s\n dirEntryCount %d",ccwd->thisDir,ccwd->dirEntryCount);
            //debugOut("and now some formalities");
            consoleSelect(curList);
            consoleClear();
            printDir(ccwd);
            redrawCursor(&cursor_y,ccwd);
        }
        if(notccwd_needs_update)
        {
            freeDir(notccwd);
            previous_state = machine_state;
            switch (machine_state)
            {
                case SELECT_SAVE:
                    scanDir(notccwd,&sdmcArchive,&sdmcFsHandle);
                    machine_state = SELECT_SDMC;
                    break;
                case SELECT_SDMC:
                    scanDir(notccwd,&saveGameArchive,&saveGameFsHandle);
                    machine_state = SELECT_SAVE;
                    break;
                default:
                    break;
            }
            
            debugOut("scanned not-current directory");
            printf("%s\n dirEntryCount %d",notccwd->thisDir,notccwd->dirEntryCount);
            //debugOut("and now some formalities");
            printDir(notccwd);
            enum state temp_state = previous_state;
            previous_state = machine_state;
            machine_state = temp_state;
            redrawCursor(&cursor_y,notccwd);
        }
		gspWaitForVBlank();
        // Flush and swap framebuffers
        gfxFlushBuffers();
        gfxSwapBuffers();
	}

	filesystemExit();

	gfxExit();
	return 0;
}
