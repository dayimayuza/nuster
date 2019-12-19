/*
 * nuster parser related variables and functions.
 *
 * Copyright (C) Jiang Wenyuan, < koubunen AT gmail DOT com >
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 */

#include <common/standard.h>
#include <common/cfgparse.h>
#include <common/errors.h>

#include <types/global.h>

#include <proto/acl.h>
#include <proto/log.h>

#include <nuster/nuster.h>

const char *nst_cache_flt_id = "cache filter id";
static const char *nst_nosql_flt_id = "nosql filter id";

static struct nst_rule_key *_nst_parse_rule_key_cast(char *str) {
    struct nst_rule_key *key = NULL;

    if(!strcmp(str, "method")) {
        key       = malloc(sizeof(*key));
        key->type = NST_RULE_KEY_METHOD;
        key->data = NULL;
    } else if(!strcmp(str, "scheme")) {
        key       = malloc(sizeof(*key));
        key->type = NST_RULE_KEY_SCHEME;
        key->data = NULL;
    } else if(!strcmp(str, "host")) {
        key       = malloc(sizeof(*key));
        key->type = NST_RULE_KEY_HOST;
        key->data = NULL;
    } else if(!strcmp(str, "uri")) {
        key       = malloc(sizeof(*key));
        key->type = NST_RULE_KEY_URI;
        key->data = NULL;
    } else if(!strcmp(str, "path")) {
        key       = malloc(sizeof(*key));
        key->type = NST_RULE_KEY_PATH;
        key->data = NULL;
    } else if(!strcmp(str, "delimiter")) {
        key       = malloc(sizeof(*key));
        key->type = NST_RULE_KEY_DELIMITER;
        key->data = NULL;
    } else if(!strcmp(str, "query")) {
        key       = malloc(sizeof(*key));
        key->type = NST_RULE_KEY_QUERY;
        key->data = NULL;
    } else if(!strncmp(str, "param_", 6) && strlen(str) > 6) {
        key       = malloc(sizeof(*key));
        key->type = NST_RULE_KEY_PARAM;
        key->data = strdup(str + 6);
    } else if(!strncmp(str, "header_", 7) && strlen(str) > 7) {
        key       = malloc(sizeof(*key));
        key->type = NST_RULE_KEY_HEADER;
        key->data = strdup(str + 7);
    } else if(!strncmp(str, "cookie_", 7) && strlen(str) > 7) {
        key       = malloc(sizeof(*key));
        key->type = NST_RULE_KEY_COOKIE;
        key->data = strdup(str + 7);
    } else if(!strcmp(str, "body")) {
        key       = malloc(sizeof(*key));
        key->type = NST_RULE_KEY_BODY;
        key->data = NULL;
    }

    return key;
}

static struct nst_rule_key **_nst_parse_rule_key(char *str) {
    struct nst_rule_key **pk = NULL;
    char *m, *tmp = strdup(str);
    int i = 0;

    m = strtok(tmp, ".");

    while(m) {
        struct nst_rule_key *key = _nst_parse_rule_key_cast(m);

        if(!key) {
            goto err;
        }

        pk = realloc(pk, (i + 1) * sizeof(struct nst_rule_key *));
        pk[i++] = key;
        m = strtok(NULL, ".");
    }

    if(!pk) {
        goto err;
    }

    pk = realloc(pk, (i + 1) * sizeof(struct nst_rule_key *));
    pk[i] = NULL;
    free(tmp);

    return pk;

err:
    if(pk) {

        while(i--) {
            free(pk[i]);
        }

        free(pk);
    }

    free(tmp);

    return NULL;
}

static struct nst_rule_code *_nst_parse_rule_code(char *str) {

    if(!strcmp(str, "all")) {
        return NULL;
    } else {
        struct nst_rule_code *code = NULL;
        char *tmp = strdup(str);
        char *m = strtok(tmp, ",");

        /* warn ","? */
        while(m) {
            int i = atoi(m);
            struct nst_rule_code *cc = malloc(sizeof(*cc));
            cc->code = i;

            if(code) {
                cc->next = code;
            } else {
                cc->next = NULL;
            }

            code = cc;
            m = strtok(NULL, ",");
        }

        free(tmp);

        return code;
    }
}


/*
 * Parse size
 */
