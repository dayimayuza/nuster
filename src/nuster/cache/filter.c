/*
 * nuster cache filter related variables and functions.
 *
 * Copyright (C) Jiang Wenyuan, < koubunen AT gmail DOT com >
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 */

#include <common/cfgparse.h>
#include <common/standard.h>

#include <types/sample.h>

#include <proto/sample.h>
#include <proto/filters.h>
#include <proto/log.h>
#include <proto/stream.h>
#include <proto/proto_http.h>
#include <proto/stream_interface.h>

#include <nuster/memory.h>
#include <nuster/cache.h>
#include <nuster/nuster.h>
#include <nuster/http.h>

static int _nst_cache_filter_init(struct proxy *px, struct flt_conf *fconf) {
    fconf->flags |= FLT_CFG_FL_HTX;
    return 0;
}

static void _nst_cache_filter_deinit(struct proxy *px, struct flt_conf *fconf) {
    struct nst_flt_conf *conf = fconf->conf;

    if(conf) {
        free(conf);
    }

    fconf->conf = NULL;
}

static int _nst_cache_filter_check(struct proxy *px, struct flt_conf *fconf) {

    if(px->mode != PR_MODE_HTTP) {
        ha_warning("Proxy [%s]: mode should be http to enable cache\n", px->id);
    }

    return 0;
}

static int _nst_cache_filter_attach(struct stream *s, struct filter *filter) {
    struct nst_flt_conf *conf = FLT_CONF(filter);

    /* disable cache if state is not NST_STATUS_ON */
    if(global.nuster.cache.status != NST_STATUS_ON
            || conf->status != NST_STATUS_ON) {

        return 0;
    }

    if(!filter->ctx) {
        struct nst_cache_ctx *ctx = pool_alloc(global.nuster.cache.pool.ctx);

        if(ctx == NULL ) {
            return 0;
        }

        memset(ctx, 0, sizeof(*ctx));

        ctx->state = NST_CACHE_CTX_STATE_INIT;
        ctx->pid   = -1;

        filter->ctx = ctx;
    }

    register_data_filter(s, &s->req, filter);
    register_data_filter(s, &s->res, filter);

    return 1;
}

static void _nst_cache_filter_detach(struct stream *s, struct filter *filter) {

    if(filter->ctx) {
        struct nst_rule_stash *stash = NULL;
        struct nst_cache_ctx *ctx    = filter->ctx;

        nst_cache_stats_update_req(ctx->state);

        if(ctx->disk.fd > 0) {
            close(ctx->disk.fd);
        }

        if(ctx->state == NST_CACHE_CTX_STATE_CREATE) {
            nst_cache_abort(ctx);
        }

        while(ctx->stash) {
            stash      = ctx->stash;
            ctx->stash = ctx->stash->next;

            if(stash->key) {
                nst_cache_memory_free(stash->key->area);
                nst_cache_memory_free(stash->key);
            }

            pool_free(global.nuster.cache.pool.stash, stash);
        }

        if(ctx->req.host.data) {
            nst_cache_memory_free(ctx->req.host.data);
        }

        if(ctx->req.path.data) {
            nst_cache_memory_free(ctx->req.path.data);
        }

        pool_free(global.nuster.cache.pool.ctx, ctx);
    }
}

