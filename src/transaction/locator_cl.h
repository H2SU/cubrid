/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * lccl.h - Transaction object locator interfacee (at client)
 *
 * Note: See .c file for overview and description of the interface functions.
 */

#ifndef _LCCL_H_
#define _LCCL_H_

#ident "$Id$"

#include "config.h"

#include "error_manager.h"
#include "work_space.h"
#include "common.h"
#include "locator_bt.h"
#include "dbdef.h"
#include "replication.h"

#define ONE_MFLUSH    true
#define MANY_MFLUSHES false

#define DECACHE       true
#define DONT_DECACHE  false

#define OID_BATCH_SIZE  2000

typedef struct list_mops LIST_MOPS;
struct list_mops
{
  int num;
  MOP mops[1];
};

typedef enum
{
  LC_OBJECT,			/* Don't know if it is an instance or a class */
  LC_CLASS,			/* A class                                    */
  LC_INSTANCE			/* An instance                                */
} LC_OBJTYPE;

extern bool locator_is_root (MOP mop);
extern bool locator_is_class (MOP mop, DB_FETCH_MODE hint_purpose);
extern LOCK locator_fetch_mode_to_lock (DB_FETCH_MODE purpose,
					LC_OBJTYPE type);
extern int locator_get_cache_coherency_number (MOP mop);
extern MOBJ locator_fetch_object (MOP mop, DB_FETCH_MODE purpose);
extern MOBJ locator_fetch_class (MOP class_mop, DB_FETCH_MODE purpose);
extern MOBJ locator_fetch_class_of_instance (MOP inst_mop, MOP * class_mop,
					     DB_FETCH_MODE purpose);
extern MOBJ locator_fetch_instance (MOP mop, DB_FETCH_MODE purpose);
extern MOBJ locator_fetch_set (int num_mops, MOP * mop_set,
			       DB_FETCH_MODE inst_purpose,
			       DB_FETCH_MODE class_purpose,
			       int quit_on_errors);
extern MOBJ locator_fetch_nested (MOP mop, DB_FETCH_MODE purpose,
				  int prune_level, int quit_on_errors);
extern int locator_flush_class (MOP class_mop);
extern int locator_flush_instance (MOP mop);
extern int locator_flush_and_decache_instance (MOP mop);
extern int locator_flush_all_instances (MOP class_mop, bool decache);
extern int locator_flush_for_multi_update (MOP class_mop);
extern int locator_all_flush (void);
extern MOP locator_add_class (MOBJ class, const char *classname);
extern MOP locator_add_instance (MOBJ instance, MOP class_mop);
extern MOP locator_add_root (OID * root_oid, MOBJ class_root);
extern int locator_remove_class (MOP class_mop);
extern void locator_remove_instance (MOP mop);
extern MOBJ locator_update_instance (MOP mop);
extern MOBJ locator_update_class (MOP mop);
extern int locator_update_tree_classes (MOP * classes_mop_set,
					int num_classes);
extern MOBJ locator_prepare_rename_class (MOP class_mop,
					  const char *old_classname,
					  const char *new_classname);
extern OID *locator_assign_permanent_oid (MOP mop);
extern MOP locator_find_class (const char *classname);
extern LC_FIND_CLASSNAME locator_find_query_class (const char *classname,
						   DB_FETCH_MODE purpose,
						   MOP * class_mop);
extern LC_FIND_CLASSNAME locator_lockhint_classes (int num_classes,
						   const char
						   **many_classnames,
						   LOCK * many_locks,
						   int *need_subclasses,
						   int quit_on_errors);
extern int locator_does_exist_object (MOP mop, DB_FETCH_MODE purpose);
extern LIST_MOPS *locator_get_all_mops (MOP class_mop,
					DB_FETCH_MODE class_purpose);
extern LIST_MOPS *locator_get_all_class_mops (DB_FETCH_MODE purpose,
					      int (*fun) (MOBJ class_obj));
extern LIST_MOPS *locator_get_all_nested_mops (MOP mop, int prune_level,
					       DB_FETCH_MODE inst_purpose);
extern void locator_free_list_mops (LIST_MOPS * mops);
extern void locator_synch_isolation_incons (void);
extern void locator_set_sig_interrupt (int set);
extern MOBJ locator_has_heap (MOP class_mop);

typedef void (*LC_OIDMAP_CALLBACK) (LC_OIDMAP * map);

extern LC_OIDMAP *locator_add_oidset_object (LC_OIDSET * oidset,
					     DB_OBJECT * obj);
extern int locator_assign_oidset (LC_OIDSET * oidset,
				  LC_OIDMAP_CALLBACK callback);
extern int locator_assign_all_permanent_oids (void);

extern int locator_decache_all_lock_instances (MOP class_mop);

extern int locator_get_append_lsa (LOG_LSA * lsa);
extern int locator_flush_replication_info (REPL_INFO * repl_info);

#endif /* _LCCL_H_ */
