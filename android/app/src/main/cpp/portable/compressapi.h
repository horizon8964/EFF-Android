#pragma once
#include "windows.h"
// Raw EFF author resources do not require Windows compiled.bin compression.
// Fail explicitly if a future caller attempts to consume that Windows cache.
using COMPRESSOR_HANDLE=void*;using DECOMPRESSOR_HANDLE=void*;
constexpr int COMPRESS_ALGORITHM_XPRESS_HUFF=4;
inline BOOL CreateCompressor(int,void*,COMPRESSOR_HANDLE*){return false;}
inline BOOL CreateDecompressor(int,void*,DECOMPRESSOR_HANDLE*){return false;}
inline BOOL Compress(void*,const void*,size_t,void*,size_t,size_t*){return false;}
inline BOOL Decompress(void*,const void*,size_t,void*,size_t,size_t*){return false;}
inline void CloseCompressor(void*){}
inline void CloseDecompressor(void*){}
