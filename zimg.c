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
 * @file zimg.c
 * @brief Convert, get and save image functions.
 * @author 招牌疯子 zp@buaa.us
 * @version 1.0
 * @date 2013-07-19
 */

#include <sys/file.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <wand/MagickWand.h>
#include "zimg.h"
#include "zmd5.h"
#include "zlog.h"
#include "zcache.h"
#include "zutil.h"

extern struct setting settings;

const char *get_img_format(const char *buff){
    if(buff == NULL){
	return NULL;
    }

    int len = sizeof(magicInfoTable) / sizeof(struct MagicInfo);
    
    int i;
    const struct MagicInfo *p = NULL;
    for(i = 0;i < len;i++){
	p = magicInfoTable + i;
	if(memcmp(p->magic,buff+p->offset,p->length) == 0){
	    return p->name;
	}
    }

    return NULL;
}

void calc_md5sum(const char *buff,const int len,char *md5sum){
    if(buff == NULL || md5sum == NULL){
	return;
    }

    LOG_PRINT(LOG_INFO, "Begin to Caculate MD5...");
    md5_state_t mdctx;
    md5_byte_t md_value[16];

    int i;
    int h, l;
    md5_init(&mdctx);
    md5_append(&mdctx, (const unsigned char*)(buff), len);
    md5_finish(&mdctx, md_value);

    for(i=0; i<16; ++i)
    {
	h = md_value[i] & 0xf0;
	h >>= 4;
	l = md_value[i] & 0x0f;
	md5sum[i * 2] = (char)((h >= 0x0 && h <= 0x9) ? (h + 0x30) : (h + 0x57));
	md5sum[i * 2 + 1] = (char)((l >= 0x0 && l <= 0x9) ? (l + 0x30) : (l + 0x57));
    }
    md5sum[32] = '\0';
    LOG_PRINT(LOG_INFO, "md5: %s", md5sum);
}

int convert2jpg(const char *buff, const int len,const char *path){
    //http://members.shaw.ca/el.supremo/MagickWand/resize.htm
    //MagickGetImageFormat
    MagickWand *m_wand = NULL;

    m_wand = NewMagickWand();

    MagickReadImageBlob(m_wand, buff, len);
    
    //strip exif,GPS data
    MagickStripImage(m_wand);

    // Set the compression quality to 75 (high quality = low compression)
    MagickSetImageCompressionQuality(m_wand,75);

    /* Write the new image */
    MagickWriteImage(m_wand,path);

    /* Clean up */
    if(m_wand)m_wand = DestroyMagickWand(m_wand);

    return ZIMG_OK;
}

char* get_phone_img(const char *phone_str, size_t *img_size){
    if(phone_str == NULL){
	return NULL;
    }

    MagickWand *m_wand  = NULL;
    PixelWand *p_wand  = NULL;
    DrawingWand *d_wand = NULL;


    /* Create a wand */
    m_wand = NewMagickWand();
    p_wand = NewPixelWand();
    d_wand = NewDrawingWand();

    PixelSetColor(p_wand,"white");

    int height = 18;
    int width = strlen(phone_str) * 130 / 12;

    MagickNewImage(m_wand, width,height ,p_wand);

    //draw number
    PixelSetColor(p_wand,"black");
    DrawSetFillColor(d_wand,p_wand);
    DrawSetFont (d_wand, "Arial" ) ;
    DrawSetFontSize(d_wand,20);
    DrawSetStrokeColor(d_wand,p_wand);
    DrawAnnotation(d_wand,0,height -2,phone_str);
    MagickDrawImage(m_wand,d_wand);
    //MagickTrimImage(m_wand,0);
    //ImageFormat MUST be SET,otherwise,otherwise we will not MagickGetImageBlob properly
    MagickSetImageFormat(m_wand,"JPEG");

    char *p = NULL;
    char *data = NULL;

    p = (char *)MagickGetImageBlob(m_wand,img_size);

    if(p != NULL){
	data = (char *)malloc(*img_size);
	if(data != NULL){
	    memcpy(data,p,*img_size);
	}else{
	    LOG_PRINT(LOG_INFO, "malloc Failed!");
	}
    }else{
	LOG_PRINT(LOG_INFO, "MagickGetImageBlob Failed!");
    }
    
    /* Tidy up */
    MagickRelinquishMemory(p);
    DestroyMagickWand(m_wand);
    DestroyPixelWand(p_wand);

    return data;
}
/**
 * @brief save_img Save buffer from POST requests
 *
 * @param buff The char * from POST request
 * @param len The length of buff
 * @param md5 Parsed md5 from url
 *
 * @return ZIMG_OK for success and ZIMG_ERR for fail
 */
