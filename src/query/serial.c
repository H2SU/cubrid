/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * qp_serial.c - Serial number handling routine 
 *
 * Note: if you feel the need
 */

#ident "$Id$"

#include "config.h"

#include <assert.h>
#include <errno.h>

#include "intl.h"
#include "error_code.h"
#include "db.h"
#include "memory_manager_4.h"
#include "arithmetic.h"
#include "query_evaluator.h"
#include "heap_file.h"
#include "page_buffer.h"
#include "log.h"
#include "transaction_sr.h"
#include "replication.h"
#include "xserver.h"

/*
 * xqp_get_serial_current_value () -
 *   return: NO_ERROR, or ER_code 
 *   oid_str_val(in)    :
 *   result_num(in)     :
 */
int
xqp_get_serial_current_value (THREAD_ENTRY * thread_p,
			      const DB_VALUE * oid_str_val,
			      DB_VALUE * result_num)
{
  int ret = NO_ERROR;
  const char *oid_str = NULL;
  int pageid, slotid, volid;
  OID serial_oid, serial_class_oid;
  VPID vpid;
  PAGE_PTR pgptr;
  SCAN_CODE scan;
  INT16 type;
  LC_COPYAREA *copyarea = NULL;
  RECDES recdesc;
  HEAP_CACHE_ATTRINFO attr_info;
  ATTR_ID attr_id;
  DB_VALUE *cur_val;
  bool page_locked = false;

  assert (oid_str_val != (DB_VALUE *) NULL);
  assert (result_num != (DB_VALUE *) NULL);

  oid_str = DB_GET_STRING (oid_str_val);
  sscanf (oid_str, "%d %d %d", &pageid, &slotid, &volid);
  serial_oid.pageid = pageid;
  serial_oid.slotid = slotid;
  serial_oid.volid = volid;

  vpid.volid = volid;
  vpid.pageid = pageid;

  /* lock and fetch page */
  pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_READ,
		     PGBUF_UNCONDITIONAL_LATCH);

  if (pgptr == NULL)
    {
      if (er_errid () == ER_PB_BAD_PAGEID)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HEAP_UNKNOWN_OBJECT,
		  3, volid, pageid, slotid);
	}

      goto exit_on_error;
    }
  page_locked = true;

  /* check record type  */
  type = spage_get_record_type (pgptr, serial_oid.slotid);
  if (type == REC_UNKNOWN)
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_HEAP_UNKNOWN_OBJECT, 3,
	      serial_oid.volid, serial_oid.pageid, serial_oid.slotid);
      goto exit_on_error;
    }

  /* get record into record desc */
  copyarea = locator_allocate_copy_area_by_length (DB_PAGESIZE);
  if (copyarea == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 0);
      goto exit_on_error;
    }

  recdesc.data = copyarea->mem;
  recdesc.area_size = copyarea->length;

  scan = spage_get_record (pgptr, serial_oid.slotid, &recdesc, COPY);
  if (scan != S_SUCCESS)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_CANNOT_FETCH_SERIAL,
	      0);
      goto exit_on_error;
    }
  or_class_oid (&recdesc, &serial_class_oid);

  /* retrieve attribute */
  attr_id = 2;
  ret =
    heap_attrinfo_start (thread_p, &serial_class_oid, 1, &attr_id,
			 &attr_info);
  if (ret != NO_ERROR)
    {
      goto exit_on_error;
    }
  ret =
    heap_attrinfo_read_dbvalues (thread_p, &serial_oid, &recdesc, &attr_info);
  if (ret != NO_ERROR)
    {
      goto exit_on_error;
    }
  cur_val = heap_attrinfo_access (attr_id, &attr_info);
  pr_clone_value (cur_val, result_num);
  heap_attrinfo_end (thread_p, &attr_info);

end:

  /* free copy area */
  if (copyarea)
    {
      locator_free_copy_area (copyarea);
    }

  /* free and unlock page */
  if (page_locked)
    {
      pgbuf_unfix (thread_p, pgptr);
    }

  return ret;

exit_on_error:

  if (ret == NO_ERROR)
    {
      ret = er_errid ();
      if (ret == NO_ERROR)
	{
	  ret = ER_FAILED;
	}
    }
  goto end;
}

/*
 * xqp_get_serial_next_value () -
 *   return: NO_ERROR, or ER_status 
 *   oid_str_val(in)    :
 *   result_num(in)     :
 */
