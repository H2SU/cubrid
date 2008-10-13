/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 * 
 * List files (Server Side)
 */

#ifndef _QP_LSSR_H_
#define _QP_LSSR_H_

#ident "$Id$"

#include "config.h"

#include <stdio.h>

#include "dbtype.h"
#include "common.h"
#include "system_parameter.h"
#include "external_sort.h"
#include "qp_xasl.h"
#include "qp_list.h"
#include "query_evaluator.h"
#include "logcp.h"
#include "object_domain.h"
#include "thread_impl.h"

#define QFILE_IS_LIST_CACHE_DISABLED \
  (PRM_LIST_QUERY_CACHE_MODE == 0 || PRM_LIST_MAX_QUERY_CACHE_ENTRIES <= 0)

typedef struct qfile_page_header QFILE_PAGE_HEADER;
struct qfile_page_header
{
  int pg_tplcnt;		/* tuple count for the page */
  PAGEID prev_pgid;		/* previous page identifier */
  PAGEID next_pgid;		/* next page identifier */
  int lasttpl_off;		/* offset value of the last tuple */
  PAGEID ovfl_pgid;		/* overflow page identifier */
  VOLID prev_volid;		/* previous page volume identifier */
  VOLID next_volid;		/* next page volume identifier */
  VOLID ovfl_volid;		/* overflow page volume identifier */
};

/* query result(list file) cache entry type definition */
typedef struct qfile_list_cache_entry QFILE_LIST_CACHE_ENTRY;
struct qfile_list_cache_entry
{
  int list_ht_no;		/* list_ht no to which this entry belongs */
  DB_VALUE_ARRAY param_values;	/* parameter values bound to this result */
  QFILE_LIST_ID list_id;	/* list file(query result) identifier */
#if defined(SERVER_MODE)
  QFILE_LIST_CACHE_ENTRY *tran_next;	/* next entry in the transaction list */
  bool uncommitted_marker;	/* the transaction that made this entry is not
				   committed yet */
  TRAN_ISOLATION tran_isolation;	/* isolation level of the transaction
					   which made this result */
  int *tran_index_array;	/* array of TID(tran index)s that are currently
				   using this list file; size is MAX_NTRANS */
  int last_ta_idx;		/* index of the last element in TIDs array */
#endif				/* SERVER_MODE */
  const char *query_string;	/* query string; information purpose only */
  struct timeval time_created;	/* when this entry created */
  struct timeval time_last_used;	/* when this entry used lastly */
  int ref_count;		/* how many times this query used */
  bool deletion_marker;		/* this entry will be deleted if marker set */
};

#if defined(SERVER_MODE)
/* in xserver.h */
/* TODO: fix header file */
extern int xqfile_get_list_file_page (THREAD_ENTRY * thread_p, int query_id,
				      VOLID volid, PAGEID pageid,
				      char *page_bufp, int *page_sizep);
#endif /* SERVER_MODE */

/* List manipulation routines */
extern int qfile_initialize (void);
extern void qfile_finalize (void);
extern void qfile_destroy_list (THREAD_ENTRY * thread_p,
				QFILE_LIST_ID * list_id);
extern void qfile_close_list (THREAD_ENTRY * thread_p,
			      QFILE_LIST_ID * list_id);
extern int qfile_add_tuple_to_list (THREAD_ENTRY * thread_p,
				    QFILE_LIST_ID * list_id, QFILE_TUPLE tpl);
extern int qfile_add_overflow_tuple_to_list (THREAD_ENTRY * thread_p,
					     QFILE_LIST_ID * list_id,
					     PAGE_PTR ovfl_tpl_pg,
					     QFILE_LIST_ID * input_list_id);
extern int qfile_get_first_page (THREAD_ENTRY * thread_p,
				 QFILE_LIST_ID * list_id);

/* Copy routines */
extern int qfile_copy_list_id (QFILE_LIST_ID * dest_list_id,
			       const QFILE_LIST_ID * src_list_id,
			       bool include_sort_list);
