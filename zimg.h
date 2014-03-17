/*   
 *   zimg - high performance image storage and processing system.
 *       http://zimg.buaa.us 
 *   
 *   Copyright (c) 2013, Peter Zhao <zp@buaa.us>.
 *   All rights reserved.
 *   
 *   Use and distribution licensed under the BSD license.
 *   See the LICENSE file for full text.
 * 
 */

/**
 * @file zimg.h
 * @brief Processing images header.
 * @author 招牌疯子 zp@buaa.us
 * @version 1.0
 * @date 2013-07-19
 */


#ifndef ZIMG_H
#define ZIMG_H


#include "zcommon.h"

#define MagickString(magic)  (const char *) (magic), sizeof(magic)-1

typedef struct zimg_req_s {
    char *md5;
    int width;
    int height;
    bool proportion;
    bool gray;
	char *rsp_path;
} zimg_req_t;

struct MagicInfo{  
    const char *name;
    int offset;
    const char *magic;
    int length;
};

static const struct MagicInfo magicInfoTable[] = 
{
    { "PNG", 0, MagickString("\211PNG\r\n\032\n") },
    { "GIF", 0, MagickString("GIF8") },
    { "JPEG", 0, MagickString("\377\330\377") }
};


int save_img(const char *buff, const int len, char *md5sum);
int new_img(const char *buff, const size_t len, const char *save_name);
int get_img(zimg_req_t *req, char **buff_ptr, size_t *img_size);
char *get_phone_img(const char *phone_str, size_t *img_size);


#endif
