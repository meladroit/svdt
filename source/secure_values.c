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
#include "mh4u.h"

int secureValueSet = 0;
u8 secureValue[SECURE_VALUE_SIZE] = {0};
secureGame whichSecureGame = SECURE_UNKNOWN;

const u32 const secureOffsets[PRECONF_GAMES] = {ACNL_OFFSET, SSB_OFFSET, POKETORU_OFFSET, POKEXY_OFFSET, POKEXY_OFFSET, POKEORAS_OFFSET, POKEORAS_OFFSET};
const char const secureFilenames[PRECONF_GAMES][MAX_PATH_LENGTH] = {"/garden.dat", "/save_data/account_data.bin", "/savedata.bin", "/main", "/main", "/main", "/main"};
const char const secureProductCodes[PRECONF_GAMES][16] = {"CTR-P-EGD",
    "CTR-P-NXC", "CTR-N-KRX",
    "CTR-P-EKJ", "CTR-P-EK2",
    "CTR-P-ECR", "CTR-P-ECL"}; // minus region
const char* const pokeRumbleWorldCode = "CTR-N-KCF";
const char* const MH4UCode = "XXX-X-XXX"; //Monster Hunter 4 Ultimate product code = "CTR-P-BFG", disabled until we know for sure it uses secure value.

char configProductCode[9] = {0};

const char* const POKERW_SVPATH = "/00slot00/00main.dat";
const char* const MH4U_SVPATH1 = "/user1";
const char* const MH4U_SVPATH2 = "/user2";
const char* const MH4U_SVPATH3 = "/user3";
const char* const customSecurePath = "/svdt_sv_data";
const char* const secureConfigPath = "asr.dat";
const char* const secureConfigBasename = "asr.dat";

int MH4U_file = 0;

int isSecureFile(const char* destPath)
{
    switch (whichSecureGame)
    {
        case SECURE_POKERW:
            return (!strcmp(destPath,POKERW_SVPATH));
            break;
		case SECURE_MH4U:
			if (!strcmp(destPath,MH4U_SVPATH1)){
				MH4U_file = 1;
				return 1;
			}else if (!strcmp(destPath,MH4U_SVPATH2)){
				MH4U_file = 2;
				return 1;
			}else if (!strcmp(destPath,MH4U_SVPATH3)){
				MH4U_file = 3;
				return 1;
			}else{
				return 0;
			}
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
                if (!strcmp(destPath,filenameBuffer))
                {
                    fclose(offsets);
                    return 1;
                }
            }
            fclose(offsets);
            return 0;
            break;
        case SECURE_CONFIG:
            return isSecureFile2(destPath,configProductCode);
            break;
        case SECURE_UNKNOWN:
            break;
        case SECURE_SSB:
            if(!strcmp(destPath,"/system_data.bin"))
                return 1;
        default:
            //printf("destPath: %s\n",destPath);
            //printf(" against: %s\n",secureFilenames[whichSecureGame]);
            //printf("strcmp result is %d\n",strcmp(destPath,secureFilenames[whichSecureGame]));
            if(!strcmp(destPath,secureFilenames[whichSecureGame]))
                return 1;
            break;
    }
    return 0;
}

int isSecureFile2(const char* destPath, const char* productCode)
{
    if (!strncmp(productCode,pokeRumbleWorldCode,9))
        return (!strcmp(destPath,POKERW_SVPATH));
    if (!strncmp(productCode,MH4UCode,9)){
			if (!strcmp(destPath,MH4U_SVPATH1)){
				MH4U_file = 1;
				return 1;
			}else if (!strcmp(destPath,MH4U_SVPATH2)){
				MH4U_file = 2;
				return 1;
			}else if (!strcmp(destPath,MH4U_SVPATH3)){
				MH4U_file = 3;
				return 1;
			}else{
				return 0;
			}
	}
    if(!checkSecureConfig())
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
        if (!strncmp(productCodeBuffer,configProductCode,9) && !strncmp(filenameBuffer,destPath,strlen(destPath)))
            return 1;
    }
    return 0;
}

Result checkCustomSecureGame()
{
    //return doesFileNotExist(customSecurePath,&sdmcFsHandle,sdmcArchive);
    return file_exist(customSecurePath);
}

Result checkSecureConfig()
{
    //return doesFileNotExist(secureConfigPath,&sdmcFsHandle,sdmcArchive);
    return file_exist(secureConfigPath);
}

