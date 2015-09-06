#include <string.h>
#include <stdio.h>

#include <3ds.h>

#include "filesystem.h"

Handle saveGameFsHandle, sdmcFsHandle;
FS_archive saveGameArchive, sdmcArchive;

typedef struct lsLine
{
    FS_dirent thisLine;
    struct lsLine* nextLine;
} lsLine;

typedef struct lsDir
{
    struct lsLine* firstLine;
    struct lsDir* parentDir;
} lsDir;


// bypass handle list
Result _srvGetServiceHandle(Handle* out, const char* name)
{
	Result rc = 0;

	u32* cmdbuf = getThreadCommandBuffer();
	cmdbuf[0] = 0x50100;
	strcpy((char*) &cmdbuf[1], name);
	cmdbuf[3] = strlen(name);
	cmdbuf[4] = 0x0;
	
	if((rc = svcSendSyncRequest(*srvGetSessionHandle())))return rc;

	*out = cmdbuf[3];
	return cmdbuf[1];
}

Result filesystemInit()
{
	Result ret;
	
	ret = _srvGetServiceHandle(&saveGameFsHandle, "fs:USER");
	if(ret)return ret;
	
	ret = FSUSER_Initialize(&saveGameFsHandle);
	if(ret)return ret;

	ret = srvGetServiceHandle(&sdmcFsHandle, "fs:USER");
	if(ret)return ret;

	saveGameArchive = (FS_archive){0x00000004, (FS_path){PATH_EMPTY, 1, (u8*)""}};
	ret = FSUSER_OpenArchive(&saveGameFsHandle, &saveGameArchive);

	sdmcArchive = (FS_archive){0x00000009, (FS_path){PATH_EMPTY, 1, (u8*)""}};
	ret = FSUSER_OpenArchive(&sdmcFsHandle, &sdmcArchive);

	return ret;
}

Result filesystemExit()
{
	Result ret;
	
	ret = FSUSER_CloseArchive(&saveGameFsHandle, &saveGameArchive);
	ret = FSUSER_CloseArchive(&sdmcFsHandle, &sdmcArchive);
	ret = svcCloseHandle(saveGameFsHandle);
	ret = svcCloseHandle(sdmcFsHandle);

	return ret;
}

Result filesystemSoftReset()
{
    // exit and reinit without giving up those handles
	Result ret;
	
	ret = FSUSER_CloseArchive(&saveGameFsHandle, &saveGameArchive);
	ret = FSUSER_CloseArchive(&sdmcFsHandle, &sdmcArchive);

	saveGameArchive = (FS_archive){0x00000004, (FS_path){PATH_EMPTY, 1, (u8*)""}};
	ret = FSUSER_OpenArchive(&saveGameFsHandle, &saveGameArchive);

	sdmcArchive = (FS_archive){0x00000009, (FS_path){PATH_EMPTY, 1, (u8*)""}};
	ret = FSUSER_OpenArchive(&sdmcFsHandle, &sdmcArchive);

	return ret;
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

Result FSUSER_GetMediaType(Handle handle, u8* mediatype)
{
	u32* cmdbuf=getThreadCommandBuffer();

	cmdbuf[0]=0x08680000;
 
	Result ret=0;
	if((ret=svcSendSyncRequest(handle)))return ret;

    if(mediatype)
        *mediatype = (u8)cmdbuf[2];
 
	return cmdbuf[1];
}

// let's add stuff from 3ds_hb_menu, just because
Result loadFile(char* path, void* dst, FS_archive* archive, Handle* fsHandle, u64 maxSize)
{
    // must malloc first! (and memset, if you'd like)
    if(!path || !dst || !archive)return -1;

    u64 size;
    u32 bytesRead;
    Result ret;
    Handle fileHandle;

    ret=FSUSER_OpenFile(fsHandle, &fileHandle, *archive, FS_makePath(PATH_CHAR, path), FS_OPEN_READ, FS_ATTRIBUTE_NONE);
    if(ret!=0)return ret;

    ret=FSFILE_GetSize(fileHandle, &size);
    if(ret!=0)goto loadFileExit;
    if(size>maxSize){ret=-2; goto loadFileExit;}

    ret=FSFILE_Read(fileHandle, &bytesRead, 0x0, dst, size);
    if(ret!=0)goto loadFileExit;
    if(bytesRead<size){ret=-3; goto loadFileExit;}

    loadFileExit:
    FSFILE_Close(fileHandle);
    return ret;
}

// oh and let's add in a writeFile, because we kind of need that
Result writeFile(char* path, u8* data, u32 size, FS_archive* archive, Handle* fsHandle)
{
    if(!path || !data)return -1;

    Handle outFileHandle;
    u32 bytesWritten;
    Result ret = 0;

    ret = FSUSER_OpenFile(fsHandle, &outFileHandle, *archive, FS_makePath(PATH_CHAR, path), FS_OPEN_CREATE | FS_OPEN_WRITE, FS_ATTRIBUTE_NONE);
    if(ret!=0)return ret;

    ret = FSFILE_Write(outFileHandle, &bytesWritten, 0x0, data, size, 0x10001);
    if(ret!=0)return ret;

    ret = FSFILE_Close(outFileHandle);
    if(ret!=0)return ret;

    if(archive==&saveGameArchive)
    {
        //printf("calling ControlArchive\n");
        ret = FSUSER_ControlArchive(saveGameFsHandle, saveGameArchive);
    }

    return ret;
}

// I'll try deleting! that's a good trick!
Result deleteFile(char* path, FS_archive* archive, Handle* fsHandle)
{
    if(!path || !archive)return -1;

    Result ret = FSUSER_DeleteFile(fsHandle, *archive, FS_makePath(PATH_CHAR, path));
    if(ret!=0)return ret;

    if(archive==&saveGameArchive)
    {
        //printf("\ncalling ControlArchive\n");
        ret = FSUSER_ControlArchive(saveGameFsHandle, saveGameArchive);
    }
    return ret;
}

u64 sizeFile(char* path, FS_archive* archive, Handle* fsHandle)
{
    if(!path || !archive)return -1;

    u64 size = -1;
    Handle fileHandle;

    Result ret=FSUSER_OpenFile(fsHandle, &fileHandle, *archive, FS_makePath(PATH_CHAR, path), FS_OPEN_READ, FS_ATTRIBUTE_NONE);
    if(ret!=0)return -1;

    ret=FSFILE_GetSize(fileHandle, &size);
    if(ret!=0)return -1;
    
    FSFILE_Close(fileHandle);
    return size;
}
