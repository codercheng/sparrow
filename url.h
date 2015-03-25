/*******************************************************************
 *  url编码和解码，解决url中包含中文的问题.
 *  参考博客:http://blog.csdn.net/langeldep/article/details/6264058
 *  本人做了少量的修改.
 *******************************************************************/

#ifndef _URL_H
#define _URL_H

#ifdef __cplusplus
extern "C" {
#endif

	int url_decode(char *str, int len);
	char *url_encode(char const *s, int len, int *new_length);

#ifdef __cplusplus
}
#endif

#endif /* URL_H */