int save_img(const char *buff, const int len, char *md5sum){
    if(buff == NULL || md5sum == NULL || len <=0){
	return ZIMG_ERR;
    }

    const char *image_format = get_img_format(buff);
    if(image_format == NULL){
	return ZIMG_ERR;
    }

    calc_md5sum(buff,len,md5sum);

    char cache_key[45];
    sprintf(cache_key, "img:%s:0:0:1:0", md5sum);

    if(exist_cache(cache_key) == 1){
	LOG_PRINT(LOG_INFO, "File Exist, Needn't Save.");
	return ZIMG_OK;
    }

    LOG_PRINT(LOG_INFO, "exist_cache not found. Begin to Check File.");

    char *save_path = (char *)malloc(512);
    if(save_path == NULL){
	return ZIMG_ERR;
    }
    //caculate 2-level path
    int lvl1 = str_hash(md5sum);
    int lvl2 = str_hash(md5sum + 3);

    sprintf(save_path, "%s/%d/%d/%s/0*0p", settings.img_path, lvl1, lvl2, md5sum);
    LOG_PRINT(LOG_INFO, "save_path: %s", save_path);

    if(is_file(save_path) == ZIMG_OK){
	LOG_PRINT(LOG_INFO, "Check File Exist. Needn't Save.");
	//cache
	if(len < CACHE_MAX_SIZE)
	{
	    // to gen cache_key like this: rsp_path-/926ee2f570dc50b2575e35a6712b08ce
	    set_cache_bin(cache_key, buff, len);
	}
	free(save_path);
	return ZIMG_OK;
    }else{
	char *p = strrchr(save_path,'/');
	*p = '\0';
	if(mk_dirs(save_path) == ZIMG_ERR){
	    LOG_PRINT(LOG_ERROR, "save_path[%s] Create Failed!", save_path);
	    free(save_path);
	    return ZIMG_ERR;
	}

	chdir(_init_path);
	LOG_PRINT(LOG_INFO, "save_path[%s] Create Finish.", save_path);

	*p = '/';
	LOG_PRINT(LOG_INFO, "save_name-->: %s", save_path);

	if(new_img(buff, len, save_path) != ZIMG_OK){
	    LOG_PRINT(LOG_WARNING, "Save Image[%s] Failed!", save_path);
	    free(save_path);
	    return ZIMG_ERR;
	}

	//shrink as JPEG
	p = strrchr(save_path,'/');
	memcpy(p+1,"0.jpg",5);
	*(p + 6) = '\0';

	if(convert2jpg(buff,len,save_path) == ZIMG_OK){
	    LOG_PRINT(LOG_WARNING, "Convert Image to JPEG [%s] OK!", save_path);
	}

	free(save_path);
	return ZIMG_OK;
    }
}

/**
 * @brief new_img The real function to save a image to disk.
 *
 * @param buff Const buff to write to disk.
 * @param len The length of buff.
 * @param save_name The name you want to save.
 *
 * @return ZIMG_OK for success and ZIMG_ERR for fail.
 */