int
xqp_get_serial_next_value (THREAD_ENTRY * thread_p,
			   const DB_VALUE * oid_str_val,
			   DB_VALUE * result_num)
{
  int ret = NO_ERROR;
  const char *oid_str = NULL;
  int pageid, slotid, volid;

  OID serial_oid, serial_class_oid;
  SCAN_CODE scan;
  VPID vpid;
  PAGE_PTR pgptr;
  INT16 type;
  LC_COPYAREA *copyarea = NULL;
  LC_COPYAREA *new_copyarea = NULL;
  int new_copyarea_length;
  RECDES recdesc;
  RECDES new_recdesc;
  HFID hfid;
  CLS_INFO *cls_info;

  HEAP_CACHE_ATTRINFO attr_info;
  ATTR_ID attr_id;
  DB_VALUE *val, tmp_val, cmp_result;
  DB_VALUE cur_val;
  DB_VALUE inc_val;
  DB_VALUE max_val;
  DB_VALUE min_val;
  DB_VALUE cyclic;
  DB_VALUE started;
  DB_VALUE next_val;
  DB_VALUE key_val;
  int savepoint_used = 0;
  LOG_LSA lsa;
  int inc_val_flag;

  bool page_locked = false;

  LOG_DATA_ADDR addr;
  int sp_success;
  LOG_CRUMB redo_crumbs[2];

  assert (oid_str_val != (DB_VALUE *) NULL);
  assert (result_num != (DB_VALUE *) NULL);

  DB_MAKE_NULL (&tmp_val);
  DB_MAKE_NULL (&next_val);

  oid_str = DB_GET_STRING (oid_str_val);
  sscanf (oid_str, "%d %d %d", &pageid, &slotid, &volid);
  serial_oid.pageid = pageid;
  serial_oid.slotid = slotid;
  serial_oid.volid = volid;

  vpid.volid = volid;
  vpid.pageid = pageid;

  /* need to start topop for replication
     Replication will recognize and realize a special type of update for serial
     by this top operation log record. */
  ret = xtran_server_start_topop (thread_p, &lsa);
  if (ret != NO_ERROR)
    {
      return ret;		/* error */
    }
  savepoint_used = 1;

#if !defined (WINDOWS)
  if (PRM_REPLICATION_MODE)
    {
      repl_start_flush_mark (thread_p);
    }
#endif /* WINDOWS */

  /* lock and fetch page */
  pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_WRITE,
		     PGBUF_UNCONDITIONAL_LATCH);
  if (pgptr == NULL)
    {
      if (er_errid () == ER_PB_BAD_PAGEID)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HEAP_UNKNOWN_OBJECT,
		  3, volid, pageid, slotid);
	}
      goto exit_on_error;
    }
  page_locked = true;


  /* check record type  */
  type = spage_get_record_type (pgptr, serial_oid.slotid);
  if (type == REC_UNKNOWN)
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_HEAP_UNKNOWN_OBJECT, 3,
	      serial_oid.volid, serial_oid.pageid, serial_oid.slotid);
      goto exit_on_error;
    }

  /* get record into record desc */
  copyarea = locator_allocate_copy_area_by_length (DB_PAGESIZE);
  if (copyarea == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 0);
      goto exit_on_error;
    }

  recdesc.data = copyarea->mem;
  recdesc.area_size = copyarea->length;

  scan = spage_get_record (pgptr, serial_oid.slotid, &recdesc, COPY);
  if (scan != S_SUCCESS)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_CANNOT_FETCH_SERIAL,
	      0);
      goto exit_on_error;
    }

  /*
   */
  if (recdesc.type != REC_HOME)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_CANNOT_FETCH_SERIAL,
	      0);
      goto exit_on_error;
    }

  or_class_oid (&recdesc, &serial_class_oid);
  cls_info = catalog_get_class_info (thread_p, &serial_class_oid);
  if (cls_info != NULL)
    {
      hfid = cls_info->hfid;
    }
  else
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_DB_SERIAL_NOT_FOUND,
	      0);
      goto exit_on_error;
    }
  catalog_free_class_info (cls_info);

  /* retrieve attribute */
  ret =
    heap_attrinfo_start (thread_p, &serial_class_oid, -1, NULL, &attr_info);
  if (ret != NO_ERROR)
    {
      goto exit_on_error;
    }
  ret =
    heap_attrinfo_read_dbvalues (thread_p, &serial_oid, &recdesc, &attr_info);
  if (ret != NO_ERROR)
    {
      goto exit_on_error;
    }
  attr_id = 0;
  val = heap_attrinfo_access (attr_id, &attr_info);
  pr_clone_value (val, &key_val);

  attr_id = 2;
  val = heap_attrinfo_access (attr_id, &attr_info);
  pr_clone_value (val, &cur_val);

  attr_id = 3;
  val = heap_attrinfo_access (attr_id, &attr_info);
  pr_clone_value (val, &inc_val);

  attr_id = 4;
  val = heap_attrinfo_access (attr_id, &attr_info);
  pr_clone_value (val, &max_val);

  attr_id = 5;
  val = heap_attrinfo_access (attr_id, &attr_info);
  pr_clone_value (val, &min_val);

  attr_id = 6;
  val = heap_attrinfo_access (attr_id, &attr_info);
  pr_clone_value (val, &cyclic);

  attr_id = 7;
  val = heap_attrinfo_access (attr_id, &attr_info);
  pr_clone_value (val, &started);

  if (DB_GET_INT (&started) == 0)
    {
      DB_MAKE_INT (&started, 1);
      ret = heap_attrinfo_set (&serial_oid, 7, &started, &attr_info);
      if (ret != NO_ERROR)
	{
	  goto exit_on_error;
	}
      pr_clone_value (&cur_val, &next_val);
    }
  else
    {
      ret = numeric_coerce_string_to_num ("0", &tmp_val);
      if (ret != NO_ERROR)
	{
	  goto exit_on_error;
	}
      DB_MAKE_INTEGER (&cmp_result, 0);
      ret = numeric_db_value_compare (&inc_val, &tmp_val, &cmp_result);
      if (ret != NO_ERROR)
	{
	  goto exit_on_error;
	}
      pr_clear_value (&tmp_val);
      inc_val_flag = DB_GET_INT (&cmp_result);

    /**********************************************************************
     * Now calculate next value 
     **********************************************************************/

      /* inc_val_flag >0 or <0 */
      if (inc_val_flag > 0)
	{
	  numeric_db_value_sub (&max_val, &inc_val, &tmp_val);
	  ret = numeric_db_value_compare (&cur_val, &tmp_val, &cmp_result);
	  if (ret != NO_ERROR)
	    {
	      goto exit_on_error;
	    }
	  pr_clear_value (&tmp_val);
	  /* cur_val + inc_val > max_val */
	  if (DB_GET_INT (&cmp_result) > 0)
	    {
	      if (DB_GET_INT (&cyclic))
		{
		  pr_clone_value (&min_val, &next_val);
		}
	      else
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			  ER_QPROC_SERIAL_RANGE_OVERFLOW, 0);
		  heap_attrinfo_end (thread_p, &attr_info);
		  goto exit_on_error;
		}
	    }
	  else
	    {
	      (void) numeric_db_value_add (&cur_val, &inc_val, &next_val);
	    }
	}
      else
	{
	  numeric_db_value_sub (&min_val, &inc_val, &tmp_val);
	  ret = numeric_db_value_compare (&cur_val, &tmp_val, &cmp_result);
	  if (ret != NO_ERROR)
	    {
	      goto exit_on_error;
	    }
	  pr_clear_value (&tmp_val);
	  /* cur_val + inc_val < min_val */
	  if (DB_GET_INT (&cmp_result) < 0)
	    {
	      if (DB_GET_INT (&cyclic))
		{
		  pr_clone_value (&max_val, &next_val);
		}
	      else
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			  ER_QPROC_SERIAL_RANGE_OVERFLOW, 0);
		  heap_attrinfo_end (thread_p, &attr_info);
		  goto exit_on_error;
		}
	    }
	  else
	    {
	      (void) numeric_db_value_add (&cur_val, &inc_val, &next_val);
	    }
	}

      /* Now update record */
      ret = heap_attrinfo_set (&serial_oid, 2, &next_val, &attr_info);
      if (ret != NO_ERROR)
	{
	  heap_attrinfo_end (thread_p, &attr_info);
	  goto exit_on_error;
	}
    }

  scan = S_DOESNT_FIT;
  new_copyarea_length = DB_PAGESIZE;
  while (scan == S_DOESNT_FIT)
    {
      new_copyarea =
	locator_allocate_copy_area_by_length (new_copyarea_length);
      if (new_copyarea == NULL)
	{
	  break;
	}

      new_recdesc.data = new_copyarea->mem;
      new_recdesc.area_size = new_copyarea->length;

      scan =
	heap_attrinfo_transform_to_disk (thread_p, &attr_info, &recdesc,
					 &new_recdesc);
      if (scan != S_SUCCESS)
	{
	  new_copyarea_length = new_copyarea->length;
	  locator_free_copy_area (new_copyarea);

	  if (scan == S_DOESNT_FIT)
	    {
	      if (new_copyarea_length < (-new_recdesc.length))
		{
		  new_copyarea_length = -new_recdesc.length;
		}
	      else
		{
		  new_copyarea_length += DB_PAGESIZE;
		}
	    }
	  else
	    {
	      new_copyarea = NULL;
	    }
	}
    }

  if (scan != S_SUCCESS)
    {
      heap_attrinfo_end (thread_p, &attr_info);
      locator_free_copy_area (new_copyarea);
      goto exit_on_error;
    }

  /* Log the changes */
  new_recdesc.type = recdesc.type;
  addr.offset = serial_oid.slotid;
  addr.pgptr = pgptr;

  if (spage_is_updatable
      (thread_p, addr.pgptr, serial_oid.slotid, &new_recdesc) == false)
    {
      heap_attrinfo_end (thread_p, &attr_info);
      locator_free_copy_area (new_copyarea);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_CANNOT_UPDATE_SERIAL,
	      0);
      goto exit_on_error;
    }

  redo_crumbs[0].length = sizeof (new_recdesc.type);
  redo_crumbs[0].data = (char *) &new_recdesc.type;
  redo_crumbs[1].length = new_recdesc.length;
  redo_crumbs[1].data = new_recdesc.data;
  log_append_redo_crumbs (thread_p, RVHF_UPDATE, &addr, 2, redo_crumbs);

  /* Now really update */
  sp_success =
    spage_update (thread_p, addr.pgptr, serial_oid.slotid, &new_recdesc);
  if (sp_success != SP_SUCCESS)
    {
      heap_attrinfo_end (thread_p, &attr_info);
      locator_free_copy_area (new_copyarea);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_CANNOT_UPDATE_SERIAL,
	      0);
      goto exit_on_error;
    }