const char *nst_parse_size(const char *text, uint64_t *ret) {
    uint64_t value = 0;

    while(1) {
        unsigned int i;
        i = *text - '0';

        if(i > 9) {
            break;
        }

        if(value > ~0ULL / 10) {
            goto end;
        }

        value *= 10;

        if(value > (value + i)) {
            goto end;
        }

        value += i;
        text++;
    }

    switch(*text) {
        case '\0':
            break;
        case 'M':
        case 'm':

            if(value > ~0ULL >> 20) {
                goto end;
            }

            value = value << 20;
            break;
        case 'G':
        case 'g':

            if(value > ~0ULL >> 30) {
                goto end;
            }

            value = value << 30;
            break;
        default:
            return text;
    }

    if(*text != '\0' && *++text != '\0') {
        return text;
    }

    if(value < NST_DEFAULT_SIZE) {
        value = NST_DEFAULT_SIZE;
    }

    *ret = value;

    return NULL;

end:
    *ret = NST_DEFAULT_SIZE;

    return NULL;
}

/*
 * Parse time
 */
const char *nst_parse_time(const char *text, int len, unsigned *ret) {
    unsigned imult, idiv, omult, odiv;
    unsigned value;

    if(*text - '0' > 9) {
        return text;
    }

    omult = odiv = imult = idiv = 1;
    value = 0;

    while(len--) {
        unsigned int j;

        j = *text - '0';

        if(j > 9) {
            switch(*text) {
                case 's':
                    break;
                case 'm':
                    imult = 60;
                    break;
                case 'h':
                    imult = 3600;
                    break;
                case 'd':
                    imult = 86400;
                    break;
                default:
                    return text;
                    break;
            }
            break;
        }

        text++;
        value *= 10;
        value += j;
    }

    if(len > 0) {
        return text;
    }

    if(omult % idiv == 0) {
        omult /= idiv;
        idiv   = 1;
    }

    if(idiv % omult == 0) {
        idiv  /= omult;
        omult  = 1;
    }

    if(imult % odiv == 0) {
        imult /= odiv;
        odiv   = 1;
    }

    if(odiv % imult == 0) {
        odiv  /= imult;
        imult  = 1;
    }

    value = (value * (imult * omult) + (idiv * odiv - 1)) / (idiv * odiv);
    *ret = value;

    return NULL;
}

