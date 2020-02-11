/*
 * include/nuster/nosql.h
 * This file defines everything related to nuster nosql.
 *
 * Copyright (C) Jiang Wenyuan, < koubunen AT gmail DOT com >
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation, version 2.1
 * exclusively.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 */

#ifndef _NUSTER_NOSQL_H
#define _NUSTER_NOSQL_H

#include <nuster/common.h>

#define NST_NOSQL_DEFAULT_CHUNK_SIZE            32
#define NST_NOSQL_DEFAULT_LOAD_FACTOR           0.75
#define NST_NOSQL_DEFAULT_GROWTH_FACTOR         2
#define NST_NOSQL_DEFAULT_KEY_SIZE              128


enum {
    NST_NOSQL_APPCTX_STATE_INIT = 0,
    NST_NOSQL_APPCTX_STATE_WAIT,
    NST_NOSQL_APPCTX_STATE_HIT,
    NST_NOSQL_APPCTX_STATE_CREATE,
    NST_NOSQL_APPCTX_STATE_DELETED,
    NST_NOSQL_APPCTX_STATE_END,
    NST_NOSQL_APPCTX_STATE_DONE,
    NST_NOSQL_APPCTX_STATE_ERROR,
    NST_NOSQL_APPCTX_STATE_NOT_ALLOWED,
    NST_NOSQL_APPCTX_STATE_NOT_FOUND,
    NST_NOSQL_APPCTX_STATE_EMPTY,
    NST_NOSQL_APPCTX_STATE_FULL,
    NST_NOSQL_APPCTX_STATE_HIT_DISK,
};

struct nst_nosql_element {
    struct nst_nosql_element *next;
    struct nst_str            msg;
};

/*
 * A nst_nosql_data contains a complete http response data,
 * and is pointed by nst_nosql_entry->data.
 * All nst_nosql_data are stored in a circular singly linked list
 */
#define NST_NOSQL_DATA_FLAG_CHUNKED    0x00000001

struct nst_nosql_data {
    int                       clients;
    int                       invalid;
    struct nst_nosql_element *element;
    struct nst_nosql_data    *next;

    struct {
        struct nst_str        content_type;
        struct nst_str        transfer_encoding;
        uint64_t              content_length;
        unsigned int          flags;
    } info;
};

/*
 * A nst_nosql_entry is an entry in nst_nosql_dict hash table
 */
enum {
    NST_NOSQL_ENTRY_STATE_CREATING = 0,
    NST_NOSQL_ENTRY_STATE_VALID,
    NST_NOSQL_ENTRY_STATE_INVALID,
    NST_NOSQL_ENTRY_STATE_EXPIRED,
};

struct nst_nosql_entry {
    int                     state;
    struct buffer          *key;
    uint64_t                hash;
    struct nst_nosql_data  *data;
    uint64_t                expire;
    uint64_t                atime;
    struct nst_str          host;
    struct nst_str          path;
    struct nst_nosql_entry *next;
    struct nst_rule        *rule;        /* rule */
    int                     pid;         /* proxy uuid */
    char                   *file;
    int                     header_len;
};

struct nst_nosql_dict {
    struct nst_nosql_entry **entry;
    uint64_t                 size;      /* number of entries */
    uint64_t                 used;      /* number of used entries */

#if defined NUSTER_USE_PTHREAD || defined USE_PTHREAD_PSHARED
    pthread_mutex_t          mutex;
#else
    unsigned int             waiters;
#endif

};

enum {
    NST_NOSQL_CTX_STATE_INIT = 0,   /* init */
    NST_NOSQL_CTX_STATE_HIT,        /* key exists */
    NST_NOSQL_CTX_STATE_CREATE,     /* to cache */
    NST_NOSQL_CTX_STATE_DELETE,     /* to delete */
    NST_NOSQL_CTX_STATE_DONE,       /* set done */
    NST_NOSQL_CTX_STATE_INVALID,    /* invalid */
    NST_NOSQL_CTX_STATE_FULL,       /* nosql full */
    NST_NOSQL_CTX_STATE_WAIT,       /* wait */
    NST_NOSQL_CTX_STATE_PASS,       /* rule passed */
    NST_NOSQL_CTX_STATE_HIT_DISK,
    NST_NOSQL_CTX_STATE_CHECK_PERSIST,
};

struct nst_nosql_ctx {
    int                       state;

    struct nst_rule          *rule;
    struct buffer            *key;
    uint64_t                  hash;

    struct nst_nosql_entry   *entry;
    struct nst_nosql_data    *data;
    struct nst_nosql_element *element;

    struct {
        int                   scheme;
        struct nst_str        host;
        struct nst_str        uri;
        struct nst_str        path;
        int                   delimiter;
        struct nst_str        query;
        struct nst_str        cookie;
        struct nst_str        content_type;
        struct nst_str        transfer_encoding;
    } req;

    int                       pid;         /* proxy uuid */

    int                       header_len;
    uint64_t                  cache_len;
    uint64_t                  cache_len2;

    struct persist            disk;
};

struct nst_nosql_stats {
    uint64_t        used_mem;

#if defined NUSTER_USE_PTHREAD || defined USE_PTHREAD_PSHARED
    pthread_mutex_t mutex;
#else
    unsigned int    waiters;
#endif

};

