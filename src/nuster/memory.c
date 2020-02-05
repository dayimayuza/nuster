/*
 * nuster memory functions.
 *
 * Copyright (C) Jiang Wenyuan, < koubunen AT gmail DOT com >
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 */

#include <sys/mman.h>

#include <nuster/shctx.h>
#include <nuster/memory.h>

#include <common/standard.h>

struct nst_memory *nst_memory_create(char *name, uint64_t size,
        uint32_t block_size, uint32_t chunk_size) {

    uint8_t *p;
    struct nst_memory *memory;
    uint64_t n;
    uint8_t *begin, *end;
    uint32_t bitmap_size;

    if(block_size < NST_MEMORY_BLOCK_MIN_SIZE) {
        block_size = NST_MEMORY_BLOCK_MIN_SIZE;
    }

    if(block_size > NST_MEMORY_BLOCK_MAX_SIZE) {
        fprintf(stderr, "tune.bufsize exceeds the maximum %d.\n",
                NST_MEMORY_BLOCK_MAX_SIZE);
        return NULL;
    }

    if(chunk_size < NST_MEMORY_CHUNK_MIN_SIZE) {
        chunk_size = NST_MEMORY_CHUNK_MIN_SIZE;
    }

    /* set block_size to minimal number that
     * 1: > block_size
     * 2: = (2**n) * NST_MEMORY_BLOCK_MIN_SIZE
     */
    for(n = NST_MEMORY_BLOCK_MIN_SHIFT; n <= NST_MEMORY_BLOCK_MAX_SHIFT; n++) {
        if(1UL << n >= block_size) {
            block_size = 1UL << n;
            break;
        }
    }

    /*
     * set chunk_size to minimal number that
     * 1, > chunk_size , 2, = n * NST_MEMORY_CHUNK_MIN_SIZE
     */
    chunk_size = ((chunk_size + NST_MEMORY_CHUNK_MIN_SIZE - 1)
            / NST_MEMORY_CHUNK_MIN_SIZE) << NST_MEMORY_CHUNK_MIN_SHIFT;

    if(chunk_size > block_size) {
        fprintf(stderr, "chunk_size cannot be greater than block_size.\n");
        return NULL;
    }

    size = (size + block_size - 1) / block_size * block_size;

    /* create shared memory */
    p = (uint8_t *) mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_ANON|MAP_SHARED,
            -1, 0);

    if(p == MAP_FAILED) {
        fprintf(stderr, "Out of memory when initialization.\n");
        return NULL;
    }

    memory = (struct nst_memory *)p;

    /* init header */
    if(name) {
        strlcpy2(memory->name, name, sizeof(memory->name));
    }

    memory->start      = p;
    memory->stop       = p + size;
    memory->block_size = block_size;
    memory->chunk_size = chunk_size;

    p += sizeof(struct nst_memory);

    /* calculate */
    for(n = NST_MEMORY_CHUNK_MIN_SHIFT; (1ULL << n) < chunk_size; n++) { }

    memory->chunk_shift = n;

    for(n = NST_MEMORY_BLOCK_MIN_SHIFT; (1ULL << n) < block_size; n++) { }

    memory->block_shift = n;
    memory->chunks      = n - memory->chunk_shift + 1;
    memory->chunk       = (struct nst_memory_ctrl **)p;

    p += memory->chunks * sizeof(struct nst_memory_ctrl *);

    memory->block = (struct nst_memory_ctrl *)p;
    memory->empty = NULL;
    memory->full  = NULL;

    bitmap_size = block_size / chunk_size / 8;

    /* set data begin */
    n = (memory->stop - p)
        / (sizeof(struct nst_memory_ctrl) + block_size + bitmap_size);

    begin = (uint8_t *) (((uintptr_t)(p)
                + n * sizeof(struct nst_memory_ctrl) + n * bitmap_size
                + ((uintptr_t) NST_MEMORY_BLOCK_MIN_SIZE - 1))
            & ~((uintptr_t) NST_MEMORY_BLOCK_MIN_SIZE - 1));

    end = begin + block_size * n;

    if(memory->stop < end) {
        n--;
        begin = (uint8_t *) (((uintptr_t)(p)
                    + n * sizeof(struct nst_memory_ctrl)
                    + n * bitmap_size
                    + ((uintptr_t) NST_MEMORY_BLOCK_MIN_SIZE - 1))
                & ~((uintptr_t) NST_MEMORY_BLOCK_MIN_SIZE - 1));
    }

    memory->blocks     = n;
    memory->bitmap     = (uint8_t *)(memory->block + n);
    memory->data.begin = begin;
    memory->data.free  = begin;
    memory->data.end   = begin + block_size * (n - 1);

    n = sizeof(struct nst_memory)
        + sizeof(struct nst_memory_ctrl *) * memory->chunks
        + sizeof(struct nst_memory_ctrl) * n;

    if(memory->blocks == 0 || memory->data.end + block_size > memory->stop) {
        return NULL;
    }

    /* initialize chunk */
    for(n = 0; n < memory->chunks; n++) {
        memory->chunk[n] = NULL;
    }

    /* initialize block */
    for(n = 0; n < memory->blocks; n++) {
        memory->block[n].info   = 0;
        memory->block[n].bitmap = memory->bitmap + n * bitmap_size;
        memory->block[n].prev   = NULL;
        memory->block[n].next   = NULL;
    }

    return memory;
}

