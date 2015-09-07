// svdt.h: functions for svdt
#define MAX_PATH_LENGTH 1024

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

typedef struct lsTitle {
    u64 thisTitle;
    struct lsTitle* nextTitle;
} lsTitle;

extern int canHasConsole;
extern lsTitle* firstTitle;

void freeDir(lsDir* dir);
void gotoParentDirectory(lsDir* dir);
void gotoSubDirectory(lsDir* dir, char* basename);
char* lsDirBasename(lsDir* dir);
void scanDir(lsDir* dir, FS_archive* archive, Handle* fsHandle);

Result getTitleList(u8 mediatype, int* usable_count);
void clearTitleList();
Result getTitleTitle(u64 tid, u8 mediatype, char* titleTitle);
Result nthTitleInList(int n, u8 mediatype, char* titleTitle);