static int _nst_cache_filter_http_headers1(struct stream *s,
        struct filter *filter, struct http_msg *msg) {

    struct channel *req         = msg->chn;
    struct channel *res         = &s->res;
    struct proxy *px            = s->be;
    struct stream_interface *si = &s->si[1];
    struct nst_cache_ctx *ctx   = filter->ctx;
    struct nst_rule *rule       = NULL;

    if(!(msg->chn->flags & CF_ISRESP)) {

        /* check http method */
        if(s->txn->meth == HTTP_METH_OTHER) {
            ctx->state = NST_CACHE_CTX_STATE_BYPASS;
        }

        /* request */
        if(ctx->state == NST_CACHE_CTX_STATE_INIT) {

            if(nst_cache_prebuild_key(ctx, s, msg) != NST_OK) {
                ctx->state = NST_CACHE_CTX_STATE_BYPASS;
                return 1;
            }

            list_for_each_entry(rule, &px->nuster.rules, list) {
                nst_debug("[nuster][cache] Checking rule: %s\n", rule->name);

                /* disabled? */
                if(*rule->state == NST_RULE_DISABLED) {
                    continue;
                }

                /* build key */
                if(nst_cache_build_key(ctx, rule->key, s, msg) != NST_OK) {
                    ctx->state = NST_CACHE_CTX_STATE_BYPASS;
                    return 1;
                }

                nst_debug("[nuster][cache] Key: ");
                nst_debug_key(ctx->key);

                ctx->hash = nst_hash(ctx->key->area, ctx->key->data);

                nst_debug("[nuster][cache] Hash: %"PRIu64"\n", ctx->hash);

                /* stash key */
                if(!nst_cache_stash_rule(ctx, rule)) {
                    ctx->state = NST_CACHE_CTX_STATE_BYPASS;
                    return 1;
                }

                /* check if cache exists  */
                nst_debug("[nuster][cache] Checking key existence: ");

                ctx->state = nst_cache_exists(ctx, rule);

                if(ctx->state == NST_CACHE_CTX_STATE_HIT) {
                    int ret;

                    nst_debug("EXIST\n[nuster][cache] Hit memory\n");
                    /* OK, cache exists */

                    ret = nst_cache_handle_conditional_req(ctx, rule, s, msg);

                    if(ret == 304) {
                        nst_res_304(si, &ctx->res.last_modified,
                                &ctx->res.etag);

                        return 1;
                    }

                    if(ret == 412) {
                        nst_res_412(si);

                        return 1;
                    }

                    break;
                }

                if(ctx->state == NST_CACHE_CTX_STATE_HIT_DISK) {
                    int ret;

                    nst_debug("EXIST\n[nuster][cache] Hit disk\n");
                    /* OK, cache exists */

                    if(rule->etag == NST_STATUS_ON) {
                        ctx->res.etag.len  =
                            nst_persist_meta_get_etag_len(ctx->disk.meta);

                        ctx->res.etag.data =
                            nst_cache_memory_alloc(ctx->res.etag.len);

                        if(!ctx->res.etag.data) {
                            goto abort_check;
                        }

                        if(nst_persist_get_etag(ctx->disk.fd, ctx->disk.meta,
                                    &ctx->res.etag) != NST_OK) {

                            goto abort_check;
                        }
                    }

                    if(rule->last_modified == NST_STATUS_ON) {
                        ctx->res.last_modified.len  =
                            nst_persist_meta_get_last_modified_len(
                                    ctx->disk.meta);

                        ctx->res.last_modified.data =
                            nst_cache_memory_alloc(ctx->res.last_modified.len);

                        if(!ctx->res.last_modified.data) {
                            goto abort_check;
                        }

                        if(nst_persist_get_last_modified(ctx->disk.fd,
                                    ctx->disk.meta, &ctx->res.last_modified)
                                != NST_OK) {

                            goto abort_check;
                        }
                    }

                    ret = nst_cache_handle_conditional_req(ctx, rule, s, msg);

                    if(ret == 304) {
                        nst_res_304(si, &ctx->res.last_modified,
                                &ctx->res.etag);

                        return 1;
                    }

                    if(ret == 412) {
                        nst_res_412(si);

                        return 1;
                    }

abort_check:
                    if(ctx->res.etag.data) {
                        nst_cache_memory_free(ctx->res.etag.data);
                    }

                    if(ctx->res.last_modified.data) {
                        nst_cache_memory_free(ctx->res.last_modified.data);
                    }

                    break;
                }

                nst_debug("NOT EXIST\n");
                /* no, there's no cache yet */

                /* test acls to see if we should cache it */
                nst_debug("[nuster][cache] Checking if rule pass: ");

                if(nst_test_rule(rule, s, msg->chn->flags & CF_ISRESP) ==
                        NST_OK) {

                    nst_debug("PASS\n");
                    ctx->state = NST_CACHE_CTX_STATE_PASS;
                    ctx->rule  = rule;
                    break;
                }

                nst_debug("FAIL\n");
            }
        }

        if(ctx->state == NST_CACHE_CTX_STATE_HIT) {
            nst_cache_hit(s, si, req, res, ctx->data);
        }

        if(ctx->state == NST_CACHE_CTX_STATE_HIT_DISK) {
            nst_cache_hit_disk(s, si, req, res, ctx);
        }

    } else {
        /* response */

        if(ctx->state == NST_CACHE_CTX_STATE_INIT) {

            list_for_each_entry(rule, &px->nuster.rules, list) {
                nst_debug("[nuster][cache] Checking if rule pass: ");

                /* test acls to see if we should cache it */
                if(nst_test_rule(rule, s, msg->chn->flags & CF_ISRESP) ==
                        NST_OK) {

                    nst_debug("PASS\n");
                    ctx->state = NST_CACHE_CTX_STATE_PASS;
                    ctx->rule  = rule;
                    break;
                }

                nst_debug("FAIL\n");
            }
        }

        if(ctx->state == NST_CACHE_CTX_STATE_PASS) {
            struct nst_rule_stash *stash = ctx->stash;
            struct nst_rule_code *cc     = ctx->rule->code;

            int valid = 0;

            ctx->pid = px->uuid;

            /* check if code is valid */
            nst_debug("[nuster][cache] Checking status code: ");

            if(!cc) {
                valid = 1;
            }

            while(cc) {

                if(cc->code == s->txn->status) {
                    valid = 1;
                    break;
                }

                cc = cc->next;
            }

            if(!valid) {
                nst_debug("FAIL\n");
                return 1;
            }

            /* get cache key */
            while(stash) {

                if(ctx->stash->rule == ctx->rule) {
                    ctx->key  = stash->key;
                    ctx->hash = stash->hash;
                    stash->key = NULL;
                    break;
                }

                stash = stash->next;
            }

            if(!ctx->key) {
                return 1;
            }

            nst_cache_build_etag(ctx, s, msg);

            nst_cache_build_last_modified(ctx, s, msg);

            ctx->header_len = msg->sov;
            nst_debug("PASS\n[nuster][cache] To create\n");

            /* start to build cache */
            nst_cache_create(ctx);
        }

    }

    return 1;
}

