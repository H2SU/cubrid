/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * cci_net_buf.c - 
 */

#ident "$Id$"

/************************************************************************
 * IMPORTED SYSTEM HEADER FILES						*
 ************************************************************************/

#include <string.h>
#include <stdlib.h>

#ifdef WIN32
#include <winsock2.h>
#include <windows.h>
#else
#include <arpa/inet.h>
#endif

/************************************************************************
 * OTHER IMPORTED HEADER FILES						*
 ************************************************************************/

#include "cas_cci.h"
#include "cci_common.h"
#include "cci_net_buf.h"

/************************************************************************
 * PRIVATE DEFINITIONS							*
 ************************************************************************/

/************************************************************************
 * PRIVATE TYPE DEFINITIONS						*
 ************************************************************************/

/************************************************************************
 * PRIVATE FUNCTION PROTOTYPES						*
 ************************************************************************/

static int net_buf_realloc (T_NET_BUF * net_buf, int size);

/************************************************************************
 * INTERFACE VARIABLES							*
 ************************************************************************/

/************************************************************************
 * PUBLIC VARIABLES							*
 ************************************************************************/

/************************************************************************
 * PRIVATE VARIABLES							*
 ************************************************************************/

/************************************************************************
 * IMPLEMENTATION OF INTERFACE FUNCTIONS 				*
 ************************************************************************/

/************************************************************************
 * IMPLEMENTATION OF PUBLIC FUNCTIONS	 				*
 ************************************************************************/

void
cnet_buf_init (T_NET_BUF * net_buf)
{
  net_buf->alloc_size = 0;
  net_buf->data_size = 0;
  net_buf->data = NULL;
  net_buf->err_code = 0;
}

void
cnet_buf_clear (T_NET_BUF * net_buf)
{
  FREE_MEM (net_buf->data);
  net_buf_init (net_buf);
}

int
cnet_buf_cp_str (T_NET_BUF * net_buf, char *buf, int size)
{
  if (size <= 0)
    return 0;

  if (net_buf_realloc (net_buf, size) < 0)
    return CCI_ER_NO_MORE_MEMORY;

  memcpy (net_buf->data + net_buf->data_size, buf, size);
  net_buf->data_size += size;
  return 0;
}

int
cnet_buf_cp_int (T_NET_BUF * net_buf, int value)
{
  if (net_buf_realloc (net_buf, 4) < 0)
    return CCI_ER_NO_MORE_MEMORY;

  value = htonl (value);
  memcpy (net_buf->data + net_buf->data_size, &value, 4);
  net_buf->data_size += 4;

  return 0;
}

int
cnet_buf_cp_float (T_NET_BUF * net_buf, float value)
{
  if (net_buf_realloc (net_buf, 4) < 0)
    return CCI_ER_NO_MORE_MEMORY;
  value = htonf (value);
  memcpy (net_buf->data + net_buf->data_size, &value, 4);
  net_buf->data_size += 4;

  return 0;
}

int
cnet_buf_cp_double (T_NET_BUF * net_buf, double value)
{
  if (net_buf_realloc (net_buf, 8) < 0)
    return CCI_ER_NO_MORE_MEMORY;
  value = htond (value);
  memcpy (net_buf->data + net_buf->data_size, &value, 8);
  net_buf->data_size += 8;

  return 0;
}

int
cnet_buf_cp_short (T_NET_BUF * net_buf, short value)
{
  if (net_buf_realloc (net_buf, 2) < 0)
    return CCI_ER_NO_MORE_MEMORY;
  value = htons (value);
  memcpy (net_buf->data + net_buf->data_size, &value, 2);
  net_buf->data_size += 2;

  return 0;
}

#ifndef BYTE_ORDER_BIG_ENDIAN
float
cnet_buf_htonf (float from)
{
  float to;
  char *p, *q;

  p = (char *) &from;
  q = (char *) &to;

  q[0] = p[3];
  q[1] = p[2];
  q[2] = p[1];
  q[3] = p[0];

  return to;
}

double
cnet_buf_htond (double from)
{
  double to;
  char *p, *q;

  p = (char *) &from;
  q = (char *) &to;

  q[0] = p[7];
  q[1] = p[6];
  q[2] = p[5];
  q[3] = p[4];
  q[4] = p[3];
  q[5] = p[2];
  q[6] = p[1];
  q[7] = p[0];

  return to;
}
#endif

/************************************************************************
 * IMPLEMENTATION OF PRIVATE FUNCTIONS	 				*
 ************************************************************************/

static int
net_buf_realloc (T_NET_BUF * net_buf, int size)
{
  int new_alloc_size;

  if (size + net_buf->data_size > net_buf->alloc_size)
    {
      new_alloc_size = net_buf->alloc_size + 1024;
      if (size + net_buf->data_size > new_alloc_size)
	{
	  new_alloc_size = size + net_buf->data_size;
	}
      net_buf->data = (char *) REALLOC (net_buf->data, new_alloc_size);
      if (net_buf->data == NULL)
	{
	  net_buf->alloc_size = 0;
	  net_buf->err_code = CCI_ER_NO_MORE_MEMORY;
	  return -1;
	}

      net_buf->alloc_size = new_alloc_size;
    }

  return 0;
}