extern QFILE_LIST_ID *qfile_clone_list_id (const QFILE_LIST_ID * list_id,
					   bool include_sort_list);

/* Free routines */
extern void qfile_free_list_id (QFILE_LIST_ID * list_id);
extern void qfile_free_sort_list (SORT_LIST * sort_list);

/* Alloc routines */
extern SORT_LIST *qfile_allocate_sort_list (int cnt);

/* sort_list related routines */
extern bool qfile_is_sort_list_covered (SORT_LIST * covering_list,
					SORT_LIST * covered_list);

/* Sorting related routines */
extern SORT_STATUS qfile_make_sort_key (THREAD_ENTRY * thread_p,
					SORTKEY_INFO * info, RECDES * key,
					QFILE_LIST_SCAN_ID * input_scan,
					QFILE_TUPLE_RECORD * tplrec);
extern QFILE_TUPLE qfile_generate_sort_tuple (SORTKEY_INFO * info,
					      SORT_REC * sort_rec,
					      RECDES * output_recdes);
extern int qfile_compare_partial_sort_record (const void *pk0,
					      const void *pk1, void *arg);
extern int qfile_compare_all_sort_record (const void *pk0, const void *pk1,
					  void *arg);
extern int qfile_get_estimated_pages_for_sorting (QFILE_LIST_ID * listid,
						  SORTKEY_INFO * info);
extern SORTKEY_INFO *qfile_initialize_sort_key_info (SORTKEY_INFO * info,
						     SORT_LIST * list,
						     QFILE_TUPLE_VALUE_TYPE_LIST
						     * types);
extern void qfile_clear_sort_key_info (SORTKEY_INFO * info);
extern QFILE_LIST_ID *qfile_sort_list_with_func (THREAD_ENTRY * thread_p,
						 QFILE_LIST_ID * list_id,
						 SORT_LIST * sort_list,
						 QUERY_OPTIONS option,
						 int ls_flag,
						 SORT_GET_FUNC * get_fn,
						 SORT_PUT_FUNC * put_fn,
						 SORT_CMP_FUNC * cmp_fn,
						 void *extra_arg);
extern QFILE_LIST_ID *qfile_sort_list (THREAD_ENTRY * thread_p,
				       QFILE_LIST_ID * list_id,
				       SORT_LIST * sort_list,
				       QUERY_OPTIONS option);

/* Query result(list file) cache routines */
extern int qfile_initialize_list_cache (THREAD_ENTRY * thread_p);
extern int qfile_finalize_list_cache (THREAD_ENTRY * thread_p);
extern int qfile_clear_list_cache (THREAD_ENTRY * thread_p, int list_ht_no,
				   bool release);
extern int qfile_dump_list_cache_internal (THREAD_ENTRY * thread_p,
					   FILE * fp);
extern int qfile_dump_list_cache (THREAD_ENTRY * thread_p, const char *fname);

/* query result(list file) cache entry manipulation functions */
void qfile_clear_uncommited_list_cache_entry (int tran_index);
QFILE_LIST_CACHE_ENTRY *qfile_lookup_list_cache_entry (THREAD_ENTRY *
						       thread_p,
						       int list_ht_no,
						       const DB_VALUE_ARRAY *
						       params);
QFILE_LIST_CACHE_ENTRY *qfile_update_list_cache_entry (THREAD_ENTRY *
						       thread_p,
						       int *list_ht_no_ptr,
						       const DB_VALUE_ARRAY *
						       params,
						       const QFILE_LIST_ID *
						       list_id,
						       const char
						       *query_string);
int qfile_end_use_of_list_cache_entry (THREAD_ENTRY * thread_p,
				       QFILE_LIST_CACHE_ENTRY * lent,
				       bool marker);

/* Scan related routines */
extern int qfile_modify_type_list (QFILE_TUPLE_VALUE_TYPE_LIST * type_list,
				   QFILE_LIST_ID * list_id);