int new_img(const char *buff, const size_t len, const char *save_name){
    LOG_PRINT(LOG_INFO, "Start to Storage the New Image...");
    int fd = -1;
    int wlen = 0;

    if((fd = open(save_name, O_WRONLY | O_TRUNC | O_CREAT, 00644)) < 0){
	LOG_PRINT(LOG_ERROR, "fd(%s) open failed!", save_name);
	return ZIMG_ERR;
    }

    if(flock(fd, LOCK_EX | LOCK_NB) == -1){
	LOG_PRINT(LOG_WARNING, "This fd is Locked by Other thread.");
	close(fd);
	return ZIMG_ERR;
    }

    wlen = write(fd, buff, len);
    if(wlen < len){
	if(wlen == -1){
	    LOG_PRINT(LOG_ERROR, "write(%s) failed!", save_name);
	}else{
	    LOG_PRINT(LOG_ERROR, "Only part of [%s] is been writed.", save_name);
	}

	close(fd);
	return ZIMG_ERR;
    }

    flock(fd, LOCK_UN | LOCK_NB);
    LOG_PRINT(LOG_INFO, "Image [%s] Write Successfully!", save_name);

    close(fd);
    return ZIMG_OK;
}

/* get image method used for zimg servise, such as:
 * http://127.0.0.1:4869/c6c4949e54afdb0972d323028657a1ef?w=100&h=50&p=1&g=1 */
/**
 * @brief get_img The function of getting a image buffer and it's length.
 *
 * @param req The zimg_req_t from zhttp and it has the params of a request.
 * @param buff_ptr This function return image buffer in it.
 * @param img_size Get_img will change this number to return the size of image buffer.
 *
 * @return ZIMG_OK for success and ZIMG_ERR for fail.
 */