#if !defined (WINDOWS)
  /* make replication log for the special type of update for serial */
  if (IS_REPLICATED_MODE (&serial_class_oid, true, true))
    {
      repl_log_insert (thread_p, &serial_class_oid, &serial_oid,
		       LOG_REPLICATION_DATA, RVREPL_DATA_UPDATE, &key_val);
      repl_add_update_lsa (thread_p, &serial_oid);
    }

  if (PRM_REPLICATION_MODE)
    {
      repl_end_flush_mark (thread_p, false);
    }
#endif /* WINDOWS */

  /* copy result value */
  pr_clone_value (&next_val, result_num);

  heap_attrinfo_end (thread_p, &attr_info);
  locator_free_copy_area (new_copyarea);

  pr_clear_value (&key_val);
  pr_clear_value (&cur_val);
  pr_clear_value (&inc_val);
  pr_clear_value (&max_val);
  pr_clear_value (&min_val);
  pr_clear_value (&next_val);
  pr_clear_value (&tmp_val);

  /* free copy area */
  locator_free_copy_area (copyarea);

  /* free and unlock page */
  pgbuf_unfix (thread_p, pgptr);

#if !defined (WINDOWS)
  if (PRM_REPLICATION_MODE)
    {
      repl_end_flush_mark (thread_p, false);
    }
