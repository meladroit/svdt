#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include <3ds.h>

#include "filesystem.h"
#include "text.h"
#include "utils.h"

#define MAX_PATH_LENGTH 1024
#define CURSOR_WIDTH 3
#define LIST_WIDTH (TOP_WIDTH/2-CURSOR_WIDTH)

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
    char fullDir[MAX_PATH_LENGTH];
    Handle thisDirHandle;
    int dirEntryCount;
    struct lsLine* firstLine;
    struct lsDir* parentDir;
} lsDir;

void freeLine(lsLine* line)
{
    if(!line) return;
    free(&line->thisLine);
    free(&line->isDirectory);
    free(&line->fileSize);
    line->nextLine = NULL;
}

void freeDir(lsDir* dir)
{
    if (!dir) return;
    free(&dir->thisDirHandle);
    // still totally cribbing from 3ds_hb_menu
    lsLine* line = dir->firstLine;
    lsLine* temp = NULL;
    while(line)
    {
        temp = line->nextLine;
        line->nextLine = NULL;
        freeLine(line);
        free(line);
        line = temp;
    }
    dir->parentDir = NULL;
}

void initDir(lsDir* dir, FS_archive* archive, Handle* fsHandle, char* path, lsDir* parentDir)
{
    if (!dir) return;

    dir->parentDir=parentDir;
    dir->firstLine = NULL;
    Handle dirHandle;
    FSUSER_OpenDirectory(fsHandle, &dirHandle, *archive, FS_makePath(PATH_CHAR, path));
    if (!dir->parentDir)
    {
        strncpy(dir->fullDir,dir->thisDir,MAX_PATH_LENGTH);
    } else {
        strncpy(dir->fullDir,dir->parentDir->fullDir,MAX_PATH_LENGTH);
        strcat(dir->fullDir,dir->thisDir);
        strcat(dir->fullDir,"/");
    }
    dir->thisDirHandle = dirHandle;
    dir->dirEntryCount = 0;
    // cribbing from 3ds_hb_menu again
    u32 entriesRead;
    lsLine* lastLine = NULL;
    static char pathname[MAX_PATH_LENGTH];
    do
    {
        static FS_dirent entry;
        memset(&entry,0,sizeof(FS_dirent));
        entriesRead=0;
        FSDIR_Read(dirHandle, &entriesRead, 1, &entry);
        unicodeToChar(pathname,entry.name,MAX_PATH_LENGTH);
        //printf("%s\n",pathname);
        if(entriesRead && (HEIGHT-5>dir->dirEntryCount))
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
}

void printDir(lsDir* dir)
{
    int i;
    char lineOut[LIST_WIDTH] = {0};
    textcolour(PURPLE);
    printf("%s\n",dir->thisDir);
    if (dir->parentDir)
        printf("../\n");
    else
        printf("[is root]\n");
    lsLine* currentLine = dir->firstLine;
    while (currentLine)
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
        //printf("[REDACTED]\n");
        currentLine = currentLine->nextLine;
    }
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
const char SDMC_CURSOR[4] = "<<<";
const char SAVE_CURSOR[4] = ">>>";
const char NULL_CURSOR[4] = "   ";

enum state
{
    SELECT_SDMC,
    SELECT_SAVE,
    CONFIRM_DELETE,
    CONFIRM_OVERWRITE
};
enum state machine_state;

PrintConsole titleBar, sdmcList, saveList, sdmcCursor, saveCursor;
PrintConsole statusBar, instructions;

void redrawCursor(int* cursor_y, int currentEntryCount)
{
    if (*cursor_y<0)
        *cursor_y = 0;
    int cursor_y_bound = HEIGHT-4;
    if (currentEntryCount+1<cursor_y_bound)
        cursor_y_bound = currentEntryCount+1;
    if (*cursor_y>cursor_y_bound)
        *cursor_y = cursor_y_bound;
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

void debugOut(char* garbled)
{
    consoleSelect(&statusBar);
    consoleClear();
    textcolour(NEONGREEN);
    printf(garbled);
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
    consoleSetWindow(&sdmcCursor,TOP_WIDTH-CURSOR_WIDTH,3,CURSOR_WIDTH,HEIGHT-3);
    consoleSetWindow(&sdmcList,TOP_WIDTH/2,3,LIST_WIDTH,HEIGHT-3);
    consoleSetWindow(&statusBar,0,0,BOTTOM_WIDTH,2);
    consoleSetWindow(&instructions,0,3,BOTTOM_WIDTH,HEIGHT-2);
    
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
    initDir(&cwd_sdmc,&sdmcArchive,&sdmcFsHandle,(char*)HOME,NULL);
    initDir(&cwd_save,&saveGameArchive,&saveGameFsHandle,(char*)HOME,NULL);
    
    consoleSelect(&sdmcList);
    printDir(&cwd_sdmc);
    consoleSelect(&saveList);
    printDir(&cwd_save);
    
    consoleSelect(&instructions);
    textcolour(WHITE);
    wordwrap("svdt is tdvs, reversed and without vowels. Use it to transfer files between your SD card and your save data. If you don't see any save data, restart until you can select a target app.\n",BOTTOM_WIDTH);
    wordwrap("* Press L/R to point at save/SD data, and up/down to point at a specific file or folder.\n",BOTTOM_WIDTH);
    wordwrap("* Press X to delete file. (Deleting folders is purposefully omitted here.)\n",BOTTOM_WIDTH);
    wordwrap("* Press A to navigate inside a folder. Press B to return to the parent folder, if there is one.\n",BOTTOM_WIDTH);
    wordwrap("* Press Y to copy selected file/folder to the working directory of the other data.\n",BOTTOM_WIDTH);
    wordwrap("* Press START to stop while you're ahead.\n",BOTTOM_WIDTH);
    
    consoleSelect(&statusBar);
    textcolour(NEONGREEN);
    debugOut("Successful startup, I guess. Huh.");

    machine_state = SELECT_SDMC;
    consoleSelect(&sdmcCursor);
    consoleClear();
    gotoxy(0,0);
    printf(SDMC_CURSOR);
    int cursor_y = 0;
    int currentEntryCount = cwd_sdmc.dirEntryCount;
    lsDir* ccwd = &cwd_sdmc;
    PrintConsole* curList = &sdmcList;
    Handle* curFsHandle = &sdmcFsHandle;
    FS_archive* curArchive = &sdmcArchive;
            
	while (aptMainLoop())
	{
		hidScanInput();
		if(hidKeysDown() & KEY_START)break;
        if(hidKeysDown() & (KEY_L | KEY_DLEFT))
        {
            consoleSelect(&sdmcCursor);
            consoleClear();
            machine_state = SELECT_SAVE;
            currentEntryCount = cwd_save.dirEntryCount;
            redrawCursor(&cursor_y,currentEntryCount);
            ccwd = &cwd_save;
            curList = &saveList;
            curFsHandle = &saveGameFsHandle;
            curArchive = &saveGameArchive;
        }
        if(hidKeysDown() & (KEY_R | KEY_DRIGHT))
        {
            consoleSelect(&saveCursor);
            consoleClear();
            machine_state = SELECT_SDMC;
            currentEntryCount = cwd_sdmc.dirEntryCount;
            redrawCursor(&cursor_y,currentEntryCount);
            ccwd = &cwd_sdmc;
            curList = &sdmcList;
            curFsHandle = &sdmcFsHandle;
            curArchive = &sdmcArchive;
        }
        if(hidKeysDown() & (KEY_UP))
        {
            cursor_y--;
            redrawCursor(&cursor_y,currentEntryCount);
        }
        if(hidKeysDown() & (KEY_DOWN))
        {
            cursor_y++;
            redrawCursor(&cursor_y,currentEntryCount);
        }
        if(hidKeysDown() & KEY_A)
        {
            debugOut("creating lsDir pointer");
            lsDir* temp_ccwd = (lsDir*)malloc(sizeof(lsDir));
            memset(temp_ccwd,0,sizeof(lsDir));
            int directory_was_selected = 0;
            switch (cursor_y)
            {
                case 0:
                    debugOut("cursor_y = 0, so nothing happens");
                    break;
                case 1:
                    debugOut("cursor_y = 1, so we check for parent");
                    if(ccwd->parentDir)
                    {
                        directory_was_selected = 1;
                        debugOut("we have parent, so moving to temp");
                        temp_ccwd = ccwd->parentDir;
                        debugOut("freeDir");
                        freeDir(ccwd);
                        debugOut("free");
                        free(ccwd);
                        debugOut("and now some formalities");
                        ccwd = temp_ccwd;
                        currentEntryCount = ccwd->dirEntryCount;
                    }
                    break;
                default: ;
                    debugOut("presumably a random entry was clicked");
                    lsLine* selection = ccwd->firstLine;
                    int i;
                    for (i=1;i<cursor_y-2;i++)
                    {
                        selection = selection->nextLine;
                    }
                    debugOut("we should be a good number of entries down the line");
                    if (selection->isDirectory)
                    {
                        directory_was_selected = 1;
                        consoleSelect(&statusBar);
                        textcolour(NEONGREEN);
                        printf("isDirectory is %d",(int)(selection->isDirectory));
                        debugOut("moving to initDir");
                        initDir(temp_ccwd,curArchive,curFsHandle,selection->thisLine,ccwd);
                    }
                    break;
            }
            if(directory_was_selected)
            {
                ccwd = temp_ccwd;
                currentEntryCount = ccwd->dirEntryCount;
                consoleSelect(curList);
                printDir(ccwd);
                
                switch (machine_state)
                {
                    case SELECT_SAVE:
                        cwd_save = *ccwd;
                        break;
                    case SELECT_SDMC:
                        cwd_sdmc = *ccwd;
                        break;
                    default:
                        break;
                }
            }
        }
        switch (machine_state)
        {
            case SELECT_SAVE:
                break;
            case SELECT_SDMC:
                break;
            default:
                break;
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