int nuster_parse_global_cache(const char *file, int linenum, char **args) {

    int err_code = 0;
    int cur_arg  = 1;

    if(global.nuster.cache.status != NST_STATUS_UNDEFINED) {
        ha_alert("parsing [%s:%d]: '%s' already specified. Ignore.\n", file,
                linenum, args[0]);

        err_code |= ERR_ALERT;
        goto out;
    }

    if(*(args[cur_arg]) == 0) {
        ha_alert("parsing [%s:%d]: '%s' expects 'on' or 'off' as argument.\n",
                file, linenum, args[0]);

        err_code |= ERR_ALERT | ERR_FATAL;
        goto out;
    }

    if(!strcmp(args[cur_arg], "off")) {
        global.nuster.cache.status = NST_STATUS_OFF;
    } else if(!strcmp(args[cur_arg], "on")) {
        global.nuster.cache.status = NST_STATUS_ON;
    } else {
        ha_alert("parsing [%s:%d]: '%s' only supports 'on' and 'off'.\n",
                file, linenum, args[0]);

        err_code |= ERR_ALERT | ERR_FATAL;
        goto out;
    }

    global.nuster.cache.purge_method =
        calloc(NST_CACHE_DEFAULT_PURGE_METHOD_SIZE, sizeof(char));

    if(!global.nuster.cache.purge_method) {
        ha_alert("parsing [%s:%d]: '%s' : out of memory.\n", file, linenum,
                args[0]);

        err_code = ERR_ALERT | ERR_FATAL;
        goto out;
    }

    memcpy(global.nuster.cache.purge_method, NST_CACHE_DEFAULT_PURGE_METHOD, 5);
    memcpy(global.nuster.cache.purge_method + 5, " ", 1);
    cur_arg++;
    global.nuster.cache.uri = NULL;

    while(*(args[cur_arg]) !=0) {
        if(!strcmp(args[cur_arg], "data-size")) {
            cur_arg++;

            if(*args[cur_arg] == 0) {
                ha_alert("parsing [%s:%d]: '%s' data-size expects a size.\n",
                        file, linenum, args[0]);

                err_code |= ERR_ALERT | ERR_FATAL;
                goto out;
            }
            if(nst_parse_size(args[cur_arg],
                        &global.nuster.cache.data_size)) {

                ha_alert("parsing [%s:%d]: '%s' invalid data_size, expects "
                        "[m|M|g|G].\n", file, linenum, args[0]);

                err_code |= ERR_ALERT | ERR_FATAL;
                goto out;
            }

            cur_arg++;
            continue;
        }

        if(!strcmp(args[cur_arg], "dict-size")) {
            cur_arg++;

            if(*args[cur_arg] == 0) {
                ha_alert("parsing [%s:%d]: '%s' dict-size expects a size.\n",
                        file, linenum, args[0]);

                err_code |= ERR_ALERT | ERR_FATAL;
                goto out;
            }

            if(nst_parse_size(args[cur_arg],
                        &global.nuster.cache.dict_size)) {

                ha_alert("parsing [%s:%d]: '%s' invalid dict-size, expects "
                        "[m|M|g|G].\n", file, linenum, args[0]);

                err_code |= ERR_ALERT | ERR_FATAL;
                goto out;
            }

            cur_arg++;
            continue;
        }

        if(!strcmp(args[cur_arg], "purge-method")) {
            cur_arg++;

            if(*args[cur_arg] == 0) {
                ha_alert("parsing [%s:%d]: '%s' purge-method expects a name."
                        "\n", file, linenum, args[0]);

                err_code |= ERR_ALERT | ERR_FATAL;
                goto out;
            }

            memset(global.nuster.cache.purge_method, 0,
                    NST_CACHE_DEFAULT_PURGE_METHOD_SIZE);

            if(strlen(args[cur_arg])
                    <= NST_CACHE_DEFAULT_PURGE_METHOD_SIZE - 2) {

                memcpy(global.nuster.cache.purge_method, args[cur_arg],
                        strlen(args[cur_arg]));

                memcpy(global.nuster.cache.purge_method + strlen(args[cur_arg]),
                        " ", 1);

            } else {
                memcpy(global.nuster.cache.purge_method, args[cur_arg],
                        NST_CACHE_DEFAULT_PURGE_METHOD_SIZE - 2);

                memcpy(global.nuster.cache.purge_method
                        + NST_CACHE_DEFAULT_PURGE_METHOD_SIZE - 2, " ", 1);

            }

            cur_arg++;
            continue;
        }

        if(!strcmp(args[cur_arg], "uri")) {
            cur_arg++;

            if(*(args[cur_arg]) == 0) {
                ha_alert("parsing [%s:%d]: '%s': `uri` expect an URI.\n",
                        file, linenum, args[0]);

                err_code |= ERR_ALERT | ERR_FATAL;
                goto out;
            }

            global.nuster.cache.uri = strdup(args[cur_arg]);
            cur_arg++;
            continue;
        }

        if(!strcmp(args[cur_arg], "dir")) {
            cur_arg++;

            if(*(args[cur_arg]) == 0) {
                ha_alert("parsing [%s:%d]: '%s': `dir` expects a root as "
                        "an argument.\n", file, linenum, args[0]);

                err_code |= ERR_ALERT | ERR_FATAL;
                goto out;
            }

            global.nuster.cache.root = strdup(args[cur_arg]);
            cur_arg++;
            continue;
        }

        if(!strcmp(args[cur_arg], "dict-cleaner")) {
            cur_arg++;

            if(*args[cur_arg] == 0) {
                ha_alert("parsing [%s:%d]: '%s' dict-cleaner expects a number."
                        "\n", file, linenum, args[0]);

                err_code |= ERR_ALERT | ERR_FATAL;
                goto out;
            }

            global.nuster.cache.dict_cleaner = atoi(args[cur_arg]);

            if(global.nuster.cache.dict_cleaner <= 0) {
                global.nuster.cache.dict_cleaner = NST_DEFAULT_DICT_CLEANER;
            }

            cur_arg++;
            continue;
        }

        if(!strcmp(args[cur_arg], "data-cleaner")) {
            cur_arg++;

            if(*args[cur_arg] == 0) {
                ha_alert("parsing [%s:%d]: '%s' data-cleaner expects a number."
                        "\n", file, linenum, args[0]);

                err_code |= ERR_ALERT | ERR_FATAL;
                goto out;
            }

            global.nuster.cache.data_cleaner = atoi(args[cur_arg]);

            if(global.nuster.cache.data_cleaner <= 0) {
                global.nuster.cache.data_cleaner = NST_DEFAULT_DATA_CLEANER;
            }

            cur_arg++;
            continue;
        }

        if(!strcmp(args[cur_arg], "disk-cleaner")) {
            cur_arg++;

            if(*args[cur_arg] == 0) {
                ha_alert("parsing [%s:%d]: '%s' disk-cleaner expects a number."
                        "\n", file, linenum, args[0]);

                err_code |= ERR_ALERT | ERR_FATAL;
                goto out;
            }

            global.nuster.cache.disk_cleaner = atoi(args[cur_arg]);

            if(global.nuster.cache.disk_cleaner <= 0) {
                global.nuster.cache.disk_cleaner = NST_DEFAULT_DISK_CLEANER;
            }

            cur_arg++;
            continue;
        }

        if(!strcmp(args[cur_arg], "disk-loader")) {
            cur_arg++;

            if(*args[cur_arg] == 0) {
                ha_alert("parsing [%s:%d]: '%s' disk-loader expects a number."
                        "\n", file, linenum, args[0]);

                err_code |= ERR_ALERT | ERR_FATAL;
                goto out;
            }

            global.nuster.cache.disk_loader = atoi(args[cur_arg]);

            if(global.nuster.cache.disk_loader <= 0) {
                global.nuster.cache.disk_loader = NST_DEFAULT_DISK_LOADER;
            }

            cur_arg++;
            continue;
        }

        if(!strcmp(args[cur_arg], "disk-saver")) {
            cur_arg++;

            if(*args[cur_arg] == 0) {
                ha_alert("parsing [%s:%d]: '%s' disk-saver expects a number."
                        "\n", file, linenum, args[0]);

                err_code |= ERR_ALERT | ERR_FATAL;
                goto out;
            }

            global.nuster.cache.disk_saver = atoi(args[cur_arg]);

            if(global.nuster.cache.disk_saver <= 0) {
                global.nuster.cache.disk_saver = NST_DEFAULT_DISK_SAVER;
            }

            cur_arg++;
            continue;
        }

        ha_alert("parsing [%s:%d]: '%s' Unrecognized .\n", file, linenum,
                args[cur_arg]);

        err_code |= ERR_ALERT | ERR_FATAL;
        goto out;
    }

out:
    return err_code;
}