#endif /* WINDOWS */

  if (savepoint_used)
    {
      if (xtran_server_end_topop (thread_p, LOG_RESULT_TOPOP_COMMIT, &lsa)
	  != TRAN_UNACTIVE_COMMITTED)
	{
	  return ER_FAILED;
	}
    }

end:

  return ret;

exit_on_error:

  pr_clear_value (&key_val);
  pr_clear_value (&cur_val);
  pr_clear_value (&inc_val);
  pr_clear_value (&max_val);
  pr_clear_value (&min_val);
  pr_clear_value (&next_val);
  pr_clear_value (&tmp_val);

  /* free copy area */
  if (copyarea)
    {
      locator_free_copy_area (copyarea);
    }

  if (page_locked)
    {
      pgbuf_unfix (thread_p, pgptr);
    }

#if !defined (WINDOWS)
  if (PRM_REPLICATION_MODE)
    {
      repl_end_flush_mark (thread_p, true);
    }
#endif /* WINDOWS */

  if (savepoint_used)
    {
      (void) xtran_server_end_topop (thread_p, LOG_RESULT_TOPOP_ABORT, &lsa);
    }

  if (ret == NO_ERROR)
    {
      ret = er_errid ();
      if (ret == NO_ERROR)
	{
	  ret = ER_FAILED;
	}
    }
  goto end;
}
