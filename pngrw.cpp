//PNG multithread encode and decode sample. (code fragment)
//You will need Libpng and Zlib sources.
//It works with multithreading only when the input is 8 bpp.


#include "pngrw.h"



DIBSECT *DIBSECTNew(int bpp, const RGBQUAD *colortable, int numcolors, int cx, int cy);
int DeleteDIBSECT(DIBSECT **dib);




using namespace std;



DIBSECT *LoadPng( LPCTSTR fileName)
{
	FILE *pFile = NULL;
	_wfopen_s(&pFile, fileName, _T("rb"));
	if (pFile == NULL)
		return NULL;

	const size_t nSizeSig = 8;
	const int nBytesSig = sizeof(BYTE) * nSizeSig;
	BYTE sig[nBytesSig];

	size_t nSizeReadSig = fread(sig, sizeof(BYTE), nSizeSig, pFile);
	if (nSizeReadSig != nSizeSig){
		fclose(pFile);
		return NULL;
	}

	if (!png_check_sig(sig, nBytesSig)){
		fclose(pFile);
		return NULL;
	}

	png_struct *pPngStruct = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	if (pPngStruct == NULL){
		fclose(pFile);
		return NULL;
	}

	png_info *pPngInfo = png_create_info_struct(pPngStruct);
	if (pPngInfo == NULL){
		png_destroy_read_struct(&pPngStruct, NULL, NULL);
		fclose(pFile);
		return NULL;
	}

	png_bytep *ppRowTable = 0;

	if (setjmp(png_jmpbuf(pPngStruct))) {
		if (ppRowTable){
			delete[] ppRowTable;
		}
		png_destroy_read_struct(&pPngStruct, &pPngInfo, NULL);
		fclose(pFile);
		return NULL;
	}


	static PNG_CONST png_byte chunks_to_process[] = {
		'I', 'D', 'A', 'T', '\0',  /* vpLt */
	};
	//png_set_keep_unknown_chunks(pPngStruct, -1 /* PNG_HANDLE_CHUNK_NEVER */,NULL, -1);
	//png_set_keep_unknown_chunks(pPngStruct,	0 /* PNG_HANDLE_CHUNK_AS_DEFAULT */, chunks_to_process,	sizeof(chunks_to_process) / 5);
	pPngStruct->flags |= 0x10000L;//PNG_FLAG_KEEP_UNSAFE_CHUNKS
	png_set_keep_unknown_chunks(pPngStruct, PNG_HANDLE_CHUNK_ALWAYS, NULL, 0);// (png_const_bytep)"vpLt\0", 1);
	//png_set_keep_unknown_chunks(pPngStruct, PNG_HANDLE_CHUNK_ALWAYS,(png_const_bytep)"IDAT\0", 1);



	png_set_read_fn(pPngStruct, (png_voidp)pFile, (png_rw_ptr)PngReadFunc);

	png_set_read_user_chunk_fn(pPngStruct, NULL, dummychunkreader);//これがないと不定チャンクは読めないらしい


	png_set_sig_bytes(pPngStruct, nBytesSig);
	png_read_info(pPngStruct, pPngInfo);

	png_uint_32 width = 0;
	png_uint_32 height = 0;
	int bit_depth = 0;
	int color_type = 0;
	int channels = 0;
	png_get_IHDR(pPngStruct, pPngInfo, &width, &height, &bit_depth, &color_type, NULL, NULL, NULL);
	channels = png_get_channels(pPngStruct, pPngInfo);

	int nPalette = 0;
	png_color *palette;
	png_get_PLTE(pPngStruct, pPngInfo, &palette, &nPalette);

	RGBQUAD pallet[256];

	for (int i = 0; i < nPalette; ++i){
		pallet[i].rgbRed = palette[i].red;
		pallet[i].rgbGreen = palette[i].green;
		pallet[i].rgbBlue = palette[i].blue;
	}

	int bpp = bit_depth * channels;
	DIBSECT *dibsect = NULL;
	if (bpp == 24){
		bpp = 32;
		dibsect = DIBSECTNew(bpp, pallet, nPalette, width, height);
	}
	else{
		dibsect = DIBSECTNew(bpp, pallet, nPalette, width, height);
	}
	ppRowTable = new png_bytep[height];
	int widthBytes;
	if (bpp == 1) widthBytes = (width + 31 & ~31) / 8;
	if (bpp == 8) widthBytes = (width + 3) / 4 * 4;
	if (bpp == 24) widthBytes = (width * 24 + 31 & ~31) / 8;
	if (bpp == 32) widthBytes = width * 4;
	for (UINT y = 0; y < height; ++y){
		ppRowTable[y] = (png_bytep)dibsect->pixel + (height - 1 - y) * widthBytes;
	}

	if (bit_depth * channels >= 24){
		png_set_bgr(pPngStruct);
		png_set_filler(pPngStruct, 0, PNG_FILLER_AFTER);
		png_set_strip_alpha(pPngStruct);
	}

	if (bpp == 8){//IDATチャンク単位でマルチスレッドデコード
		FILE *fp;
		BYTE data[4];
		size_t length = 0;
		vector<Bytef *> idatArray;
		vector<size_t> idatSize;
		bool loadError = false;

		//IDATチャンク読み込み
		if (_wfopen_s(&fp, fileName, _T("rb")) != 0){
			DebugDlg(_T("Cannot open png file."));
			return NULL;
		}

		fseek(fp, 8, SEEK_SET);
		do{
			if (fread_s(data, sizeof(data), 1, 4, fp) != 4){
				break;
			}
			length = data[0] << 24 | data[1] << 16 | data[2] << 8 | data[3]; //チャンク長

			if (fread_s(data, sizeof(data), 1, 4, fp) != 4){//チャンク名
				break;
			}

			if (data[0] == 'I' && data[1] == 'D' && data[2] == 'A' && data[3] == 'T'){//IDATチャンクだったら読み込む
				Bytef *idat;

				if (idatArray.size() == 0){
					idat = new Bytef[length];

					if (fread_s(idat, length, 1, length, fp) != length){
						delete[] idat;
						break;
					}
				}
				else{
					idat = new Bytef[length + 2];//plus zlib header

					if (fread_s(&idat[2], length, 1, length, fp) != length){
						delete[] idat;
						break;
					}

					idat[0] = idatArray[0][0];
					idat[1] = idatArray[0][1];

					length += 2;
				}

				idatArray.push_back(idat);
				idatSize.push_back(length);


				fseek(fp, 4, SEEK_CUR);//CRCを読み飛ばす
			}
			else{
				fseek(fp, length + 4, SEEK_CUR);//次のチャンクまで読み飛ばす
			}

		} while (!(data[0] == 'I' && data[1] == 'E' && data[2] == 'N' && data[3] == 'D'));


		if (idatArray.size() > 1 && 8 <= idatArray.size() && idatArray.size() <= 64){//チャンクが8個以上64個以下なら自前のマルチスレッド伸長ルーチンを使う
			int num_thread = idatArray.size();;
			int *beginY = new int[num_thread];
			int *endY = new int[num_thread];
			vector<thread> threads;
			bool *error = new bool[num_thread];

			//num_thread = 1;//debug
			//マルチスレッドで伸長
			for (int i = 0; i < num_thread; ++i){
				error[i] = false;

				beginY[i] = height * i / num_thread;
				endY[i] = height * (i + 1) / num_thread;

				if (endY[i] > height){
					endY[i] = height;
				}

				threads.push_back(thread([&, i]
				{
					z_stream zs;
					int flush;
					int status;
					int y;

					flush = Z_NO_FLUSH;
					//flush = Z_SYNC_FLUSH;

					zs.next_in = Z_NULL;
					zs.avail_in = 0;
					zs.zalloc = Z_NULL;
					zs.zfree = Z_NULL;
					zs.opaque = Z_NULL;

					zs.next_in = idatArray[i];
					zs.avail_in = idatSize[i];
					zs.avail_out = width;
					zs.next_out = (png_bytep)dibsect->pixel + (height - 1 - beginY[i]) * widthBytes;


					for (y = beginY[i]; y < endY[i]; ++y){
						unsigned char filter = 0;


						//フィルタを読み捨て
						zs.avail_out = 1;
						zs.next_out = &filter;
						status = inflate(&zs, flush); /* 伸長する */
						if (status != Z_OK) {   /* エラー */
							error[i] = true;
							return;
						}
						if (filter != 0) {
							error[i] = true;
							return;
						}

						//1ライン伸長
						//zs.avail_in = idatSize[i];
						zs.avail_out = width;
						zs.next_out = (png_bytep)dibsect->pixel + (height - 1 - y) * widthBytes;
						//if (y == endY[i] - 1){//分割単位で最終行だったらフラッシュ
						//	flush = Z_SYNC_FLUSH;
						//}
						//flush = Z_NO_FLUSH;
						while (status != Z_STREAM_END){
							status = inflate(&zs, flush); /* 伸長する */
							if (status == Z_STREAM_END) {
								if (zs.avail_in != 0){
									status = inflateReset(&zs);
								}
								else{
									break;
								}
							}
							if (status == Z_STREAM_ERROR) {   /* エラー */
								error[i] = true;
								return;
							}

							if (zs.avail_in == 0) {  /* 入力が尽きれば */
								break;
							}
							if (zs.avail_out == 0) { /* 出力バッファが尽きれば */
								break;
							}
						}
						if (error[i]){
							return;
						}
						if (zs.avail_in == 0){
							break;
						}
						if (zs.avail_out == 0 && y == endY[i] - 1){
						}
					}
					inflateEnd(&zs);
				}
				));


			}
			for (int i = 0; i < num_thread; ++i){
				threads[i].join();
			}

			for (int i = 0; i < num_thread; ++i){
				if (error[i] == true){//libpngで伸長
					loadError = true;
					break;
				}
			}

			delete[] error;
			delete[] beginY;
			delete[] endY;
		}
		else{//libpngで伸長
			loadError = true;
		}


		//解放
		fclose(fp);

		for (int i = 0; i < idatArray.size(); ++i){
			delete[] idatArray[i];
		}



		//マルチスレッド読み込みが失敗ならlibpngで読み込み
		if (loadError){
			png_read_image(pPngStruct, ppRowTable);
			png_read_end(pPngStruct, pPngInfo);
		}
	}
	else{
		png_read_image(pPngStruct, ppRowTable);
		png_read_end(pPngStruct, pPngInfo);
	}
	delete[] ppRowTable;




	png_destroy_read_struct(&pPngStruct, &pPngInfo, NULL);
	fclose(pFile);


	return dibsect;
}

