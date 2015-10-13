#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <3ds.h>

#include "text.h"
#include "svdt.h"
#include "filesystem.h"
#include "secure_values.h"

#define MAX_LS_LINES HEIGHT-5
#define CURSOR_WIDTH 3
#define LIST_WIDTH (TOP_WIDTH/2-CURSOR_WIDTH)
#define RES_OUT_OF_SPACE_CARD 0xc86044cd  // probably
#define RES_OUT_OF_SPACE_ESHOP 0xd8604664 // at least I think so

#define HELD_THRESHOLD 10
// I know, global variables, but I swear they're useful here
int canHasConsole = 0;
int alphabetSort = 1; // 1 for sort by name, 0 for sort by filesystem load order (probably date)
int calledFromCopyDir = 0; // incremented for every call of copyDir, decremented at end of call
int dirOverwriteAll = 1; // 0 to keep prompting, 1 to always overwrite, -1 to always skip
// not to worry ... this is set to 0 past emergency mode
lsTitle* firstTitle = NULL;

u8 mediatype = 2;
//int productCodeKnown = 0;
char productCode[9] = {0};

const char HOME[2] = "/";
const char SDMC_CURSOR[4] = ">>>";
const char SAVE_CURSOR[4] = ">>>";

enum state
{
    SELECT_SDMC,
    SELECT_SAVE,
    CONFIRM_DELETE,
    CONFIRM_OVERWRITE,
    SVDT_IS_KILL,
    SET_TARGET_TITLE,
    CONFIRM_SAVE_ROOT,
    CONFIRM_SECURE_VALUE,
    EXIT_REQUESTED
};
enum state machine_state;
enum state previous_state;

PrintConsole titleBar, sdmcList, saveList, sdmcCursor, saveCursor;
PrintConsole statusBar, notifyBar;
void printAlert();
void printTarget();

void debugOut(char* garbled)
{
    consoleSelect(&statusBar);
    printf("\n");//consoleClear();
    textcolour(NEONGREEN);
    printf("%s\n",garbled);
}

void debugOutPrompt(char* garbled)
{
    consoleSelect(&statusBar);
    printf("\n");//consoleClear();
    textcolour(YELLOW);
    printf("%s\n",garbled);
}

void debugOutSuccess(char* garbled)
{
    consoleSelect(&statusBar);
    printf("\n");//consoleClear();
    textcolour(CYAN);
    printf("%s\n",garbled);
}

void debugOutCancel(char* garbled)
{
    consoleSelect(&statusBar);
    printf("\n");//consoleClear();
    textcolour(RED);
    printf("%s\n",garbled);
}