void *_nst_memory_block_alloc(struct nst_memory *memory,
        struct nst_memory_ctrl *block, int chunk_idx) {

    int chunk_size = 1<<(memory->chunk_shift + chunk_idx);
    int block_idx  = block - memory->block;

    int bits_need  = memory->block_size / chunk_size;
    int bits_idx   = 0;
    int i          = 0;
    int unset      = 1;
    int full       = 1;

    /* use info, should not use anymore */
    if(chunk_size * NST_MEMORY_INFO_BITMAP_BITS >= memory->block_size) {
        uint32_t mask =  ~0U >> (NST_MEMORY_INFO_BITMAP_BITS - bits_need);
        uint32_t *v   = (uint32_t *)(&block->info) + 1;
        uint32_t t    = *v;

        /* get bits_idx */
        bits_idx      = __builtin_ffs(~t) - 1;
        /* set rightmost 0 to 1 */
        *v           |= *v + 1;
        full          = (mask == *v);
    }
    /* use bitmap */
    else {
        uint64_t *begin;
        begin = (uint64_t *)block->bitmap;

        i     = 0;
        unset = 1;

        for(i = 0; i < bits_need / 64; i++) {
            uint64_t *v = begin + i;

            if(*v == ~0ULL && unset) {
                bits_idx += 64;
                continue;
            }

            if(unset) {
                uint64_t t = *v;
                bits_idx  += __builtin_ffsll(~t) - 1;
                *v        |= *v + 1;
                unset      = 0;
            }

            if(*v != ~0ULL) {
                full = 0;
                break;
            }
        }
    }

    /* yes */
    if(full) {
        _nst_memory_block_set_full(block);
        /* remove from chunk list */
        memory->chunk[chunk_idx] = block->next;

        if(block->next) {
            block->next->prev = NULL;
        }

        /* add to full list */
        if(memory->full) {
            block->next        = memory->full;
            memory->full->prev = block;
        } else {
            block->next = NULL;
        }

        block->prev  = NULL;
        memory->full = block;
    }

    return (void *)(memory->data.begin + 1ULL * memory->block_size * block_idx
            + chunk_size * bits_idx);
}

void _nst_memory_block_init(struct nst_memory * memory,
        struct nst_memory_ctrl *block, int chunk_idx) {

    struct nst_memory_ctrl *chunk;
    chunk       = memory->chunk[chunk_idx];
    block->info = 0;
    _nst_memory_block_set_type(block, chunk_idx);
    _nst_memory_block_set_inited(block);

    memset(block->bitmap, 0, memory->block_size / memory->chunk_size / 8);

    block->prev = NULL;
    block->next = NULL;

    /* add to chunk list */
    if(chunk) {
        block->next = chunk;
        chunk->prev = block;
    }

    memory->chunk[chunk_idx] = block;
}

void *nst_memory_alloc_locked(struct nst_memory *memory, int size) {
    int i, chunk_idx = 0;
    struct nst_memory_ctrl *chunk, *block;

    if(!size || size > memory->block_size) {
        return NULL;
    }

    for(i = (size - 1) >> (memory->chunk_shift - 1); i >>= 1; chunk_idx++) {}

    chunk = memory->chunk[chunk_idx];

    /* check chunk list */
    if(chunk) {
        block = chunk;
    }
    /* check empty list */
    else if(memory->empty) {
        /* remove from empty list */
        block         = memory->empty;
        memory->empty = block->next;

        if(memory->empty) {
            memory->empty->prev = NULL;
        }

        _nst_memory_block_init(memory, block, chunk_idx);
    }
    /* require new block from unused */
    else if(memory->data.free <= memory->data.end) {
        int block_idx      = (memory->data.free - memory->data.begin)
            / memory->block_size;

        memory->data.free += memory->block_size;
        block              = &memory->block[block_idx];

        if(_nst_memory_block_is_inited(block)) {
            return NULL;
        } else {
            _nst_memory_block_init(memory, block, chunk_idx);
        }
    }
    else {
        return NULL;
    }

    return _nst_memory_block_alloc(memory, block, chunk_idx);
}