int nuster_parse_global_nosql(const char *file, int linenum, char **args) {

    int err_code = 0;
    int cur_arg  = 1;

    if(global.nuster.nosql.status != NST_STATUS_UNDEFINED) {
        ha_alert("parsing [%s:%d]: '%s' already specified. Ignore.\n", file,
                linenum, args[0]);

        err_code |= ERR_ALERT;
        goto out;
    }

    if(*(args[cur_arg]) == 0) {
        ha_alert("parsing [%s:%d]: '%s' expects 'on' or 'off' as argument.\n",
                file, linenum, args[0]);

        err_code |= ERR_ALERT | ERR_FATAL;
        goto out;
    }

    if(!strcmp(args[cur_arg], "off")) {
        global.nuster.nosql.status = NST_STATUS_OFF;
    } else if(!strcmp(args[cur_arg], "on")) {
        global.nuster.nosql.status = NST_STATUS_ON;
    } else {
        ha_alert("parsing [%s:%d]: '%s' only supports 'on' and 'off'.\n",
                file, linenum, args[0]);

        err_code |= ERR_ALERT | ERR_FATAL;
        goto out;
    }

    cur_arg++;

    while(*(args[cur_arg]) !=0) {

        if(!strcmp(args[cur_arg], "dict-size")) {
            cur_arg++;

            if(*args[cur_arg] == 0) {
                ha_alert("parsing [%s:%d]: '%s' dict-size expects a size.\n",
                        file, linenum, args[0]);

                err_code |= ERR_ALERT | ERR_FATAL;
                goto out;
            }

            if(nst_parse_size(args[cur_arg], &global.nuster.nosql.dict_size)) {

                ha_alert("parsing [%s:%d]: '%s' invalid dict-size, expects "
                        "[m|M|g|G].\n", file, linenum, args[0]);

                err_code |= ERR_ALERT | ERR_FATAL;
                goto out;
            }

            cur_arg++;
            continue;
        }

        if(!strcmp(args[cur_arg], "data-size")) {
            cur_arg++;

            if(*args[cur_arg] == 0) {
                ha_alert("parsing [%s:%d]: '%s' data-size expects a size.\n",
                        file, linenum, args[0]);

                err_code |= ERR_ALERT | ERR_FATAL;
                goto out;
            }

            if(nst_parse_size(args[cur_arg], &global.nuster.nosql.data_size)) {

                ha_alert("parsing [%s:%d]: '%s' invalid data_size, expects "
                        "[m|M|g|G].\n", file, linenum, args[0]);

                err_code |= ERR_ALERT | ERR_FATAL;
                goto out;
            }

            cur_arg++;
            continue;
        }

        if(!strcmp(args[cur_arg], "dir")) {
            cur_arg++;

            if(*(args[cur_arg]) == 0) {
                ha_alert("parsing [%s:%d]: '%s': `dir` expects a root as "
                        "an argument.\n", file, linenum, args[0]);

                err_code |= ERR_ALERT | ERR_FATAL;
                goto out;
            }

            global.nuster.nosql.root = strdup(args[cur_arg]);
            cur_arg++;
            continue;
        }

        if(!strcmp(args[cur_arg], "dict-cleaner")) {
            cur_arg++;

            if(*args[cur_arg] == 0) {
                ha_alert("parsing [%s:%d]: '%s' dict-cleaner expects a number."
                        "\n", file, linenum, args[0]);

                err_code |= ERR_ALERT | ERR_FATAL;
                goto out;
            }

            global.nuster.nosql.dict_cleaner = atoi(args[cur_arg]);

            if(global.nuster.nosql.dict_cleaner <= 0) {
                global.nuster.nosql.dict_cleaner = NST_DEFAULT_DICT_CLEANER;
            }

            cur_arg++;
            continue;
        }

        if(!strcmp(args[cur_arg], "data-cleaner")) {
            cur_arg++;

            if(*args[cur_arg] == 0) {
                ha_alert("parsing [%s:%d]: '%s' data-cleaner expects a number."
                        "\n", file, linenum, args[0]);

                err_code |= ERR_ALERT | ERR_FATAL;
                goto out;
            }

            global.nuster.nosql.data_cleaner = atoi(args[cur_arg]);

            if(global.nuster.nosql.data_cleaner <= 0) {
                global.nuster.nosql.data_cleaner =
                    NST_DEFAULT_DATA_CLEANER;
            }

            cur_arg++;
            continue;
        }

        if(!strcmp(args[cur_arg], "disk-cleaner")) {
            cur_arg++;

            if(*args[cur_arg] == 0) {
                ha_alert("parsing [%s:%d]: '%s' disk-cleaner expects a number."
                        "\n", file, linenum, args[0]);

                err_code |= ERR_ALERT | ERR_FATAL;
                goto out;
            }

            global.nuster.nosql.disk_cleaner = atoi(args[cur_arg]);

            if(global.nuster.nosql.disk_cleaner <= 0) {
                global.nuster.nosql.disk_cleaner = NST_DEFAULT_DISK_CLEANER;
            }

            cur_arg++;
            continue;
        }

        if(!strcmp(args[cur_arg], "disk-loader")) {
            cur_arg++;

            if(*args[cur_arg] == 0) {
                ha_alert("parsing [%s:%d]: '%s' disk-loader expects a number."
                        "\n", file, linenum, args[0]);

                err_code |= ERR_ALERT | ERR_FATAL;
                goto out;
            }

            global.nuster.nosql.disk_loader = atoi(args[cur_arg]);

            if(global.nuster.nosql.disk_loader <= 0) {
                global.nuster.nosql.disk_loader = NST_DEFAULT_DISK_LOADER;
            }

            cur_arg++;
            continue;
        }

        if(!strcmp(args[cur_arg], "disk-saver")) {
            cur_arg++;

            if(*args[cur_arg] == 0) {
                ha_alert("parsing [%s:%d]: '%s' disk-saver expects a number."
                        "\n", file, linenum, args[0]);

                err_code |= ERR_ALERT | ERR_FATAL;
                goto out;
            }

            global.nuster.nosql.disk_saver = atoi(args[cur_arg]);

            if(global.nuster.nosql.disk_saver <= 0) {
                global.nuster.nosql.disk_saver = NST_DEFAULT_DISK_SAVER;
            }

            cur_arg++;
            continue;
        }

        ha_alert("parsing [%s:%d]: '%s' Unrecognized .\n", file, linenum,
                args[cur_arg]);

        err_code |= ERR_ALERT | ERR_FATAL;
        goto out;
    }

out:
    return err_code;
}

