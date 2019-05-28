#include <stdlib.h>
//#include <xtgmath.h>
//#include <math.h>
#include <tchar.h>
#include <setjmp.h>
#include <windows.h>
#include "pngpriv.h"
//#include "pnglibconf.h"
#include "png.h"
#include "pngstruct.h"
#include "zlib.h"
#include <string>
#include <string.h>
#include <vector>
#include <functional>
#include <thread>

#pragma comment( lib ,"libpng16.lib")
#pragma comment( lib ,"zdll.lib")


typedef struct _tagDIBSECT {
	HBITMAP ddb;
	HDC hdc;
	LPDWORD pixel;
	BITMAPINFO info;
	RGBQUAD bmiColors[255];
} DIBSECT;


#ifdef __cplusplus
extern "C"{
#endif
DIBSECT *LoadPng( LPCTSTR fileName);
int SavePng(LPCTSTR fileName, const BITMAPINFO *info, const DWORD *pixel);
#ifdef __cplusplus
}
#endif
void PngWriteFunc(png_structp pPngStruct, png_bytep buf, png_size_t size);
void PngFlushFunc( png_structp pPngStruct);
void PngReadFunc( png_structp pPngStruct ,png_bytep buf ,png_size_t size);
int dummychunkreader( png_structp a ,png_unknown_chunkp b);