void PngReadFunc(png_structp pPngStruct, png_bytep buf, png_size_t size)
{
	FILE *pFile = (FILE *)png_get_io_ptr( pPngStruct);
	fread( buf ,(size_t)size ,1 ,pFile);
}


int dummychunkreader( png_structp pPngStruct ,png_unknown_chunkp puc)
{

	char *src = (char *)(puc[0].data);
	size_t size = puc[0].size;
	char *str;


	return 1;
}



int SavePng( LPCTSTR fileName ,const BITMAPINFO *info ,const DWORD *pixel)
{
	FILE *pFile = NULL;
	_wfopen_s( &pFile ,fileName , _T("wb"));
	if( pFile == NULL)
		return 1;
	
	png_struct *pPngStruct = png_create_write_struct( PNG_LIBPNG_VER_STRING ,NULL ,NULL ,NULL);
	if( pPngStruct == NULL){
		fclose( pFile);
		return 2;
	}

	png_info *pPngInfo = png_create_info_struct( pPngStruct);
	if( pPngInfo == NULL){
		png_destroy_read_struct( &pPngStruct ,NULL ,NULL);
		fclose( pFile);
		return 3;
	}

	UINT width = info->bmiHeader.biWidth;
	UINT height = info->bmiHeader.biHeight;
	UINT bpp = info->bmiHeader.biBitCount;
	int widthBytes;
	if( bpp == 1) widthBytes = (width + 31 & ~31) / 8;
	if( bpp == 8) widthBytes = (width + 3) / 4 * 4;
	if( bpp == 24) widthBytes = (width * 24 + 31 & ~31) / 8;
	if( bpp == 32) widthBytes = width * 4;
	DWORD dwSizeImage = widthBytes * height;


	png_colorp pngPalette = NULL;
	int nPalette = 0;
	if( bpp == 1) nPalette = 2;
	if( bpp == 4) nPalette = 16;
	if( bpp == 8) nPalette = 256;
	pngPalette = new png_color[ nPalette];
	for( UINT i = 0 ; i < nPalette; ++i){
		pngPalette[i].red = info->bmiColors[i].rgbRed;
		pngPalette[i].green = info->bmiColors[i].rgbGreen;
		pngPalette[i].blue = info->bmiColors[i].rgbBlue;
	}

	png_bytepp ppRowTable;
	ppRowTable = new png_bytep[ height];
	for( UINT y = 0 ; y < height ; ++y){
		ppRowTable[y] = (png_bytep)pixel + (height - 1 - y) * widthBytes;
	}
	
    if (setjmp( png_jmpbuf(pPngStruct))) {
        if( pngPalette) delete[]pngPalette;
		if( ppRowTable) delete []ppRowTable;
        //PNGのヘッダー情報の削除
        png_destroy_write_struct(&pPngStruct ,&pPngInfo);
        return 4;
    }


	png_set_write_fn( pPngStruct ,(png_voidp)pFile ,(png_rw_ptr)PngWriteFunc ,(png_flush_ptr)PngFlushFunc);
	png_set_filter( pPngStruct ,0 ,PNG_FILTER_NONE);
	if (bpp <= 8){
		png_set_IHDR(pPngStruct, pPngInfo, width, height, bpp, PNG_COLOR_TYPE_PALETTE, PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
	}
	else{
		png_set_IHDR(pPngStruct, pPngInfo, width, height, 8, PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
	}
	if (bpp <= 8){
		png_set_PLTE(pPngStruct, pPngInfo, pngPalette, nPalette);
	}

	png_write_info( pPngStruct ,pPngInfo);
	
	if (bpp == 32){
		png_set_filler(pPngStruct, 0, PNG_FILLER_AFTER);
		png_set_bgr(pPngStruct);
	}

	if (bpp == 8){//マルチスレッドで圧縮
		const int num_thread = 16;//スレッド数
		const size_t blockSize = 1048576;
		int beginY[num_thread];
		int endY[num_thread];
		vector<Bytef> *outBuf;
		size_t size[num_thread];
		unsigned int adler32[num_thread];
		vector<thread> threads;
		unsigned long adler32Combined = 1L;

		outBuf = new vector<Bytef>[num_thread];

		//マルチスレッドで圧縮
		for (int i = 0; i < num_thread; ++i){
			beginY[i] = height * i / num_thread;
			endY[i] = height * (i + 1) / num_thread;

			if (endY[i] > height){
				endY[i] = height;
			}

			threads.push_back(thread([&,i]
			{
				z_stream zs;
				size_t bufSize = blockSize;
				int flush;
				int status;

				outBuf[i].resize(bufSize);

				flush = Z_NO_FLUSH;
				//flush = Z_SYNC_FLUSH;

				zs.zalloc = Z_NULL;
				zs.zfree = Z_NULL;
				zs.opaque = Z_NULL;
				if (status = deflateInit(&zs, Z_DEFAULT_COMPRESSION)){
				}

				//zs.avail_in = 0;
				zs.next_out = &outBuf[i][0];
				zs.avail_out = bufSize;

				for (int y = beginY[i]; y < endY[i]; ++y){
					unsigned char filter = 0;

					//フィルタを圧縮
					zs.next_in = &filter;
					zs.avail_in = 1;
					while (1){
						status = deflate(&zs, flush); /* 圧縮する */
						if (status == Z_STREAM_END) break; /* 完了 */
						if (status == Z_STREAM_ERROR) {   /* エラー */
							exit(1);
						}

						if (zs.avail_in == 0) {  /* 入力が尽きれば */
							break;
						}
						if (zs.avail_out == 0) { /* 出力バッファが尽きれば */
							bufSize += blockSize;
							outBuf[i].resize(bufSize);

							zs.next_out = &outBuf[i][bufSize - blockSize];
							zs.avail_out = blockSize;
						}
					}

					//1ライン圧縮
					zs.next_in = &((Bytef *)pixel)[(height - 1 - y) * widthBytes];
					zs.avail_in = width;
					if (y == endY[i] - 1){//分割単位で最終行だったらフラッシュ
						flush = Z_SYNC_FLUSH;
					}
					while (1){
						status = deflate(&zs, flush); /* 圧縮する */
						if (status == Z_STREAM_END) break; /* 完了 */
						if (status == Z_STREAM_ERROR) {   /* エラー */
							exit(1);
						}

						if (zs.avail_in == 0) {  /* 入力が尽きれば */
							if (y == height - 1){//最終行だったら
								flush = Z_FINISH;
							}
							else{
								break;
							}
						}
						if (zs.avail_out == 0) { /* 出力バッファが尽きれば */
							bufSize += blockSize;
							outBuf[i].resize(bufSize);

							zs.next_out = &outBuf[i][bufSize - blockSize];
							zs.avail_out = blockSize;
						}
					}
				}
				size[i] = bufSize - zs.avail_out;;
				adler32[i] = zs.adler;

				deflateEnd(&zs);
			}
			));
		}
		for (int i = 0; i < num_thread; ++i){
			threads[i].join();
		}


		//adler32を求める
		for (int i = 0; i < num_thread; ++i){
			int inputLength = (endY[i] - beginY[i]) * (width + 1);
			adler32Combined = adler32_combine(adler32Combined, adler32[i], inputLength);
		}
		//memcpy(&outBuf[num_thread - 1][size[num_thread - 1] - 4], &adler32Combined, sizeof(adler32Combined));
		outBuf[num_thread - 1][size[num_thread - 1] - 4] = adler32Combined >> 24;
		outBuf[num_thread - 1][size[num_thread - 1] - 3] = adler32Combined >> 16;
		outBuf[num_thread - 1][size[num_thread - 1] - 2] = adler32Combined >> 8;
		outBuf[num_thread - 1][size[num_thread - 1] - 1] = adler32Combined >> 0;

		//圧縮データを出力
		pPngStruct->mode |= PNG_HAVE_IDAT;
		png_unknown_chunk uc[num_thread];

		for (int i = 0; i < num_thread; ++i){
			strcpy_s((char *)uc[i].name, sizeof(uc[i].name), "IDAT");
			//2番目以降のチャンクは先頭2バイトを捨てる
			if (i == 0){
				uc[i].data = &outBuf[i][0];
				uc[i].size = size[i];
			}
			else{
				uc[i].data = &outBuf[i][2];
				uc[i].size = size[i] - 2;
			}
			uc[i].location = PNG_AFTER_IDAT;
		}
		//uc[num_thread - 1].size += 4;

		pPngStruct->flags |= 0x10000L;//PNG_FLAG_KEEP_UNSAFE_CHUNKS
		png_set_keep_unknown_chunks(pPngStruct, PNG_HANDLE_CHUNK_ALWAYS, NULL, 0);
		png_set_unknown_chunks(pPngStruct, pPngInfo, uc, num_thread);
		png_set_unknown_chunk_location(pPngStruct, pPngInfo, 0, PNG_AFTER_IDAT);

		delete[] outBuf;
	}
	else{
		png_write_rows(pPngStruct, ppRowTable, height);
	}
	

	png_write_end( pPngStruct ,pPngInfo);
	png_destroy_write_struct( &pPngStruct ,&pPngInfo);

	delete[] pngPalette;
	delete[] ppRowTable;

	fclose( pFile);

	return 0;
}


void PngWriteFunc( png_structp pPngStruct ,png_bytep buf ,png_size_t size)
{
	FILE *pFile = (FILE *)png_get_io_ptr( pPngStruct);
	fwrite( buf ,(size_t)size ,1 ,pFile);
}


void PngFlushFunc( png_structp pPngStruct)
{
	FILE *pFile = (FILE *)png_get_io_ptr( pPngStruct);
	fflush( pFile);
}





//Other functions required for operation

DIBSECT *DIBSECTNew(int bpp, const RGBQUAD *colortable, int numcolors, int cx, int cy)
{
	DIBSECT *newdib;

	if (bpp != 1 && bpp != 4 && bpp != 8 && bpp != 24 && bpp != 32)
		return NULL;
	if (cx <= 0 || cy <= 0)
		return NULL;

	newdib = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(DIBSECT));
	if (newdib == NULL){
		ferr = 41;
		return NULL;
	}
	newdib->info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	newdib->info.bmiHeader.biWidth = cx;
	newdib->info.bmiHeader.biHeight = cy;
	newdib->info.bmiHeader.biPlanes = 1;
	newdib->info.bmiHeader.biBitCount = bpp;
	newdib->info.bmiHeader.biCompression = BI_RGB;
	newdib->info.bmiHeader.biClrUsed = 0;
	newdib->info.bmiHeader.biClrImportant = 0;
	if (colortable != NULL && numcolors > 0)
		memcpy(newdib->info.bmiColors, colortable, numcolors * sizeof(RGBQUAD));
	newdib->ddb = CreateDIBSection(NULL, &newdib->info, DIB_RGB_COLORS, &newdib->pixel, NULL, 0);
	newdib->hdc = CreateCompatibleDC(NULL);
	SelectObject(newdib->hdc, newdib->ddb);

	return newdib;
}



int DeleteDIBSECT(DIBSECT **dib)
{
	if (*dib == NULL)
		return 1;
	DeleteDC((*dib)->hdc);
	DeleteObject((*dib)->ddb);
	HeapFree(GetProcessHeap(), 0, *dib);
	*dib = NULL;

	return 0;
}