int nst_parse_proxy_cache(char **args, int section, struct proxy *px,
        struct proxy *defpx, const char *file, int line, char **err) {

    struct flt_conf *fconf;
    struct nst_flt_conf *conf;
    int cur_arg = 1;

    list_for_each_entry(fconf, &px->filter_configs, list) {

        if(fconf->id == nst_cache_flt_id) {
            memprintf(err, "%s: Proxy supports only one cache filter\n",
                    px->id);

            return -1;
        }

    }

    fconf = calloc(1, sizeof(*fconf));
    conf  = malloc(sizeof(*conf));

    if(!fconf || !conf) {
        memprintf(err, "out of memory");
        return -1;
    }

    memset(fconf, 0, sizeof(*fconf));
    memset(conf, 0, sizeof(*conf));

    conf->status = NST_STATUS_ON;
    cur_arg++;

    if(*args[cur_arg]) {

        if(!strcmp(args[cur_arg], "off")) {
            conf->status = NST_STATUS_OFF;
        } else if(!strcmp(args[cur_arg], "on")) {
            conf->status = NST_STATUS_ON;
        } else {
            memprintf(err, "%s: expects [on|off], default on", args[cur_arg]);
            return -1;
        }

        cur_arg++;
    }

    fconf->id   = nst_cache_flt_id;
    fconf->conf = conf;
    fconf->ops  = &nst_cache_filter_ops;

    LIST_ADDQ(&px->filter_configs, &fconf->list);

    px->nuster.mode = NST_MODE_CACHE;

    return 0;
}

