// secure_values.c
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <3ds.h>
#include <zlib.h>

#include "utils.h"
#include "svdt.h"
#include "filesystem.h"
#include "secure_values.h"

int secureValueSet = 0;
u8 secureValue[SECURE_VALUE_SIZE] = {0};

const u32 const secureOffsets[PRECONF_GAMES] = {ACNL_OFFSET, SSB_OFFSET, POKETORU_OFFSET, POKEXY_OFFSET, POKEXY_OFFSET, POKEORAS_OFFSET, POKEORAS_OFFSET};
const char const secureFilenames[PRECONF_GAMES][MAX_PATH_LENGTH] = {"/garden.dat", "/account_data.bin", "/savedata.bin", "/main", "/main", "/main", "/main"};
const char const secureProductCodes[PRECONF_GAMES][16] = {"CTR-P-EGD",
    "CTR-P-NXC", "CTR-N-KRX",
    "CTR-P-EKJ", "CTR-P-EK2",
    "CTR-P-ECR", "CTR-P-ECL"}; // minus region
const char* const pokeRumbleWorldCode = "CTR-N-KCF";

char configProductCode[9] = {0};

const char* const POKERW_SVPATH = "/00slot00/00main.dat";
const char* const customSecurePath = "/svdt_sv_data";
const char* const secureConfigPath = "/3ds/svdt/asr.dat";
const char* const secureConfigBasename = "asr.dat";

Result checkCustomSecureGame()
{
    return doesFileExist(customSecurePath,&sdmcFsHandle,sdmcArchive);
}

Result checkSecureConfig()
{
    return doesFileExist(secureConfigPath,&sdmcFsHandle,sdmcArchive);
}

secureGame secureGameFromProductCode(const char* productCode)
{
    int i;
    // let's get Poke Rumble World out of the way ...
    if (!strncmp(productCode,pokeRumbleWorldCode,9))
        return SECURE_POKERW;
    // ideally we actually have asr.dat in /3ds/svdt
    if(!checkSecureConfig())
    {
        // and ideally we actually have the product code in there ...
        FILE* config = fopen(secureConfigBasename,"rb");
        int ret = 0;
        char productCodeBuffer[9];
        char* out1;
        unsigned int* out2;
        out1 = NULL;
        out2 = NULL;
        while (ret!=EOF)
        {
            ret = fscanf(config,"%s %s %08x ",productCodeBuffer,out1,out2);
            if (!strncmp(productCode,productCodeBuffer,9))
            {
                strcpy(configProductCode,productCode);
                return SECURE_CONFIG;
            }
        }
    }
    // however, if asr.dat is absent, then we have info to fall back on ...
    for (i=0;i<PRECONF_GAMES;i++)
        if (!strncmp(productCode,secureProductCodes[i],9))
            return i;
    // custom specifier does not take precedence over having the actual product code
    if(!checkCustomSecureGame())
        return SECURE_EMERGENCY;
    return SECURE_UNKNOWN;
}

secureGame secureGameFromFilesystem()
{
    u64 size = 0;
    // first, some sure guesses
    if(!getSaveGameFileSize("/garden.dat",&size))
        return SECURE_ACNL;
    if(!getSaveGameFileSize(POKERW_SVPATH,&size))
    {
        // would like to check for cAVIAR4\x00 identifier (and >100 files in /00slot/?)
        char charviar[8] = {0};
        Result ret = readBytesFromSaveFile(POKERW_SVPATH,0x0,(u8*)charviar,8);
        if(!ret && !strcmp("cAVIAR4\x00",charviar))
            return SECURE_POKERW;
    }
    if(!getSaveGameFileSize("/account_data.bin",&size))
        if(!getSaveGameFileSize("/system_data.bin",&size))
            return SECURE_SSB;
    // from here, the guesses are a little trickier
    // custom specifier absolutely takes precedence over these guesses
    if(!checkCustomSecureGame())
        return SECURE_EMERGENCY;
    if(!getSaveGameFileSize("/main",&size))
    {
        if (size < POKEORAS_OFFSET)
            return SECURE_POKEX; // to be perfectly honest we don't care about X/Y
        else
            return SECURE_POKEOR; // ditto with OR/AS
    }
    // and finally we just give up before even guessing whether savedata.bin belongs to Shuffle
    return SECURE_UNKNOWN;
}

