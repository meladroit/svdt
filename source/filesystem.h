#ifndef FILESYSTEM_H
#define FILESYSTEM_H

extern Handle saveGameFsHandle, sdmcFsHandle;
extern FS_archive saveGameArchive, sdmcArchive;

Result filesystemInit();
Result filesystemExit();
Result filesystemSoftReset();
Result FSUSER_ControlArchive(Handle handle, FS_archive archive);
Result FSUSER_GetMediaType(Handle handle, u8* mediatype);
Result loadFile(char* path, void* dst, FS_archive* archive, Handle* fsHandle, u64 maxSize);
Result writeFile(char* path, u8* data, u32 size, FS_archive* archive, Handle* fsHandle);
Result deleteFile(char* path, FS_archive* archive, Handle* fsHandle);
u64 sizeFile(char* path, FS_archive* archive, Handle* fsHandle);
Result readBytesFromSaveFile(const char* filename, u64 offset, u8* buffer, u32 size);
Result writeBytesToSaveFile(const char* filename, u64 offset, u8* buffer, u32 size);
Result getSaveGameFileSize(const char* filename, u64* size);
Result doesFileExist(const char* filename, Handle* fsHandle, FS_archive archive);
#endif