int get_img(zimg_req_t *req, char **buff_ptr, size_t *img_size)
{
    int result = -1;
    char *rsp_path = NULL;
    char *whole_path = NULL;
    char *orig_path = NULL;
    char *color_path = NULL;
    char *img_format = NULL;
    size_t len;
    int fd = -1;
    struct stat f_stat;

    MagickBooleanType status;
    MagickWand *magick_wand = NULL;

    LOG_PRINT(LOG_INFO, "get_img() start processing zimg request...");

    char *cache_key = (char *)malloc(strlen(req->md5) + 32);
    if(cache_key == NULL){
	LOG_PRINT(LOG_INFO, "malloc failed!");
	return ZIMG_ERR;
    }
    // to gen cache_key like this: img:926ee2f570dc50b2575e35a6712b08ce:0:0:1:0
    sprintf(cache_key, "img:%s:%d:%d:%d:%d", req->md5, req->width, req->height, req->proportion, req->gray);
    if(find_cache_bin(cache_key, buff_ptr, img_size) == 1){
	LOG_PRINT(LOG_INFO, "Hit Cache[Key: %s].", cache_key);
	//        sprintf(cache_key, "type:%s:%d:%d:%d:%d", req->md5, req->width, req->height, req->proportion, req->gray);
	//        if(find_cache(cache_key, img_format) == -1)
	//        {
	//            LOG_PRINT(LOG_WARNING, "Cannot Hit Type Cache[Key: %s]. Use jpeg As Default.", cache_key);
	//            strcpy(img_format, "jpeg");
	//        }
	free(cache_key);
	return ZIMG_OK;
    }
    free(cache_key);

    LOG_PRINT(LOG_INFO, "Start to Find the Image...");

    //check img dir
    // /1023/1023/xxxxxxxxxxxxxxx.png
    len = strlen(req->md5) + strlen(settings.img_path) + 12;
    whole_path = malloc(len);

    if (whole_path == NULL){
	LOG_PRINT(LOG_ERROR, "whole_path malloc failed!");
	return ZIMG_ERR;
    }

    int lvl1 = str_hash(req->md5);
    int lvl2 = str_hash(req->md5 + 3);
    sprintf(whole_path, "%s/%d/%d/%s", settings.img_path, lvl1, lvl2, req->md5);
    LOG_PRINT(LOG_INFO, "docroot: %s", settings.img_path);
    LOG_PRINT(LOG_INFO, "req->md5: %s", req->md5);
    LOG_PRINT(LOG_INFO, "whole_path: %s", whole_path);

    char name[128];
    if(req->proportion && req->gray)
	sprintf(name, "%d*%dpg", req->width, req->height);
    else if(req->proportion && !req->gray)
	sprintf(name, "%d*%dp", req->width, req->height);
    else if(!req->proportion && req->gray)
	sprintf(name, "%d*%dg", req->width, req->height);
    else
	sprintf(name, "%d*%d", req->width, req->height);

    orig_path = (char *)malloc(strlen(whole_path) + 6);
    sprintf(orig_path, "%s/0*0p", whole_path);
    LOG_PRINT(LOG_INFO, "0rig File Path: %s", orig_path);

    rsp_path = (char *)malloc(512);
    if(req->width == 0 && req->height == 0 && req->gray == 0)
    {
	LOG_PRINT(LOG_INFO, "Return original image.");
	strcpy(rsp_path, orig_path);
    }
    else
    {
	sprintf(rsp_path, "%s/%s", whole_path, name);
    }
    LOG_PRINT(LOG_INFO, "Got the rsp_path: %s", rsp_path);
    bool got_rsp = true;
    bool got_color = false;


    //status=MagickReadImage(magick_wand, rsp_path);
    if((fd = open(rsp_path, O_RDONLY)) == -1)
	//if(status == MagickFalse)
    {
	magick_wand = NewMagickWand();
	got_rsp = false;

	if(req->gray == 1)
	{
	    sprintf(cache_key, "img:%s:%d:%d:%d:0", req->md5, req->width, req->height, req->proportion);
	    if(find_cache_bin(cache_key, buff_ptr, img_size) == 1)
	    {
		LOG_PRINT(LOG_INFO, "Hit Color Image Cache[Key: %s, len: %d].", cache_key, *img_size);
		status = MagickReadImageBlob(magick_wand, *buff_ptr, *img_size);
		if(status == MagickFalse)
		{
		    LOG_PRINT(LOG_WARNING, "Color Image Cache[Key: %s] is Bad. Remove.", cache_key);
		    del_cache(cache_key);
		}
		else
		{
		    got_color = true;
		    LOG_PRINT(LOG_INFO, "Read Image from Color Image Cache[Key: %s, len: %d] Succ. Goto Convert.", cache_key, *img_size);
		    goto convert;
		}
	    }

	    len = strlen(rsp_path);
	    color_path = (char *)malloc(len);
	    strncpy(color_path, rsp_path, len);
	    color_path[len - 1] = '\0';
	    LOG_PRINT(LOG_INFO, "color_path: %s", color_path);
	    status=MagickReadImage(magick_wand, color_path);
	    if(status == MagickTrue)
	    {
		got_color = true;
		LOG_PRINT(LOG_INFO, "Read Image from Color Image[%s] Succ. Goto Convert.", color_path);
		*buff_ptr = (char *)MagickGetImageBlob(magick_wand, img_size);
		if(*img_size < CACHE_MAX_SIZE)
		{
		    set_cache_bin(cache_key, *buff_ptr, *img_size);
		    //                    img_format = MagickGetImageFormat(magick_wand);
		    //                    sprintf(cache_key, "type:%s:%d:%d:%d:0", req->md5, req->width, req->height, req->proportion);
		    //                    set_cache(cache_key, img_format);
		}

		goto convert;
	    }
	}

	// to gen cache_key like this: rsp_path-/926ee2f570dc50b2575e35a6712b08ce
	sprintf(cache_key, "img:%s:0:0:1:0", req->md5);
	if(find_cache_bin(cache_key, buff_ptr, img_size) == 1)
	{
	    LOG_PRINT(LOG_INFO, "Hit Orignal Image Cache[Key: %s].", cache_key);
	    status = MagickReadImageBlob(magick_wand, *buff_ptr, *img_size);
	    if(status == MagickFalse)
	    {
		LOG_PRINT(LOG_WARNING, "Open Original Image From Blob Failed! Begin to Open it From Disk.");
		ThrowWandException(magick_wand);
		del_cache(cache_key);
		status = MagickReadImage(magick_wand, orig_path);
		if(status == MagickFalse)
		{
		    ThrowWandException(magick_wand);
		    goto err;
		}
		else
		{
		    *buff_ptr = (char *)MagickGetImageBlob(magick_wand, img_size);
		    if(*img_size < CACHE_MAX_SIZE)
		    {
			set_cache_bin(cache_key, *buff_ptr, *img_size);
			//                        img_format = MagickGetImageFormat(magick_wand);
			//                        sprintf(cache_key, "type:%s:0:0:1:0", req->md5);
			//                        set_cache(cache_key, img_format);
		    }
		}
	    }
	}
	else
	{
	    LOG_PRINT(LOG_INFO, "Not Hit Original Image Cache. Begin to Open it.");
	    status = MagickReadImage(magick_wand, orig_path);
	    if(status == MagickFalse)
	    {
		ThrowWandException(magick_wand);
		goto err;
	    }
	    else
	    {
		*buff_ptr = (char *)MagickGetImageBlob(magick_wand, img_size);
		if(*img_size < CACHE_MAX_SIZE)
		{
		    set_cache_bin(cache_key, *buff_ptr, *img_size);
		    //                    img_format = MagickGetImageFormat(magick_wand);
		    //                    sprintf(cache_key, "type:%s:0:0:1:0", req->md5);
		    //                    set_cache(cache_key, img_format);
		}
	    }
	}
	int width, height;
	width = req->width;
	height = req->height;
	float owidth = MagickGetImageWidth(magick_wand);
	float oheight = MagickGetImageHeight(magick_wand);
	if(width <= owidth && height <= oheight)
	{
	    if(req->proportion == 1)
	    {
		if(req->width != 0 && req->height == 0)
		{
		    height = width * oheight / owidth;
		}
		//else if(height != 0 && width == 0)
		else
		{
		    width = height * owidth / oheight;
		}
	    }
	    status = MagickResizeImage(magick_wand, width, height, LanczosFilter, 1.0);
	    if(status == MagickFalse)
	    {
		LOG_PRINT(LOG_ERROR, "Image[%s] Resize Failed!", orig_path);
		goto err;
	    }
	    LOG_PRINT(LOG_INFO, "Resize img succ.");
	}
	/* this section can caculate the correct rsp_path, but not use, so I note them
	   else if(req->gray == true)
	   {
	   strcpy(rsp_path, orig_path);
	   rsp_path[strlen(orig_path)] = 'g';
	   rsp_path[strlen(orig_path) + 1] = '\0';
	   got_rsp = true;
	   LOG_PRINT(LOG_INFO, "Args width/height is bigger than real size, return original gray image.");
	   LOG_PRINT(LOG_INFO, "Original Gray Image: %s", rsp_path);
	   }
	 */
	else
	{
	    // Note this strcpy because rsp_path is not useful. We needn't to save the new image.
	    //strcpy(rsp_path, orig_path);
	    got_rsp = true;
	    LOG_PRINT(LOG_INFO, "Args width/height is bigger than real size, return original image.");
	}
    }
    else
    {
	fstat(fd, &f_stat);
	size_t rlen = 0;
	*img_size = f_stat.st_size;
	if(*img_size <= 0)
	{
	    LOG_PRINT(LOG_ERROR, "File[%s] is Empty.", rsp_path);
	    goto err;
	}
	if((*buff_ptr = (char *)malloc(*img_size)) == NULL)
	{
	    LOG_PRINT(LOG_ERROR, "buff_ptr Malloc Failed!");
	    goto err;
	}
	LOG_PRINT(LOG_INFO, "img_size = %d", *img_size);
	//*buff_ptr = (char *)MagickGetImageBlob(magick_wand, img_size);
	if((rlen = read(fd, *buff_ptr, *img_size)) == -1)
	{
	    LOG_PRINT(LOG_ERROR, "File[%s] Read Failed.", rsp_path);
	    LOG_PRINT(LOG_ERROR, "Error: %s.", strerror(errno));
	    goto err;
	}
	else if(rlen < *img_size)
	{
	    LOG_PRINT(LOG_ERROR, "File[%s] Read Not Compeletly.", rsp_path);
	    goto err;
	}
	goto done;
    }



convert:
    //gray image
    if(req->gray == true)
    {
	LOG_PRINT(LOG_INFO, "Start to Remove Color!");
	status = MagickSetImageColorspace(magick_wand, GRAYColorspace);
	if(status == MagickFalse)
	{
	    LOG_PRINT(LOG_ERROR, "Image[%s] Remove Color Failed!", orig_path);
	    goto err;
	}
	LOG_PRINT(LOG_INFO, "Image Remove Color Finish!");
    }

    if(got_color == false || (got_color == true && req->width == 0) )
    {
	//compress image
	LOG_PRINT(LOG_INFO, "Start to Compress the Image!");
	img_format = MagickGetImageFormat(magick_wand);
	LOG_PRINT(LOG_INFO, "Image Format is %s", img_format);
	if(strcmp(img_format, "JPEG") != 0)
	{
	    LOG_PRINT(LOG_INFO, "Convert Image Format from %s to JPEG.", img_format);
	    status = MagickSetImageFormat(magick_wand, "JPEG");
	    if(status == MagickFalse)
	    {
		LOG_PRINT(LOG_WARNING, "Image[%s] Convert Format Failed!", orig_path);
	    }
	    LOG_PRINT(LOG_INFO, "Compress Image with JPEGCompression");
	    status = MagickSetImageCompression(magick_wand, JPEGCompression);
	    if(status == MagickFalse)
	    {
		LOG_PRINT(LOG_WARNING, "Image[%s] Compression Failed!", orig_path);
	    }
	}
	size_t quality = MagickGetImageCompressionQuality(magick_wand) * 0.75;
	LOG_PRINT(LOG_INFO, "Image Compression Quality is %u.", quality);
	if(quality == 0)
	{
	    quality = 75;
	}
	LOG_PRINT(LOG_INFO, "Set Compression Quality to 75%.");
	status = MagickSetImageCompressionQuality(magick_wand, quality);
	if(status == MagickFalse)
	{
	    LOG_PRINT(LOG_WARNING, "Set Compression Quality Failed!");
	}

	//strip image EXIF infomation
	LOG_PRINT(LOG_INFO, "Start to Remove Exif Infomation of the Image...");
	status = MagickStripImage(magick_wand);
	if(status == MagickFalse)
	{
	    LOG_PRINT(LOG_WARNING, "Remove Exif Infomation of the ImageFailed!");
	}
    }
    *buff_ptr = (char *)MagickGetImageBlob(magick_wand, img_size);
    if(*buff_ptr == NULL)
    {
	LOG_PRINT(LOG_ERROR, "Magick Get Image Blob Failed!");
	goto err;
    }


done:
    if(*img_size < CACHE_MAX_SIZE)
    {
	// to gen cache_key like this: rsp_path-/926ee2f570dc50b2575e35a6712b08ce
	sprintf(cache_key, "img:%s:%d:%d:%d:%d", req->md5, req->width, req->height, req->proportion, req->gray);
	set_cache_bin(cache_key, *buff_ptr, *img_size);
	//        sprintf(cache_key, "type:%s:%d:%d:%d:%d", req->md5, req->width, req->height, req->proportion, req->gray);
	//        set_cache(cache_key, img_format);
    }

    result = 1;
    if(got_rsp == false)
    {
	LOG_PRINT(LOG_INFO, "Image[%s] is Not Existed. Begin to Save it.", rsp_path);
	result = 2;
    }
    else
	LOG_PRINT(LOG_INFO, "Image Needn't to Storage.", rsp_path);

err:
    if(fd != -1)
	close(fd);
    req->rsp_path = rsp_path;
    if(magick_wand)
    {
	magick_wand=DestroyMagickWand(magick_wand);
    }
    if(img_format)
	free(img_format);
    if(cache_key)
	free(cache_key);
    if (orig_path)
	free(orig_path);
    if (whole_path)
	free(whole_path);
    return result;
}