int detectOverwrite(char* path, lsDir* destDir)
{   
    if (!destDir || !path) return -1;
    if (canHasConsole)
    {
        debugOut("Checking for overwrite risk.");
        //printf("checking against %s\n",path);
        //printf(" destDir: %s",destDir->thisDir);
    }
    lsLine* curLine = destDir->firstLine;
    //if (canHasConsole)
    //    debugOut("starting check");
    while (curLine)
    {
        if (!(curLine->isDirectory))
            //if(canHasConsole)printf("looking at %s\n",curLine->thisLine);
            if (!strcmp(curLine->thisLine,path))
            {
                if (canHasConsole)
                    printf("Found overwrite for: %s\n",curLine->thisLine);
                return 1;
            }
        curLine = curLine->nextLine;
    }
    if (canHasConsole)
        printf("No overwrite risk.\n");
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
    if (detectOverwrite(path,destDir)) {    
        int goGoGadgetOverwrite = 0;
        if (dirOverwriteAll == -1) return;
        if (!dirOverwriteAll)
        {
            debugOutPrompt("Possible overwrite detected.\n\t[SELECT] Overwrite\n\t[B] Skip");
            if(calledFromCopyDir)
            {
                printf("\t[L+R] apply to all files\n");
                textcolour(BROWN);
                wordwrap("(Hold down both shoulder buttons while choosing to apply for all other files in this directory.)",BOTTOM_WIDTH);
            }
            previous_state = machine_state;
            machine_state = CONFIRM_OVERWRITE;
            printAlert();
            while (aptMainLoop())
            {
                hidScanInput();
                if(hidKeysDown() & KEY_START)
                {
                    machine_state = EXIT_REQUESTED;
                    return;
                }
                if(hidKeysDown() & KEY_B)
                {
                    debugOutCancel("Overwrite cancelled.");
                    if(hidKeysHeld() & (KEY_L | KEY_R))
                        dirOverwriteAll = -1;
                    break;
                }
                if(hidKeysDown() & KEY_SELECT)
                {
                    debugOutSuccess("Overwrite confirmed!");
                    goGoGadgetOverwrite = 1;
                    if(hidKeysHeld() & (KEY_L | KEY_R))
                        dirOverwriteAll = 1;
                    break;
                }
                gspWaitForVBlank();
                // Flush and swap framebuffers
                gfxFlushBuffers();
                gfxSwapBuffers();
            }
            machine_state = previous_state;
            previous_state = CONFIRM_OVERWRITE;
        } else { goGoGadgetOverwrite = 1; }
        if(canHasConsole)
            printTarget();
        if (!goGoGadgetOverwrite)
        {
            hidScanInput();
            return;
        }
        char* deletePath = (char*)malloc(strlen(dir->thisDir)+strlen(path)+1);
        strcpy(deletePath,dir->thisDir);
        strcat(deletePath,path);
        if(canHasConsole)
        {
            debugOut("Overwriting file.");
            printf("[deletePath %s]",deletePath);
        }
        if (curArchive==&sdmcArchive)
            deleteFile(deletePath,&saveGameArchive,&saveGameFsHandle);
        if (curArchive==&saveGameArchive)
            deleteFile(deletePath,&sdmcArchive,&sdmcFsHandle);
        free(deletePath);
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
        debugOut("Reading original file ...");
        printf("[original path %s]\n",origpath);
    }
    Result res = loadFile(origpath,data,curArchive,curFsHandle,size);
    if(res)
    {
        if (canHasConsole)
        {
            debugOutCancel("! Error reading file.");
            printf("[result code %08x]",(unsigned int)res);
        }
        return;
    }
    if (canHasConsole)
    {
        debugOut("Writing new file ...");
        printf("[destination path %s]\n",destpath);
    }
    res = writeFile(destpath,data,(u32)size,destArchive,destFsHandle);
    free(data);
    if(res)
    {
        if (canHasConsole)
        {
            debugOutCancel("! Error writing file.");
            printf("[result code %08x]\n",(unsigned int)res);
            if(res==RES_OUT_OF_SPACE_CARD || res==RES_OUT_OF_SPACE_ESHOP)
                printf("(You may be running out of save space!)\n");
        }
        machine_state = SVDT_IS_KILL;
        return;
    }
    if((isSecureFile(destpath))&&(whichSecureGame!=SECURE_UNKNOWN))
    {
        if (canHasConsole)
            debugOut("Rewriting secure value from value loaded at startup.");
        res = writeSecureValue();
    }
    if(res)
    {
        if (canHasConsole)
            debugOutCancel("! Error rewriting secure value.");
        printf("[result code %08x]\n",(unsigned int)res);
    }
    if (canHasConsole)
        debugOutSuccess("Finished!");
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
    //if (canHasConsole)
    //    debugOut("constructing paths:");
    strncpy(destpath,destDir->thisDir,MAX_PATH_LENGTH);
    //if (canHasConsole)
    //    printf("origpath %s\n",origpath);
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
    //if (canHasConsole)
    //    debugOut("got handles");
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
    scanDir(destDir,destArchive,destFsHandle);
    if(destArchive==&saveGameArchive)
    {
        //if (canHasConsole)
        //    printf("\ncalling ControlArchive\n");
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
    calledFromCopyDir++;
    while (curLine && (machine_state!=SVDT_IS_KILL))
    {
        if (curLine->isDirectory)
        {
            copyDir(dir,curLine->thisLine,destDir,NULL);
        }
        else
        {
            copyFile(dir,curLine->thisLine,curLine->fileSize,destDir);
            if(machine_state == EXIT_REQUESTED) return;
        }    
        curLine = curLine->nextLine;
    }
    calledFromCopyDir--;
    if (!calledFromCopyDir)
        dirOverwriteAll = 0;
    if (path)
        gotoParentDirectory(dir);
    gotoParentDirectory(destDir);
    scanDir(destDir,destArchive,destFsHandle);
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
    int cursor_y_bound = HEIGHT-6;
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
    printf("\n> [L/R] or [left/right] to change\n        between save/SD data.\n");
    printf("> [up/down] select file or folder.\n");
    printf("> [X] delete file or folder.\n");
    printf("> [A] navigate inside a folder.\n");
    printf("> [B] return to the parent folder.\n");
    printf("> [Y] copy file/folder from/to\n      save/SD.\n");
    printf("> [Y on dir root] copy current dir.\n");
    printf("> [SELECT] reprint these instructions\n           and toggle sorting method\n");
    printf("> [START] exit.\n");
}

void printAlert()
{
    consoleSelect(&notifyBar);
    consoleClear();
    textcolour(SALMON);
    printf("Action requires confirmation. See lower screen.");
}

char titleTitle[0x40];
int titleTitle_set = 0;

void printTarget()
{
    consoleSelect(&notifyBar);
    consoleClear();
    gotoxy(0,0);
    if(titleTitle_set)
    {
        textcolour(TEAL);
        printf("Title: ");
        textcolour(WHITE);
        printf("%.42s",titleTitle);
    }
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
    amInit();
    filesystemInit();
    //if (!doesFileNotExist("/3ds/svdt/no_alpha_sort",&sdmcFsHandle,sdmcArchive))
    if (file_exist("no_alpha_sort"))
        alphabetSort = 0;

    FSUSER_CreateDirectory(&sdmcFsHandle,sdmcArchive,FS_makePath(PATH_CHAR,"/svdt"));
    
    u64 tid;
    u64 tid2 = 0;
    char productCodeBuffer[16] = {0};
    int titleTitles_available;
    char destPath[MAX_PATH_LENGTH];
    char tempStr[16] = {0};
    time_t temps = time(NULL);
    strftime(tempStr,16,"%Y%m%d_%H%M%S",gmtime(&temps));
    strncpy(titleTitle,tempStr,MAX_PATH_LENGTH);
    FSUSER_GetMediaType(&saveGameFsHandle,&mediatype);
    if (mediatype==2)
    {
        // we fetch target app title automatically for gamecards
        getTitleTitle(0x0,2,titleTitle);
        titleTitle_set = 1;
    } else {
		aptOpenSession();
		APT_GetProgramID(NULL, &tid2);
		aptCloseSession();
/*
        //Fetch title from /svdt/tid.bin
        FILE * pFile;
        long lSize;
        size_t result;
        pFile = fopen ( "/svdt/tid.bin" , "rb" );
        if (pFile!=NULL) {
            fseek (pFile , 0 , SEEK_END);
            lSize = ftell (pFile);
            rewind (pFile);
            result = fread (&tid2,1,sizeof(tid2),pFile);
            if (result == lSize) {
                fclose (pFile);
                //Make sure we can't load this again without going trough HBL
                remove("/svdt/tid.bin");
            }
        }
        fclose (pFile);
*/
	    getTitleList(mediatype,&titleTitles_available);
		if (!titleTitle_set){
			if (tid2 != 0){
				lsTitle* currentTitle = firstTitle;
				int i = 0;
				while(currentTitle != NULL){
					if (currentTitle->thisTitle == tid2){
						titleTitle_set = i;
						getTitleTitle(tid2, mediatype, titleTitle);
						tid = tid2;
						break;
					}
					currentTitle = currentTitle->nextTitle;
					i++;
				}
			}
		}
        AM_GetTitleProductCode(mediatype,tid2,productCodeBuffer);
        strncpy(productCode,productCodeBuffer,9);
        secureGameFromProductCode(productCode);
        if (whichSecureGame == SECURE_UNKNOWN) {
            secureGameFromFilesystem();
        }
        getSecureValue();
    }
    
    lsDir cwd_sdmc, cwd_save;
    strncpy(cwd_sdmc.thisDir,HOME,MAX_PATH_LENGTH);
    strncpy(cwd_save.thisDir,HOME,MAX_PATH_LENGTH);
    scanDir(&cwd_sdmc,&sdmcArchive,&sdmcFsHandle);
    scanDir(&cwd_save,&saveGameArchive,&saveGameFsHandle);
    
    hidScanInput();
    
    //if ((hidKeysHeld() & KEY_L) != doesFileNotExist("/3ds/svdt/disable_auto_backups",&sdmcFsHandle,sdmcArchive))
    if ((hidKeysHeld() & KEY_L) || !file_exist("disable_auto_backups"))
    {
        //  so! if disable_auto_backups is present but L is held down, we execute a backup
        //      if disable_auto_backups is present but L is left open, no backup is made
        //      if disable_auto_backups is absent, we always execute a backup
        machine_state = SELECT_SAVE;
        memset(destPath,0,MAX_PATH_LENGTH);
        gotoSubDirectory(&cwd_sdmc,"svdt");
        if (titleTitle_set)
        {
            strcat(destPath,"/svdt/");
            strcat(destPath,titleTitle);
            FSUSER_CreateDirectory(&sdmcFsHandle,sdmcArchive,FS_makePath(PATH_CHAR,destPath));
            gotoSubDirectory(&cwd_sdmc,titleTitle);
        }
        copyDir(&cwd_save,NULL,&cwd_sdmc,tempStr);
        gotoParentDirectory(&cwd_sdmc);
        if (titleTitle_set)
        {
            gotoParentDirectory(&cwd_sdmc);
        }
        scanDir(&cwd_sdmc,&sdmcArchive,&sdmcFsHandle);
        if(!(hidKeysHeld() & KEY_R))
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
    if (mediatype!=2)
        machine_state = SET_TARGET_TITLE;
    
    gfxInitDefault();
    gfxSet3D(false);
    
    dirOverwriteAll = 0;

    consoleInit(GFX_TOP, &titleBar);
    consoleInit(GFX_TOP, &sdmcList);
    consoleInit(GFX_TOP, &saveList);
    consoleInit(GFX_TOP, &sdmcCursor);
    consoleInit(GFX_TOP, &saveCursor);
    consoleInit(GFX_BOTTOM, &statusBar);
    consoleInit(GFX_TOP, &notifyBar);

    consoleSetWindow(&titleBar,0,0,TOP_WIDTH,3);
    consoleSetWindow(&saveCursor,0,3,3,MAX_LS_LINES+1);
    consoleSetWindow(&saveList,CURSOR_WIDTH,3,LIST_WIDTH,MAX_LS_LINES+1);
    consoleSetWindow(&sdmcCursor,TOP_WIDTH/2,3,CURSOR_WIDTH,MAX_LS_LINES+1);
    consoleSetWindow(&sdmcList,TOP_WIDTH/2+CURSOR_WIDTH,3,LIST_WIDTH,MAX_LS_LINES+1);
    consoleSetWindow(&statusBar,0,0,BOTTOM_WIDTH,HEIGHT);//8);
    consoleSetWindow(&notifyBar,0,HEIGHT-1,TOP_WIDTH,1);
    
    if (machine_state != SET_TARGET_TITLE)
    {
        printInstructions();
        printTarget();
    }
    switch (canHasConsole)
    {
        case -1:
            debugOut("Emergency inject invoked w/o svdt_inject directory.");
            break;
        case 2:
            debugOut("Standard dump to SD was invoked.");
            break;
        case 3:
            debugOut("Emergency savegame inject was invoked.");
            break;
        default:
            debugOut("Successful startup, I guess. Huh.");
            canHasConsole = 1;
            break;
    }
    if (mediatype!=2)
    {
        printf("Secure game inferred at startup:\n ");
        printSecureGame();
        if(secureValueSet)
        {
            debugOutSuccess("Secure value set.");
            int i;
            for(i=0;i<8;i++)
                printf("%02x ",secureValue[i]);
            putchar('\n');
        }
    }
    
    consoleSelect(&titleBar);
    textcolour(TEAL);
    printf("svdt 0.10.42c, meladroit/willidleaway/suloku\n");
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
    char deletePath[MAX_PATH_LENGTH] = {0};
    lsDir* ccwd = &cwd_sdmc;
    lsDir* notccwd = &cwd_save;
    PrintConsole* curList = &sdmcList;
    Handle* curFsHandle = &sdmcFsHandle;
    FS_archive* curArchive = &sdmcArchive;
    
    u8 sdmcCurrent, sdmcPrevious;
    sdmcCurrent = 1;
    
    if (mediatype!=2)
        machine_state = SET_TARGET_TITLE;
    if (machine_state == SET_TARGET_TITLE)
    {
        //getTitleList(mediatype,&titleTitles_available);
        consoleSelect(&statusBar);
        gotoxy(0,10);
        int i;
        for (i=0;i<BOTTOM_WIDTH;i++) { printf(" "); }
		if (!titleTitle_set){
			if (tid2 != 0){
				lsTitle* currentTitle = firstTitle;
				int i = 0;
				while(currentTitle != NULL){
					if (currentTitle->thisTitle == tid2){
						titleTitle_set = i;
						getTitleTitle(tid2, mediatype, titleTitle);
						tid = tid2;
						break;
					}
					currentTitle = currentTitle->nextTitle;
					i++;
				}
			}
		}
        if (!titleTitle_set)
            nthTitleInList(titleTitle_set,mediatype,titleTitle,&tid);
        AM_GetTitleProductCode(mediatype,tid,productCodeBuffer);
        strncpy(productCode,productCodeBuffer,9);
        gotoxy(1,10);
        printf("<");
        gotoxy(BOTTOM_WIDTH-2,10);
        printf(">");
        gotoxy((BOTTOM_WIDTH-strlen(titleTitle))/2,10);
        textcolour(NEONGREEN);
        printf(titleTitle);
        gotoxy(0,29);
        textcolour(WHITE);
        printf(productCode);
        printf(" - %016llX", tid);
        gotoxy(0,12);
        textcolour(SALMON);
        wordwrap("Target app is not on gamecard. Fetching target app titles automatically is not implemented for NAND/SD apps. Use left/right on D-pad with the [A] button to select the correct target app name. Press [B] to skip.",BOTTOM_WIDTH);
        textcolour(WHITE);
    }
            
    int heldU = 0;
    int heldD = 0;
    while (aptMainLoop())
    {
        hidScanInput();
        if(hidKeysDown() & KEY_START)break;
        if(machine_state == EXIT_REQUESTED)break;
        if(machine_state == SET_TARGET_TITLE)
        {
            int titleTitle_update = 0;
            if(hidKeysDown() & KEY_START)break;
            if(hidKeysDown() & KEY_LEFT)
            {
                titleTitle_set--;
                titleTitle_set+= titleTitles_available;
                titleTitle_update = 1;
            }
            if(hidKeysDown() & KEY_RIGHT)
            {
                titleTitle_set++;
                titleTitle_update = 1;
            }
            if (titleTitle_update)
            {
                titleTitle_set = titleTitle_set % titleTitles_available;
                gotoxy(0,10);
                int i;
                for (i=0;i<BOTTOM_WIDTH;i++) { printf(" "); }
                nthTitleInList(titleTitle_set,mediatype,titleTitle,&tid);
                gotoxy(1,10);
                printf("<");
                gotoxy(BOTTOM_WIDTH-2,10);
                printf(">");
                gotoxy((BOTTOM_WIDTH-strlen(titleTitle))/2,10);
                textcolour(NEONGREEN);
                printf(titleTitle);
                titleTitle_update = 0;
                textcolour(WHITE);
                AM_GetTitleProductCode(mediatype,tid,productCodeBuffer);
                strncpy(productCode,productCodeBuffer,9);
                gotoxy(0,29);
                printf(productCode);
                printf(" - %016llX", tid);
            }
            if(hidKeysDown() & KEY_A)
            {
                printInstructions();
                printTarget();
                previous_state = machine_state;
                machine_state = SELECT_SDMC;
                if (canHasConsole == 2 && !titleTitle_set)
                {
                    debugOut("Trying to rename dump directory");
                    char tempPath[MAX_PATH_LENGTH] = {0};
                    memset(destPath,0,MAX_PATH_LENGTH);
                    strcat(destPath,"/svdt/");
                    strcat(destPath,titleTitle);
                    FSUSER_CreateDirectory(&sdmcFsHandle,sdmcArchive,FS_makePath(PATH_CHAR,destPath));
                    strcat(destPath,"/");
                    strcat(destPath,tempStr);
                    strcat(tempPath,"/svdt/");
                    strcat(tempPath,tempStr);
                    Result res = FSUSER_RenameDirectory(&sdmcFsHandle,sdmcArchive,FS_makePath(PATH_CHAR,tempPath),sdmcArchive,FS_makePath(PATH_CHAR,destPath));
                    if (res)
                    {
                        textcolour(RED);
                        printf("Failed with result code %08x",(unsigned int)res);
                    } else { debugOutSuccess("Success!"); }
                }
                titleTitle_set = 1;
                scanDir(&cwd_sdmc,&sdmcArchive,&sdmcFsHandle);
                clearTitleList();
                // if we're here, then mediatype!=2, so ...
                secureGameFromProductCode(productCode);
                debugOut("ASR inferred from product code:");
                printSecureGame();
                getSecureValue();
                if(secureValueSet)
                {
                    debugOut("Secure value set:");
                    int i;
                    for(i=0;i<8;i++)
                        printf("%02x ",secureValue[i]);
                    putchar('\n');
                    textcolour(YELLOW);
                    wordwrap("Enable automatic secure value rewriting on restore?\n",BOTTOM_WIDTH);
                    printf("\t[A] enable secure value rewriting\n\t[B] disable secure value rewriting\n");
                    previous_state = machine_state;
                    machine_state = CONFIRM_SECURE_VALUE;
                }
                continue;
            }
            if(hidKeysDown() & KEY_B)
            {
                titleTitle_set = 0;
                printInstructions();
                wordwrap("Target app title unknown. Copies of / will be timestamped.",BOTTOM_WIDTH);
                whichSecureGame = SECURE_UNKNOWN;
                previous_state = machine_state;
                machine_state = SELECT_SDMC;
                clearTitleList();
            }
            gspWaitForVBlank();
            // Flush and swap framebuffers
            gfxFlushBuffers();
            gfxSwapBuffers();
            continue;
        }
        if(machine_state == CONFIRM_SECURE_VALUE)
        {   
            if(hidKeysDown() & KEY_A)
            {
                printInstructions();
                debugOutSuccess("Anti-anti savegame restore enabled.");
                previous_state = machine_state;
                machine_state = SELECT_SDMC;
            }
            if(hidKeysDown() & KEY_B)
            {
                whichSecureGame = SECURE_UNKNOWN;
                printInstructions();
                debugOutCancel("Anti-anti savegame restore disabled.");
                previous_state = machine_state;
                machine_state = SELECT_SDMC;
            }
            gspWaitForVBlank();
            // Flush and swap framebuffers
            gfxFlushBuffers();
            gfxSwapBuffers();
            continue;
        }
        if(machine_state == SVDT_IS_KILL)
        {
            if(previous_state != SVDT_IS_KILL)
            {
                consoleSelect(&statusBar);
                int i;
                for (i=0;i<5;i++)
                {                
                    textcolour((enum colour)((1+i*6)%16));
                    wordwrap("svdt has encountered an error, and has halted to protect your data. Press [START] to exit.\n", BOTTOM_WIDTH);
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
        if(machine_state == CONFIRM_SAVE_ROOT)
        {
            int goGoGadgetCopy = 0;
            if(previous_state != CONFIRM_SAVE_ROOT)
            {
                previous_state = CONFIRM_SAVE_ROOT;
                consoleSelect(&statusBar);
                putchar('\n');
                textcolour(YELLOW);
                wordwrap("You are about to extract all target save data to the SD card:",BOTTOM_WIDTH);
                debugOutPrompt("\t[Y] Extract to SD working directory\n\t[A] Extract in folder at /svdt/\n\t[B] Cancel\n");
            }
            if(hidKeysDown() & KEY_B)
            {
                printTarget();
                debugOutCancel("Save data extraction cancelled.");
                machine_state = SELECT_SAVE;
                previous_state = CONFIRM_SAVE_ROOT;
            }
            if(hidKeysDown() & KEY_A)
            {
                char origSDPath[MAX_PATH_LENGTH];
                strncpy(origSDPath,notccwd->thisDir,MAX_PATH_LENGTH);
                strncpy(notccwd->thisDir,HOME,MAX_PATH_LENGTH);
                scanDir(&cwd_sdmc,&sdmcArchive,&sdmcFsHandle);
                gotoSubDirectory(&cwd_sdmc,"svdt");
                if (titleTitle_set)
                {
                    strcat(destPath,"/svdt/");
                    strcat(destPath,titleTitle);
                    FSUSER_CreateDirectory(&sdmcFsHandle,sdmcArchive,FS_makePath(PATH_CHAR,destPath));
                    gotoSubDirectory(&cwd_sdmc,titleTitle);
                }
                machine_state = SELECT_SAVE;
                copyDir(&cwd_save,NULL,&cwd_sdmc,tempStr);
                strncpy(notccwd->thisDir,origSDPath,MAX_PATH_LENGTH);
                scanDir(&cwd_sdmc,&sdmcArchive,&sdmcFsHandle);
                goGoGadgetCopy = 1;           
            }
            if(hidKeysDown() & KEY_Y)
            {
                if (titleTitle_set)
                {
                    strcat(destPath,titleTitle);
                    strcat(destPath,"_");
                }
                strcat(destPath,tempStr);
                //printf("using destPath %s",destPath);
                machine_state = SELECT_SAVE;
                copyDir(ccwd,NULL,notccwd,destPath);
                scanDir(&cwd_sdmc,&sdmcArchive,&sdmcFsHandle);
                goGoGadgetCopy = 1;
            }
            if(goGoGadgetCopy)
            {
                printTarget();
                debugOutSuccess("Save data extraction complete.");
                previous_state = CONFIRM_SAVE_ROOT;
                notccwd_needs_update = 1;
            } else {
                gspWaitForVBlank();
                // Flush and swap framebuffers
                gfxFlushBuffers();
                gfxSwapBuffers();
                continue;
            }
        }
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
            switch (machine_state)
            {
                case CONFIRM_DELETE:
                    debugOut("> Attempting to delete selection.");
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
                            printf("[isDirectory: %d]",(int)selection->isDirectory);
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
                    printTarget();
                    cwd_needs_update = 1;
                    break;
                default:
                    printInstructions();
                    debugOut("Changing sort order.");
                    alphabetSort = !alphabetSort;
                    cwd_needs_update = 1;
                    notccwd_needs_update = 1;
            }
        } else {
            if (hidKeysDown() && (machine_state == CONFIRM_DELETE))
            {
                machine_state = previous_state;
                previous_state = CONFIRM_DELETE;
                printTarget();
                debugOutCancel("Delete unconfirmed.");
                hidScanInput();
            }
        }
        if(hidKeysDown() & KEY_X)
        {
            previous_state = machine_state;
            machine_state = CONFIRM_DELETE;
            debugOutPrompt("Press [SELECT] to confirm delete!");
            printAlert();
        }
        if((hidKeysDown() & KEY_Y)&&(previous_state!=CONFIRM_SAVE_ROOT))
        {
            debugOut("> Attempting to copy selection.");
            int i;
            switch (cursor_y)
            {
                case 0:
                    if (!strcmp("/",(const char*)ccwd->thisDir))
                    {
                        debugOut("Copying root contents.");
                        memset(destPath,0,MAX_PATH_LENGTH);
                        temps = time(NULL);
                        strftime(tempStr,16,"%Y%m%d_%H%M%S",gmtime(&temps));
                        if (machine_state == SELECT_SAVE)
                        {
                            previous_state = machine_state;
                            machine_state = CONFIRM_SAVE_ROOT;
                            printAlert();
                            continue;
                        } else {
                            debugOut("Copying SD root to save data is disabled at present.");
                            continue;
                        }
                        /*strcat(destPath,tempStr);
                        printf("using destPath %s",destPath);
                        copyDir(ccwd,NULL,notccwd,destPath);*/
                    }
                    else
                    {
                        debugOut("Copying subdirectory.");
                        //printf("using basename %s",lsDirBasename(notcwd));
                        copyDir(ccwd,NULL,notccwd,lsDirBasename(notccwd));
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
                    printf("[isDirectory: %d]",(int)selection->isDirectory);
                    if(selection->isDirectory)
                    {
                        copyDir(ccwd,selection->thisLine,notccwd,NULL);
                    } else {
                        copyFile(ccwd,selection->thisLine,selection->fileSize,notccwd);
                    }
                    notccwd_needs_update = 1;
                    break;
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
            previous_state = machine_state;
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
            heldU = 0;
        } else if (hidKeysHeld() & (KEY_UP)) {
            heldU++;
            if (heldU > HELD_THRESHOLD)
            {
                cursor_y--;
                redrawCursor(&cursor_y,ccwd);       
                heldU = 0;
            }
        }        
        if(hidKeysDown() & (KEY_DOWN))
        {
            cursor_y++;
            redrawCursor(&cursor_y,ccwd);
            heldD = 0;
        } else if (hidKeysHeld() & (KEY_DOWN)) {
            heldD++;
            if (heldD > HELD_THRESHOLD)
            {
                cursor_y++;
                redrawCursor(&cursor_y,ccwd);
                heldD = 0;
            }
        }
        if(hidKeysDown() & KEY_B)
        {
            if (strcmp("/",(const char*)ccwd->thisDir))
            {
                gotoParentDirectory(ccwd);
                debugOut("Navigating to parent directory.");
                cwd_needs_update = 1;
            }
        }
        if(hidKeysDown() & KEY_A)
        {
            switch (cursor_y)
            {
                case 0:
                    debugOut("Refreshing current directory.");
                    cwd_needs_update = 1;
                    break;
                case 1:
                    if (strcmp("/",(const char*)ccwd->thisDir) && ccwd->lsOffset == 0)
                    {
                        gotoParentDirectory(ccwd);
                        debugOut("Navigating to parent directory.");
                        cwd_needs_update = 1;
					    break;
                    }
					if (ccwd->lsOffset == 0)
						break;
                default: ;
                    lsLine* selection = ccwd->firstLine;
                    int i;
                    for (i=0;i<cursor_y+ccwd->lsOffset-2;i++)
                    {
                        selection = selection->nextLine;
                    }
                    debugOut(selection->thisLine);
                    printf("[isDirectory: %d]",(int)selection->isDirectory);
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
                        debugOut("Navigating to subdirectory.");
                    }
                    if ((whichSecureGame!=SECURE_UNKNOWN)&&(machine_state==SELECT_SAVE))
                    {
                        char* destPath = (char*)malloc(strlen(ccwd->thisDir)+strlen(selection->thisLine)+1);
                        memset(destPath,0,strlen(ccwd->thisDir)+strlen(selection->thisLine)+1);
                        strcat(destPath,ccwd->thisDir);
                        strcat(destPath,selection->thisLine);
                        printf("\n[isSecureFile: %d]",isSecureFile(destPath));
                        free(destPath);
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
            debugOut("Scanned current directory.");
            printf("[path: %s]\n[dirEntryCount: %d]\n",ccwd->thisDir,ccwd->dirEntryCount);
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
            
            debugOut("Scanned not-current directory.");
            printf("[path: %s]\n[dirEntryCount: %d]\n",notccwd->thisDir,notccwd->dirEntryCount);
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
    amExit();
    gfxExit();
    return 0;
}
