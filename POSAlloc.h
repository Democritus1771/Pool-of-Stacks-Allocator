#ifndef POS_ALLOC_H_
#define POS_ALLOC_H_
#include <stdio.h>
#include <unistd.h>
#include <sys/mman.h>
#include <stdbool.h>

#define REQ_SIZE(size) (sizeof(header) + (size) + ((size)%16 <= 8 ? 8 - (size)%16 : 24 - (size)%16) + sizeof(footer))
#define MIN_SIZE (33)
#define SET_FREE_CHUNK_BIT (0b0100)
#define MAGIC_MASK (getpagesize() - 1)
#define isChunkFree(header) (((header)->size & SET_FREE_CHUNK_BIT) >> 2)
#define getStackMagic(header) ((size_t)(header)->owner & MAGIC_MASK)
#define getChunkOwner(header) (stackInPool *)((size_t)(header)->owner & ~MAGIC_MASK)
//just to be safe make sure magic is exactly 12 bits
#define setChunkMagic(header, magic) (stackInPool *)((size_t)getChunkOwner((header)) + (magic & (getpagesize() - 1))) 
#define globalMmapMagic (0x123456789ABCDEF0)
#define globalStackMagic (0xABC) // for now its just 12 bits packed into header

typedef struct stackInPool{
    bool stackFullFlag;
    void *bound;
    void *end;
    struct stackInPool *next;
    struct stackInPool *nextInOrder; // this is if we want to free the whole pool
    size_t padding;
}stackInPool __attribute__((aligned(16)));

typedef struct {
    size_t size; // divisible by 8, not 16, so first 3 bits are free. Bit 3 is free bool
    stackInPool *owner; // divisible by pageSize so first pageSize bits are used for magic, prolly 12
}header __attribute__((aligned(16)));


typedef struct {
    size_t size; //unlike the footer this is the total chunk size
}footer;

typedef struct {
    stackInPool *head;
    stackInPool *tail; // only changes when we add a new page
    size_t stackSize;
}memoryPool;

void *allocFromPool(memoryPool *, size_t);
void freeFromPool(memoryPool *, void *);
void initMemoryPool(memoryPool *, size_t);
void freeMemoryPool(memoryPool *);

#define arrPoke(ptr, offset, val) do {\
        if (offset < 0) {                                               \
            fprintf(stderr, "Negative offset not permitted in poke!: %i\n", offset); \
            _exit(1);                                                   \
        }                                                               \
        if ((((size_t)ptr ^ getStackMagic((header *)((size_t)ptr - sizeof(header)))) & MAGIC_MASK) != globalStackMagic) { \
            fprintf(stderr, "Invalid pointer poked!: %p\n", ptr);       \
            _exit(1);                                                   \
        }                                                               \
        if (offset >= ((*(header *)((size_t)ptr - sizeof(header))).size & ~0b111)) { \
            fprintf(stderr, "Out of bounds offset in poke!: %i\n", offset);     \
            _exit(1);                                                   \
        }                                                               \
        ptr[offset] = val;                                              \
}while (0)

#define arrPeek(ptr, offset) ({                                         \
        if (offset < 0) {                                               \
            fprintf(stderr, "Negative offset not permitted in peek!: %i\n", offset); \
            _exit(1);                                                   \
        }                                                               \
        if ((((size_t)ptr ^ getStackMagic((header *)((size_t)ptr - sizeof(header)))) & MAGIC_MASK) != globalStackMagic) { \
            fprintf(stderr, "Invalid pointer peeked!: %p\n", ptr);       \
            _exit(1);                                                   \
        }                                                               \
        if (offset >= ((*(header *)((size_t)ptr - sizeof(header))).size & ~0b111)) { \
            fprintf(stderr, "Out of bounds offset in peek!: %i\n", offset);     \
            _exit(1);                                                   \
        }                                                               \
        ptr[offset];                                                    \
})

#endif