extern void qfile_clear_list_id (QFILE_LIST_ID * list_id);
extern int qfile_store_xasl (THREAD_ENTRY * thread_p, const char *xasl,
			     int size, XASL_ID * xasl_id);
extern int qfile_load_xasl (THREAD_ENTRY * thread_p, const XASL_ID * xasl_id,
			    char **xasl, int *size);
extern QFILE_LIST_ID *qfile_open_list (THREAD_ENTRY * thread_p,
				       QFILE_TUPLE_VALUE_TYPE_LIST *
				       type_list, SORT_LIST * sort_list,
				       int query_id, int flag);
extern int qfile_generate_tuple_into_list (THREAD_ENTRY * thread_p,
					   QFILE_LIST_ID * list_id,
					   QFILE_TUPLE_TYPE tpl_type);
extern int qfile_add_item_to_list (THREAD_ENTRY * thread_p, char *item,
				   int item_size, QFILE_LIST_ID * list_id);
extern QFILE_LIST_ID *qfile_combine_two_list (THREAD_ENTRY * thread_p,
					      QFILE_LIST_ID * lhs_file,
					      QFILE_LIST_ID * rhs_file,
					      int flag);
extern int qfile_reallocate_tuple (QFILE_TUPLE_RECORD * tplrec,
				   long tpl_size);
extern void qfile_print_list (THREAD_ENTRY * thread_p,
			      QFILE_LIST_ID * list_id);
extern QFILE_LIST_ID *qfile_duplicate_list (THREAD_ENTRY * thread_p,
					    QFILE_LIST_ID * list_id,
					    int flag);
extern int qfile_get_tuple (THREAD_ENTRY * thread_p, PAGE_PTR first_page,
			    QFILE_TUPLE tuplep, QFILE_TUPLE_RECORD * tplrec,
			    QFILE_LIST_ID * list_idp);
extern void qfile_save_current_scan_tuple_position (QFILE_LIST_SCAN_ID * s_id,
						    QFILE_TUPLE_POSITION *
						    ls_tplpos);
extern SCAN_CODE qfile_jump_scan_tuple_position (THREAD_ENTRY * thread_p,
						 QFILE_LIST_SCAN_ID * s_id,
						 QFILE_TUPLE_POSITION *
						 ls_tplpos,
						 QFILE_TUPLE_RECORD * tplrec,
						 int peek);
extern int qfile_start_scan_fix (THREAD_ENTRY * thread_p,
				 QFILE_LIST_SCAN_ID * s_id);
extern int qfile_open_list_scan (QFILE_LIST_ID * list_id,
				 QFILE_LIST_SCAN_ID * s_id);
extern SCAN_CODE qfile_scan_list_next (THREAD_ENTRY * thread_p,
				       QFILE_LIST_SCAN_ID * s_id,
				       QFILE_TUPLE_RECORD * tplrec, int peek);
extern SCAN_CODE qfile_scan_list_prev (THREAD_ENTRY * thread_p,
				       QFILE_LIST_SCAN_ID * s_id,
				       QFILE_TUPLE_RECORD * tplrec, int peek);
extern void qfile_end_scan_fix (THREAD_ENTRY * thread_p,
				QFILE_LIST_SCAN_ID * s_id);
extern void qfile_close_scan (THREAD_ENTRY * thread_p,
			      QFILE_LIST_SCAN_ID * s_id);

/* Miscellaneous */
extern QFILE_TUPLE_VALUE_FLAG qfile_locate_tuple_value (QFILE_TUPLE tpl,
							int index,
							char **tpl_val,
							int *val_size);
extern QFILE_TUPLE_VALUE_FLAG qfile_locate_tuple_value_r (QFILE_TUPLE tpl,
							  int index,
							  char **tpl_val,
							  int *val_size);
#endif /* _QP_LSSR_H_ */
