#pragma once
#include "windows.h"
#include <array>
#include <vector>
// SHA-256, FIPS 180-4. Used for the upstream BCrypt SHA-256 call contract.
struct EffSha256 {
 uint32_t h[8]={0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
 unsigned char block[64]{};size_t used=0;uint64_t bytes=0;
 static uint32_t R(uint32_t n,int k){return (n>>k)|(n<<(32-k));}
 void Transform(){
  static constexpr uint32_t k[64]={0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
  uint32_t w[64];for(int i=0;i<16;++i)w[i]=(uint32_t(block[i*4])<<24)|(uint32_t(block[i*4+1])<<16)|(uint32_t(block[i*4+2])<<8)|block[i*4+3];
  for(int i=16;i<64;++i)w[i]=w[i-16]+(R(w[i-15],7)^R(w[i-15],18)^(w[i-15]>>3))+w[i-7]+(R(w[i-2],17)^R(w[i-2],19)^(w[i-2]>>10));
  uint32_t a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],v=h[7];
  for(int i=0;i<64;++i){uint32_t t1=v+(R(e,6)^R(e,11)^R(e,25))+((e&f)^(~e&g))+k[i]+w[i];uint32_t t2=(R(a,2)^R(a,13)^R(a,22))+((a&b)^(a&c)^(b&c));v=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;}
  uint32_t state[]={a,b,c,d,e,f,g,v};for(int i=0;i<8;++i)h[i]+=state[i];
 }
 void Update(const unsigned char* p,size_t n){bytes+=n;while(n){size_t step=std::min(n,64-used);memcpy(block+used,p,step);used+=step;p+=step;n-=step;if(used==64){Transform();used=0;}}}
 void Finish(unsigned char* out){uint64_t bits=bytes*8;unsigned char one=128,zero=0;Update(&one,1);while(used!=56)Update(&zero,1);unsigned char length[8];for(int i=0;i<8;++i)length[7-i]=bits>>(i*8);Update(length,8);for(int i=0;i<8;++i)for(int j=0;j<4;++j)out[i*4+j]=h[i]>>(24-j*8);}
};
using BCRYPT_ALG_HANDLE=const wchar_t*;using BCRYPT_HASH_HANDLE=EffSha256*;
constexpr auto BCRYPT_SHA256_ALGORITHM=L"SHA256";constexpr auto BCRYPT_MD5_ALGORITHM=L"MD5";
inline NTSTATUS BCryptOpenAlgorithmProvider(BCRYPT_ALG_HANDLE* out,const wchar_t* name,void*,int){*out=name;return wcscmp(name,L"SHA256")==0?0:-1;}
inline NTSTATUS BCryptCreateHash(BCRYPT_ALG_HANDLE,BCRYPT_HASH_HANDLE* out,void*,int,void*,int,int){*out=new EffSha256;return 0;}
inline NTSTATUS BCryptHashData(BCRYPT_HASH_HANDLE h,PUCHAR p,ULONG n,int){h->Update(p,n);return 0;}
inline NTSTATUS BCryptFinishHash(BCRYPT_HASH_HANDLE h,PUCHAR p,ULONG n,int){if(n!=32)return -1;h->Finish(p);return 0;}
inline void BCryptDestroyHash(BCRYPT_HASH_HANDLE h){delete h;}
inline void BCryptCloseAlgorithmProvider(BCRYPT_ALG_HANDLE,int){}
