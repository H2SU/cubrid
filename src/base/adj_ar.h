/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * adj_ar.h : adjustable array definitions
 *
 */

#ifndef _ADJ_AR_H_
#define _ADJ_AR_H_

#ident "$Id$"

#include <stdio.h>
#include <stdlib.h>

#define ADJ_AR_EOA -1

typedef enum adj_err_code ADJ_ERR_CODE;
enum adj_err_code
{
  ADJ_NOERROR = 0,
  ADJ_ERR_BAD_START = -1,
  ADJ_ERR_BAD_END = -2,
  ADJ_ERR_BAD_NFROM = -3,
  ADJ_ERR_BAD_ALLOC = -4,
  ADJ_ERR_BAD_ELEMENT = -5,
  ADJ_ERR_BAD_MIN = -6,
  ADJ_ERR_BAD_RATE = -7,
  ADJ_ERR_BAD_INIT = -8,
  ADJ_ERR_BAD_INITIAL = -9,
  ADJ_ERR_BAD_LENGTH = -10,
  ADJ_ERR_BAD_ADJ_ARR_PTR = -99
};

typedef struct adj_array ADJ_ARRAY;
struct adj_array
{
  size_t cur_length;		/* current array length */
  void *buffer;			/* current array buffer */
  size_t max_length;		/* maximum elements in buffer */
  size_t min_length;		/* minimum elements in buffer */
  int element_size;		/* size of array element in bytes */
  float rate;			/* growth rate (>= 1.0) */
};

extern const char *adj_ar_concat_strings (const char *string1,
					  const char *string2, ...);

extern ADJ_ARRAY *adj_ar_new (int element_size, int min, float growth_rate);

extern void adj_ar_free (ADJ_ARRAY * adj_array_p);

extern int adj_ar_reset (ADJ_ARRAY * adj_array_p, int element_size, int min,
			 float growth_rate);

extern int adj_ar_initialize (ADJ_ARRAY * adj_array_p, const void *initial,
			      int initial_length);

extern int adj_ar_replace (ADJ_ARRAY * adj_array_p, const void *src,
			   int src_length, int start, int end);

extern int adj_ar_remove (ADJ_ARRAY * adj_array_p, int start, int end);

extern int adj_ar_insert (ADJ_ARRAY * adj_array_p, const void *src,
			  int src_length, int start);

extern int adj_ar_append (ADJ_ARRAY * adj_array_p, const void *src,
			  int src_length);

extern void *adj_ar_get_buffer (const ADJ_ARRAY * adj_array_p);

extern size_t adj_ar_length (const ADJ_ARRAY * adj_array_p);

extern void *adj_ar_get_nth_buffer (const ADJ_ARRAY * adj_array_p, int n);

#endif /* _ADJ_AR_H_ */
