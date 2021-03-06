/*
 * nuster cache stats functions.
 *
 * Copyright (C) Jiang Wenyuan, < koubunen AT gmail DOT com >
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 */

#include <inttypes.h>

#include <types/global.h>

#include <proto/stream_interface.h>
#include <proto/proxy.h>

#include <nuster/nuster.h>
#include <nuster/memory.h>
#include <nuster/shctx.h>

void nst_cache_stats_update_used_mem(int i) {
    nst_shctx_lock(global.nuster.cache.stats);
    global.nuster.cache.stats->used_mem += i;
    nst_shctx_unlock(global.nuster.cache.stats);
}

void nst_cache_stats_update_req(int state) {
    nst_shctx_lock(global.nuster.cache.stats);
    global.nuster.cache.stats->req.total++;

    switch(state) {
        case NST_CACHE_CTX_STATE_HIT:
        case NST_CACHE_CTX_STATE_HIT_DISK:
            global.nuster.cache.stats->req.hit++;
            break;
        case NST_CACHE_CTX_STATE_CREATE:
            global.nuster.cache.stats->req.abort++;
            break;
        case NST_CACHE_CTX_STATE_DONE:
            global.nuster.cache.stats->req.fetch++;
            break;
        default:
            break;
    }

    nst_shctx_unlock(global.nuster.cache.stats);
}

int nst_cache_stats_full() {
    int i;

    nst_shctx_lock(global.nuster.cache.stats);
    i =  global.nuster.cache.data_size <= global.nuster.cache.stats->used_mem;
    nst_shctx_unlock(global.nuster.cache.stats);

    return i;
}

/*
 * return 1 if the req is done, otherwise 0
 */
int nst_cache_stats(struct stream *s, struct channel *req, struct proxy *px) {
    struct stream_interface *si = &s->si[1];
    struct http_txn *txn        = s->txn;
    struct http_msg *msg        = &txn->req;
    struct appctx *appctx       = NULL;

    if(global.nuster.cache.status != NST_STATUS_ON) {
        return 0;
    }

    /* GET stats uri */
    if(txn->meth == HTTP_METH_GET && nst_cache_check_uri(msg) == NST_OK) {
        s->target = &nuster.applet.cache_stats.obj_type;

        if(unlikely(!si_register_handler(si, objt_applet(s->target)))) {
            return 1;
        } else {
            appctx      = si_appctx(si);
            appctx->st0 = NST_CACHE_STATS_HEAD;
            appctx->st1 = proxies_list->uuid;
            appctx->st2 = 0;

            req->analysers &=
                (AN_REQ_HTTP_BODY | AN_REQ_FLT_HTTP_HDRS | AN_REQ_FLT_END);

            req->analysers &= ~AN_REQ_FLT_XFER_DATA;
            req->analysers |= AN_REQ_HTTP_XFER_BODY;
        }
    }

    return 0;
}

int _nst_cache_stats_head(struct appctx *appctx, struct stream *s,
        struct stream_interface *si, struct channel *res) {

    chunk_printf(&trash,
            "HTTP/1.1 200 OK\r\n"
            "Cache-Control: no-cache\r\n"
            "Connection: close\r\n"
            "Content-Type: text/plain\r\n"
            "\r\n");

    chunk_appendf(&trash, "**GLOBAL**\n");
    chunk_appendf(&trash, "global.nuster.cache.data.size: %"PRIu64"\n",
            global.nuster.cache.data_size);

    chunk_appendf(&trash, "global.nuster.cache.dict.size: %"PRIu64"\n",
            global.nuster.cache.dict_size);

    chunk_appendf(&trash, "global.nuster.cache.uri: %s\n",
            global.nuster.cache.uri);

    chunk_appendf(&trash, "global.nuster.cache.purge_method: %.*s\n",
            (int)strlen(global.nuster.cache.purge_method) - 1,
            global.nuster.cache.purge_method);

    chunk_appendf(&trash, "global.nuster.cache.stats.used_mem: %"PRIu64"\n",
            global.nuster.cache.stats->used_mem);

    chunk_appendf(&trash, "global.nuster.cache.stats.req_total: %"PRIu64"\n",
            global.nuster.cache.stats->req.total);

    chunk_appendf(&trash, "global.nuster.cache.stats.req_hit: %"PRIu64"\n",
            global.nuster.cache.stats->req.hit);

    chunk_appendf(&trash, "global.nuster.cache.stats.req_fetch: %"PRIu64"\n",
            global.nuster.cache.stats->req.fetch);

    chunk_appendf(&trash, "global.nuster.cache.stats.req_abort: %"PRIu64"\n",
            global.nuster.cache.stats->req.abort);

    chunk_appendf(&trash, "\n**PERSISTENCE**\n");

    if(global.nuster.cache.root) {
        chunk_appendf(&trash, "global.nuster.cache.dir: %s\n",
                global.nuster.cache.root);
        chunk_appendf(&trash, "global.nuster.cache.loaded: %s\n",
            nuster.cache->disk.loaded ? "yes" : "no");
    }

    s->txn->status = 200;

    if(ci_putchk(res, &trash) == -1) {
        si_rx_room_blk(si);

        return 0;
    }

    return 1;
}

