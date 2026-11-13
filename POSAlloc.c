#include <stdio.h>
#include <unistd.h>
#include <stdbool.h>
#include <sys/mman.h>
#include "POSAlloc.h"

#define chunkSizeGivenPayload(size) (size + sizeof(header) + sizeof(footer)) //this is for padded size

void *callToMmap(size_t size) {
    header *newStack = mmap(NULL, size + sizeof(header), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (newStack == MAP_FAILED) {
        fprintf(stderr, "Mmap failed in callToMmap\n");
        _exit(1);
    }
    newStack->size = size + sizeof(header);
    newStack->owner = (void *)(globalMmapMagic ^ (size_t)(newStack + 1));
    return newStack + 1;
}

void mmapFree(void *ptr) {
    header *curHeader = (header *)ptr - 1;
    if (((size_t)curHeader->owner ^ (size_t)ptr) != globalMmapMagic) {
        fprintf(stderr, "Invalid pointer freed!\n");
        _exit(1);
    }
    int succ = munmap(curHeader, curHeader->size);
    if (succ == -1) {
        fprintf(stderr, "mmapFree found the right magic number but munmap failed, something is very wrong!\n");
        _exit(1);
    }
}

// the function will only be called by our main pool allocator, so it is invariant to assume the stack is not
// full and has enough room
void *stackAlloc(stackInPool *userStack, size_t size) {
    header *newHeader = userStack->bound;
    void *payLoad = newHeader + 1;
    newHeader->size = REQ_SIZE(size) - sizeof(header) - sizeof(footer);
    newHeader->size &= ~SET_FREE_CHUNK_BIT;
    size_t localMagic = globalStackMagic ^ (size_t)payLoad;
    newHeader->owner = userStack;
    newHeader->owner = setChunkMagic(newHeader, localMagic);
    footer *newFooter = (footer *)((size_t)newHeader + REQ_SIZE(size) - sizeof(footer));
    newFooter->size = REQ_SIZE(size);
    userStack->bound = newFooter + 1;
    return payLoad;
}


void freePtrFromStack(memoryPool *userPool, stackInPool *userStack, void *ptr) {
    header *curHeader = (header *)((char *)ptr - sizeof(header));
    curHeader->size |= SET_FREE_CHUNK_BIT;
    if ((size_t)userStack->bound != (size_t)curHeader + chunkSizeGivenPayload(curHeader->size & ~0b111)) {
        return;
    }
    userStack->stackFullFlag = false; // since now we know we are deallocating some chunks it will be free
    footer *prevFooter;
    while (isChunkFree(curHeader)) { // deallocate all top blocks marked for deallocation
        userStack->bound = curHeader;
        if (curHeader == (void *)(userStack + 1)) {
            return;
        }
        prevFooter = (footer *)((size_t)curHeader - sizeof(footer));
        curHeader = (header *)((size_t)curHeader - prevFooter->size);
    }
}

void addNewStack(memoryPool *userPool) {
    stackInPool *newStack = mmap(NULL, userPool->stackSize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (newStack == MAP_FAILED) {
        fprintf(stderr, "mmap failed!\n");
        _exit(1);
    }
    if (!userPool->head) userPool->head = newStack; else userPool->tail->next = newStack;
    if (userPool->tail) userPool->tail->nextInOrder = newStack; // to free whole pool later
    userPool->tail = newStack;
    newStack->next = NULL;
    newStack->end = (char *)newStack + userPool->stackSize;
    newStack->bound = newStack + 1;
}

// all allocations are 16 byte aligned
void *allocFromPool(memoryPool *userPool, size_t size) {
    if (REQ_SIZE(size) < MIN_SIZE) {
        printf("Allocations requiring less than %i bytes not permitted!\n", MIN_SIZE);
        return NULL;
    }
    if (userPool->stackSize - sizeof(stackInPool) < REQ_SIZE(size)) {
        return callToMmap(size);
    }
    stackInPool *oldTail = userPool->tail;
    stackInPool *curStack = userPool->head;
    stackInPool *prevStack = userPool->head; // this is for moving pointers later
    if (!userPool->head) {
        addNewStack(userPool);
        curStack = userPool->tail;
    }
    else {
        while (curStack && curStack->end - curStack->bound < REQ_SIZE(size)) {
            prevStack = curStack;
            curStack = curStack->next;
        }
        // pool is still usable but all stacks are too packed
        if (!curStack) {
            addNewStack(userPool);
            // we know our pool was not full so we need the old tail
            // tail can never be full unless the whole pool is full, and we know it isnt
            prevStack = oldTail;
            curStack = oldTail->next;
        }
    }
    void *res = stackAlloc(curStack, size);
    if (curStack->stackFullFlag) {
        if (curStack == userPool->head) userPool->head = curStack->next; else prevStack->next = curStack->next;
        curStack->next = NULL; // a full stack cannot point to another stack
    }
    return res;
}


void freeFromPool(memoryPool *userPool, void *ptr) {
    header *curHeader = (header *)ptr - 1;
    header *isMmap = curHeader;
    if (((getStackMagic(curHeader) ^ (size_t)ptr) & MAGIC_MASK) != globalStackMagic && ((size_t)isMmap->owner ^ (size_t)ptr) != globalMmapMagic) {
        fprintf(stderr, "Freed Pointer definitely wrong!\n");
        _exit(1);
    }
    else if (((size_t)isMmap->owner ^ (size_t)ptr) == globalMmapMagic) {
        mmapFree(ptr);
        return;
    }
    stackInPool *curStack = getChunkOwner(curHeader);
    bool wasFull = curStack->stackFullFlag;
    freePtrFromStack(userPool, curStack, ptr);
    if (wasFull) {
        curStack->next = userPool->head;
        userPool->head = curStack;
    }
}

void initMemoryPool(memoryPool *userPool, size_t numPages) {
    userPool->stackSize = getpagesize()*numPages;
    userPool->head = NULL;
    userPool->tail = NULL;
    addNewStack(userPool);
}

void freeMemoryPool(memoryPool *userPool) {
    stackInPool *curHeader = userPool->head;
    stackInPool *tmpHeader;
    while (curHeader) {
        tmpHeader = curHeader->nextInOrder;
        int succ = munmap(curHeader, userPool->stackSize);
        if (succ == -1) {
            fprintf(stderr, "munmap could not free stack %p, something is very wrong!\n", curHeader);
            _exit(1);
        }
        curHeader = tmpHeader;
    }
}
