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
 * @file zhttpd.c
 * @brief http protocol parse functions.
 * @author 招牌疯子 zp@buaa.us
 * @version 1.0
 * @date 2013-07-19
 */

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdint.h>
#include <htparse.h>
#include "zhttpd.h"
#include "zimg.h"
#include "zutil.h"
#include "zlog.h"

static char *server_name = "zimg/1.0.0 (Unix)";
extern struct setting settings;

static const char * guess_type(const char *type);
static const char * guess_content_type(const char *path);
static int print_headers(evhtp_header_t * header, void * arg); 
void dump_request_cb(evhtp_request_t *req, void *arg);
void echo_cb(evhtp_request_t *req, void *arg);
void post_request_cb(evhtp_request_t *req, void *arg);
void send_document_cb(evhtp_request_t *req, void *arg);
static int get_req_method(evhtp_request_t *req);

static int get_req_method(evhtp_request_t *req)
{
    int req_method = evhtp_request_get_method(req);
    if(req_method > htp_method_UNKNOWN)
    {
	req_method = htp_method_UNKNOWN;
    }

    LOG_PRINT(LOG_INFO, "Method: %s", method_strmap[req_method]);

    return req_method;
}
/**
 * @brief guess_type It returns a HTTP type by guessing the file type.
 *
 * @param type Input a file type.
 *
 * @return Const string of type.
 */
static const char * guess_type(const char *type)
{
    const struct table_entry *ent;
    for (ent = &content_type_table[0]; ent->extension; ++ent) {
	if (!evutil_ascii_strcasecmp(ent->extension, type))
	    return ent->content_type;
    }
    return "application/misc";
}

/* Try to guess a good content-type for 'path' */
/**
 * @brief guess_content_type Likes guess_type, but it process a whole path of file.
 *
 * @param path The path of a file you want to guess type.
 *
 * @return The string of type.
 */
static const char * guess_content_type(const char *path)
{
    const char *last_period, *extension;
    const struct table_entry *ent;
    last_period = strrchr(path, '.');
    if (!last_period || strchr(last_period, '/'))
	goto not_found; /* no exension */
    extension = last_period + 1;
    for (ent = &content_type_table[0]; ent->extension; ++ent) {
	if (!evutil_ascii_strcasecmp(ent->extension, extension))
	    return ent->content_type;
    }

not_found:
    return "application/misc";
}

/**
 * @brief print_headers It displays all headers and values.
 *
 * @param header The header of a request.
 * @param arg The evbuff you want to store the k-v string.
 *
 * @return It always return 1 for success.
 */
static int print_headers(evhtp_header_t * header, void * arg) 
{
    evbuf_t * buf = arg;

    evbuffer_add(buf, header->key, header->klen);
    evbuffer_add(buf, ": ", 2);
    evbuffer_add(buf, header->val, header->vlen);
    evbuffer_add(buf, "\r\n", 2);
    return 1;
}

/**
 * @brief dump_request_cb The callback of a dump request.
 *
 * @param req The request you want to dump.
 * @param arg It is not useful.
 */
void dump_request_cb(evhtp_request_t *req, void *arg)
{
    const char *uri = req->uri->path->full;

    //switch (evhtp_request_t_get_command(req)) {
    int req_method = evhtp_request_get_method(req);
    if(req_method >= 16)
	req_method = 16;

    LOG_PRINT(LOG_INFO, "Received a %s request for %s", method_strmap[req_method], uri);
    evbuffer_add_printf(req->buffer_out, "uri : %s\r\n", uri);
    evbuffer_add_printf(req->buffer_out, "query : %s\r\n", req->uri->query_raw);
    evhtp_headers_for_each(req->uri->query, print_headers, req->buffer_out);
    evbuffer_add_printf(req->buffer_out, "Method : %s\n", method_strmap[req_method]);
    evhtp_headers_for_each(req->headers_in, print_headers, req->buffer_out);

    evbuf_t *buf = req->buffer_in;;
    puts("Input data: <<<");
    while (evbuffer_get_length(buf)) {
	int n;
	char cbuf[128];
	n = evbuffer_remove(buf, cbuf, sizeof(buf)-1);
	if (n > 0)
	    (void) fwrite(cbuf, 1, n, stdout);
    }
    puts(">>>");

    //evhtp_headers_add_header(req->headers_out, evhtp_header_new("Server", "zimg/1.0.0 (Unix) (OpenSUSE/Linux)", 0, 0));
    evhtp_headers_add_header(req->headers_out, evhtp_header_new("Server", server_name, 0, 0));
    evhtp_headers_add_header(req->headers_out, evhtp_header_new("Content-Type", "text/plain", 0, 0));
    evhtp_send_reply(req, EVHTP_RES_OK);
}

