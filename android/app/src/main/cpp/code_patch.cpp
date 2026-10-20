#include <dobby.h>
#include <sys/mman.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <vector>
// Adapted from Better-Endfield's checked multi-page POSIX code writer.
extern "C" int DobbyCodePatch(void* address,uint8_t* bytes,uint32_t size) {
    static std::mutex mutex;
    std::lock_guard<std::mutex> lock(mutex);
    const auto begin=reinterpret_cast<uintptr_t>(address);const long queried=sysconf(_SC_PAGESIZE);
    if(!address||!bytes||!size||queried<=0||begin>UINTPTR_MAX-size)return -1;
    const size_t page=static_cast<size_t>(queried);const auto first=begin-begin%page,last=(begin+size-1)-(begin+size-1)%page;
    struct Page{uintptr_t address;int protection;};std::vector<Page> pages;
    for(uintptr_t at=first;;at+=page) {
        FILE* maps=std::fopen("/proc/self/maps","r");if(!maps)return -1;
        char line[1024]{};bool found=false;int protection=0;
        while(std::fgets(line,sizeof(line),maps)){unsigned long low=0,high=0;char mode[5]{};
            if(std::sscanf(line,"%lx-%lx %4s",&low,&high,mode)==3&&at>=low&&at<high&&page<=high-at){
                protection=(mode[0]=='r'?PROT_READ:0)|(mode[1]=='w'?PROT_WRITE:0)|(mode[2]=='x'?PROT_EXEC:0);found=(protection&PROT_READ)!=0;break;}}
        std::fclose(maps);if(!found)return -1;pages.push_back({at,protection});if(at==last)break;
    }
    size_t writable=0;
    for(;writable<pages.size();++writable)if(mprotect(reinterpret_cast<void*>(pages[writable].address),page,pages[writable].protection|PROT_WRITE)!=0)break;
    if(writable!=pages.size()){while(writable){--writable;mprotect(reinterpret_cast<void*>(pages[writable].address),page,pages[writable].protection);}return -1;}
    std::memmove(address,bytes,size);__builtin___clear_cache(static_cast<char*>(address),static_cast<char*>(address)+size);
    bool restored=true;for(const auto& p:pages)restored=(mprotect(reinterpret_cast<void*>(p.address),page,p.protection)==0)&&restored;
    return restored?0:-1;
}
