# nuster

[Wiki](https://github.com/jiangwenyuan/nuster/wiki) | [English](README.md) | [中文](README-CN.md) | [日本語](README-JP.md)

A high-performance HTTP proxy cache server and RESTful NoSQL cache server based on HAProxy.

WIP: Migration to HAProxy v2.0, need explicitly `no option http-use-htx` defined

# Table of Contents

* [Introduction](#introduction)
* [Performance](#performance)
* [Getting Started](#getting-started)
* [Usage](#usage)
* [Directives](#directives)
* [Cache](#cache)
  * [Management](#cache-management)
  * [Enable/Disable](#cache-rule-enable-and-disable)
  * [TTL](#cache-ttl)
  * [Purging](#cache-purging)
  * [Stats](#cache-stats)
* [NoSQL](#nosql)
  * [Set](#set)
  * [Get](#get)
  * [Delete](#delete)
* [Disk persistence](#disk-persistence)
* [Sample fetches](#sample-fetches)
* [FAQ](#faq)

# Introduction

nuster is a high-performance HTTP proxy cache server and RESTful NoSQL cache server based on HAProxy.
It is 100% compatible with HAProxy and takes full advantage of the ACL functionality of HAProxy to provide fine-grained caching policy based on the content of request, response or server status.

## Features

### As HTTP/TCP loader balancer

nuster can be used as an HTTP/TCP load balancer just like HAProxy.

* All features of HAProxy are inherited, 100% compatible with HAProxy
* Load balancing
* HTTPS supports on both frontend and backend
* HTTP compression
* HTTP rewriting and redirection
* HTTP fixing
* HTTP2
* Monitoring
* Stickiness
* ACLs and conditions
* Content switching

### As HTTP cache server

nuster can also be used as an HTTP proxy cache server like Varnish or Nginx to cache dynamic and static HTTP response.

* All features from HAProxy(HTTPS, HTTP/2, ACL, etc)
* Extremely fast
* Powerful dynamic cache ability
  * Based on HTTP method, URI, path, query, header, cookies, etc
  * Based on HTTP request or response contents, etc
  * Based on environment variables, server state, etc
  * Based on SSL version, SNI, etc
  * Based on connection rate, number, byte, etc
* Cache management
* Cache purging
* Cache stats
* Cache TTL
* Disk persistence

### As RESTful NoSQL cache server

nuster can also be used as a RESTful NoSQL cache server, using HTTP `POST/GET/DELETE` to set/get/delete Key/Value object.

It can be used as an internal NoSQL cache sits between your application and database like Memcached or Redis as well as a user-facing NoSQL cache that sits between end-user and your application.
It supports headers, cookies, so you can store per-user data to the same endpoint.

* All features from HAProxy(HTTPS, HTTP/2, ACL, etc)
* Conditional cache
* Internal KV cache
* User facing RESTful cache
* Support any kind of data
* Support all programming languages as long as HTTP is supported
* Disk persistence

# Performance

nuster is very fast, some test shows nuster is almost three times faster than nginx when both using single core, and nearly two times faster than nginx and three times faster than varnish when using all cores.

See [detailed benchmark](https://github.com/jiangwenyuan/nuster/wiki/Web-cache-server-performance-benchmark:-nuster-vs-nginx-vs-varnish-vs-squid)

# Getting Started

## Download

Download stable version from [Download](Download.md) page for production use, otherwise git clone the source code.

## Build

```
make TARGET=linux2628 USE_LUA=1 LUA_INC=/usr/include/lua5.3 USE_OPENSSL=1 USE_PCRE=1 USE_ZLIB=1
make install PREFIX=/usr/local/nuster
```

> use `USE_PTHREAD_PSHARED=1` to use pthread lib

> omit `USE_LUA=1 LUA_INC=/usr/include/lua5.3 USE_OPENSSL=1 USE_PCRE=1 USE_ZLIB=1` if unnecessary

See [HAProxy README](README) for details.

## Create a config file

A minimal config file: `nuster.cfg`

```
global
    nuster cache on data-size 100m uri /_nuster
    nuster nosql on data-size 200m
    master-worker # since v3
defaults
    mode http
frontend fe
    bind *:8080
    #bind *:4433 ssl crt example.com.pem alpn h2,http/1.1
    use_backend be2 if { path_beg /_kv/ }
    default_backend be1
backend be1
    nuster cache on
    nuster rule img ttl 1d if { path_beg /img/ }
    nuster rule api ttl 30s if { path /api/some/api }
    server s1 127.0.0.1:8081
    server s2 127.0.0.1:8082
backend be2
    nuster nosql on
    nuster rule r1 ttl 3600
```

nuster listens on port 8080 and accepts HTTP requests.
Requests start with `/_kv/` go to backend `be2`, you can make `POST/GET/DELETE` requests to `/_kv/any_key` to `set/get/delete` K/V object.
Other requests go to backend `be1`, and will be passed to servers `s1` or `s2`. Among those requests, `/img/*` will be cached for 1 day and `/api/some/api` will be cached for 30 seconds.

## Start

`/usr/local/nuster/sbin/nuster -f nuster.cfg`

## Docker

```
docker pull nuster/nuster
docker run -d -v /path/to/nuster.cfg:/etc/nuster/nuster.cfg:ro -p 8080:8080 nuster/nuster
```

# Usage

nuster is based on HAProxy, all directives from HAProxy are supported in nuster.

## Basic

There are four basic `section`s: `global`, `defaults`, `frontend` and `backend` as you can find out in the above config file.

* global
  * defines process-wide and often OS-specific parameters
  * `nuster cache on` or `nuster nosql on` must be declared in this section in order to use cache or nosql functionality
* defaults
  * defines default parameters for all other `frontend`, `backend` sections
  * and can be overwritten in specific `frontend` or `backend` section
* frontend
  * describes a set of listening sockets accepting client connections
* backend
  * describes a set of servers to which the proxy will connect to forward incoming connections
  * `nuster cache on` or `nuster nosql on` must be declared in this section
  * `nuster rule` must be declared here

You can define multiple `frontend` or `backend` sections. If `nuster cache|nosql off` is declared or no `nuster cache|nosql on|off` declared, nuster acts just like HAProxy, as a TCP and HTTP load balancer.

Although `listen` is a complete proxy with its frontend and backend parts combined in one section, you cannot use nuster in `listen`, use `frontend` and `backend` pairs.

You can find HAProxy documentation in `/doc`, and [Online HAProxy Documentation](https://cbonte.github.io/haproxy-dconv/)

## As TCP loader balancer

```
frontend mysql-lb
   bind *:3306
   mode tcp
   default_backend mysql-cluster
backend mysql-cluster
   balance roundrobin
   mode tcp
   server s1 10.0.0.101:3306
   server s2 10.0.0.102:3306
   server s3 10.0.0.103:3306
```

## As HTTP/HTTPS loader balancer

```
frontend web-lb
   bind *:80
   #bind *:443 ssl crt XXX.pem
   mode http
   default_backend apps
backend apps
   balance roundrobin
   mode http
   server s1 10.0.0.101:8080
   server s2 10.0.0.102:8080
   server s3 10.0.0.103:8080
   #server s4 10.0.0.101:8443 ssl verify none
```

## As HTTP cache server

```
global
    nuster cache on data-size 200m
frontend fe
    bind *:8080
    default_backend be
backend be
    nuster cache on
    nuster rule all
    server s1 127.0.0.1:8081
```

## As RESTful NoSQL cache server

```
global
    nuster nosql on data-size 200m
frontend fe
    bind *:8080
    default_backend be
backend be
    nuster nosql on
    nuster rule r1 ttl 3600
```

# Directives

## global: nuster cache|nosql

**syntax:**

nuster cache on|off [data-size size] [dict-size size] [dir DIR] [dict-cleaner n] [data-cleaner n] [disk-cleaner n] [disk-loader n] [disk-saver n] [purge-method method] [uri uri]

nuster nosql on|off [data-size size] [dict-size size] [dir DIR] [dict-cleaner n] [data-cleaner n] [disk-cleaner n] [disk-loader n] [disk-saver n]

**default:** *none*

**context:** *global*

Determines whether to use cache/nosql or not.

A memory zone with a size of `data-size + dict-size` will be created.

Except for temporary data created and destroyed within a request, all cache related data including HTTP response data, keys and overheads are stored in this memory zone and shared between all processes.
If no more memory can be allocated from this memory zone, new requests that should be cached according to defined rules will not be cached unless some memory is freed.
Temporary data are stored in a memory pool which allocates memory dynamically from system in case there is no available memory in the pool.
A global internal counter monitors the memory usage of all HTTP response data across all processes, new requests will not be cached if the counter exceeds `data-size`.

### data-size

Determines the size of the memory zone along with `dict-size`.

It accepts units like `m`, `M`, `g` and `G`. By default, the size is 1024 * 1024 bytes, which is also the minimal size.

### dict-size

Determines the size of memory used by the hash table.

It accepts units like `m`, `M`, `g` and `G`. By default, the size is 1024 * 1024 bytes, which is also the minimal size.

Note that it only decides the memory used by hash table buckets, not keys. In fact, keys are stored in the memory zone which is limited by `data-size`.

**dict-size(number of buckets)** is different from **number of keys**. New keys can still be added to the hash table even if the number of keys exceeds dict-size(number of buckets) as long as there is enough memory.

Nevertheless, it may lead to a potential performance drop if `number of keys` is greater than `dict-size(number of buckets)`. An approximate number of keys multiplied by 8 (normally) as `dict-size` should be fine.

> dict-size will be removed in a future release, automatically resizing the hash table in the first version will be added back.

### dir

Specify the root directory of the disk persistence. This has to be set in order to use disk persistence.

### dict-cleaner

Prior to v2.x, manager tasks like removing invalid cache data, resetting dict entries are executed in iterations in each HTTP request. Corresponding indicators or pointers are increased or advanced in each iteration.

In v3.x these tasks are moved to the master process and also done in iterations, and these parameters can be set to control the number of times of certain task during one iteration.

During one iteration `dict-cleaner` entries are checked, invalid entries will be deleted (by default, 100).

### data-cleaner

During one iteration `data-cleaner` data are checked, invalid data will be deleted (by default, 100).

### disk-cleaner

If disk persistence is enabled, data are stored in files. These files are checked by master process and will be deleted if invalid, for example, expired.

During one iteration `disk-cleaner` files are checked, invalid files will be deleted (by default, 100).

### disk-loader

After the start of nuster, master process will load information about data previously stored on disk into memory.

During one iteration `disk-loader` files are loaded(by default, 100).

### disk-saver

Master process will save `disk async` cache data periodically.

During one iteration `disk-saver` data are checked and saved to disk if necessary (by default, 100).

See [nuster rule disk mode](#disk-mode) for details.

### purge-method [cache only]

Define a customized HTTP method with a max length of 14 to purge cache, it is `PURGE` by default.

### uri [cache only]

Enable cache manager/stats API and define the endpoint:

`nuster cache on uri /_my/_unique/_/_cache/_uri`

By default, the cache manager/stats are disabled. When it is enabled, remember to restrict the access(see [FAQ](#how-to-restrict-access)).

See [Cache Management](#cache-management) and [Cache stats](#cache-stats) for details.

## proxy: nuster cache|nosql

**syntax:**

nuster cache [on|off]

nuster nosql [on|off]

**default:** *on*

**context:** *backend*

Determines whether or not to use cache/nosql on this proxy, additional `nuster rule` should be defined.
If there are filters on this proxy, put this directive after all other filters.

## nuster rule

**syntax:** nuster rule name [key KEY] [ttl TTL] [code CODE] [disk MODE] [etag on|off] [last-modified on|off] [if|unless condition]

**default:** *none*

**context:** *backend*

Define rule to specify cache/nosql creating conditions, properties. At least one rule should be defined.

```
nuster cache on

# cache request `/asdf` for 30 seconds
nuster rule asdf ttl 30 if { path /asdf }

# cache if the request path begins with /img/
nuster rule img if { path_beg /img/ }

# cache if the response header `cache` is `yes`
acl resHdrCache res.hdr(cache) yes
nuster rule r1 if resHdrCache
```

It is possible and recommended to declare multiple rules in the same section. The order is important because the matching process stops on the first match.

```
acl pathA path /a.html
nuster cache on
nuster rule all ttl 3600
nuster rule path01 ttl 60 if pathA
```

rule `path01` will never match because the first rule will cache everything.

### name

Define a name for this rule.

It will be used in cache manager API, it does not have to be unique, but it might be a good idea to make it unique. Rules with the same name are treated as one.

### key KEY

Define the key for cache/nosql, it takes a string combined by following keywords with `.` separator:

 * method:       http method, GET/POST...
 * scheme:       http or https
 * host:         the host in the request
 * uri:          first slash to end of the url
 * path:         the URL path of the request
 * delimiter:    '?' if query exists otherwise empty
 * query:        the whole query string of the request
 * header\_NAME: the value of header `NAME`
 * cookie\_NAME: the value of cookie `NAME`
 * param\_NAME:  the value of query `NAME`
 * body:         the body of the request

The default key of CACHE is `method.scheme.host.uri`, and default key of NoSQL is `GET.scheme.host.uri`.

Example

```
GET http://www.example.com/q?name=X&type=Y

http header:
GET /q?name=X&type=Y HTTP/1.1
Host: www.example.com
ASDF: Z
Cookie: logged_in=yes; user=nuster;
```

Should result:

 * method:       GET
 * scheme:       http
 * host:         www.example.com
 * uri:          /q?name=X&type=Y
 * path:         /q
 * delimiter:    ?
 * query:        name=X&type=Y
 * header\_ASDF: Z
 * cookie\_user: nuster
 * param\_type:  Y
 * body:         (empty)

So default key produces `GET\0http\0www.example.com\0/q?name=X&type=Y\0`, and `key method.scheme.host.path.header_ASDF.cookie_user.param_type` produces `GET\0http\0www.example.com\0/q\0Z\0nuster\0Y\0`.

> `\0` is NULL character

If a request has the same key as a cached HTTP response data, then cached data will be sent to the client.

### ttl TTL

Set a TTL on key, after the TTL has expired, the key will be deleted.

It accepts units like `d`, `h`, `m` and `s`. Default ttl is `0` which does not expire the key.

### code CODE1,CODE2...

Cache only if the response status code is CODE.

By default, only 200 response is cached. You can use `all` to cache all responses.

```
nuster rule only200
nuster rule 200and404 code 200,404
nuster rule all code all
```

### disk MODE

Specify how and where to save the cached data. There are four MODEs.

* off:   default, disable disk persistence, data are stored in memory only
* only:  save data to disk only, do not store in memory
* sync:  save data to memory and disk(kernel), then return to the client
* async: save data to memory and return to the client, cached data will be saved to disk later by the master process

### etag on|off

Enable etag conditional requests handling. Add `ETag` header if absent.

Default off.

### last-modified on|off

Enable last-modified conditional requests handling. Add `Last-Modified` header if absent.

Default off.

### if|unless condition

Define when to cache using HAProxy ACL.

The evaluation involves two stages: request stage and response stage.

Cache will be performed if:

1. The evaluation in the request stage is true,
2. The evaluation in the request stage is false but true in the response stage.

**Please be very careful if you use negation in the condition or samples not available in certain stage**

For example,

1.  Cache if the request path begins with `/img/`

    nuster rule img if { path_beg /img/ }

This will work because the evaluation in the request stage will either be true or false and will never be true in the response stage as `path` is not available in the response stage.

2. Cache if `Content-Type` in response is `image/jpeg`

    nuster rule jpeg if { res.hdr(Content-Type) image/jpeg }

This will work because the evaluation in the request stage is always false as `res.hdr` is not available in the request stage, and will be either true or false in the response stage.

3. Cache if the request path begins with `/img/` and `Content-Type` in response is `image/jpeg`

It won't work if you define the rule as:

    nuster rule img if { path_beg /img/ } { res.hdr(Content-Type) image/jpeg }

because `path` is not available in the response stage and `res.hdr` is not available in the request stage, so the evaluation will never be true.

In order the make this work, `path` needs to be allocated for further use in reponse stage:

    http-request set-var(txn.pathImg) path
    acl pathImg var(txn.pathImg) -m beg /img/
    acl resHdrCT res.hdr(Content-Type) image/jpeg
    nuster rule r3 if pathImg resHdrCT

4. Another example, cache if the request path does not begin with `/api/`

It won't work neither:

    acl NoCache path_beg /api/
    nuster rule r3 if !NoCache

Because the evaluation of `NoCache` against `/api/` in the request stage is true, and the negation is false, which is the desired state, but in response stage, the evaluation of `NoCache` is always false as `path` is not available in response stage, and it will be cached as the negation `!NoCache` is true.

This will work:

    http-request set-var(txn.path) path
    acl NoCache var(txn.path) -m beg /api/
    nuster rule r1 if !NoCache

I will add several new sample fetch methods to simplify this kind of tasks in future versions.

See **7. Using ACLs and fetching samples** section in [HAProxy configuration](doc/configuration.txt)

# Cache

nuster can be used as an HTTP proxy cache server like Varnish or Nginx to cache dynamic and static HTTP response.

You can use HAProxy functionalities to terminate SSL, normalize HTTP, support HTTP2, rewrite the URL or modify headers and so on, and additional functionalities provided by nuster to control cache.

## Cache Management

Cache can be managed via a manager API which endpoints is defined by `uri` and can be accessed by making HTTP requests along with some headers.

**Enable and define the endpoint**

```
nuster cache on uri /nuster/cache
```

**Basic usage**

`curl -X POST -H "X: Y" http://127.0.0.1/nuster/cache`

**REMEMBER to enable access restriction**

## Cache rule enable and disable

Rule can be disabled at run time through manager uri. Disabled rule will not be processed, nor will the cache created by that.

***headers***

| header | value       | description
| ------ | -----       | -----------
| state  | enable      | enable  rule
|        | disable     | disable rule
| name   | rule NAME   | the rule to be enabled/disabled
|        | proxy NAME  | all rules belong to proxy NAME
|        | *           | all rules

Keep in mind that if name is not unique, **all** rules with that name will be disabled/enabled.

***Examples***

* Disable cache rule r1

  `curl -X POST -H "name: r1" -H "state: disable" http://127.0.0.1/nuster/cache`

* Disable all cache rule defined in proxy app1b

  `curl -X POST -H "name: app1b" -H "state: disable" http://127.0.0.1/nuster/cache`

* Enable all cache rule

  `curl -X POST -H "name: *" -H "state: enable" http://127.0.0.1/nuster/cache`

## Cache TTL

Change the TTL. It only affects the TTL of the responses to be cached, **does not** update the TTL of existing caches.

***headers***

| header | value      | description
| ------ | -----      | -----------
| ttl    | new TTL    | see `ttl` in `nuster rule`
| name   | rule NAME  | the cache rule to be changed
|        | proxy NAME | all cache rules belong to proxy NAME
|        | *          | all cache rules

***Examples***

```
curl -X POST -H "name: r1" -H "ttl: 0" http://127.0.0.1/nuster/cache
curl -X POST -H "name: r2" -H "ttl: 2h" http://127.0.0.1/nuster/cache
```

## Update state and TTL

state and ttl can be updated at the same time

```
curl -X POST -H "name: r1" -H "ttl: 0" -H "state: enabled" http://127.0.0.1/nuster/cache
```

## Cache Purging

There are several ways to purge cache by making HTTP `PURGE` requests to the manager uri defined by `uri`.

You can define customized HTTP method using `purge-method MYPURGE` other than the default `PURGE` in case you need to forward `PURGE` to backend servers.

### Purge one specific url

This method deletes the specific url that is being requested, like this:

`curl -XPURGE https://127.0.0.1/imgs/test.jpg`

It creates a key of `GET.scheme.host.uri` and deletes the cache with that key.

Note by default cache key contains `Host` if you cache a request like `http://example.com/test` and purge from localhost you need to specify `Host` header:

`curl -XPURGE -H "Host: example.com" http://127.0.0.1/test`


### Purge by name

Cache can be purged by making HTTP `PURGE`(or `purge-method`) requests to the manager uri along with a `name` HEADER.

***headers***

| header | value            | description
| ------ | -----            | -----------
| name   | nuster rule NAME | caches belong to rule ${NAME} will be purged
|        | proxy NAME       | caches belong to proxy ${NAME}
|        | *                | all caches

***Examples***

```
# purge all caches
curl -X PURGE -H "name: *" http://127.0.0.1/nuster/cache
# purge all caches belong to proxy applb
curl -X PURGE -H "name: app1b" http://127.0.0.1/nuster/cache
# purge all caches belong to rule r1
curl -X PURGE -H "name: r1" http://127.0.0.1/nuster/cache
```

### Purge by host

You can also purge cache by host, all caches belong to that host will be deleted:

***headers***

| header | value | description
| ------ | ----- | -----------
| x-host | HOST  | the ${HOST}

***Examples***

```
curl -X PURGE -H "x-host: 127.0.0.1:8080" http://127.0.0.1/nuster/cache
```

### Purge by path

By default, the query part is also used as a cache key, so there will be multiple caches if the query differs.

For example, for cache rule `nuster rule imgs if { path_beg /imgs/ }`, and request

```
curl https://127.0.0.1/imgs/test.jpg?w=120&h=120
curl https://127.0.0.1/imgs/test.jpg?w=180&h=180
```

There will be two cache objects since the default key contains the query part.

In order to delete that, you can

***delete one by one in case you know all queries***

```
curl -XPURGE https://127.0.0.1/imgs/test.jpg?w=120&h=120
curl -XPURGE https://127.0.0.1/imgs/test.jpg?w=180&h=180
```

It does not work if you don't know all queries.

***use a customized key and delete once in case that the query part is irrelevant***

Define a key like `nuster rule imgs key method.scheme.host.path if { path_beg /imgs }`, in this way only one cache will be created, and you can purge without query:

`curl -XPURGE https://127.0.0.1/imgs/test.jpg`

It does not work if the query part is required.

***delete by rule NAME***

`curl -X PURGE -H "name: imgs" http://127.0.0.1/nuster/cache`

It does not work if the nuster rule is defined something like `nuster rule static if { path_beg /imgs/ /css/ }`.

This method provides a way to purge just by path:

***headers***

| header | value | description
| ------ | ----- | -----------
| path   | PATH  | caches with ${PATH} will be purged
| x-host | HOST  | and host is ${HOST}

***Examples***

```
#delete all caches which path is /imgs/test.jpg
curl -X PURGE -H "path: /imgs/test.jpg" http://127.0.0.1/nuster/cache
#delete all caches which path is /imgs/test.jpg and belongs to 127.0.0.1:8080
curl -X PURGE -H "path: /imgs/test.jpg" -H "x-host: 127.0.0.1:8080" http://127.0.0.1/nuster/cache
```

### Purge by regex

You can also purge cache by regex, the caches which path match the regex will be deleted.

***headers***

| header | value | description
| ------ | ----- | -----------
| regex  | REGEX | caches which path match with ${REGEX} will be purged
| x-host | HOST  | and host is ${HOST}

***Examples***

```
#delete all caches which path starts with /imgs and ends with .jpg
curl -X PURGE -H "regex: ^/imgs/.*\.jpg$" http://127.0.0.1/nuster/cache
#delete all caches which path starts with /imgs and ends with .jpg and belongs to 127.0.0.1:8080
curl -X PURGE -H "regex: ^/imgs/.*\.jpg$" -H "127.0.0.1:8080" http://127.0.0.1/nuster/cache
```

**PURGE CAUTION**

1. **ENABLE ACCESS RESTRICTION**

2. If there are mixed headers, use the precedence of `name`, `path & host`, `path`, `regex & host`, `regex`, `host`

   `curl -XPURGE -H "name: rule1" -H "path: /imgs/a.jpg"`: purge by name

3. If there are redundant headers, use the first occurrence

   `curl -XPURGE -H "name: rule1" -H "name: rule2"`: purge by `rule1`

4. `regex` is **NOT glob**

   For example, all jpg files under /imgs should be `^/imgs/.*\.jpg$` instead of `/imgs/*.jpg`

5. Purging cache files by rule name or proxy name only works in current session. If nuster restarts, then cache files cannot be purged by rule name or proxy name as information like rule name and proxy name is not persisted in the cache fiels.

6. Purging cache files by host or path or regex only works after the disk loader process is finished. You can check the status through stats url.

## Cache Stats

Cache stats can be accessed by making HTTP GET request to the endpoint defined by `uri`;

### Enable and define the endpoint

```
nuster cache on uri /nuster/cache
```

### Usage

`curl http://127.0.0.1/nuster/cache`

### Output

* used\_mem:  The memory used for response, not include overheads.
* req\_total: Total request number handled by cache enabled backends, not include the requests handled by backends without cache enabled
* req\_hit:   Number of requests handled by cache
* req\_fetch: Fetched from backends
* req\_abort: Aborted when fetching from backends

Others are very straightforward.

# NoSQL

nuster can be used as a RESTful NoSQL cache server, using HTTP `POST/GET/DELETE` to set/get/delete Key/Value object.

## Basic Operations

### Set

```
curl -v -X POST -d value1 http://127.0.0.1:8080/key1
curl -v -X POST --data-binary @icon.jpg http://127.0.0.1:8080/imgs/icon.jpg
```

### Get

`curl -v http://127.0.0.1:8080/key1`

### Delete

`curl -v -X DELETE http://127.0.0.1:8080/key1`

## Response

Check status code.

* 200 OK
  * POST/GET: succeeds
  * DELETE: always
* 400 Bad request
  * empty value
  * incorrect acl, rules, etc
* 404 Not Found
  * POST: failed on all rule tests
  * GET: not found
* 405 Method Not Allowed
  * other methods
* 500 Internal Server Error
  * any error occurs
* 507 Insufficient Storage
  * exceeds max data-size

## Per-user data

By using header or cookie in key, you can save per-user data to the same endpoint.

```
nuster rule r1 key method.scheme.host.uri.header_userId if { path /mypoint }
nuster rule r2 key method.scheme.host.uri.cookie_sessionId if { path /mydata }
```

### Set


```
curl -v -X POST -d "333" -H "userId: 1000" http://127.0.0.1:8080/mypoint
curl -v -X POST -d "555" -H "userId: 1001" http://127.0.0.1:8080/mypoint

curl -v -X POST -d "userA data" --cookie "sessionId=ijsf023xe" http://127.0.0.1:8080/mydata
curl -v -X POST -d "userB data" --cookie "sessionId=rosre329x" http://127.0.0.1:8080/mydata
```

### Get

```
curl -v http://127.0.0.1:8080/mypoint
< 404 Not Found

curl -v -H "userId: 1000" http://127.0.0.1:8080/mypoint
< 200 OK
333

curl -v --cookie "sessionId=ijsf023xe" http://127.0.0.1:8080/mydata
< 200 OK
userA data
```

## Clients

You can use any tools or libs which support HTTP: `curl`, `postman`, python `requests`, go `net/http`, etc.

# Disk persistence

Disk persistence is introduced in v3, it supports 4 persistence modes as described above.

A minimal config file looks like

```
global
    master-worker
    nuster cache on data-size 10m dir /tmp/cache
    nuster nosql on data-size 10m dir /tmp/nosql
backend be
    nuster cache on
    nuster rule off   disk off   ttl 1m if { path_beg /disk-off }
    nuster rule only  disk only  ttl 1d if { path_beg /disk-only }
    nuster rule sync  disk sync  ttl 1h if { path_beg /disk-sync }
    nuster rule async disk async ttl 2h if { path_beg /disk-async }
    nuster rule others ttl 100
```

1. `/disk-off` will be cached only in memory
2. `/disk-only` will be cached only in disk
3. `/disk-sync` will be cached in memory and in disk, then return to the client
4. `/disk-async` will be cached in memory and return to the client, cached data will be saved to disk later
5. other requests will be cached only in memory

# Sample fetches

Nuster introduced following sample fetches

## nuster.cache.hit: boolean

Returns a boolean indicating whether it's a HIT or not and can be used like

    http-response set-header x-cache hit if { nuster.cache.hit }

# FAQ

## Cannot start: not in master-worker mode

Set `master-worker` in `global` section, or start `nuster` with `-W`.

## How to debug?

Set `debug` in `global` section, or start `nuster` with `-d`.

Nuster related debug messages start with `[nuster`.

## How to cache POST request?

Enable `option http-buffer-request` and set `body` in cache rule `key`.

By default, the cache key does not include the body of the request, remember to put `body` in key field.

Note that the size of the request body must be smaller than `tune.bufsize - tune.maxrewrite - request_header_size`, which by default is `16384 - 1024 - request_header_size`.

Refer to **option http-buffer-request** and **tune.bufsize** section in [HAProxy configuration](doc/configuration.txt) for details.

Also, it might be a good idea to put it separately in a dedicated backend as the example does.

## How to restrict access?

You can use the powerful HAProxy ACL, something like this

```
acl network_allowed src 127.0.0.1
acl purge_method method PURGE
http-request deny if purge_method !network_allowed
```

## How to enable HTTP2

```
bind :443 ssl crt pub.pem alpn h2,http/1.1
```

# Example

```
global
    nuster cache on data-size 100m
    nuster nosql on data-size 100m
    master-worker # since v3
    # daemon
    # debug
defaults
    retries 3
    option redispatch
    timeout client  30s
    timeout connect 30s
    timeout server  30s
frontend web1
    bind *:8080
    mode http
    acl pathPost path /search
    use_backend app1a if pathPost
    default_backend app1b
backend app1a
    balance roundrobin
    # mode must be http
    mode http

    # http-buffer-request must be enabled to cache post request
    option http-buffer-request

    acl pathPost path /search

    # enable cache for this proxy
    nuster cache

    # cache /search for 120 seconds. Only works when POST/PUT
    nuster rule rpost key method.scheme.host.uri.body ttl 120 if pathPost

    server s1 10.0.0.10:8080
backend app1b
    balance     roundrobin
    mode http

    nuster cache on

    # cache /a.jpg, not expire
    acl pathA path /a.jpg
    nuster rule r1 ttl 0 if pathA

    # cache /mypage, key contains cookie[userId], so it will be cached per user
    acl pathB path /mypage
    nuster rule r2 key method.scheme.host.path.delimiter.query.cookie_userId ttl 60 if pathB

    # cache /a.html if response's header[cache] is yes
    http-request set-var(txn.pathC) path
    acl pathC var(txn.pathC) -m str /a.html
    acl resHdrCache1 res.hdr(cache) yes
    nuster rule r3 if pathC resHdrCache1

    # cache /heavy for 100 seconds if be_conn greater than 10
    acl heavypage path /heavy
    acl tooFast be_conn ge 100
    nuster rule heavy ttl 100 if heavypage tooFast

    # cache all if response's header[asdf] is fdsa
    acl resHdrCache2 res.hdr(asdf)  fdsa
    nuster rule resCache ttl 0 if resHdrCache1

    server s1 10.0.0.10:8080

frontend web2
    bind *:8081
    mode http
    default_backend app2
backend app2
    balance     roundrobin
    mode http

    # disable cache on this proxy
    nuster cache off
    nuster rule all

    server s2 10.0.0.11:8080

frontend nosql_fe
    bind *:9090
    default_backend nosql_be
backend nosql_be
    nuster nosql on
    nuster rule r1 ttl 3600
```

# Contributing

* Join the development
* Give feedback
* Report issues
* Send pull requests
* Spread nuster

# License

Copyright (C) 2017-2019, [Jiang Wenyuan](https://github.com/jiangwenyuan), < koubunen AT gmail DOT com >

All rights reserved.

Licensed under GPL, the same as HAProxy

HAProxy and other sources license notices: see relevant individual files.