Result getSecureValue(secureGame whichSecureGame)
{
    if(secureValueSet)
        return 0;
    switch (whichSecureGame)
    {
        case SECURE_POKERW:
            return getPokeRumbleSecureValue();
            break;
        case SECURE_EMERGENCY: ;
            FILE* offsets = fopen(customSecurePath,"rb");
            char filenameBuffer[MAX_PATH_LENGTH];
            unsigned int offset;
            int ret = 0;
            while (ret!=EOF)
            {
                ret = fscanf(offsets,"%s %08x ",filenameBuffer,&offset);
                if (ret==EOF)
                    break;
                readBytesFromSaveFile(filenameBuffer,(u32)offset,secureValue,SECURE_VALUE_SIZE);
            }
            fclose(offsets);
            break;
        case SECURE_CONFIG:
            return getSecureValue2(configProductCode);
            break;
        case SECURE_UNKNOWN:
            break;
        default:
            readBytesFromSaveFile(secureFilenames[whichSecureGame],secureOffsets[whichSecureGame],secureValue,SECURE_VALUE_SIZE);
            break;
    }
    secureValueSet = 1;
    return 0;
}

Result getSecureValue2(const char* productCode)
{
    if (!strncmp(productCode,pokeRumbleWorldCode,9))
        return getPokeRumbleSecureValue();
    if(checkSecureConfig())
        return -1;
    if(!secureValueSet)
        return 0;
    FILE* config = fopen(secureConfigBasename,"rb");
    int ret = 0;
    char productCodeBuffer[9];
    char filenameBuffer[MAX_PATH_LENGTH];
    unsigned int offset;
    while (ret!=EOF)
    {
        ret = fscanf(config,"%s %s %08x ",productCodeBuffer,filenameBuffer,&offset);
        if (feof(config)) break;
        if (!strncmp(productCode,productCodeBuffer,9))
        {
            readBytesFromSaveFile(filenameBuffer,offset,secureValue,SECURE_VALUE_SIZE);
            secureValueSet = 1;
            return 0;
        }
    }
    return -1;
}

Result getPokeRumbleProps(u64* compressed_size, u64* decompressed_size, int* is_compressed, u32* crc32_check)
{
    Result res = getSaveGameFileSize(POKERW_SVPATH,compressed_size);
    if(res) return res;
    *compressed_size-= 0x30; // not going to work with the header outside this function
    
    unsigned char temp, temp2;
    int i;
    
    res = readBytesFromSaveFile(POKERW_SVPATH,0x28,&temp,1);
    if(res) return res;
    *is_compressed = (int)!!temp;
    
    *decompressed_size = 0;
    
    for(i=0;i<4;i++)
    {
        *decompressed_size = *decompressed_size << 8;
        res = readBytesFromSaveFile(POKERW_SVPATH,0x2c+i,&temp,1);
        if(res) return res;
        *decompressed_size|= temp;
        *crc32_check = *crc32_check << 8;
        res = readBytesFromSaveFile(POKERW_SVPATH,0x2c+i,&temp2,1);
        if(res) return res;
        *crc32_check|= temp2;
    }
    return 0;
}

Result getPokeRumbleSecureValue()
{
    if(!secureValueSet)
        return 0;
    u64 compressed_size, decompressed_size;
    u32 crc32_check;
    int is_compressed;
    Result res = getPokeRumbleProps(&compressed_size,&decompressed_size,&is_compressed,&crc32_check);
    
    uLong crc = crc32(0L, Z_NULL, 0);
    
    uLong ul_compressed_size = (uLong)compressed_size;
    uLong ul_decompressed_size = (uLong)decompressed_size;

    u8* final_buffer = (u8*)malloc(decompressed_size);
    if(is_compressed)
    {
        u8* initial_buffer = (u8*)malloc(compressed_size);
        res = readBytesFromSaveFile(POKERW_SVPATH,0x30,initial_buffer,compressed_size);
        if(res) return res;
        crc = crc32(crc,initial_buffer,compressed_size);
        uncompress((Bytef*)final_buffer,&ul_decompressed_size,(Bytef*)initial_buffer,ul_compressed_size);
        free(initial_buffer);
    } else {
        res = readBytesFromSaveFile(POKERW_SVPATH,0x30,final_buffer,decompressed_size);
        crc = crc32(crc,final_buffer,compressed_size);
    }
    if(decompressed_size!=(u32)ul_decompressed_size)
        return -1;
    if (crc32_check!=(u32)crc)
        return -1;
        
    strncpy((char*)secureValue,(char*)(final_buffer+decompressed_size-10),8);
    secureValueSet = 1;
    
    free(final_buffer);
    return 0;
}

Result writeSecureValue(secureGame whichSecureGame)
{
    if(!secureValueSet)
        return -1;
    switch (whichSecureGame)
    {
        case SECURE_SSB:
            writeBytesToSaveFile("/system_data.bin",secureOffsets[whichSecureGame],secureValue,SECURE_VALUE_SIZE);
            break;
        case SECURE_POKERW:
            writePokeRumbleSecureValue();
            break;
        case SECURE_EMERGENCY: ;
            FILE* offsets = fopen(customSecurePath,"rb");
            char filenameBuffer[MAX_PATH_LENGTH];
            unsigned int offset;
            int ret = 0;
            while (ret!=EOF)
            {
                ret = fscanf(offsets,"%s %08x ",filenameBuffer,&offset);
                if (ret==EOF)
                    break;
                writeBytesToSaveFile(filenameBuffer,(u32)offset,secureValue,SECURE_VALUE_SIZE);
            }
            fclose(offsets);
            break;
        case SECURE_CONFIG:
            return writeSecureValue2(configProductCode);
            break;
        case SECURE_UNKNOWN:
            break;
        default:
            writeBytesToSaveFile(secureFilenames[whichSecureGame],secureOffsets[whichSecureGame],secureValue,SECURE_VALUE_SIZE);
            break;
    }
    return 0;
}

