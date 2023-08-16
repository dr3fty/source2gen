#pragma once

#pragma warning(push)
#pragma warning(disable:4191)
#include "tools/virtual.h"

#ifdef CSGO2
    #define MEMALLOC_ALLOC_INDEX 1
    #define MEMALLOC_REALLOC_INDEX 2
    #define MEMALLOC_FREE_INDEX 3
    #define MEMALLOC_GETSIZE_INDEX 17
#else
    #define MEMALLOC_ALLOC_INDEX 1
    #define MEMALLOC_REALLOC_INDEX 3
    #define MEMALLOC_FREE_INDEX 5
    #define MEMALLOC_GETSIZE_INDEX 21
#endif

class IMemAlloc {
public:
    void* Alloc(std::size_t size) {
        return Virtual::Get<void*(__thiscall*)(IMemAlloc*, std::size_t)>(this, MEMALLOC_ALLOC_INDEX)(this, size);
    }

    void* ReAlloc(const void* p, std::size_t size) {
        return Virtual::Get<void*(__thiscall*)(IMemAlloc*, const void*, std::size_t)>(this, MEMALLOC_REALLOC_INDEX)(this, p, size);
    }

    void Free(const void* p) {
        return Virtual::Get<void(__thiscall*)(IMemAlloc*, const void*)>(this, MEMALLOC_FREE_INDEX)(this, p);
    }

    std::size_t GetSize(const void* p) {
        return Virtual::Get<std::size_t(__thiscall*)(IMemAlloc*, const void*)>(this, MEMALLOC_GETSIZE_INDEX)(this, p);
    }
};

extern IMemAlloc* GetMemAlloc();

_VCRT_ALLOCATOR void* __CRTDECL operator new(std::size_t s);
_VCRT_ALLOCATOR void* __CRTDECL operator new[](std::size_t s);
void __CRTDECL operator delete(void* p);
void __CRTDECL operator delete(void* p, std::size_t s);
void __CRTDECL operator delete[](void* p);

#pragma warning(pop)
