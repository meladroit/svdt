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
#define RES_OUT_OF_SPACE 0xc86044cd
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

// hey look yet another 3ds_hb_menu derivative
void gotoParentDirectory(lsDir* dir)
{
    char* cwd = dir->thisDir;
    char *p = cwd + strlen(cwd)-2;
    while(p > cwd && *p != '/') *p-- = 0;
}

void gotoSubDirectory(lsDir* dir, char* basename)
{
    char* cwd = dir->thisDir;
    cwd = strcat(cwd,basename);
    cwd = strcat(cwd,"/");
}

char* lsDirBasename(lsDir* dir)
{
    char* ret = NULL;
    // our convention is awkward enough to have to find the second-to-last slash in most cases
    char* baseidx = dir->thisDir;
    char* baseidx2 = strrchr(dir->thisDir,'/');
    while (baseidx!=baseidx2)
    {
        ret = baseidx;
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
            printf("error reading directory\n");
            printf("result code %08x\n",(unsigned int)res);
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

Result FSUSER_ControlArchive(Handle handle, FS_archive archive)
{
	u32* cmdbuf=getThreadCommandBuffer();

	u32 b1 = 0, b2 = 0;

	cmdbuf[0]=0x080d0144;
	cmdbuf[1]=archive.handleLow;
	cmdbuf[2]=archive.handleHigh;
	cmdbuf[3]=0x0;
	cmdbuf[4]=0x1; //buffer1 size
	cmdbuf[5]=0x1; //buffer1 size
	cmdbuf[6]=0x1a;
	cmdbuf[7]=(u32)&b1;
	cmdbuf[8]=0x1c;
	cmdbuf[9]=(u32)&b2;
 
	Result ret=0;
	if((ret=svcSendSyncRequest(handle)))return ret;
 
	return cmdbuf[1];
}

const char HOME[2] = "/";
const char SDMC_CURSOR[4] = ">>>";
const char SAVE_CURSOR[4] = ">>>";
const char NULL_CURSOR[4] = "   ";

enum state
{
    SELECT_SDMC,
    SELECT_SAVE,
    CONFIRM_DELETE,
    CONFIRM_OVERWRITE,
};
enum state machine_state;
enum state previous_state;

PrintConsole titleBar, sdmcList, saveList, sdmcCursor, saveCursor;
PrintConsole statusBar, instructions;

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
    lsLine* curLine = destDir->firstLine;
    while (curLine)
    {
        if (!(curLine->isDirectory))
            if (!strcmp(curLine->thisLine,path))
                return 1;
        curLine = curLine->nextLine;
    }
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
    
    debugOut("reading original file ...");
    printf("original path %s\n",origpath);
    Result res = loadFile(origpath,data,curArchive,curFsHandle,size);
    if(res)
    {
        debugOut("error reading file");
        printf("result code %08x",(unsigned int)res);
        return;
    }
    debugOut("writing new file ...");
    printf("destination path %s\n",destpath);
    res = writeFile(destpath,data,(u32)size,destArchive,destFsHandle);
    if(res)
    {
        debugOut("error writing file");
        printf("result code %08x\n",(unsigned int)res);
        if(res==RES_OUT_OF_SPACE)
            printf("(you may be running out of space!)\n");
        return;
    }
    debugOut("success!");
    free(data);
}

void copyDir(lsDir* dir, char* path, lsDir* destDir)
{
    if (!dir || !destDir) return;
    Handle* curFsHandle = NULL;
    Handle* destFsHandle = NULL;
    FS_archive* curArchive = NULL;
    FS_archive* destArchive = NULL;
    
    char origpath[MAX_PATH_LENGTH] = {0};
    char destpath[MAX_PATH_LENGTH] = {0};
    char timeStr[16] = {0};
    strncpy(origpath,dir->thisDir,MAX_PATH_LENGTH);
    strcat(origpath,path);
    debugOut("constructing paths");
    strncpy(destpath,destDir->thisDir,MAX_PATH_LENGTH);
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
    debugOut("got handles");
    if (!path)
    {
        time_t temps = time(NULL);
        strftime(timeStr,16,"%Y%m%d_%H%M%S",gmtime(&temps));
        strcat(destpath,timeStr);
        FSUSER_CreateDirectory(destFsHandle,*destArchive,FS_makePath(PATH_CHAR,destpath));
        gotoSubDirectory(destDir,timeStr);
    } else {
        strcat(destpath,path);
        FSUSER_CreateDirectory(destFsHandle,*destArchive,FS_makePath(PATH_CHAR,destpath));
        gotoSubDirectory(destDir,path);
    }
    printf("destpath %s",destpath);
    scanDir(dir,curArchive,curFsHandle);
    lsLine* curLine = dir->firstLine;
    while (curLine)
    {
        if (curLine->isDirectory)
            copyDir(dir,curLine->thisLine,destDir);
        else
            copyFile(dir,curLine->thisLine,curLine->fileSize,destDir);
        curLine = curLine->nextLine;
    }
    gotoParentDirectory(dir);
    gotoParentDirectory(destDir);
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
    consoleSelect(&instructions);
    consoleClear();
    textcolour(WHITE);
    wordwrap("svdt is tdvs, reversed and without vowels. Use it to transfer files between your SD card and your save data. (Directories marked in purple.) If you don't see any save data, restart until you can select a target app.\n",BOTTOM_WIDTH);
    wordwrap("> Press L/R to point at save/SD data, and up/down to point at a specific file or folder.\n",BOTTOM_WIDTH);
    wordwrap("> Press X to delete file. (Deleting folders is purposefully omitted here.)\n",BOTTOM_WIDTH);
    wordwrap("> Press A to navigate inside a folder. Press B to return to the parent folder, if there is one.\n",BOTTOM_WIDTH);
    wordwrap("> Press Y to copy selected file/folder to the working directory of the other data.\n",BOTTOM_WIDTH);
    wordwrap("> Press START to stop while you're ahead.\n",BOTTOM_WIDTH);
}

int main()
{
	gfxInitDefault();
	gfxSet3D(false);

	filesystemInit();

	consoleInit(GFX_TOP, &titleBar);
	consoleInit(GFX_TOP, &sdmcList);
	consoleInit(GFX_TOP, &saveList);
	consoleInit(GFX_TOP, &sdmcCursor);
	consoleInit(GFX_TOP, &saveCursor);
	consoleInit(GFX_BOTTOM, &statusBar);
	consoleInit(GFX_BOTTOM, &instructions);

    consoleSetWindow(&titleBar,0,0,TOP_WIDTH,HEIGHT);
    consoleSetWindow(&saveCursor,0,3,3,HEIGHT-3);
    consoleSetWindow(&saveList,CURSOR_WIDTH,3,LIST_WIDTH,HEIGHT-3);
    consoleSetWindow(&sdmcCursor,TOP_WIDTH/2,3,CURSOR_WIDTH,HEIGHT-3);
    consoleSetWindow(&sdmcList,TOP_WIDTH/2+CURSOR_WIDTH,3,LIST_WIDTH,HEIGHT-3);
    consoleSetWindow(&statusBar,0,0,BOTTOM_WIDTH,8);
    consoleSetWindow(&instructions,0,8,BOTTOM_WIDTH,HEIGHT-2);
    
	consoleSelect(&titleBar);
    textcolour(SALMON);
    printf("svdt 0.00001\n");
    printf("meladroit/willidleaway at your service\n");
    gotoxy(CURSOR_WIDTH,2);
    textcolour(GREY);
    printf("save data:");
    gotoxy(TOP_WIDTH/2,2);
    printf("SD data:");
    lsDir cwd_sdmc, cwd_save;
    strncpy(cwd_sdmc.thisDir,HOME,MAX_PATH_LENGTH);
    strncpy(cwd_save.thisDir,HOME,MAX_PATH_LENGTH);
    scanDir(&cwd_sdmc,&sdmcArchive,&sdmcFsHandle);
    scanDir(&cwd_save,&saveGameArchive,&saveGameFsHandle);
    
    machine_state = SELECT_SAVE;
    printDir(&cwd_save);
    previous_state = machine_state;
    machine_state = SELECT_SDMC;
    printDir(&cwd_sdmc);
    
    printInstructions();
    
    consoleSelect(&statusBar);
    textcolour(NEONGREEN);
    debugOut("Successful startup, I guess. Huh.");

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
                if(curList == &sdmcList)
                    cwd_needs_update = 1;
                printInstructions();
            }
        }
		if(hidKeysDown() & KEY_START)break;
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
                            } else {
                                deleteFile(deletePath,curArchive,curFsHandle);
                            }
                            break;
                    }
                    machine_state = previous_state;
                    previous_state = CONFIRM_DELETE;
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
            }
            debugOut("attempting to copy selection");
            int i;
            switch (cursor_y)
            {
                case 0:
                    copyDir(ccwd,NULL,notccwd);
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
                        copyDir(ccwd,selection->thisLine,notccwd);
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
            redrawCursor(&cursor_y,ccwd);
            ccwd = &cwd_save;
            notccwd = &cwd_sdmc;
            curList = &saveList;
            curFsHandle = &saveGameFsHandle;
            curArchive = &saveGameArchive;
        }
        if((hidKeysDown() & (KEY_R | KEY_DRIGHT)) && sdmcCurrent)
        {
            consoleSelect(&saveCursor);
            consoleClear();
            machine_state = SELECT_SDMC;
            redrawCursor(&cursor_y,ccwd);
            ccwd = &cwd_sdmc;
            notccwd = &cwd_save;
            curList = &sdmcList;
            curFsHandle = &sdmcFsHandle;
            curArchive = &sdmcArchive;
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