static int _nst_cache_filter_http_headers2(struct stream *s,
        struct filter *filter, struct http_msg *msg) {

    struct channel *req         = msg->chn;
    struct channel *res         = &s->res;
    struct proxy *px            = s->be;
    struct stream_interface *si = &s->si[1];
    struct nst_cache_ctx *ctx   = filter->ctx;
    struct nst_rule *rule       = NULL;

    if(!(msg->chn->flags & CF_ISRESP)) {

        /* check http method */
        if(s->txn->meth == HTTP_METH_OTHER) {
            ctx->state = NST_CACHE_CTX_STATE_BYPASS;
        }

        /* request */
        if(ctx->state == NST_CACHE_CTX_STATE_INIT) {

            if(nst_cache_prebuild_key2(ctx, s, msg) != NST_OK) {
                ctx->state = NST_CACHE_CTX_STATE_BYPASS;
                return 1;
            }

            list_for_each_entry(rule, &px->nuster.rules, list) {
                nst_debug("[nuster][cache] Checking rule: %s\n", rule->name);

                /* disabled? */
                if(*rule->state == NST_RULE_DISABLED) {
                    continue;
                }

                /* build key */
                if(nst_cache_build_key2(ctx, rule->key, s, msg) != NST_OK) {
                    ctx->state = NST_CACHE_CTX_STATE_BYPASS;
                    return 1;
                }

                nst_debug("[nuster][cache] Key: ");
                nst_debug_key(ctx->key);

                ctx->hash = nst_hash(ctx->key->area, ctx->key->data);

                nst_debug("[nuster][cache] Hash: %"PRIu64"\n", ctx->hash);

                /* stash key */
                if(!nst_cache_stash_rule(ctx, rule)) {
                    ctx->state = NST_CACHE_CTX_STATE_BYPASS;
                    return 1;
                }

                /* check if cache exists  */
                nst_debug("[nuster][cache] Checking key existence: ");

                ctx->state = nst_cache_exists(ctx, rule);

                if(ctx->state == NST_CACHE_CTX_STATE_HIT) {
                    int ret;

                    nst_debug("EXIST\n[nuster][cache] Hit memory\n");
                    /* OK, cache exists */

                    ret = nst_cache_handle_conditional_req(ctx, rule, s, msg);

                    if(ret == 304) {
                        nst_res_304(si, &ctx->res.last_modified,
                                &ctx->res.etag);

                        return 1;
                    }

                    if(ret == 412) {
                        nst_res_412(si);

                        return 1;
                    }

                    break;
                }

                if(ctx->state == NST_CACHE_CTX_STATE_HIT_DISK) {
                    int ret;

                    nst_debug("EXIST\n[nuster][cache] Hit disk\n");
                    /* OK, cache exists */

                    if(rule->etag == NST_STATUS_ON) {
                        ctx->res.etag.len  =
                            nst_persist_meta_get_etag_len(ctx->disk.meta);

                        ctx->res.etag.data =
                            nst_cache_memory_alloc(ctx->res.etag.len);

                        if(!ctx->res.etag.data) {
                            goto abort_check;
                        }

                        if(nst_persist_get_etag(ctx->disk.fd, ctx->disk.meta,
                                    &ctx->res.etag) != NST_OK) {

                            goto abort_check;
                        }
                    }

                    if(rule->last_modified == NST_STATUS_ON) {
                        ctx->res.last_modified.len  =
                            nst_persist_meta_get_last_modified_len(
                                    ctx->disk.meta);

                        ctx->res.last_modified.data =
                            nst_cache_memory_alloc(ctx->res.last_modified.len);

                        if(!ctx->res.last_modified.data) {
                            goto abort_check;
                        }

                        if(nst_persist_get_last_modified(ctx->disk.fd,
                                    ctx->disk.meta, &ctx->res.last_modified)
                                != NST_OK) {

                            goto abort_check;
                        }
                    }

                    ret = nst_cache_handle_conditional_req(ctx, rule, s, msg);

                    if(ret == 304) {
                        nst_res_304(si, &ctx->res.last_modified,
                                &ctx->res.etag);

                        return 1;
                    }

                    if(ret == 412) {
                        nst_res_412(si);

                        return 1;
                    }

abort_check:
                    if(ctx->res.etag.data) {
                        nst_cache_memory_free(ctx->res.etag.data);
                    }

                    if(ctx->res.last_modified.data) {
                        nst_cache_memory_free(ctx->res.last_modified.data);
                    }

                    break;
                }

                nst_debug("NOT EXIST\n");
                /* no, there's no cache yet */

                /* test acls to see if we should cache it */
                nst_debug("[nuster][cache] Checking if rule pass: ");

                if(nst_test_rule(rule, s, msg->chn->flags & CF_ISRESP) ==
                        NST_OK) {

                    nst_debug("PASS\n");
                    ctx->state = NST_CACHE_CTX_STATE_PASS;
                    ctx->rule  = rule;
                    break;
                }

                nst_debug("FAIL\n");
            }
        }

        if(ctx->state == NST_CACHE_CTX_STATE_HIT) {
            nst_cache_hit(s, si, req, res, ctx->data);
        }

        if(ctx->state == NST_CACHE_CTX_STATE_HIT_DISK) {
            nst_cache_hit_disk(s, si, req, res, ctx);
        }

    } else {
        /* response */

        if(ctx->state == NST_CACHE_CTX_STATE_INIT) {

            list_for_each_entry(rule, &px->nuster.rules, list) {
                nst_debug("[nuster][cache] Checking if rule pass: ");

                /* test acls to see if we should cache it */
                if(nst_test_rule(rule, s, msg->chn->flags & CF_ISRESP) ==
                        NST_OK) {

                    nst_debug("PASS\n");
                    ctx->state = NST_CACHE_CTX_STATE_PASS;
                    ctx->rule  = rule;
                    break;
                }

                nst_debug("FAIL\n");
            }
        }

        if(ctx->state == NST_CACHE_CTX_STATE_PASS) {
            struct nst_rule_stash *stash = ctx->stash;
            struct nst_rule_code *cc     = ctx->rule->code;

            int valid = 0;

            ctx->pid = px->uuid;

            /* check if code is valid */
            nst_debug("[nuster][cache] Checking status code: ");

            if(!cc) {
                valid = 1;
            }

            while(cc) {

                if(cc->code == s->txn->status) {
                    valid = 1;
                    break;
                }

                cc = cc->next;
            }

            if(!valid) {
                nst_debug("FAIL\n");
                return 1;
            }

            /* get cache key */
            while(stash) {

                if(ctx->stash->rule == ctx->rule) {
                    ctx->key  = stash->key;
                    ctx->hash = stash->hash;
                    stash->key = NULL;
                    break;
                }

                stash = stash->next;
            }

            if(!ctx->key) {
                return 1;
            }

            nst_cache_build_etag(ctx, s, msg);

            nst_cache_build_last_modified(ctx, s, msg);

            ctx->header_len = msg->sov;
            nst_debug("PASS\n[nuster][cache] To create\n");

            /* start to build cache */
            nst_cache_create2(ctx, msg);
        }
    }

    return 1;
}