Result writeSecureValue2(const char* productCode)
{
    if (!strncmp(productCode,pokeRumbleWorldCode,9))
        return getPokeRumbleSecureValue();
    if(checkSecureConfig())
        return -1;
    if(!secureValueSet)
        return -1;
    FILE* config = fopen(secureConfigBasename,"rb");
    int ret = 0;
    char productCodeBuffer[9];
    char filenameBuffer[MAX_PATH_LENGTH];
    unsigned int offset;
    while (ret!=EOF)
    {
        ret = fscanf(config,"%s %s %08x ",productCodeBuffer,filenameBuffer,&offset);
        if (feof(config)) break;
        if (!strncmp(productCode,productCodeBuffer,9))
            writeBytesToSaveFile(filenameBuffer,offset,secureValue,SECURE_VALUE_SIZE);
    }
    return 0;
}

Result writePokeRumbleSecureValue()
{
    int i;
    if(!secureValueSet)
        return -1;
    u64 compressed_size, decompressed_size;
    u32 crc32_check;
    int is_compressed;
    Result res = getPokeRumbleProps(&compressed_size,&decompressed_size,&is_compressed,&crc32_check);
    
    uLong crc = crc32(0L, Z_NULL, 0);
    
    uLong ul_compressed_size = (uLong)compressed_size;
    uLong ul_decompressed_size = (uLong)decompressed_size;

    u8 header_buffer[0x30] = {0};
    res = readBytesFromSaveFile(POKERW_SVPATH,0x0,header_buffer,0x30);
    if(res) return res;
    u8* temp_buffer = (u8*)malloc(decompressed_size);
    if(is_compressed)
    {
        u8* initial_buffer = (u8*)malloc(compressed_size);
        res = readBytesFromSaveFile(POKERW_SVPATH,0x30,initial_buffer,compressed_size);
        if(res) return res;
        crc = crc32(crc,initial_buffer,compressed_size);
        uncompress((Bytef*)temp_buffer,&ul_decompressed_size,(Bytef*)initial_buffer,ul_compressed_size);
        free(initial_buffer);
    } else {
        res = readBytesFromSaveFile(POKERW_SVPATH,0x30,temp_buffer,decompressed_size);
        crc = crc32(crc,temp_buffer,compressed_size);
    }
    if(decompressed_size!=(u32)ul_decompressed_size)
        return -1;
    if (crc32_check!=(u32)crc)
        return -1;
        
    strncpy((char*)(temp_buffer+decompressed_size-10),(char*)secureValue,8);
    
    u8* final_buffer = (u8*)malloc(compressBound(ul_decompressed_size));
    if(is_compressed)
    {
        compress((Bytef*)final_buffer,&ul_compressed_size,(Bytef*)temp_buffer,ul_decompressed_size);
        crc = crc32(crc,final_buffer,ul_compressed_size);
    } else {
        crc = crc32(crc,temp_buffer,compressed_size);
        strncpy((char*)final_buffer,(char*)temp_buffer,compressed_size);
    }
    free(temp_buffer);
    
    for(i=0;i<4;i++)
        header_buffer[0x8+i] = (crc >> (8*(3-i))) & 0xff;
    
    deleteFile((char*)POKERW_SVPATH,&saveGameArchive,&saveGameFsHandle);
    
	Handle outFileHandle;
	u32 bytesWritten;
	
	res = FSUSER_OpenFile(&saveGameFsHandle, &outFileHandle, saveGameArchive, FS_makePath(PATH_CHAR, POKERW_SVPATH), FS_OPEN_CREATE | FS_OPEN_WRITE, FS_ATTRIBUTE_NONE);
	if(res){return res;}

	res = FSFILE_Write(outFileHandle, &bytesWritten, 0x0, header_buffer, 0x30, 0x10001);
	if(res){return res;}
	res = FSFILE_Write(outFileHandle, &bytesWritten, 0x30, final_buffer, ul_compressed_size, 0x10001);
	if(res){return res;}

	res = FSFILE_Close(outFileHandle);
	if(res){return res;}

	res = FSUSER_ControlArchive(saveGameFsHandle, saveGameArchive);
    
    free(final_buffer);
    return 0;
}