void secureGameFromProductCode(const char* productCode)
{
    int i;
    secureValueSet = 0;
    // let's get Poke Rumble World out of the way ...
    if (!strncmp(productCode,pokeRumbleWorldCode,9))
    {
        whichSecureGame = SECURE_POKERW;
        return;
    }
    // and also Monster Hunter 4 Ultimate ...
    if (!strncmp(productCode,MH4UCode,9))
    {
        whichSecureGame = SECURE_MH4U;
        return;
    }	
    // ideally we actually have asr.dat in /3ds/svdt
    if(checkSecureConfig())
    {
        // and ideally we actually have the product code in there ...
        FILE* config = fopen(secureConfigBasename,"rb");
        int ret = 0;
        char productCodeBuffer[9] = {0};
        char out1[MAX_PATH_LENGTH] = {0};
        unsigned int out2 = 0;
        //printf("\nasr.dat opened\n");
        while (ret!=EOF)
        {
            ret = fscanf(config,"%s %s %08x ",productCodeBuffer,out1,&out2);
            if (!strncmp(productCode,productCodeBuffer,9))
            {
                //printf("found match, so closing file\n");
                strcpy(configProductCode,productCode);
                fclose(config);
                whichSecureGame = SECURE_CONFIG; return;
            }
        }
        //printf("reached EOF without match, apparently\n");
        fclose(config);
    }
    // however, if asr.dat is absent, then we have info to fall back on ...
    for (i=0;i<PRECONF_GAMES;i++)
    {
        if (!strncmp(productCode,secureProductCodes[i],9))
        {
            whichSecureGame = i;
            return;
        }
    }
    // custom specifier does not take precedence over having the actual product code
    if(checkCustomSecureGame())
    {
        whichSecureGame = SECURE_EMERGENCY;
        return;
    }
    whichSecureGame = SECURE_UNKNOWN; return;
}

void secureGameFromFilesystem()
{
    u64 size = 0;
    secureValueSet = 0;
    // custom specifier absolutely takes precedence over these guesses
    if(checkCustomSecureGame())
    {
        whichSecureGame = SECURE_EMERGENCY;
        return;
    }
    // first, we're most sure about ACNL
    if(!getSaveGameFileSize("/garden.dat",&size))
    {
        whichSecureGame = SECURE_ACNL;
        return;
    }
    // from here, the guesses are much less safe
    // (there's a reason we override these if we can get to the target title prompt)
    if(!getSaveGameFileSize("/save_data/account_data.bin",&size))
    {
        if(!getSaveGameFileSize("/save_data/system_data.bin",&size))
        {
            whichSecureGame = SECURE_SSB;
            return;
        }
    }
    if(!getSaveGameFileSize(POKERW_SVPATH,&size))
    {
        // would like to check for cAVIAR4\x00 identifier (and >100 files in /00slot/?)
        // this check still has a collision with Rumble Blast
        // but if the rest of the header is compatible, and the secure value, compression, and checksum are compatible, the handling would be the same
        // and if the rest of the header is not compatible, the handling should fail out while trying to get properties from the header and/or verify the CRC32 checksum
        // (notice the area in between ...)
        char charviar[8] = {0};
        Result ret = readBytesFromSaveFile(POKERW_SVPATH,0x0,(u8*)charviar,8);
        if(!ret && !strcmp("cAVIAR4\x00",charviar))
        {
            whichSecureGame = SECURE_POKERW;
            return;
        }
    }
    /* ORAS demo makes this one tricky, I'm afraid
    if(!getSaveGameFileSize("/main",&size))
    {
        if (size < POKEORAS_OFFSET)
            whichSecureGame = SECURE_POKEX; // to be perfectly honest we don't care about X/Y
        else
            whichSecureGame = SECURE_POKEOR; // ditto with OR/AS
            
        return;
    } */
    // and finally we just give up before even guessing whether savedata.bin belongs to Shuffle
    whichSecureGame = SECURE_UNKNOWN;
    return;
}

void printSecureGame()
{
    switch (whichSecureGame)
    {
        case SECURE_ACNL:
            printf("Animal Crossing: New Leaf");
            break;
        case SECURE_SSB:
            printf("Super Smash Bros. for 3DS");
            break;
        case SECURE_POKETORU: //not implemented
            printf("Pokemon Shuffle");
            break;
        case SECURE_POKEX:
            printf("Pokemon X");
            break;
        case SECURE_POKEY:
            printf("Pokemon Y");
            break;
        case SECURE_POKEOR:
            printf("Pokemon Omega Ruby");
            break;
        case SECURE_POKEAS:
            printf("Pokemon Alpha Sapphire");
            break;
        case SECURE_EMERGENCY:
            printf("apparently you know what it is");
            break;
        case SECURE_CONFIG:
            printf("apparently asr.dat knows what it is");
            break;
        case SECURE_POKERW:
            printf("oh god it's Pokemon Rumble World");
            break;
        case SECURE_MH4U:
            printf("oh god it's Monster Hunter 4 Ultimate");
            break;
        default:
            printf("no, not inferred at all");
            break;
    }
    printf("\n    (ASR%d)",whichSecureGame);
}