static int _nst_cache_filter_http_headers(struct stream *s,
        struct filter *filter, struct http_msg *msg) {
    if (IS_HTX_STRM(s)) {
        return _nst_cache_filter_http_headers2(s, filter, msg);
    } else {
        return _nst_cache_filter_http_headers1(s, filter, msg);
    }
}

static int _nst_cache_filter_http_forward_data(struct stream *s,
        struct filter *filter, struct http_msg *msg, unsigned int len) {

    struct nst_cache_ctx *ctx = filter->ctx;

    int ret = len;

    if(len <= 0) {
        return 0;
    }

    if(ctx->state == NST_CACHE_CTX_STATE_CREATE
            && (msg->chn->flags & CF_ISRESP)) {

        if(ctx->header_len > 0) {
            ret = ctx->header_len;

            ctx->header_len = 0;
        }

        if(nst_cache_update(ctx, msg, ret) != NST_OK) {
            goto err;
        }
    }

    return ret;

err:
    ctx->entry->state = NST_CACHE_ENTRY_STATE_INVALID;
    ctx->entry->data  = NULL;
    ctx->state        = NST_CACHE_CTX_STATE_BYPASS;
    return ret;
}

static int _nst_cache_filter_http_payload(struct stream *s,
        struct filter *filter, struct http_msg *msg, unsigned int offset,
        unsigned int len) {

    struct nst_cache_ctx *ctx = filter->ctx;

    int ret = len;

    if(len <= 0) {
        return 0;
    }

    if(ctx->state == NST_CACHE_CTX_STATE_CREATE
            && (msg->chn->flags & CF_ISRESP)) {

        if(nst_cache_update2(ctx, msg, offset,ret) != NST_OK) {
            goto err;
        }
    }

    return ret;

err:
    ctx->entry->state = NST_CACHE_ENTRY_STATE_INVALID;
    ctx->entry->data  = NULL;
    ctx->state        = NST_CACHE_CTX_STATE_BYPASS;
    return ret;
}