int _nst_cache_stats_data(struct appctx *appctx, struct stream *s,
        struct stream_interface *si, struct channel *res) {

    struct proxy *p;

    p = proxies_list;
    while(p) {
        struct nst_rule *rule = NULL;

        if(buffer_almost_full(&res->buf)) {
            si_rx_room_blk(si);
            return 0;
        }

        if(p->uuid != appctx->st1) {
            goto next;
        }

        if(p->cap & PR_CAP_BE && p->nuster.mode == NST_MODE_CACHE) {

            if(!LIST_ISEMPTY(&p->nuster.rules)) {

                list_for_each_entry(rule, &p->nuster.rules, list) {

                    if(buffer_almost_full(&res->buf)) {
                        si_rx_room_blk(si);
                        return 0;
                    }

                    if(rule->uuid == appctx->st2) {

                        if((struct nst_rule *)(&p->nuster.rules)->n == rule) {
                            chunk_printf(&trash, "\n**PROXY %s %d**\n",
                                    p->id, p->uuid);
                            chunk_appendf(&trash, "%s.rule.%s: ",
                                    p->id, rule->name);
                        } else {
                            chunk_printf(&trash, "%s.rule.%s: ",
                                    p->id, rule->name);
                        }

                        chunk_appendf(&trash, "state=%s ttl=%"PRIu32" disk=%s\n",
                                *rule->state == NST_RULE_ENABLED
                                ? "on" : "off", *rule->ttl,
                                rule->disk == NST_DISK_OFF ? "off"
                                : rule->disk == NST_DISK_ONLY ? "only"
                                : rule->disk == NST_DISK_SYNC ? "sync"
                                : rule->disk == NST_DISK_ASYNC ? "async"
                                : "invalid");

                        if(ci_putchk(res, &trash) == -1) {
                            si_rx_room_blk(si);
                            return 0;
                        }

                        appctx->st2++;
                    }
                }
            }

        }

        appctx->st1 = p->next ? p->next->uuid : 0;

next:
        p = p->next;
    }

    return 1;
}

static void nst_cache_stats_handler(struct appctx *appctx) {
    struct stream_interface *si = appctx->owner;
    struct channel *res         = si_ic(si);
    struct stream *s            = si_strm(si);

    if(appctx->st0 == NST_CACHE_STATS_HEAD) {

        if(_nst_cache_stats_head(appctx, s, si, res)) {
            appctx->st0 = NST_CACHE_STATS_DATA;
        }
    }

    if(appctx->st0 == NST_CACHE_STATS_DATA) {

        if(_nst_cache_stats_data(appctx, s, si, res)) {
            appctx->st0 = NST_CACHE_STATS_DONE;
        }
    }

    if(appctx->st0 == NST_CACHE_STATS_DONE) {
        co_skip(si_oc(si), co_data(si_oc(si)));
        si_shutr(si);
        res->flags |= CF_READ_NULL;
    }

}

int nst_cache_stats_init() {
    global.nuster.cache.stats =
        nst_cache_memory_alloc(sizeof(struct nst_cache_stats));

    if(!global.nuster.cache.stats) {
        return NST_ERR;
    }

    if(nst_shctx_init(global.nuster.cache.stats) != NST_OK) {
        return NST_ERR;
    }

    global.nuster.cache.stats->used_mem  = 0;
    global.nuster.cache.stats->req.total = 0;
    global.nuster.cache.stats->req.fetch = 0;
    global.nuster.cache.stats->req.hit   = 0;
    global.nuster.cache.stats->req.abort = 0;
    nuster.applet.cache_stats.fct        = nst_cache_stats_handler;

    return NST_OK;
}

