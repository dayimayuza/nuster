/*
 * nuster file related functions.
 *
 * Copyright (C) [Jiang Wenyuan](https://github.com/jiangwenyuan), < koubunen AT gmail DOT com >
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 */

#include <nuster/file.h>

int nuster_create_path(char *path) {
    char *p = path;
    while(*p != '\0') {
        p++;
        while(*p != '/' && *p != '\0') p++;
        if(*p == '/') {
            *p = '\0';
            if(mkdir(path, S_IRWXU) == -1 && errno != EEXIST) {
                *p = '/';
                return 0;
            }
            *p = '/';
        }
    }
    if(mkdir(path, S_IRWXU) == -1 && errno != EEXIST) {
        return 0;
    }
    return 1;
}