static int _nst_cache_filter_http_end(struct stream *s, struct filter *filter,
        struct http_msg *msg) {

    struct nst_cache_ctx *ctx = filter->ctx;

    if(ctx->state == NST_CACHE_CTX_STATE_CREATE
            && (msg->chn->flags & CF_ISRESP)) {

        nst_cache_finish(ctx);
    }

    return 1;
}

struct flt_ops nst_cache_filter_ops = {
    /* Manage cache filter, called for each filter declaration */
    .init   = _nst_cache_filter_init,
    .deinit = _nst_cache_filter_deinit,
    .check  = _nst_cache_filter_check,

    .attach = _nst_cache_filter_attach,
    .detach = _nst_cache_filter_detach,

    /* Filter HTTP requests and responses */
    .http_headers      = _nst_cache_filter_http_headers,
    .http_payload      = _nst_cache_filter_http_payload,
    .http_forward_data = _nst_cache_filter_http_forward_data,
    .http_end          = _nst_cache_filter_http_end,

};

static int nst_smp_fetch_cache_hit(const struct arg *args, struct sample *smp,
        const char *kw,  void *private) {

    struct nst_cache_ctx *ctx;
    struct filter        *filter;

    list_for_each_entry(filter, &strm_flt(smp->strm)->filters, list) {
        if(FLT_ID(filter) != nst_cache_flt_id) {
            continue;
        }

        if(!(ctx = filter->ctx)) {
            break;
        }

        smp->data.type = SMP_T_BOOL;
        smp->data.u.sint = ctx->state == NST_CACHE_CTX_STATE_HIT
            || ctx->state == NST_CACHE_CTX_STATE_HIT_DISK;;

        return 1;
    }

    return 0;
}

static struct sample_fetch_kw_list nst_sample_fetch_keywords = {
    ILH, {
        { "nuster.cache.hit", nst_smp_fetch_cache_hit, 0, NULL, SMP_T_BOOL,
            SMP_USE_HRSHP
        },
    }
};

INITCALL1(STG_REGISTER, sample_register_fetches, &nst_sample_fetch_keywords);