void *nst_memory_alloc(struct nst_memory *memory, int size) {
    void *p;
    nst_shctx_lock(memory);
    p = nst_memory_alloc_locked(memory, size);
    nst_shctx_unlock(memory);
    return p;
}

void nst_memory_free_locked(struct nst_memory *memory, void *p) {
    int block_idx, chunk_size, bits, bits_idx, empty, full;
    struct nst_memory_ctrl *chunk, *block;
    uint8_t chunk_idx;

    if((uint8_t *)p < memory->data.begin || (uint8_t *)p >= memory->data.free) {
        return;
    }

    block_idx  = ((uint8_t *)p - memory->data.begin) / memory->block_size;
    block      = &memory->block[block_idx];
    chunk_idx  = block->info & 0xFF;
    chunk      = memory->chunk[chunk_idx];
    chunk_size = 1<<(memory->chunk_shift + chunk_idx);
    bits       = memory->block_size / chunk_size;
    bits_idx   = ((uint8_t *)p
            - (memory->data.begin + block_idx * memory->block_size))
        / chunk_size;

    empty      = 0;
    full       = _nst_memory_block_is_full(block);
    _nst_memory_block_clear_full(block);

    /* info used */
    if(chunk_size * NST_MEMORY_INFO_BITMAP_BITS >= memory->block_size) {
        block->info &= ~(1ULL << (bits_idx + 32));

        if(!(block->info & 0xFFFFFFFF00000000ULL)) {
            empty = 1;
        }
    }
    /* bitmap used */
    else {
        int i;
        *((uint64_t *)block->bitmap + bits_idx / 64 ) &=
            ~(1ULL<<(bits_idx % 64));

        for(i = 0; i < bits / 64; i++) {

            if(*((uint64_t *)block->bitmap + i) == 0) {
                empty += 64;
            }
        }

        if(empty == bits) {
            empty = 1;
        } else {
            empty = 0;
        }
    }

    /*
     * 1. if the block previously was full
     *  a. if chunk_id is LAST, move the block from full list to empty list
     *  b. else move the block from full list to chunk[chunk_idx]
     * 2. else if the block became empty after free
     *  a. if chunk_id is LAST, move the block from full list to empty list
     *  b. else move the block from chunk[chunk_idx] to empty list
     * 3. else do nothing
     *
     * 1. if chunk_id is LAST(full && empty)
     *  a. move the block from full list to empty list
     * 2. else
     *  a. if previously full, move the block from full list to chunk[chunk_idx]
     *  b. else if empty after free, move the block
     *     from chunk[chunk_idx] to empty list
     *  c. else do nothing
     */
    /* remove from full list and add to chunk list */
    if(full && empty) {

        /* remove from full list */
        if(block->prev) {
            block->prev->next = block->next;
        } else {
            memory->full = block->next;
        }

        if(block->next) {
            block->next->prev = block->prev;
        }

        /* add to empty list */
        block->prev   = NULL;
        block->next   = memory->empty;
        memory->empty = block;

        if(block->next) {
            block->next->prev = block;
        }
    } else {

        if(full) {

            /* remove from full list */
            if(block->prev) {
                block->prev->next = block->next;
            } else {
                memory->full = block->next;
            }

            if(block->next) {
                block->next->prev = block->prev;
            }

            /* add to chunk list */
            block->prev              = NULL;
            block->next              = chunk;
            memory->chunk[chunk_idx] = block;

            if(block->next) {
                block->next->prev = block;
            }
        } else if(empty) {

            /* remove from chunk list */
            if(block->prev) {
                block->prev->next = block->next;
            } else {
                memory->chunk[chunk_idx] = block->next;
            }

            if(block->next) {
                block->next->prev = block->prev;
            }

            /* add to empty list */
            block->prev   = NULL;
            block->next   = memory->empty;
            memory->empty = block;

            if(block->next) {
                block->next->prev = block;
            }
        }
    }
}

void nst_memory_free(struct nst_memory *memory, void *p) {
    nst_shctx_lock(memory);
    nst_memory_free_locked(memory, p);
    nst_shctx_unlock(memory);
}

