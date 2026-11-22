#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <dirent.h>
#include <pthread.h>
#include <fstream>
#include <string.h>
#include <time.h>
#include <malloc.h>
#include <iostream>
#include <string>
#include <sys/stat.h>
#include <sys/wait.h>
#include "res/pz.h"


/*
 * ============================ 对接参数 ============================
 * 加密算法已由 RC4 改为 AES-256-GCM（标准，见 res/Encrypt.h）
 * 注意：AESKEY 长度必须为 32 字节（256bit）。若填其它长度，
 *       程序会自动退化为 key = SHA256(AESKEY)。
 * ==================================================================
 */
char *APPID = (char *)"11054";
const char *APPKEY = "y4sH2w11imYIx1K4";                  /* 16 字节 AppKey，用于 MD5 签名 */
const char *AESKEY = "mN4bV7cX1zL9kJ2hG5fD8sA0qW3enknb";  /* 32 字节 AES-256 原始密钥 */
extern const bool AES_ON = true;                          /* true = 传输加密开启（现已改为 SM4-CBC 动态IV+HMAC-SM3，
                                                             密钥见 res/pz.h 的 GM_KEY16）；false = 明文直传（后台须设「不加密」） */
const char *LOCAL_VERSION = "1.0.0";



int main()
{
    wyz();
}