struct nst_nosql {
    /* 0: using, 1: rehashing */
    struct nst_nosql_dict  dict[2];

    /* point to the circular linked list, tail->next ===  head */
    struct nst_nosql_data *data_head;

    /* and will be moved together constantly to check invalid data */
    struct nst_nosql_data *data_tail;

#if defined NUSTER_USE_PTHREAD || defined USE_PTHREAD_PSHARED
    pthread_mutex_t        mutex;
#else
    unsigned int           waiters;
#endif

    /* >=0: rehashing, index, -1: not rehashing */
    int                    rehash_idx;

    /* cache dict cleanup index */
    int                    cleanup_idx;

    int                    persist_idx;

    /* for disk_loader and disk_cleaner */
    struct {
        int                loaded;
        int                idx;
        DIR               *dir;
        struct dirent     *de;
        char              *file;
    } disk;
};

extern struct flt_ops  nst_nosql_filter_ops;

/* engine */
void nst_nosql_init();
void nst_nosql_housekeeping();
int nst_nosql_check_applet(struct stream *s, struct channel *req,
        struct proxy *px);

int nst_nosql_check_applet2(struct stream *s, struct channel *req,
        struct proxy *px);

struct nst_nosql_data *nst_nosql_data_new();
int nst_nosql_prebuild_key(struct nst_nosql_ctx *ctx, struct stream *s,
        struct http_msg *msg);

int nst_nosql_prebuild_key2(struct nst_nosql_ctx *ctx, struct stream *s,
        struct http_msg *msg);

int nst_nosql_build_key(struct nst_nosql_ctx *ctx, struct nst_rule_key **pck,
        struct stream *s, struct http_msg *msg);

int nst_nosql_build_key2(struct nst_nosql_ctx *ctx, struct nst_rule_key **pck,
        struct stream *s, struct http_msg *msg);

uint64_t nst_nosql_hash_key(const char *key);
int nst_nosql_exists(struct nst_nosql_ctx *ctx, int mode);
int nst_nosql_delete(struct buffer *key, uint64_t hash);
void nst_nosql_create(struct nst_nosql_ctx *ctx, struct stream *s,
        struct http_msg *msg);

void nst_nosql_create2(struct nst_nosql_ctx *ctx, struct stream *s,
        struct http_msg *msg);

int nst_nosql_update(struct nst_nosql_ctx *ctx, struct http_msg *msg,
        long msg_len);

int nst_nosql_update2(struct nst_nosql_ctx *ctx, struct http_msg *msg,
        unsigned int offset, unsigned int msg_len);

void nst_nosql_finish(struct nst_nosql_ctx *ctx, struct http_msg *msg);
void nst_nosql_finish2(struct nst_nosql_ctx *ctx, struct stream *s,
        struct http_msg *msg);

void nst_nosql_abort(struct nst_nosql_ctx *ctx);
int nst_nosql_get_headers(struct nst_nosql_ctx *ctx, struct stream *s,
        struct http_msg *msg);

int nst_nosql_get_headers2(struct nst_nosql_ctx *ctx, struct stream *s,
        struct http_msg *msg);

void nst_nosql_persist_async();
void nst_nosql_persist_async2();
void nst_nosql_persist_cleanup();
void nst_nosql_persist_load();

/* dict */
int nst_nosql_dict_init();
struct nst_nosql_entry *nst_nosql_dict_get(struct buffer *key, uint64_t hash);
struct nst_nosql_entry *nst_nosql_dict_set(struct nst_nosql_ctx *ctx);
int nst_nosql_dict_set_from_disk(char *file, char *meta, struct buffer *key);
void nst_nosql_dict_rehash();
void nst_nosql_dict_cleanup();

/* stats */
void nst_nosql_stats_update_used_mem(int i);
int nst_nosql_stats_init();
int nst_nosql_stats_full();

static inline int nst_nosql_dict_entry_expired(struct nst_nosql_entry *entry) {

    if(entry->expire == 0) {
        return 0;
    } else {
        return entry->expire <= get_current_timestamp() / 1000;
    }

}

static inline int nst_nosql_entry_invalid(struct nst_nosql_entry *entry) {

    /* check state */
    if(entry->state == NST_NOSQL_ENTRY_STATE_INVALID) {
        return 1;
    } else if(entry->state == NST_NOSQL_ENTRY_STATE_EXPIRED) {
        return 1;
    }

    /* check expire */
    return nst_nosql_dict_entry_expired(entry);
}

#define nst_nosql_key_init() nst_key_init(global.nuster.nosql.memory)
#define nst_nosql_key_advance(key, step)                                      \
    nst_key_advance(global.nuster.nosql.memory, key, step)
#define nst_nosql_key_append(key, str, len)                                   \
    nst_key_append(global.nuster.nosql.memory, key, str, len)
#define nst_nosql_memory_alloc(size)                                          \
    nst_memory_alloc(global.nuster.nosql.memory, size)
#define nst_nosql_memory_free(p) nst_memory_free(global.nuster.nosql.memory, p);

#endif /* _NUSTER_NOSQL_H */
