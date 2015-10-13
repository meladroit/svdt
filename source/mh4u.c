// mh4u.c
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "blowfish.h"

typedef uint8_t u8;
typedef uint64_t u64;

#define be16(x) ((((x)>>8)&0xFF)|(((x)<<8))&0xFF00)
#define be32(x) (((x)<<24)|(((x)>>8)&0xFF00)|(((x)<<8)&0xFF0000)|((x)>>24))
//be64 from http://ubuntuforums.org/showthread.php?t=1190710&s=71d0a980f2bc5192b67a07de5f9afa03&p=7477357#post7477357
#define be64(val) ((u64) ( \
    (((u64) (val) & (u64) 0x00000000000000ff) << 56) | \
    (((u64) (val) & (u64) 0x000000000000ff00) << 40) | \
    (((u64) (val) & (u64) 0x0000000000ff0000) << 24) | \
    (((u64) (val) & (u64) 0x00000000ff000000) <<  8) | \
    (((u64) (val) & (u64) 0x000000ff00000000) >>  8) | \
    (((u64) (val) & (u64) 0x0000ff0000000000) >> 24) | \
    (((u64) (val) & (u64) 0x00ff000000000000) >> 40) | \
    (((u64) (val) & (u64) 0xff00000000000000) >> 56)))

/*
These are the Blowfish Keys.

KEY_MUL and KEY_MOD are used to advance the key when XORing the savegame.

char* SAV_MH4U[] = {"blowfish key iorajegqmrna4itjeangmb agmwgtobjteowhv9mope"};
char* DLC_MH4U_EU[] = {"AgK2DYheaCjyHGPB"};
char* DLC_MH4U_JP[] = {"AgK2DYheaCjyHGP8"};
char* DLC_MH4U_KR[] = {"AgK2DYheaOjyHGP8"};
char* DLC_MH4U_TW[] = {"Capcom123 "};
short KEY_MUL = 0xB0;
short KEY_MOD = 0xFF53;
*/
int MH4U_decryptBuff(u8** inbuff, u64 lSize){

    u8 * buffer;
    long position = 0;

    buffer = *inbuff;

    //Start decryption
        //printf ("Decrypting...\n");

        BLOWFISH_CTX ctx;
        Blowfish_Init (&ctx, (unsigned char*)"blowfish key iorajegqmrna4itjeangmb agmwgtobjteowhv9mope", 56);
        for (position = 0; position < lSize; position += 8){
            Blowfish_Decrypt(&ctx, buffer+position, buffer+4+position);
        }
        //Verify
        unsigned short SIXTEEN;
        memcpy (&SIXTEEN, buffer, 2);
        if (SIXTEEN != 16){
			//printf ("\tDecryption failed!\n");
			return 0;
		}

        //Get Key
        unsigned short KEY;
        memcpy (&KEY, buffer+2, 2);
        //printf("\tKey: %X\n",KEY);
        unsigned short out;
        unsigned int tempkey = KEY;
        unsigned int csum1 = 0;
        unsigned int csum2 = 0;
        //XOR with key and advance KEY
        for (position = 4; position < (lSize); position += 2){
            if (tempkey == 0)
                tempkey = 1;
            tempkey = (tempkey * 0xB0);
            tempkey = tempkey % 0xFF53;
            memcpy (&out, buffer+position, 2);
            out = out ^ tempkey;
            memcpy(buffer+position, &out, 2);
            //get checksum
            if (position == 4){
                memcpy (&csum1, &out, 2);
                //printf("%X\n",csum1);
            }
            if (position == 6){
                memcpy (&csum2, &out, 2);
                //printf("%X\n",csum2);
            }
        }

        //Calculate decrypted chcecksum
        unsigned int byte = 0;
        unsigned int csum_calc = 0;
        for (position = 8; position < (lSize); position ++){
            memcpy(&byte, buffer+position, 1);
            csum_calc += byte;
        }
        csum_calc &= 0xFFFFFFFF;
        //printf ("\tCalculated checksum: %08X\n", csum_calc);

        //Get file checksum
        unsigned int csum;
        csum = ((csum2 & 0xFFFF) << 16) | (csum1 & 0xFFFF);
        //printf ("\tFile checksum:       %08X\n", csum);

        if (csum != csum_calc){
            //printf("Checksum mismatch!\n");
            return 0;
        }

        return 1;
}

int MH4U_encryptBuff(u8** inbuff, u64 lSize){

    u8 * buffer2;
    long position = 0;

    buffer2 = *inbuff;

        //printf("Encrypting buffer...\n");
    //put 16 bit "16" magic in buffer2
        short tmp = 16;
        memcpy (buffer2, &tmp, 2);

    //Calculate checksum
        unsigned int byte2 = 0;
        unsigned int csum_calc2 = 0;
        for (position = 8; position < (lSize+8); position ++){
            memcpy(&byte2, buffer2+position, 1);
            csum_calc2 += byte2;
        }
        csum_calc2 &= 0xFFFFFFFF;
        //printf ("\tCalculated checksum: %08X\n", csum_calc2);

        //put checksum in buffer2
        memcpy (buffer2+4, &csum_calc2, 4);

    //Generate pseudo-random key
        unsigned short KEY2 = 0;
        KEY2 = csum_calc2 & 0xFFFF;
        //printf("\tkey2: %d\n", KEY2);
        KEY2 = ((KEY2 << 7) & 0xFFFF) | (KEY2 >> 9); //16bit rotated left 7x
        //printf("\tkey2: %d\n", KEY2);
        KEY2 ^= 0x484D; // xor with "MH"
        //printf("\tKEY: %X\n", KEY2);

        //put 16 bit key in buffer2
        tmp = KEY2;
        memcpy (buffer2+2, &tmp, 2);

    //XOR with random key
        unsigned short out2;
        unsigned int KEYtmp = KEY2;
        for (position = 4; position < (lSize+8); position += 2){
            if (KEYtmp == 0)
                KEYtmp = 1;
            KEYtmp = (KEYtmp * 0xB0);
            //printf("keytmp1: %d\n", KEYtmp);
            KEYtmp = KEYtmp % 0xFF53;
            //printf("keytmp2: %d\n", KEYtmp);
            memcpy (&out2, buffer2+position, 2);
            out2 = out2 ^ KEYtmp;
            //getchar();

            memcpy(buffer2+position, &out2, 2);
        }

    //Blowfish encrypt
        BLOWFISH_CTX ctx;
        Blowfish_Init (&ctx, (unsigned char*)"blowfish key iorajegqmrna4itjeangmb agmwgtobjteowhv9mope", 56);
        for (position = 0; position < lSize+8; position += 8){
            Blowfish_Encrypt(&ctx, buffer2+position, buffer2+4+position);
        }

        return 1;
}
