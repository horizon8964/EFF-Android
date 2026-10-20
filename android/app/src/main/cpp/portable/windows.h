#pragma once
// POSIX implementations of the file primitives used by the offline EFF compiler.
// This directory is visible only to the compiler target, never Dobby/runtime code.
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <climits>
#include <cstdlib>
#include <cerrno>
#include <cwchar>
#include <string>
#include <strings.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <map>
#include <mutex>
#include <chrono>
using DWORD=uint32_t; using ULONG=uint32_t; using UINT=uint32_t;
using BOOL=int; using LONGLONG=int64_t; using ULONGLONG=uint64_t;
using SIZE_T=size_t; using PUCHAR=unsigned char*; using NTSTATUS=int;
struct FILETIME{DWORD dwLowDateTime=0,dwHighDateTime=0;};
struct WIN32_FILE_ATTRIBUTE_DATA{DWORD dwFileAttributes=0;FILETIME ftCreationTime{},ftLastAccessTime{},ftLastWriteTime{};DWORD nFileSizeHigh=0,nFileSizeLow=0;};
struct BY_HANDLE_FILE_INFORMATION:WIN32_FILE_ATTRIBUTE_DATA{DWORD dwVolumeSerialNumber=0,nFileIndexHigh=0,nFileIndexLow=0;};
struct LARGE_INTEGER{int64_t QuadPart=0;};
struct SYSTEM_INFO{DWORD dwAllocationGranularity=4096;};
struct EffFileHandle{int fd=-1; bool owns=true;};
using HANDLE=EffFileHandle*;
#define INVALID_HANDLE_VALUE ((HANDLE)(intptr_t)-1)
constexpr int GENERIC_READ=1,GENERIC_WRITE=2,FILE_SHARE_READ=1,FILE_SHARE_WRITE=2,FILE_SHARE_DELETE=4,OPEN_EXISTING=0,CREATE_ALWAYS=1,FILE_ATTRIBUTE_NORMAL=0,FILE_ATTRIBUTE_DIRECTORY=16,FILE_FLAG_SEQUENTIAL_SCAN=0,FILE_BEGIN=SEEK_SET,GetFileExInfoStandard=0,PAGE_READONLY=1,FILE_MAP_READ=1,MOVEFILE_REPLACE_EXISTING=1,MOVEFILE_WRITE_THROUGH=2,ERROR_INSUFFICIENT_BUFFER=122,ERROR_SUCCESS=0;
inline DWORD GetLastError(){return errno;}
inline uint64_t GetCurrentProcessId(){return getpid();}
inline uint64_t GetTickCount64(){return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
inline void EffStat(const struct stat& s,WIN32_FILE_ATTRIBUTE_DATA* out){out->dwFileAttributes=S_ISDIR(s.st_mode)?16:0;uint64_t n=s.st_size,t=uint64_t(s.st_mtim.tv_sec)*10000000+s.st_mtim.tv_nsec/100;out->nFileSizeLow=n;out->nFileSizeHigh=n>>32;out->ftLastWriteTime={DWORD(t),DWORD(t>>32)};}
inline BOOL GetFileAttributesExW(const char* path,int,WIN32_FILE_ATTRIBUTE_DATA* out){struct stat s{};if(stat(path,&s))return false;EffStat(s,out);return true;}
inline HANDLE CreateFileW(const char* path,int access,int,void*,int create,int,void*){int fd=open(path,access==GENERIC_WRITE?(O_WRONLY|O_CREAT|(create==CREATE_ALWAYS?O_TRUNC:0)):O_RDONLY,0600);return fd<0?INVALID_HANDLE_VALUE:new EffFileHandle{fd,true};}
inline BOOL CloseHandle(HANDLE h){if(!h||h==INVALID_HANDLE_VALUE)return false;if(h->owns)close(h->fd);delete h;return true;}
inline BOOL GetFileInformationByHandle(HANDLE h,BY_HANDLE_FILE_INFORMATION* out){struct stat s{};if(fstat(h->fd,&s))return false;EffStat(s,out);out->dwVolumeSerialNumber=s.st_dev;out->nFileIndexLow=s.st_ino;out->nFileIndexHigh=uint64_t(s.st_ino)>>32;return true;}
inline BOOL GetFileSizeEx(HANDLE h,LARGE_INTEGER* n){struct stat s{};if(fstat(h->fd,&s))return false;n->QuadPart=s.st_size;return true;}
inline BOOL SetFilePointerEx(HANDLE h,LARGE_INTEGER n,LARGE_INTEGER* result,int origin){auto p=lseek(h->fd,n.QuadPart,origin);if(result)result->QuadPart=p;return p>=0;}
inline BOOL ReadFile(HANDLE h,void* p,DWORD n,DWORD* actual,void*){ssize_t readBytes;do{readBytes=read(h->fd,p,n);}while(readBytes<0&&errno==EINTR);*actual=readBytes<0?0:DWORD(readBytes);return readBytes>=0;}
inline BOOL MoveFileExW(const char* from,const char* to,int){return rename(from,to)==0;}
inline void GetSystemInfo(SYSTEM_INFO* s){s->dwAllocationGranularity=sysconf(_SC_PAGESIZE);}
inline HANDLE CreateFileMappingW(HANDLE h,void*,int,int,int,void*){return new EffFileHandle{h->fd,false};}
inline std::mutex effMapMutex;inline std::map<const void*,size_t> effMaps;
inline void* MapViewOfFile(HANDLE h,int,DWORD hi,DWORD lo,size_t n){void* p=mmap(nullptr,n,PROT_READ,MAP_PRIVATE,h->fd,(uint64_t(hi)<<32)|lo);if(p==MAP_FAILED)return nullptr;std::lock_guard<std::mutex> lock(effMapMutex);effMaps[p]=n;return p;}
inline BOOL UnmapViewOfFile(const void* p){std::lock_guard<std::mutex> lock(effMapMutex);auto it=effMaps.find(p);if(it==effMaps.end())return false;bool ok=munmap(const_cast<void*>(p),it->second)==0;effMaps.erase(it);return ok;}
inline int _wcsicmp(const char* a,const char* b){return strcasecmp(a,b);}
inline int _wcsicmp(const char* a,const wchar_t* b){std::string s;for(;*b;++b)s+=char(*b);return strcasecmp(a,s.c_str());}
constexpr int CP_UTF8=65001,MB_ERR_INVALID_CHARS=8;
inline int MultiByteToWideChar(int,int,const char* data,int length,void*,int){
 int count=0;for(int i=0;i<length;++count){unsigned c=(unsigned char)data[i++];if(c<128)continue;int extra;uint32_t value,minimum;
 if(c>=0xC2&&c<=0xDF){extra=1;value=c&31;minimum=128;}else if(c>=0xE0&&c<=0xEF){extra=2;value=c&15;minimum=2048;}else if(c>=0xF0&&c<=0xF4){extra=3;value=c&7;minimum=65536;}else return 0;
 if(extra>length-i)return 0;for(int j=0;j<extra;++j){unsigned d=(unsigned char)data[i++];if((d&192)!=128)return 0;value=(value<<6)|(d&63);}if(value<minimum||value>0x10FFFF||(value>=0xD800&&value<=0xDFFF))return 0;
 }return count;
}

constexpr UINT VK_F1=112;
constexpr UINT VK_F24=135;
constexpr UINT VK_INSERT=45;
constexpr UINT VK_DELETE=46;
constexpr UINT VK_HOME=36;
constexpr UINT VK_END=35;
constexpr UINT VK_PRIOR=33;
constexpr UINT VK_NEXT=34;
constexpr UINT VK_LEFT=37;
constexpr UINT VK_RIGHT=39;
constexpr UINT VK_UP=38;
constexpr UINT VK_DOWN=40;
constexpr UINT VK_SPACE=32;
constexpr UINT VK_RETURN=13;
constexpr UINT VK_ESCAPE=27;
constexpr UINT VK_TAB=9;
constexpr UINT VK_BACK=8;
constexpr UINT VK_CAPITAL=20;
constexpr UINT VK_OEM_3=192;
constexpr UINT VK_ADD=107;
constexpr UINT VK_SUBTRACT=109;
constexpr UINT VK_MULTIPLY=106;
constexpr UINT VK_DIVIDE=111;
constexpr UINT VK_DECIMAL=110;
constexpr UINT MOD_CONTROL=2;
constexpr UINT MOD_SHIFT=4;
constexpr UINT MOD_ALT=1;
constexpr UINT VK_NUMPAD0=96;
constexpr UINT VK_NUMPAD1=97;
constexpr UINT VK_NUMPAD2=98;
constexpr UINT VK_NUMPAD3=99;
constexpr UINT VK_NUMPAD4=100;
constexpr UINT VK_NUMPAD5=101;
constexpr UINT VK_NUMPAD6=102;
constexpr UINT VK_NUMPAD7=103;
constexpr UINT VK_NUMPAD8=104;
constexpr UINT VK_NUMPAD9=105;
inline BOOL CreateHardLinkW(const char* target,const char* source,void*){return link(source,target)==0;}
