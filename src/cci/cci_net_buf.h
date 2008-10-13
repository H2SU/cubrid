/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * cci_net_buf.h - 
 */

#ifndef	_CCI_NET_BUF_H_
#define	_CCI_NET_BUF_H_

#ident "$Id$"

#ifdef CAS
#error include error
#endif

/************************************************************************
 * IMPORTED SYSTEM HEADER FILES						*
 ************************************************************************/

/************************************************************************
 * IMPORTED OTHER HEADER FILES						*
 ************************************************************************/

/************************************************************************
 * EXPORTED DEFINITIONS							*
 ************************************************************************/

#if (defined(SOLARIS) && !defined(SOLARIS_X86)) || defined(HPUX) || defined(AIX)
#define BYTE_ORDER_BIG_ENDIAN
#elif defined(WIN32) || defined(LINUX) || defined(SOLARIS_X86)
#ifdef BYTE_ORDER_BIG_ENDIAN
#error BYTE_ORDER_BIG_ENDIAN defined
#endif
#else
#error PLATFORM NOT DEFINED
#endif

#ifdef BYTE_ORDER_BIG_ENDIAN
#define htonf(X)                (X)
#define htond(X)                (X)
#define ntohf(X)                (X)
#define ntohd(X)                (X)
#else
#define ntohf(X)		htonf(X)
#define ntohd(X)		htond(X)
#endif

/*
 *  change function names to avoid naming conflict with cas server.
  */
#define net_buf_init		cnet_buf_init
#define net_buf_clear		cnet_buf_clear
#define net_buf_cp_str		cnet_buf_cp_str
#define net_buf_cp_int		cnet_buf_cp_int
#define net_buf_cp_float	cnet_buf_cp_float
#define net_buf_cp_double	cnet_buf_cp_double
#define net_buf_cp_short	cnet_buf_cp_short
#ifndef BYTE_ORDER_BIG_ENDIAN
#define htonf			cnet_buf_htonf
#define htond			cnet_buf_htond
#endif

/************************************************************************
 * EXPORTED TYPE DEFINITIONS						*
 ************************************************************************/

typedef struct
{
  int alloc_size;
  int data_size;
  char *data;
  int err_code;
} T_NET_BUF;

/************************************************************************
 * EXPORTED FUNCTION PROTOTYPES						*
 ************************************************************************/

extern void cnet_buf_init (T_NET_BUF *);
extern void cnet_buf_clear (T_NET_BUF *);
extern int cnet_buf_cp_str (T_NET_BUF *, char *, int);
extern int cnet_buf_cp_int (T_NET_BUF *, int);
extern int cnet_buf_cp_float (T_NET_BUF *, float);
extern int cnet_buf_cp_double (T_NET_BUF *, double);
extern int cnet_buf_cp_short (T_NET_BUF *, short);

#ifndef BYTE_ORDER_BIG_ENDIAN
extern float cnet_buf_htonf (float from);
extern double cnet_buf_htond (double from);
#endif

/************************************************************************
 * EXPORTED VARIABLES							*
 ************************************************************************/

#endif /* _CCI_NET_BUF_H_ */