/**
 * @brief echo_cb Callback function of echo test.
 *
 * @param req The request of a test url.
 * @param arg It is not useful.
 */
void echo_cb(evhtp_request_t *req, void *arg)
{
    evbuffer_add_printf(req->buffer_out, "<html><body><h1>zimg works!</h1></body></html>");
    //evhtp_headers_add_header(req->headers_out, evhtp_header_new("Server", "zimg/1.0.0 (Unix) (OpenSUSE/Linux)", 0, 0));
    evhtp_headers_add_header(req->headers_out, evhtp_header_new("Server", server_name, 0, 0));
    evhtp_headers_add_header(req->headers_out, evhtp_header_new("Content-Type", "text/html", 0, 0));
    evhtp_send_reply(req, EVHTP_RES_OK);
}


/**
 * @brief post_request_cb The callback function of a POST request to upload a image.
 *
 * @param req The request with image buffer.
 * @param arg It is not useful.
 */
void post_request_cb(evhtp_request_t *req, void *arg)
{
/**
Sample Post Data
Header:
Content-Length:50108
Content-Type:multipart/form-data; boundary=----WebKitFormBoundaryhIgUVzoG5V655hmr
Body:
------WebKitFormBoundaryhIgUVzoG5V655hmr
Content-Disposition: form-data; name="userfile"; filename="t.png"
Content-Type: image/png


------WebKitFormBoundaryhIgUVzoG5V655hmr--
*/

    //statement here for common goto err
    char *fileName = NULL;
    char *buff = NULL;
    char *boundaryPattern = NULL;

    //eheck request method
    int req_method = get_req_method(req);
    if(req_method != htp_method_POST)
    {
	LOG_PRINT(LOG_INFO, "Request Method Not Support.");
	goto err;
    }

    const char *p = NULL;
    const char *q = NULL;

    //check Content-Length
    p = evhtp_header_find(req->headers_in, "Content-Length");
    if (p == NULL)
    {
	LOG_PRINT(LOG_ERROR, "Content-Length error!");
	goto err;
    }
    int post_size = atoi(p);

    //check Content-Type
    p = evhtp_header_find(req->headers_in, "Content-Type");
    if (p == NULL)
    {
	LOG_PRINT(LOG_ERROR, "Content-Type error!");
	goto err;
    }

    if(strstr(p, "multipart/form-data") == 0)
    {
	LOG_PRINT(LOG_ERROR, "POST form error!");
	goto err;
    }

    p = strstr(p, "boundary=");
    if(p == 0)
    {
	LOG_PRINT(LOG_ERROR, "boundary NOT found!");
	goto err;
    }

    //find the boundary
    p += 9;
    if (strlen(p)<= 0)
    {
	LOG_PRINT(LOG_ERROR, "boundary length error!");
	goto err;
    }

    boundaryPattern = (char *)malloc(strlen(p) + 3);

    memcpy(boundaryPattern,"--",2);
    memcpy(boundaryPattern + 2,p,strlen(p));
    boundaryPattern[strlen(p) + 2] = '\0';
    LOG_PRINT(LOG_INFO, "boundary Find: boundary=%s", boundaryPattern + 4);
    LOG_PRINT(LOG_INFO, "boundaryPattern = %s, strlen = %d", boundaryPattern, strlen(boundaryPattern));

    //copy evhtp buffer
    evbuf_t *buf = req->buffer_in;
    buff = (char *)malloc(post_size);

    int rmblen, evblen;
    int img_size = 0;

    if(evbuffer_get_length(buf) <= 0)
    {
	LOG_PRINT(LOG_ERROR, "Empty Request!");
	goto err;
    }

    while((evblen = evbuffer_get_length(buf)) > 0)
    {
	LOG_PRINT(LOG_INFO, "evblen = %d", evblen);
	rmblen = evbuffer_remove(buf, buff, evblen);
	LOG_PRINT(LOG_INFO, "rmblen = %d", rmblen);
	if(rmblen < 0)
	{
	    LOG_PRINT(LOG_ERROR, "evbuffer_remove failed!");
	    goto err;
	}
    }

    //find the fileName
    p = strstr(buff,"filename=");
    if (p == NULL)
    {
	LOG_PRINT(LOG_ERROR, "Content-Disposition Not Found!");
	goto err;
    }

    p += 9;
    if(p[0] == '\"')
    {
	p++;
	q = strstr(p,"\"");
    }
    else
    {
	q = strstr(p,"\r\n");
    }

    if(q == NULL)
    {
	LOG_PRINT(LOG_ERROR, "quote \" or \\r\\n Not Found!");
	goto err;
    }

    fileName = (char *)malloc((q - p)+ 1);
    memcpy(fileName, p, q - p);
    fileName[q - p] = '\0';
    LOG_PRINT(LOG_INFO, "fileName = %s", fileName);

    //check file extension
    char fileExt[32];
    if(get_ext(fileName, fileExt) == -1)
    {
	LOG_PRINT(LOG_ERROR, "Get Type of File[%s] Failed!", fileName);
	goto err;
    }

    if(is_img(fileExt) != 1)
    {
	LOG_PRINT(LOG_ERROR, "fileExt[%s] is Not Supported!", fileExt);
	goto err;
    }

    p = q + 2;
    //check Content-Type in body
    if(strstr(p,"Content-Type") == NULL)
    {
	LOG_PRINT(LOG_ERROR, "Content-Type Not Found!");
	goto err;
    }

    q = strstr(p,"\r\n");
    if(p == NULL)
    {
	LOG_PRINT(LOG_ERROR, "Content-Type ERROR!");
	goto err;
    }
    p = q + 2;

    //the following is binary data,you CAN NOT use string function which is not binary safe!
    int m = kmp(p,post_size + (p - buff),boundaryPattern,strlen(boundaryPattern));
    q = p + m;
    if(q == NULL)
    {
	LOG_PRINT(LOG_ERROR, "Image Not complete!");
	goto err;
    }
    img_size = q - p - 2;


    LOG_PRINT(LOG_INFO, "post_size = %d", post_size);
    LOG_PRINT(LOG_INFO, "img_size = %d", img_size);
    if(img_size <= 0)
    {
	LOG_PRINT(LOG_ERROR, "Image Size is Zero!");
	goto err;
    }

    char md5sum[33];

    LOG_PRINT(LOG_INFO, "Begin to Save Image...");
    if(save_img(p, img_size, md5sum) == -1)
    {
	LOG_PRINT(LOG_ERROR, "Image Save Failed!");
	goto err;
    }

    evbuffer_add_printf(req->buffer_out, "{\"status\":0,\"picture\":%s}",md5sum);
    evhtp_headers_add_header(req->headers_out, evhtp_header_new("Server", server_name, 0, 0));
    evhtp_headers_add_header(req->headers_out, evhtp_header_new("Content-Type", "application/json", 0, 0));
    evhtp_send_reply(req, EVHTP_RES_OK);
    return;

err:
    //clean up
    if(fileName != NULL)
    {
	free(fileName);
    }

    if(boundaryPattern != NULL)
    {
	free(boundaryPattern);
    }

    if(buff != NULL)
    {
	free(buff);
    }

    LOG_PRINT(LOG_INFO, "============post_request_cb() ERROR!===============");
    evbuffer_add_printf(req->buffer_out, "{\"status\":-1}"); 
    evhtp_headers_add_header(req->headers_out, evhtp_header_new("Server", server_name, 0, 0));
    evhtp_headers_add_header(req->headers_out, evhtp_header_new("Content-Type", "application/json", 0, 0));
    evhtp_send_reply(req, EVHTP_RES_200);
}


