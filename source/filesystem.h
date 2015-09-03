#ifndef FILESYSTEM_H
#define FILESYSTEM_H

extern Handle saveGameFsHandle, sdmcFsHandle;
extern FS_archive saveGameArchive, sdmcArchive;

Result filesystemInit();
Result filesystemExit();

Result loadFile(char* path, void* dst, FS_archive* archive, Handle* fsHandle, u64 maxSize);
Result writeFile(char* path, u8* data, u32 size, FS_archive* archive, Handle* fsHandle);
Result deleteFile(char* path, FS_archive* archive, Handle* fsHandle);
#endif
