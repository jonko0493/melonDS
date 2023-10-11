/***********************************************************
 * 文字コード変換 (ShiftJIS -> UTF-8)
 **********************************************************/
#include <fstream>
#include <iostream>          // for cout, endl
#include <stdio.h>           // for fopen
#include <string.h>          // for strlen
#include <iconv.h>           // for iconv

#define ENC_SRC "Shift_JIS"  // Encoding ( Src )
#define ENC_DST "UTF-8"      // Encoding ( Dst )
#define B_SIZE  1024         // Buffer size

bool convert(char* src, char* dst)
{
    char    *pSrc, *pDst;
    size_t  nSrc, nDst;
    iconv_t icd;

    try {
        pSrc = src;
        pDst = dst;
        nSrc = strlen(src);
        nDst = B_SIZE - 1;
        while (0 < nSrc) {
            icd = iconv_open(ENC_DST, ENC_SRC);
            iconv(icd, &pSrc, &nSrc, &pDst, &nDst);
            iconv_close(icd);
        }
        *pDst = '\0';

        return true;
    } catch (char *e) {
        std::cerr << "EXCEPTION : " << e << std::endl;
        return false;
    }
}

std::string sj2utf8(const std::string& sjInput)
{
    char src[B_SIZE];
    char dst[B_SIZE];
    strcpy(src, sjInput.c_str());
    convert(src, dst);
    return std::string(dst);
}