Result getSecureValue()
{
    Result res = 0;
    switch (whichSecureGame)
    {
        case SECURE_POKERW:
            return getPokeRumbleSecureValue();
            break;
        case SECURE_MH4U:
            return getMH4USecureValue();
            break;
        case SECURE_EMERGENCY: ;
            FILE* offsets = fopen(customSecurePath,"rb");
            char filenameBuffer[MAX_PATH_LENGTH];
            unsigned int offset;
            int ret = 0;
            while (ret!=EOF)
            {
                ret = fscanf(offsets,"%s %08x ",filenameBuffer,&offset);
                res = readBytesFromSaveFile(filenameBuffer,(u32)offset,secureValue,SECURE_VALUE_SIZE);
            }
            fclose(offsets);
            break;
        case SECURE_CONFIG:
            return getSecureValue2(configProductCode);
            break;
        case SECURE_UNKNOWN:
            return -1;
            break;
        default:
            res = readBytesFromSaveFile(secureFilenames[whichSecureGame],secureOffsets[whichSecureGame],secureValue,SECURE_VALUE_SIZE);
            break;
    }
    secureValueSet = 1;
    return res;
}

Result getSecureValue2(const char* productCode)
{
    if (!strncmp(productCode,pokeRumbleWorldCode,9))
        return getPokeRumbleSecureValue();
    if (!strncmp(productCode,MH4UCode,9))
        return getMH4USecureValue();	
    if(!checkSecureConfig())
        return -1;
    //printf("\nopening asr.dat\n");
    FILE* config = fopen(secureConfigBasename,"rb");
    int ret = 0;
    char productCodeBuffer[9];
    char filenameBuffer[MAX_PATH_LENGTH];
    unsigned int offset;
    while (ret!=EOF)
    {
        ret = fscanf(config,"%s %s %08x ",productCodeBuffer,filenameBuffer,&offset);
        if (!strncmp(productCode,productCodeBuffer,9))
        {
            //printf("%s %s %08x\n",productCodeBuffer,filenameBuffer,offset);
            Result res = readBytesFromSaveFile(filenameBuffer,offset,secureValue,SECURE_VALUE_SIZE);
            if(res) return res;
            secureValueSet = 1;
            fclose(config);
            return 0;
        }
    }
    fclose(config);
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
        res = readBytesFromSaveFile(POKERW_SVPATH,0x08+i,&temp2,1);
        if(res) return res;
        *crc32_check|= temp2;
    }
    return 0;
}

Result getPokeRumbleSecureValue()
{
    u64 compressed_size, decompressed_size;
    u32 crc32_check;
    int is_compressed;
    Result res = getPokeRumbleProps(&compressed_size,&decompressed_size,&is_compressed,&crc32_check);
    if(res) return res;
    
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
        if(res) return res;
        crc = crc32(crc,final_buffer,compressed_size);
    }
    if(decompressed_size!=(u32)ul_decompressed_size)
        return -1;
    if (crc32_check!=(u32)crc)
        return -1;
        
    memcpy((void*)secureValue,(void*)(final_buffer+decompressed_size-10),8);
    secureValueSet = 1;
    
    free(final_buffer);
    return 0;
}

Result writeSecureValue()
{
    if(!secureValueSet)
        return -1;
    switch (whichSecureGame)
    {
        case SECURE_POKERW:
            writePokeRumbleSecureValue();
            break;
        case SECURE_MH4U:
            writeMH4USecureValue();
            break;
        case SECURE_EMERGENCY: ;
            FILE* offsets = fopen(customSecurePath,"rb");
            char filenameBuffer[MAX_PATH_LENGTH];
            unsigned int offset;
            int ret = 0;
            while (ret!=EOF)
            {
                ret = fscanf(offsets,"%s %08x ",filenameBuffer,&offset);
                writeBytesToSaveFile(filenameBuffer,(u32)offset,secureValue,SECURE_VALUE_SIZE);
            }
            fclose(offsets);
            break;
        case SECURE_CONFIG:
            return writeSecureValue2(configProductCode);
            break;
        case SECURE_UNKNOWN:
            break;
        case SECURE_SSB:
            writeBytesToSaveFile("/system_data.bin",secureOffsets[whichSecureGame],secureValue,SECURE_VALUE_SIZE);
        default:
            writeBytesToSaveFile(secureFilenames[whichSecureGame],secureOffsets[whichSecureGame],secureValue,SECURE_VALUE_SIZE);
            break;
    }
    return 0;
}

Result writeSecureValue2(const char* productCode)
{
    if (!strncmp(productCode,pokeRumbleWorldCode,9))
        return writePokeRumbleSecureValue();
    if (!strncmp(productCode,MH4UCode,9))
        return writeMH4USecureValue();
    if(!checkSecureConfig())
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
        if (!strncmp(productCode,productCodeBuffer,9))
            writeBytesToSaveFile(filenameBuffer,offset,secureValue,SECURE_VALUE_SIZE);
    }
    fclose(config);
    return 0;
}

