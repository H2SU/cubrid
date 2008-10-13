/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * bosr.h - Boot managment in the server (interface)
 * 		       
 * Note: See .c file for overview and description of the interface functions.
 * 									       
 */

#ifndef _BOSR_H_
#define _BOSR_H_

#ident "$Id$"

#include "config.h"

#include <time.h>

#include "error_manager.h"
#include "common.h"
#include "oid.h"
#include "disk_manager.h"
#include "logcp.h"
#include "file_io.h"


#define BO_ISSERVER_RESTARTED() (boot_Server_up)

extern int boot_Server_up;
extern bool skip_to_check_ct_classes_for_rebuild;

#if defined(SERVER_MODE)
/* in xserver.h */
extern int
xboot_initialize_server (THREAD_ENTRY * thread_p, int print_version,
			 bool db_overwrite, PGLENGTH db_desired_pagesize,
			 const char *db_name, const char *xdb_path,
			 const char *vol_path, const char *db_comments,
			 DKNPAGES db_npages, const char *file_addmore_vols,
			 const char *db_server_host, const char *xlog_path,
			 DKNPAGES xlog_npages, OID * rootclass_oid,
			 HFID * rootclass_hfid, const char *client_prog_name,
			 const char *client_user_name,
			 const char *client_host_name, int client_process_id,
			 int client_lock_wait,
			 TRAN_ISOLATION client_isolation);
extern VOLID xboot_add_volume_extension (THREAD_ENTRY * thread_p,
					 const char *ext_path,
					 const char *ext_name,
					 const char *ext_comments,
					 DKNPAGES ext_npages,
					 DISK_VOLPURPOSE ext_purpose,
					 bool ext_overwrite);

extern void boot_donot_shutdown_server_at_exit (void);
extern void xboot_notify_unregister_client (THREAD_ENTRY * thread_p,
					    int tran_index);
#endif /* SERVER_MODE */

extern const char *boot_db_name (void);
extern const char *boot_db_full_name (void);
extern HFID *boot_find_root_heap (void);

#if defined(SERVER_MODE)
extern int xboot_find_number_permanent_volumes (THREAD_ENTRY * thread_p);
extern int xboot_find_number_temp_volumes (THREAD_ENTRY * thread_p);
extern VOLID xboot_find_last_temp (THREAD_ENTRY * thread_p);
#endif /* SERVER_MODE */

extern VOLID boot_find_next_permanent_volid (THREAD_ENTRY * thread_p);
extern int boot_reset_db_parm (THREAD_ENTRY * thread_p);
extern DKNPAGES boot_max_pages_new_volume (void);
extern DKNPAGES boot_max_pages_for_new_auto_volume_extension (void);
extern DKNPAGES boot_max_pages_for_new_temp_volume (void);
extern VOLID
boot_add_auto_volume_extension (THREAD_ENTRY * thread_p, DKNPAGES min_npages,
				DISK_SETPAGE_TYPE setpage_type);
extern VOLID boot_add_temp_volume (THREAD_ENTRY * thread_p, DKNPAGES npages);
extern int boot_add_temp_volume_and_file (VFID * vfid, DKNPAGES npages);
extern int boot_remove_temp_volume (THREAD_ENTRY * thread_p, VOLID volid);
extern int boot_shrink_temp_volume (THREAD_ENTRY * thread_p);
extern int
boot_restart_server (THREAD_ENTRY * thread_p, int print_restart,
		     const char *db_name, bool from_backup,
		     BO_RESTART_ARG * r_args);
extern int xboot_restart_from_backup (THREAD_ENTRY * thread_p,
				     int print_restart, const char *db_name,
				     BO_RESTART_ARG * r_args);
extern bool xboot_shutdown_server (THREAD_ENTRY * thread_p, bool iserfinal);
#if defined(SERVER_MODE)
extern int
xboot_register_client (THREAD_ENTRY * thread_p, int print_restart,
		       const char *db_name, OID * rootclass_oid,
		       HFID * rootclass_hfid, const char *client_prog_name,
		       const char *client_user_name,
		       const char *client_host_name, int client_process_id,
		       int client_lock_wait, TRAN_ISOLATION client_isolation,
		       TRAN_STATE * transtate, PGLENGTH * current_pagesize);
extern int xboot_unregister_client (THREAD_ENTRY * thread_p, int tran_index);
extern int
xboot_backup (THREAD_ENTRY * thread_p, const char *backup_path,
	      FILEIO_BACKUP_LEVEL backup_level,
	      int delete_unneeded_logarchives,
	      const char *backup_verbose_file, int num_threads,
	      FILEIO_ZIP_METHOD zip_method, FILEIO_ZIP_LEVEL zip_level,
	      int skip_activelog, PAGEID safe_pageid);
#endif /* SERVER_MODE */
extern int
xboot_copy (THREAD_ENTRY * thread_p, const char *from_dbname,
	   const char *newdb_name, const char *newdb_path,
	   const char *newlog_path, const char *newdb_server_host,
	   const char *new_volext_path, const char *fileof_vols_and_copypaths,
	   bool newdb_overwrite);
extern int xboot_soft_rename (THREAD_ENTRY * thread_p, const char *olddb_name,
			     const char *newdb_name, const char *newdb_path,
			     const char *newlog_path,
			     const char *newdb_server_host,
			     const char *new_volext_path,
			     const char *fileof_vols_and_renamepaths,
			     bool newdb_overwrite, bool extern_rename,
			     bool force_delete);
extern int xboot_delete (THREAD_ENTRY * thread_p, const char *db_name,
			bool force_delete);
extern int xboot_emergency_patch (THREAD_ENTRY * thread_p, const char *db_name,
				 bool recreate_log);
extern void boot_server_all_finalize (THREAD_ENTRY * thread_p,
				      bool iserfinal);

#if defined(SERVER_MODE)
extern int xboot_check_db_consistency (THREAD_ENTRY * thread_p,
				       int check_flag);
#endif /* SERVER_MODE */

#endif /* _BOSR_H_ */
