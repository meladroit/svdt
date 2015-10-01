// MH4U.h: helper functions to decrypt and encrypt Monster Hunter 4 Ultimate savegame
#ifndef MH4U_H
#define MH4U_H

int MH4U_decryptBuff(u8** inbuff, u64 lSize);
int MH4U_encryptBuff(u8** inbuff, u64 lSize);

#endif