/**
 * @brief send_document_cb The callback function of get a image request.
 *
 * @param req The request with a list of params and the md5 of image.
 * @param arg It is not useful.
 */
void send_document_cb(evhtp_request_t *req, void *arg)
{
    char *md5 = NULL;
    size_t len;
    zimg_req_t *zimg_req = NULL;
    char *buff = NULL;

    int req_method = get_req_method(req);
    if(req_method == htp_method_POST){
	LOG_PRINT(LOG_INFO, "POST Request.");
	post_request_cb(req, NULL);
	return;
    }
    else if(req_method != htp_method_GET)
    {
	LOG_PRINT(LOG_INFO, "Request Method Not Support.");
	goto err;
    }


    const char *uri;
    uri = req->uri->path->full;
    const char *rfull = req->uri->path->full;
    const char *rpath = req->uri->path->path;
    const char *rfile= req->uri->path->file;
    LOG_PRINT(LOG_INFO, "uri->path->full: %s",  rfull);
    LOG_PRINT(LOG_INFO, "uri->path->path: %s",  rpath);
    LOG_PRINT(LOG_INFO, "uri->path->file: %s",  rfile);

    if(strlen(uri) == 1 && uri[0] == '/')
    {
	LOG_PRINT(LOG_INFO, "Root Request.");
	int fd = -1;
	struct stat st;
	if((fd = open(settings.root_path, O_RDONLY)) == -1)
	{
	    LOG_PRINT(LOG_WARNING, "Root_page Open Failed. Return Default Page.");
	    evbuffer_add_printf(req->buffer_out, "<html>\n<body>\n<h1>\nWelcome To zimg World!</h1>\n</body>\n</html>\n");
	}
	else
	{
	    if (fstat(fd, &st) < 0)
	    {
		/* Make sure the length still matches, now that we
		 * opened the file :/ */
		LOG_PRINT(LOG_WARNING, "Root_page Length fstat Failed. Return Default Page.");
		evbuffer_add_printf(req->buffer_out, "<html>\n<body>\n<h1>\nWelcome To zimg World!</h1>\n</body>\n</html>\n");
	    }
	    else
	    {
		evbuffer_add_file(req->buffer_out, fd, 0, st.st_size);
	    }
	}
	evbuffer_add_printf(req->buffer_out, "<html>\n </html>\n");
	//evhtp_headers_add_header(req->headers_out, evhtp_header_new("Server", "zimg/1.0.0 (Unix) (OpenSUSE/Linux)", 0, 0));
	evhtp_headers_add_header(req->headers_out, evhtp_header_new("Server", server_name, 0, 0));
	evhtp_headers_add_header(req->headers_out, evhtp_header_new("Content-Type", "text/html", 0, 0));
	evhtp_send_reply(req, EVHTP_RES_OK);
	LOG_PRINT(LOG_INFO, "============send_document_cb() DONE!===============");
	goto done;
    }

    if(strstr(uri, "favicon.ico"))
    {
	LOG_PRINT(LOG_INFO, "favicon.ico Request, Denied.");
	//evhtp_headers_add_header(req->headers_out, evhtp_header_new("Server", "zimg/1.0.0 (Unix) (OpenSUSE/Linux)", 0, 0));
	evhtp_headers_add_header(req->headers_out, evhtp_header_new("Server", server_name, 0, 0));
	evhtp_headers_add_header(req->headers_out, evhtp_header_new("Content-Type", "text/html", 0, 0));
	evhtp_send_reply(req, EVHTP_RES_OK);
	goto done;
    }
    LOG_PRINT(LOG_INFO, "Got a GET request for <%s>",  uri);

    /* Don't allow any ".."s in the path, to avoid exposing stuff outside
     * of the docroot.  This test is both overzealous and underzealous:
     * it forbids aceptable paths like "/this/one..here", but it doesn't
     * do anything to prevent symlink following." */
    if (strstr(uri, ".."))
	goto err;

    md5 = (char *)malloc(strlen(uri) + 1);
    if(uri[0] == '/')
	strcpy(md5, uri+1);
    else
	strcpy(md5, uri);
    LOG_PRINT(LOG_INFO, "md5 of request is <%s>",  md5);
    if(is_md5(md5) == -1)
    {
	LOG_PRINT(LOG_WARNING, "Url is Not a zimg Request.");
	goto err;
    }
    /* This holds the content we're sending. */

    int width, height, proportion, gray;
    evhtp_kvs_t *params;
    params = req->uri->query;
    if(!params)
    {
	width = 0;
	height = 0;
	proportion = 1;
	gray = 0;
    }
    else
    {
	const char *str_w, *str_h;
	str_w = evhtp_kv_find(params, "w");
	if(str_w == NULL)
	    str_w = "0";
	str_h = evhtp_kv_find(params, "h");
	if(str_h == NULL)
	    str_h = "0";
	LOG_PRINT(LOG_INFO, "w() = %s; h() = %s;", str_w, str_h);
	if(strcmp(str_w, "g") == 0 && strcmp(str_h, "w") == 0)
	{
	    LOG_PRINT(LOG_INFO, "Love is Eternal.");
	    evbuffer_add_printf(req->buffer_out, "<html>\n <head>\n"
		    "  <title>Love is Eternal</title>\n"
		    " </head>\n"
		    " <body>\n"
		    "  <h1>Single1024</h1>\n"
		    "Since 2008-12-22, there left no room in my heart for another one.</br>\n"
		    "</body>\n</html>\n"
		    );
	    //evhtp_headers_add_header(req->headers_out, evhtp_header_new("Server", "zimg/1.0.0 (Unix) (OpenSUSE/Linux)", 0, 0));
	    evhtp_headers_add_header(req->headers_out, evhtp_header_new("Server", server_name, 0, 0));
	    evhtp_headers_add_header(req->headers_out, evhtp_header_new("Content-Type", "text/html", 0, 0));
	    evhtp_send_reply(req, EVHTP_RES_OK);
	    LOG_PRINT(LOG_INFO, "============send_document_cb() DONE!===============");
	    goto done;
	}
	else
	{
	    width = atoi(str_w);
	    height = atoi(str_h);
	    const char *str_p = evhtp_kv_find(params, "p");
	    const char *str_g = evhtp_kv_find(params, "g");
	    if(str_p)
		proportion = atoi(str_p);
	    else
		proportion = 1;
	    if(str_g)
		gray = atoi(str_g);
	    else
		gray = 0;
	}
    }

    zimg_req = (zimg_req_t *)malloc(sizeof(zimg_req_t)); 
    zimg_req -> md5 = md5;
    zimg_req -> width = width;
    zimg_req -> height = height;
    zimg_req -> proportion = proportion;
    zimg_req -> gray = gray;

    int get_img_rst = get_img(zimg_req, &buff,  &len);


    if(get_img_rst == -1)
    {
	LOG_PRINT(LOG_ERROR, "zimg Requset Get Image[MD5: %s] Failed!", zimg_req->md5);
	goto err;
    }

    LOG_PRINT(LOG_INFO, "get buffer length: %d", len);
    evbuffer_add(req->buffer_out, buff, len);

    LOG_PRINT(LOG_INFO, "Got the File!");
    //evhtp_headers_add_header(req->headers_out, evhtp_header_new("Server", "zimg/1.0.0 (Unix) (OpenSUSE/Linux)", 0, 0));
    evhtp_headers_add_header(req->headers_out, evhtp_header_new("Server", server_name, 0, 0));
    evhtp_headers_add_header(req->headers_out, evhtp_header_new("Content-Type", "image/jpeg", 0, 0));
    evhtp_send_reply(req, EVHTP_RES_OK);
    LOG_PRINT(LOG_INFO, "============send_document_cb() DONE!===============");


    if(get_img_rst == 2)
    {
	if(new_img(buff, len, zimg_req->rsp_path) == -1)
	{
	    LOG_PRINT(LOG_WARNING, "New Image[%s] Save Failed!", zimg_req->rsp_path);
	}
    }
    goto done;

err:
    evbuffer_add_printf(req->buffer_out, "<html><body><h1>404 Not Found!</h1></body></html>");
    //evhtp_headers_add_header(req->headers_out, evhtp_header_new("Server", "zimg/1.0.0 (Unix) (OpenSUSE/Linux)", 0, 0));
    evhtp_headers_add_header(req->headers_out, evhtp_header_new("Server", server_name, 0, 0));
    evhtp_headers_add_header(req->headers_out, evhtp_header_new("Content-Type", "text/html", 0, 0));
    evhtp_send_reply(req, EVHTP_RES_NOTFOUND);
    LOG_PRINT(LOG_INFO, "============send_document_cb() ERROR!===============");

done:
    if(buff)
	free(buff);
    if(zimg_req)
    {
	if(zimg_req->md5)
	    free(zimg_req->md5);
	if(zimg_req->rsp_path)
	    free(zimg_req->rsp_path);
	free(zimg_req);
    }
}