int nst_parse_proxy_nosql(char **args, int section, struct proxy *px,
        struct proxy *defpx, const char *file, int line, char **err) {

    struct flt_conf *fconf;
    fconf = calloc(1, sizeof(*fconf));

    if(!fconf) {
        memprintf(err, "out of memory");
        return -1;
    }

    memset(fconf, 0, sizeof(*fconf));
    fconf->id   = nst_nosql_flt_id;
    fconf->ops  = &nst_nosql_filter_ops;

    LIST_ADDQ(&px->filter_configs, &fconf->list);

    px->nuster.mode = NST_MODE_NOSQL;

    return 0;
}

int nst_parse_proxy_rule(char **args, int section, struct proxy *proxy,
        struct proxy *defpx, const char *file, int line, char **err) {

    struct nst_rule *rule = NULL;
    struct acl_cond *cond = NULL;

    char *name = NULL;
    char *key  = NULL;
    char *code = NULL;
    int ttl    = -1;
    int disk   = -1;
    int etag   = -1;

    int last_modified = -1;

    int extend[3] = { -1 };

    int cur_arg = 2;

    if(proxy == defpx || !(proxy->cap & PR_CAP_BE)) {
        memprintf(err, "`rule` is not allowed in a 'frontend' or 'defaults' "
                "section.");

        return -1;
    }

    if(*(args[cur_arg]) == 0) {
        memprintf(err, "'%s' expects a name.", args[0]);
        return -1;
    }

    name = strdup(args[cur_arg]);
    cur_arg = 3;

    while(*(args[cur_arg]) !=0 && strcmp(args[cur_arg], "if") !=0
            && strcmp(args[cur_arg], "unless") != 0) {

        if(!strcmp(args[cur_arg], "key")) {

            if(key != NULL) {
                memprintf(err, "'%s %s': key already specified.", args[0],
                        name);

                goto out;
            }

            cur_arg++;

            if(*(args[cur_arg]) == 0) {
                memprintf(err, "'%s %s': expects a key.", args[0], name);
                goto out;
            }

            key = args[cur_arg];
            cur_arg++;
            continue;
        }

        if(!strcmp(args[cur_arg], "ttl")) {

            if(ttl != -1) {
                /* except this case: ttl 4294967295 ttl 4294967295 */
                memprintf(err, "'%s %s': ttl already specified.", args[0],
                        name);

                goto out;
            }
            cur_arg++;

            if(*args[cur_arg] == 0) {
                memprintf(err, "'%s %s': expects a ttl(in seconds).", args[0],
                        name);

                goto out;
            }

            /* "d", "h", "m", "s"
             * s is returned
             * */
            if(nst_parse_time(args[cur_arg], strlen(args[cur_arg]),
                        (unsigned *)&ttl)) {

                memprintf(err, "'%s %s': invalid ttl.", args[0], name);
                goto out;
            }

            cur_arg++;
            continue;
        }

        if(!strcmp(args[cur_arg], "code")) {

            if(code != NULL) {
                memprintf(err, "'%s %s': code already specified.", args[0],
                        name);

                goto out;
            }

            cur_arg++;

            if(*(args[cur_arg]) == 0) {
                memprintf(err, "'%s %s': expects a code.", args[0], name);
                goto out;
            }

            code = args[cur_arg];
            cur_arg++;
            continue;
        }

        if(!strcmp(args[cur_arg], "disk")) {

            if(disk != -1) {
                memprintf(err, "'%s %s': disk already specified.", args[0],
                        name);

                goto out;
            }

            cur_arg++;
            if(*args[cur_arg] == 0) {
                memprintf(err, "'%s %s': expects [off|only|sync|async], "
                        "default off.", args[0], name);

                goto out;
            }

            if(!strcmp(args[cur_arg], "off")) {
                disk = NST_DISK_OFF;
            } else if(!strcmp(args[cur_arg], "only")) {
                disk = NST_DISK_ONLY;
            } else if(!strcmp(args[cur_arg], "sync")) {
                disk = NST_DISK_SYNC;
            } else if(!strcmp(args[cur_arg], "async")) {
                disk = NST_DISK_ASYNC;
            } else {
                memprintf(err, "'%s %s': expects [off|only|sync|async], "
                        "default off.", args[0], name);

                goto out;
            }

            cur_arg++;
            continue;
        }

        if(!strcmp(args[cur_arg], "etag")) {

            if(etag != -1) {
                memprintf(err, "'%s %s': etag already specified.",
                        args[0], name);

                goto out;
            }

            cur_arg++;
            if(*args[cur_arg] == 0) {
                memprintf(err, "'%s %s': expects [on|off], default off.",
                        args[0], name);

                goto out;
            }

            if(!strcmp(args[cur_arg], "on")) {
                etag = NST_STATUS_ON;
            } else if(!strcmp(args[cur_arg], "off")) {
                etag = NST_STATUS_OFF;
            } else {
                memprintf(err, "'%s %s': expects [on|off], default off.",
                        args[0], name);

                goto out;
            }

            cur_arg++;
            continue;
        }

        if(!strcmp(args[cur_arg], "last-modified")) {

            if(last_modified != -1) {
                memprintf(err, "'%s %s': last-modified already specified.",
                        args[0], name);

                goto out;
            }

            cur_arg++;
            if(*args[cur_arg] == 0) {
                memprintf(err, "'%s %s': expects [on|off], default off.",
                        args[0], name);

                goto out;
            }

            if(!strcmp(args[cur_arg], "on")) {
                last_modified = NST_STATUS_ON;
            } else if(!strcmp(args[cur_arg], "off")) {
                last_modified = NST_STATUS_OFF;
            } else {
                memprintf(err, "'%s %s': expects [on|off], default off.",
                        args[0], name);

                goto out;
            }

            cur_arg++;
            continue;
        }

        if(!strcmp(args[cur_arg], "extend")) {

            if(extend[0] != -1) {
                memprintf(err, "'%s %s': extend already specified.",
                        args[0], name);

                goto out;
            }

            cur_arg++;
            if(*args[cur_arg] == 0) {
                memprintf(err, "'%s %s': expects [on|off|N1,N2], default off.",
                        args[0], name);

                goto out;
            }

            if(!strcmp(args[cur_arg], "on")) {
                extend[0] = extend[1] = extend[2] = 33;
            } else if(!strcmp(args[cur_arg], "off")) {
                extend[0] = extend[1] = extend[3] = 0;
            } else {
                char *tmp = strdup(args[cur_arg]);
                char *ptr = tmp;
                char *next;

                int i = 0;

                while(tmp != NULL) {
                    strsep(&ptr, ",");
                    extend[i++ % 3] = strtol(tmp, &next, 10);

                    if((next == tmp) || (*next != '\0')) {
                        memprintf(err, "'%s %s': expects [on|off|N1,N2,N3],"
                                "default off.", args[0], name);

                        goto out;
                    }

                    tmp = ptr;
                }

                if(i != 3) {
                    memprintf(err, "'%s %s': expects [on|off|N1,N2,N3],ff "
                            "default off.", args[0], name);

                    goto out;
                }

                if(extend[0] < -1 || extend[1] < -1 || extend[2] < -1) {
                    memprintf(err, "'%s %s': expects positive integer",
                            args[0], name);

                    goto out;
                }

                if(extend[0] + extend[1] + extend[2] > 100) {
                    memprintf(err, "'%s %s': N1 + N2 + N3 cannot be greater "
                            "than 100", args[0], name);

                    goto out;
                }

            }

            cur_arg++;
            continue;
        }

        memprintf(err, "'%s %s': Unrecognized '%s'.", args[0], name,
                args[cur_arg]);
        goto out;
    }

    if(!strcmp(args[cur_arg], "if") || !strcmp(args[cur_arg], "unless")) {

        if(*args[cur_arg + 1] != 0) {
            char *errmsg = NULL;

            if((cond = build_acl_cond(file, line, &proxy->acl, proxy,
                            (const char **)args + cur_arg, &errmsg)) == NULL) {

                memprintf(err, "%s", errmsg);
                free(errmsg);
                goto out;
            }

        } else {
            memprintf(err, "'%s %s': [if|unless] expects an acl.", args[0],
                    name);

            goto out;
        }
    }

    rule       = malloc(sizeof(*rule));
    rule->cond = cond;
    rule->name = name;
    rule->key  = _nst_parse_rule_key(key == NULL
            ? NST_CACHE_DEFAULT_KEY : key);

    if(!rule->key) {
        memprintf(err, "'%s %s': invalid key.", args[0], name);
        goto out;
    }

    rule->code = _nst_parse_rule_code(code == NULL
            ? NST_CACHE_DEFAULT_CODE : code);

    rule->ttl  = malloc(sizeof(*rule->ttl));
    *rule->ttl = ttl == -1 ? NST_DEFAULT_TTL : ttl;

    if(disk > 0 && !global.nuster.cache.root) {
        memprintf(err, "rule %s: disk enabled but no `dir` defined", name);
        goto out;
    }

    rule->disk = disk == -1 ? NST_DISK_OFF : disk;
    rule->etag = etag == -1 ? NST_STATUS_OFF : etag;

    rule->last_modified = last_modified == -1 ? NST_STATUS_OFF : last_modified;

    if(extend[0] == -1) {
        rule->extend[0] = rule->extend[1] = 0;
    } else {
        rule->extend[0] = extend[0];
        rule->extend[1] = extend[1];
    }

    rule->id   = -1;
    LIST_INIT(&rule->list);
    LIST_ADDQ(&proxy->nuster.rules, &rule->list);

    return 0;

out:
    return -1;
}

int nst_parse_proxy(char **args, int section, struct proxy *px,
        struct proxy *defpx, const char *file, int line, char **err) {

    if(px->cap != PR_CAP_BE) {
        memprintf(err, "[proxy] '%s' is only allowed in 'backend' section.",
                args[0]);

        return -1;
    }

    if(*args[1]) {

        if(!strcmp(args[1], "cache")) {
            return nst_parse_proxy_cache(args, section, px, defpx, file,
                    line, err);

        } else if(!strcmp(args[1], "nosql")) {
            return nst_parse_proxy_nosql(args, section, px, defpx, file,
                    line, err);

        } else if(!strcmp(args[1], "rule")) {
            return nst_parse_proxy_rule(args, section, px, defpx, file,
                    line, err);

        } else {
            memprintf(err, "%s: expects [cache|rule]", args[0]);
            return -1;
        }

    }

    return 0;
}

static struct cfg_kw_list cfg_kws = {ILH, {
    { CFG_LISTEN, "nuster", nst_parse_proxy}, { 0, NULL, NULL }, }
};

__attribute__((constructor)) static void __nst_parser_init(void) {
    cfg_register_keywords(&cfg_kws);
}