Result writePokeRumbleSecureValue()
{
    int i;
    if(!secureValueSet) return -1;
    /*
        secureValue[0] = 0xfb; secureValue[1] = 0x89; secureValue[2] = 0x06;
        secureValue[3] = 0x14; secureValue[4] = 0x72; secureValue[5] = 0xdf;
        secureValue[6] = 0x66; secureValue[7] = 0x1c;
    */
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
        
    memcpy((void*)(temp_buffer+decompressed_size-10),(void*)secureValue,8);
    
    crc = crc32(0L, Z_NULL, 0);
    ul_compressed_size = compressBound(ul_decompressed_size);
    u8* final_buffer = (u8*)malloc(ul_compressed_size);
    if(is_compressed)
    {
        res = compress2((Bytef*)final_buffer,&ul_compressed_size,(Bytef*)temp_buffer,ul_decompressed_size,9);
        if(res) return res;
        crc = crc32(crc,final_buffer,ul_compressed_size);
    } else {
        crc = crc32(crc,temp_buffer,compressed_size);
        memcpy((void*)final_buffer,(void*)temp_buffer,compressed_size);
    }
    /*
    for(i=0;i<8;i++)
    {
        printf("%02x ",temp_buffer[decompressed_size-10+i]);
    }
    putchar('\n');*/
    free(temp_buffer);
    
    for(i=0;i<4;i++)
    {
        header_buffer[0x8+i] = (crc >> (8*(3-i))) & 0xff;
        //printf("%02x ",header_buffer[0x8+i]);
    }
    //putchar('\n');
    
    
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

Result getMH4USecureValue()
{
	u64 size;
	u8* buffer;
	//Try to open all savefiles
	Result res = getSaveGameFileSize(MH4U_SVPATH1,&size);
	if (res != 0 ){
		res = getSaveGameFileSize(MH4U_SVPATH2,&size);
			if (res != 0 ){
				res = getSaveGameFileSize(MH4U_SVPATH3,&size);
				if (res != 0){
					return res;
				}else{
					buffer = (u8*)malloc(size);
					res = readBytesFromSaveFile(MH4U_SVPATH3,0,buffer,size);
				}
			}else{
				buffer = (u8*)malloc(size);
				res = readBytesFromSaveFile(MH4U_SVPATH2,0,buffer,size);
			}
	} else{
		buffer = (u8*)malloc(size);
		res = readBytesFromSaveFile(MH4U_SVPATH1,0,buffer,size);
	}
	if (res != 0 ) return res;

	if (!MH4U_decryptBuff(&buffer, size))
		return 1;
        
    memcpy((void*)secureValue,(void*)(buffer+8+0x112),8);
    secureValueSet = 1;
    
    free(buffer);
    return 0;
}

Result writeMH4USecureValue()
{
    if(!secureValueSet) return -1;

	char PATH[12];
	
	switch (MH4U_file)
    {
        case 1:
            strcpy(PATH, MH4U_SVPATH1);
            break;
        case 2:
            strcpy(PATH, MH4U_SVPATH2);
            break;
        case 3:
            strcpy(PATH, MH4U_SVPATH3);
            break;
        default:
			return 0;
            break;
    }
	//Read file
	u64 size;
	Result res = getSaveGameFileSize(PATH,&size);
	if (res != 0) return res;
	u8* buffer = (u8*)malloc(size);
	res = readBytesFromSaveFile(PATH,0,buffer,size);
	if (res != 0) return res;

	//Decrypt
	MH4U_decryptBuff(&buffer, size);
    
	//Change secure value
    memcpy((void*)(buffer+8+0x112),(void*)secureValue,8);
    
	//Encrypt
	MH4U_encryptBuff(&buffer, size-8);
	
	//Write file
    deleteFile((char*)PATH,&saveGameArchive,&saveGameFsHandle);
    
	Handle outFileHandle;
	u32 bytesWritten;
	
	res = FSUSER_OpenFile(&saveGameFsHandle, &outFileHandle, saveGameArchive, FS_makePath(PATH_CHAR, PATH), FS_OPEN_CREATE | FS_OPEN_WRITE, FS_ATTRIBUTE_NONE);
	if(res){return res;}

	res = FSFILE_Write(outFileHandle, &bytesWritten, 0, buffer, size, 0x10001);
	if(res){return res;}

	res = FSFILE_Close(outFileHandle);
	if(res){return res;}

	res = FSUSER_ControlArchive(saveGameFsHandle, saveGameArchive);
    
    free(buffer);
    return 0;
}
