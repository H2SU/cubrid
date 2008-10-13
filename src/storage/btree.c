/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * btree.c - B+-Tree mananger
 *
 * The Btree manager manages the creation, destruction, insertion, deletion,
 * and search operations on Btree indices. The basic data structure is a
 * special Btree called a prefix Btree [BAYE77]. Unlike a regular Btree
 * structures, in prefix Btrees, non leaf nodes do not contain complete keys
 * but instead only prefixes of the keys that are adequate to guide the search
 * on down the tree.  The basic idea is to improve the storage utilization in
 * non leaf nodes, thus decreasing the height of the tree and increasing the
 * performance of the search operations by reducing I/O operations.
 *
 * Btrees are used to implement indexes and to enforce unique constraints
 * on attributes.  Btrees can be created on an empty class which results
 * in the creation of an empty Btree.  Btrees may also be created on classes
 * with existing objects.  In this case, it is necessary to pre-load the
 * index with the initial objects.  An efficient load algorithm is used
 * to cut down on the logging and tree balancing overhead of simply adding
 * the existing objects one by one.
 *
 * An index may be created on any CUBRID type except for the collection
 * types and ELOs.  There is really no reason (in theory) why the collection
 * types could not be indexed.
 *
 * Prefix keys are only used on the variable length types (currently only
 * the six string types).  When multi-column indexes/uniques are
 * implemented, they could also use prefix keys, but this must be done
 * carefully because of the special ordering semantics for collections.
 *
 * For each key, a set of values is stored in the leaf nodes of the
 * Btree. Each value is an object identifier which consists of a volume
 * identifier, a page identifier and a page slot identifier. There is
 * virtually no limit to the number of OIDs that can be entered for a
 * key.
 *
 * The Page Buffer Manager, the Slotted Page Manager, the File Manager,
 * the Lock Manager and the Log Manager are all called by the Btree Manager.
 * The Schema Manager, the Transaction Locator, the Query Processor,
 * the Statistics Manager, and the Recovery Manager all call the Btree
 * Manager.
 *
 * ARCHITECTURE
 *
 * The index manager is built on top of the slotted page manager (sp_).
 * Each node of the Btree is physically represented as a slotted page in
 * which the order of the records (slots) is preserved.  The user creates
 * the index by specifying only the domain of the key for the index and
 * the volume in which the index is going to reside. The user is then given
 * a unique Btree index identifier, which is used as the basic interface
 * parameter by the index management routines for insertion, deletion, and
 * search operations. When the user destroys the index, all the pages that
 * form the tree are deallocated and the Btree index identifier becomes
 * invalid.
 *
 * NON LEAF NODES
 *
 * There are two different types of nodes: leaf nodes and non leaf nodes.
 * Leaf nodes are where the keys and their OIDs are actually stored.
 * Non leaf nodes form the actual index to be used to access a key and
 * its OIDs quickly. The basic lay-out of a non leaf node is:
 * ___________________________________________________________________________
 * | Node Header || Child Ptr | Key_Len | Key || ...        ... || Child Ptr |
 * ---------------------------------------------------------------------------
 * <-   Header ->  <-------- Record 1 ------->                 <- Rec n+1 ->
 *
 * The node header for a non leaf node contains the key count and maximum key
 * length information as well as a pointer to the next page on that level
 * of the Btree (known as the next sibling page).  For historical reasons,
 * key count is one less than the number of records on the page.  This is
 * because the last record on the page does not contain a valid key, only
 * a valid page pointer.  Thus, if there are n keys in the node, there are
 * n+1 child page pointers (page identifiers).  The maximum key length is
 * the length of the longest key in the subtree rooted at that node. This
 * information is used by node splitting routines during insertion operations.
 *
 * Although it seems counter intuitive, there is a case where a non leaf node
 * can have only one child page pointer and no keys, i.e. n = 0. This happens
 * when a non leaf node has only two child pages and these pages are
 * merged during deletion operations. These kind of non leaf pages are
 * eliminated during subsequent deletion or insertion operations, when the
 * page is merged with one of it's sibling pages, or is supplied with a key
 * when it's child page is split. If there are k records (slots) in a non leaf
 * node, the node contains k-2 keys. Because, the first record contains the
 * header information, and the last record contains only a child page
 * identifier.
 *
 * OVERFLOW KEYS
 *
 * If a key is too large to fit on a page, it is stored using the Overflow
 * Manager (ovf_).  In this case, the key length will be -1 (to indicate that
 * it is an overflow key) and the overflow page ptr returned by the Overflow
 * Manger is stored in the key's place.
 *
 * If overflow keys are necessary, the Btree Manager creates a new overflow
 * file for the overflow keys.  This overflow file is used exclusively by
 * the Btree that it was created for.  This allows the overflow file to be
 * deleted when the Btree is destroyed without having to worry if keys from
 * other Btrees are stored in that overflow file.
 *
 * LEAF NODES
 *
 * The basic layout of a leaf node is as follows:
 * _________________________________________________________________________
 * |Node Header|| Ovfl_Pg | Key_Len| Key| OIDs || ...    ||                 |
 * -------------------------------------------------------------------------
 * <-  Header  -> <------------- Record 1 --------------->        <- Rec n ->
 *
 * The node header for a leaf node contains the key count and maximum key
 * length information as well as a pointer to the next page on that level
 * of the Btree (known as the next sibling page).  In the case of leaf pages,
 * the key count is the actual number of records on the page.
 *
 * A leaf node record contains an OID overflow page pointer, a key length,
 * the key, and a list of OIDs.  As with non leaf pages, if the key length
 * is -1, the key is stored by the overflow manager in the overflow key file.
 *
 * OID OVERFLOW PAGES
 *
 * The OID list is the set of OIDs that correspond to the record's key.
 * This OID list can be arbitrarily long.  If it becomes longer than the
 * number that will fit on one Btree page, the excess OIDs are stored in a
 * new Btree page, called an OID overflow page and the pointer to this page
 * is stored in the OID overflow page pointer for this record.  If a key has
 * overflow OIDs it will be the only record on that leaf page.  No other keys
 * can share the page with a record that contains overflow OIDs.
 *
 * An OID overflow page consists of two records; a header record and a list
 * of OIDs.  The OID overflow header contains a pointer to the next OID
 * overflow page in this chain.  The rest of the page consists of a single
 * record of OIDs.
 *
 * ROOT NODE
 *
 * The root node is a special node with special header information. It is a
 * leaf node when the tree contains only one node, and a non leaf node
 * otherwise. The root page always remains the same page, the first page
 * allocated by the File Manager during Btree index creation. This enables
 * access to the root page of the tree by requesting the first allocated page
 * from the File Manager. The fact that the root page is always the same page
 * brings about slightly different split and merge operations when the root
 * page is involved in these operations (see below).
 *
 * In addition to the regular node header information, the root header
 * contains the domain for the key values, the VFID for the overflow key
 * file, and the unique statistics.  Only Btrees that implement unique
 * constraints maintain the unique statistics.  Btrees for indexes leave
 * these statistics empty.
 *
 * UNIQUE STATISTICS
 *
 * When a Btree is used to enforce a unique constraint, it is necessary to
 * maintain statistics on the number of keys in the Btree, the number of OIDs
 * in the Btree, and the number of NULLs for the constrained attribute(s).
 * Using these statistics the unique constraint check becomes simply:
 *
 *      unique = ((# NULLs + # keys) == # OIDs)
 *
 * These statistics are maintained during Btree insertions and deletions.
 *
 * CONCURRENCY ISSUES
 *
 * Like all CUBRID page locks, Btree page locks are not held to the end
 * of the transaction.  Due to page splitting/merging a Btree may be
 * substantially altered by a transaction that will later abort.  These
 * page splits/merges are not undone since other transactions may have inserted
 * or deleted keys using the new Btree structure.  This raises interesting
 * logging and recovery issues for the Btree manager.
 *
 * The locking technique employed by the index manager [GARZ88] can be
 * summarized as follows :
 *
 *  i - For a read-only access, acquire a page-lock in Share (S) mode on the
 *      index page.  While descending the tree, at most two pages are locked
 *      at a time.  At any given page (call it the parent page), its lock is
 *      held only long enough to determine the correct child page to follow.
 *
 * ii - For insertion, deletion, or update operations, acquire a page-lock
 *      in Exclusive (X) mode on the index page
 *
 * ii - In the case of an index-page split, acquire an X lock on the
 *      old index page,the newly allocated index page, and the parent of
 *      the two pages.
 *
 * iv - In the case of a page merging, acquire an X lock on both index
 *      pages that are merged as well as their common parent.
 *
 *  v - Release all locks on index pages as soon as the pages are no longer
 *      needed.
 *
 * NODE SPLITS AND MERGES
 *
 * During insertions (deletions), the node split (merge) operations are done
 * while traversing the tree from top to bottom. This eliminates the upward
 * propagation of split (merge) operations which may cause deadlocks. During
 * insertion, while accessing an index page P, the index manager determines
 * if the insertion may result in the split of the page Q, the child page of
 * P to be next accessed. If so, it will split page Q into two pages, Q and R
 * and insert an entry in P that will point to the newly allocated page R.
 * Similarly, during deletion, while accessing an index page P, the index
 * manager determines if a pair of child pages can be merged. If so, it moves
 * entries from one to the other and updates the parent page P, while the page
 * is locked in X mode.
 *
 * It is during splitting that prefix keys are generated.  This is only done
 * while splitting a leaf page.  More plainly, we will not generate a prefix
 * key when splitting a non leaf page.  The reason for this is that when
 * splitting a non leaf page you know nothing about the distribution of the
 * keys of adjacent child pages.  You simply know that all of the keys on
 * a child page are smaller than its non leaf key.  This is not enough
 * information to construct a prefix key for a non leaf split.  The
 * information required resides in the leaves of the tree and is too
 * expensive to locate during node splitting.  In reality, this is not much
 * of a problem since keys in non leaf pages were ultimately a prefix key
 * generated from a previous leaf page split.
 *
 * ROOT SPLITS AND MERGES
 *
 * The fact that the root page is always the same page brings about slightly
 * different split and merge operations when the root page is involved in
 * these operations. When the root page is to be split, its content is moved
 * to two new child pages, and similarly, when the last two child pages of the
 * root page are to be merged, their contents are moved to the root page.
 *
 * BTREE LOADING (see btree_load.c)
 *
 * When a Btree is created on a class that contains existing instances, it
 * is necessary to load the index with the attribute values/OIDs for those
 * instances.
 *
 * The Btree load operation is performed in two stages. In the first stage,
 * the leaf pages of the tree are created. This is done by using the Sort
 * Manager.  The pairs of (key, OID) values extracted from the instances of
 * the class are provided to the sorting process as input. Once the sorting
 * process is finished, the sorted (key, OID) pairs are used to build the leaf
 * pages (including the OID overflow pages if there are too many OIDs
 * associated with a key to fit onto a single page) of the index.
 *
 * In the second stage the non leaf pages of the tree are constructed in a
 * bottom-up fashion one level at a time. For the first non leaf level of
 * the tree the prefixes of keys (if the key is one of the six string types)
 * residing in the leaf level pages are used. For upper levels, the key values
 * of the previous level are used without considering the prefixes. Once the
 * highest level of the tree is constructed, the one and only page at this
 * highest level is declared as the root of the tree and this page identifier
 * is used for the Btree identifier.
 *
 * OTHER REFERENCES
 *
 * Relevant literature includes:
 *
 * [GARZ88] Garza, J.F., and Kim, W., "Transaction Management in an Object
 * Oriented Database System", Proc. ACM-SIGMOD Conf. on the Management of
 * Data, June 1988.
 *
 * [KIM89] Kim, W., Kim, K.C., and Dale, A., "Indexing Techniques for
 * Object-Oriented Databases", in Object-Oriented Concepts, Databases and
 * Applications, ed. by W. Kim and F. Lochovsky, Addison-Wesley, 1989.
 *
 * [BAYE77] Bayer, R., and Unterauer, K., "Prefix B-Trees", ACM Transactions
 * on Database Systems, Vol. 2, No. 1, March 1977.
 *
 * [COME79] Comer, D., "The Ubiquitous B-Tree", Computing Surveys, Vol. 11,
 * No. 2, June 1979.
 *
 */

#ident "$Id$"

#include "config.h"

#include <stdlib.h>
#include <string.h>

#include "btree_load.h"
#include "common.h"
#include "error_manager.h"
#include "page_buffer.h"
#include "file_io.h"
#include "file_manager.h"
#include "fldesc.h"
#include "slotted_page.h"
#include "oid.h"
#include "log.h"
#include "memory_manager_2.h"
#include "overflow_file.h"
#include "xserver.h"
#include "set_object_1.h"
#include "btree.h"
#include "scan_manager.h"
#if defined(SERVER_MODE)
#include "thread_impl.h"
#endif /* SERVER_MODE */
#include "heap_file.h"
#include "object_primitive.h"

/* this must be the last header file included!!! */
#include "dbval.h"

/*
 * Page header information related defines
 */

#define BTREE_NEXT_OVFL_VPID_OFFSET    (0)

/*
 * Recovery structures
 */
typedef struct pageid_struct PAGEID_STRUCT;
struct pageid_struct
{				/* Recovery pageid structure */
  VFID vfid;			/* Volume id in which page resides */
  VPID vpid;			/* Virtual page identifier */
};

typedef struct recset_header RECSET_HEADER;
struct recset_header
{				/* Recovery set of recdes structure */
  INT16 rec_cnt;		/* number of RECDESs stored */
  INT16 first_slotid;		/* first slot id */
};

typedef enum
{
  REGULAR = 1,
  OVERFLOW
} LEAF_RECORD_TYPE;

typedef struct recins_struct RECINS_STRUCT;
struct recins_struct
{				/* Recovery leaf record oid insertion structure */
  OID class_oid;		/* class oid only in case of unique index */
  OID oid;			/* oid to be inserted to the record    */
  LEAF_RECORD_TYPE rec_type;	/* Regular or Overflow Leaf Record  */
  int oid_inserted;		/* oid inserted to the record ? */
  int ovfl_changed;		/* next ovfl pageid changed ? */
  int new_ovflpg;		/* A new overflow page ? */
  VPID ovfl_vpid;		/* Next Overflow pageid  */
};

/* Offset of the fields in the Leaf/NonLeaf Record Recovery Log Data */
#define OFFS1  0		/* Node Type Offset: Leaf/NonLeaf Information */
#define OFFS2  2		/* RECDES Type Offset */
#define OFFS3  4		/* RECDES Data Offset */

/* for Leaf Page Key Insertions */
#define LOFFS1  0		/* Key Len Offset */
#define LOFFS2  2		/* Node Type Offset: Leaf/NonLeaf Information */
#define LOFFS3  4		/* RECDES Type Offset */
#define LOFFS4  6		/* RECDES Data Offset */

/* B+tree statistical information environment */
typedef struct btree_stats_env BTREE_STATS_ENV;
struct btree_stats_env
{
  BTREE_STATS *stat_info;
  bool get_pkeys;		/* true to compute partial keys info, or false */
  DB_VALUE *pkeys;		/* partial key-value */
};

static int btree_create_overflow_key_file (THREAD_ENTRY * thread_p,
					   BTID_INT * btid, bool loading);
static int btree_store_overflow_key (THREAD_ENTRY * thread_p, BTID_INT * btid,
				     DB_VALUE * key, int size, bool loading,
				     VPID * firstpg_vpid);
static int btree_load_overflow_key (THREAD_ENTRY * thread_p, BTID_INT * btid,
				    VPID * firstpg_vpid, DB_VALUE * key);
static int btree_delete_overflow_key (THREAD_ENTRY * thread_p,
				      BTID_INT * btid, PAGE_PTR page_ptr,
				      INT16 slot_id, bool leaf);
static void btree_read_node_header (RECDES * Rec, BTREE_NODE_HEADER * header);
static void
btree_write_fixed_portion_of_leaf_record_to_orbuf (OR_BUF * buf,
						   LEAF_REC * lf_rec);
static int
btree_read_fixed_portion_of_leaf_record_from_orbuf (OR_BUF * buf,
						    LEAF_REC * lf_rec);
static void
btree_write_fixed_portion_of_non_leaf_record (RECDES * Rec,
					      NON_LEAF_REC * nlf_rec);
static void
btree_read_fixed_portion_of_non_leaf_record (RECDES * Rec,
					     NON_LEAF_REC * nlf_rec);
static void
btree_write_fixed_portion_of_non_leaf_record_to_orbuf (OR_BUF * buf,
						       NON_LEAF_REC *
						       nlf_rec);
static int
btree_read_fixed_portion_of_non_leaf_record_from_orbuf (OR_BUF * buf,
							NON_LEAF_REC *
							nlf_rec);
static void btree_append_oid (RECDES * Rec, OID * oid);
static int btree_start_overflow_page (THREAD_ENTRY * thread_p, RECDES * Rec,
				      BTID_INT * btid, VPID * new_vpid,
				      PAGE_PTR * newp, VPID * near_vpid,
				      OID * class_oid, OID * oid);
static PAGE_PTR btree_get_new_page (THREAD_ENTRY * thread_p, BTID_INT * btid,
				    VPID * vpid, VPID * near_vpid);
static bool btree_initialize_new_page (THREAD_ENTRY * thread_p,
				       const VFID * vfid, const VPID * vpid,
				       INT32 ignore_napges,
				       void *args_ignore);
static int btree_search_nonleaf_page (THREAD_ENTRY * thread_p,
				      BTID_INT * btid, PAGE_PTR page_ptr,
				      DB_VALUE * key, INT16 * slot_id,
				      VPID * child_vpid);
static int btree_search_leaf_page (THREAD_ENTRY * thread_p, BTID_INT * btid,
				   PAGE_PTR page_ptr, DB_VALUE * key,
				   INT16 * slot_id);
#if 0				/* TODO: currently, unused */
static int btree_get_key_oid_count (BTID * btid, DB_VALUE * key);
#endif /* 0 */
static int btree_get_subtree_stats (THREAD_ENTRY * thread_p, BTID_INT * btid,
				    PAGE_PTR pg_ptr, BTREE_STATS_ENV * env);
static DISK_ISVALID btree_check_page_key (THREAD_ENTRY * thread_p,
					  BTID_INT * btid, PAGE_PTR page_ptr,
					  VPID * page_vpid, bool * clear_key,
					  DB_VALUE * maxkey);
static DISK_ISVALID btree_check_pages (THREAD_ENTRY * thread_p,
				       BTID_INT * btid, PAGE_PTR pg_ptr,
				       VPID * pg_vpid);
static DISK_ISVALID btree_verify_subtree (THREAD_ENTRY * thread_p,
					  BTID_INT * btid, PAGE_PTR pg_ptr,
					  VPID * pg_vpid,
					  BTREE_NODE_INFO * INFO);
static int btree_get_subtree_capacity (THREAD_ENTRY * thread_p,
				       BTID_INT * btid, PAGE_PTR pg_ptr,
				       BTREE_CAPACITY * cpc);
static void btree_print_space (int n);
static int btree_delete_from_leaf (THREAD_ENTRY * thread_p, BTID_INT * btid,
				   VPID * leaf_vpid, DB_VALUE * key,
				   OID * class_oid, OID * oid, int *del_key);
static int btree_insert_into_leaf (THREAD_ENTRY * thread_p, BTID_INT * btid,
				   PAGE_PTR page_ptr, DB_VALUE * key,
				   OID * cls_oid, OID * oid,
				   VPID * nearp_vpid, int *add_key,
				   int do_unique_check);
static int btree_merge_root (THREAD_ENTRY * thread_p, BTID_INT * btid,
			     PAGE_PTR P, PAGE_PTR Q, PAGE_PTR R,
			     VPID * P_vpid, VPID * Q_vpid, VPID * R_vpid,
			     bool leaf_page);
static int btree_merge_node (THREAD_ENTRY * thread_p, BTID_INT * btid,
			     PAGE_PTR P, PAGE_PTR Q, PAGE_PTR R,
			     VPID * P_vpid, VPID * Q_vpid, VPID * R_vpid,
			     INT16 p_slot_id, bool leaf_page,
			     int is_left_merge, VPID * child_vpid);
static DB_VALUE *btree_find_split_point (THREAD_ENTRY * thread_p,
					 BTID_INT * btid, PAGE_PTR page_ptr,
					 INT16 * mid_slot, DB_VALUE * key,
					 bool * clear_midkey);
static int btree_split_node (THREAD_ENTRY * thread_p, BTID_INT * btid,
			     PAGE_PTR P, PAGE_PTR Q, PAGE_PTR R,
			     VPID * P_vpid, VPID * Q_vpid, VPID * R_vpid,
			     INT16 p_slot_id, bool leaf_page, DB_VALUE * key,
			     VPID * child_vpid);
static int btree_split_root (THREAD_ENTRY * thread_p, BTID_INT * btid,
			     PAGE_PTR P, PAGE_PTR Q, PAGE_PTR R,
			     VPID * P_page_vpid, VPID * Q_page_vpid,
			     VPID * R_page_vpid, bool leaf_page,
			     DB_VALUE * key, VPID * child_vpid);
static PAGE_PTR btree_locate_key (THREAD_ENTRY * thread_p,
				  BTID_INT * btid_int, DB_VALUE * key,
				  VPID * pg_vpid, INT16 * slot_id,
				  int *found);
static PAGE_PTR btree_find_first_leaf (THREAD_ENTRY * thread_p, BTID * btid,
				       VPID * pg_vpid);
static PAGE_PTR btree_find_last_leaf (THREAD_ENTRY * thread_p, BTID * btid,
				      VPID * pg_vpid);
static int btree_coerce_key (DB_VALUE * src_keyp, DB_VALUE * dest_keyp,
			     int keysize, BTID_INT * btid, int key_minmax,
			     bool * clear);
static int btree_initialize_bts (THREAD_ENTRY * thread_p, BTREE_SCAN * bts,
				 BTID * btid, int readonly_purpose,
				 int lock_hint, OID * class_oid,
				 DB_VALUE * key1, DB_VALUE * key2,
				 RANGE range, FILTER_INFO * filter,
				 bool need_construct_btid_int, char *copy_buf,
				 int copy_buf_len);
static int btree_find_next_index_record (THREAD_ENTRY * thread_p,
					 BTREE_SCAN * bts);
static int btree_get_next_oidset_pos (THREAD_ENTRY * thread_p,
				      BTREE_SCAN * bts,
				      VPID * first_ovfl_vpid);
static int btree_prepare_first_search (THREAD_ENTRY * thread_p,
				       BTREE_SCAN * bts);
static int btree_prepare_next_search (THREAD_ENTRY * thread_p,
				      BTREE_SCAN * bts);
static int btree_apply_key_range_and_filter (THREAD_ENTRY * thread_p,
					     BTREE_SCAN * bts,
					     bool * key_range_satisfied,
					     bool * key_filter_satisfied);
#if defined(SERVER_MODE)
#if 0				/* TODO: currently, unused */
static int btree_get_prev_keyvalue (BTREE_SCAN * bts, DB_VALUE * prev_key,
				    int *prev_clr_key);
#endif
static int btree_handle_prev_leaf_after_locking (THREAD_ENTRY * thread_p,
						 BTREE_SCAN * bts,
						 int oid_idx,
						 LOG_LSA * prev_leaf_lsa,
						 DB_VALUE * prev_key,
						 int *which_action);
static int btree_handle_curr_leaf_after_locking (THREAD_ENTRY * thread_p,
						 BTREE_SCAN * bts,
						 int oid_idx,
						 LOG_LSA * ovfl_page_lsa,
						 DB_VALUE * prev_key,
						 OID * prev_oid_ptr,
						 int *which_action);
#endif /* SERVER_MODE */
static int btree_rv_save_keyval (BTID_INT * btid, DB_VALUE * key,
				 OID * cls_oid, OID * oid, char **data,
				 int *length);
static void btree_rv_save_root_head (int max_key_len, int null_delta,
				     int oid_delta, int key_delta,
				     RECDES * recdes);
static void btree_rv_read_keyval_info_nocopy (THREAD_ENTRY * thread_p,
					      char *datap, int data_size,
					      BTID_INT * btid, OID * cls_oid,
					      OID * oid, DB_VALUE * key);
static int init_boundbits (char *bufptr, int n_atts);
static bool btree_find_oid_from_rec (BTID_INT * btid, char *ptr, int oid_cnt,
				     OID * target);
static DISK_ISVALID btree_find_key_from_leaf (THREAD_ENTRY * thread_p,
					      BTID_INT * btid,
					      PAGE_PTR pg_ptr, int key_cnt,
					      OID * oid, DB_VALUE * key,
					      bool * clear_key);
static DISK_ISVALID btree_find_key_from_nleaf (THREAD_ENTRY * thread_p,
					       BTID_INT * btid,
					       PAGE_PTR pg_ptr, int key_cnt,
					       OID * oid, DB_VALUE * key,
					       bool * clear_key);
static DISK_ISVALID btree_find_key_from_page (THREAD_ENTRY * thread_p,
					      BTID_INT * btid,
					      PAGE_PTR pg_ptr, OID * oid,
					      DB_VALUE * key,
					      bool * clear_key);

/* Dump routines */
static void btree_dump_root_header (RECDES Rec);
static void btree_dump_leaf_record (THREAD_ENTRY * thread_p, BTID_INT * btid,
				    RECDES * Rec, int n);
static void btree_dump_non_leaf_record (THREAD_ENTRY * thread_p,
					BTID_INT * btid, RECDES * Rec, int n,
					int print_key);
static void btree_dump_page (THREAD_ENTRY * thread_p, BTID_INT * btid,
			     PAGE_PTR page_ptr, VPID * pg_vpid, int n,
			     int level);
static void btree_dump_page_with_subtree (THREAD_ENTRY * thread_p,
					  BTID_INT * btid, PAGE_PTR pg_ptr,
					  VPID * pg_vpid, int n, int level);

static VPID *btree_get_next_overflow_vpid (char *header_ptr,
					   VPID * overflow_vpid_ptr);
/*
 * btree_clear_key_value () -
 *   return: cleared flag
 *   clear_flag (in/out):
 *   key_value (in/out):
 */
bool
btree_clear_key_value (bool * clear_flag, DB_VALUE * key_value)
{
  if (*clear_flag == true)
    {
      pr_clear_value (key_value);
      *clear_flag = false;
    }

  return *clear_flag;
}

/*
 * btree_create_overflow_key_file () -
 *   return: NO_ERROR
 *   btid(in):
 *   loading(in):
 *
 * Note: An overflow key file is created (permanently) and the VFID
 * is written to the root header for the btree.
 */
static int
btree_create_overflow_key_file (THREAD_ENTRY * thread_p, BTID_INT * btid,
				bool loading)
{
  FILE_OVF_BTREE_DES btdes_ovf;
  VPID P_vpid;
  PAGE_PTR P = NULL;
  char *header_ptr;

  /* Start a top system operation */

  if (log_start_system_op (thread_p) == NULL)
    {
      goto error;
    }

  /*
   * Create the overflow file. Try to create the overflow file in the
   * same volume where the btree was defined
   */

  btid->ovfid.volid = btid->sys_btid->vfid.volid;

  /* Initialize description of overflow heap file */
  btdes_ovf.btid = *btid->sys_btid;	/* structure copy */

  if (file_create
      (thread_p, &btid->ovfid, 3, FILE_BTREE_OVERFLOW_KEY, &btdes_ovf, NULL,
       0) == NULL)
    {
      VFID_SET_NULL (&btid->ovfid);
      goto error;
    }

  /*
   * if we are loading an index, we can't store the overflow
   * VFID in the root header, since the root header is the last
   * thing created during loading. In this case, the overflow
   * VFID will be stored when the root record is written.
   */
  if (!loading)			/* not loading */
    {
      VFID ovfid;

      P_vpid.volid = btid->sys_btid->vfid.volid;
      P_vpid.pageid = btid->sys_btid->root_pageid;

      P = pgbuf_fix (thread_p, &P_vpid, OLD_PAGE, PGBUF_LATCH_WRITE,
		     PGBUF_UNCONDITIONAL_LATCH);
      if (P == NULL)
	{
	  goto error;
	}

      btree_get_header_ptr (P, &header_ptr);

      /* read the root header */
      BTREE_GET_OVFID (header_ptr, &ovfid);

      /* undo logging */
      log_append_undo_data2 (thread_p, RVBT_UPDATE_OVFID,
			     &btid->sys_btid->vfid, P, HEADER, sizeof (VFID),
			     &ovfid);
      pgbuf_set_dirty (thread_p, P, DONT_FREE);

      /* update the root header */
      BTREE_PUT_OVFID (header_ptr, &btid->ovfid);

      /* redo logging */
      log_append_redo_data2 (thread_p, RVBT_UPDATE_OVFID,
			     &btid->sys_btid->vfid, P, HEADER, sizeof (VFID),
			     &btid->ovfid);
      pgbuf_set_dirty (thread_p, P, FREE);
      P = NULL;
    }

  if (file_new_isvalid (thread_p, &btid->sys_btid->vfid) == DISK_VALID)
    {
      log_end_system_op (thread_p, LOG_RESULT_TOPOP_ATTACH_TO_OUTER);
    }
  else
    {
      log_end_system_op (thread_p, LOG_RESULT_TOPOP_COMMIT);
      file_new_declare_as_old (thread_p, &btid->ovfid);
    }

  return NO_ERROR;

error:

  if (P)
    {
      pgbuf_unfix (thread_p, P);
      P = NULL;
    }

  log_end_system_op (thread_p, LOG_RESULT_TOPOP_ABORT);

  return ER_FAILED;
}

/*
 * btree_store_overflow_key () -
 *   return: NO_ERROR
 *   btid(in): B+tree index identifier
 *   key(in): Pointer to the overflow key memory area
 *   size(in): Overflow key memory area size
 *   loading(in):
 *   first_overflow_page_vpid(out): Set to the first overflow key page identifier
 *
 * Note: The overflow key given is stored in a chain of pages.
 */
static int
btree_store_overflow_key (THREAD_ENTRY * thread_p, BTID_INT * btid,
			  DB_VALUE * key, int size, bool loading,
			  VPID * first_overflow_page_vpid)
{
  RECDES rec;
  OR_BUF buf;
  PR_TYPE *pr_type = btid->key_type->type;
  VFID overflow_file_vfid;
  int ret = NO_ERROR;

  /* Check where we'll store the overflow key.  If this is an old style
   * btree, we'll use the btree's vfid.  If its a new style btree, we'll
   * use the ovfid in the btid.  If this is the first overflow key for
   * the btree, we'll need to create the overflow file.
   */
  if (VFID_ISNULL (&btid->ovfid))
    {
      ret = btree_create_overflow_key_file (thread_p, btid, loading);
      if (ret != NO_ERROR)
	{
	  return ret;
	}
    }

  overflow_file_vfid = btid->ovfid;	/* structure copy */

  rec.area_size = size;
  rec.data = (char *) malloc (size);
  if (rec.data == NULL)
    {
      goto exit_on_error;
    }

  or_init (&buf, rec.data, rec.area_size);

  if ((*(pr_type->writeval)) (&buf, key) != NO_ERROR)
    {
      goto exit_on_error;
    }

  rec.length = buf.ptr - buf.buffer;

  if (overflow_insert
      (thread_p, &overflow_file_vfid, first_overflow_page_vpid, &rec) == NULL)
    {
      goto exit_on_error;
    }

  if (rec.data)
    {
      free_and_init (rec.data);
    }

  return ret;

exit_on_error:

  if (rec.data)
    {
      free_and_init (rec.data);
    }

  if (ret == NO_ERROR)
    {
      ret = er_errid ();
      if (ret == NO_ERROR)
	{
	  ret = ER_FAILED;
	}
    }

  return ret;
}

/*
 * btree_load_overflow_key () -
 *   return: NO_ERROR
 *   btid(in): B+tree index identifier
 *   first_overflow_page_vpid(in): Overflow key first page identifier
 *   key(out): Set to the overflow key memory area
 *
 * Note: The overflow key is loaded from the pages.
 */
static int
btree_load_overflow_key (THREAD_ENTRY * thread_p, BTID_INT * btid,
			 VPID * first_overflow_page_vpid, DB_VALUE * key)
{
  RECDES rec;
  OR_BUF buf;
  PR_TYPE *pr_type = btid->key_type->type;
  int ret = NO_ERROR;

  rec.area_size = overflow_get_length (thread_p, first_overflow_page_vpid);
  if (rec.area_size == -1)
    {
      return ER_FAILED;
    }

  rec.data = (char *) malloc (rec.area_size);
  if (rec.data == NULL)
    {
      goto exit_on_error;
    }

  if (overflow_get (thread_p, first_overflow_page_vpid, &rec) != S_SUCCESS)
    {
      goto exit_on_error;
    }

  or_init (&buf, rec.data, rec.length);

  /* we always copy overflow keys */
  if ((*(pr_type->readval)) (&buf, key, btid->key_type, -1, true,
			     NULL, 0) != NO_ERROR)
    {
      goto exit_on_error;
    }

  if (rec.data)
    {
      free_and_init (rec.data);
    }

  return NO_ERROR;

exit_on_error:

  if (rec.data)
    {
      free_and_init (rec.data);
    }

  if (ret == NO_ERROR)
    {
      ret = er_errid ();
      if (ret == NO_ERROR)
	{
	  ret = ER_FAILED;
	}
    }

  return ret;
}

/*
 * btree_delete_overflow_key () -
 *   return: NO_ERROR
 *   btid(in): B+tree index identifier
 *   page_ptr(in): Page that contains the overflow key
 *   slot_id(in): Slot that contains the overflow key
 *   leaf(in): Leaf or NonLeaf page
 *
 * Note: The overflow key is deleted. This routine will not delete the
 * btree slot containing the key.
 */
static int
btree_delete_overflow_key (THREAD_ENTRY * thread_p, BTID_INT * btid,
			   PAGE_PTR page_ptr, INT16 slot_id, bool leaf)
{
  RECDES rec;
  VPID page_vpid;
  char *ptr;
  VFID overflow_file_vfid;
  int ret = NO_ERROR;

  /*
   * Check which file contains the overflow key.  If this is an old style
   * btree, we'll use the btree's vfid.  If its a new style btree, we'll
   * use the ovfid in the btid.
   */
  overflow_file_vfid = btid->ovfid;	/* structure copy */

  rec.area_size = -1;

  /* first read the record to get first page identifier */
  if (spage_get_record (page_ptr, slot_id, &rec, PEEK) != S_SUCCESS)
    {
      goto exit_on_error;
    }

  /* get first page identifier */
  if (leaf)
    {
      ptr = rec.data + LEAF_RECORD_SIZE;
    }
  else
    {
      ptr = rec.data + NON_LEAF_RECORD_SIZE;
    }

  page_vpid.pageid = OR_GET_INT (ptr);
  ptr += OR_INT_SIZE;

  page_vpid.volid = OR_GET_SHORT (ptr);

  if (overflow_delete (thread_p, &overflow_file_vfid, &page_vpid) == NULL)
    {
      goto exit_on_error;
    }

  return NO_ERROR;

exit_on_error:

  if (ret == NO_ERROR)
    {
      ret = er_errid ();
      if (ret == NO_ERROR)
	{
	  ret = ER_FAILED;
	}
    }

  return ret;
}

/*
 * Common utility routines
 */

/*
 * btree_write_overflow_header () -
 *   return:
 *   rec(in):
 *   next_overflow_page(in):
 *
 * Note: Writes the first record (header record) for an overflow page.
 * rec must be long enough to hold the header record.
 */
void
btree_write_overflow_header (RECDES * rec, VPID * next_overflow_page)
{
  char *ptr = rec->data;

  rec->length = OR_INT_SIZE + OR_SHORT_SIZE;

  OR_PUT_INT (ptr, next_overflow_page->pageid);
  ptr += OR_INT_SIZE;

  OR_PUT_SHORT (ptr, next_overflow_page->volid);
}

/*
 * btree_read_overflow_header () -
 *   return:
 *   rec(in):
 *   next_overflow_page(in):
 *
 * Note: Reads the first record (header record) for an overflow page.
 */
void
btree_read_overflow_header (RECDES * rec, VPID * next_overflow_page)
{
  char *ptr = rec->data;

  next_overflow_page->pageid = OR_GET_INT (ptr);
  ptr += OR_INT_SIZE;

  next_overflow_page->volid = OR_GET_SHORT (ptr);
}

/*
 * btree_write_node_header () -
 *   return:
 *   rec(out):
 *   header(in):
 *
 * Note: Writes the first record (header record) for non root page.
 * rec must be long enough to hold the header record.
 *
 */
void
btree_write_node_header (RECDES * rec, BTREE_NODE_HEADER * header)
{
  char *ptr = rec->data;

  OR_PUT_SHORT (ptr, header->node_type);
  ptr += OR_SHORT_SIZE;

  OR_PUT_SHORT (ptr, header->key_cnt);
  ptr += OR_SHORT_SIZE;

  OR_PUT_SHORT (ptr, header->max_key_len);
  ptr += OR_SHORT_SIZE;

  OR_PUT_SHORT (ptr, header->next_vpid.volid);
  ptr += OR_SHORT_SIZE;

  OR_PUT_INT (ptr, header->next_vpid.pageid);

  rec->type = REC_HOME;
  rec->length = NODE_HEADER_SIZE;
}

/*
 * btree_read_node_header () -
 *   return:
 *   rec(in):
 *   header(out):
 *
 * Note: Reads the first record (header record) for a non root page.
 */
static void
btree_read_node_header (RECDES * rec, BTREE_NODE_HEADER * header)
{
  char *ptr = rec->data;

  header->node_type = OR_GET_SHORT (ptr);
  ptr += OR_SHORT_SIZE;

  header->key_cnt = OR_GET_SHORT (ptr);
  ptr += OR_SHORT_SIZE;

  header->max_key_len = OR_GET_SHORT (ptr);
  ptr += OR_SHORT_SIZE;

  header->next_vpid.volid = OR_GET_SHORT (ptr);
  ptr += OR_SHORT_SIZE;

  header->next_vpid.pageid = OR_GET_INT (ptr);
}

/*
 * btree_write_root_header () -
 *   return:
 *   rec(out):
 *   root_header(in):
 *
 * Note: Writes the first record (header record) for a root page.
 * rec must be long enough to hold the header record.
 */
void
btree_write_root_header (RECDES * rec, BTREE_ROOT_HEADER * root_header)
{
  char *ptr = rec->data;

  btree_write_node_header (rec, &root_header->node);
  ptr += NODE_HEADER_SIZE;

  OR_PUT_INT (ptr, root_header->num_oids);
  ptr += OR_INT_SIZE;

  OR_PUT_INT (ptr, root_header->num_nulls);
  ptr += OR_INT_SIZE;

  OR_PUT_INT (ptr, root_header->num_keys);
  ptr += OR_INT_SIZE;

  OR_PUT_INT (ptr, root_header->unique);
  ptr += OR_INT_SIZE;

  OR_PUT_INT (ptr, root_header->reverse);
  ptr += OR_INT_SIZE;

  OR_PUT_INT (ptr, root_header->rev_level);
  ptr += OR_INT_SIZE;

  OR_PUT_INT (ptr, root_header->ovfid.fileid);
  ptr += OR_INT_SIZE;

  OR_PUT_SHORT (ptr, root_header->ovfid.volid);
  ptr += OR_SHORT_SIZE;

  ptr += BTREE_RESERVED_SIZE;	/* currently, not used */

  ptr = or_pack_domain (ptr, root_header->key_type, 0, 0);
/*  ptr = PTR_ALIGN (ptr, OR_INT_SIZE); */

  rec->type = REC_HOME;
  rec->length =
    ROOT_HEADER_FIXED_SIZE + or_packed_domain_size (root_header->key_type, 0);
}

/*
 * btree_read_root_header () -
 *   return:
 *   rec(in):
 *   root_header(out):
 *
 * Note: Reads the first record (header record) for a root page.
 */
void
btree_read_root_header (RECDES * rec, BTREE_ROOT_HEADER * root_header)
{
  char *ptr = rec->data;

  btree_read_node_header (rec, &root_header->node);
  ptr += NODE_HEADER_SIZE;

  root_header->num_oids = OR_GET_INT (ptr);
  ptr += OR_INT_SIZE;

  root_header->num_nulls = OR_GET_INT (ptr);
  ptr += OR_INT_SIZE;

  root_header->num_keys = OR_GET_INT (ptr);
  ptr += OR_INT_SIZE;

  root_header->unique = OR_GET_INT (ptr);
  ptr += OR_INT_SIZE;

  root_header->reverse = OR_GET_INT (ptr);
  ptr += OR_INT_SIZE;

  /* Btree revision */
  root_header->rev_level = OR_GET_INT (ptr);
  ptr += OR_INT_SIZE;

  /* unpack overflow VFID */
  root_header->ovfid.fileid = OR_GET_INT (ptr);
  ptr += OR_INT_SIZE;

  root_header->ovfid.volid = OR_GET_SHORT (ptr);
  ptr += OR_SHORT_SIZE;

  ptr += BTREE_RESERVED_SIZE;	/* currently, not used */

  ptr = or_unpack_domain (ptr, &root_header->key_type, 0);
/*  ptr = PTR_ALIGN (ptr, OR_INT_SIZE); */
}

/*
 * btree_write_fixed_portion_of_leaf_record () -
 *   return:
 *   rec(in):
 *   leaf_rec(in):
 *
 * Note: Writes the fixed portion (preamble) of a leaf record.
 * rec must be long enough to hold the header info.
 */
void
btree_write_fixed_portion_of_leaf_record (RECDES * rec, LEAF_REC * leaf_rec)
{
  char *ptr = rec->data;

  OR_PUT_INT (ptr, leaf_rec->ovfl.pageid);
  ptr += OR_INT_SIZE;

  OR_PUT_SHORT (ptr, leaf_rec->ovfl.volid);
  ptr += OR_SHORT_SIZE;

  OR_PUT_SHORT (ptr, leaf_rec->key_len);
}

/*
 * btree_read_fixed_portion_of_leaf_record () -
 *   return:
 *   rec(in):
 *   leaf_rec(in):
 *
 * Note: Reads the fixed portion (preamble) of a leaf record.
 */
void
btree_read_fixed_portion_of_leaf_record (RECDES * rec, LEAF_REC * leaf_rec)
{
  char *ptr = rec->data;

  leaf_rec->ovfl.pageid = OR_GET_INT (ptr);
  ptr += OR_INT_SIZE;

  leaf_rec->ovfl.volid = OR_GET_SHORT (ptr);
  ptr += OR_SHORT_SIZE;

  leaf_rec->key_len = OR_GET_SHORT (ptr);
}

/*
 * btree_write_fixed_portion_of_leaf_record_to_orbuf () -
 *   return:
 *   buf(in):
 *   leaf_rec(in):
 *
 * Note: Writes the fixed portion (preamble) of a leaf record using
 * the OR_BUF stuff.
 */
static void
btree_write_fixed_portion_of_leaf_record_to_orbuf (OR_BUF * buf,
						   LEAF_REC * leaf_rec)
{
  or_put_int (buf, leaf_rec->ovfl.pageid);
  or_put_short (buf, leaf_rec->ovfl.volid);
  or_put_short (buf, leaf_rec->key_len);
}

/*
 * btree_read_fixed_portion_of_leaf_record_from_orbuf () -
 *   return: NO_ERROR
 *   buf(in):
 *   leaf_rec(in):
 *
 * Note: Reads the fixed portion (preamble) of a leaf record using
 * the OR_BUF stuff.
 */
static int
btree_read_fixed_portion_of_leaf_record_from_orbuf (OR_BUF * buf,
						    LEAF_REC * leaf_rec)
{
  int rc = NO_ERROR;

  leaf_rec->ovfl.pageid = or_get_int (buf, &rc);

  if (rc == NO_ERROR)
    {
      leaf_rec->ovfl.volid = or_get_short (buf, &rc);
    }

  if (rc == NO_ERROR)
    {
      leaf_rec->key_len = or_get_short (buf, &rc);
    }

  return rc;
}

/*
 * btree_write_fixed_portion_of_non_leaf_record () -
 *   return:
 *   rec(in):
 *   non_leaf_rec(in):
 *
 * Note: Writes the fixed portion (preamble) of a non leaf record.
 * Rec must be long enough to hold the header info.
 */
static void
btree_write_fixed_portion_of_non_leaf_record (RECDES * rec,
					      NON_LEAF_REC * non_leaf_rec)
{
  char *ptr = rec->data;

  OR_PUT_INT (ptr, non_leaf_rec->pnt.pageid);
  ptr += OR_INT_SIZE;

  OR_PUT_SHORT (ptr, non_leaf_rec->pnt.volid);
  ptr += OR_SHORT_SIZE;

  OR_PUT_SHORT (ptr, non_leaf_rec->key_len);
}

/*
 * btree_read_fixed_portion_of_non_leaf_record () -
 *   return:
 *   rec(in):
 *   non_leaf_rec(in):
 *
 * Note: Reads the fixed portion (preamble) of a non leaf record.
 */
static void
btree_read_fixed_portion_of_non_leaf_record (RECDES * rec,
					     NON_LEAF_REC * non_leaf_rec)
{
  char *ptr = rec->data;

  non_leaf_rec->pnt.pageid = OR_GET_INT (ptr);
  ptr += OR_INT_SIZE;

  non_leaf_rec->pnt.volid = OR_GET_SHORT (ptr);
  ptr += OR_SHORT_SIZE;

  non_leaf_rec->key_len = OR_GET_SHORT (ptr);
}

/*
 * btree_write_fixed_portion_of_non_leaf_record_to_orbuf () -
 *   return:
 *   buf(in):
 *   nlf_rec(in):
 *
 * Note: Writes the fixed portion (preamble) of a non leaf record using
 * the OR_BUF stuff.
 */
static void
btree_write_fixed_portion_of_non_leaf_record_to_orbuf (OR_BUF * buf,
						       NON_LEAF_REC *
						       non_leaf_rec)
{
  or_put_int (buf, non_leaf_rec->pnt.pageid);
  or_put_short (buf, non_leaf_rec->pnt.volid);
  or_put_short (buf, non_leaf_rec->key_len);
}

/*
 * btree_read_fixed_portion_of_non_leaf_record_from_orbuf () -
 *   return: NO_ERROR
 *   buf(in):
 *   non_leaf_rec(in):
 *
 * Note: Reads the fixed portion (preamble) of a non leaf record using
 * the OR_BUF stuff.
 */
static int
btree_read_fixed_portion_of_non_leaf_record_from_orbuf (OR_BUF * buf,
							NON_LEAF_REC *
							non_leaf_rec)
{
  int rc = NO_ERROR;

  non_leaf_rec->pnt.pageid = or_get_int (buf, &rc);

  if (rc == NO_ERROR)
    {
      non_leaf_rec->pnt.volid = or_get_short (buf, &rc);
    }

  if (rc == NO_ERROR)
    {
      non_leaf_rec->key_len = or_get_short (buf, &rc);
    }

  return rc;
}

/*
 * btree_append_oid () -
 *   return:
 *   rec(in):
 *   oid(in):
 *
 * Note: Appends an OID onto the record.  Rec is assumed to have room
 * for the new OID and Rec.length points to the end of the record
 * where the new OID will go and is word aligned.
 */
static void
btree_append_oid (RECDES * rec, OID * oid)
{
  char *ptr;

  ptr = rec->data + rec->length;
  OR_PUT_OID (ptr, oid);
  rec->length += OR_OID_SIZE;
}

/*
 * btree_start_overflow_page () -
 *   return: NO_ERROR
 *   Rec(in):
 *   btid(in):
 *   new_vpid(in):
 *   new_page_ptr(out):
 *   near_vpid(in):
 *   class_oid(in):
 *   oid(in):
 *
 * Note: Gets a new overflow page and puts the first OID onto it.  The
 * VPID is returned.  Rec is assumed to be large enough to write
 * the overflow records.
 */
static int
btree_start_overflow_page (THREAD_ENTRY * thread_p, RECDES * rec,
			   BTID_INT * btid, VPID * new_vpid,
			   PAGE_PTR * new_page_ptr, VPID * near_vpid,
			   OID * class_oid, OID * oid)
{
  RECINS_STRUCT recins;		/* for recovery purposes */
  VPID next_vpid;
  int ret = NO_ERROR;

  /* get a new overflow page */
  *new_page_ptr = btree_get_new_page (thread_p, btid, new_vpid, near_vpid);
  if (*new_page_ptr == NULL)
    {
      goto exit_on_error;
    }

  /*
   * new page is the last overflow page, so there's no
   * following page and there will only be one oid on the page.
   */
  VPID_SET_NULL (&next_vpid);
  btree_write_overflow_header (rec, &next_vpid);
  if (spage_insert_at (thread_p, *new_page_ptr, HEADER, rec) != SP_SUCCESS)
    {
      goto exit_on_error;
    }

  /* insert the value in the new overflow page */
  rec->length = 0;
  if (BTREE_IS_UNIQUE (btid))
    {
      btree_append_oid (rec, class_oid);
    }
  btree_append_oid (rec, oid);

  if (spage_insert_at (thread_p, *new_page_ptr, 1, rec) != SP_SUCCESS)
    {
      goto exit_on_error;
    }

  /* log new overflow page changes for redo purposes */
  if (BTREE_IS_UNIQUE (btid))
    {
      recins.class_oid = *class_oid;
    }
  else
    {
      OID_SET_NULL (&recins.class_oid);
    }

  recins.oid = *oid;
  recins.rec_type = OVERFLOW;
  recins.oid_inserted = true;
  recins.ovfl_changed = false;
  recins.new_ovflpg = true;
  log_append_redo_data2 (thread_p, RVBT_LFRECORD_OIDINS,
			 &btid->sys_btid->vfid, *new_page_ptr, -1,
			 sizeof (RECINS_STRUCT), &recins);

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

  return ret;
}

/*
 * btree_get_key_length () -
 *   return: 
 *   key(in): 
 */
int
btree_get_key_length (DB_VALUE * key)
{
  if (key == NULL || db_value_is_null (key)
      || btree_multicol_key_is_null (key))
    {
      return 0;
    }

  return pr_writeval_disk_size (key);
}

/*
 * btree_write_record () -
 *   return: NO_ERROR
 *   btid(in):
 *   node_rec(in):
 *   key(in):
 *   is_leaf_page(in):
 *   is_overflow_key(in):
 *   key_len(in):
 *   during_loading(in):
 *   class_oid(in):
 *   oid(in):
 *   Rec(in):
 *
 * Note: This routine forms a btree record for both leaf and non leaf pages.
 *
 * node_rec is a NON_LEAF_REC * if we are writing a non leaf page,
 * otherwise it is a LEAF_REC *. ovfl_key indicates whether the key will
 * be written to the page or stored by the overflow manager. If we are
 * writing a non leaf record, oid should be NULL and will be ignored in
 * any case.
 */
int
btree_write_record (THREAD_ENTRY * thread_p, BTID_INT * btid, void *node_rec,
		    DB_VALUE * key, bool is_leaf_page, int is_overflow_key,
		    int key_len, bool during_loading, OID * class_oid,
		    OID * oid, RECDES * rec)
{
  VPID key_vpid;
  OR_BUF buf;
  int rc = NO_ERROR;

  or_init (&buf, rec->data, rec->area_size);

  if (is_leaf_page)
    {
      LEAF_REC *leaf_rec = (LEAF_REC *) node_rec;

      btree_write_fixed_portion_of_leaf_record_to_orbuf (&buf, leaf_rec);
    }
  else
    {
      NON_LEAF_REC *non_leaf_rec = (NON_LEAF_REC *) node_rec;

      btree_write_fixed_portion_of_non_leaf_record_to_orbuf (&buf,
							     non_leaf_rec);
    }

  /* write the key */
  if (is_overflow_key == false)
    {				/* key fits in page */
      PR_TYPE *pr_type;

      pr_type = ((is_leaf_page) ?
		 btid->key_type->type : btid->nonleaf_key_type->type);

      (*(pr_type->writeval)) (&buf, key);
    }
  else
    {
      /* overflow key */
      if (btree_store_overflow_key
	  (thread_p, btid, key, key_len, during_loading,
	   &key_vpid) != NO_ERROR)
	{
	  return ER_FAILED;
	}

      /* write the overflow VPID as the key */
      rc = or_put_int (&buf, key_vpid.pageid);
      if (rc == NO_ERROR)
	{
	  rc = or_put_short (&buf, key_vpid.volid);
	}
    }

  if (rc == NO_ERROR)
    {
      if (is_leaf_page)
	{
	  /* align to word boundary */
	  buf.ptr = PTR_ALIGN (buf.ptr, OR_INT_SIZE);

	  if (BTREE_IS_UNIQUE (btid))
	    {
	      /* write the class OID */
	      rc = or_put_oid (&buf, class_oid);
	      if (rc != NO_ERROR)
		{
		  rec->length = buf.ptr - buf.buffer;
		  return rc;
		}
	    }

	  /* write the OID */
	  rc = or_put_oid (&buf, oid);
	}
    }

  rec->length = buf.ptr - buf.buffer;

  return rc;
}

/*
 * btree_read_record () -
 *   return:
 *   btid(in):
 *   rec(in):
 *   key(in):
 *   rec_header(in):
 *   leaf_page(in):
 *   clear_key(in):
 *   offset(in):
 *   copy_key(in):
 *
 * Note: This routine reads a btree record for both leaf and non leaf pages.
 *
 * copy_key indicates whether strings should be malloced and copied
 * or just returned via pointer.  offset will point to the oid(s) following
 * the key for leaf pages.  clear_key will indicate whether the key needs
 * to be cleared via pr_clear_value by the caller.  If this record is
 * a leaf record, rec_header will be filled in with the LEAF_REC,
 * otherwise, rec_header will be filled in with the NON_LEAF_REC for this
 * record.
 *
 * If you don't want to actually read the key (possibly incurring a
 * malloc for the string), you can send a NULL pointer for the key.
 * readval() will do the right thing and simply skip the key in this case.
 */
void
btree_read_record (THREAD_ENTRY * thread_p, BTID_INT * btid, RECDES * rec,
		   DB_VALUE * key, void *rec_header, bool leaf_page,
		   bool * clear_key, int *offset, int copy_key)
{
  OR_BUF buf;
  int key_len;
  VPID overflow_vpid;
  char *copy_key_buf;
  int copy_key_buf_len;
  int rc = NO_ERROR;

  if (key)
    {
      db_make_null (key);
    }

  *clear_key = false;

#if defined(BTREE_DEBUG)
  if (!rec || !rec->data)
    {
      er_log_debug (ARG_FILE_LINE,
		    "btree_read_record: null node header pointer."
		    " Operation Ignored.");
      return;
    }
#endif /* BTREE_DEBUG */

  or_init (&buf, rec->data, rec->length);

  /*
   * Find the beginning position of the key within the record and read
   * the key length.
   */
  if (leaf_page)
    {
      LEAF_REC *leaf_rec = (LEAF_REC *) rec_header;

      if (btree_read_fixed_portion_of_leaf_record_from_orbuf (&buf, leaf_rec)
	  != NO_ERROR)
	{
	  return;
	}

      key_len = leaf_rec->key_len;
    }
  else
    {
      NON_LEAF_REC *non_leaf_rec = (NON_LEAF_REC *) rec_header;

      if (btree_read_fixed_portion_of_non_leaf_record_from_orbuf (&buf,
								  non_leaf_rec)
	  != NO_ERROR)
	{
	  return;
	}

      key_len = non_leaf_rec->key_len;
    }

  if (key_len >= 0)
    {				/* key is within page */
      TP_DOMAIN *key_domain;
      PR_TYPE *pr_type;

      key_domain = (leaf_page) ? btid->key_type : btid->nonleaf_key_type;
      pr_type = key_domain->type;

      copy_key_buf = NULL;
      copy_key_buf_len = 0;

      /*
       * When we read the key, must copy in two cases:
       *   1) we are told to via the copy_key flag, or 2) it is a set.
       */
      *clear_key = (key && copy_key) ? true : false;

      if (*clear_key)
	{			/* need to copy the key */
	  if (key_len <= btid->copy_buf_len)
	    {			/* check for copy_buf */
	      if (pr_type->id == DB_TYPE_MIDXKEY
		  || QSTR_IS_ANY_CHAR_OR_BIT (pr_type->id))
		{		/* check for the key type */
		  /* read key_val image into the copy_buf */
		  copy_key_buf = btid->copy_buf;
		  copy_key_buf_len = btid->copy_buf_len;
		}
	    }
	}

      if (pr_type->id != DB_TYPE_MIDXKEY)
	{
	  key_len = -1;
	}

      (*(pr_type->readval)) (&buf, key, key_domain, key_len, *clear_key,
			     copy_key_buf, copy_key_buf_len);
    }
  else
    {
      /* follow the chain of overflow key page pointers and fetch key */
      overflow_vpid.pageid = or_get_int (&buf, &rc);
      if (rc == NO_ERROR)
	{
	  overflow_vpid.volid = or_get_short (&buf, &rc);
	}
      if (rc != NO_ERROR)
	{
	  db_make_null (key);
	  return;
	}

      if (key)
	{
	  if (btree_load_overflow_key (thread_p, btid, &overflow_vpid, key) !=
	      NO_ERROR)
	    {
	      db_make_null (key);
	    }

	  /* we always clear overflow keys */
	  *clear_key = true;
	}
      else
	{
	  /* we aren't copying the key so they don't have to clear it */
	  *clear_key = false;
	}
    }

  buf.ptr = PTR_ALIGN (buf.ptr, OR_INT_SIZE);

  *offset = buf.ptr - buf.buffer;
}

/*
 * btree_dump_root_header () -
 *   return:
 *   rec(in):
 */
static void
btree_dump_root_header (RECDES rec)
{
  BTREE_ROOT_HEADER root_header;

  /* dump the root header information */

  /* output root header information */
  btree_read_root_header (&rec, &root_header);
  fprintf (stdout,
	   "\n==============    R O O T    P A G E   ================\n\n");
  fprintf (stdout, " Key_Type: %s\n",
	   pr_type_name (root_header.key_type->type->id));
  fprintf (stdout, " Num OIDs: %d, Num NULLs: %d, Num keys: %d\n",
	   root_header.num_oids, root_header.num_nulls, root_header.num_keys);
  fprintf (stdout, " OVFID: %d|%d\n",
	   root_header.ovfid.fileid, root_header.ovfid.volid);
  fprintf (stdout, " Btree Revision Level: %d\n", root_header.rev_level);
}

/*
 * btree_dump_key () -
 *   return:
 *   key(in):
 */
void
btree_dump_key (DB_VALUE * key)
{
  DB_TYPE key_type = DB_VALUE_DOMAIN_TYPE (key);
  PR_TYPE *pr_type = PR_TYPE_FROM_ID (key_type);

  fprintf (stdout, " ");
  (*(pr_type->fptrfunc)) (stdout, key);
  fprintf (stdout, " ");
}

/*
 * btree_dump_leaf_record () -
 *   return: nothing
 *   btid(in): B+tree index identifier
 *   rec(in): Pointer to a record in a leaf page of the tree
 *   n(in): Indentation left margin (number of preceding blanks)
 *
 * Note: Dumps the content of a leaf record, namely key and the set of
 * values for the key.
 */
static void
btree_dump_leaf_record (THREAD_ENTRY * thread_p, BTID_INT * btid,
			RECDES * rec, int n)
{
  LEAF_REC leaf_record;
  int i, k, oid_cnt;
  OID class_oid;
  OID oid;
  int key_len, offset;
  VPID overflow_vpid;
  DB_VALUE key;
  char *ptr;
  char *header_ptr;
  bool clear_key;
  int oid_size;

  if (BTREE_IS_UNIQUE (btid))
    {
      oid_size = 2 * OR_OID_SIZE;
    }
  else
    {
      oid_size = OR_OID_SIZE;
    }

  /* output the leaf record structure content */
  btree_print_space (n);

  btree_read_record (thread_p, btid, rec, &key, &leaf_record, true,
		     &clear_key, &offset, 0);
  key_len = btree_get_key_length (&key);

  if (leaf_record.key_len > 0)
    {
      /* regular key */
      fprintf (stdout, "Key_Len: %d Ovfl_Page: {%d , %d} ",
	       leaf_record.key_len, leaf_record.ovfl.volid,
	       leaf_record.ovfl.pageid);
    }
  else
    {
      /* overflow key */
      fprintf (stdout, "Key_Len: %d Ovfl_Page: {%d , %d} ",
	       key_len, leaf_record.ovfl.volid, leaf_record.ovfl.pageid);
      key_len = DISK_VPID_SIZE;
    }

  fprintf (stdout, "Key: ");
  btree_dump_key (&key);

  btree_clear_key_value (&clear_key, &key);

  overflow_vpid = leaf_record.ovfl;

  /* output the values */
  fprintf (stdout, "  Values: ");
  fprintf (stdout, "Oid_Cnt: %d ",
	   CEIL_PTVDIV (rec->length - offset, oid_size));
  ptr = rec->data + offset;
  if (BTREE_IS_UNIQUE (btid))
    {
      for (k = 0; k < CEIL_PTVDIV (rec->length - offset, oid_size); k++)
	{
	  /* values stored within the record */
	  if (k % 2 == 0)
	    {
	      fprintf (stdout, "\n");
	    }

	  OR_GET_OID (ptr, &class_oid);
	  ptr += OR_OID_SIZE;
	  OR_GET_OID (ptr, &oid);
	  ptr += OR_OID_SIZE;

	  fprintf (stdout, " (%d %d %d : %d, %d, %d) ",
		   class_oid.volid, class_oid.pageid, class_oid.slotid,
		   oid.volid, oid.pageid, oid.slotid);
	}
    }
  else
    {
      for (k = 0; k < CEIL_PTVDIV (rec->length - offset, OR_OID_SIZE); k++)
	{
	  /* values stored within the record */
	  if (k % 4 == 0)
	    {
	      fprintf (stdout, "\n");
	    }

	  OR_GET_OID (ptr, &oid);
	  ptr += OR_OID_SIZE;

	  fprintf (stdout, " (%d, %d, %d) ", oid.volid, oid.pageid,
		   oid.slotid);
	}
    }

  if (overflow_vpid.pageid != NULL_PAGEID)
    {
      /* record has an overflow page continuation */
      RECDES overflow_rec;
      PAGE_PTR overflow_page_ptr = NULL;

      overflow_rec.area_size = DB_PAGESIZE;
      overflow_rec.data = (char *) malloc (DB_PAGESIZE);

      fprintf (stdout,
	       "\n\n=======    O V E R F L O W   P A G E S     =========\n");
      fflush (stdout);

      /* get all the overflow pages and output their value content */
      while (overflow_vpid.pageid != NULL_PAGEID)
	{
	  fprintf (stdout, "\n ------ Overflow Page {%d , %d} \n",
		   overflow_vpid.volid, overflow_vpid.pageid);
	  overflow_page_ptr = pgbuf_fix (thread_p, &overflow_vpid, OLD_PAGE,
					 PGBUF_LATCH_READ,
					 PGBUF_UNCONDITIONAL_LATCH);

	  btree_get_header_ptr (overflow_page_ptr, &header_ptr);
	  btree_get_next_overflow_vpid (header_ptr, &overflow_vpid);

	  (void) spage_get_record (overflow_page_ptr, 1, &overflow_rec, COPY);

	  oid_cnt = CEIL_PTVDIV (overflow_rec.length, oid_size);
	  ptr = (char *) overflow_rec.data;
	  fprintf (stdout, "Oid_Cnt: %d ", oid_cnt);

	  if (BTREE_IS_UNIQUE (btid))
	    {
	      for (i = 0; i < oid_cnt; i++)
		{
		  if (i % 2 == 0)
		    {
		      fprintf (stdout, "\n");
		    }

		  OR_GET_OID (ptr, &class_oid);
		  ptr += OR_OID_SIZE;
		  OR_GET_OID (ptr, &oid);
		  ptr += OR_OID_SIZE;

		  fprintf (stdout, " (%d %d %d : %d, %d, %d) ",
			   class_oid.volid, class_oid.pageid,
			   class_oid.slotid, oid.volid, oid.pageid,
			   oid.slotid);
		}
	    }
	  else
	    {
	      for (i = 0; i < oid_cnt; i++)
		{
		  if (i % 4 == 0)
		    {
		      fprintf (stdout, "\n");
		    }

		  OR_GET_OID (ptr, &oid);
		  ptr += OR_OID_SIZE;

		  fprintf (stdout, " (%d, %d, %d) ", oid.volid,
			   oid.pageid, oid.slotid);
		}
	    }

	  pgbuf_unfix (thread_p, overflow_page_ptr);
	  overflow_page_ptr = NULL;
	}

      free_and_init (overflow_rec.data);
    }

  fprintf (stdout, "\n");
  fflush (stdout);
}

/*
 * btree_dump_non_leaf_record () -
 *   return: void
 *   btid(in):
 *   rec(in): Pointer to a record in a non_leaf page
 *   n(in): Indentation left margin (number of preceding blanks)
 *   print_key(in):
 *
 * Note: Dumps the content of a nonleaf record, namely key and child
 * page identifier.
 */
static void
btree_dump_non_leaf_record (THREAD_ENTRY * thread_p, BTID_INT * btid,
			    RECDES * rec, int n, int print_key)
{
  NON_LEAF_REC non_leaf_record;
  int key_len, offset;
  DB_VALUE key;
  bool clear_key;

  /* output the non_leaf record structure content */
  btree_read_record (thread_p, btid, rec, &key, &non_leaf_record, false,
		     &clear_key, &offset, 0);

  btree_print_space (n);
  fprintf (stdout, "Child_Page: {%d , %d} ",
	   non_leaf_record.pnt.volid, non_leaf_record.pnt.pageid);

  if (print_key)
    {
      key_len = btree_get_key_length (&key);
      fprintf (stdout, "Key_Len: %d  Key: ", key_len);
      btree_dump_key (&key);
    }
  else
    {
      fprintf (stdout, "No Key");
    }

  btree_clear_key_value (&clear_key, &key);

  fprintf (stdout, "\n");
  fflush (stdout);
}

/*
 * btree_get_new_page () - GET a NEW PAGE for the B+tree index
 *   return: The pointer to a newly allocated page for the given
 *           B+tree or NULL.
 *           The parameter vpid is set to the page identifier.
 *   btid(in): B+tree index identifier
 *   vpid(out): Set to the page identifier for the newly allocated page
 *   near_vpid(in): A page identifier that may be used in a nearby page
 *                  allocation. (It may be ignored.)
 *
 * Note: Allocates a new page for the B+tree
 */
static PAGE_PTR
btree_get_new_page (THREAD_ENTRY * thread_p, BTID_INT * btid, VPID * vpid,
		    VPID * near_vpid)
{
  PAGE_PTR page_ptr = NULL;

  if (file_alloc_pages (thread_p, &(btid->sys_btid->vfid), vpid, 1, near_vpid,
			btree_initialize_new_page, NULL) == NULL)
    {
      return NULL;
    }

  /*
   * Note: we fetch the page as old since it was initialized during the
   * allocation by btree_initialize_new_page, therfore, we care about
   * the current content of the page.
   */
  page_ptr = pgbuf_fix (thread_p, vpid, OLD_PAGE, PGBUF_LATCH_WRITE,
			PGBUF_UNCONDITIONAL_LATCH);
  if (page_ptr == NULL)
    {
      (void) file_dealloc_page (thread_p, &btid->sys_btid->vfid, vpid);
      return NULL;
    }

  return page_ptr;
}

/*
 * btree_initialize_new_page () -
 *   return: bool
 *   vfid(in): File where the new page belongs
 *   vpid(in): The new page
 *   ignore_npages(in): Number of contiguous allocated pages
 *                      (Ignored in this function. We allocate only one page)
 *   ignore_args(in): More arguments to function.
 *                    Ignored at this moment.
 *
 * Note: Initialize a newly allocated btree page.
 */
static bool
btree_initialize_new_page (THREAD_ENTRY * thread_p, const VFID * vfid,
			   const VPID * vpid, INT32 ignore_npages,
			   void *ignore_args)
{
  PAGE_PTR pgptr;

  /*
   * fetch and initialize the new page. The parameter UNANCHORED_KEEP_
   * SEQUENCE indicates that the order of records will be preserved
   * during insertions and deletions.
   */

  pgptr = pgbuf_fix (thread_p, vpid, NEW_PAGE, PGBUF_LATCH_WRITE,
		     PGBUF_UNCONDITIONAL_LATCH);
  if (pgptr == NULL)
    {
      return false;
    }

  spage_initialize (thread_p, pgptr, UNANCHORED_KEEP_SEQUENCE, INT_ALIGNMENT,
		    DONT_SAFEGUARD_RVSPACE);
  log_append_redo_data2 (thread_p, RVBT_GET_NEWPAGE, vfid, pgptr, -1, 0,
			 NULL);
  pgbuf_set_dirty (thread_p, pgptr, FREE);

  return true;
}

/*
 * btree_search_nonleaf_page () -
 *   return: NO_ERROR
 *   btid(in):
 *   page_ptr(in): Pointer to the non_leaf page to be searched
 *   key(in): Key to find
 *   slot_id(out): Set to the record number that contains the key
 *   child_vpid(out): Set to the child page identifier to be followed,
 *                    or NULL_PAGEID
 *
 * Note: Binary search the page to locate the record that contains the
 * child page pointer to be followed to locate the key, and
 * return the page identifier for this child page.
 */
static int
btree_search_nonleaf_page (THREAD_ENTRY * thread_p, BTID_INT * btid,
			   PAGE_PTR page_ptr, DB_VALUE * key, INT16 * slot_id,
			   VPID * child_vpid)
{
  int key_cnt, offset;
  int c;
  bool clear_key;
  /* the start position of non-equal-value column */
  int start_col, left_start_col, right_start_col;
  INT16 left, right;
  INT16 middle = 0;
  DB_VALUE temp_key;
  RECDES rec;
  char *header_ptr;
  NON_LEAF_REC non_leaf_rec;

  /* initialize child page identifier */
  VPID_SET_NULL (child_vpid);

#if defined(BTREE_DEBUG)
  if (!page_ptr || !key || db_value_is_null (key))
    {
      er_log_debug (ARG_FILE_LINE,
		    "btree_search_nonleaf_page: null page/key pointer."
		    " Operation Ignored.");
      return ER_FAILED;
    }
#endif /* BTREE_DEBUG */

  /* read the node header */
  btree_get_header_ptr (page_ptr, &header_ptr);
  key_cnt = BTREE_GET_NODE_KEY_CNT (header_ptr);	/* get the key count */

  if (spage_number_of_records (page_ptr) <= 1)
    {				/* node record underflow */
      er_log_debug (ARG_FILE_LINE,
		    "btree_search_nonleaf_page: node key count underflow: %d",
		    key_cnt);
      return ER_FAILED;
    }

  if (key_cnt == 0)
    {
      /*
       * node has no keys, but a child page pointer
       * So, follow this pointer
       */
      if (spage_get_record (page_ptr, 1, &rec, PEEK) != S_SUCCESS)
	{
	  return ER_FAILED;
	}

      btree_read_fixed_portion_of_non_leaf_record (&rec, &non_leaf_rec);
      *slot_id = 1;
      *child_vpid = non_leaf_rec.pnt;

      return NO_ERROR;
    }

  /* binary search the node to find the child page pointer to be followed */
  c = 0;
  left_start_col = right_start_col = 0;

  left = 1;
  right = key_cnt;

  while (left <= right)
    {
      middle = CEIL_PTVDIV ((left + right), 2);	/* get the middle record */
      if (spage_get_record (page_ptr, middle, &rec, PEEK) != S_SUCCESS)
	{
	  return ER_FAILED;
	}

      btree_read_record (thread_p, btid, &rec, &temp_key, &non_leaf_rec,
			 false, &clear_key, &offset, 0);

      if (DB_VALUE_DOMAIN_TYPE (key) == DB_TYPE_MIDXKEY)
	{
	  start_col = MIN (left_start_col, right_start_col);
	}

      c = (*(btid->nonleaf_key_type->type->cmpval)) (key, &temp_key,
						     btid->key_type,
						     btid->reverse, 0, 1,
						     &start_col);

      btree_clear_key_value (&clear_key, &temp_key);

      if (c == 0)
	{
	  /* child page to be followed has been found */
	  *slot_id = middle;
	  *child_vpid = non_leaf_rec.pnt;

	  return NO_ERROR;
	}
      else if (c < 0)
	{
	  right = middle - 1;
	  right_start_col = start_col;
	}
      else
	{
	  left = middle + 1;
	  left_start_col = start_col;
	}
    }

  if (c < 0)
    {
      /* child page is the one pointed by the middle record */
      *slot_id = middle;
      *child_vpid = non_leaf_rec.pnt;

      return NO_ERROR;
    }
  else
    {
      /* child page is the one pointed by the record right to the middle  */
      if (spage_get_record (page_ptr, middle + 1, &rec, PEEK) != S_SUCCESS)
	{
	  return ER_FAILED;
	}

      btree_read_fixed_portion_of_non_leaf_record (&rec, &non_leaf_rec);
      *child_vpid = non_leaf_rec.pnt;
      *slot_id = middle + 1;

      return NO_ERROR;
    }
}

/*
 * btree_search_leaf_page () -
 *   return: int false: key does not exists, true: key exists
 *           (if error, false and slot_id = NULL_SLOTID)
 *   btid(in):
 *   page_ptr(in): Pointer to the leaf page to be searched
 *   key(in): Key to search
 *   slot_id(out): Set to the record number that contains the key if key is
 *                 found, or the record number in which the key should have
 *                 been located if it doesn't exist
 *
 * Note: Binary search the page to find the location of the key.
 * If the key does not exist, it returns the location where it
 * should have been located.
 */
static int
btree_search_leaf_page (THREAD_ENTRY * thread_p, BTID_INT * btid,
			PAGE_PTR page_ptr, DB_VALUE * key, INT16 * slot_id)
{
  int key_cnt, offset;
  int c;
  bool clear_key;
  /* the start position of non-equal-value column */
  int start_col, left_start_col, right_start_col;
  INT16 left, right, middle;
  DB_VALUE temp_key;
  RECDES Rec;
  char *header_ptr;
  LEAF_REC leaf_rec;

  *slot_id = NULL_SLOTID;

#if defined(BTREE_DEBUG)
  if (!key || db_value_is_null (key))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_BTREE_NULL_KEY, 0);
      return false;
    }
#endif /* BTREE_DEBUG */

  /* read the header record */
  btree_get_header_ptr (page_ptr, &header_ptr);
  key_cnt = BTREE_GET_NODE_KEY_CNT (header_ptr);	/* get the key count */

  c = 0;
  middle = 0;
  left_start_col = right_start_col = 0;

  if (key_cnt < 0)
    {
      er_log_debug (ARG_FILE_LINE,
		    "btree_search_leaf_page: node key count underflow: %d.",
		    key_cnt);
      return false;
    }

  /*
   * binary search the node to find if the key exits and in which record it
   * exists, or if it doesn't exist , the in which record it should have been
   * located to preserve the order of keys
   */

  left = 1;
  right = key_cnt;

  while (left <= right)
    {
      middle = CEIL_PTVDIV ((left + right), 2);	/* get the middle record */
      if (spage_get_record (page_ptr, middle, &Rec, PEEK) != S_SUCCESS)
	{
	  er_log_debug (ARG_FILE_LINE,
			"btree_search_leaf_page: sp_getrec fails for middle record.");
	  return false;
	}

      btree_read_record (thread_p, btid, &Rec, &temp_key, &leaf_rec, true,
			 &clear_key, &offset, 0);

      if (DB_VALUE_DOMAIN_TYPE (key) == DB_TYPE_MIDXKEY)
	{
	  start_col = MIN (left_start_col, right_start_col);
	}

      c = (*(btid->key_type->type->cmpval)) (key, &temp_key, btid->key_type,
					     btid->reverse, 0, 1, &start_col);

      btree_clear_key_value (&clear_key, &temp_key);

      if (c == 0)
	{
	  /* key exists in the middle record */
	  *slot_id = middle;

	  return true;
	}
      else if (c < 0)
	{
	  right = middle - 1;
	  right_start_col = start_col;
	}
      else
	{
	  left = middle + 1;
	  left_start_col = start_col;
	}
    }

  if (c < 0)
    {
      /* key not exists, should be inserted in the current middle record */
      *slot_id = middle;

#if defined(BTREE_DEBUG)
      er_log_debug (ARG_FILE_LINE,
		    "btree_search_leaf_page: key not exists, "
		    "should be inserted in the current middle record.");
#endif /* BTREE_DEBUG */

      return false;
    }
  else
    {
      /* key not exists, should be inserted in the record right to the middle */
      *slot_id = middle + 1;

#if defined(BTREE_DEBUG)
      er_log_debug (ARG_FILE_LINE,
		    "btree_search_leaf_page: key not exists, "
		    "should be inserted in the record right to the middle.");
#endif /* BTREE_DEBUG */

      return false;
    }
}

/*
 * xbtree_add_index () - ADD (create) a new B+tree INDEX
 *   return: BTID * (btid on success and NULL on failure)
 *   btid(out): Set to the created B+tree index identifier
 *              (Note: btid->vfid.volid should be set by the caller)
 *   key_type(in): Key type of the index to be created.
 *   class_oid(in): OID of the class for which the index is created
 *   attr_id(in): Identifier of the attribute of the class for which the
 *                index is created.
 *   unique_btree(in):
 *   reverse_btree(in):
 *   num_oids(in):
 *   num_nulls(in):
 *   num_keys(in):
 *
 * Note: Creates the B+tree index. A file identifier (index identifier)
 * is defined on the given volume. This identifier is used by
 * insertion, deletion and search routines, for the created
 * index. The routine allocates the root page of the tree and
 * initializes the root header information.
 */
BTID *
xbtree_add_index (THREAD_ENTRY * thread_p, BTID * btid, TP_DOMAIN * key_type,
		  OID * class_oid, int attr_id, int is_unique_btree,
		  int is_reverse_btree, int num_oids, int num_nulls,
		  int num_keys)
{
  BTREE_ROOT_HEADER root_header;
  RECDES rec;
  VPID vpid;
  PAGE_PTR page_ptr = NULL;
  FILE_BTREE_DES btree_descriptor;
  bool is_file_created = false;

  rec.data = NULL;

  /* create a file descriptor */
  if (class_oid != NULL)
    {
      COPY_OID (&btree_descriptor.class_oid, class_oid);
      btree_descriptor.attr_id = attr_id;
    }
  else
    {
      OID_SET_NULL (&btree_descriptor.class_oid);
      btree_descriptor.attr_id = attr_id;
    }

  /* create a file descriptor, allocate and initialize the root page */

  if (file_create
      (thread_p, &btid->vfid, 2, FILE_BTREE, &btree_descriptor, &vpid,
       1) == NULL)
    {
      goto error;
    }

  is_file_created = true;

  if (btree_initialize_new_page (thread_p, &btid->vfid, &vpid, 1, NULL) ==
      false)
    {
      goto error;
    }

  /*
   * Note: we fetch the page as old since it was initialized by
   * btree_initialize_new_page, therfore, we care about the current content of
   * the page.
   */
  page_ptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_WRITE,
			PGBUF_UNCONDITIONAL_LATCH);
  if (page_ptr == NULL)
    {
      goto error;
    }

  /* form the root header information */
  root_header.node.node_type = LEAF_NODE;
  root_header.node.key_cnt = 0;
  root_header.node.max_key_len = 0;
  VPID_SET_NULL (&root_header.node.next_vpid);
  root_header.key_type = key_type;

  if (is_unique_btree)
    {
      root_header.num_oids = num_oids;
      root_header.num_nulls = num_nulls;
      root_header.num_keys = num_keys;
      root_header.unique = is_unique_btree;
    }
  else
    {
      root_header.num_oids = -1;
      root_header.num_nulls = -1;
      root_header.num_keys = -1;
      root_header.unique = false;
    }

  if (is_reverse_btree)
    {
      root_header.reverse = true;
    }
  else
    {
      root_header.reverse = false;
    }

  VFID_SET_NULL (&root_header.ovfid);
  root_header.rev_level = BTREE_CURRENT_REV_LEVEL;

  rec.area_size = DB_PAGESIZE;
  rec.data = (char *) malloc (DB_PAGESIZE);
  if (rec.data == NULL)
    {
      goto error;
    }

  btree_write_root_header (&rec, &root_header);
  /* insert the root header information into the root page */
  if (spage_insert_at (thread_p, page_ptr, HEADER, &rec) != SP_SUCCESS)
    {
      goto error;
    }

  /* log the new header record for redo purposes */
  log_append_redo_data2 (thread_p, RVBT_NDHEADER_INS, &btid->vfid, page_ptr,
			 HEADER, rec.length, rec.data);

  pgbuf_set_dirty (thread_p, page_ptr, FREE);
  page_ptr = NULL;
  free_and_init (rec.data);

  /* set the B+tree index identifier */
  btid->root_pageid = vpid.pageid;

  return btid;

error:
  if (page_ptr)
    {
      pgbuf_unfix (thread_p, page_ptr);
      page_ptr = NULL;
    }

  if (rec.data)
    {
      free_and_init (rec.data);
    }

  if (is_file_created)
    {
      (void) file_destroy (thread_p, &btid->vfid);
    }

  VFID_SET_NULL (&btid->vfid);
  btid->root_pageid = NULL_PAGEID;

  return NULL;
}

/*
 * xbtree_delete_index () -
 *   return: NO_ERROR
 *   btid(in): B+tree index identifier
 *
 * Note: Removes the B+tree index. All pages associated with the index
 * are removed. After the routine is called, the index identifier
 * is not valid any more.
 */
int
xbtree_delete_index (THREAD_ENTRY * thread_p, BTID * btid)
{
  PAGE_PTR P = NULL;
  VPID P_vpid;
  char *header_ptr;
  VFID ovfid;
  int ret = NO_ERROR;

  P_vpid.volid = btid->vfid.volid;	/* read the root page */
  P_vpid.pageid = btid->root_pageid;
  P = pgbuf_fix (thread_p, &P_vpid, OLD_PAGE, PGBUF_LATCH_WRITE,
		 PGBUF_UNCONDITIONAL_LATCH);
  if (P == NULL)
    {
      goto exit_on_error;
    }

  /* read the header record */
  btree_get_header_ptr (P, &header_ptr);
  BTREE_GET_OVFID (header_ptr, &ovfid);
  pgbuf_unfix (thread_p, P);
  P = NULL;

  btid->root_pageid = NULL_PAGEID;

  /*
   * even if the first destroy fails for some reason, still try and
   * destroy the overflow file if there is one.
   */
  ret = file_destroy (thread_p, &btid->vfid);
  if (ret != NO_ERROR)
    {
      goto exit_on_error;
    }

  if (!VFID_ISNULL (&ovfid))
    {
      ret = file_destroy (thread_p, &ovfid);
      if (ret != NO_ERROR)
	{
	  goto exit_on_error;
	}
    }

  return ret;

exit_on_error:

  if (P)
    {
      pgbuf_unfix (thread_p, P);
      P = NULL;
    }

  if (ret == NO_ERROR)
    {
      ret = er_errid ();
      if (ret == NO_ERROR)
	{
	  ret = ER_FAILED;
	}
    }

  return ret;
}

/*
 * btree_generate_prefix_domain () -
 *   return:
 *   btid(in):
 *
 * Note: This routine returns a varying domain of the same precision
 * for fixed domains which are one of the string types.  For all other
 * domains, it returns the same domain.
 */
TP_DOMAIN *
btree_generate_prefix_domain (BTID_INT * btid)
{
  TP_DOMAIN *domain = btid->key_type;
  TP_DOMAIN *var_domain;
  DB_TYPE dbtype = domain->type->id;
  DB_TYPE vartype;

  /* varying domains did not come into use until btree revision level 1 */
  if (!pr_is_variable_type (dbtype) && pr_is_string_type (dbtype))
    {
      switch (dbtype)
	{
	case DB_TYPE_CHAR:
	  vartype = DB_TYPE_VARCHAR;
	  break;
	case DB_TYPE_NCHAR:
	  vartype = DB_TYPE_VARNCHAR;
	  break;
	case DB_TYPE_BIT:
	  vartype = DB_TYPE_VARBIT;
	  break;
	default:
#if defined(CUBRID_DEBUG)
	  printf ("Corrupt domain in btree_generate_prefix_domain\n");
#endif /* CUBRID_DEBUG */
	  return NULL;
	}

      var_domain = tp_domain_resolve (vartype, domain->class_mop,
				      domain->precision, domain->scale,
				      domain->setdomain);
    }
  else
    {
      var_domain = domain;
    }

  return var_domain;
}

/*
 * btree_glean_root_header_info () - 
 *   return: 
 *   root_header(in): 
 *   btid(in): 
 * 
 * Note: This captures the interesting header info into the BTID_INT structure.
 */
int
btree_glean_root_header_info (BTREE_ROOT_HEADER * root_header,
			      BTID_INT * btid)
{
  int rc;
  TP_DOMAIN *domain, *set_domain;

  rc = NO_ERROR;

  btid->unique = root_header->unique;
  btid->reverse = root_header->reverse;
  btid->key_type = root_header->key_type;

  btid->ovfid = root_header->ovfid;	/* structure copy */

  /*
   * check for the last element domain of partial-key and key is desc;
   * set default according to reverse info
   * for btree_range_search, part_key_desc is re-set at btree_initialize_bts
   */
  btid->part_key_desc = btid->last_key_desc = btid->reverse;

  /* check for last key domain is desc */
  if (!BTREE_IS_LAST_KEY_DESC (btid))
    {
      domain = btid->key_type;
      if (domain->type->id == DB_TYPE_MIDXKEY)
	{
	  domain = domain->setdomain;
	}

      /* get the last key domain */
      while (domain->next != NULL)
	{
	  domain = domain->next;
	}

      btid->last_key_desc = domain->is_desc;
    }

  /* init index key copy_buf info */
  btid->copy_buf = NULL;
  btid->copy_buf_len = 0;

  /* this must be discovered after the Btree revision level */
  btid->nonleaf_key_type = btree_generate_prefix_domain (btid);

  return rc;

exit_on_error:

  rc = er_errid ();
  if (rc == NO_ERROR)
    {
      rc = ER_GENERIC_ERROR;
    }

  return rc;
}

#if 0				/* TODO: currently, unused */
/*
 * btree_get_key_oid_count () -
 *   return:
 *   btid(in):
 *   key(in):
 *
 * Note: This returns the number of object identifiers in the btree with
 * the given key_value.
 */
static int
btree_get_key_oid_count (BTID * btid, DB_VALUE * key)
{
  PAGE_PTR P = NULL;
  PAGE_PTR Q = NULL;
  VPID P_vpid, O_vpid;
  int found, oid_cnt;
  bool clear_key;
  int offset;
  RECDES Rec;
  INT16 slot_id;
  LEAF_REC Leaf_Pnt;
  BTREE_ROOT_HEADER root_header;
  VPID Root_vpid;
  PAGE_PTR Root = NULL;
  BTID_INT btid_int;

  Root_vpid.pageid = btid->root_pageid;
  Root_vpid.volid = btid->vfid.volid;
  Root = pgbuf_fix (thread_p, &Root_vpid, OLD_PAGE, PGBUF_LATCH_READ,
		    PGBUF_UNCONDITIONAL_LATCH);
  if (Root == NULL
      || (spage_get_record (Root, HEADER, &Rec, PEEK) != S_SUCCESS))
    {
      goto error;
    }

  btree_read_root_header (&Rec, &root_header);
  pgbuf_unfix (thread_p, Root);
  Root = NULL;

  btid_int.sys_btid = btid;
  if (btree_glean_root_header_info (&root_header, &btid_int) != NO_ERROR)
    {
      goto error;
    }

  if (key == NULL || db_value_is_null (key)
      || btree_multicol_key_is_null (key))
    {
      return root_header.num_nulls;
    }

  P = btree_locate_key (&btid_int, key, &P_vpid, &slot_id, &found);

  if (!found)
    {
      if (slot_id == NULL_SLOTID)
	{
	  goto error;
	}
      /* not found */
      pgbuf_unfix (thread_p, P);
      P = NULL;
      oid_cnt = 0;
    }
  else
    {
      if (spage_get_record (P, slot_id, &Rec, PEEK) != S_SUCCESS)
	{
	  goto error;
	}

      btree_read_record (thread_p, &btid_int, &Rec, NULL, &Leaf_Pnt, true,
			 &clear_key, &offset, 0);

      O_vpid = Leaf_Pnt.ovfl;

      if (BTREE_IS_UNIQUE (&btid_int))
	{
	  oid_cnt = CEIL_PTVDIV (Rec.length - offset, (2 * OR_OID_SIZE));
	}
      else
	{
	  oid_cnt = CEIL_PTVDIV (Rec.length - offset, OR_OID_SIZE);
	}

      while (O_vpid.pageid != NULL_PAGEID)
	{
	  Q = pgbuf_fix (thread_p, &O_vpid, OLD_PAGE, PGBUF_LATCH_READ,
			 PGBUF_UNCONDITIONAL_LATCH);
	  if (Q == NULL
	      || (spage_get_record (Q, HEADER, &Rec, PEEK) != S_SUCCESS))
	    {
	      goto error;
	    }

	  /* set up for the next overflow page, if any */
	  btree_read_overflow_header (&Rec, &O_vpid);

	  /* add the overflow OIDs to the cnt */
	  if (spage_get_record (Q, 1, &Rec, PEEK) != S_SUCCESS)
	    {
	      goto error;
	    }

	  if (BTREE_IS_UNIQUE (&btid_int))
	    {
	      oid_cnt += CEIL_PTVDIV (Rec.length, (2 * OR_OID_SIZE));
	    }
	  else
	    {
	      oid_cnt += CEIL_PTVDIV (Rec.length, OR_OID_SIZE);
	    }
	  pgbuf_unfix (thread_p, Q);
	  Q = NULL;
	}

      pgbuf_unfix (thread_p, P);
      P = NULL;
    }

  return oid_cnt;

error:

  if (P)
    {
      pgbuf_unfix (thread_p, P);
      P = NULL;
    }
  if (Q)
    {
      pgbuf_unfix (thread_p, Q);
      Q = NULL;
    }
  if (Root)
    {
      pgbuf_unfix (thread_p, Root);
      Root = NULL;
    }

  return -1;
}
#endif /* 0 */

/*
 * xbtree_find_unique () -
 *   return:
 *   btid(in):
 *   key(in):
 *   class_oid(in):
 *   oid(in):
 *   is_all_class_srch(in):
 *
 * Note: This returns the oid for the given key.  It assumes that the
 * btree is unique.
 */
BTREE_SEARCH
xbtree_find_unique (THREAD_ENTRY * thread_p, BTID * btid, DB_VALUE * key,
		    OID * class_oid, OID * oid, bool is_all_class_srch)
{
  BTREE_SCAN btree_scan;
  int oid_cnt;
  BTREE_SEARCH status;
  INDX_SCAN_ID index_scan_id;
  /* Unique btree can have at most 1 OID for a key */
  OID temp_oid[2];

  BTREE_INIT_SCAN (&btree_scan);

  index_scan_id.oid_list.oid_cnt = oid_cnt = 0;
  index_scan_id.oid_list.oidp = temp_oid;
  /* do not use copy_buf for key-val scan, only use for key-range scan */
  index_scan_id.copy_buf = NULL;
  index_scan_id.copy_buf_len = 0;

  if (key == NULL || db_value_is_null (key)
      || btree_multicol_key_is_null (key))
    {
      status = BTREE_KEY_NOTFOUND;
    }
  else
    {
      oid_cnt = btree_keyval_search (thread_p, btid, true, &btree_scan, key,
				     class_oid, index_scan_id.oid_list.oidp,
				     2 * sizeof (OID), NULL, &index_scan_id,
				     is_all_class_srch);
      if (oid_cnt == -1 || (oid_cnt > 1))
	{
	  if (oid_cnt > 1)
	    {
	      COPY_OID (oid, index_scan_id.oid_list.oidp);

	      /* clear all the used keys */
	      btree_scan_clear_key (&btree_scan);
	    }
	  status = BTREE_ERROR_OCCURRED;
	}
      else if (oid_cnt == 0)
	{
	  status = BTREE_KEY_NOTFOUND;
	}
      else
	{
	  COPY_OID (oid, index_scan_id.oid_list.oidp);
	  status = BTREE_KEY_FOUND;
	}
    }

  /* do not use copy_buf for key-val scan, only use for key-range scan */

  return status;
}

/*
 * btree_find_foreign_key () -
 *   return:
 *   btid(in):
 *   key(in):
 *   class_oid(in):
 */
int
btree_find_foreign_key (THREAD_ENTRY * thread_p, BTID * btid, DB_VALUE * key,
			OID * class_oid)
{
  BTREE_SCAN btree_scan;
  int oid_cnt;
  INDX_SCAN_ID index_scan_id;
  OID oid_buf[2];

  BTREE_INIT_SCAN (&btree_scan);

  index_scan_id.oid_list.oid_cnt = oid_cnt = 0;
  index_scan_id.oid_list.oidp = oid_buf;
  /* do not use copy_buf for key-val scan, only use for key-range scan */
  index_scan_id.copy_buf = NULL;
  index_scan_id.copy_buf_len = 0;

  if (key == NULL || db_value_is_null (key)
      || btree_multicol_key_is_null (key))
    {
      return 0;
    }

  oid_cnt =
    btree_keyval_search (thread_p, btid, true, &btree_scan, key, class_oid,
			 index_scan_id.oid_list.oidp, 2 * sizeof (OID), NULL,
			 &index_scan_id, false);

  btree_scan_clear_key (&btree_scan);

  return oid_cnt;
}

/*
 * btree_scan_clear_key () -
 *   return:
 *   btree_scan(in):
 */
void
btree_scan_clear_key (BTREE_SCAN * btree_scan)
{
  btree_clear_key_value (&btree_scan->clear_cur_key, &btree_scan->cur_key);
  btree_clear_key_value (&btree_scan->key_range.clear_lower,
			 &btree_scan->key_range.lower_value);
  btree_clear_key_value (&btree_scan->key_range.clear_upper,
			 &btree_scan->key_range.upper_value);
}

/*
 * xbtree_class_test_unique () -
 *   return: int
 *   buf(in):
 *   buf_size(in):
 *
 * Note: Return NO_ERROR if the btrees given are unique.
 * Return ER_BTREE_UNIQUE_FAILED if one of the unique tests failed.
 * This is used for interpreter and xasl batch checking of uniqueness.
 */
int
xbtree_class_test_unique (THREAD_ENTRY * thread_p, char *buf, int buf_size)
{
  int status = NO_ERROR;
  char *bufp, *buf_endptr;
  BTID btid;

  bufp = buf;
  buf_endptr = (buf + buf_size);

  while ((bufp < buf_endptr) && (status == NO_ERROR))
    {
      /* unpack the BTID */
      bufp = or_unpack_btid (bufp, &btid);
      bufp = PTR_ALIGN (bufp, OR_INT_SIZE);

      /* check if the btree is unique */
      if ((status == NO_ERROR) && (xbtree_test_unique (thread_p, &btid) != 1))
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_BTREE_UNIQUE_FAILED,
		  0);
	  status = ER_BTREE_UNIQUE_FAILED;
	}
    }

  return status;
}

/*
 * xbtree_test_unique () -
 *   return: int
 *   btid(in): B+tree index identifier
 *
 * Note: Return 1 (true) if the index is unique, return 0 if
 * the index is not unique, return -1 if the btree isn't
 * keeping track of unique statistics (a regular, plain jane btree).
 */
int
xbtree_test_unique (THREAD_ENTRY * thread_p, BTID * btid)
{
  VPID root_vpid;
  PAGE_PTR root = NULL;
  char *header_ptr;
  INT32 num_nulls, num_keys, num_oids;

  root_vpid.pageid = btid->root_pageid;
  root_vpid.volid = btid->vfid.volid;

  root = pgbuf_fix (thread_p, &root_vpid, OLD_PAGE, PGBUF_LATCH_READ,
		    PGBUF_UNCONDITIONAL_LATCH);
  if (root == NULL)
    {
      goto error;
    }

  btree_get_header_ptr (root, &header_ptr);
  num_nulls = BTREE_GET_NUM_NULLS (header_ptr);
  num_keys = BTREE_GET_NUM_KEYS (header_ptr);
  num_oids = BTREE_GET_NUM_OIDS (header_ptr);
  pgbuf_unfix (thread_p, root);
  root = NULL;

  if (num_nulls == -1)
    {
      return -1;
    }
  else if ((num_nulls + num_keys) != num_oids)
    {
      return 0;
    }
  else
    {
      return 1;
    }

error:

  if (root)
    {
      pgbuf_unfix (thread_p, root);
      root = NULL;
    }

  return 0;
}

/*
 * xbtree_get_unique () -
 *   return:
 *   btid(in):
 */
int
xbtree_get_unique (THREAD_ENTRY * thread_p, BTID * btid)
{
  VPID root_vpid;
  PAGE_PTR root = NULL;
  char *header_ptr;
  INT32 unique;

  root_vpid.pageid = btid->root_pageid;
  root_vpid.volid = btid->vfid.volid;

  root = pgbuf_fix (thread_p, &root_vpid, OLD_PAGE, PGBUF_LATCH_READ,
		    PGBUF_UNCONDITIONAL_LATCH);
  if (root == NULL)
    {
      goto error;
    }

  btree_get_header_ptr (root, &header_ptr);
  unique = BTREE_GET_UNIQUE (header_ptr);

  pgbuf_unfix (thread_p, root);
  root = NULL;

  return unique;

error:

  if (root)
    {
      pgbuf_unfix (thread_p, root);
      root = NULL;
    }

  return 0;

}

/*
 * btree_is_unique () -
 *   return:
 *   btid(in): B+tree index identifier
 *
 * Note: Return 1 (true) if the btree is a btree for uniques, 0 if
 * the btree is an index btree.
 */
int
btree_is_unique (THREAD_ENTRY * thread_p, BTID * btid)
{
  VPID root_vpid;
  PAGE_PTR root = NULL;
  char *header_ptr;
  INT32 num_nulls;

  root_vpid.pageid = btid->root_pageid;
  root_vpid.volid = btid->vfid.volid;

  root = pgbuf_fix (thread_p, &root_vpid, OLD_PAGE, PGBUF_LATCH_READ,
		    PGBUF_UNCONDITIONAL_LATCH);
  if (root == NULL)
    {
      goto error;
    }

  btree_get_header_ptr (root, &header_ptr);
  num_nulls = BTREE_GET_NUM_NULLS (header_ptr);

  pgbuf_unfix (thread_p, root);
  root = NULL;

  return (num_nulls != -1);

error:

  if (root)
    {
      pgbuf_unfix (thread_p, root);
      root = NULL;
    }

  return 0;			/* could use a better return value */
}

/*
 * btree_get_unique_statistics () -
 *   returns: NO_ERROR
 *   btid(in):
 *   oid_cnt(in):
 *   null_cnt(in):
 *   key_cnt(in):
 *
 * Note: Reads the unique btree statistics from the root header.  If
 * the btree is not a unique btree, all the stats will be -1.
 */
int
btree_get_unique_statistics (THREAD_ENTRY * thread_p, BTID * btid,
			     int *oid_cnt, int *null_cnt, int *key_cnt)
{
  VPID root_vpid;
  PAGE_PTR root = NULL;
  char *header_ptr;
  int ret = NO_ERROR;

  root_vpid.pageid = btid->root_pageid;
  root_vpid.volid = btid->vfid.volid;

  root = pgbuf_fix (thread_p, &root_vpid, OLD_PAGE, PGBUF_LATCH_READ,
		    PGBUF_UNCONDITIONAL_LATCH);
  if (root == NULL)
    {
      goto exit_on_error;
    }

  btree_get_header_ptr (root, &header_ptr);
  *oid_cnt = BTREE_GET_NUM_OIDS (header_ptr);
  *null_cnt = BTREE_GET_NUM_NULLS (header_ptr);
  *key_cnt = BTREE_GET_NUM_KEYS (header_ptr);

  pgbuf_unfix (thread_p, root);
  root = NULL;

  return ret;

exit_on_error:

  if (root)
    {
      pgbuf_unfix (thread_p, root);
      root = NULL;
    }

  if (ret == NO_ERROR)
    {
      ret = er_errid ();
      if (ret == NO_ERROR)
	{
	  ret = ER_FAILED;
	}
    }

  return ret;
}

/*
 * btree_get_subtree_stats () -
 *   return: NO_ERROR
 *   btid(in):
 *   pg_ptr(in):
 *   env(in):
 */
static int
btree_get_subtree_stats (THREAD_ENTRY * thread_p, BTID_INT * btid,
			 PAGE_PTR page_ptr, BTREE_STATS_ENV * stats_env)
{
  char *header_ptr;
  int key_cnt, keys_cnt;
  int i, j;
  NON_LEAF_REC non_leaf_rec;
  VPID page_vpid;
  PAGE_PTR page = NULL;
  RECDES rec;
  DB_DOMAIN *key_type;
  int ret = NO_ERROR;

  key_type = btid->key_type;

  btree_get_header_ptr (page_ptr, &header_ptr);
  key_cnt = BTREE_GET_NODE_KEY_CNT (header_ptr);

  if (BTREE_GET_NODE_TYPE (header_ptr) == NON_LEAF_NODE)
    {
      if (key_cnt < 0)
	{
	  er_log_debug (ARG_FILE_LINE,
			"btree_get_subtree_stats: node key count"
			" underflow: %d", key_cnt);
	  goto exit_on_error;
	}

      /*
       * traverse all the subtrees of this non_leaf page and accumulate
       * the statistical data in the enviroment structure
       */
      keys_cnt = key_cnt + 1;
      for (i = 1; i <= keys_cnt; i++)
	{
	  if (spage_get_record (page_ptr, i, &rec, PEEK) != S_SUCCESS)
	    {
	      goto exit_on_error;
	    }

	  btree_read_fixed_portion_of_non_leaf_record (&rec, &non_leaf_rec);
	  page_vpid = non_leaf_rec.pnt;

	  page = pgbuf_fix (thread_p, &page_vpid, OLD_PAGE, PGBUF_LATCH_READ,
			    PGBUF_UNCONDITIONAL_LATCH);
	  if (page == NULL)
	    {
	      goto exit_on_error;
	    }

	  ret = btree_get_subtree_stats (thread_p, btid, page, stats_env);
	  if (ret != NO_ERROR)
	    {
	      goto exit_on_error;
	    }

	  pgbuf_unfix (thread_p, page);
	  page = NULL;
	}

      stats_env->stat_info->height++;
    }
  else
    {
      DB_VALUE key, elem;
      LEAF_REC leaf_rec;
      bool clear_key;
      int offset;
      int k;
      DB_MIDXKEY *midxkey;
      int prev_j_index, prev_k_index;
      char *prev_j_ptr, *prev_k_ptr;

      stats_env->stat_info->leafs++;
      stats_env->stat_info->keys += key_cnt;
      stats_env->stat_info->height = 1;	/* init */

      if (stats_env->get_pkeys)
	{
	  if (key_type->type->id != DB_TYPE_MIDXKEY)
	    {
	      /* single column index */
	      stats_env->stat_info->pkeys[0] += key_cnt;
	    }
	  else
	    {
	      for (i = 1; i <= key_cnt; i++)
		{
		  if (spage_get_record (page_ptr, i, &rec, PEEK) != S_SUCCESS)
		    {
		      goto exit_on_error;
		    }

		  /* read key-value */
		  btree_read_record (thread_p, btid, &rec, &key, &leaf_rec,
				     true, &clear_key, &offset, 0);

		  /* extract the sequence of the key-value */
		  midxkey = DB_GET_MIDXKEY (&key);

		  prev_j_index = 0;
		  prev_j_ptr = NULL;
		  for (j = 0; j < stats_env->stat_info->key_size; j++)
		    {
		      /* extract the element of the midxkey */
		      ret = set_midxkey_get_element_nocopy (midxkey, j, &elem,
							    &prev_j_index,
							    &prev_j_ptr);
		      if (ret != NO_ERROR)
			{
			  goto exit_on_error;
			}

		      if (tp_value_compare (&(stats_env->pkeys[j]), &elem,
					    0, 1) != DB_EQ)
			{
			  /* found different value */
			  stats_env->stat_info->pkeys[j] += 1;
			  pr_clear_value (&(stats_env->pkeys[j]));	/* clear saved */
			  pr_clone_value (&elem, &(stats_env->pkeys[j]));	/* save */

			  /* propagate to the following partial key-values */
			  prev_k_index = prev_j_index;
			  prev_k_ptr = prev_j_ptr;
			  for (k = j + 1; k < stats_env->stat_info->key_size;
			       k++)
			    {
			      ret = set_midxkey_get_element_nocopy (midxkey,
								    k,
								    &elem,
								    &prev_k_index,
								    &prev_k_ptr);
			      if (ret != NO_ERROR)
				{
				  goto exit_on_error;
				}

			      stats_env->stat_info->pkeys[k]++;
			      pr_clear_value (&(stats_env->pkeys[k]));
			      pr_clone_value (&elem, &(stats_env->pkeys[k]));	/* save */
			    }

			  /* go to the next key */
			  break;
			}
		    }

		  btree_clear_key_value (&clear_key, &key);
		}
	    }
	}
    }

  stats_env->stat_info->pages++;

  return ret;

exit_on_error:

  if (page)
    {
      pgbuf_unfix (thread_p, page);
      page = NULL;
    }

  if (ret == NO_ERROR)
    {
      ret = er_errid ();
      if (ret == NO_ERROR)
	{
	  ret = ER_FAILED;
	}
    }

  return ret;
}

/*
 * btree_get_stats () - Get Statistical Information about the B+tree index
 *   return: NO_ERROR
 *   btid(in): B+tree index identifier
 *   stat_info(in): Structure to store and return the statistical information
 *   get_pkeys(in): true to compute partial keys info, or false
 *
 * Note: Computes and returns statistical information about B+tree
 * which consist of the number of leaf pages, total number of
 * pages, number of keys and the height of the tree.
 */
int
btree_get_stats (THREAD_ENTRY * thread_p, BTID * btid,
		 BTREE_STATS * stat_info, bool get_partial_keys)
{
  VPID root_vpi;
  PAGE_PTR root = NULL;
  BTID_INT btid_int;
  RECDES rec;
  BTREE_ROOT_HEADER root_header;
  BTREE_STATS_ENV stat_env, *env = NULL;
  int i;
  int ret = NO_ERROR;

  stat_env.pkeys = NULL;

  root_vpi.pageid = btid->root_pageid;	/* read root page */
  root_vpi.volid = btid->vfid.volid;

  root = pgbuf_fix (thread_p, &root_vpi, OLD_PAGE, PGBUF_LATCH_READ,
		    PGBUF_UNCONDITIONAL_LATCH);
  if (root == NULL)
    {
      goto exit_on_error;
    }

  if (spage_get_record (root, HEADER, &rec, PEEK) != S_SUCCESS)
    {
      goto exit_on_error;
    }

  btree_read_root_header (&rec, &root_header);

  btid_int.sys_btid = btid;
  ret = btree_glean_root_header_info (&root_header, &btid_int);
  if (ret != NO_ERROR)
    {
      goto exit_on_error;
    }

  /* get number of OIDs, NULLs and unique keys from the root page root_header */
  stat_info->oids = root_header.num_oids;
  stat_info->nulls = root_header.num_nulls;
  stat_info->ukeys = root_header.num_keys;

  /* set environment variable */
  env = &stat_env;
  env->stat_info = stat_info;
  env->get_pkeys = get_partial_keys;
  if (env->get_pkeys)
    {
      env->pkeys = (DB_VALUE *) db_private_alloc (thread_p,
						  sizeof (DB_VALUE) *
						  env->stat_info->key_size);
      if (env->pkeys == NULL)
	{
	  goto exit_on_error;
	}
    }

  /* initialize environment stat_info structure */
  env->stat_info->leafs = 0;
  env->stat_info->pages = 0;
  env->stat_info->height = 0;
  env->stat_info->keys = 0;

  if (env->get_pkeys)
    {
      for (i = 0; i < env->stat_info->key_size; i++)
	{
	  env->stat_info->pkeys[i] = 0;
	  PRIM_INIT_NULL (&(env->pkeys[i]));
	}
    }

  /* traverse the tree and store the statistical data in the INFO structure */
  ret = btree_get_subtree_stats (thread_p, &btid_int, root, env);
  if (ret != NO_ERROR)
    {
      goto exit_on_error;
    }

end:

  if (root)
    {
      pgbuf_unfix (thread_p, root);
      root = NULL;		/* mark as unlocked */
    }

  /* clear partial key-values */
  if (env)
    {
      if (env->pkeys)
	{
	  for (i = 0; i < env->stat_info->key_size; i++)
	    {
	      pr_clear_value (&(env->pkeys[i]));
	    }
	  db_private_free_and_init (thread_p, env->pkeys);
	}
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
 * btree_check_page_key () - Check (verify) page
 *   return: either: DISK_INVALID, DISK_VALID, DISK_ERROR
 *   btid(in):
 *   page_ptr(in): Page pointer
 *   page_vpid(in): Page identifier
 *   clear_key(in):
 *   max_key_value(out): Set to the biggest key of the page in question.
 *
 * Note: Verifies the correctness of the specified page of the B+tree.
 * Tests include checking the order of the keys in the page,
 * checking the key count and maximum key length values stored page header.
 */
static DISK_ISVALID
btree_check_page_key (THREAD_ENTRY * thread_p, BTID_INT * btid,
		      PAGE_PTR page_ptr, VPID * page_vpid, bool * clear_key,
		      DB_VALUE * max_key_value)
{
  int key_cnt, key_cnt2, max_key, nrecs, offset;
  RECDES peek_rec1, peek_rec2;
  char *header_ptr;
  DB_VALUE key1, key2;
  bool leaf_page;
  TP_DOMAIN *key_domain;
  int k, overflow_key1 = 0, overflow_key2 = 0;
  bool clear_key1, clear_key2;
  LEAF_REC leaf_pnt;
  NON_LEAF_REC nleaf_pnt;
  DISK_ISVALID valid = DISK_ERROR;
  int c;

  btree_get_header_ptr (page_ptr, &header_ptr);
  key_cnt = BTREE_GET_NODE_KEY_CNT (header_ptr);
  max_key = BTREE_GET_NODE_MAX_KEY_LEN (header_ptr);
  leaf_page = (BTREE_GET_NODE_TYPE (header_ptr) == LEAF_NODE) ? true : false;

  nrecs = spage_number_of_records (page_ptr);

  key_domain = (leaf_page) ? btid->key_type : btid->nonleaf_key_type;

  /* initializations */
  db_value_domain_init (max_key_value, key_domain->type->id,
			key_domain->precision, key_domain->scale);

  /* computed key_cnt value */
  key_cnt2 = (leaf_page) ? (nrecs - 1) : (nrecs - 2);

  if (key_cnt != key_cnt2)
    {
      er_log_debug (ARG_FILE_LINE, "btree_check_page_key: "
		    "--- key count (%d) test failed for page {%d , %d}."
		    " Expected count %d",
		    key_cnt, page_vpid->volid, page_vpid->pageid, key_cnt2);
      btree_dump_page (thread_p, btid, page_ptr, page_vpid, 2, 2);
      valid = DISK_INVALID;
      goto error;
    }

  if ((!leaf_page && key_cnt == 0) || (leaf_page && key_cnt == 1))
    {
      /* there is only one key, so no order check */
      if (spage_get_record (page_ptr, 1, &peek_rec1, PEEK) != S_SUCCESS)
	{
	  valid = DISK_ERROR;
	  goto error;
	}
      btree_read_record (thread_p, btid, &peek_rec1, max_key_value,
			 (leaf_page ? (void *) &leaf_pnt : (void *)
			  &nleaf_pnt), leaf_page, clear_key, &offset, 1);
      return DISK_VALID;
    }

  for (k = 1; k < key_cnt; k++)
    {
      if (spage_get_record (page_ptr, k, &peek_rec1, PEEK) != S_SUCCESS)
	{
	  valid = DISK_ERROR;
	  goto error;
	}

      /* read the current record key */
      btree_read_record (thread_p, btid, &peek_rec1, &key1,
			 (leaf_page ? (void *) &leaf_pnt : (void *)
			  &nleaf_pnt), leaf_page, &clear_key1, &offset, 0);

      overflow_key1 =
	(leaf_page) ? (leaf_pnt.key_len < 0) : (nleaf_pnt.key_len < 0);

      if ((!(overflow_key1) && btree_get_key_length (&key1) > max_key)
	  || (overflow_key1 && DISK_VPID_SIZE > max_key))
	{
	  er_log_debug (ARG_FILE_LINE, "btree_check_page_key: "
			"--- max key length test failed for page "
			"{%d , %d}. Check key_rec = %d\n",
			page_vpid->volid, page_vpid->pageid, k);
	  btree_dump_page (thread_p, btid, page_ptr, page_vpid, 2, 2);
	  valid = DISK_INVALID;
	  goto error;
	}

      if (spage_get_record (page_ptr, k + 1, &peek_rec2, PEEK) != S_SUCCESS)
	{
	  valid = DISK_ERROR;
	  goto error;
	}

      /* read the next record key */
      btree_read_record (thread_p, btid, &peek_rec2, &key2,
			 (leaf_page ? (void *) &leaf_pnt : (void *)
			  &nleaf_pnt), leaf_page, &clear_key2, &offset, 0);

      overflow_key2 =
	(leaf_page) ? (leaf_pnt.key_len < 0) : (nleaf_pnt.key_len < 0);

      if ((!(overflow_key2) && btree_get_key_length (&key2) > max_key)
	  || (overflow_key2 && DISK_VPID_SIZE > max_key))
	{
	  er_log_debug (ARG_FILE_LINE, "btree_check_page_key: "
			"--- max key length test failed for page "
			"{%d , %d}. Check key_rec = %d\n",
			page_vpid->volid, page_vpid->pageid, k + 1);
	  btree_dump_page (thread_p, btid, page_ptr, page_vpid, 2, 2);
	  valid = DISK_INVALID;
	  goto error;
	}

      /* compare the keys for the order */
      c = (*(key_domain->type->cmpval))
	(&key1, &key2, btid->key_type, btid->reverse, 0, 1, NULL);

      if (c >= 0)
	{
	  er_log_debug (ARG_FILE_LINE, "btree_check_page_key:"
			"--- key order test failed for page"
			" {%d , %d}. Check key_recs = %d and %d\n",
			page_vpid->volid, page_vpid->pageid, k, k + 1);
	  btree_dump_page (thread_p, btid, page_ptr, page_vpid, 2, 2);
	  valid = DISK_INVALID;
	  goto error;
	}

      if (k == (key_cnt - 1))
	{
	  (void) pr_clone_value (&key2, max_key_value);	/* last key is maximum key */
	  *clear_key = true;
	}

      btree_clear_key_value (&clear_key1, &key1);
      btree_clear_key_value (&clear_key2, &key2);
    }

  /* page check passed */
  return DISK_VALID;

error:

  btree_clear_key_value (&clear_key1, &key1);
  btree_clear_key_value (&clear_key2, &key2);

  return valid;
}

/*
 * btree_verify_subtree () - Check (verify) a page and its subtrees
 *   return: either: DISK_INVALID, DISK_VALID, DISK_ERROR
 *   btid(in): B+tree index identifier
 *   pg_ptr(in): Page pointer for the subtree root page
 *   pg_vpid(in): Page identifier for the subtree root page
 *   INFO(in):
 *
 * Note: Verifies the correctness of the content of the given page
 * together with its subtree
 */
static DISK_ISVALID
btree_verify_subtree (THREAD_ENTRY * thread_p, BTID_INT * btid,
		      PAGE_PTR pg_ptr, VPID * pg_vpid, BTREE_NODE_INFO * INFO)
{
  char *header_ptr;
  INT16 key_cnt;
  int keys_cnt;
  NON_LEAF_REC NLeaf_Ptr;
  VPID page_vpid;
  PAGE_PTR page = NULL;
  RECDES Rec;
  DB_VALUE maxkey, curr_key;
  int offset;
  bool clear_key = false, m_clear_key = false;
  int i;
  DISK_ISVALID valid = DISK_ERROR;
  BTREE_NODE_INFO INFO2;

  db_make_null (&INFO2.max_key);

  /* test the page for the order of the keys within the page and get the
   * biggest key of this page
   */
  valid =
    btree_check_page_key (thread_p, btid, pg_ptr, pg_vpid, &m_clear_key,
			  &maxkey);
  if (valid != DISK_VALID)
    {
      goto error;
    }

  btree_get_header_ptr (pg_ptr, &header_ptr);
  key_cnt = BTREE_GET_NODE_KEY_CNT (header_ptr);

  /* initialize INFO structure */
  INFO->max_key_len = BTREE_GET_NODE_MAX_KEY_LEN (header_ptr);
  INFO->height = 0;
  INFO->tot_key_cnt = 0;
  INFO->page_cnt = 0;
  INFO->leafpg_cnt = 0;
  INFO->nleafpg_cnt = 0;
  db_make_null (&INFO->max_key);

  if (BTREE_GET_NODE_TYPE (header_ptr) == NON_LEAF_NODE)
    {				/* a non-leaf page */
      btree_clear_key_value (&m_clear_key, &maxkey);

      if (key_cnt < 0)
	{
	  er_log_debug (ARG_FILE_LINE, "btree_verify_subtree: "
			"node key count underflow: %d\n", key_cnt);
	  btree_dump_page (thread_p, btid, pg_ptr, pg_vpid, 2, 2);
	  valid = DISK_INVALID;
	  goto error;
	}

      INFO2.key_area_len = 0;
      db_make_null (&INFO2.max_key);

      /* traverse all the subtrees of this non_leaf page and accumulate
       * the statistical data in the INFO structure
       */
      keys_cnt = key_cnt + 1;	/* this is because the header key count is always
				 * one less than the actual number for some
				 * unknown reason
				 */
      for (i = 1; i <= keys_cnt; i++)
	{

	  if (spage_get_record (pg_ptr, i, &Rec, PEEK) != S_SUCCESS)
	    {
	      valid = DISK_ERROR;
	      goto error;
	    }

	  btree_read_record (thread_p, btid, &Rec, &curr_key, &NLeaf_Ptr,
			     false, &clear_key, &offset, 0);

	  page_vpid = NLeaf_Ptr.pnt;

	  page = pgbuf_fix (thread_p, &page_vpid, OLD_PAGE, PGBUF_LATCH_READ,
			    PGBUF_UNCONDITIONAL_LATCH);
	  if (page == NULL)
	    {
	      valid = DISK_ERROR;
	      goto error;
	    }

	  valid =
	    btree_verify_subtree (thread_p, btid, page, &page_vpid, &INFO2);
	  if (valid != DISK_VALID)
	    {
	      goto error;
	    }

	  /* accumulate results */
	  INFO->height = INFO2.height + 1;
	  INFO->tot_key_cnt += INFO2.tot_key_cnt;
	  INFO->page_cnt += INFO2.page_cnt;
	  INFO->leafpg_cnt += INFO2.leafpg_cnt;
	  INFO->nleafpg_cnt += INFO2.nleafpg_cnt;

	  if (i <= (keys_cnt - 1))
	    {
	      if ((*(btid->key_type->type->cmpval))
		  (&INFO2.max_key, &curr_key,
		   btid->key_type, btid->reverse, 0, 1, NULL) > 0)
		{
		  er_log_debug (ARG_FILE_LINE, "btree_verify_subtree: "
				"key order test among nodes failed...\n");
		  btree_dump_page (thread_p, btid, pg_ptr, pg_vpid, 2, 2);
		  valid = DISK_INVALID;
		  goto error;
		}
	    }
	  else
	    {			/* maximum key is the maximum key of the last subtree */
	      pr_clone_value (&INFO2.max_key, &INFO->max_key);
	    }

	  pgbuf_unfix (thread_p, page);
	  page = NULL;
	  pr_clear_value (&INFO2.max_key);
	  btree_clear_key_value (&clear_key, &curr_key);
	}
      INFO->page_cnt += 1;
      INFO->nleafpg_cnt += 1;

    }
  else
    {				/* a leaf page */
      if (!db_value_is_null (&maxkey))
	{
	  /* form the INFO structure from the header information */
	  INFO->height = 1;
	  INFO->tot_key_cnt = key_cnt;
	  INFO->page_cnt = 1;
	  INFO->leafpg_cnt = 1;
	  INFO->nleafpg_cnt = 0;

	  pr_clone_value (&maxkey, &INFO->max_key);
	  btree_clear_key_value (&m_clear_key, &maxkey);
	}
    }

  return DISK_VALID;

error:

  btree_clear_key_value (&m_clear_key, &maxkey);
  btree_clear_key_value (&clear_key, &curr_key);

  if (page)
    {
      pgbuf_unfix (thread_p, page);
      page = NULL;
    }
  pr_clear_value (&INFO2.max_key);
  return valid;

}

/*
 * btree_verify_tree () - Check (verify) tree
 *   return: either: DISK_INVALID, DISK_VALID, DISK_ERROR
 *   btid_int(in): B+tree index identifier
 *
 * Note: Verifies the correctness of the B+tree index . During tree
 * traversal,  several tests are  conducted, such as checking
 * the order of keys on a page or among pages that are in a
 * father-child relationship.
 */
DISK_ISVALID
btree_verify_tree (THREAD_ENTRY * thread_p, BTID_INT * btid_int)
{
  VPID p_vpid;
  PAGE_PTR Root = NULL;
  BTREE_NODE_INFO INFO;
  DISK_ISVALID valid = DISK_ERROR;

  db_make_null (&INFO.max_key);

  p_vpid.pageid = btid_int->sys_btid->root_pageid;	/* read root page */
  p_vpid.volid = btid_int->sys_btid->vfid.volid;
  Root = pgbuf_fix (thread_p, &p_vpid, OLD_PAGE, PGBUF_LATCH_READ,
		    PGBUF_UNCONDITIONAL_LATCH);
  if (Root == NULL)
    {
      valid = DISK_ERROR;
      goto error;
    }

  db_make_null (&INFO.max_key);

  /* traverse the tree and store the statistical data in the INFO structure */
  valid = btree_verify_subtree (thread_p, btid_int, Root, &p_vpid, &INFO);
  if (valid != DISK_VALID)
    {
      goto error;
    }

  pr_clear_value (&INFO.max_key);
  pgbuf_unfix (thread_p, Root);
  Root = NULL;

  return DISK_VALID;

error:

  if (Root)
    {
      pgbuf_unfix (thread_p, Root);
      Root = NULL;
    }
  pr_clear_value (&INFO.max_key);
  return valid;

}

/*
 *       		 db_check consistency routines
 */

/*
 * btree_check_pages () -
 *   return: DISK_VALID, DISK_VALID or DISK_ERROR
 *   btid(in): B+tree index identifier
 *   pg_ptr(in): Page pointer
 *   pg_vpid(in): Page identifier
 *
 * Note: Verify that given page and all its subpages are valid.
 */
static DISK_ISVALID
btree_check_pages (THREAD_ENTRY * thread_p, BTID_INT * btid, PAGE_PTR pg_ptr,
		   VPID * pg_vpid)
{
  VPID page_vpid;		/* Child page identifier */
  PAGE_PTR page = NULL;		/* Child page pointer */
  RECDES Rec;			/* Record descriptor for page node records */
  DISK_ISVALID vld = DISK_ERROR;	/* Validity return code from subtree */
  int key_cnt;			/* Number of keys in the page */
  int i;			/* Loop counter */
  char *header_ptr;
  NON_LEAF_REC nleaf;

  /* Verify the given page */
  vld = file_isvalid_page_partof (thread_p, pg_vpid, &btid->sys_btid->vfid);
  if (vld != DISK_VALID)
    {
      goto error;
    }

  btree_get_header_ptr (pg_ptr, &header_ptr);

  /* Verify subtree child pages */
  if (BTREE_GET_NODE_TYPE (header_ptr) == NON_LEAF_NODE)
    {				/* non-leaf page */
      key_cnt = BTREE_GET_NODE_KEY_CNT (header_ptr);
      for (i = 1; i <= (key_cnt + 1); i++)
	{
	  if (spage_get_record (pg_ptr, i, &Rec, PEEK) != S_SUCCESS)
	    {
	      vld = DISK_ERROR;
	      goto error;
	    }
	  btree_read_fixed_portion_of_non_leaf_record (&Rec, &nleaf);
	  page_vpid = nleaf.pnt;

	  page = pgbuf_fix (thread_p, &page_vpid, OLD_PAGE, PGBUF_LATCH_READ,
			    PGBUF_UNCONDITIONAL_LATCH);
	  if (page == NULL)
	    {
	      vld = DISK_ERROR;
	      goto error;
	    }

	  vld = btree_check_pages (thread_p, btid, page, &page_vpid);
	  if (vld != DISK_VALID)
	    {
	      goto error;
	    }
	  pgbuf_unfix (thread_p, page);
	  page = NULL;
	}			/* for */
    }				/* if */

  return DISK_VALID;

error:

  if (page)
    {
      pgbuf_unfix (thread_p, page);
      page = NULL;
    }
  return vld;

}

/*
 * btree_check_tree () -
 *   return: DISK_VALID, DISK_INVALID or DISK_ERROR
 *   btid(in): B+tree index identifier
 *
 * Note: Verify that all the pages of the specified index are valid.
 */
DISK_ISVALID
btree_check_tree (THREAD_ENTRY * thread_p, BTID * btid)
{
  DISK_ISVALID valid = DISK_ERROR;
  VPID r_vpid;			/* Root page identifier */
  PAGE_PTR r_pgptr = NULL;	/* Root page pointer */
  BTID_INT btid_int;
  RECDES Rec;
  BTREE_ROOT_HEADER root_header;

  /* Fetch the root page */
  r_vpid.pageid = btid->root_pageid;
  r_vpid.volid = btid->vfid.volid;
  r_pgptr = pgbuf_fix (thread_p, &r_vpid, OLD_PAGE, PGBUF_LATCH_READ,
		       PGBUF_UNCONDITIONAL_LATCH);
  if (r_pgptr == NULL
      || (spage_get_record (r_pgptr, HEADER, &Rec, PEEK) != S_SUCCESS))
    {
      valid = DISK_ERROR;
      goto error;
    }

  btree_read_root_header (&Rec, &root_header);

  btid_int.sys_btid = btid;
  if (btree_glean_root_header_info (&root_header, &btid_int) != NO_ERROR)
    {
      goto error;
    }

  valid = btree_check_pages (thread_p, &btid_int, r_pgptr, &r_vpid);
  if (valid != DISK_VALID)
    {
      goto error;
    }

  pgbuf_unfix (thread_p, r_pgptr);
  r_pgptr = NULL;

  /* Now check for the logical correctness of the tree */
  return btree_verify_tree (thread_p, &btid_int);

error:

  if (r_pgptr)
    {
      pgbuf_unfix (thread_p, r_pgptr);
      r_pgptr = NULL;
    }
  return valid;

}

/*
 * btree_check_all () -
 *   return: either: DISK_INVALID, DISK_VALID, DISK_ERROR
 *
 * Note: Verify that all pages of all btree indices are valid.
 */
DISK_ISVALID
btree_check_all (THREAD_ENTRY * thread_p)
{
  int num_files;		/* Number of files in the system */
  BTID btid;			/* Btree index identifier        */
  VPID vpid;			/* Index root page identifier    */
  DISK_ISVALID valid, allvalid;	/* Validation return code        */
  FILE_TYPE file_type;		/* TYpe of file                  */
  int i;			/* Loop counter                  */

  /* Find number of files */
  num_files = file_get_numfiles (thread_p);
  if (num_files < 0)
    {
      return DISK_ERROR;
    }

  allvalid = DISK_VALID;

  /* Go to each file, check only the btree files */
  for (i = 0; i < num_files && allvalid != DISK_ERROR; i++)
    {
      if (file_find_nthfile (thread_p, &btid.vfid, i) != 1)
	{
	  break;
	}

      file_type = file_get_type (thread_p, &btid.vfid);
      if (file_type == FILE_UNKNOWN_TYPE)
	{
	  allvalid = DISK_ERROR;
	  break;
	}

      if (file_type != FILE_BTREE)
	{
	  continue;
	}

      if (file_find_nthpages (thread_p, &btid.vfid, &vpid, 0, 1) != 1)
	{
	  return DISK_ERROR;
	}

      btid.root_pageid = vpid.pageid;

      valid = btree_check_tree (thread_p, &btid);
      if (valid != DISK_VALID)
	{
	  allvalid = valid;
	}
    }

  return allvalid;

}

/*
 * btree_keyoid_checkscan_start () -
 *   return: NO_ERROR
 *   btid(in): B+tree index identifier
 *   btscan(out): Set to key-oid check scan structure.
 *
 * Note: Start a <key-oid> check scan on the index.
 */
int
btree_keyoid_checkscan_start (BTID * btid, BTREE_CHECKSCAN * btscan)
{
  /* initialize scan structure */
  btscan->btid.vfid.volid = btid->vfid.volid;
  btscan->btid.vfid.fileid = btid->vfid.fileid;
  btscan->btid.root_pageid = btid->root_pageid;
  BTREE_INIT_SCAN (&btscan->btree_scan);
  btscan->oid_area_size = DB_PAGESIZE * PRM_BT_OID_NBUFFERS;
  btscan->oid_cnt = 0;
  btscan->oid_ptr = (OID *) malloc (btscan->oid_area_size);
  if (btscan->oid_ptr == NULL)
    {
      return ER_FAILED;
    }

  return NO_ERROR;
}

/*
 * btree_keyoid_checkscan_check () -
 *   return: either: DISK_INVALID, DISK_VALID, DISK_ERROR
 *   btscan(in): B+tree key-oid check scan structure.
 *   cls_oid(in):
 *   key(in): Key pointer
 *   oid(in): Object identifier for the key
 *
 * Note: Check if the given key-oid pair exists in the index.
 */
DISK_ISVALID
btree_keyoid_checkscan_check (THREAD_ENTRY * thread_p,
			      BTREE_CHECKSCAN * btscan, OID * cls_oid,
			      DB_VALUE * key, OID * oid)
{
  int k;			/* Loop iteration variable */
  INDX_SCAN_ID isid;
  DISK_ISVALID status;

  /* initialize scan structure */
  BTREE_INIT_SCAN (&btscan->btree_scan);

  isid.oid_list.oid_cnt = 0;
  isid.oid_list.oidp = btscan->oid_ptr;
  /* do not use copy_buf for key-val scan, only use for key-range scan */
  isid.copy_buf = NULL;
  isid.copy_buf_len = 0;

  do
    {
      /* search index */
      btscan->oid_cnt = btree_keyval_search (thread_p, &btscan->btid, true,
					     &btscan->btree_scan,
					     key, cls_oid, btscan->oid_ptr,
					     btscan->oid_area_size, NULL,
					     &isid, false);
      if (btscan->oid_cnt == -1)
	{
	  btscan->oid_ptr = isid.oid_list.oidp;
	  status = DISK_ERROR;
	  goto end;
	}

      btscan->oid_ptr = isid.oid_list.oidp;

      /* search current set of OIDs to see if given <key-oid> pair exists */
      for (k = 0; k < btscan->oid_cnt; k++)
	{
	  if (OID_EQ (&btscan->oid_ptr[k], oid))
	    {			/* <key-oid> pair found */
	      status = DISK_VALID;
	      goto end;
	    }
	}
    }
  while (!BTREE_END_OF_SCAN (&btscan->btree_scan));

  /* indicate <key_oid> pair is not found */
  status = DISK_INVALID;

end:

  /* clear all the used keys */
  btree_scan_clear_key (&btscan->btree_scan);

  /* do not use copy_buf for key-val scan, only use for key-range scan */

  return status;
}

/*
 * btree_keyoid_checkscan_end () -
 *   return:
 *   btscan(in): B+tree key-oid check scan structure.
 *
 * Note: End the <key-oid> check scan on the index.
 */
void
btree_keyoid_checkscan_end (BTREE_CHECKSCAN * btscan)
{
  /* Deallocate allocated areas */
  if (btscan->oid_ptr)
    {
      free_and_init (btscan->oid_ptr);
      btscan->oid_ptr = NULL;
      btscan->oid_area_size = 0;
    }
}

/*
 *       		     b+tree space routines
 */

/*
 * btree_estimate_total_numpages () -
 *   return: int
 *   dis_key_cnt(in): Distinct number of key values
 *   avg_key_len(in): Average key length
 *   domain(in):
 *   tot_val_cnt(in): Total value count
 *   blt_pgcnt_est(out): Set to index built(not-loaded) total page cnt estimate
 *   blt_wrs_pgcnt_est(out): Set to index built(not-loaded) worst case pgcnt
 *                           estimate
 *
 * Note: Estimate and return total number of pages for the B+tree to
 * be constructed.
 */
int
btree_estimate_total_numpages (THREAD_ENTRY * thread_p, int dis_key_cnt,
			       int avg_key_len, TP_DOMAIN * domain,
			       int tot_val_cnt, int *blt_pgcnt_est,
			       int *blt_wrs_pgcnt_est)
{
  int load_pgcnt_est;
  int rec_oid_cnt;
  int avg_rec_len;
  int avg_nrec_len;
  int page_size;
  int ovfl_page_size;
  int nrecs_leaf_page;
  int nrecs_nleaf_page;
  int num_leaf_pages;
  int num_ovfl_pages;
  int num_nleaf_pages;
  int order;
  int nlevel_cnt;
  int nlevel_pg_cnt;
  int num_pages;
  int k, s;

  /* initializations */
  load_pgcnt_est = -1;
  *blt_pgcnt_est = -1;
  *blt_wrs_pgcnt_est = -1;

  /* check for passed parameters */
  if (dis_key_cnt == 0)
    {
      dis_key_cnt++;
    }
  if (tot_val_cnt < dis_key_cnt)
    {
      tot_val_cnt = dis_key_cnt;
    }

  /* find average leaf record length */
  /* LEAF RECORD: Key-Length : Ovfl_vpid :    key   :  oid1 : oid2 ... */
  rec_oid_cnt = CEIL_PTVDIV (tot_val_cnt, dis_key_cnt);
  rec_oid_cnt = MAX (1, rec_oid_cnt);
  avg_rec_len = LEAF_RECORD_SIZE;
  DB_ALIGN (avg_rec_len, OR_INT_SIZE);
  avg_rec_len += pr_estimate_size (domain, avg_key_len);
  DB_ALIGN (avg_rec_len, OR_INT_SIZE);
  avg_rec_len += (rec_oid_cnt * OIDSIZE);

  /* find average non-leaf record length */
  /* NLEAF RECORD: Child_vpid : key_len : key */
  avg_nrec_len = NON_LEAF_RECORD_SIZE;
  DB_ALIGN (avg_nrec_len, OR_INT_SIZE);
  avg_nrec_len += pr_estimate_size (domain, avg_key_len);

  /* find actually available page size for index records:
   * The index pages are usually 80% full and each one contains
   * a node header (NODE_HEADER_SIZE).
   *
   * Reserved space: page-header-overhead + header record +
   *                 one record size (the one not to be inserted) +
   *                 free area reserved in the page
   */

  /* Do the estimations for three cases.
   * Regular index loading, use index unfill factor,
   * Regular index built (one at a time), assume 30% free in pages,
   * Worst case index built, assume 50% free space in pages.
   */
  for (s = 0; s < 3; s++)
    {
      page_size = DB_PAGESIZE - (spage_header_size () +
				 (NODE_HEADER_SIZE + spage_slot_size ()) +
				 (DB_PAGESIZE * (((s == 0) ?
						  PRM_BT_UNFILL_FACTOR :
						  ((s == 1) ? 0.30 : 0.50))
						 + 0.05)));

      /* find the number of records per index page */
      if (avg_rec_len >= page_size)
	{
	  /* records will use overflow pages, so each leaf page will get
	   * one record, plus number overflow pages
	   */
	  nrecs_leaf_page = 1;
	  ovfl_page_size = DB_PAGESIZE - (spage_header_size () +
					  (DISK_VPID_SIZE +
					   spage_slot_size ()) +
					  spage_slot_size ());
	  num_ovfl_pages =
	    dis_key_cnt *
	    (CEIL_PTVDIV (avg_rec_len - page_size, ovfl_page_size));
	}
      else
	{
	  /* consider the last record size not to be put in page */
	  page_size -= (avg_rec_len + spage_slot_size ());
	  nrecs_leaf_page = page_size / (avg_rec_len + spage_slot_size ());
	  nrecs_leaf_page = MAX (1, nrecs_leaf_page);
	  num_ovfl_pages = 0;
	}
      nrecs_nleaf_page = page_size / (avg_nrec_len + spage_slot_size ());
      nrecs_nleaf_page = MAX (2, nrecs_nleaf_page);

      /* find the number of leaf pages */
      num_leaf_pages = CEIL_PTVDIV (dis_key_cnt, nrecs_leaf_page);
      num_leaf_pages = MAX (1, num_leaf_pages);

      /* find the number of nleaf pages */
      num_nleaf_pages = 1;
      order = 1;
      do
	{
	  nlevel_cnt = 1;
	  for (k = 0; k < order; k++)
	    {
	      nlevel_cnt *= ((int) nrecs_nleaf_page);
	    }
	  nlevel_pg_cnt = (num_leaf_pages / nlevel_cnt);
	  num_nleaf_pages += nlevel_pg_cnt;
	  order++;
	}
      while (nlevel_pg_cnt > 1);

      /* find total number of index tree pages, one page is added for the
       * file manager overhead.
       */
      num_pages = num_leaf_pages + num_ovfl_pages + num_nleaf_pages;
      num_pages += file_guess_numpages_overhead (thread_p, NULL, num_pages);

      /* record corresponding estimation */
      if (s == 0)
	{
	  load_pgcnt_est = num_pages;
	}
      else if (s == 1)
	{
	  *blt_pgcnt_est = num_pages;
	}
      else
	{
	  *blt_wrs_pgcnt_est = num_pages;
	}

    }				/* for */

  /* make sure that built tree estimations are not lower than loaded
   * tree estimations.
   */
  if (*blt_pgcnt_est < load_pgcnt_est)
    {
      *blt_pgcnt_est = load_pgcnt_est;
    }
  if (*blt_wrs_pgcnt_est < *blt_pgcnt_est)
    {
      *blt_wrs_pgcnt_est = *blt_pgcnt_est;
    }

  return load_pgcnt_est;
}

/*
 * btree_get_subtree_capacity () -
 *   return: NO_ERROR
 *   btid(in):
 *   pg_ptr(in):
 *   cpc(in):
 */
static int
btree_get_subtree_capacity (THREAD_ENTRY * thread_p, BTID_INT * btid,
			    PAGE_PTR pg_ptr, BTREE_CAPACITY * cpc)
{
  RECDES Rec;			/* Page record descriptor */
  char *header_ptr;
  int key_cnt;			/* Page key count */
  NON_LEAF_REC NLeaf_Ptr;	/* NonLeaf Record pointer */
  VPID page_vpid;		/* Child page identifier */
  PAGE_PTR page = NULL;		/* Child page pointer */
  int i;			/* Loop counter */
  LEAF_REC leaf_ptr;
  bool clear_key = false;
  PAGE_PTR ovfp = NULL;
  DB_VALUE key1;
  int oid_size;
  int ret = NO_ERROR;

  if (BTREE_IS_UNIQUE (btid))
    {
      oid_size = 2 * OR_OID_SIZE;
    }
  else
    {
      oid_size = OR_OID_SIZE;
    }

  /* initialize capacity structure */
  cpc->dis_key_cnt = 0;
  cpc->tot_val_cnt = 0;
  cpc->avg_val_per_key = 0;
  cpc->leaf_pg_cnt = 0;
  cpc->nleaf_pg_cnt = 0;
  cpc->tot_pg_cnt = 0;
  cpc->height = 0;
  cpc->sum_rec_len = 0;
  cpc->sum_key_len = 0;
  cpc->avg_key_len = 0;
  cpc->avg_rec_len = 0;
  cpc->tot_free_space = 0;
  cpc->tot_space = 0;
  cpc->tot_used_space = 0;
  cpc->avg_pg_key_cnt = 0;
  cpc->avg_pg_free_sp = 0;

  btree_get_header_ptr (pg_ptr, &header_ptr);
  key_cnt = BTREE_GET_NODE_KEY_CNT (header_ptr);

  if (BTREE_GET_NODE_TYPE (header_ptr) == NON_LEAF_NODE)
    {				/* a non-leaf page */
      BTREE_CAPACITY cpc2;

      /* traverse all the subtrees of this non_leaf page and accumulate
       * the statistical data in the cpc structure
       */
      for (i = 1; i <= (key_cnt + 1); i++)
	{
	  if (spage_get_record (pg_ptr, i, &Rec, PEEK) != S_SUCCESS)
	    {
	      goto exit_on_error;
	    }
	  btree_read_fixed_portion_of_non_leaf_record (&Rec, &NLeaf_Ptr);;
	  page_vpid = NLeaf_Ptr.pnt;
	  page = pgbuf_fix (thread_p, &page_vpid, OLD_PAGE, PGBUF_LATCH_READ,
			    PGBUF_UNCONDITIONAL_LATCH);
	  if (page == NULL)
	    {
	      goto exit_on_error;
	    }

	  ret = btree_get_subtree_capacity (thread_p, btid, page, &cpc2);
	  if (ret != NO_ERROR)
	    {
	      goto exit_on_error;
	    }

	  /* form the cpc structure for a non-leaf node page */
	  cpc->dis_key_cnt += cpc2.dis_key_cnt;
	  cpc->tot_val_cnt += cpc2.tot_val_cnt;
	  cpc->leaf_pg_cnt += cpc2.leaf_pg_cnt;
	  cpc->nleaf_pg_cnt += cpc2.nleaf_pg_cnt;
	  cpc->tot_pg_cnt += cpc2.tot_pg_cnt;
	  cpc->height = cpc2.height + 1;
	  cpc->sum_rec_len += cpc2.sum_rec_len;
	  cpc->sum_key_len += cpc2.sum_key_len;
	  cpc->tot_free_space += cpc2.tot_free_space;
	  cpc->tot_space += cpc2.tot_space;
	  cpc->tot_used_space += cpc2.tot_used_space;
	  pgbuf_unfix (thread_p, page);
	  page = NULL;
	}			/* for */
      cpc->avg_val_per_key = (cpc->dis_key_cnt > 0) ?
	(cpc->tot_val_cnt / cpc->dis_key_cnt) : 0;
      cpc->nleaf_pg_cnt += 1;
      cpc->tot_pg_cnt += 1;
      cpc->tot_free_space += spage_get_free_space (thread_p, pg_ptr);
      cpc->tot_space += DB_PAGESIZE;
      cpc->tot_used_space +=
	(DB_PAGESIZE - spage_get_free_space (thread_p, pg_ptr));
      cpc->avg_key_len =
	(cpc->dis_key_cnt > 0) ? (cpc->sum_key_len / cpc->dis_key_cnt) : 0;
      cpc->avg_rec_len =
	(cpc->dis_key_cnt > 0) ? (cpc->sum_rec_len / cpc->dis_key_cnt) : 0;
      cpc->avg_pg_key_cnt =
	(cpc->leaf_pg_cnt > 0) ? (cpc->dis_key_cnt / cpc->leaf_pg_cnt) : 0;
      cpc->avg_pg_free_sp =
	(cpc->tot_pg_cnt > 0) ? (cpc->tot_free_space / cpc->tot_pg_cnt) : 0;
    }
  else
    {				/* a leaf page */

      /* form the cpc structure for a leaf node page */
      cpc->dis_key_cnt = key_cnt;
      cpc->leaf_pg_cnt = 1;
      cpc->nleaf_pg_cnt = 0;
      cpc->tot_pg_cnt = 1;
      cpc->height = 1;
      for (i = 1; i <= cpc->dis_key_cnt; i++)
	{
	  int offset;		/* Offset to the beginning of OID list */
	  int oid_cnt;		/* Number of OIDs */
	  VPID ovfl_vpid;	/* Overflow page identifier */
	  RECDES oRec;		/* Overflow record descriptor */
	  LEAF_REC leaf_pnt;

	  if (spage_get_record (pg_ptr, i, &Rec, PEEK) != S_SUCCESS)
	    {
	      goto exit_on_error;
	    }
	  cpc->sum_rec_len += Rec.length;

	  /* read the current record key */
	  btree_read_record (thread_p, btid, &Rec, &key1, &leaf_pnt,
			     true, &clear_key, &offset, 0);
	  cpc->sum_key_len += btree_get_key_length (&key1);
	  btree_clear_key_value (&clear_key, &key1);

	  /* find the value (OID) count for the record */
	  btree_read_fixed_portion_of_leaf_record (&Rec, &leaf_ptr);
	  oid_cnt = CEIL_PTVDIV (Rec.length - offset, oid_size);
	  ovfl_vpid = leaf_ptr.ovfl;
	  if (!VPID_ISNULL (&ovfl_vpid))
	    {			/* overflow pages exist */
	      do
		{
		  ovfp = pgbuf_fix (thread_p, &ovfl_vpid, OLD_PAGE,
				    PGBUF_LATCH_READ,
				    PGBUF_UNCONDITIONAL_LATCH);
		  if (ovfp == NULL)
		    {
		      goto exit_on_error;
		    }

		  btree_get_header_ptr (ovfp, &header_ptr);
		  btree_get_next_overflow_vpid (header_ptr, &ovfl_vpid);

		  if (spage_get_record (ovfp, 1, &oRec, PEEK) != S_SUCCESS)
		    {
		      goto exit_on_error;
		    }
		  oid_cnt += CEIL_PTVDIV (oRec.length, oid_size);
		  pgbuf_unfix (thread_p, ovfp);
		  ovfp = NULL;
		}
	      while (!VPID_ISNULL (&ovfl_vpid));
	    }			/* if */
	  cpc->tot_val_cnt += oid_cnt;

	}			/* for */
      cpc->avg_val_per_key = (cpc->dis_key_cnt > 0) ?
	(cpc->tot_val_cnt / cpc->dis_key_cnt) : 0;
      cpc->avg_key_len = (cpc->dis_key_cnt > 0) ?
	(cpc->sum_key_len / cpc->dis_key_cnt) : 0;
      cpc->avg_rec_len = (cpc->dis_key_cnt > 0) ?
	(cpc->sum_rec_len / cpc->dis_key_cnt) : 0;
      cpc->tot_free_space = spage_get_free_space (thread_p, pg_ptr);
      cpc->tot_space = DB_PAGESIZE;
      cpc->tot_used_space = (cpc->tot_space - cpc->tot_free_space);
      cpc->avg_pg_key_cnt = (cpc->leaf_pg_cnt > 0) ?
	(cpc->dis_key_cnt / cpc->leaf_pg_cnt) : 0;
      cpc->avg_pg_free_sp = (cpc->tot_pg_cnt > 0) ?
	(cpc->tot_free_space / cpc->tot_pg_cnt) : 0;

    }				/* if-else */

end:

  return ret;

exit_on_error:

  if (page)
    {
      pgbuf_unfix (thread_p, page);
      page = NULL;
    }
  if (ovfp)
    {
      pgbuf_unfix (thread_p, ovfp);
      ovfp = NULL;
    }

  btree_clear_key_value (&clear_key, &key1);

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
 * btree_index_capacity () -
 *   return: NO_ERROR
 *   btid(in): B+tree index identifier
 *   cpc(out): Set to contain index capacity information
 *
 * Note: Form and return index capacity/space related information
 */
int
btree_index_capacity (THREAD_ENTRY * thread_p, BTID * btid,
		      BTREE_CAPACITY * cpc)
{
  VPID Root_vpid;		/* Root page identifier */
  PAGE_PTR Root = NULL;		/* Root page pointer */
  BTID_INT btid_int;
  RECDES Rec;
  BTREE_ROOT_HEADER root_header;
  int ret = NO_ERROR;

  /* read root page */
  Root_vpid.pageid = btid->root_pageid;
  Root_vpid.volid = btid->vfid.volid;
  Root = pgbuf_fix (thread_p, &Root_vpid, OLD_PAGE, PGBUF_LATCH_READ,
		    PGBUF_UNCONDITIONAL_LATCH);
  if (Root == NULL)
    {
      goto exit_on_error;
    }

  if (spage_get_record (Root, HEADER, &Rec, PEEK) != S_SUCCESS)
    {
      goto exit_on_error;
    }

  btree_read_root_header (&Rec, &root_header);

  btid_int.sys_btid = btid;
  ret = btree_glean_root_header_info (&root_header, &btid_int);
  if (ret != NO_ERROR)
    {
      goto exit_on_error;
    }

  /* traverse the tree and store the capacity info */
  ret = btree_get_subtree_capacity (thread_p, &btid_int, Root, cpc);
  if (ret != NO_ERROR)
    {
      goto exit_on_error;
    }

  pgbuf_unfix (thread_p, Root);
  Root = NULL;

end:

  return ret;

exit_on_error:

  if (Root)
    {
      pgbuf_unfix (thread_p, Root);
      Root = NULL;
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

/*
 * btree_dump_capacity () -
 *   return: NO_ERROR
 *   btid(in): B+tree index identifier
 *
 * Note: Dump index capacity/space information.
 */
int
btree_dump_capacity (THREAD_ENTRY * thread_p, BTID * btid)
{
  BTREE_CAPACITY cpc;
  int ret = NO_ERROR;

  /* get index capacity information */
  ret = btree_index_capacity (thread_p, btid, &cpc);
  if (ret != NO_ERROR)
    {
      goto exit_on_error;
    }

  fprintf (stdout,
	   "\n--------------------------------------------------"
	   "-----------\n");
  fprintf (stdout, "BTID: {{%d, %d}, %d}  CAPACITY INFORMATION:\n",
	   btid->vfid.volid, btid->vfid.fileid, btid->root_pageid);

  /* dump the capacity information */
  fprintf (stdout, "\nDistinct Key Count: %d\n", cpc.dis_key_cnt);
  fprintf (stdout, "Total Value Count: %d\n", cpc.tot_val_cnt);
  fprintf (stdout, "Average Value Count Per Key: %d\n", cpc.avg_val_per_key);
  fprintf (stdout, "Total Page Count: %d\n", cpc.tot_pg_cnt);
  fprintf (stdout, "Leaf Page Count: %d\n", cpc.leaf_pg_cnt);
  fprintf (stdout, "NonLeaf Page Count: %d\n", cpc.nleaf_pg_cnt);
  fprintf (stdout, "Height: %d\n", cpc.height);
  fprintf (stdout, "Average Key Length: %d\n", cpc.avg_key_len);
  fprintf (stdout, "Average Record Length: %d\n", cpc.avg_rec_len);
  fprintf (stdout, "Total Index Space: %.0f bytes\n", cpc.tot_space);
  fprintf (stdout, "Used Index Space: %.0f bytes\n", cpc.tot_used_space);
  fprintf (stdout, "Free Index Space: %.0f bytes\n", cpc.tot_free_space);
  fprintf (stdout, "Average Page Free Space: %.0f bytes\n",
	   cpc.avg_pg_free_sp);
  fprintf (stdout, "Average Page Key Count: %d\n", cpc.avg_pg_key_cnt);
  fprintf (stdout, "--------------------------------------------------"
	   "-----------\n");

end:

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
 * btree_dump_capacity_all () -
 *   return: NO_ERROR
 *
 * Note: Dump the capacity/space information of all indices.
 */
int
btree_dump_capacity_all (THREAD_ENTRY * thread_p)
{
  int num_files;		/* Number of files in the system */
  BTID btid;			/* Btree index identifier */
  VPID vpid;			/* Index root page identifier */
  int i;			/* Loop counter */
  int ret = NO_ERROR;

  /* Find number of files */
  num_files = file_get_numfiles (thread_p);
  if (num_files < 0)
    {
      goto exit_on_error;
    }

  /* Go to each file, check only the btree files */
  for (i = 0; i < num_files; i++)
    {
      if (file_find_nthfile (thread_p, &btid.vfid, i) != 1)
	{
	  break;
	}

      if (file_get_type (thread_p, &btid.vfid) != FILE_BTREE)
	{
	  continue;
	}

      if (file_find_nthpages (thread_p, &btid.vfid, &vpid, 0, 1) != 1)
	{
	  goto exit_on_error;
	}

      btid.root_pageid = vpid.pageid;

      ret = btree_dump_capacity (thread_p, &btid);
      if (ret != NO_ERROR)
	{
	  goto exit_on_error;
	}
    }				/* for */

end:

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
 * b+tree dump routines
 */

/*
 * btree_print_space () -
 *   return:
 *   n(in):
 */
static void
btree_print_space (int n)
{

  while (n--)			/* print n space character */
    {
      fprintf (stdout, " ");
    }

}

/*
 * btree_dump_page () -
 *   return: nothing
 *   btid(in): B+tree index identifier
 *   page_ptr(in): Page pointer
 *   pg_vpid(in): Page identifier
 *   n(in): Identation left margin (number of preceding blanks)
 *   level(in):
 *
 * Note: Dumps the content of the given page of the tree.
 */
static void
btree_dump_page (THREAD_ENTRY * thread_p, BTID_INT * btid, PAGE_PTR page_ptr,
		 VPID * pg_vpid, int n, int level)
{
  int key_cnt;
  int i;
  RECDES Rec;
  bool leaf_page;
  char *header_ptr;
  VPID next_vpid;

  /* get the header record */
  btree_get_header_ptr (page_ptr, &header_ptr);
  key_cnt = BTREE_GET_NODE_KEY_CNT (header_ptr);
  leaf_page = (BTREE_GET_NODE_TYPE (header_ptr) == LEAF_NODE) ? true : false;
  BTREE_GET_NODE_NEXT_VPID (header_ptr, &next_vpid);
  btree_print_space (n);
  fprintf (stdout,
	   "\n<<<<<<<<<<<<<<<<  N O D E   P A G E  >>>>>>>>>>>>>>>>> \n\n");
  btree_print_space (n);
  /* output header information */
  fprintf (stdout,
	   "--- Page_Id: {%d , %d} Node_Type: %s Key_Cnt: %d Next_Page_Id: {%d , %d} Max_Key_Len %d ---\n\n",
	   pg_vpid->volid, pg_vpid->pageid,
	   leaf_page ? "LEAF " : "NON_LEAF ", key_cnt,
	   next_vpid.volid, next_vpid.pageid,
	   BTREE_GET_NODE_MAX_KEY_LEN (header_ptr));
  fflush (stdout);

  if (key_cnt < 0)
    {
      fprintf (stdout,
	       "btree_dump_page: node key count underflow: %d\n", key_cnt);
      return;
    }

  if (level > 1)
    {
      /* output the content of each record */
      for (i = 1; i <= key_cnt; i++)
	{
	  (void) spage_get_record (page_ptr, i, &Rec, PEEK);
	  if (leaf_page)
	    {
	      btree_dump_leaf_record (thread_p, btid, &Rec, n);
	    }
	  else
	    {
	      btree_dump_non_leaf_record (thread_p, btid, &Rec, n, 1);
	    }
	  fprintf (stdout, "\n\n");
	}

      if (!leaf_page)
	{
	  /* print the last record of a non leaf page, it has no key */
	  (void) spage_get_record (page_ptr, key_cnt + 1, &Rec, PEEK);
	  btree_dump_non_leaf_record (thread_p, btid, &Rec, n, 0);
	  fprintf (stdout, "Last Rec, Key ignored.\n\n");
	}
    }

}

/*
 * btree_dump_page_with_subtree () -
 *   return: nothing
 *   btid(in): B+tree index identifier
 *   pg_ptr(in): Page pointer
 *   pg_vpid(in): Page identifier
 *   n(in): Identation left margin (number of preceding blanks)
 *   level(in):
 *
 * Note: Dumps the content of the given page together with its subtrees
 */
static void
btree_dump_page_with_subtree (THREAD_ENTRY * thread_p, BTID_INT * btid,
			      PAGE_PTR pg_ptr, VPID * pg_vpid, int n,
			      int level)
{
  char *header_ptr;
  int key_cnt, right;
  int i;
  NON_LEAF_REC NLeaf_Ptr;
  VPID page_vpid;
  PAGE_PTR page = NULL;
  RECDES Rec;

  btree_dump_page (thread_p, btid, pg_ptr, pg_vpid, n, level);	/* dump current page */

  /* get the header record */
  btree_get_header_ptr (pg_ptr, &header_ptr);
  if (BTREE_GET_NODE_TYPE (header_ptr) == NON_LEAF_NODE)
    {				/* page is non_leaf */
      key_cnt = BTREE_GET_NODE_KEY_CNT (header_ptr);

#if defined(BTREE_DEBUG)
      if (key_cnt < 0)
	{
	  fprintf (stdout,
		   "btree_dump_page_with_subtree: node key count underflow: %d.\n",
		   key_cnt);
	  return;
	}
#endif /* BTREE_DEBUG */

      /* for each child page pointer in this non_leaf page,
       * dump the corresponding subtree
       */
      right = key_cnt + 1;
      for (i = 1; i <= right; i++)
	{
	  (void) spage_get_record (pg_ptr, i, &Rec, PEEK);
	  btree_read_fixed_portion_of_non_leaf_record (&Rec, &NLeaf_Ptr);
	  page_vpid = NLeaf_Ptr.pnt;
	  page = pgbuf_fix (thread_p, &page_vpid, OLD_PAGE, PGBUF_LATCH_READ,
			    PGBUF_UNCONDITIONAL_LATCH);
	  btree_dump_page_with_subtree (thread_p, btid, page, &page_vpid,
					n + 2, level);
	  pgbuf_unfix (thread_p, page);
	  page = NULL;
	}
    }

}

/*
 * btree_dump () -
 *   return: nothing
 *   btid(in): B+tree index identifier
 *   level(in):
 *
 * Note: Dumps the content of the each page in the B+tree by
 * traversing the tree in an "inorder" manner. The header
 * information, as well as the content of each record in a page
 * are dumped. The header information for a non_leaf page
 * contains the key count and maximum key length information.
 * Maximum key length refers to the longest key in the page and
 * in its subtrees. The header information for a leaf page
 * contains also the next_page information, which is the page
 * identifier of the next sibling page, and the overflow page
 * count information. Root header information contains
 * statistical data for the whole tree. These consist of total
 * key count of the tree, total page count, leaf page count,
 * non_leaf page count, total overflow page count and the height
 * of the tree. Total key count refers only to those keys that
 * are stored in the leaf pages of the tree. The index key type
 * is also stored in the root header.
 */
void
btree_dump (THREAD_ENTRY * thread_p, BTID * btid, int level)
{
  VPID p_vpid;
  PAGE_PTR Root = NULL;
  RECDES Rec;
  BTID_INT btid_int;
  BTREE_ROOT_HEADER root_header;

  p_vpid.pageid = btid->root_pageid;	/* read root page */
  p_vpid.volid = btid->vfid.volid;
  Root = pgbuf_fix (thread_p, &p_vpid, OLD_PAGE, PGBUF_LATCH_READ,
		    PGBUF_UNCONDITIONAL_LATCH);
  (void) spage_get_record (Root, HEADER, &Rec, PEEK);
  btree_read_root_header (&Rec, &root_header);

  btid_int.sys_btid = btid;
  if (btree_glean_root_header_info (&root_header, &btid_int) != NO_ERROR)
    {				/* unknown error */
      pgbuf_unfix (thread_p, Root);
      Root = NULL;
      return;			/* do nothing */
    }

  fprintf (stdout,
	   "\n------------ The B+Tree Index Content: ---------------------\n\n");
  btree_dump_root_header (Rec);	/* output root header information */

  if (level != 0)
    {
      btree_dump_page_with_subtree (thread_p, &btid_int, Root, &p_vpid, 2,
				    level);
    }

  pgbuf_unfix (thread_p, Root);
  Root = NULL;
}

/*
 * btree_read_key_type () -
 *   return:
 *   btid(in):
 */
TP_DOMAIN *
btree_read_key_type (THREAD_ENTRY * thread_p, BTID * btid)
{
  VPID p_vpid;
  PAGE_PTR Root = NULL;
  char *header_ptr;
  TP_DOMAIN *key_type = NULL;

  p_vpid.pageid = btid->root_pageid;	/* read root page */
  p_vpid.volid = btid->vfid.volid;
  Root = pgbuf_fix (thread_p, &p_vpid, OLD_PAGE, PGBUF_LATCH_READ,
		    PGBUF_UNCONDITIONAL_LATCH);

  btree_get_header_ptr (Root, &header_ptr);
  (void) or_unpack_domain (header_ptr + BTREE_KEY_TYPE_OFFSET, &key_type, 0);

  pgbuf_unfix (thread_p, Root);
  Root = NULL;

  return key_type;
}

/*
 * btree_delete_from_leaf () -
 *   return: NO_ERROR
 *   btid(in):
 *   leaf_vpid(in):
 *   key(in):
 *   class_oid(in):
 *   oid(in):
 *   del_key(in):
 *
 * Note: Deletes the OID of the specified key from the leaf page
 * If the key or the OID doesn't exist, the corresponding
 * error codes are raised. Deletion of the last OID for the
 * key results also in the deletion of the key. Deletion of the
 * last key from the leaf page causes that page to be empty,
 * however the page is not deallocated. The removal of such an
 * empty page is done during merge operations in the later
 * deletions. When an OID is the deleted, it is actually
 * replaced by the last OID of the key, in order to avoid
 * expensive shift operations.  When the last OID is removed,
 * it may be the last OID on an overflow OID page.  This causes
 * the overflow page to be deleted and the overflow OID page
 * ptr chain to be updated appropriately.
 *
 * LOGGING Note: When the btree is new, splits and merges will
 * not be committed, but will be attached.  If the transaction
 * is rolled back, the merge and split actions will be rolled
 * back as well.  The undo (and redo) logging for splits and
 * merges are page based (physical) logs, thus the rest of the
 * logs for the undo session must be page based as well.  When
 * the btree is old, splits and merges are committed and all
 * the rest of the logging must be logical (non page based)
 * since pages may change as splits and merges are performed.
 */
static int
btree_delete_from_leaf (THREAD_ENTRY * thread_p, BTID_INT * btid,
			VPID * leaf_vpid, DB_VALUE * key, OID * class_oid,
			OID * oid, int *del_key)
{
  char *err_key, *oid_list_start, *keyvalp, *oid_list;
  OID last_class_oid;
  OID last_oid;
  PAGE_PTR last_pg = NULL;
  PAGE_PTR leaf_pg = NULL;
  VPID update_vpid, last_vpid, prev_vpid, next_ovfl_vpid;
  int del_oid_offset, oid_list_offset, oid_cnt, i, keyval_len;
  bool dummy;
  INT16 leaf_slot_id, slot_id;
  char *header_ptr;
  RECDES peek_rec;
  INT16 key_cnt;
  RECDES copy_rec;
  LEAF_REC leafrec_pnt;
  BTREE_NODE_HEADER header;
  char *recset_data = NULL;
  int oid_size;
#if defined(SERVER_MODE)
  bool old_check_interrupt;
#endif /* SERVER_MODE */
  int ret = NO_ERROR;

  if (BTREE_IS_UNIQUE (btid))
    {
      oid_size = 2 * OR_OID_SIZE;
    }
  else
    {
      oid_size = OR_OID_SIZE;
    }

  copy_rec.data = NULL;

#if defined(SERVER_MODE)
  old_check_interrupt = thread_set_check_interrupt (thread_p, false);
#endif /* SERVER_MODE */

  leaf_pg = pgbuf_fix (thread_p, leaf_vpid, OLD_PAGE, PGBUF_LATCH_WRITE,
		       PGBUF_UNCONDITIONAL_LATCH);
  if (leaf_pg == NULL)
    {
      goto exit_on_error;
    }
  last_pg = leaf_pg;
  last_vpid = *leaf_vpid;

  /* find the slot for the key */
  if (!btree_search_leaf_page (thread_p, btid, leaf_pg, key, &leaf_slot_id))
    {
      /* key does not exist */

      /* We can get this situation in the following senario:
       * We are recovering from a crash of a btree_insert() where
       * the key/value insert undo log was written and flushed to disk
       * but the OID insertion redo log was not flushed to disk.
       * This is the logging undo/redo coupling problem.  For now,
       * we will make this a warning severity.
       *
       * When this logging hole gets filled, we should remove the
       * warning severity.
       *
       * We put a NOOP redo log here, which does NOTHING, this is used
       * to accompany the corresponding logical undo log, if there is
       * any, which caused this routine to be called.
       */
      log_append_redo_data2 (thread_p, RVBT_NOOP, &btid->sys_btid->vfid,
			     leaf_pg, -1, 0, NULL);
      pgbuf_set_dirty (thread_p, leaf_pg, DONT_FREE);
      err_key = pr_valstring (key);

      er_set ((log_is_in_crash_recovery ())? ER_WARNING_SEVERITY :
	      ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_BTREE_UNKNOWN_KEY, 5,
	      (err_key) ? err_key : "_NULL_KEY", btid->sys_btid->vfid.fileid,
	      btid->sys_btid->vfid.volid, btid->sys_btid->root_pageid,
	      PR_TYPE_FROM_ID (btid->key_type->type->id)->name);
      er_log_debug (ARG_FILE_LINE,
		    "btree_delete_from_leaf: btree_search_leaf_page fails.");
      if (err_key)
	{
	  free_and_init (err_key);
	}
      goto exit_on_error;
    }

  copy_rec.area_size = DB_PAGESIZE;
  copy_rec.data = (char *) malloc (DB_PAGESIZE);
  if (copy_rec.data == NULL)
    {
      goto exit_on_error;
    }
  recset_data = (char *) malloc (DB_PAGESIZE);
  if (recset_data == NULL)
    {
      goto exit_on_error;
    }

  /* Discover where the OID to delete is in the OID list.  This could
   * be on the leaf page or on an OID overflow page.  Since we are
   * traversing the OID list, we can also find out the last OID in
   * the OID list.  We will eventually use the last OID to replace the
   * deleted OID in the OID list.
   */
  OID_SET_NULL (&last_class_oid);
  OID_SET_NULL (&last_oid);
  VPID_SET_NULL (&prev_vpid);
  VPID_SET_NULL (&update_vpid);
  del_oid_offset = -1;
  slot_id = leaf_slot_id;

  do
    {
      /* read the record that contains the key */
      if (spage_get_record (last_pg, slot_id, &copy_rec, COPY) != S_SUCCESS)
	{
	  goto exit_on_error;
	}

      if (last_pg == leaf_pg)
	{
	  btree_read_record (thread_p, btid, &copy_rec, NULL, &leafrec_pnt,
			     true, &dummy, &oid_list_offset, 0);
	  next_ovfl_vpid = leafrec_pnt.ovfl;
	  oid_list_start = copy_rec.data + oid_list_offset;
	  oid_cnt = (copy_rec.length - oid_list_offset) / oid_size;
	}
      else
	{
	  /* it is an overflow OID page */
	  btree_get_header_ptr (last_pg, &header_ptr);
	  btree_get_next_overflow_vpid (header_ptr, &next_ovfl_vpid);
	  oid_list_start = copy_rec.data;
	  oid_cnt = copy_rec.length / oid_size;
	}

      /* we only need to look for the OID to delete if we haven't already
       * found it.
       */
      if (del_oid_offset == -1)
	{
	  OID tmp_oid;

	  for (oid_list = oid_list_start, i = 0;
	       i < oid_cnt && del_oid_offset == -1; i++)
	    {
	      if (BTREE_IS_UNIQUE (btid))
		{
		  OR_GET_OID ((oid_list + OR_OID_SIZE), &tmp_oid);
		}
	      else
		{
		  OR_GET_OID (oid_list, &tmp_oid);
		}

	      if (OID_EQ (oid, &tmp_oid))
		{
		  /* we've found the oid that is to be deleted, remember
		   * the page and offset that it is on.
		   */
		  update_vpid = last_vpid;
		  del_oid_offset = oid_list - copy_rec.data;
		}
	      oid_list += oid_size;
	    }
	}

      /* Move to the next overflow page.  If there isn't one, we have
       * the final OID list and we can grab the last OID and shorten this
       * OID list (remember that the last OID will replace the deleted
       * OID in the OID list).
       */
      if (!VPID_ISNULL (&next_ovfl_vpid))
	{
	  prev_vpid = last_vpid;
	  last_vpid = next_ovfl_vpid;
	  VPID_SET_NULL (&next_ovfl_vpid);
	  pgbuf_unfix (thread_p, last_pg);
	  last_pg = NULL;
	  /* slot_id is 1 because the last page must now be an overflow
	   * oid page and their OID lists are always in slot 1.
	   */
	  slot_id = 1;
	  last_pg =
	    pgbuf_fix (thread_p, &last_vpid, OLD_PAGE, PGBUF_LATCH_WRITE,
		       PGBUF_UNCONDITIONAL_LATCH);
	  if (last_pg == NULL)
	    {
	      goto exit_on_error;
	    }
	}
      else
	{
	  /* copy the last OID */
	  if (BTREE_IS_UNIQUE (btid))
	    {
	      oid_list = copy_rec.data + copy_rec.length - oid_size;
	      OR_GET_OID (oid_list, &last_class_oid);
	      oid_list += OR_OID_SIZE;
	      OR_GET_OID (oid_list, &last_oid);
	    }
	  else
	    {
	      oid_list = copy_rec.data + copy_rec.length - OR_OID_SIZE;
	      OR_GET_OID (oid_list, &last_oid);
	    }

	  /* We only want to shorten the OID list if we have actually
	   * found the OID to delete.  Due to the log undo/redo coupling
	   * problem, we might not find the OID to delete and thus we should
	   * not change the Btree.  See the comment below.
	   */
	  if (del_oid_offset != -1)
	    {
	      /* log the logical undo for the keyval delete since we now
	       * now that the keyval exists.
	       */
	      if (file_new_isvalid (thread_p, &btid->sys_btid->vfid) ==
		  DISK_INVALID)
		{
		  /* "logical" undo logging needed (see comment
		   * in function header).
		   */
		  keyvalp = NULL;
		  ret = btree_rv_save_keyval (btid, key, class_oid, oid,
					      &keyvalp, &keyval_len);
		  if (ret != NO_ERROR)
		    {
		      goto exit_on_error;
		    }
		  log_append_undo_data2 (thread_p, RVBT_KEYVAL_DEL,
					 &btid->sys_btid->vfid, NULL, -1,
					 keyval_len, keyvalp);
		  if (keyvalp != NULL)
		    {
		      db_private_free_and_init (thread_p, keyvalp);
		      keyvalp = NULL;
		    }
		}

	      /* shorten the OID list */
	      oid_cnt--;

	      /* have we exhausted the current OID list? */
	      if (oid_cnt == 0)
		{
		  /* we either get rid of the page, if it is an overflow
		   * page, or we remove the key and slot from the leaf
		   * page.
		   */
		  if (VPID_EQ (&last_vpid, leaf_vpid))
		    {
		      *del_key = 1;

		      /* last OID deleted, delete the key and slot too */
		      if (leafrec_pnt.key_len < 0)
			{
			  /* get the overflow manager to delete the key */
			  ret =
			    btree_delete_overflow_key (thread_p, btid,
						       leaf_pg, slot_id,
						       true);
			  if (ret != NO_ERROR)
			    {
			      goto exit_on_error;
			    }
			}

		      if (file_new_isvalid (thread_p, &btid->sys_btid->vfid)
			  == DISK_VALID)
			{
			  /* page level undo logging needed (see comment
			   * in function header).
			   */
			  *(INT16 *) ((char *) recset_data + OFFS1) = 0;	/* Leaf Record */
			  *(INT16 *) ((char *) recset_data + OFFS2)
			    = copy_rec.type;
			  memcpy ((char *) recset_data + OFFS3,
				  copy_rec.data, copy_rec.length);
			  log_append_undo_data2 (thread_p, RVBT_NDRECORD_DEL,
						 &btid->sys_btid->vfid,
						 leaf_pg, slot_id,
						 copy_rec.length + OFFS3,
						 recset_data);
			}

		      /* now delete the btree slot */
		      if (spage_delete (thread_p, leaf_pg, slot_id) !=
			  slot_id)
			{
			  goto exit_on_error;
			}

		      /* key deleted, update node header */
		      if (spage_get_record (leaf_pg, HEADER, &peek_rec, PEEK)
			  != S_SUCCESS)
			{
			  goto exit_on_error;
			}

		      if (file_new_isvalid (thread_p, &btid->sys_btid->vfid)
			  == DISK_VALID)
			{
			  /* page level undo logging needed (see comment
			   * in function header).
			   */
			  *(INT16 *) ((char *) recset_data + OFFS1) = 0;	/* Leaf Record */
			  *(INT16 *) ((char *) recset_data + OFFS2)
			    = peek_rec.type;
			  memcpy ((char *) recset_data + OFFS3,
				  peek_rec.data, peek_rec.length);
			  log_append_undo_data2 (thread_p, RVBT_NDRECORD_UPD,
						 &btid->sys_btid->vfid,
						 leaf_pg, HEADER,
						 peek_rec.length + OFFS3,
						 recset_data);
			}

		      key_cnt = BTREE_GET_NODE_KEY_CNT (peek_rec.data);
		      key_cnt--;
		      BTREE_PUT_NODE_KEY_CNT (peek_rec.data, key_cnt);
		      if (key_cnt == 0)
			{
			  BTREE_PUT_NODE_MAX_KEY_LEN (peek_rec.data, 0);
			}

		      /* log the record deletion and the header update redo.
		       */
		      log_append_redo_data2 (thread_p, RVBT_LFRECORD_DEL,
					     &btid->sys_btid->vfid, leaf_pg,
					     slot_id, peek_rec.length,
					     peek_rec.data);

		      pgbuf_set_dirty (thread_p, leaf_pg, DONT_FREE);
		    }
		  else
		    {
		      /* We have an empty overflow page that we can delete */
		      pgbuf_unfix (thread_p, last_pg);
		      last_pg = NULL;
		      ret = file_dealloc_page (thread_p,
					       &btid->sys_btid->vfid,
					       &last_vpid);
		      if (ret != NO_ERROR)
			{
			  goto exit_on_error;
			}

		      /* grab the previous page */
		      last_vpid = prev_vpid;
		      last_pg = pgbuf_fix (thread_p, &last_vpid, OLD_PAGE,
					   PGBUF_LATCH_WRITE,
					   PGBUF_UNCONDITIONAL_LATCH);
		      if (last_pg == NULL)
			{
			  goto exit_on_error;
			}

		      /* NULL the previous page's overflow page pointer */
		      if (VPID_EQ (&prev_vpid, leaf_vpid))
			{
			  /* previous page is the leaf page */
			  if (spage_get_record (last_pg, leaf_slot_id,
						&copy_rec, COPY) != S_SUCCESS)
			    {
			      goto exit_on_error;
			    }

			  if (file_new_isvalid
			      (thread_p, &btid->sys_btid->vfid) == DISK_VALID)
			    {
			      /* page level undo logging needed (see comment
			       * in function header).
			       */
			      *(INT16 *) ((char *) recset_data + OFFS1) = 0;	/* Leaf Record */
			      *(INT16 *) ((char *) recset_data + OFFS2)
				= copy_rec.type;
			      memcpy ((char *) recset_data + OFFS3,
				      copy_rec.data, copy_rec.length);
			      log_append_undo_data2 (thread_p,
						     RVBT_NDRECORD_UPD,
						     &btid->sys_btid->vfid,
						     leaf_pg, leaf_slot_id,
						     copy_rec.length + OFFS3,
						     recset_data);
			    }

			  btree_read_fixed_portion_of_leaf_record (&copy_rec,
								   &leafrec_pnt);
			  VPID_SET_NULL (&leafrec_pnt.ovfl);
			  btree_write_fixed_portion_of_leaf_record (&copy_rec,
								    &leafrec_pnt);
			  if (spage_update
			      (thread_p, last_pg, leaf_slot_id,
			       &copy_rec) != SP_SUCCESS)
			    {
			      goto exit_on_error;
			    }

			  *(INT16 *) ((char *) recset_data + OFFS1) = 0;	/* Leaf Record */
			  *(INT16 *) ((char *) recset_data + OFFS2)
			    = copy_rec.type;
			  memcpy ((char *) recset_data + OFFS3,
				  copy_rec.data, copy_rec.length);
			  log_append_redo_data2 (thread_p, RVBT_NDRECORD_UPD,
						 &btid->sys_btid->vfid,
						 last_pg, leaf_slot_id,
						 copy_rec.length + OFFS3,
						 recset_data);
			  pgbuf_set_dirty (thread_p, last_pg, DONT_FREE);

			}
		      else
			{
			  /* previous page is an overflow page */
			  if (spage_get_record (last_pg, HEADER,
						&copy_rec, COPY) != S_SUCCESS)
			    {
			      goto exit_on_error;
			    }

			  if (file_new_isvalid
			      (thread_p, &btid->sys_btid->vfid) == DISK_VALID)
			    {
			      /* page level undo logging needed (see comment
			       * in function header).
			       */
			      *(INT16 *) ((char *) recset_data + OFFS1) = 0;	/* Leaf Record */
			      *(INT16 *) ((char *) recset_data + OFFS2)
				= copy_rec.type;
			      memcpy ((char *) recset_data + OFFS3,
				      copy_rec.data, copy_rec.length);
			      log_append_undo_data2 (thread_p,
						     RVBT_NDRECORD_UPD,
						     &btid->sys_btid->vfid,
						     last_pg, HEADER,
						     copy_rec.length + OFFS3,
						     recset_data);
			    }

			  VPID_SET_NULL (&next_ovfl_vpid);
			  btree_write_overflow_header (&copy_rec,
						       &next_ovfl_vpid);
			  if (spage_update
			      (thread_p, last_pg, HEADER,
			       &copy_rec) != SP_SUCCESS)
			    {
			      goto exit_on_error;
			    }

			  /* log the new header record for redo purposes */
			  log_append_redo_data2 (thread_p, RVBT_NDHEADER_UPD,
						 &btid->sys_btid->vfid,
						 last_pg, HEADER,
						 copy_rec.length,
						 copy_rec.data);
			  pgbuf_set_dirty (thread_p, last_pg, DONT_FREE);
			}
		    }
		}
	      else
		{
		  if (file_new_isvalid (thread_p, &btid->sys_btid->vfid) ==
		      DISK_VALID)
		    {
		      /* page level undo logging needed (see comment
		       * in function header).
		       */
		      *(INT16 *) ((char *) recset_data + OFFS1) = 0;	/* Leaf Record */
		      *(INT16 *) ((char *) recset_data + OFFS2)
			= copy_rec.type;
		      memcpy ((char *) recset_data + OFFS3,
			      copy_rec.data, copy_rec.length);
		      log_append_undo_data2 (thread_p, RVBT_NDRECORD_UPD,
					     &btid->sys_btid->vfid, last_pg,
					     slot_id, copy_rec.length + OFFS3,
					     recset_data);
		    }

		  copy_rec.length -= oid_size;

		  /* we still some OIDs in the list, write it out */
		  if (spage_update (thread_p, last_pg, slot_id, &copy_rec) !=
		      SP_SUCCESS)
		    {
		      goto exit_on_error;
		    }
		  pgbuf_set_dirty (thread_p, last_pg, DONT_FREE);

		  log_append_redo_data2 (thread_p, RVBT_OID_TRUNCATE,
					 &btid->sys_btid->vfid, last_pg,
					 slot_id, OR_INT_SIZE, &oid_size);
		}
	    }
	  pgbuf_unfix (thread_p, last_pg);
	  last_pg = NULL;
	}
    }
  while (OID_ISNULL (&last_oid));

  if (del_oid_offset == -1)
    {
      /* OID does not exist */

      /* We can get this situation in the following senario:
       * We are recovering from a crash of a btree_insert() where
       * the key/value insert undo log was written and flushed to disk
       * but the OID insertion redo log was not flushed to disk.
       * This is the logging undo/redo coupling problem.  For now,
       * we will make this a warning severity.
       *
       * When this logging hole gets filled, we should remove the
       * warning severity.
       *
       * We put a NOOP redo log here, which does NOTHING, this is used
       * to accompany the corresponding logical undo log, if there is
       * any, which caused this routine to be called.
       */
      log_append_redo_data2 (thread_p, RVBT_NOOP, &btid->sys_btid->vfid,
			     leaf_pg, -1, 0, NULL);
      pgbuf_set_dirty (thread_p, leaf_pg, DONT_FREE);
      err_key = pr_valstring (key);

      er_set ((log_is_in_crash_recovery ())? ER_WARNING_SEVERITY :
	      ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_BTREE_UNKNOWN_KEY, 5,
	      (err_key) ? err_key : "_NULL_KEY", btid->sys_btid->vfid.fileid,
	      btid->sys_btid->vfid.volid, btid->sys_btid->root_pageid,
	      PR_TYPE_FROM_ID (btid->key_type->type->id)->name);
      er_log_debug (ARG_FILE_LINE,
		    "btree_delete_from_leaf: caused by del_oid_offset == -1.");
      if (err_key)
	{
	  free_and_init (err_key);
	}
      goto exit_on_error;
    }

  /* At this point, we have shortened the OID list, removed any newly
   * empty overflow pages, and found the position and page of the OID to
   * delete.  We just need to replace the deleted OID with the last OID.
   *
   * If we got lucky and the OID that we wanted to delete was the last
   * OID in the list.  It was deleted when we shortened the OID list and
   * we've nothing left to do.
   */
  if (!OID_EQ (oid, &last_oid))
    {
      /* use last_vpid and last_pg variables so that the error cleanup
       * code is consistent and less messy.
       */
      last_vpid = update_vpid;
      if (VPID_EQ (leaf_vpid, &last_vpid))
	{
	  slot_id = leaf_slot_id;
	}
      else
	{
	  slot_id = 1;		/* overflow OIDs are always in slot 1 */
	}

      last_pg = pgbuf_fix (thread_p, &last_vpid, OLD_PAGE, PGBUF_LATCH_WRITE,
			   PGBUF_UNCONDITIONAL_LATCH);
      if (last_pg == NULL)
	{
	  goto exit_on_error;
	}

      if (spage_get_record (last_pg, slot_id, &copy_rec, COPY) != S_SUCCESS)
	{
	  goto exit_on_error;
	}

      if (file_new_isvalid (thread_p, &btid->sys_btid->vfid) == DISK_VALID)
	{
	  /* page level undo logging needed (see comment
	   * in function header).
	   */
	  *(INT16 *) ((char *) recset_data + OFFS1) = 0;	/* Leaf Record */
	  *(INT16 *) ((char *) recset_data + OFFS2) = copy_rec.type;
	  memcpy ((char *) recset_data + OFFS3,
		  copy_rec.data, copy_rec.length);
	  log_append_undo_data2 (thread_p, RVBT_NDRECORD_UPD,
				 &btid->sys_btid->vfid, last_pg, slot_id,
				 copy_rec.length + OFFS3, recset_data);
	}

      if (!OID_ISNULL (&last_class_oid))
	{
	  OR_PUT_OID ((copy_rec.data + del_oid_offset), &last_class_oid);
	  del_oid_offset += OR_OID_SIZE;
	}

      OR_PUT_OID ((copy_rec.data + del_oid_offset), &last_oid);

      if (spage_update (thread_p, last_pg, slot_id, &copy_rec) != SP_SUCCESS)
	{
	  goto exit_on_error;
	}

      *(INT16 *) ((char *) recset_data + OFFS1) = 0;	/* doesn't matter */
      *(INT16 *) ((char *) recset_data + OFFS2) = copy_rec.type;
      memcpy ((char *) recset_data + OFFS3, copy_rec.data, copy_rec.length);
      log_append_redo_data2 (thread_p, RVBT_NDRECORD_UPD,
			     &btid->sys_btid->vfid, last_pg, slot_id,
			     copy_rec.length + OFFS3, recset_data);
      pgbuf_set_dirty (thread_p, last_pg, DONT_FREE);
      pgbuf_unfix (thread_p, last_pg);
      last_pg = NULL;
    }

  free_and_init (copy_rec.data);
  free_and_init (recset_data);

end:

#if defined(SERVER_MODE)
  (void) thread_set_check_interrupt (thread_p, old_check_interrupt);
#endif /* SERVER_MODE */

  return ret;

exit_on_error:

  if (last_pg)
    {
      pgbuf_unfix (thread_p, last_pg);
      last_pg = NULL;
    }
  if (copy_rec.data)
    {
      free_and_init (copy_rec.data);
    }
  if (recset_data)
    {
      free_and_init (recset_data);
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

/*
 * btree_merge_root () -
 *   return: NO_ERROR
 *   btid(in): B+tree index identifier
 *   P(in): Page pointer for the root to be merged
 *   Q(in): Page pointer for the root child page to be merged
 *   R(in): Page pointer for the root child page to be merged
 *   P_vpid(in): Page identifier for page P
 *   Q_vpid(in): Page identifier for page Q
 *   R_vpid(in): Page identifier for page R
 *   leaf_page(in): Flag which shows whether root child pages are leaf, or not
 *
 * Note: When the root page has only two children (leaf or non_leaf)
 * that can be merged together, then they are merged through
 * this specific root merge operation. The main distinction of
 * this routine from the regular merge operation is that in this
 * the content of the two child pages are moved to the root, in
 * order not to change the originial root page. The root can also
 * be a specific non-leaf page, that is, it may have only one key
 * and one child page pointer. In this case, R_id, the page
 * identifier for the page R is NULL_PAGEID. In both cases, the
 * height of the tree is reduced by one, after the merge
 * operation. The two (one) child pages are not deallocated by
 * this routine. Deallocation of these pages are left to the
 * calling routine.
 *
 * Note:  Page Q and Page R contents are not changed by this routine,
 * since these pages will be deallocated by the calling routine.
 */
static int
btree_merge_root (THREAD_ENTRY * thread_p, BTID_INT * btid, PAGE_PTR P,
		  PAGE_PTR Q, PAGE_PTR R, VPID * P_vpid, VPID * Q_vpid,
		  VPID * R_vpid, bool leaf_page)
{
  int left_cnt, right_cnt;
  RECDES peek_rec;
  RECDES peek_rec1, peek_rec2, copy_rec;
  NON_LEAF_REC NLeaf_Pnt, NonLeaf_Rec;
  int i;
  int key_len, offset;
  DB_VALUE mid_key;
  int ovfl_key;
  bool clear_key = false;
  char *recset_data;		/* for recovery purposes */
  int recset_length;		/* for recovery purposes */
  RECSET_HEADER recset_header;	/* for recovery purposes */
  int sp_success;
  LOG_LSA temp_lsa;
  int recset_data_length;
  PGLENGTH log_addr_offset;
  int ret = NO_ERROR;

  /* initializations */
  copy_rec.data = NULL;
  recset_data = NULL;
  db_make_null (&mid_key);

  ovfl_key = false;

  /* log the P record contents for undo purposes,
   * if a crash happens the records of root page P will be inserted
   * back. There is no need for undo logs for pages Q and R,
   * since they are not changed by this routine, because they will be
   * deallocated after a succesful merge operation. There is also no need
   * for redo logs for pages Q and R, since these pages will be deallocated
   * by the caller routine.
   */

  /* for recovery purposes */
  recset_data = (char *) malloc (DB_PAGESIZE);
  if (recset_data == NULL)
    {
      goto exit_on_error;
    }

  left_cnt = spage_number_of_records (Q) - 1;
  right_cnt = spage_number_of_records (R) - 1;

  /* read the first record and the mid key */
  if (spage_get_record (P, 1, &peek_rec1, PEEK) != S_SUCCESS)
    {
      goto exit_on_error;
    }

  /* we have to copy the key here because we will soon be overwriting this
   * page and our pointer will point to a record from one of the other pages.
   */
  btree_read_record (thread_p, btid, &peek_rec1, &mid_key, &NLeaf_Pnt, false,
		     &clear_key, &offset, 1);
  ovfl_key = ((NLeaf_Pnt.key_len) < 0);

  /* get the record to see if it has overflow pages */
  if (spage_get_record (P, 2, &peek_rec2, PEEK) != S_SUCCESS)
    {
      goto exit_on_error;
    }

  btree_read_fixed_portion_of_non_leaf_record (&peek_rec2, &NLeaf_Pnt);

  /* delete second record */
  /* prepare undo log record */
  *(INT16 *) ((char *) recset_data + OFFS1) = 0;	/* leaf record */
  *(INT16 *) ((char *) recset_data + OFFS2) = peek_rec2.type;
  memcpy ((char *) recset_data + OFFS3, peek_rec2.data, peek_rec2.length);
  recset_data_length = peek_rec2.length;

  if (NLeaf_Pnt.key_len < 0)
    {				/* overflow key */
      /* get the overflow manager to delete the key */
      ret = btree_delete_overflow_key (thread_p, btid, P, 2, false);
      if (ret != NO_ERROR)
	{
	  goto exit_on_error;
	}
    }

  if (spage_delete (thread_p, P, 2) != 2)
    {
      goto exit_on_error;
    }

  log_addr_offset = 2;
  /* log the deleted slotid for undo/redo purposes */
  log_append_undoredo_data2 (thread_p, RVBT_NDRECORD_DEL,
			     &btid->sys_btid->vfid, P, log_addr_offset,
			     recset_data_length + OFFS3,
			     sizeof (log_addr_offset), recset_data,
			     &log_addr_offset);

  /* delete first record */
  /* prepare undo log record */
  *(INT16 *) ((char *) recset_data + OFFS1) = 0;	/* leaf record */
  *(INT16 *) ((char *) recset_data + OFFS2) = peek_rec1.type;
  memcpy ((char *) recset_data + OFFS3, peek_rec1.data, peek_rec1.length);
  recset_data_length = peek_rec1.length;

  if (ovfl_key)
    {
      /* get the overflow manager to delete the key */
      ret = btree_delete_overflow_key (thread_p, btid, P, 1, false);
      if (ret != NO_ERROR)
	{
	  goto exit_on_error;
	}
    }

  if (spage_delete (thread_p, P, 1) != 1)
    {
      goto exit_on_error;
    }

  log_addr_offset = 1;
  /* log the deleted slotid for undo/redo purposes */
  log_append_undoredo_data2 (thread_p, RVBT_NDRECORD_DEL,
			     &btid->sys_btid->vfid, P, log_addr_offset,
			     recset_data_length + OFFS3,
			     sizeof (log_addr_offset), recset_data,
			     &log_addr_offset);

  /* Log the page Q records for undo/redo purposes on page P. */
  recset_header.rec_cnt = left_cnt;
  recset_header.first_slotid = 1;
  ret = btree_rv_util_save_page_records (Q, 1, left_cnt, 1, recset_data,
					 &recset_length);
  if (ret != NO_ERROR)
    {
      goto exit_on_error;
    }

  /* move content of the left page to the root page */
  for (i = 1; i <= left_cnt; i++)
    {
      if (spage_get_record (Q, i, &peek_rec2, PEEK) != S_SUCCESS ||
	  ((sp_success =
	    spage_insert_at (thread_p, P, i, &peek_rec2)) != SP_SUCCESS))
	{
	  if (i > 1)
	    {
	      recset_header.rec_cnt = i - 1;
	      recset_header.first_slotid = 1;
	      log_append_undo_data2 (thread_p, RVBT_INS_PGRECORDS,
				     &btid->sys_btid->vfid, P, -1,
				     sizeof (RECSET_HEADER), &recset_header);
	    }
	  goto exit_on_error;
	}
    }				/* for */

  log_append_undoredo_data2 (thread_p, RVBT_INS_PGRECORDS,
			     &btid->sys_btid->vfid, P, -1,
			     sizeof (RECSET_HEADER), recset_length,
			     &recset_header, recset_data);

  /* increment lsa of the page to be deallocated */
  LSA_COPY (&temp_lsa, pgbuf_get_lsa (Q));
  temp_lsa.offset++;
  pgbuf_set_lsa (thread_p, Q, &temp_lsa);
  pgbuf_set_dirty (thread_p, Q, DONT_FREE);

  if (!leaf_page)
    {				/* form the middle record in the root */

      copy_rec.area_size = DB_PAGESIZE;
      copy_rec.data = (char *) malloc (DB_PAGESIZE);
      if (copy_rec.data == NULL)
	{
	  goto exit_on_error;
	}

      if (spage_get_record (P, left_cnt, &copy_rec, COPY) != S_SUCCESS)
	{
	  goto exit_on_error;
	}

      btree_read_fixed_portion_of_non_leaf_record (&copy_rec, &NLeaf_Pnt);
      NonLeaf_Rec.pnt = NLeaf_Pnt.pnt;
      key_len = btree_get_key_length (&mid_key);
      NonLeaf_Rec.key_len =
	(key_len < BTREE_MAX_KEYLEN_INPAGE) ? key_len : -1;
      ret =
	btree_write_record (thread_p, btid, &NonLeaf_Rec, &mid_key, leaf_page,
			    (NonLeaf_Rec.key_len == -1), key_len, false, NULL,
			    NULL, &copy_rec);
      if (ret != NO_ERROR)
	{
	  goto exit_on_error;
	}

      if (spage_update (thread_p, P, left_cnt, &copy_rec) != SP_SUCCESS)
	{
	  goto exit_on_error;
	}

      /* log the new node record for redo purposes */
      *(INT16 *) ((char *) recset_data + OFFS1) = 1;	/* NonLeaf Record */
      *(INT16 *) ((char *) recset_data + OFFS2) = copy_rec.type;
      memcpy ((char *) recset_data + OFFS3, copy_rec.data, copy_rec.length);
      log_append_redo_data2 (thread_p, RVBT_NDRECORD_UPD,
			     &btid->sys_btid->vfid, P, left_cnt,
			     copy_rec.length + OFFS3, recset_data);

      free_and_init (copy_rec.data);
    }

  /* Log the page R records for undo purposes on page P. */
  recset_header.rec_cnt = right_cnt;
  recset_header.first_slotid = left_cnt + 1;

  ret = btree_rv_util_save_page_records (R, 1, right_cnt, left_cnt + 1,
					 recset_data, &recset_length);
  if (ret != NO_ERROR)
    {
      goto exit_on_error;
    }

  /* move content of the right page to the root page */
  for (i = 1; i <= right_cnt; i++)
    {
      if ((spage_get_record (R, i, &peek_rec2, PEEK) != S_SUCCESS) ||
	  (spage_insert_at (thread_p, P, left_cnt + i, &peek_rec2) !=
	   SP_SUCCESS))
	{
	  if (i > 1)
	    {
	      recset_header.rec_cnt = i - 1;
	      recset_header.first_slotid = left_cnt + 1;
	      log_append_undo_data2 (thread_p, RVBT_INS_PGRECORDS,
				     &btid->sys_btid->vfid, P, -1,
				     sizeof (RECSET_HEADER), &recset_header);
	    }
	  goto exit_on_error;
	}
    }				/* for */

  log_append_undoredo_data2 (thread_p, RVBT_INS_PGRECORDS,
			     &btid->sys_btid->vfid, P, -1,
			     sizeof (RECSET_HEADER), recset_length,
			     &recset_header, recset_data);

  /* update root page */
  if (spage_get_record (P, HEADER, &peek_rec, PEEK) != S_SUCCESS)
    {
      goto exit_on_error;
    }

  /* log the before image of the root header */
  log_append_undo_data2 (thread_p, RVBT_NDHEADER_UPD, &btid->sys_btid->vfid,
			 P, HEADER, peek_rec.length, peek_rec.data);

  BTREE_PUT_NODE_TYPE (peek_rec.data,
		       (leaf_page) ? LEAF_NODE : NON_LEAF_NODE);

  /* Care must be taken here in figuring the key count. Remember that
   * leaf nodes have the real key cnt, while, for some stupid reason,
   * non leaf nodes have a key count one less than the actual count.
   */
  BTREE_PUT_NODE_KEY_CNT (peek_rec.data,
			  (leaf_page) ? (left_cnt + right_cnt) : (left_cnt +
								  right_cnt -
								  1));
  {
    VPID null_vpid;

    VPID_SET_NULL (&null_vpid);
    BTREE_PUT_NODE_NEXT_VPID (peek_rec.data, &null_vpid);
  }

  /* log the new header record for redo purposes */
  log_append_redo_data2 (thread_p, RVBT_NDHEADER_UPD, &btid->sys_btid->vfid,
			 P, HEADER, peek_rec.length, peek_rec.data);

  pgbuf_set_dirty (thread_p, P, DONT_FREE);

  /* increment lsa of the page to be deallocated */
  LSA_COPY (&temp_lsa, pgbuf_get_lsa (R));
  temp_lsa.offset++;
  pgbuf_set_lsa (thread_p, R, &temp_lsa);
  pgbuf_set_dirty (thread_p, R, DONT_FREE);

  btree_clear_key_value (&clear_key, &mid_key);
  free_and_init (recset_data);

  return ret;

exit_on_error:

  if (copy_rec.data)
    {
      free_and_init (copy_rec.data);
    }
  if (recset_data)
    {
      free_and_init (recset_data);
    }

  btree_clear_key_value (&clear_key, &mid_key);

  if (ret == NO_ERROR)
    {
      ret = er_errid ();
      if (ret == NO_ERROR)
	{
	  ret = ER_FAILED;
	}
    }

  return ret;
}


/*
 * btree_merge_node () -
 *   return: NO_ERROR
 *   btid(in): The B+tree index identifier
 *   P(in): Page pointer for the parent page of page Q
 *   Q(in): Page pointer for the child page of P that will be merged
 *   R(in): Page pointer for the left or right sibling of page Q
 *   P_vpid(in): Page identifier for page P
 *   Q_vpid(in): Page identifier for page Q
 *   R_vpid(in): Page identifier for page R
 *   p_slot_id(in): The slot of parent page P which points to page Q
 *   leaf_page(in): Flag which shows whether page Q is a leaf page, or not
 *   is_left_merge(in): Flag which shows whether Q will merge with its left or
 *                   right sibling
 *   child_vpid(in): Child page identifier to be followed, Q or R.
 *
 * Note: Page Q is merged with page R which may be its left or right
 * sibling. Depending on the efficiency of the merge operation
 * the merge operation may take place on Page Q or on page R to
 * reduce the size of the data that will moved. After the merge
 * operation either page Q or page R becomes ready for
 * deallocation. Deallocation is left to the calling routine.
 *
 * Note:  The page which will be deallocted by the caller after a
 * succesful merge operation is not changed by this routine.
 */
static int
btree_merge_node (THREAD_ENTRY * thread_p, BTID_INT * btid, PAGE_PTR P,
		  PAGE_PTR Q, PAGE_PTR R, VPID * P_vpid, VPID * Q_vpid,
		  VPID * R_vpid, INT16 p_slot_id, bool leaf_page,
		  int is_left_merge, VPID * child_vpid)
{
  INT16 left_slotid, right_slotid;
  PAGE_PTR left_pg = NULL;
  PAGE_PTR right_pg = NULL;
  VPID left_vpid, right_vpid, next_vpid;
  int left_cnt, right_cnt;
  INT16 key_cnt;
  char *header_ptr;
  RECDES peek_rec;
  RECDES peek_rec1, peek_rec2, copy_rec;
  NON_LEAF_REC NLeaf_Pnt, NonLeaf_Rec, junk_rec;
  int i;
  int max_key, key_len, offset;
  DB_VALUE left_key, right_key;
  bool clear_lkey = false, clear_rkey = false;
  char *recset_data;		/* for recovery purposes */
  int recset_length;		/* for recovery purposes */
  RECSET_HEADER recset_header;	/* for recovery purposes */
  short node_type;		/* for recovery purposes */
  LOG_LSA temp_lsa;
  int ret = NO_ERROR;

  /* initializations */
  recset_data = NULL;
  copy_rec.data = NULL;
  db_make_null (&left_key);
  db_make_null (&right_key);
  VPID_SET_NULL (child_vpid);
  copy_rec.area_size = DB_PAGESIZE;
  copy_rec.data = (char *) malloc (DB_PAGESIZE);
  if (copy_rec.data == NULL)
    {
      goto exit_on_error;
    }
  recset_data = (char *) malloc (DB_PAGESIZE);
  if (recset_data == NULL)
    {
      goto exit_on_error;
    }

  node_type = (leaf_page) ? LEAF_NODE : NON_LEAF_NODE;

  left_pg = is_left_merge ? R : Q;
  if (is_left_merge)
    {
      left_vpid = *R_vpid;
    }
  else
    {
      left_vpid = *Q_vpid;
    }
  left_slotid = is_left_merge ? (p_slot_id - 1) : p_slot_id;
  right_pg = is_left_merge ? Q : R;
  if (is_left_merge)
    {
      right_vpid = *Q_vpid;
    }
  else
    {
      right_vpid = *R_vpid;
    }
  right_slotid = is_left_merge ? p_slot_id : (p_slot_id + 1);
  left_cnt = spage_number_of_records (left_pg) - 1;
  right_cnt = spage_number_of_records (right_pg) - 1;

  if (leaf_page
      || (spage_get_free_space (thread_p, right_pg) >=
	  spage_get_free_space (thread_p, left_pg)))
    {

      /* move the keys from the right page to the left page */
      if (!leaf_page)
	{

	  /* move the key from the parent P to left child */
	  if (spage_get_record (P, left_slotid, &peek_rec1, PEEK) !=
	      S_SUCCESS)
	    {
	      goto exit_on_error;
	    }

	  btree_read_record (thread_p, btid, &peek_rec1, &left_key, &junk_rec,
			     false, &clear_lkey, &offset, 0);

	  /* Warning!!! Don't use peek_rec1 again since left_key may point
	   * into it.  Use peek_rec2.
	   */

	  left_cnt = spage_number_of_records (left_pg) - 1;

	  /* we need to use COPY here instead of PEEK because we are updating
	   * the child page pointer in place.
	   */
	  if (spage_get_record (left_pg, left_cnt, &copy_rec, COPY) !=
	      S_SUCCESS)
	    {
	      goto exit_on_error;
	    }

	  /* log the old node record for undo purposes */
	  *(INT16 *) ((char *) recset_data + OFFS1) = node_type;
	  *(INT16 *) ((char *) recset_data + OFFS2) = copy_rec.type;
	  memcpy ((char *) recset_data + OFFS3, copy_rec.data,
		  copy_rec.length);
	  log_append_undo_data2 (thread_p, RVBT_NDRECORD_UPD,
				 &btid->sys_btid->vfid, left_pg, left_cnt,
				 copy_rec.length + OFFS3, recset_data);

	  btree_read_fixed_portion_of_non_leaf_record (&copy_rec, &NLeaf_Pnt);
	  NonLeaf_Rec.pnt = NLeaf_Pnt.pnt;
	  key_len = btree_get_key_length (&left_key);
	  NonLeaf_Rec.key_len =
	    (key_len < BTREE_MAX_KEYLEN_INPAGE) ? key_len : -1;
	  ret =
	    btree_write_record (thread_p, btid, &NonLeaf_Rec, &left_key,
				leaf_page, (NonLeaf_Rec.key_len == -1),
				key_len, false, NULL, NULL, &copy_rec);
	  if (ret != NO_ERROR)
	    {
	      goto exit_on_error;
	    }

	  if (spage_update (thread_p, left_pg, left_cnt, &copy_rec) !=
	      SP_SUCCESS)
	    {
	      goto exit_on_error;
	    }

	  /* log the new node record for redo purposes */
	  memcpy ((char *) recset_data + OFFS3, copy_rec.data,
		  copy_rec.length);
	  log_append_redo_data2 (thread_p, RVBT_NDRECORD_UPD,
				 &btid->sys_btid->vfid, left_pg, left_cnt,
				 copy_rec.length + OFFS3, recset_data);
	}


      /* Log the right page records for undo purposes on the left page. */
      recset_header.rec_cnt = right_cnt;
      recset_header.first_slotid = left_cnt + 1;
      ret = btree_rv_util_save_page_records
	(right_pg, 1, right_cnt, left_cnt + 1, recset_data, &recset_length);
      if (ret != NO_ERROR)
	{
	  goto exit_on_error;
	}

      /* move content of the right page to the left page */
      for (i = 1; i <= right_cnt; i++)
	{
	  if ((spage_get_record (right_pg, i, &peek_rec2, PEEK) !=
	       S_SUCCESS)
	      ||
	      (spage_insert_at (thread_p, left_pg, left_cnt + i, &peek_rec2)
	       != SP_SUCCESS))
	    {
	      if (i > 1)
		{
		  recset_header.rec_cnt = i - 1;
		  recset_header.first_slotid = left_cnt + 1;
		  log_append_undo_data2 (thread_p, RVBT_INS_PGRECORDS,
					 &btid->sys_btid->vfid,
					 left_pg, -1, sizeof (RECSET_HEADER),
					 &recset_header);
		}
	      goto exit_on_error;
	    }
	}			/* for */

      log_append_undoredo_data2 (thread_p, RVBT_INS_PGRECORDS,
				 &btid->sys_btid->vfid, left_pg, -1,
				 sizeof (RECSET_HEADER), recset_length,
				 &recset_header, recset_data);

      /* update parent page P, use COPY here instead of PEEK because we
       * are updating the child pointer in place.
       */
      if (spage_get_record (P, right_slotid, &copy_rec, COPY) != S_SUCCESS)
	{
	  goto exit_on_error;
	}

      /* log the old node record for undo purposes */
      *(INT16 *) ((char *) recset_data + OFFS1) = 1;	/* NonLeaf Record */
      *(INT16 *) ((char *) recset_data + OFFS2) = copy_rec.type;
      memcpy ((char *) recset_data + OFFS3, copy_rec.data, copy_rec.length);
      log_append_undo_data2 (thread_p, RVBT_NDRECORD_UPD,
			     &btid->sys_btid->vfid, P, right_slotid,
			     copy_rec.length + OFFS3, recset_data);

      btree_read_fixed_portion_of_non_leaf_record (&copy_rec, &NLeaf_Pnt);
      NLeaf_Pnt.pnt = left_vpid;

      btree_write_fixed_portion_of_non_leaf_record (&copy_rec, &NLeaf_Pnt);
      if (spage_update (thread_p, P, right_slotid, &copy_rec) != SP_SUCCESS)
	{
	  goto exit_on_error;
	}

      /* log the new node record for redo purposes */
      memcpy ((char *) recset_data + OFFS3, copy_rec.data, copy_rec.length);
      log_append_redo_data2 (thread_p, RVBT_NDRECORD_UPD,
			     &btid->sys_btid->vfid, P, right_slotid,
			     copy_rec.length + OFFS3, recset_data);

      /* get and log the old node record to be deleted for undo purposes */

      if (spage_get_record (P, left_slotid, &peek_rec2, PEEK) != S_SUCCESS)
	{
	  goto exit_on_error;
	}

      btree_read_fixed_portion_of_non_leaf_record (&peek_rec2, &NLeaf_Pnt);

      if (NLeaf_Pnt.key_len < 0)
	{			/* overflow key */
	  /* get the overflow manager to delete the key */
	  ret =
	    btree_delete_overflow_key (thread_p, btid, P, left_slotid, false);
	  if (ret != NO_ERROR)
	    {
	      goto exit_on_error;
	    }
	}

      *(INT16 *) ((char *) recset_data + OFFS1) = 1;	/* NonLeaf Record */
      *(INT16 *) ((char *) recset_data + OFFS2) = peek_rec2.type;
      memcpy ((char *) recset_data + OFFS3, peek_rec2.data, peek_rec2.length);
      log_append_undoredo_data2 (thread_p, RVBT_NDRECORD_DEL,
				 &btid->sys_btid->vfid, P, left_slotid,
				 peek_rec2.length + OFFS3,
				 sizeof (left_slotid), recset_data,
				 &left_slotid);

      if (spage_delete (thread_p, P, left_slotid) != left_slotid)
	{
	  goto exit_on_error;
	}

      /* get right page header information */
      btree_get_header_ptr (right_pg, &header_ptr);
      BTREE_GET_NODE_NEXT_VPID (header_ptr, &next_vpid);
      max_key = BTREE_GET_NODE_MAX_KEY_LEN (header_ptr);

      /* update left page header
       */
      if (spage_get_record (left_pg, HEADER, &peek_rec, PEEK) != S_SUCCESS)
	{
	  goto exit_on_error;
	}

      /* log the old header record for undo purposes */
      log_append_undo_data2 (thread_p, RVBT_NDHEADER_UPD,
			     &btid->sys_btid->vfid, left_pg, HEADER,
			     peek_rec.length, peek_rec.data);

      /* The key count already incorporates the semantics of the
       * non leaf/leaf key count difference (non leaf key count is one less
       * than the actual key count).  We simply increment the key count by the
       * actual number of keys we've added to the left page.
       */
      key_cnt = BTREE_GET_NODE_KEY_CNT (peek_rec.data);
      key_cnt += right_cnt;
      BTREE_PUT_NODE_KEY_CNT (peek_rec.data, key_cnt);
      BTREE_PUT_NODE_NEXT_VPID (peek_rec.data, &next_vpid);
      if (max_key > BTREE_GET_NODE_MAX_KEY_LEN (peek_rec.data))
	{
	  BTREE_PUT_NODE_MAX_KEY_LEN (peek_rec.data, max_key);
	}

      /* log the new header record for redo purposes */
      log_append_redo_data2 (thread_p, RVBT_NDHEADER_UPD,
			     &btid->sys_btid->vfid, left_pg, HEADER,
			     peek_rec.length, peek_rec.data);

      *child_vpid = left_vpid;

      pgbuf_set_dirty (thread_p, left_pg, DONT_FREE);

      /* increment lsa of the page to be deallocated */
      LSA_COPY (&temp_lsa, pgbuf_get_lsa (right_pg));
      temp_lsa.offset++;
      pgbuf_set_lsa (thread_p, right_pg, &temp_lsa);
      pgbuf_set_dirty (thread_p, right_pg, DONT_FREE);

    }
  else
    {				/* move from left to right */

      /* move the keys from the left page to the right page */

      /* Log the left page records for undo purposes on the right page. */
      recset_header.rec_cnt = left_cnt;
      recset_header.first_slotid = 1;
      ret = btree_rv_util_save_page_records (left_pg, 1, left_cnt, 1,
					     recset_data, &recset_length);
      if (ret != NO_ERROR)
	{
	  goto exit_on_error;
	}

      /* move content of the left page to the right page */
      for (i = 1; i <= left_cnt; i++)
	{
	  if ((spage_get_record (left_pg, i, &peek_rec2, PEEK) !=
	       S_SUCCESS)
	      || (spage_insert_at (thread_p, right_pg, i, &peek_rec2) !=
		  SP_SUCCESS))
	    {
	      if (i > 1)
		{
		  recset_header.rec_cnt = i - 1;
		  recset_header.first_slotid = 1;
		  log_append_undo_data2 (thread_p, RVBT_INS_PGRECORDS,
					 &btid->sys_btid->vfid, right_pg, -1,
					 sizeof (RECSET_HEADER),
					 &recset_header);
		}
	      goto exit_on_error;
	    }
	}			/* for */

      log_append_undoredo_data2 (thread_p, RVBT_INS_PGRECORDS,
				 &btid->sys_btid->vfid, right_pg, -1,
				 sizeof (RECSET_HEADER), recset_length,
				 &recset_header, recset_data);

      if (!leaf_page)
	{
	  /* move the middle key from parent to the right page */

	  if (spage_get_record (P, left_slotid, &peek_rec1, PEEK) !=
	      S_SUCCESS)
	    {
	      goto exit_on_error;
	    }

	  /* we have to copy the key here because we will soon be writing on P
	   * and our pointer may point to another record
	   */
	  btree_read_record (thread_p, btid, &peek_rec1, &right_key,
			     &junk_rec, false, &clear_rkey, &offset, 1);

	  /* we need to use COPY here instead of PEEK because we are updating
	   * the child page pointer in place.
	   */
	  if (spage_get_record (right_pg, left_cnt, &copy_rec, COPY) !=
	      S_SUCCESS)
	    {
	      goto exit_on_error;
	    }

	  /* log the old node record for undo purposes */
	  *(INT16 *) ((char *) recset_data + OFFS1) = node_type;
	  *(INT16 *) ((char *) recset_data + OFFS2) = copy_rec.type;
	  memcpy ((char *) recset_data + OFFS3, copy_rec.data,
		  copy_rec.length);
	  log_append_undo_data2 (thread_p, RVBT_NDRECORD_UPD,
				 &btid->sys_btid->vfid, right_pg, left_cnt,
				 copy_rec.length + OFFS3, recset_data);

	  btree_read_fixed_portion_of_non_leaf_record (&copy_rec, &NLeaf_Pnt);
	  NonLeaf_Rec.pnt = NLeaf_Pnt.pnt;
	  key_len = btree_get_key_length (&right_key);
	  NonLeaf_Rec.key_len =
	    (key_len < BTREE_MAX_KEYLEN_INPAGE) ? key_len : -1;
	  ret =
	    btree_write_record (thread_p, btid, &NonLeaf_Rec, &right_key,
				leaf_page, (NonLeaf_Rec.key_len == -1),
				key_len, false, NULL, NULL, &copy_rec);
	  if (ret != NO_ERROR)
	    {
	      goto exit_on_error;
	    }

	  if (spage_update (thread_p, right_pg, left_cnt, &copy_rec) !=
	      SP_SUCCESS)
	    {
	      goto exit_on_error;
	    }

	  /* log the new node record for redo purposes */
	  memcpy ((char *) recset_data + OFFS3, copy_rec.data,
		  copy_rec.length);
	  log_append_redo_data2 (thread_p, RVBT_NDRECORD_UPD,
				 &btid->sys_btid->vfid, right_pg, left_cnt,
				 copy_rec.length + OFFS3, recset_data);
	}

      /* get and log the old node record to be deleted for undo purposes */

      if (spage_get_record (P, left_slotid, &peek_rec2, PEEK) != S_SUCCESS)
	{
	  goto exit_on_error;
	}

      btree_read_fixed_portion_of_non_leaf_record (&peek_rec2, &NLeaf_Pnt);

      if ((NLeaf_Pnt.key_len) < 0)
	{			/* overflow key */
	  /* get the overflow manager to delete the key */
	  ret =
	    btree_delete_overflow_key (thread_p, btid, P, left_slotid, false);
	  if (ret != NO_ERROR)
	    {
	      goto exit_on_error;
	    }
	}

      *(INT16 *) ((char *) recset_data + OFFS1) = 1;	/* NonLeaf Record */
      *(INT16 *) ((char *) recset_data + OFFS2) = peek_rec2.type;
      memcpy ((char *) recset_data + OFFS3, peek_rec2.data, peek_rec2.length);
      log_append_undoredo_data2 (thread_p, RVBT_NDRECORD_DEL,
				 &btid->sys_btid->vfid, P, left_slotid,
				 peek_rec2.length + OFFS3,
				 sizeof (left_slotid), recset_data,
				 &left_slotid);

      /* update parent page P */
      if (spage_delete (thread_p, P, left_slotid) != left_slotid)
	{
	  goto exit_on_error;
	}

      /* get left page header information */
      btree_get_header_ptr (left_pg, &header_ptr);
      max_key = BTREE_GET_NODE_MAX_KEY_LEN (header_ptr);

      /* update right page header
       */
      if (spage_get_record (right_pg, HEADER, &peek_rec, PEEK) != S_SUCCESS)
	{
	  goto exit_on_error;
	}

      /* log the old header record for undo purposes */
      log_append_undo_data2 (thread_p, RVBT_NDHEADER_UPD,
			     &btid->sys_btid->vfid, right_pg, HEADER,
			     peek_rec.length, peek_rec.data);

      /* The key count already incorporates the semantics of the
       * non leaf/leaf key count difference (non leaf key count is one less
       * than the actual key count). We simply increment the key count by the
       * actual number of keys we've added to the right page.
       */
      key_cnt = BTREE_GET_NODE_KEY_CNT (peek_rec.data);
      key_cnt += left_cnt;
      BTREE_PUT_NODE_KEY_CNT (peek_rec.data, key_cnt);
      if (max_key > BTREE_GET_NODE_MAX_KEY_LEN (peek_rec.data))
	{
	  BTREE_PUT_NODE_MAX_KEY_LEN (peek_rec.data, max_key);
	}

      /* log the new header record for redo purposes */
      log_append_redo_data2 (thread_p, RVBT_NDHEADER_UPD,
			     &btid->sys_btid->vfid, right_pg, HEADER,
			     peek_rec.length, peek_rec.data);

      *child_vpid = right_vpid;

      pgbuf_set_dirty (thread_p, right_pg, DONT_FREE);

      /* increment lsa of the page to be deallocated */
      LSA_COPY (&temp_lsa, pgbuf_get_lsa (left_pg));
      temp_lsa.offset++;
      pgbuf_set_lsa (thread_p, left_pg, &temp_lsa);
      pgbuf_set_dirty (thread_p, left_pg, DONT_FREE);

    }

  /* update parent page header, use COPY here instead of PEEK because we
   * are updateing the child pointer in place.
   */
  if (spage_get_record (P, HEADER, &peek_rec, PEEK) != S_SUCCESS)
    {
      goto exit_on_error;
    }

  /* log the old header record for undo purposes */
  log_append_undo_data2 (thread_p, RVBT_NDHEADER_UPD, &btid->sys_btid->vfid,
			 P, HEADER, peek_rec.length, peek_rec.data);

  key_cnt = BTREE_GET_NODE_KEY_CNT (peek_rec.data);
  key_cnt--;
  BTREE_PUT_NODE_KEY_CNT (peek_rec.data, key_cnt);

  /* log the new header record for redo purposes */
  log_append_redo_data2 (thread_p, RVBT_NDHEADER_UPD, &btid->sys_btid->vfid,
			 P, HEADER, peek_rec.length, peek_rec.data);

  pgbuf_set_dirty (thread_p, P, DONT_FREE);
  btree_clear_key_value (&clear_lkey, &left_key);
  btree_clear_key_value (&clear_rkey, &right_key);
  free_and_init (copy_rec.data);
  free_and_init (recset_data);

end:

  return ret;

exit_on_error:

  if (recset_data)
    {
      free_and_init (recset_data);
    }
  if (copy_rec.data)
    {
      free_and_init (copy_rec.data);
    }

  btree_clear_key_value (&clear_lkey, &left_key);
  btree_clear_key_value (&clear_rkey, &right_key);

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
 * btree_delete () -
 *   return: (the specified key on succes, or NULL on failure)
 *   btid(in): B+tree index identifier
 *   key(in): Key from which the specified value will be deleted
 *   cls_oid(in):
 *   oid(in): Object identifier to be deleted
 *   unique(in):
 *   op_type(in):
 *   unique_stat_info(in):
 *
 *
 * Note: Deletes the value 'oid' from the specified key 'key' in the
 * B+tree index structure. If the deletion results in the
 * removal of the last value of the key, then the key itself is
 * deleted. If the specified key or the value does not exist in
 * the index, then corresponding error codes are set.
 *
 * The page merge operations are done while traversing the tree
 * from top to bottom to eliminate the upward propagation of the
 * page merge operations that may cause deadlocks. While
 * accessing an index page P (holding an exclusive X lock on it)
 * the index manager determines if a pair of child pages of P,
 * the next page to be accessed and its left or right sibling,
 * can be merged. If so, it moves entries from one to another.
 * The page that becomes empty is deallocated and parent page P
 * is updated. If a non_leaf page has only two child pages that
 * can be merged, the merge operation results in a non_leaf page
 * which has no keys and only one child page pointer. On the
 * other hand, if the last key from a leaf page is deleted, the
 * page becomes empty, too. Both type of pages are removed in
 * subsequent deletion and insertion operations. The leaf pages
 * that contain one "big" record, possibly with several overflow
 * pages, cannot be merged with their sibling pages until the
 * "big" record that they contain becomes a "small" record with
 * successive delete operations.
 */
DB_VALUE *
btree_delete (THREAD_ENTRY * thread_p, BTID * btid, DB_VALUE * key,
	      OID * cls_oid, OID * oid, int *unique, int op_type,
	      BTREE_UNIQUE_STATS * unique_stat_info)
{
  char *header_ptr;
  INT16 key_cnt;
  VPID next_vpid;
  VPID P_vpid, Q_vpid, R_vpid, child_vpid, Left_vpid, Right_vpid;
  PAGE_PTR P = NULL, Q = NULL, R = NULL, Left = NULL, Right = NULL;
  RECDES peek_recdes1, peek_recdes2, copy_rec, copy_rec1;
  BTREE_ROOT_HEADER root_header;
  int offset;
  int Q_Used, L_Used, R_Used;
  NON_LEAF_REC NLeaf_Pnt;
  INT16 p_slot_id, last_rec;
  int top_op_active = 0;
  bool clear_key;
  bool leaf_page;
  int merged, qEmpty, rEmpty, lEmpty, del_key;
  DB_VALUE mid_key;
  BTID_INT btid_int;
  int NextPageFlag = false;
  int NextLockFlag = false;
  OID class_oid;
  LOCK class_lock = NULL_LOCK;
  int tran_index;
  int nextkey_lock_request;
  int ret_val;
  INT16 nSlot_id;
  LEAF_REC Leaf_Pnt;
  PAGE_PTR N = NULL;
  VPID N_vpid;
  OID N_oid, Saved_N_oid;
  char *rec_oid_ptr;
  LOG_LSA Saved_pLSA, Saved_nLSA;
  LOG_LSA *temp_lsa;
  OID N_class_oid;
  OID Saved_N_class_oid;
  char *err_key;
  PAGE_PTR temp_page = NULL;

  copy_rec.data = NULL;
  copy_rec1.data = NULL;

#if defined(BTREE_DEBUG)
  if (BTREE_INVALID_INDEX_ID (btid))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_BTREE_INVALID_INDEX_ID, 3,
	      btid->vfid.fileid, btid->vfid.volid, btid->root_pageid);
      goto error;
    }
#endif /* DB_DEBUG */

  P_vpid.volid = btid->vfid.volid;	/* read the root page */
  P_vpid.pageid = btid->root_pageid;

  P = pgbuf_fix (thread_p, &P_vpid, OLD_PAGE, PGBUF_LATCH_WRITE,
		 PGBUF_UNCONDITIONAL_LATCH);
  if (P == NULL
      || (spage_get_record (P, HEADER, &peek_recdes1, PEEK) != S_SUCCESS))
    {
      goto error;
    }

  btree_read_root_header (&peek_recdes1, &root_header);
  btid_int.sys_btid = btid;
  if (btree_glean_root_header_info (&root_header, &btid_int) != NO_ERROR)
    {
      goto error;
    }

  leaf_page = (root_header.node.node_type == LEAF_NODE) ? true : false;

  *unique = btid_int.unique;

  if (key && DB_VALUE_DOMAIN_TYPE (key) == DB_TYPE_MIDXKEY)
    {
      key->data.midxkey.domain = btid_int.key_type;	/* set complete setdomain */
    }

  if (key == NULL || db_value_is_null (key)
      || btree_multicol_key_is_null (key))
    {
      /* update root header statistics if it's a Btree for uniques.
       * this only wants to be done if the transaction is active.  that
       * is, if we are aborting a transaction the statistics are "rolled
       * back by their own logging.
       *
       * unique statitistics for non null keys will be updated after we
       * find out if we have deleted a key or not.  */
      if (logtb_is_current_active (thread_p) && BTREE_IS_UNIQUE (&btid_int))
	{
	  if (op_type == SINGLE_ROW_DELETE || op_type == SINGLE_ROW_UPDATE ||
	      op_type == SINGLE_ROW_MODIFY)
	    {
	      /* single instance will be deleted. Update global statistics directly. */
	      root_header.num_nulls--;
	      root_header.num_oids--;
	      copy_rec.area_size = DB_PAGESIZE;
	      copy_rec1.area_size = DB_PAGESIZE;
	      copy_rec.data = (char *) malloc (DB_PAGESIZE);
	      if (copy_rec.data == NULL)
		{
		  goto error;
		}
	      copy_rec1.data = (char *) malloc (DB_PAGESIZE);
	      if (copy_rec1.data == NULL)
		{
		  goto error;
		}

	      /* save root head for undo purposes */
	      btree_rv_save_root_head (root_header.node.max_key_len, 1, 1, 0,
				       &copy_rec1);

	      /* update the root header */
	      btree_write_root_header (&copy_rec, &root_header);

	      log_append_undoredo_data2 (thread_p, RVBT_ROOTHEADER_UPD,
					 &btid->vfid, P, HEADER,
					 copy_rec1.length, copy_rec.length,
					 copy_rec1.data, copy_rec.data);

	      if (spage_update (thread_p, P, HEADER, &copy_rec) != SP_SUCCESS)
		{
		  goto error;
		}

	      pgbuf_set_dirty (thread_p, P, DONT_FREE);
	      free_and_init (copy_rec.data);
	      free_and_init (copy_rec1.data);
	    }
	  else
	    {
	      /* Multiple instances will be deleted.  Update local statistics. */
	      if (unique_stat_info == NULL)
		{
		  goto error;
		}
	      unique_stat_info->num_nulls--;
	      unique_stat_info->num_oids--;
	    }
	}

      /* nothing more to do -- this is not an error */
      pgbuf_unfix (thread_p, P);
      P = NULL;
      return key;
    }

  /* Decide whether key range locking must be performed.
   * If class_oid is transferred through a new argument,
   * this operation will be performed more efficiently.
   */
  if (cls_oid != NULL && !OID_ISNULL (cls_oid))
    {
      /* cls_oid can be NULL_OID in case of non-unique index.
       * But it does not make problem.
       */
      COPY_OID (&class_oid, cls_oid);
    }
  else
    {
      if (logtb_is_current_active (thread_p) == true)
	{
	  if (heap_get_class_oid (thread_p, oid, &class_oid) == NULL)
	    {
	      goto error;
	      /* nextkey_lock_request = true; goto start_point; */
	    }
	}
      else
	{
	  OID_SET_NULL (&class_oid);
	}
    }

  if (logtb_is_current_active (thread_p) == true)
    {
      /* initialize Saved_N_oid */
      OID_SET_NULL (&Saved_N_oid);
      OID_SET_NULL (&Saved_N_class_oid);

      /* Find the lock that is currently acquired on the class */
      tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
      class_lock = lock_get_object_lock (&class_oid, oid_Root_class_oid,
					 tran_index);

      /* get nextkey_lock_request from the class lock mode */
      switch (class_lock)
	{
	case X_LOCK:
	case SIX_LOCK:
	case IX_LOCK:
	  nextkey_lock_request = true;
	  break;
	case S_LOCK:
	case IS_LOCK:
	case NULL_LOCK:
	default:
	  goto error;
	}
      if (!BTREE_IS_UNIQUE (&btid_int))
	{			/* non-unique index */
	  if (class_lock == X_LOCK)
	    {
	      nextkey_lock_request = false;
	    }
	  else
	    {
	      nextkey_lock_request = true;
	    }
	}
    }
  else
    {
      /* total rollback, partial rollback, undo phase in recovery */
      nextkey_lock_request = false;
    }

  COPY_OID (&N_class_oid, &class_oid);

start_point:

  if (NextLockFlag == true)
    {

      P_vpid.volid = btid->vfid.volid;	/* read the root page */
      P_vpid.pageid = btid->root_pageid;
      P = pgbuf_fix (thread_p, &P_vpid, OLD_PAGE, PGBUF_LATCH_WRITE,
		     PGBUF_UNCONDITIONAL_LATCH);
      if (P == NULL
	  || (spage_get_record (P, HEADER, &peek_recdes1, PEEK) != S_SUCCESS))
	{
	  goto error;
	}

      btree_read_root_header (&peek_recdes1, &root_header);
      btid_int.sys_btid = btid;
      if (btree_glean_root_header_info (&root_header, &btid_int) != NO_ERROR)
	{
	  goto error;
	}

      leaf_page = (root_header.node.node_type == LEAF_NODE) ? true : false;

    }

  if (!leaf_page && (root_header.node.key_cnt == 1))
    {				/* root merge may be needed */

      /* read the first record */
      if (spage_get_record (P, 1, &peek_recdes1, PEEK) != S_SUCCESS)
	{
	  goto error;
	}

      btree_read_record (thread_p, &btid_int, &peek_recdes1, &mid_key,
			 &NLeaf_Pnt, leaf_page, &clear_key, &offset, 0);
      Q_vpid = NLeaf_Pnt.pnt;

      Q = pgbuf_fix (thread_p, &Q_vpid, OLD_PAGE, PGBUF_LATCH_WRITE,
		     PGBUF_UNCONDITIONAL_LATCH);
      if (Q == NULL)
	{
	  goto error;
	}

      Q_Used = DB_PAGESIZE - spage_get_free_space (thread_p, Q);
      qEmpty = (spage_number_of_records (Q) == 1);

      btree_get_header_ptr (Q, &header_ptr);
      leaf_page =
	(BTREE_GET_NODE_TYPE (header_ptr) == LEAF_NODE) ? true : false;

      /* read the second record */
      if (spage_get_record (P, 2, &peek_recdes2, PEEK) != S_SUCCESS)
	{
	  goto error;
	}

      btree_read_fixed_portion_of_non_leaf_record (&peek_recdes2, &NLeaf_Pnt);
      R_vpid = NLeaf_Pnt.pnt;
      R = pgbuf_fix (thread_p, &R_vpid, OLD_PAGE, PGBUF_LATCH_WRITE,
		     PGBUF_UNCONDITIONAL_LATCH);
      if (R == NULL)
	{
	  goto error;
	}

      rEmpty = (spage_number_of_records (R) == 1);
      R_Used = DB_PAGESIZE - spage_get_free_space (thread_p, R);

      /* we need to take into consideration the largest key size since the
       * merge will use a key from the root page as the mid_key.  It may be
       * longer than any key on Q or R.  This is a little bit overly
       * pessimistic, which is probably not bad for root merges.
       */
      if (((Q_Used + R_Used + FIXED_EMPTY + root_header.node.max_key_len) <
	   DB_PAGESIZE))
	{
	  /* root merge possible */

	  /* Start system permanent operation */
	  log_start_system_op (thread_p);
	  top_op_active = 1;

	  if (btree_merge_root
	      (thread_p, &btid_int, P, Q, R, &P_vpid, &Q_vpid, &R_vpid,
	       leaf_page) != NO_ERROR)
	    {
	      goto error;
	    }

	  pgbuf_unfix (thread_p, Q);
	  Q = NULL;
	  if (file_dealloc_page (thread_p, &btid->vfid, &Q_vpid) != NO_ERROR)
	    {
	      pgbuf_unfix (thread_p, R);
	      R = NULL;
	      goto error;
	    }
	  pgbuf_unfix (thread_p, R);
	  R = NULL;
	  if (file_dealloc_page (thread_p, &btid->vfid, &R_vpid) != NO_ERROR)
	    {
	      goto error;
	    }

	  if (file_new_isvalid (thread_p, &btid->vfid) == DISK_VALID)
	    {			/* New B+tree ? */
	      log_end_system_op (thread_p, LOG_RESULT_TOPOP_ATTACH_TO_OUTER);
	    }
	  else
	    {
	      log_end_system_op (thread_p, LOG_RESULT_TOPOP_COMMIT);
	    }

	  top_op_active = 0;

	}
      else
	{			/* merge not possible */
	  int c;

	  c = (*(btid_int.key_type->type->cmpval)) (key, &mid_key,
						    btid_int.key_type,
						    btid_int.reverse, 0, 1,
						    NULL);

	  if (c <= 0)
	    {
	      /* choose left child */
	      pgbuf_unfix (thread_p, R);
	      R = NULL;
	    }
	  else
	    {			/* choose right child */
	      pgbuf_unfix (thread_p, Q);
	      Q = R;
	      R = NULL;
	      Q_vpid = R_vpid;
	    }

	  pgbuf_unfix (thread_p, P);
	  P = Q;
	  Q = NULL;
	  P_vpid = Q_vpid;
	}

      btree_clear_key_value (&clear_key, &mid_key);
    }

  while (!leaf_page)		/* non-leaf */
    {
      /* find and get the child page to be followed */
      if (btree_search_nonleaf_page
	  (thread_p, &btid_int, P, key, &p_slot_id, &Q_vpid) != NO_ERROR)
	{
	  goto error;
	}

      Q = pgbuf_fix (thread_p, &Q_vpid, OLD_PAGE, PGBUF_LATCH_WRITE,
		     PGBUF_UNCONDITIONAL_LATCH);
      if (Q == NULL)
	{
	  goto error;
	}

      merged = 0;
      last_rec = spage_number_of_records (P) - 1;
      Q_Used = DB_PAGESIZE - spage_get_free_space (thread_p, Q);

      /* read the header record */
      btree_get_header_ptr (Q, &header_ptr);
      leaf_page =
	(BTREE_GET_NODE_TYPE (header_ptr) == LEAF_NODE) ? true : false;
      qEmpty = (spage_number_of_records (Q) == 1);

      if (p_slot_id < last_rec)
	{			/* right merge */

	  if (spage_get_record (P, p_slot_id + 1, &peek_recdes1, PEEK) !=
	      S_SUCCESS)
	    {
	      goto error;
	    }

	  btree_read_fixed_portion_of_non_leaf_record (&peek_recdes1,
						       &NLeaf_Pnt);
	  Right_vpid = NLeaf_Pnt.pnt;
	  Right = pgbuf_fix (thread_p, &Right_vpid, OLD_PAGE,
			     PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
	  if (Right == NULL)
	    {
	      goto error;
	    }

	  R_Used = DB_PAGESIZE - spage_get_free_space (thread_p, Right);
	  rEmpty = (spage_number_of_records (Right) == 1);

	  if (((Q_Used + R_Used + FIXED_EMPTY) < DB_PAGESIZE) ||
	      (leaf_page && (qEmpty || rEmpty)))
	    {			/* right merge possible */

	      /* start system permanent operation */
	      log_start_system_op (thread_p);
	      top_op_active = 1;

	      if (btree_merge_node
		  (thread_p, &btid_int, P, Q, Right, &P_vpid, &Q_vpid,
		   &Right_vpid, p_slot_id, leaf_page, RIGHT_MERGE,
		   &child_vpid) != NO_ERROR)
		{
		  goto error;
		}
	      merged = 1;

	      if (VPID_EQ (&child_vpid, &Q_vpid))
		{
		  /* child page to be followed is Q */
		  pgbuf_unfix (thread_p, Right);
		  Right = NULL;
		  if (file_dealloc_page (thread_p, &btid->vfid, &Right_vpid)
		      != NO_ERROR)
		    {
		      goto error;
		    }

		  if (file_new_isvalid (thread_p, &btid->vfid) == DISK_VALID)
		    {		/* New B+tree ? */
		      log_end_system_op (thread_p,
					 LOG_RESULT_TOPOP_ATTACH_TO_OUTER);
		    }
		  else
		    {
		      log_end_system_op (thread_p, LOG_RESULT_TOPOP_COMMIT);
		    }

		  top_op_active = 0;

		}
	      else if (VPID_EQ (&child_vpid, &Right_vpid))
		{
		  /* child page to be followed is Right */
		  pgbuf_unfix (thread_p, Q);
		  Q = NULL;
		  if (file_dealloc_page (thread_p, &btid->vfid, &Q_vpid) !=
		      NO_ERROR)
		    {
		      goto error;
		    }

		  if (file_new_isvalid (thread_p, &btid->vfid) == DISK_VALID)
		    {		/* New B+tree ? */
		      log_end_system_op (thread_p,
					 LOG_RESULT_TOPOP_ATTACH_TO_OUTER);
		    }
		  else
		    {
		      log_end_system_op (thread_p, LOG_RESULT_TOPOP_COMMIT);
		    }

		  top_op_active = 0;

		  Q = Right;
		  Right = NULL;
		  Q_vpid = Right_vpid;
		}
	      else
		{
		  pgbuf_unfix (thread_p, P);
		  P = NULL;
		  pgbuf_unfix (thread_p, Q);
		  Q = NULL;
		  pgbuf_unfix (thread_p, Right);
		  Right = NULL;
		  log_end_system_op (thread_p, LOG_RESULT_TOPOP_ABORT);
		  top_op_active = 0;
		  return NULL;
		}

	    }
	  else
	    {			/* not merged */
	      pgbuf_unfix (thread_p, Right);
	      Right = NULL;
	    }

	}

      if (!merged && (p_slot_id > 1))
	{			/* left sibling accesible */

	  if (spage_get_record (P, p_slot_id - 1, &peek_recdes1, PEEK) !=
	      S_SUCCESS)
	    {
	      goto error;
	    }

	  btree_read_fixed_portion_of_non_leaf_record (&peek_recdes1,
						       &NLeaf_Pnt);
	  Left_vpid = NLeaf_Pnt.pnt;

	  /* try to fix left page conditionally */
	  Left = pgbuf_fix (thread_p, &Left_vpid, OLD_PAGE, PGBUF_LATCH_WRITE,
			    PGBUF_CONDITIONAL_LATCH);
	  if (Left == NULL)
	    {
	      /* unfix Q page */
	      pgbuf_unfix (thread_p, Q);
	      Q = NULL;

	      Left = pgbuf_fix (thread_p, &Left_vpid, OLD_PAGE,
				PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
	      if (Left == NULL)
		{
		  goto error;
		}

	      Q = pgbuf_fix (thread_p, &Q_vpid, OLD_PAGE, PGBUF_LATCH_WRITE,
			     PGBUF_UNCONDITIONAL_LATCH);
	      if (Q == NULL)
		{
		  goto error;
		}
	      /* follow code may not be required */
	      Q_Used = DB_PAGESIZE - spage_get_free_space (thread_p, Q);
	      qEmpty = (spage_number_of_records (Q) == 1);
	    }

	  L_Used = DB_PAGESIZE - spage_get_free_space (thread_p, Left);
	  lEmpty = (spage_number_of_records (Left) == 1);

	  if (((Q_Used + L_Used + FIXED_EMPTY) < DB_PAGESIZE) ||
	      (leaf_page && (qEmpty || lEmpty)))
	    {			/* left merge possible */

	      /* Start system permanent operation */
	      log_start_system_op (thread_p);
	      top_op_active = 1;

	      if (btree_merge_node
		  (thread_p, &btid_int, P, Q, Left, &P_vpid, &Q_vpid,
		   &Left_vpid, p_slot_id, leaf_page, LEFT_MERGE,
		   &child_vpid) != NO_ERROR)
		{
		  goto error;
		}
	      merged = 1;

	      if (VPID_EQ (&child_vpid, &Q_vpid))
		{
		  /* child page to be followed is Q */
		  pgbuf_unfix (thread_p, Left);
		  Left = NULL;
		  if (file_dealloc_page (thread_p, &btid->vfid, &Left_vpid) !=
		      NO_ERROR)
		    {
		      goto error;
		    }

		  if (file_new_isvalid (thread_p, &btid->vfid) == DISK_VALID)
		    {		/* New B+tree ? */
		      log_end_system_op (thread_p,
					 LOG_RESULT_TOPOP_ATTACH_TO_OUTER);
		    }
		  else
		    {
		      log_end_system_op (thread_p, LOG_RESULT_TOPOP_COMMIT);
		    }

		  top_op_active = 0;

		}
	      else if (VPID_EQ (&child_vpid, &Left_vpid))
		{
		  /* child page to be followed is Left */
		  pgbuf_unfix (thread_p, Q);
		  Q = NULL;
		  if (file_dealloc_page (thread_p, &btid->vfid, &Q_vpid) !=
		      NO_ERROR)
		    {
		      goto error;
		    }

		  if (file_new_isvalid (thread_p, &btid->vfid) == DISK_VALID)
		    {		/* New B+tree ? */
		      log_end_system_op (thread_p,
					 LOG_RESULT_TOPOP_ATTACH_TO_OUTER);
		    }
		  else
		    {
		      log_end_system_op (thread_p, LOG_RESULT_TOPOP_COMMIT);
		    }

		  top_op_active = 0;

		  Q = Left;
		  Left = NULL;
		  Q_vpid = Left_vpid;
		}
	      else
		{
		  pgbuf_unfix (thread_p, P);
		  P = NULL;
		  pgbuf_unfix (thread_p, Q);
		  Q = NULL;
		  pgbuf_unfix (thread_p, Left);
		  Left = NULL;
		  log_end_system_op (thread_p, LOG_RESULT_TOPOP_ABORT);
		  top_op_active = 0;
		  return NULL;
		}

	    }
	  else
	    {			/* not merged */
	      pgbuf_unfix (thread_p, Left);
	      Left = NULL;
	    }

	}

      pgbuf_unfix (thread_p, P);
      P = Q;
      Q = NULL;
      P_vpid = Q_vpid;
    }

  /* keys(the number of keyvals) of the leaf page is valid */

  if (nextkey_lock_request == false)
    {
      goto key_deletion;
    }

  /* save node info. of the leaf page 
   * Header must be calculated again. 
   * because, SMO might have been occurred.
   */
  btree_get_header_ptr (P, &header_ptr);
  key_cnt = BTREE_GET_NODE_KEY_CNT (header_ptr);
  BTREE_GET_NODE_NEXT_VPID (header_ptr, &next_vpid);

  /* find next key */
  if (btree_search_leaf_page (thread_p, &btid_int, P, key, &p_slot_id))
    {
      /* key has been found */
      if (p_slot_id == key_cnt)
	{
	  nSlot_id = 1;
	  NextPageFlag = true;
	}
      else
	{
	  nSlot_id = p_slot_id + 1;
	}
    }
  else
    {
      /* key has not been found
       * we can get this situation in the following senario:
       * We are recovering from a crash of a btree_insert() where
       * the key/value insert undo log was written and flushed to disk
       * but the OID insertion redo log was not flushed to disk.
       * this is the logging undo/redo coupling problem.  for now,
       * we will make this a warning severity.
       *
       * when this logging hole gets filled, we should remove the
       * warning severity.
       *
       * we put a NOOP redo log here, which does NOTHING, this is used
       * to accompany the corresponding logical undo log, if there is
       * any, which caused this routine to be called.
       */
      log_append_redo_data2 (thread_p, RVBT_NOOP, &btid->vfid, P, -1, 0,
			     NULL);
      pgbuf_set_dirty (thread_p, P, DONT_FREE);
      err_key = pr_valstring (key);

      er_set ((log_is_in_crash_recovery ())? ER_WARNING_SEVERITY :
	      ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_BTREE_UNKNOWN_KEY, 5,
	      (err_key) ? err_key : "_NULL_KEY",
	      btid_int.sys_btid->vfid.fileid, btid_int.sys_btid->vfid.volid,
	      btid_int.sys_btid->root_pageid,
	      PR_TYPE_FROM_ID (btid_int.key_type->type->id)->name);
      er_log_debug (ARG_FILE_LINE,
		    "btree_delete_from_leaf: btree_search_leaf_page fails, "
		    "next key not found.");
      if (err_key)
	{
	  free_and_init (err_key);
	}

      goto error;
    }

  /* get the next OID */
  if (NextPageFlag == true)
    {
      /* the next key exists in the next leaf page */
      N_vpid = next_vpid;

    get_next_oid:

      if (VPID_ISNULL (&N_vpid))
	{			/* next page does not exist */
	  NextPageFlag = false;	/* reset NextPageFlag */
	  /* the first entry of the root page is used as the next OID */
	  N_oid.volid = btid->vfid.volid;
	  N_oid.pageid = btid->root_pageid;
	  N_oid.slotid = -1;
	  N_class_oid.volid = btid->vfid.volid;
	  N_class_oid.pageid = btid->root_pageid;
	  N_class_oid.slotid = 0;
	  if (temp_page != NULL)
	    {
	      pgbuf_unfix (thread_p, temp_page);
	      temp_page = NULL;
	    }
	}
      else
	{			/* next page exists */
	  N = pgbuf_fix (thread_p, &N_vpid, OLD_PAGE, PGBUF_LATCH_READ,
			 PGBUF_UNCONDITIONAL_LATCH);
	  if (N == NULL)
	    {
	      if (temp_page != NULL)
		{
		  pgbuf_unfix (thread_p, temp_page);
		  temp_page = NULL;
		}
	      goto error;
	    }
	  if (temp_page != NULL)
	    {
	      pgbuf_unfix (thread_p, temp_page);
	      temp_page = NULL;
	    }
	  if (spage_number_of_records (N) == 1)
	    {			/* empty leaf page */
	      btree_get_header_ptr (N, &header_ptr);
	      BTREE_GET_NODE_NEXT_VPID (header_ptr, &N_vpid);

	      temp_page = N;
	      goto get_next_oid;
	    }
	  if (spage_get_record (N, nSlot_id, &peek_recdes1, PEEK) !=
	      S_SUCCESS)
	    {
	      goto error;
	    }
	  btree_read_record (thread_p, &btid_int, &peek_recdes1, NULL,
			     &Leaf_Pnt, true, &clear_key, &offset, 0);
	  rec_oid_ptr = peek_recdes1.data + offset;
	  if (BTREE_IS_UNIQUE (&btid_int))
	    {			/* unique index */
	      OR_GET_OID (rec_oid_ptr, &N_class_oid);
	      rec_oid_ptr += OR_OID_SIZE;
	      OR_GET_OID (rec_oid_ptr, &N_oid);
	      if (OID_EQ (&N_class_oid, &class_oid) && class_lock == X_LOCK)
		{
		  if (NextLockFlag == true)
		    {
		      (void) lock_unlock_object (thread_p, &Saved_N_oid,
						 &Saved_N_class_oid, NX_LOCK,
						 true);
		      NextLockFlag = false;
		      OID_SET_NULL (&Saved_N_oid);
		      OID_SET_NULL (&Saved_N_class_oid);
		    }
		  pgbuf_unfix (thread_p, N);
		  N = NULL;
		  goto key_deletion;
		}
	    }
	  else
	    {			/* non-unique index */
	      COPY_OID (&N_class_oid, &class_oid);
	      OR_GET_OID (rec_oid_ptr, &N_oid);
	    }
	}
    }
  else
    {				/* NextPageFlag == false */
      if (spage_get_record (P, nSlot_id, &peek_recdes1, PEEK) != S_SUCCESS)
	{
	  goto error;
	}
      btree_read_record (thread_p, &btid_int, &peek_recdes1, NULL, &Leaf_Pnt,
			 true, &clear_key, &offset, 0);
      rec_oid_ptr = peek_recdes1.data + offset;
      if (BTREE_IS_UNIQUE (&btid_int))
	{			/* unique index */
	  OR_GET_OID (rec_oid_ptr, &N_class_oid);
	  rec_oid_ptr += OR_OID_SIZE;
	  OR_GET_OID (rec_oid_ptr, &N_oid);
	  if (OID_EQ (&N_class_oid, &class_oid) && class_lock == X_LOCK)
	    {
	      if (NextLockFlag == true)
		{
		  (void) lock_unlock_object (thread_p, &Saved_N_oid,
					     &Saved_N_class_oid, NX_LOCK,
					     true);
		  NextLockFlag = false;
		  OID_SET_NULL (&Saved_N_oid);
		  OID_SET_NULL (&Saved_N_class_oid);
		}
	      goto key_deletion;
	    }
	}
      else
	{			/* non-unique index */
	  COPY_OID (&N_class_oid, &class_oid);
	  OR_GET_OID (rec_oid_ptr, &N_oid);
	}
    }

  if (NextLockFlag == true)
    {
      if (OID_EQ (&Saved_N_oid, &N_oid))
	{
	  if (NextPageFlag == true)
	    {
	      pgbuf_unfix (thread_p, N);
	      N = NULL;
	    }
	  goto key_deletion;
	}
      (void) lock_unlock_object (thread_p, &Saved_N_oid, &Saved_N_class_oid,
				 NX_LOCK, true);
      NextLockFlag = false;
      OID_SET_NULL (&Saved_N_oid);
      OID_SET_NULL (&Saved_N_class_oid);
    }

  /* CONDITIONAL lock request */
  ret_val =
    lock_object (thread_p, &N_oid, &N_class_oid, NX_LOCK, LK_COND_LOCK);

  if (ret_val == LK_GRANTED)
    {
      NextLockFlag = true;
      if (NextPageFlag == true)
	{
	  pgbuf_unfix (thread_p, N);
	  N = NULL;
	}
    }
  else if (ret_val == LK_NOTGRANTED_DUE_TIMEOUT)
    {
      /* save some information for validation checking
       * after UNCONDITIONAL lock request
       */

      temp_lsa = pgbuf_get_lsa (P);
      LSA_COPY (&Saved_pLSA, temp_lsa);
      pgbuf_unfix (thread_p, P);
      P = NULL;
      if (NextPageFlag == true)
	{
	  temp_lsa = pgbuf_get_lsa (N);
	  LSA_COPY (&Saved_nLSA, temp_lsa);
	  pgbuf_unfix (thread_p, N);
	  N = NULL;
	}
      COPY_OID (&Saved_N_oid, &N_oid);
      COPY_OID (&Saved_N_class_oid, &N_class_oid);

      /* UNCONDITIONAL lock request */
      ret_val =
	lock_object (thread_p, &N_oid, &N_class_oid, NX_LOCK, LK_UNCOND_LOCK);
      if (ret_val != LK_GRANTED)
	{
	  goto error;
	}
      NextLockFlag = true;

      /* Validation checking after the unconditional lock acquisition
       * In this implementation, only PageLSA of the page is checked.
       * It means that if the PageLSA has not been changed,
       * the page image does not changed
       * during the unconditional next key lock acquisition.
       * So, the next lock that is acquired is valid.
       * If we give more accurate and precise checking condition,
       * the operation that traverse the tree can be reduced.
       */
      P = pgbuf_fix (thread_p, &P_vpid, OLD_PAGE, PGBUF_LATCH_WRITE,
		     PGBUF_UNCONDITIONAL_LATCH);
      if (P == NULL)
	{
	  goto error;
	}

      temp_lsa = pgbuf_get_lsa (P);
      if (!LSA_EQ (&Saved_pLSA, temp_lsa))
	{
	  pgbuf_unfix (thread_p, P);
	  P = NULL;
	  NextPageFlag = false;
	  goto start_point;
	}

      /* the first leaf page is valid */
      if (NextPageFlag == true)
	{
	  N = pgbuf_fix (thread_p, &N_vpid, OLD_PAGE, PGBUF_LATCH_READ,
			 PGBUF_UNCONDITIONAL_LATCH);
	  if (N == NULL)
	    {
	      goto error;
	    }

	  temp_lsa = pgbuf_get_lsa (N);
	  if (!LSA_EQ (&Saved_nLSA, temp_lsa))
	    {
	      pgbuf_unfix (thread_p, P);
	      P = NULL;
	      pgbuf_unfix (thread_p, N);
	      N = NULL;
	      NextPageFlag = false;
	      goto start_point;
	    }
	  /* the next leaf page is valid */

	  pgbuf_unfix (thread_p, N);
	  N = NULL;
	}

      /* only page P is currently locked and fetched */
    }
  else
    {
      goto error;
    }

key_deletion:

  /* a leaf page is reached, perform the deletion */
  del_key = 0;
  if (btree_delete_from_leaf
      (thread_p, &btid_int, &P_vpid, key, &class_oid, oid,
       &del_key) != NO_ERROR)
    {
      goto error;
    }

  pgbuf_unfix (thread_p, P);
  P = NULL;

  /* update root header statistics if it is a Btree for uniques.
   * this will happen only when the transaction is active.
   * that is, if we are aborting a transaction the statistics are "rolled back"
   * by their own logging.
   */
  if (logtb_is_current_active (thread_p) && BTREE_IS_UNIQUE (&btid_int))
    {
      if (op_type == SINGLE_ROW_DELETE || op_type == SINGLE_ROW_UPDATE ||
	  op_type == SINGLE_ROW_MODIFY)
	{
	  /* single instance will be deleted.
	   * therefore, update global statistical information directly.
	   */
	  P_vpid.volid = btid->vfid.volid;
	  P_vpid.pageid = btid->root_pageid;
	  P = pgbuf_fix (thread_p, &P_vpid, OLD_PAGE, PGBUF_LATCH_WRITE,
			 PGBUF_UNCONDITIONAL_LATCH);
	  if (P == NULL)
	    {
	      goto error;
	    }

	  /* read the header record */
	  if (spage_get_record (P, HEADER, &peek_recdes1, PEEK) != S_SUCCESS)
	    {
	      goto error;
	    }

	  btree_read_root_header (&peek_recdes1, &root_header);

	  copy_rec.area_size = DB_PAGESIZE;
	  copy_rec1.area_size = DB_PAGESIZE;
	  copy_rec.data = (char *) malloc (DB_PAGESIZE);
	  if (copy_rec.data == NULL)
	    {
	      goto error;
	    }
	  copy_rec1.data = (char *) malloc (DB_PAGESIZE);
	  if (copy_rec1.data == NULL)
	    {
	      goto error;
	    }

	  /* save root head for undo purposes */
	  btree_rv_save_root_head (root_header.node.max_key_len, 0, 1,
				   del_key, &copy_rec1);

	  /* update the root header */
	  root_header.num_oids--;
	  if (del_key)
	    {
	      root_header.num_keys--;
	    }
	  btree_write_root_header (&copy_rec, &root_header);

	  log_append_undoredo_data2 (thread_p, RVBT_ROOTHEADER_UPD,
				     &btid->vfid, P, HEADER, copy_rec1.length,
				     copy_rec.length, copy_rec1.data,
				     copy_rec.data);

	  if (spage_update (thread_p, P, HEADER, &copy_rec) != SP_SUCCESS)
	    {
	      goto error;
	    }

	  pgbuf_set_dirty (thread_p, P, FREE);
	  P = NULL;
	  free_and_init (copy_rec.data);
	  free_and_init (copy_rec1.data);
	}
      else
	{
	  /* Multiple instances will be deleted.
	   * Therefore, update local statistical information.
	   */
	  if (unique_stat_info == NULL)
	    {
	      goto error;
	    }
	  unique_stat_info->num_oids--;
	  if (del_key)
	    {
	      unique_stat_info->num_keys--;
	    }
	}
    }

  return key;

error:

  if (P)
    {
      pgbuf_unfix (thread_p, P);
      P = NULL;
    }
  if (Q)
    {
      pgbuf_unfix (thread_p, Q);
      Q = NULL;
    }
  if (R)
    {
      pgbuf_unfix (thread_p, R);
      R = NULL;
    }
  if (N)
    {
      pgbuf_unfix (thread_p, N);
      N = NULL;
    }
  /* even if an error occurs,
   * the next-key lock acquired already must not be released.
   * if the next-key lock is to be released,
   * a new key that has same key value with one of deleted key values
   * can be inserted before current transaction aborts the deletion.
   * therefore, uniqueness violation could be occurred.
   */
  if (Left)
    {
      pgbuf_unfix (thread_p, Left);
      Left = NULL;
    }
  if (Right)
    {
      pgbuf_unfix (thread_p, Right);
      Right = NULL;
    }
  if (top_op_active)
    {
      log_end_system_op (thread_p, LOG_RESULT_TOPOP_ABORT);
    }
  if (copy_rec.data)
    {
      free_and_init (copy_rec.data);
    }
  if (copy_rec1.data)
    {
      free_and_init (copy_rec1.data);
    }

  return NULL;
}

/*
 * btree_insert_into_leaf () -
 *   return: NO_ERROR
 *   btid(in): B+tree index identifier
 *   page_ptr(in): Leaf page pointer to which the key is to be inserted
 *   key(in): Key to be inserted
 *   cls_oid(in):
 *   oid(in): Object identifier to be inserted together with the key
 *   nearp_vpid(in): Near page identifier that may be used in allocating a new
 *                   overflow page. (Note: it may be ignored.)
 *   add_key(in):
 *   do_unique_check(in):
 *
 * Note: Insert the given < key, oid > pair into the leaf page
 * specified. If the key is a new one, it assumes that there is
 * enough space in the page to make insertion, otherwise an
 * error condition is raised. If the key is an existing one,
 * inserting "oid" may necessitate the use of overflow pages.
 *
 * LOGGING Note: When the btree is new, splits and merges will
 * not be committed, but will be attached.  If the transaction
 * is rolled back, the merge and split actions will be rolled
 * back as well.  The undo (and redo) logging for splits and
 * merges are page based (physical) logs, thus the rest of the
 * logs for the undo session must be page based as well.  When
 * the btree is old, splits and merges are committed and all
 * the rest of the logging must be logical (non page based)
 * since pages may change as splits and merges are performed.
 */
static int
btree_insert_into_leaf (THREAD_ENTRY * thread_p, BTID_INT * btid,
			PAGE_PTR page_ptr, DB_VALUE * key, OID * cls_oid,
			OID * oid, VPID * nearp_vpid, int *add_key,
			int do_unique_check)
{
  PAGE_PTR ovfp = NULL, newp = NULL;
  VPID ovfl_vpid, new_vpid, p_ovfl_vpid;
  char *header_ptr;
  INT16 key_cnt;
  INT16 slot_id;
  int i;
  OID oid1;
  LEAF_REC LeafRec_Pnt;
  LEAF_REC LeafRec_Node;
  RECDES peek_rec;
  RECDES Rec, oRec;
  char *ptr, *recset_data = NULL;
  int oid_cnt;
  bool dummy;
  INT16 max_free;
  int key_len, offset;
  char *keyvalp;		/* for recovery purposes */
  int keyval_len, recset_length;	/* for recovery purposes */
  RECINS_STRUCT recins;		/* for recovery purposes */
  int oid_size;
#if defined(SERVER_MODE)
  bool old_check_interrupt;
#endif /* SERVER_MODE */
  int ret = NO_ERROR;

#if defined(BTREE_DEBUG)
  if (!key || db_value_is_null (key))
    {
      ret = ER_BTREE_NULL_KEY;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ret, 0);
      goto exit_on_error;
    }
#endif /* DB_DEBUG */

#if defined(SERVER_MODE)
  old_check_interrupt = thread_set_check_interrupt (thread_p, false);
#endif /* SERVER_MODE */

  /* In an unique index, each OID information in leaf entry
   * is composed of <class OID, instance OID>.
   * In a non-unique index, each OID information in leaf entry
   * is composed of <instance OID>.
   */
  if (BTREE_IS_UNIQUE (btid))
    {
      oid_size = 2 * OR_OID_SIZE;	/* <class OID, inst OID> */
    }
  else
    {
      oid_size = OR_OID_SIZE;	/* <inst OID only> */
    }

  /* initializations */
  oRec.data = NULL;
  Rec.type = REC_HOME;
  Rec.area_size = DB_PAGESIZE;
  Rec.data = (char *) malloc (DB_PAGESIZE);
  if (Rec.data == NULL)
    {
      goto exit_on_error;
    }

  if (file_new_isvalid (thread_p, &btid->sys_btid->vfid) == DISK_VALID)
    {
      /* We only need this area if we are doing page level logging
       * (see comment in the header).
       */
      recset_data = (char *) malloc (DB_PAGESIZE);
      if (recset_data == NULL)
	{
	  goto exit_on_error;
	}
    }

  if (BTREE_IS_UNIQUE (btid))
    {				/* unique index */
      recins.class_oid.volid = cls_oid->volid;
      recins.class_oid.pageid = cls_oid->pageid;
      recins.class_oid.slotid = cls_oid->slotid;
    }
  else
    {				/* non-unique index */
      OID_SET_NULL (&recins.class_oid);
    }

  recins.oid.volid = oid->volid;
  recins.oid.pageid = oid->pageid;
  recins.oid.slotid = oid->slotid;
  VPID_SET_NULL (&recins.ovfl_vpid);
  recins.oid_inserted = true;
  recins.ovfl_changed = false;
  recins.new_ovflpg = false;
  recins.rec_type = REGULAR;

  /* get the free space size in page */
  max_free = spage_max_space_for_new_record (thread_p, page_ptr);
  key_len = btree_get_key_length (key);

  if (!btree_search_leaf_page (thread_p, btid, page_ptr, key, &slot_id))
    {
      /* key does not exist */
      *add_key = 1;

      if (slot_id == NULL_SLOTID)
	{
	  goto exit_on_error;
	}

      /* put a LOGICAL log to undo the insertion of <key, oid> pair
       * to the B+tree index. This will be a call to delete this pair
       * from the index. Put this logical log here, because now we know
       * that the <key, oid> pair to be inserted is not already in the index.
       */
      if (file_new_isvalid (thread_p, &btid->sys_btid->vfid) == DISK_INVALID)
	{
	  /* "logical" undo logging needed (see comment in function header). */
	  keyvalp = NULL;
	  ret = btree_rv_save_keyval (btid, key, cls_oid, oid, &keyvalp,
				      &keyval_len);
	  if (ret != NO_ERROR)
	    {
	      goto exit_on_error;
	    }

	  log_append_undo_data2 (thread_p, RVBT_KEYVAL_INS,
				 &btid->sys_btid->vfid, NULL, -1, keyval_len,
				 keyvalp);
	  if (keyvalp != NULL)
	    {
	      db_private_free_and_init (thread_p, keyvalp);
	    }
	}

      /* form a new leaf record */
      LeafRec_Node.key_len = ((key_len < BTREE_MAX_KEYLEN_INPAGE)
			      ? key_len : -1);
      VPID_SET_NULL (&LeafRec_Node.ovfl);
      ret = btree_write_record (thread_p, btid, &LeafRec_Node, key, true,
				(LeafRec_Node.key_len == -1), key_len,
				false, cls_oid, oid, &Rec);
      if (ret != NO_ERROR)
	{
	  goto exit_on_error;
	}

      if (Rec.length > max_free)
	{
	  /* if this block is entered, that means there is not enough space
	   * in the leaf page for a new key. This shows a bug in the algorithm.
	   */
	  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR,
		  0);
	  er_log_debug (ARG_FILE_LINE,
			"btree_insert_into_leaf: no space to insert a new key.");
	  goto exit_on_error;
	}

      /* insert the new record */
      if (spage_insert_at (thread_p, page_ptr, slot_id, &Rec) != SP_SUCCESS)
	{
	  goto exit_on_error;
	}
      oRec.data = (char *) malloc (DB_PAGESIZE);
      if (oRec.data == NULL)
	{
	  goto exit_on_error;
	}

      /* save the inserted record for redo purposes,
       * in the case of redo, the record will be inserted
       */
      *(INT16 *) ((char *) oRec.data + LOFFS1) = (INT16) key_len;
      *(INT16 *) ((char *) oRec.data + LOFFS2) = 0;	/* Leaf Record */
      *(INT16 *) ((char *) oRec.data + LOFFS3) = Rec.type;	/* Record Type */
      memcpy ((char *) oRec.data + LOFFS4, Rec.data, Rec.length);
      recset_length = Rec.length + LOFFS4;

      /* update the page header */
      btree_get_header_ptr (page_ptr, &header_ptr);

      key_cnt = BTREE_GET_NODE_KEY_CNT (header_ptr);
      key_cnt++;
      BTREE_PUT_NODE_KEY_CNT (header_ptr, key_cnt);
      if (key_len >= BTREE_MAX_KEYLEN_INPAGE)
	{
	  key_len = DISK_VPID_SIZE;
	}
      if (BTREE_GET_NODE_MAX_KEY_LEN (header_ptr) < key_len)
	{
	  BTREE_PUT_NODE_MAX_KEY_LEN (header_ptr, key_len);
	}

      /* log the new record insertion and update to the header record for
       * undo/redo purposes.  This can be after the insert/update since we
       * still have the page pinned.
       */
      if (file_new_isvalid (thread_p, &btid->sys_btid->vfid) == DISK_VALID)
	{
	  /* page level undo logging needed (see comment in function header). */
	  log_append_undoredo_data2 (thread_p, RVBT_LFRECORD_KEYINS,
				     &btid->sys_btid->vfid, page_ptr, slot_id,
				     sizeof (slot_id), recset_length,
				     &slot_id, oRec.data);
	}
      else
	{
	  log_append_redo_data2 (thread_p, RVBT_LFRECORD_KEYINS,
				 &btid->sys_btid->vfid, page_ptr, slot_id,
				 recset_length, oRec.data);
	}
      pgbuf_set_dirty (thread_p, page_ptr, DONT_FREE);
      free_and_init (oRec.data);
    }
  else
    {				/* key already exits */
      /* If do_unique_check argument is true,
       * set error code and exit
       */
      if (do_unique_check == true)
	{
	  if (PRM_UNIQUE_ERROR_KEY_VALUE)
	    {
	      char *keyval = pr_valstring (key);
	      ret = ER_UNIQUE_VIOLATION_WITHKEY;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ret, 1,
		      (keyval == NULL) ? " " : keyval);
	      if (keyval)
		free_and_init (keyval);
	    }
	  else
	    {
	      ret = ER_BTREE_UNIQUE_FAILED;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ret, 0);
	    }
	  goto exit_on_error;
	}

      /* read the record that contains the key */
      if (spage_get_record (page_ptr, slot_id, &Rec, COPY) != S_SUCCESS)
	{
	  goto exit_on_error;
	}

      btree_read_record (thread_p, btid, &Rec, NULL, &LeafRec_Pnt, true,
			 &dummy, &offset, 0);

      /* check for duplicate OID */
      ptr = Rec.data + offset;
      for (i = 0; i < CEIL_PTVDIV (Rec.length - offset, oid_size); i++)
	{
	  if (BTREE_IS_UNIQUE (btid))
	    {
	      ptr += OR_OID_SIZE;	/* ignore class OID */
	    }
	  OR_GET_OID (ptr, &oid1);
	  ptr += OR_OID_SIZE;
	  if (oid_compare (oid, &oid1) == 0)
	    {
	      /* put a NOOP redo log here, which does NOTHING, this is used
	       * to accompany the corresponding logical undo log, if there is
	       * any, which caused this routine to be called.
	       */
	      log_append_redo_data2 (thread_p, RVBT_NOOP,
				     &btid->sys_btid->vfid, page_ptr, slot_id,
				     0, NULL);
	      pgbuf_set_dirty (thread_p, page_ptr, DONT_FREE);

	      /* We can get duplicate OIDs in the following senario:
	       * We are recovering from a crash of a btree_delete() where
	       * the key/value delete undo log was written and flushed to disk
	       * but the OID removal redo log was not flushed to disk.
	       * In this case, we will make this a warning severity.
	       *
	       * When this logging hole gets filled, we should remove the
	       * warning severity.  Actually, I'd argue that we should remove
	       * the duplicate OID check altogether, since in a healthy btree
	       * this should never happen.  If we do this, we need to beef up
	       * checkdb to check for duplicate btree OIDs.
	       */
	      er_set ((log_is_in_crash_recovery ())? ER_WARNING_SEVERITY :
		      ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_BTREE_DUPLICATE_OID, 3, oid->volid, oid->pageid,
		      oid->slotid);
	      goto exit_on_error;
	    }
	}

      ovfl_vpid = LeafRec_Pnt.ovfl;
      if (ovfl_vpid.pageid == NULL_PAGEID)
	{			/* no overflow page */

	  /* put a LOGICAL log to undo the insertion of <key, oid> pair
	   * to the B+tree index. This will be a call to delete this pair
	   * from the index. Put this logical log here, because now we know
	   * that the <key, oid> pair to be inserted is not already in the index.
	   */
	  if (file_new_isvalid (thread_p, &btid->sys_btid->vfid) ==
	      DISK_INVALID)
	    {
	      /* "logical" undo logging needed (see comment in function header). */
	      keyvalp = NULL;
	      ret = btree_rv_save_keyval (btid, key, cls_oid, oid,
					  &keyvalp, &keyval_len);
	      if (ret != NO_ERROR)
		{
		  goto exit_on_error;
		}

	      log_append_undo_data2 (thread_p, RVBT_KEYVAL_INS,
				     &btid->sys_btid->vfid, NULL, -1,
				     keyval_len, keyvalp);
	      if (keyvalp != NULL)
		{
		  db_private_free_and_init (thread_p, keyvalp);
		}
	    }

	  if (max_free > oid_size)
	    {			/* enough space in page */
	      if (file_new_isvalid (thread_p, &btid->sys_btid->vfid) ==
		  DISK_VALID)
		{
		  /* page level undo logging needed (see comment in
		   * function header).
		   */
		  *(INT16 *) ((char *) recset_data + OFFS1) = 0;	/* leaf record */
		  *(INT16 *) ((char *) recset_data + OFFS2) = Rec.type;
		  memcpy ((char *) recset_data + OFFS3, Rec.data, Rec.length);
		  log_append_undo_data2 (thread_p, RVBT_NDRECORD_UPD,
					 &btid->sys_btid->vfid, page_ptr,
					 slot_id, Rec.length + OFFS3,
					 recset_data);
		}
	      if (BTREE_IS_UNIQUE (btid))
		{
		  btree_append_oid (&Rec, cls_oid);
		}
	      btree_append_oid (&Rec, oid);
	      btree_write_fixed_portion_of_leaf_record (&Rec, &LeafRec_Pnt);

	      /* insert the value into the record */
	      if (spage_update (thread_p, page_ptr, slot_id, &Rec) !=
		  SP_SUCCESS)
		{
		  goto exit_on_error;
		}

	      /* log the new node record for redo purposes */
	      log_append_redo_data2 (thread_p, RVBT_LFRECORD_OIDINS,
				     &btid->sys_btid->vfid, page_ptr, slot_id,
				     sizeof (RECINS_STRUCT), &recins);

	      pgbuf_set_dirty (thread_p, page_ptr, DONT_FREE);

	    }
	  else
	    {			/* needs an overflow page */
	      oRec.type = REC_HOME;
	      oRec.area_size = DB_PAGESIZE;
	      oRec.data = (char *) malloc (DB_PAGESIZE);
	      if (oRec.data == NULL)
		{
		  goto exit_on_error;
		}

	      ret =
		btree_start_overflow_page (thread_p, &oRec, btid, &new_vpid,
					   &newp, nearp_vpid, cls_oid, oid);
	      if (ret != NO_ERROR)
		{
		  goto exit_on_error;
		}

	      if (file_new_isvalid (thread_p, &btid->sys_btid->vfid) ==
		  DISK_VALID)
		{
		  /* page level undo logging needed (see comment in
		   * function header).
		   */
		  *(INT16 *) ((char *) recset_data + OFFS1) = 0;	/* leaf rec */
		  *(INT16 *) ((char *) recset_data + OFFS2) = Rec.type;
		  memcpy ((char *) recset_data + OFFS3, Rec.data, Rec.length);
		  log_append_undo_data2 (thread_p, RVBT_NDRECORD_UPD,
					 &btid->sys_btid->vfid, page_ptr,
					 slot_id, Rec.length + OFFS3,
					 recset_data);
		}

	      /* make the leaf record point to the new overflow page */
	      LeafRec_Pnt.ovfl = new_vpid;
	      btree_write_fixed_portion_of_leaf_record (&Rec, &LeafRec_Pnt);
	      if (spage_update (thread_p, page_ptr, slot_id, &Rec) !=
		  SP_SUCCESS)
		{
		  goto exit_on_error;
		}

	      /* log the changes to the leaf node record for redo purposes */
	      recins.rec_type = REGULAR;
	      recins.ovfl_vpid = new_vpid;
	      recins.ovfl_changed = true;
	      recins.oid_inserted = false;
	      log_append_redo_data2 (thread_p, RVBT_LFRECORD_OIDINS,
				     &btid->sys_btid->vfid, page_ptr, slot_id,
				     sizeof (RECINS_STRUCT), &recins);

	      pgbuf_set_dirty (thread_p, newp, FREE);
	      newp = NULL;
	      pgbuf_set_dirty (thread_p, page_ptr, DONT_FREE);

	      free_and_init (oRec.data);
	    }
	}
      else
	{			/* overflow page exists */
	  oRec.area_size = DB_PAGESIZE;
	  oRec.data = (char *) malloc (DB_PAGESIZE);
	  if (oRec.data == NULL)
	    {
	      goto exit_on_error;
	    }

	  /* find the last overflow page */
	  do
	    {
	      ovfp = pgbuf_fix (thread_p, &ovfl_vpid, OLD_PAGE,
				PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
	      if (ovfp == NULL)
		{
		  goto exit_on_error;
		}

	      p_ovfl_vpid = ovfl_vpid;	/* structure copy */

	      btree_get_header_ptr (ovfp, &header_ptr);
	      btree_get_next_overflow_vpid (header_ptr, &ovfl_vpid);

	      /* check for duplicate OID */
	      (void) spage_get_record (ovfp, 1, &oRec, COPY);
	      oid_cnt = CEIL_PTVDIV (oRec.length, oid_size);
	      ptr = oRec.data;

	      for (i = 0; i < oid_cnt; i++)
		{
		  if (BTREE_IS_UNIQUE (btid))
		    {
		      ptr += OR_OID_SIZE;	/* skip class OID */
		    }
		  OR_GET_OID (ptr, &oid1);
		  ptr += OR_OID_SIZE;
		  if (oid_compare (oid, &oid1) == 0)
		    {
		      /* put a NOOP redo log here, which does NOTHING, this is used
		       * to accompany the corresponding logical undo log, if there is
		       * any, which caused this routine to be called.
		       */
		      log_append_redo_data2 (thread_p, RVBT_NOOP,
					     &btid->sys_btid->vfid, ovfp, 1,
					     0, NULL);

		      /* We can get duplicate OIDs in the following senario:
		       * We are recovering from a crash of a btree_delete() where
		       * the key/value delete undo log was written and flushed to disk
		       * but the OID removal redo log was not flushed to disk.
		       * In this case, we will make this a warning severity.
		       *
		       * When this logging hole gets filled, we should remove the
		       * warning severity.  Actually, I'd argue that we should remove
		       * the duplicate OID check altogether, since in a healthy btree
		       * this should never happen.  If we do this, we need to beef up
		       * checkdb to check for duplicate btree OIDs.
		       */
		      er_set ((log_is_in_crash_recovery ())?
			      ER_WARNING_SEVERITY : ER_ERROR_SEVERITY,
			      ARG_FILE_LINE, ER_BTREE_DUPLICATE_OID, 3,
			      oid->volid, oid->pageid, oid->slotid);
		      pgbuf_set_dirty (thread_p, ovfp, FREE);
		      ovfp = NULL;
		      goto exit_on_error;
		    }
		}

	      if (ovfl_vpid.pageid != NULL_PAGEID)
		{
		  pgbuf_unfix (thread_p, ovfp);
		  ovfp = NULL;
		}

	    }
	  while (ovfl_vpid.pageid != NULL_PAGEID);


	  /* put a LOGICAL log to undo the insertion of <key, oid> pair
	   * to the B+tree index. This will be a call to delete this pair
	   * from the index. Put this logical log here, because now we know
	   * that the <key, oid> pair to be inserted is not already in the index.
	   */
	  if (file_new_isvalid (thread_p, &btid->sys_btid->vfid) ==
	      DISK_INVALID)
	    {
	      /* "logical" undo logging needed (see comment in function header). */
	      keyvalp = NULL;
	      ret = btree_rv_save_keyval (btid, key, cls_oid, oid,
					  &keyvalp, &keyval_len);
	      if (ret != NO_ERROR)
		{
		  goto exit_on_error;
		}

	      log_append_undo_data2 (thread_p, RVBT_KEYVAL_INS,
				     &btid->sys_btid->vfid, NULL, -1,
				     keyval_len, keyvalp);
	      if (keyvalp != NULL)
		{
		  db_private_free_and_init (thread_p, keyvalp);
		}
	    }

	  if (spage_max_space_for_new_record (thread_p, ovfp) > oid_size)
	    {			/* enough space */
	      /* insert the value into the last overflow page */
	      if (spage_get_record (ovfp, 1, &oRec, COPY) != S_SUCCESS)
		{
		  goto exit_on_error;
		}

	      if (file_new_isvalid (thread_p, &btid->sys_btid->vfid) ==
		  DISK_VALID)
		{
		  /* page level undo logging needed (see comment in
		   * function header).
		   */
		  *(INT16 *) ((char *) recset_data + OFFS1) = 0;	/* leaf rec */
		  *(INT16 *) ((char *) recset_data + OFFS2) = oRec.type;
		  memcpy ((char *) recset_data + OFFS3, oRec.data,
			  oRec.length);
		  log_append_undo_data2 (thread_p, RVBT_NDRECORD_UPD,
					 &btid->sys_btid->vfid, ovfp, 1,
					 oRec.length + OFFS3, recset_data);
		}

	      if (BTREE_IS_UNIQUE (btid))
		{
		  btree_append_oid (&oRec, cls_oid);
		}
	      btree_append_oid (&oRec, oid);

	      if (spage_update (thread_p, ovfp, 1, &oRec) != SP_SUCCESS)
		{
		  goto exit_on_error;
		}

	      /* log the new node record for redo purposes */
	      recins.rec_type = OVERFLOW;
	      recins.new_ovflpg = false;
	      recins.oid_inserted = true;
	      recins.ovfl_changed = false;
	      log_append_redo_data2 (thread_p, RVBT_LFRECORD_OIDINS,
				     &btid->sys_btid->vfid, ovfp, 1,
				     sizeof (RECINS_STRUCT), &recins);

	      pgbuf_set_dirty (thread_p, ovfp, FREE);
	      ovfp = NULL;

	      /* Leaf Page is NOT set dirty, because It has not been changed! */

	    }
	  else
	    {			/* needs a new overflow page */
	      ret =
		btree_start_overflow_page (thread_p, &oRec, btid, &new_vpid,
					   &newp, nearp_vpid, cls_oid, oid);
	      if (ret != NO_ERROR)
		{
		  goto exit_on_error;
		}

	      if (spage_get_record (ovfp, HEADER, &peek_rec, PEEK) !=
		  S_SUCCESS)
		{
		  goto exit_on_error;
		}

	      if (file_new_isvalid (thread_p, &btid->sys_btid->vfid) ==
		  DISK_VALID)
		{
		  /* page level undo logging needed (see comment in
		   * function header).
		   */
		  *(INT16 *) ((char *) recset_data + OFFS1) = 0;	/* leaf rec */
		  *(INT16 *) ((char *) recset_data + OFFS2) = peek_rec.type;
		  memcpy ((char *) recset_data + OFFS3, peek_rec.data,
			  peek_rec.length);
		  log_append_undo_data2 (thread_p, RVBT_NDRECORD_UPD,
					 &btid->sys_btid->vfid, ovfp, HEADER,
					 peek_rec.length + OFFS3,
					 recset_data);
		}

	      /* make the last overflow page point to the new one */
	      btree_write_overflow_header (&peek_rec, &new_vpid);

	      /* log the last overflow page changes for redo purposes */
	      recins.rec_type = OVERFLOW;
	      recins.ovfl_vpid = new_vpid;
	      recins.new_ovflpg = false;
	      recins.oid_inserted = false;
	      recins.ovfl_changed = true;
	      log_append_redo_data2 (thread_p, RVBT_LFRECORD_OIDINS,
				     &btid->sys_btid->vfid, ovfp, HEADER,
				     sizeof (RECINS_STRUCT), &recins);

	      pgbuf_set_dirty (thread_p, newp, FREE);
	      newp = NULL;
	      pgbuf_set_dirty (thread_p, ovfp, FREE);
	      ovfp = NULL;

	      /* Leaf Page NOT set  dirty, since not changed */
	    }
	  free_and_init (oRec.data);
	}
    }

  free_and_init (Rec.data);
  if (recset_data)
    {
      free_and_init (recset_data);
    }

end:

#if defined(SERVER_MODE)
  (void) thread_set_check_interrupt (thread_p, old_check_interrupt);
#endif /* SERVER_MODE */

  return ret;

exit_on_error:

  if (ovfp)
    {
      pgbuf_unfix (thread_p, ovfp);
      ovfp = NULL;
    }
  if (newp)
    {
      pgbuf_unfix (thread_p, newp);
      newp = NULL;
    }
  if (oRec.data)
    {
      free_and_init (oRec.data);
    }
  if (Rec.data)
    {
      free_and_init (Rec.data);
    }
  if (recset_data)
    {
      free_and_init (recset_data);
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

/*
 * btree_get_prefix () -
 *   return: db_value containing the prefix key.  This must be
 *           cleared when it is done being used.
 *   key1(in): first key
 *   key2(in): second key
 *   prefix_key(in):
 *   is_reverse(in):
 *
 * Note: This function finds the prefix (the separator) of two strings.
 * Currently this is only done for one of the six string types,
 * but with multi-column indexes and uniques coming, we may want
 * to do prefix keys for sequences as well.
 *
 * The purpose of this routine is to find a prefix that is
 * greater than or equal to the first key but strictly less
 * than the second key.  This routine assumes that the second
 * key is strictly greater than the first key.
 */
int
btree_get_prefix (const DB_VALUE * key1, const DB_VALUE * key2,
		  DB_VALUE * prefix_key, int is_reverse)
{
  /* currently we only do prefix keys for the string types */
  return db_string_unique_prefix (key1, key2, prefix_key, is_reverse);
}

/*
 * btree_find_split_point () -
 *   return: the key or key separator (prefix) to be moved to the
 *           parent page, or NULL_KEY. The length of the returned
 *           key, or prefix, is set in mid_keylen. The parameter
 *           mid_slot is set to the record number of the split point record.
 *   btid(in):
 *   page_ptr(in): Pointer to the page
 *   mid_slot(out): Set to contain the record number for the split point slot
 *   key(in): Key to be inserted to the index
 *   clear_midkey(in):
 *
 * Note: Finds the split point of the given page by considering the
 * length of the existing records and the length of the key.
 * For a leaf page split operation, if there are n keys in the
 * page, then mid_slot can be set to :
 *
 *              0 : all the records in the page are to be moved to the newly
 *                  allocated page, key is to be inserted into the original
 *                  page. Mid_key is between key and the first record key.
 *
 *              n : all the records will be kept in the original page. Key is
 *                  to be inserted to the newly allocated page. Mid_key is
 *                  between the last record key and the key.
 *      otherwise : slot point is in the range 1 to n-1, inclusive. The page
 *                  is to be split into half.
 *
 * Note: the returned db_value should be cleared and FREED by the caller.
 */
static DB_VALUE *
btree_find_split_point (THREAD_ENTRY * thread_p, BTID_INT * btid,
			PAGE_PTR page_ptr, INT16 * mid_slot, DB_VALUE * key,
			bool * clear_midkey)
{

  RECDES Rec;
  bool leaf_page;
  INT16 slot_id;
  int ent_size;
  int key_cnt, key_len, offset;
  INT16 tot_rec, sum;
  int n, i, mid_size;
  bool m_clear_key, n_clear_key;
  DB_VALUE *mid_key = NULL, *next_key = NULL, *prefix_key = NULL, *tmp_key;
  int key_read, found;
  char *header_ptr;
  LEAF_REC leaf_pnt;
  NON_LEAF_REC nleaf_pnt;

  /* get the page header */
  btree_get_header_ptr (page_ptr, &header_ptr);
  leaf_page = (BTREE_GET_NODE_TYPE (header_ptr) == LEAF_NODE) ? true : false;
  key_cnt = BTREE_GET_NODE_KEY_CNT (header_ptr);	/* get the key count */
  n = spage_number_of_records (page_ptr) - 1;	/* last record position */

  if (key_cnt <= 0)
    {
      er_log_debug (ARG_FILE_LINE,
		    "btree_find_split_point: node key count underflow: %d",
		    key_cnt);
      goto error;
    }

  key_read = false;

  /* find the slot position of the key if it is to be located in the page */
  if (leaf_page)
    {
      found =
	btree_search_leaf_page (thread_p, btid, page_ptr, key, &slot_id);
      if (slot_id == NULL_SLOTID)	/* leaf search failed */
	{
	  goto error;
	}
    }
  else
    {
      found = 0;
      slot_id = NULL_SLOTID;
    }

  key_len = btree_get_key_length (key);
  key_len = (key_len < BTREE_MAX_KEYLEN_INPAGE) ? key_len : DISK_VPID_SIZE;

  /* records will be variable if:
   *    1) the btree key_type is variable
   *    2) the btree key_type is a string type and this is a non leaf page,
   *       in this case we will be using prefix keys which are variable.
   *    3) we are splitting a leaf page (there may be an arbitrary number
   *       of OIDs associated with this key).
   */
  if (!(pr_is_variable_type (btid->key_type->type->id) ||
	(pr_is_string_type (btid->key_type->type->id) && !leaf_page) ||
	leaf_page))
    {
      /* records are of fixed size */
      *mid_slot = CEIL_PTVDIV (n, 2);
    }

  else
    {
      /* first find out the size of the data on the page, don't count the
       * header record.
       */
      for (i = 1, tot_rec = 0; i <= n; i++)
	{
	  tot_rec += spage_get_record_length (page_ptr, i);
	}

      if (leaf_page && !found)
	{			/* take key length into consideration */

	  ent_size = LEAFENTSZ (key_len);
	  tot_rec += ent_size;
	  mid_size = CEIL_PTVDIV (tot_rec, 2);
	  for (i = 1, sum = 0; i < slot_id && sum < mid_size; i++)
	    {
	      sum += spage_get_record_length (page_ptr, i);
	    }
	  if (sum < mid_size)
	    {
	      sum += ent_size;
	      key_read = true;
	      for (; sum < mid_size && i <= n; i++)
		{
		  sum += spage_get_record_length (page_ptr, i);
		}
	    }

	}
      else
	{			/* consider only the length of the records in the page */

	  mid_size = CEIL_PTVDIV (tot_rec, 2);
	  for (i = 1, sum = 0; sum < mid_size && i <= n; i++)
	    {
	      sum += spage_get_record_length (page_ptr, i);
	    }
	}

      i--;
      *mid_slot = i;

      /* We used to have a check here to make sure that the key could be
       * inserted into one of the pages after the split operation.  It must
       * always be the case that the key can be inserted into one of the
       * pages after split because keys can be no larger than
       * BTREE_MAX_KEYLEN_INPAGE and the determination of the splitpoint above
       * should always guarantee that both pages have at least that much
       * free (usually closer to half the page, certainly more than 2 *
       * BTREE_MAX_KEYLEN_INPAGE).
       */
    }

  if (*mid_slot == n && (!leaf_page || (slot_id != (n + 1))))
    {
      (*mid_slot)--;
    }

  mid_key = (DB_VALUE *) db_private_alloc (thread_p, sizeof (DB_VALUE));
  if (mid_key == NULL)
    {
      goto error;
    }
  db_make_null (mid_key);

  if (*mid_slot == 0
      || ((*mid_slot != n) && ((*mid_slot == (slot_id - 1) && key_read))))
    {
      /* the new key is the split key */
      PR_TYPE *pr_type =
	(leaf_page ? btid->key_type->type : btid->nonleaf_key_type->type);

      m_clear_key = false;

      (*(pr_type->setval)) (mid_key, key, m_clear_key);
    }
  else
    {
      /* the split key is one of the keys on the page */
      if (spage_get_record (page_ptr, *mid_slot, &Rec, PEEK) != S_SUCCESS)
	{
	  goto error;
	}

      /* we copy the key here because Rec lives on the stack and mid_key
       * is returned from this routine.
       */
      btree_read_record (thread_p, btid, &Rec, mid_key,
			 (leaf_page ? (void *) &leaf_pnt : (void *)
			  &nleaf_pnt), leaf_page, &m_clear_key, &offset, 1);
    }

  /* Check if we can make use of prefix keys.  We can't use them in the
   * upper levels of the trees because the algorithm will fall apart.  We
   * can only use them when splitting a leaf page.
   */
  if (!pr_is_string_type (btid->key_type->type->id) || !leaf_page)
    {
      /* no need to find the prefix, return the middle key as it is */
      *clear_midkey = m_clear_key;
      goto success;
    }

  /* The determination of the prefix key is dependent on the next key */
  next_key = (DB_VALUE *) db_private_alloc (thread_p, sizeof (DB_VALUE));
  if (next_key == NULL)
    {
      goto error;
    }
  db_make_null (next_key);

  /* I think that this determination of the next key being the new key
   * is wrong!!!!  Can't the next key be the new key and not be the last
   * key??  I think it ought to be something like:
   *       if (*mid_slot == (slot_id + 1))
   *
   * there may also be something having to do with key_read, but I don't
   * know.
   *
   * CHECK THIS OUT!!!!!! -- TODO:
   */
  if (*mid_slot == n && slot_id == (n + 1))
    {
      /* the next key is the new key, we don't have to read it */
      n_clear_key = true;
      if (pr_clone_value (key, next_key) != NO_ERROR)
	{
	  goto error;
	}
    }
  else
    {
      /* The next key is one of the keys on the page */
      if (spage_get_record (page_ptr, (*mid_slot) + 1, &Rec, PEEK) !=
	  S_SUCCESS)
	{
	  goto error;
	}

      /* we copy the key here because Rec lives on the stack and mid_key
       * is returned from this routine.
       */
      btree_read_record (thread_p, btid, &Rec, next_key,
			 (leaf_page ? (void *) &leaf_pnt : (void *)
			  &nleaf_pnt), leaf_page, &n_clear_key, &offset, 1);
    }

  /* now that we have the mid key and the next key, we can determine the
   * prefix key.
   */

  prefix_key = (DB_VALUE *) db_private_alloc (thread_p, sizeof (DB_VALUE));
  if (prefix_key == NULL
      || (btree_get_prefix (mid_key, next_key, prefix_key,
			    BTREE_IS_LAST_KEY_DESC (btid)) != NO_ERROR))
    {
      goto error;
    }

  *clear_midkey = true;		/* we must always clear prefix keys */

  /* replace the mid_key with the prefix_key */
  tmp_key = mid_key;
  mid_key = prefix_key;
  prefix_key = tmp_key;		/* this makes sure we clear/free the old mid key */
  goto success;

  /* error handling and cleanup. */
error:

  if (mid_key)
    {
      btree_clear_key_value (&m_clear_key, mid_key);
      db_private_free_and_init (thread_p, mid_key);
    }
  mid_key = NULL;

  /* fall through */

success:

  if (next_key)
    {
      btree_clear_key_value (&n_clear_key, next_key);
      db_private_free_and_init (thread_p, next_key);
    }
  if (prefix_key)
    {
      pr_clear_value (prefix_key);
      db_private_free_and_init (thread_p, prefix_key);
    }

  return mid_key;
}

/*
 * btree_split_node () -
 *   return: NO_ERROR
 *           child_vpid is set to page identifier for the child page to be
 *           followed, Q or R, or the page identifier of a newly allocated
 *           page to insert the key, or NULL_PAGEID. The parameter key is
 *           set to the middle key that will be put into the parent page P.
 *   btid(in): The index identifier
 *   P(in): Page pointer for the parent page of page Q
 *   Q(in): Page pointer for the page to be split
 *   R(in): Page pointer for the newly allocated page
 *   P_vpid(in): Page identifier for page Q
 *   Q_vpid(in): Page identifier for page Q
 *   R_vpid(in): Page identifier for page R
 *   p_slot_id(in): The slot of parent page P which points to page Q
 *   leaf_page(in): Flag which shows whether page Q is a leaf page, or not
 *   key(out): Set to contain the middle key of the split operation
 *   child_vpid(out): Set to the child page identifier
 *
 * Note: Page Q is split into two pages: Q and R. The second half of
 * of the page Q is move to page R. The middle key of of the
 * split operation is moved to parent page P. Depending on the
 * split point, the whole page Q may be moved to page R, or the
 * whole page content may be kept in page Q. If the key can not
 * fit into one of the pages after the split, a new page is
 * allocated for the key and its page identifier is returned.
 * The headers of all pages are updated, accordingly.
 */
static int
btree_split_node (THREAD_ENTRY * thread_p, BTID_INT * btid, PAGE_PTR P,
		  PAGE_PTR Q, PAGE_PTR R, VPID * P_vpid, VPID * Q_vpid,
		  VPID * R_vpid, INT16 p_slot_id, bool leaf_page,
		  DB_VALUE * key, VPID * child_vpid)
{
  INT16 midSlot_id;
  int nrecs, keys_cnt, leftcnt, rightcnt, right;
  RECDES peek_rec;
  RECDES Rec, tRec;
  NON_LEAF_REC NLeaf_Rec, nleaf_ptr;
  BTREE_NODE_HEADER pHeader;
  BTREE_NODE_HEADER rHeader;
  VPID next_vpid, page_vpid;
  int i, c;
  INT16 max_key_len;
  int max_key, key_len;
  bool clear_midkey;
  DB_VALUE *mid_key;
  int q_moved;
  RECSET_HEADER recset_header;	/* for recovery purposes */
  char *recset_data, *datap;	/* for recovery purposes */
  int recset_length;		/* for recovery purposes */
  int ret = NO_ERROR;

  recset_data = NULL;
  Rec.data = NULL;

  /* initialize child page identifier */
  VPID_SET_NULL (child_vpid);
  mid_key = NULL;

#if defined(BTREE_DEBUG)
  if ((!P || !Q || !R) || (P_vpid->pageid == NULL_PAGEID) ||
      (Q_vpid->pageid == NULL_PAGEID) || (R_vpid->pageid == NULL_PAGEID))
    {
      goto exit_on_error;
    }
#endif /* BTREE_DEBUG */

  /* initializatioins */
  Rec.area_size = DB_PAGESIZE;
  Rec.data = (char *) malloc (DB_PAGESIZE);
  if (Rec.data == NULL)
    {
      goto exit_on_error;
    }

  nrecs = spage_number_of_records (Q);	/* get the key count of page Q */

  if (spage_get_record (Q, HEADER, &peek_rec, PEEK) != S_SUCCESS)
    {
      goto exit_on_error;
    }

  keys_cnt = BTREE_GET_NODE_KEY_CNT (peek_rec.data);
  if (keys_cnt <= 0)
    {
      goto exit_on_error;
    }

  /* find the middle record of the page Q  and find the number of
   * keys after split in pages Q and R, respectively
   */
  mid_key =
    btree_find_split_point (thread_p, btid, Q, &midSlot_id, key,
			    &clear_midkey);

  if (mid_key == NULL)
    {
      er_log_debug (ARG_FILE_LINE,
		    "btree_split_node: Null middle key after split."
		    " Operation Ignored.\n");
      goto exit_on_error;
    }

  leftcnt = (leaf_page) ? midSlot_id : (midSlot_id - 1);
  rightcnt = (leaf_page) ? (keys_cnt - leftcnt) : (keys_cnt - leftcnt - 1);
  q_moved = (midSlot_id == 0) ? 1 : 0;

  /* log the old header record for undo purposes */
  log_append_undo_data2 (thread_p, RVBT_NDHEADER_UPD, &btid->sys_btid->vfid,
			 Q, HEADER, peek_rec.length, peek_rec.data);

  BTREE_PUT_NODE_KEY_CNT (peek_rec.data, leftcnt);
  BTREE_GET_NODE_NEXT_VPID (peek_rec.data, &next_vpid);

  /* We may need to update the max_key length if the mid key is larger than
   * the max key length.  This can happen due to disk padding when the
   * prefix key length approaches the fixed key length.
   */
  max_key = btree_get_key_length (mid_key);
  max_key_len = BTREE_GET_NODE_MAX_KEY_LEN (peek_rec.data);
  if (max_key > max_key_len)
    {
      BTREE_PUT_NODE_MAX_KEY_LEN (peek_rec.data, max_key);
    }
  else
    {
      max_key = max_key_len;
    }

  if (leaf_page)
    {
      BTREE_PUT_NODE_NEXT_VPID (peek_rec.data, R_vpid);
    }
  else
    {
      VPID null_vpid;

      VPID_SET_NULL (&null_vpid);
      BTREE_PUT_NODE_NEXT_VPID (peek_rec.data, &null_vpid);
    }

  if (q_moved)
    {
      BTREE_PUT_NODE_MAX_KEY_LEN (peek_rec.data, 0);
    }

  /* log the new header record for redo purposes */
  log_append_redo_data2 (thread_p, RVBT_NDHEADER_UPD, &btid->sys_btid->vfid,
			 Q, HEADER, peek_rec.length, peek_rec.data);

  rHeader.node_type = (leaf_page) ? LEAF_NODE : NON_LEAF_NODE;
  rHeader.key_cnt = rightcnt;
  rHeader.max_key_len = max_key;
  rHeader.next_vpid = next_vpid;
  btree_write_node_header (&Rec, &rHeader);
  if (spage_insert_at (thread_p, R, HEADER, &Rec) != SP_SUCCESS)
    {
      goto exit_on_error;
    }

  /* log the new header record for redo purposes, there is no need
     to undo the change to the header record, since the page will be
     dealloacted on futher undo operations. */
  log_append_redo_data2 (thread_p, RVBT_NDHEADER_INS, &btid->sys_btid->vfid,
			 R, HEADER, Rec.length, Rec.data);

  /* move second half of page Q to page R */
  right = (leaf_page) ? rightcnt : (rightcnt + 1);

  /* for recovery purposes */
  recset_data = (char *) malloc (DB_PAGESIZE);
  if (recset_data == NULL)
    {
      goto exit_on_error;
    }

  /* read the before image of second half of page Q for undo logging */
  ret = btree_rv_util_save_page_records (Q, midSlot_id + 1, right,
					 midSlot_id + 1, recset_data,
					 &recset_length);
  if (ret != NO_ERROR)
    {
      goto exit_on_error;
    }

  /* move the second half of page Q to page R */
  for (i = 1; i <= right; i++)
    {
      if ((spage_get_record (Q, midSlot_id + 1, &tRec, PEEK) !=
	   S_SUCCESS)
	  || (spage_insert_at (thread_p, R, i, &tRec) != SP_SUCCESS)
	  || (spage_delete (thread_p, Q, midSlot_id + 1) != midSlot_id + 1))
	{
	  if (i > 1)
	    {
	      ret = btree_rv_util_save_page_records (R, 1, i - 1, 1,
						     recset_data,
						     &recset_length);
	      if (ret != NO_ERROR)
		{
		  goto exit_on_error;
		}
	      log_append_undo_data2 (thread_p, RVBT_DEL_PGRECORDS,
				     &btid->sys_btid->vfid, Q, -1,
				     recset_length, recset_data);
	    }
	  goto exit_on_error;
	}
    }				/* for */

  /* for delete redo logging of page Q */
  recset_header.rec_cnt = right;
  recset_header.first_slotid = midSlot_id + 1;

  /* undo/redo logging for page Q */
  log_append_undoredo_data2 (thread_p, RVBT_DEL_PGRECORDS,
			     &btid->sys_btid->vfid, Q, -1, recset_length,
			     sizeof (RECSET_HEADER), recset_data,
			     &recset_header);

  /* Log the second half of page Q for redo purposes on Page R,
     the records on the second half of page Q will be inserted to page R */
  datap = recset_data;
  ((RECSET_HEADER *) datap)->first_slotid = 1;
  log_append_redo_data2 (thread_p, RVBT_INS_PGRECORDS, &btid->sys_btid->vfid,
			 R, -1, recset_length, recset_data);

  /* update parent page P */
  if (spage_get_record (P, p_slot_id, &Rec, COPY) != S_SUCCESS)
    {
      goto exit_on_error;
    }

  /* log the old node record for undo purposes */
  *(INT16 *) ((char *) recset_data + OFFS1) = 1;	/* NonLeaf Record */
  *(INT16 *) ((char *) recset_data + OFFS2) = Rec.type;
  memcpy ((char *) recset_data + OFFS3, Rec.data, Rec.length);
  log_append_undo_data2 (thread_p, RVBT_NDRECORD_UPD, &btid->sys_btid->vfid,
			 P, p_slot_id, Rec.length + OFFS3, recset_data);

  btree_read_fixed_portion_of_non_leaf_record (&Rec, &nleaf_ptr);
  nleaf_ptr.pnt = *R_vpid;
  btree_write_fixed_portion_of_non_leaf_record (&Rec, &nleaf_ptr);
  if (spage_update (thread_p, P, p_slot_id, &Rec) != SP_SUCCESS)
    {
      goto exit_on_error;
    }

  /* log the new node record for redo purposes */
  memcpy ((char *) recset_data + OFFS3, Rec.data, Rec.length);
  log_append_redo_data2 (thread_p, RVBT_NDRECORD_UPD, &btid->sys_btid->vfid,
			 P, p_slot_id, Rec.length + OFFS3, recset_data);

  /* update the parent page P to keep the middle key and to point to
   * pages Q and R.  Remember that this mid key will be on a non leaf page
   * regardless of whether we are splitting a leaf or non leaf page.
   */
  NLeaf_Rec.pnt = *Q_vpid;
  key_len = btree_get_key_length (mid_key);
  NLeaf_Rec.key_len = (key_len < BTREE_MAX_KEYLEN_INPAGE) ? key_len : -1;
  ret = btree_write_record (thread_p, btid, &NLeaf_Rec, mid_key, false,
			    (NLeaf_Rec.key_len == -1), key_len,
			    false, NULL, NULL, &Rec);
  if (ret != NO_ERROR)
    {
      goto exit_on_error;
    }

  /* log the inserted record for both undo and redo purposes,
   * in the case of undo, the inserted record at p_slot_id will be deleted,
   * in the case of redo, the record will be inserted at p_slot_id
   */
  if (spage_insert_at (thread_p, P, p_slot_id, &Rec) != SP_SUCCESS)
    {
      goto exit_on_error;
    }

  *(INT16 *) ((char *) recset_data + OFFS1) = 1;	/* NonLeaf Record */
  *(INT16 *) ((char *) recset_data + OFFS2) = Rec.type;
  memcpy ((char *) recset_data + OFFS3, Rec.data, Rec.length);
  log_append_undoredo_data2 (thread_p, RVBT_NDRECORD_INS,
			     &btid->sys_btid->vfid, P, p_slot_id,
			     sizeof (p_slot_id), Rec.length + OFFS3,
			     &p_slot_id, recset_data);

  if (spage_get_record (P, HEADER, &peek_rec, PEEK) != S_SUCCESS)
    {
      goto exit_on_error;
    }

  /* log the old header record for undo purposes */
  log_append_undo_data2 (thread_p, RVBT_NDHEADER_UPD, &btid->sys_btid->vfid,
			 P, HEADER, peek_rec.length, peek_rec.data);

  keys_cnt = BTREE_GET_NODE_KEY_CNT (peek_rec.data);
  keys_cnt++;
  BTREE_PUT_NODE_KEY_CNT (peek_rec.data, keys_cnt);

  /* We may need to update the max_key length if the mid key is larger than
   * the max key length. This can happen due to disk padding when the 
   * prefix key length approaches the fixed key length.
   */
  max_key = btree_get_key_length (mid_key);
  if (max_key > BTREE_GET_NODE_MAX_KEY_LEN (peek_rec.data))
    {
      BTREE_PUT_NODE_MAX_KEY_LEN (peek_rec.data, max_key);
    }

  /* log the new header record for redo purposes */
  log_append_redo_data2 (thread_p, RVBT_NDHEADER_UPD, &btid->sys_btid->vfid,
			 P, HEADER, peek_rec.length, peek_rec.data);

  /* find the child page to be followed */

  c = (*(btid->nonleaf_key_type->type->cmpval))
    (key, mid_key, btid->key_type, btid->reverse, 0, 1, NULL);

  if (c <= 0)
    {
      page_vpid = *Q_vpid;
    }
  else
    {
      page_vpid = *R_vpid;
    }

  if (mid_key != key)
    {
      btree_clear_key_value (&clear_midkey, mid_key);
      db_private_free_and_init (thread_p, mid_key);
      mid_key = NULL;
    }

  pgbuf_set_dirty (thread_p, P, DONT_FREE);
  pgbuf_set_dirty (thread_p, Q, DONT_FREE);
  pgbuf_set_dirty (thread_p, R, DONT_FREE);
  free_and_init (Rec.data);
  free_and_init (recset_data);

  /* set child page pointer */
  *child_vpid = page_vpid;

  return ret;

exit_on_error:

  if (recset_data)
    {
      free_and_init (recset_data);
    }
  if (Rec.data)
    {
      free_and_init (Rec.data);
    }
  if (mid_key)
    {
      btree_clear_key_value (&clear_midkey, mid_key);
      db_private_free_and_init (thread_p, mid_key);
    }

  if (ret == NO_ERROR)
    {
      ret = er_errid ();
      if (ret == NO_ERROR)
	{
	  ret = ER_FAILED;
	}
    }

  return ret;
}

/*
 * btree_split_root () -
 *   return: NO_ERROR
 *           child_vpid parameter is set to the child page to be followed
 *           after the split operation, or the page identifier of a newly
 *           allocated page for future key insertion, or NULL_PAGEID.
 *           The parameter key is set to the middle key of the split operation.
 *   btid(in): B+tree index identifier
 *   P(in): Page pointer for the root to be split
 *   Q(in): Page pointer for the newly allocated page
 *   R(in): Page pointer for the newly allocated page
 *   P_page_vpid(in): Page identifier for root page P
 *   Q_page_vpid(in): Page identifier for page Q
 *   R_page_vpid(in): Page identifier for page R
 *   leaf_page(in): Flag which shows whether root is currenly a leaf page,
 *                  or not
 *   key(out): Set to contain the middle key of the split operation
 *   child_vpid(out): Set to the child page identifier
 *
 * Note: The root page P is split into two pages: Q and R. In order
 * not to change the actual root page, the first half of the page
 * is moved to page Q and the second half is moved to page R.
 * Depending on the split point found, the whole root page may be
 * moved to Q, or R, leaving the other one empty for future  key
 * insertion. If the key cannot fit into either Q or R after the
 * split, a new page is allocated and its page identifier is
 * returned. Two new records are formed within root page to point
 * to pages Q and R. The headers of all pages are updated.
 */
static int
btree_split_root (THREAD_ENTRY * thread_p, BTID_INT * btid, PAGE_PTR P,
		  PAGE_PTR Q, PAGE_PTR R, VPID * P_page_vpid,
		  VPID * Q_page_vpid, VPID * R_page_vpid, bool leaf_page,
		  DB_VALUE * key, VPID * child_vpid)
{
  INT16 midSlot_id;
  int nrecs, keys_cnt, leftcnt, rightcnt, right, left;
  RECDES Rec, peek_rec;
  NON_LEAF_REC NLeaf_Rec;
  INT16 max_key_len;
  char *header_ptr;
  BTREE_NODE_HEADER qHeader, rHeader;
  int i, c;
  int max_key, key_len;
  bool clear_midkey;
  short node_type;
  DB_VALUE *mid_key;
  VPID page_vpid;
  int q_moved, r_moved;
  char *recset_data;		/* for recovery purposes */
  RECSET_HEADER recset_header;	/* for recovery purposes */
  int recset_length;		/* for recovery purposes */
  int sp_success;
  PGLENGTH log_addr_offset;
  int ret = NO_ERROR;

  recset_data = NULL;
  Rec.data = NULL;

  /* initialize child page identifier */
  VPID_SET_NULL (child_vpid);
  mid_key = NULL;

#if defined(BTREE_DEBUG)
  if ((!P || !Q || !R) || (P_page_vpid->pageid == NULL_PAGEID) ||
      (Q_page_vpid->pageid == NULL_PAGEID) ||
      (R_page_vpid->pageid == NULL_PAGEID))
    {
      goto exit_on_error;
    }
#endif /* BTREE_DEBUG */

  /* initializations */
  Rec.area_size = DB_PAGESIZE;
  Rec.data = (char *) malloc (DB_PAGESIZE);
  if (Rec.data == NULL)
    {
      goto exit_on_error;
    }

  /* log the whole root page P for undo purposes. */
  log_append_undo_data2 (thread_p, RVBT_COPYPAGE, &btid->sys_btid->vfid, P,
			 -1, DB_PAGESIZE, P);

  nrecs = spage_number_of_records (P);

  /* get the number of keys in the root page P */
  btree_get_header_ptr (P, &header_ptr);
  keys_cnt = BTREE_GET_NODE_KEY_CNT (header_ptr);
  if (keys_cnt <= 0)
    {
      goto exit_on_error;
    }

  /* find the middle record of the root page and find the number of
   * keys in pages Q and R, respectively
   */
  mid_key =
    btree_find_split_point (thread_p, btid, P, &midSlot_id, key,
			    &clear_midkey);

  if (!mid_key)
    {
      er_log_debug (ARG_FILE_LINE,
		    "btree_split_root: Null middle key after split."
		    " Operation Ignored.\n");
      goto exit_on_error;
    }

  leftcnt = (leaf_page) ? midSlot_id : (midSlot_id - 1);
  rightcnt = (leaf_page) ? (keys_cnt - leftcnt) : (keys_cnt - leftcnt - 1);
  q_moved = (midSlot_id == (nrecs - 1)) ? 1 : 0;
  r_moved = (midSlot_id == 0) ? 1 : 0;

  /* update root page P header */
  if (spage_get_record (P, HEADER, &peek_rec, PEEK) != S_SUCCESS)
    {
      goto exit_on_error;
    }

  BTREE_PUT_NODE_TYPE (peek_rec.data, NON_LEAF_NODE);
  BTREE_PUT_NODE_KEY_CNT (peek_rec.data, 1);

  /* We may need to update the max_key length if the mid key is larger than
   * the max key length. This can happen due to disk padding when the 
   * prefix key length approaches the fixed key length.
   */
  max_key = btree_get_key_length (mid_key);
  max_key_len = BTREE_GET_NODE_MAX_KEY_LEN (peek_rec.data);
  if (max_key > max_key_len)
    {
      BTREE_PUT_NODE_MAX_KEY_LEN (peek_rec.data, max_key);
    }
  else
    {
      max_key = max_key_len;
    }

  /* log the new header record for redo purposes */
  log_append_redo_data2 (thread_p, RVBT_NDHEADER_UPD, &btid->sys_btid->vfid,
			 P, HEADER, peek_rec.length, peek_rec.data);

  node_type = (leaf_page) ? LEAF_NODE : NON_LEAF_NODE;	/* update page Q header */
  qHeader.node_type = node_type;
  qHeader.key_cnt = leftcnt;
  qHeader.max_key_len = max_key;
  if (leaf_page)
    {
      qHeader.next_vpid = *R_page_vpid;
    }
  else
    {
      VPID_SET_NULL (&qHeader.next_vpid);
    }

  btree_write_node_header (&Rec, &qHeader);
  sp_success = spage_insert_at (thread_p, Q, HEADER, &Rec);
  if (sp_success != SP_SUCCESS)
    {
      goto exit_on_error;
    }

  /* log the new header record for redo purposes */
  log_append_redo_data2 (thread_p, RVBT_NDHEADER_INS, &btid->sys_btid->vfid,
			 Q, HEADER, Rec.length, Rec.data);

  /* update page R header */
  rHeader.node_type = node_type;
  rHeader.key_cnt = rightcnt;
  rHeader.max_key_len = max_key;
  VPID_SET_NULL (&rHeader.next_vpid);
  btree_write_node_header (&Rec, &rHeader);
  if (spage_insert_at (thread_p, R, HEADER, &Rec) != SP_SUCCESS)
    {
      goto exit_on_error;
    }

  /* log the new header record for redo purposes */
  log_append_redo_data2 (thread_p, RVBT_NDHEADER_INS, &btid->sys_btid->vfid,
			 R, HEADER, Rec.length, Rec.data);

  /* move the second half of root page P to page R */
  right = (leaf_page) ? rightcnt : (rightcnt + 1);
  for (i = 1; i <= right; i++)
    {
      if ((spage_get_record (P, midSlot_id + 1, &peek_rec, PEEK) !=
	   S_SUCCESS)
	  || ((sp_success = spage_insert_at (thread_p, R, i, &peek_rec)) !=
	      SP_SUCCESS)
	  || (spage_delete (thread_p, P, midSlot_id + 1) != midSlot_id + 1))
	{
	  goto exit_on_error;
	}
    }				/* for */

  /* for recovery purposes */
  recset_data = (char *) malloc (DB_PAGESIZE);
  if (recset_data == NULL)
    {
      goto exit_on_error;
    }

  /* Log page R records for redo purposes */
  ret =
    btree_rv_util_save_page_records (R, 1, right, 1, recset_data,
				     &recset_length);
  if (ret != NO_ERROR)
    {
      goto exit_on_error;
    }
  log_append_redo_data2 (thread_p, RVBT_INS_PGRECORDS, &btid->sys_btid->vfid,
			 R, -1, recset_length, recset_data);

  /* move the first half of root page P to page Q */
  left = leaf_page ? leftcnt : (leftcnt + 1);
  for (i = 1; i <= left; i++)
    {
      if (spage_get_record (P, 1, &peek_rec, PEEK) != S_SUCCESS)
	{
	  goto exit_on_error;
	}
      sp_success = spage_insert_at (thread_p, Q, i, &peek_rec);
      if (sp_success != SP_SUCCESS)
	{
	  goto exit_on_error;
	}
      if (spage_delete (thread_p, P, 1) != 1)
	{
	  goto exit_on_error;
	}
    }				/* for */

  /* Log page Q records for redo purposes */
  ret =
    btree_rv_util_save_page_records (Q, 1, left, 1, recset_data,
				     &recset_length);
  if (ret != NO_ERROR)
    {
      goto exit_on_error;
    }
  log_append_redo_data2 (thread_p, RVBT_INS_PGRECORDS, &btid->sys_btid->vfid,
			 Q, -1, recset_length, recset_data);

  /* Log deletion of all page P records (except the header!!)
   * for redo purposes
   */
  recset_header.rec_cnt = nrecs - 1;
  recset_header.first_slotid = 1;
  log_append_redo_data2 (thread_p, RVBT_DEL_PGRECORDS, &btid->sys_btid->vfid,
			 P, -1, sizeof (RECSET_HEADER), &recset_header);

  /* update the root page P to keep the middle key and to point to
   * page Q and R.  Remember that this mid key will be on a non leaf page
   * regardless of whether we are splitting a leaf or non leaf page.
   */
  NLeaf_Rec.pnt = *Q_page_vpid;
  key_len = btree_get_key_length (mid_key);
  NLeaf_Rec.key_len = (key_len < BTREE_MAX_KEYLEN_INPAGE) ? key_len : -1;
  ret = btree_write_record (thread_p, btid, &NLeaf_Rec, mid_key, false,
			    (NLeaf_Rec.key_len == -1), key_len,
			    false, NULL, NULL, &Rec);
  if (ret != NO_ERROR)
    {
      goto exit_on_error;
    }

  if (spage_insert_at (thread_p, P, 1, &Rec) != SP_SUCCESS)
    {
      goto exit_on_error;
    }

  /* log the inserted record for undo/redo purposes, */
  *(INT16 *) ((char *) recset_data + OFFS1) = 1;	/* NonLeaf Record */
  *(INT16 *) ((char *) recset_data + OFFS2) = Rec.type;
  memcpy ((char *) recset_data + OFFS3, Rec.data, Rec.length);

  log_addr_offset = 1;
  log_append_undoredo_data2 (thread_p, RVBT_NDRECORD_INS,
			     &btid->sys_btid->vfid, P, log_addr_offset,
			     sizeof (log_addr_offset), Rec.length + OFFS3,
			     &log_addr_offset, recset_data);

  NLeaf_Rec.pnt = *R_page_vpid;
  key_len = btree_get_key_length (mid_key);
  NLeaf_Rec.key_len = (key_len < BTREE_MAX_KEYLEN_INPAGE) ? key_len : -1;
  ret = btree_write_record (thread_p, btid, &NLeaf_Rec, mid_key, false,
			    (NLeaf_Rec.key_len == -1), key_len,
			    false, NULL, NULL, &Rec);
  if (ret != NO_ERROR)
    {
      goto exit_on_error;
    }

  if (spage_insert_at (thread_p, P, 2, &Rec) != SP_SUCCESS)
    {
      goto exit_on_error;
    }

  /* log the inserted record for undo/redo purposes, */
  *(INT16 *) ((char *) recset_data + OFFS1) = 1;	/* NonLeaf Record */
  *(INT16 *) ((char *) recset_data + OFFS2) = Rec.type;
  memcpy ((char *) recset_data + OFFS3, Rec.data, Rec.length);

  log_addr_offset = 2;
  log_append_undoredo_data2 (thread_p, RVBT_NDRECORD_INS,
			     &btid->sys_btid->vfid, P, log_addr_offset,
			     sizeof (log_addr_offset), Rec.length + OFFS3,
			     &log_addr_offset, recset_data);

  /* find the child page to be followed */

  c = (*(btid->nonleaf_key_type->type->cmpval))
    (key, mid_key, btid->key_type, btid->reverse, 0, 1, NULL);

  if (c <= 0)
    {
      page_vpid = *Q_page_vpid;
    }
  else
    {
      page_vpid = *R_page_vpid;
    }

  if (mid_key != key)
    {
      btree_clear_key_value (&clear_midkey, mid_key);
      db_private_free_and_init (thread_p, mid_key);
      mid_key = NULL;
    }

  pgbuf_set_dirty (thread_p, P, DONT_FREE);
  pgbuf_set_dirty (thread_p, Q, DONT_FREE);
  pgbuf_set_dirty (thread_p, R, DONT_FREE);
  free_and_init (Rec.data);
  free_and_init (recset_data);

  /* set child page identifier */
  *child_vpid = page_vpid;

end:

  return ret;

exit_on_error:

  if (recset_data)
    {
      free_and_init (recset_data);
    }
  if (Rec.data)
    {
      free_and_init (Rec.data);
    }
  if (mid_key)
    {
      db_private_free_and_init (thread_p, mid_key);
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

/*
 * btree_insert () -
 *   return: (the key to be inserted or NULL)
 *   btid(in): B+tree index identifier
 *   key(in): Key to be inserted
 *   cls_oid(in): To find out the lock mode of corresponding class.
 *                The lock mode is used to make a decision about if the key
 *                range locking must be performed.
 *   oid(in): Object identifier to be inserted for the key
 *   op_type(in): operation types
 *                SINGLE_ROW_INSERT, SINGLE_ROW_UPDATE, SINGLE_ROW_MODIFY
 *                MULTI_ROW_INSERT, MULTI_ROW_UPDATE
 *   unique_stat_info(in):
 *            When multiple rows are inserted, unique_stat_info maintains
 *            the local statistical infomation related to uniqueness checking
 *            such as num_nulls, num_keys, and num_oids, and that is locally
 *            updated during the process of one INSERT or UPDATE statement.
 *            After those rows are inserted correctly, the local information
 *            would be reflected into global information saved in root page.
 *   unique(in):
 *
 * Note: If the key is new, the < key, oid > pair is inserted into
 * the B+tree index structure, otherwise the value oid is added
 * to the existing set of values for the key. The key must be
 * of type DB_TYPE_STRING, DB_TYPE_SHORT, DB_TYPE_INTEGER,
 * DB_TYPE_DOUBLE, DB_TYPE_OBJECT, DB_TYPE_FLOAT, DB_TYPE_TIME,
 * DB_TYPE_UTIME, DB_TYPE_DATE or DB_TYPE_MONETARY.
 * If  the key is of type DB_TYPE_STRING, it should be supplied
 * with a terminating null character. The string(character-based)
 * keys are handled in a case-sensitive manner.
 * There is virtually no limit to the number of values that can
 * be stored for a key.
 *
 * The node split operations are done while traversing the tree
 * from top to bottom to eliminate the upward propagation of node
 * split operations that may cause deadlocks. While accessing an
 * index page P (holding an exclusive X lock on it), if the
 * insertion may result in the split of page Q, the child page of
 * P which is to be accessed next. If so, the page Q is split
 * into two pages: Q and R, a newly allocated page, and an entry
 * is inserted into parent page P to point to the page R. Both
 * pages Q and R are kept in exculsive X lock mode. The split
 * point of the page is found by considering the length of the
 * records it contains and the length of the key to be inserted.
 * If a leaf page is split and it is realized that the key can
 * not fit into the page it is to be inserted, a new page is
 * allocated for the key and an entry is inserted to the parent
 * page P to point to the newly allocated page. Each leaf page
 * record contains a key and a set of values. If a leaf page
 * record  becomes too big to fit into a page, then overflow
 * pages are used to store the record. In the non_leaf pages part
 * of the index, only key separators (prefixes) that are adequate
 * to guide the search are stored for performance reasons.
 */
DB_VALUE *
btree_insert (THREAD_ENTRY * thread_p, BTID * btid, DB_VALUE * key,
	      OID * cls_oid, OID * oid, int op_type,
	      BTREE_UNIQUE_STATS * unique_stat_info, int *unique)
{
  VPID P_vpid, Q_vpid, R_vpid, child_vpid;
  PAGE_PTR P = NULL, Q = NULL, R = NULL;
  RECDES peek_rec, copy_rec, copy_rec1;
  BTREE_ROOT_HEADER root_header;
  int key_len, max_key, max_entry;
  INT16 p_slot_id, qSlot_id;
  int top_op_active = 0;
  bool leaf_page;
  int max_free, keys;
  PAGEID_STRUCT pageid_struct;	/* for recovery purposes */
  int add_key;
  BTID_INT btid_int;
  int NextPageFlag = false;
  int NextLockFlag = false;
  OID class_oid;
  LOCK class_lock = NULL_LOCK;
  int tran_index;
  int nextkey_lock_request;
  int offset;
  bool dummy;
  int ret_val;
  LEAF_REC Leaf_Pnt;
  PAGE_PTR N = NULL;
  char *header_ptr;
  INT16 node_type;
  INT16 key_cnt;
  VPID next_vpid;
  VPID N_vpid;
  OID N_oid, Saved_N_oid;
  INT16 nSlot_id;
  char *rec_oid_ptr;
  LOG_LSA Saved_pLSA, Saved_nLSA;
  LOG_LSA *temp_lsa;
  int do_unique_check;
  OID N_class_oid;
  OID Saved_N_class_oid;
  PAGE_PTR temp_page = NULL;
  BTREE_NODE_HEADER nHeader;

  copy_rec.data = NULL;
  copy_rec1.data = NULL;

#if defined(BTREE_DEBUG)
  if (BTREE_INVALID_INDEX_ID (btid))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_BTREE_INVALID_INDEX_ID, 3,
	      btid->vfid.fileid, btid->vfid.volid, btid->root_pageid);
      goto error;
    }
#endif /* BTREE_DEBUG */

  P_vpid.volid = btid->vfid.volid;	/* read the root page */
  P_vpid.pageid = btid->root_pageid;
  P = pgbuf_fix (thread_p, &P_vpid, OLD_PAGE, PGBUF_LATCH_WRITE,
		 PGBUF_UNCONDITIONAL_LATCH);
  if (P == NULL)
    {
      goto error;
    }

  /* free space in the root node */
  max_free = spage_max_space_for_new_record (thread_p, P);

  /* read the header record */
  if (spage_get_record (P, HEADER, &peek_rec, PEEK) != S_SUCCESS)
    goto error;

  btree_read_root_header (&peek_rec, &root_header);
  btid_int.sys_btid = btid;
  if (btree_glean_root_header_info (&root_header, &btid_int) != NO_ERROR)
    {
      goto error;
    }

  if (unique)
    {
      *unique = btid_int.unique;
    }

  leaf_page = (root_header.node.node_type == LEAF_NODE) ? true : false;
  keys = root_header.node.key_cnt;
  /* if root is a non leaf node, the number of keys is actually one greater */
  keys = (leaf_page) ? keys : keys + 1;

  key_len = btree_get_key_length (key);
  if (key_len >= BTREE_MAX_KEYLEN_INPAGE)
    {
      key_len = DISK_VPID_SIZE;
    }

  max_key = root_header.node.max_key_len;

  if (key_len > max_key)
    {
      /* new key is longer than all the keys in index */
      copy_rec.area_size = DB_PAGESIZE;
      copy_rec.data = (char *) malloc (DB_PAGESIZE);
      if (copy_rec.data == NULL)
	{
	  goto error;
	}

      copy_rec1.area_size = DB_PAGESIZE;
      copy_rec1.data = (char *) malloc (DB_PAGESIZE);
      if (copy_rec1.data == NULL)
	{
	  goto error;
	}

      /* save root head for undo purposes */
      btree_rv_save_root_head (root_header.node.max_key_len, 0, 0, 0,
			       &copy_rec1);

      root_header.node.max_key_len = key_len;
      max_key = key_len;

      /* update the root header */
      btree_write_root_header (&copy_rec, &root_header);

      log_append_undoredo_data2 (thread_p, RVBT_ROOTHEADER_UPD, &btid->vfid,
				 P, HEADER, copy_rec1.length, copy_rec.length,
				 copy_rec1.data, copy_rec.data);

      if (spage_update (thread_p, P, HEADER, &copy_rec) != SP_SUCCESS)
	{
	  goto error;
	}

      pgbuf_set_dirty (thread_p, P, DONT_FREE);
      free_and_init (copy_rec.data);
      free_and_init (copy_rec1.data);
    }

  if (key && DB_VALUE_DOMAIN_TYPE (key) == DB_TYPE_MIDXKEY)
    {
      key->data.midxkey.domain = btid_int.key_type;	/* set complete setdomain */
    }

  if (key == NULL || db_value_is_null (key)
      || btree_multicol_key_is_null (key))
    {
      /* update root header statistics if it's a unique Btree
       * and the transaction is active.
       *
       * unique statitistics for non null keys will be updated after
       * we find out if we have a new key or not.
       */
      if (logtb_is_current_active (thread_p) && BTREE_IS_UNIQUE (&btid_int))
	{
	  if (op_type == SINGLE_ROW_INSERT || op_type == SINGLE_ROW_UPDATE ||
	      op_type == SINGLE_ROW_MODIFY)
	    {
	      root_header.num_nulls++;
	      root_header.num_oids++;
	      copy_rec.area_size = DB_PAGESIZE;
	      copy_rec.data = (char *) malloc (DB_PAGESIZE);
	      if (copy_rec.data == NULL)
		{
		  goto error;
		}

	      copy_rec1.data = (char *) malloc (DB_PAGESIZE);
	      copy_rec1.area_size = DB_PAGESIZE;
	      if (copy_rec1.data == NULL)
		{
		  goto error;
		}

	      /* save root head for undo purposes */
	      btree_rv_save_root_head (root_header.node.max_key_len, -1, -1,
				       0, &copy_rec1);

	      /* update the root header */
	      btree_write_root_header (&copy_rec, &root_header);

	      log_append_undoredo_data2 (thread_p, RVBT_ROOTHEADER_UPD,
					 &btid->vfid, P, HEADER,
					 copy_rec1.length, copy_rec.length,
					 copy_rec1.data, copy_rec.data);

	      if (spage_update (thread_p, P, HEADER, &copy_rec) != SP_SUCCESS)
		{
		  goto error;
		}

	      pgbuf_set_dirty (thread_p, P, DONT_FREE);
	      free_and_init (copy_rec.data);
	      free_and_init (copy_rec1.data);
	    }
	  else
	    {			/* MULTI_ROW_INSERT, MULTI_ROW_UPDATE */
	      if (unique_stat_info == NULL)
		{
		  goto error;
		}
	      unique_stat_info->num_nulls++;
	      unique_stat_info->num_oids++;
	    }
	}

      /* nothing more to do -- this is not an error */
      pgbuf_unfix (thread_p, P);
      P = NULL;
      return key;
    }

  /* decide whether key range locking must be performed.
   * if class_oid is transferred through a new argument,
   * this operation will be performed more efficiently.
   */
  if (cls_oid != NULL && !OID_ISNULL (cls_oid))
    {
      COPY_OID (&class_oid, cls_oid);
      /* cls_oid might be NULL_OID. But it does not make problem. */
    }
  else
    {
      if (logtb_is_current_active (thread_p) == true)
	{
	  if (heap_get_class_oid (thread_p, oid, &class_oid) == NULL)
	    {
	      goto error;
	    }
	}
      else
	{
	  OID_SET_NULL (&class_oid);
	}
    }

  if (logtb_is_current_active (thread_p) == true)
    {
      /* initialize Saved_N_oid */
      OID_SET_NULL (&Saved_N_oid);
      OID_SET_NULL (&Saved_N_class_oid);

      /* find the lock that is currently acquired on the class */
      tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
      class_lock = lock_get_object_lock (&class_oid, oid_Root_class_oid,
					 tran_index);

      /* get nextkey_lock_request from the class lock mode */
      switch (class_lock)
	{
	case X_LOCK:
	case SIX_LOCK:
	case IX_LOCK:
	  nextkey_lock_request = true;
	  break;
	case S_LOCK:
	case IS_LOCK:
	case NULL_LOCK:
	default:
	  goto error;
	}

      if (!BTREE_IS_UNIQUE (&btid_int))
	{
	  if (class_lock == X_LOCK)
	    {
	      nextkey_lock_request = false;
	    }
	  else
	    {
	      nextkey_lock_request = true;
	    }
	}
    }
  else
    {
      /* total rollback, partial rollback, undo phase in recovery */
      nextkey_lock_request = false;
    }

  COPY_OID (&N_class_oid, &class_oid);

start_point:

  if (NextLockFlag == true)
    {
      P_vpid.volid = btid->vfid.volid;	/* read the root page */
      P_vpid.pageid = btid->root_pageid;
      P = pgbuf_fix (thread_p, &P_vpid, OLD_PAGE, PGBUF_LATCH_WRITE,
		     PGBUF_UNCONDITIONAL_LATCH);
      if (P == NULL)
	{
	  goto error;
	}

      /* free space in the root node */
      max_free = spage_max_space_for_new_record (thread_p, P);

      /* read the header record */
      if (spage_get_record (P, HEADER, &peek_rec, PEEK) != S_SUCCESS)
	{
	  goto error;
	}

      keys = BTREE_GET_NODE_KEY_CNT (peek_rec.data);
      if (BTREE_GET_NODE_TYPE (peek_rec.data) == LEAF_NODE)
	{
	  leaf_page = true;
	}
      else
	{
	  leaf_page = false;
	  /* if root is a non leaf node, 
	   * the number of keys is actually one greater
	   */
	  keys += 1;
	}

      max_key = BTREE_GET_NODE_MAX_KEY_LEN (peek_rec.data);
    }

  /* find the maximum entry size that may need to be inserted to the root */
  max_entry = (leaf_page) ? (2 * LEAFENTSZ (max_key)) : NLEAFENTSZ (max_key);
  /* slotted page overhead */
  max_entry += INT_ALIGNMENT +	/* sphdr->alignment */
    (sizeof (int) * 3);		/* sizeof(struct spage_slot) */

  /* there is a need to split the root, only if there is not enough space
   * for a new entry and either there are more than one record or else
   * the root is a leaf node and a non_existent key is to be inserted.
   *
   * in no case should a split happen if the node is currently empty
   * (keys == 0).  this can happen with large keys (greater than half
   * the page size).
   */
  if ((max_entry > max_free)
      && (keys != 0)
      && ((keys > 1)
	  || (leaf_page
	      && !btree_search_leaf_page (thread_p, &btid_int, P, key,
					  &p_slot_id))))
    {
      /* start system top operation */
      log_start_system_op (thread_p);
      top_op_active = 1;

      /* get two new pages */
      Q = btree_get_new_page (thread_p, &btid_int, &Q_vpid, &P_vpid);
      if (Q == NULL)
	{
	  goto error;
	}

      /* log the newly allocated pageid for deallocation for undo purposes */
      if (file_new_isvalid (thread_p, &btid->vfid) == DISK_INVALID)
	{
	  /* we don't do undo logging for new files */
	  pageid_struct.vpid = Q_vpid;
	  pageid_struct.vfid.fileid = btid->vfid.fileid;
	  pageid_struct.vfid.volid = btid->vfid.volid;
	  log_append_undo_data2 (thread_p, RVBT_NEW_PGALLOC, &btid->vfid,
				 NULL, -1, sizeof (PAGEID_STRUCT),
				 &pageid_struct);
	}

      R = btree_get_new_page (thread_p, &btid_int, &R_vpid, &P_vpid);
      if (R == NULL)
	{
	  goto error;
	}

      /* log the newly allocated pageid for deallocation for undo purposes */
      if (file_new_isvalid (thread_p, &btid->vfid) == DISK_INVALID)
	{
	  /* we don't do undo logging for new files */
	  pageid_struct.vpid = R_vpid;
	  log_append_undo_data2 (thread_p, RVBT_NEW_PGALLOC, &btid->vfid,
				 NULL, -1, sizeof (PAGEID_STRUCT),
				 &pageid_struct);
	}

      /* split the root P into two pages Q and R */
      if (btree_split_root
	  (thread_p, &btid_int, P, Q, R, &P_vpid, &Q_vpid, &R_vpid, leaf_page,
	   key, &child_vpid) != NO_ERROR)
	{
	  goto error;
	}

      pgbuf_unfix (thread_p, P);
      P = NULL;

      if (VPID_EQ (&child_vpid, &Q_vpid))
	{
	  /* child page to be followed is page Q */
	  pgbuf_unfix (thread_p, R);
	  R = NULL;

	  if (file_new_isvalid (thread_p, &btid->vfid) == DISK_VALID)
	    {			/* New B+tree ? */
	      log_end_system_op (thread_p, LOG_RESULT_TOPOP_ATTACH_TO_OUTER);
	    }
	  else
	    {
	      log_end_system_op (thread_p, LOG_RESULT_TOPOP_COMMIT);
	    }

	  top_op_active = 0;

	  P = Q;
	  Q = NULL;
	  P_vpid = Q_vpid;
	}
      else if (VPID_EQ (&child_vpid, &R_vpid))
	{
	  /* child page to be followed is page R */
	  pgbuf_unfix (thread_p, Q);
	  Q = NULL;

	  if (file_new_isvalid (thread_p, &btid->vfid) == DISK_VALID)
	    {			/* New B+tree ? */
	      log_end_system_op (thread_p, LOG_RESULT_TOPOP_ATTACH_TO_OUTER);
	    }
	  else
	    {
	      log_end_system_op (thread_p, LOG_RESULT_TOPOP_COMMIT);
	    }

	  top_op_active = 0;

	  P = R;
	  R = NULL;
	  P_vpid = R_vpid;
	}
      else
	{
	  pgbuf_unfix (thread_p, R);
	  R = NULL;
	  pgbuf_unfix (thread_p, Q);
	  Q = NULL;

	  if (file_new_isvalid (thread_p, &btid->vfid) == DISK_VALID)
	    {			/* New B+tree ? */
	      log_end_system_op (thread_p, LOG_RESULT_TOPOP_ATTACH_TO_OUTER);
	    }
	  else
	    {
	      log_end_system_op (thread_p, LOG_RESULT_TOPOP_COMMIT);
	    }

	  top_op_active = 0;

	  P_vpid = child_vpid;
	  P = pgbuf_fix (thread_p, &P_vpid, OLD_PAGE, PGBUF_LATCH_WRITE,
			 PGBUF_UNCONDITIONAL_LATCH);
	  if (P == NULL)
	    {
	      goto error;
	    }
	}
    }

  /* get the header record */
  btree_get_header_ptr (P, &header_ptr);
  node_type = BTREE_GET_NODE_TYPE (header_ptr);
  key_cnt = BTREE_GET_NODE_KEY_CNT (header_ptr);
  BTREE_GET_NODE_NEXT_VPID (header_ptr, &next_vpid);

  while (node_type == NON_LEAF_NODE)
    {

      /* find and get the child page to be followed */
      if (btree_search_nonleaf_page
	  (thread_p, &btid_int, P, key, &p_slot_id, &Q_vpid) != NO_ERROR)
	{
	  goto error;
	}
      Q = pgbuf_fix (thread_p, &Q_vpid, OLD_PAGE, PGBUF_LATCH_WRITE,
		     PGBUF_UNCONDITIONAL_LATCH);
      if (Q == NULL)
	{
	  goto error;
	}

      max_free = spage_max_space_for_new_record (thread_p, Q);

      /* read the header record */
      if (spage_get_record (Q, HEADER, &peek_rec, PEEK) != S_SUCCESS)
	{
	  goto error;
	}

      leaf_page =
	(BTREE_GET_NODE_TYPE (peek_rec.data) == LEAF_NODE) ? true : false;
      keys = BTREE_GET_NODE_KEY_CNT (peek_rec.data);
      /* if Q is a non leaf node, the number of keys is actually one greater */
      keys = (leaf_page) ? keys : keys + 1;
      max_key = BTREE_GET_NODE_MAX_KEY_LEN (peek_rec.data);

      /* is new key longer than all in the subtree of child page Q ? */
      if (key_len > max_key)
	{
	  BTREE_PUT_NODE_MAX_KEY_LEN (peek_rec.data, key_len);
	  max_key = key_len;

	  /* log the new header record for redo purposes, there is no need
	   * to undo the change to the header record
	   */
	  log_append_redo_data2 (thread_p, RVBT_NDHEADER_UPD, &btid->vfid, Q,
				 HEADER, peek_rec.length, peek_rec.data);
	  pgbuf_set_dirty (thread_p, Q, DONT_FREE);
	}

      /* find the maximum entry size that may need to be inserted to Q */
      max_entry =
	(leaf_page) ? (2 * LEAFENTSZ (max_key)) : NLEAFENTSZ (max_key);

      /* slotted page overhead */
      max_entry += INT_ALIGNMENT +	/* sphdr->alignment */
	(sizeof (int) * 3);	/* sizeof(struct spage_slot) */

      /* there is a need to split Q, only if there is not enough space
       * for a new entry and either there are more than one record or else
       * the root is a leaf node and a non_existent key is to inserted
       *
       * in no case should a split happen if the node is currently empty
       * (keys == 0).  This can happen with large keys (greater than half
       * the page size).
       */
      if ((max_entry > max_free)
	  && (keys != 0)
	  && ((keys > 1)
	      || (leaf_page
		  && !btree_search_leaf_page (thread_p, &btid_int, Q, key,
					      &qSlot_id))))
	{

	  /* start system top operation */
	  log_start_system_op (thread_p);
	  top_op_active = 1;

	  /* split the page Q into two pages Q and R, and update parent page P */

	  R = btree_get_new_page (thread_p, &btid_int, &R_vpid, &Q_vpid);
	  if (R == NULL)
	    {
	      goto error;
	    }

	  /* Log the newly allocated pageid for deallocation for undo purposes */
	  if (file_new_isvalid (thread_p, &btid->vfid) == DISK_INVALID)
	    {
	      /* we don't do undo logging for new files */
	      pageid_struct.vpid = R_vpid;
	      pageid_struct.vfid.fileid = btid->vfid.fileid;
	      pageid_struct.vfid.volid = btid->vfid.volid;
	      log_append_undo_data2 (thread_p, RVBT_NEW_PGALLOC, &btid->vfid,
				     NULL, -1, sizeof (PAGEID_STRUCT),
				     &pageid_struct);
	    }

	  if (btree_split_node
	      (thread_p, &btid_int, P, Q, R, &P_vpid, &Q_vpid, &R_vpid,
	       p_slot_id, leaf_page, key, &child_vpid) != NO_ERROR)
	    {
	      goto error;
	    }

	  if (VPID_EQ (&child_vpid, &Q_vpid))
	    {
	      /* child page to be followed is Q */
	      pgbuf_unfix (thread_p, R);
	      R = NULL;

	      if (file_new_isvalid (thread_p, &btid->vfid) == DISK_VALID)
		{		/* New B+tree ? */
		  log_end_system_op (thread_p,
				     LOG_RESULT_TOPOP_ATTACH_TO_OUTER);
		}
	      else
		{
		  log_end_system_op (thread_p, LOG_RESULT_TOPOP_COMMIT);
		}

	      top_op_active = 0;

	    }
	  else if (VPID_EQ (&child_vpid, &R_vpid))
	    {
	      /* child page to be followed is R */
	      pgbuf_unfix (thread_p, Q);
	      Q = NULL;

	      if (file_new_isvalid (thread_p, &btid->vfid) == DISK_VALID)
		{		/* New B+tree ? */
		  log_end_system_op (thread_p,
				     LOG_RESULT_TOPOP_ATTACH_TO_OUTER);
		}
	      else
		{
		  log_end_system_op (thread_p, LOG_RESULT_TOPOP_COMMIT);
		}

	      top_op_active = 0;

	      Q = R;
	      R = NULL;
	      Q_vpid = R_vpid;
	    }
	  else
	    {
	      pgbuf_unfix (thread_p, Q);
	      Q = NULL;
	      pgbuf_unfix (thread_p, R);
	      R = NULL;

	      if (file_new_isvalid (thread_p, &btid->vfid) == DISK_VALID)
		{		/* New B+tree ? */
		  log_end_system_op (thread_p,
				     LOG_RESULT_TOPOP_ATTACH_TO_OUTER);
		}
	      else
		{
		  log_end_system_op (thread_p, LOG_RESULT_TOPOP_COMMIT);
		}

	      top_op_active = 0;

	      Q_vpid = child_vpid;
	      Q = pgbuf_fix (thread_p, &Q_vpid, OLD_PAGE, PGBUF_LATCH_WRITE,
			     PGBUF_UNCONDITIONAL_LATCH);
	      if (Q == NULL)
		{
		  goto error;
		}
	    }
	}

      /* release parent page P, and repeat the same operations from child
       * page Q on
       */
      pgbuf_unfix (thread_p, P);
      P = Q;
      Q = NULL;
      P_vpid = Q_vpid;

      /* node_type must be recalculated */
      btree_get_header_ptr (P, &header_ptr);
      node_type = BTREE_GET_NODE_TYPE (header_ptr);
      key_cnt = BTREE_GET_NODE_KEY_CNT (header_ptr);
      BTREE_GET_NODE_NEXT_VPID (header_ptr, &next_vpid);
    }				/* while */

  /* find next OID for range locking */
  if (nextkey_lock_request == false)
    {
      goto key_insertion;
    }

  /* find next key */
  if (btree_search_leaf_page (thread_p, &btid_int, P, key, &p_slot_id))
    {
      /* key has been found
       * In a key insertion, if the key that having the same key value
       * with the key to be inserted, the next-key is the key.
       */
      nSlot_id = p_slot_id;
    }
  else
    {
      /* key has not been found */
      if (p_slot_id == NULL_SLOTID)
	{
	  goto error;
	}

      if (p_slot_id > key_cnt)
	{
	  nSlot_id = 1;
	  NextPageFlag = true;
	}
      else
	{
	  nSlot_id = p_slot_id;
	}
    }

  /* get the next OID */
  if (NextPageFlag == true)
    {
      /* The next key exists in the next leaf page */
      N_vpid = next_vpid;

    get_next_oid:

      if (VPID_ISNULL (&N_vpid))
	{			/* next page does not exist */
	  NextPageFlag = false;	/* reset NextPageFlag */
	  /* The first entry of the root page is used as the next OID */
	  N_oid.volid = btid->vfid.volid;
	  N_oid.pageid = btid->root_pageid;
	  N_oid.slotid = -1;
	  N_class_oid.volid = btid->vfid.volid;
	  N_class_oid.pageid = btid->root_pageid;
	  N_class_oid.slotid = 0;
	  if (temp_page != NULL)
	    {
	      pgbuf_unfix (thread_p, temp_page);
	      temp_page = NULL;
	    }
	}
      else
	{			/* next page exists */
	  /* I think that the page lock, S_LOCK, is sufficient. */
	  N = pgbuf_fix (thread_p, &N_vpid, OLD_PAGE, PGBUF_LATCH_READ,
			 PGBUF_UNCONDITIONAL_LATCH);
	  if (N == NULL)
	    {
	      if (temp_page != NULL)
		{
		  pgbuf_unfix (thread_p, temp_page);
		  temp_page = NULL;
		}
	      goto error;
	    }
	  if (temp_page != NULL)
	    {
	      pgbuf_unfix (thread_p, temp_page);
	      temp_page = NULL;
	    }
	  if (spage_number_of_records (N) == 1)
	    {			/* empty leaf page */
	      btree_get_header_ptr (N, &header_ptr);
	      BTREE_GET_NODE_NEXT_VPID (header_ptr, &N_vpid);

	      temp_page = N;
	      goto get_next_oid;
	    }
	  if (spage_get_record (N, nSlot_id, &peek_rec, PEEK) != S_SUCCESS)
	    {
	      goto error;
	    }
	  btree_read_record (thread_p, &btid_int, &peek_rec, NULL, &Leaf_Pnt,
			     true, &dummy, &offset, 0);
	  rec_oid_ptr = peek_rec.data + offset;
	  if (BTREE_IS_UNIQUE (&btid_int))
	    {			/* unique index */
	      OR_GET_OID (rec_oid_ptr, &N_class_oid);
	      rec_oid_ptr += OR_OID_SIZE;
	      OR_GET_OID (rec_oid_ptr, &N_oid);
	      if (OID_EQ (&N_class_oid, &class_oid) && class_lock == X_LOCK)
		{
		  if (NextLockFlag == true)
		    {
		      (void) lock_unlock_object (thread_p, &Saved_N_oid,
						 &Saved_N_class_oid, NX_LOCK,
						 true);
		      NextLockFlag = false;
		      OID_SET_NULL (&Saved_N_oid);
		      OID_SET_NULL (&Saved_N_class_oid);
		    }
		  pgbuf_unfix (thread_p, N);
		  N = NULL;
		  goto key_insertion;
		}
	    }
	  else
	    {			/* non-unique index */
	      OR_GET_OID (rec_oid_ptr, &N_oid);
	      COPY_OID (&N_class_oid, &class_oid);
	    }
	}
    }
  else
    {				/* NextPageFlag == false */
      if (spage_get_record (P, nSlot_id, &peek_rec, PEEK) != S_SUCCESS)
	{
	  goto error;
	}
      btree_read_record (thread_p, &btid_int, &peek_rec, NULL, &Leaf_Pnt,
			 true, &dummy, &offset, 0);
      rec_oid_ptr = peek_rec.data + offset;
      if (BTREE_IS_UNIQUE (&btid_int))
	{			/* unique index */
	  OR_GET_OID (rec_oid_ptr, &N_class_oid);
	  rec_oid_ptr += OR_OID_SIZE;
	  OR_GET_OID (rec_oid_ptr, &N_oid);
	  if (OID_EQ (&N_class_oid, &class_oid) && class_lock == X_LOCK)
	    {
	      if (NextLockFlag == true)
		{
		  (void) lock_unlock_object (thread_p, &Saved_N_oid,
					     &Saved_N_class_oid, NX_LOCK,
					     true);
		  NextLockFlag = false;
		  OID_SET_NULL (&Saved_N_oid);
		  OID_SET_NULL (&Saved_N_class_oid);
		}
	      goto key_insertion;
	    }
	}
      else
	{			/* non-unique index */
	  OR_GET_OID (rec_oid_ptr, &N_oid);
	  COPY_OID (&N_class_oid, &class_oid);
	}
    }

  if (NextLockFlag == true)
    {
      if (OID_EQ (&Saved_N_oid, &N_oid))
	{
	  if (NextPageFlag == true)
	    {
	      pgbuf_unfix (thread_p, N);
	      N = NULL;
	    }
	  goto key_insertion;
	}
      (void) lock_unlock_object (thread_p, &Saved_N_oid, &Saved_N_class_oid,
				 NX_LOCK, true);
      NextLockFlag = false;
      OID_SET_NULL (&Saved_N_oid);
      OID_SET_NULL (&Saved_N_class_oid);
    }

  /* CONDITIONAL lock request */
  ret_val =
    lock_hold_object_instant (thread_p, &N_oid, &N_class_oid, NX_LOCK);

  if (ret_val == LK_GRANTED)
    {
      if (NextPageFlag == true)
	{
	  pgbuf_unfix (thread_p, N);
	  N = NULL;
	}
    }
  else if (ret_val == LK_NOTGRANTED)
    {
      /* save some information for validation checking
       * after UNCONDITIONAL lock request
       */
      temp_lsa = pgbuf_get_lsa (P);
      LSA_COPY (&Saved_pLSA, temp_lsa);
      pgbuf_unfix (thread_p, P);
      P = NULL;
      if (NextPageFlag == true)
	{
	  temp_lsa = pgbuf_get_lsa (N);
	  LSA_COPY (&Saved_nLSA, temp_lsa);
	  pgbuf_unfix (thread_p, N);
	  N = NULL;
	}
      COPY_OID (&Saved_N_oid, &N_oid);
      COPY_OID (&Saved_N_class_oid, &N_class_oid);

      /* UNCONDITIONAL lock request */
      ret_val =
	lock_object (thread_p, &N_oid, &N_class_oid, NX_LOCK, LK_UNCOND_LOCK);
      if (ret_val != LK_GRANTED)
	{
	  goto error;
	}
      NextLockFlag = true;

      /* validation checking after the unconditional lock acquisition
       * in this implementation, only PageLSA of the page is checked.
       * it means that if the PageLSA has not been changed,
       * the page image does not changed
       * during the unconditional next key lock acquisition.
       * so, the next lock that is acquired is valid.
       * if we give more accurate and precise checking condition,
       * the operation that traverse the tree can be reduced.
       */

      P = pgbuf_fix (thread_p, &P_vpid, OLD_PAGE, PGBUF_LATCH_WRITE,
		     PGBUF_UNCONDITIONAL_LATCH);
      if (P == NULL)
	{
	  goto error;
	}

      temp_lsa = pgbuf_get_lsa (P);
      if (!LSA_EQ (&Saved_pLSA, temp_lsa))
	{
	  pgbuf_unfix (thread_p, P);
	  P = NULL;
	  NextPageFlag = false;
	  goto start_point;
	}

      /* The first leaf page is valid */

      if (NextPageFlag == true)
	{
	  N = pgbuf_fix (thread_p, &N_vpid, OLD_PAGE, PGBUF_LATCH_READ,
			 PGBUF_UNCONDITIONAL_LATCH);
	  if (N == NULL)
	    {
	      goto error;
	    }

	  temp_lsa = pgbuf_get_lsa (N);
	  if (!LSA_EQ (&Saved_nLSA, temp_lsa))
	    {
	      pgbuf_unfix (thread_p, P);
	      P = NULL;
	      pgbuf_unfix (thread_p, N);
	      N = NULL;
	      NextPageFlag = false;
	      goto start_point;
	    }

	  /* The next leaf page is valid */

	  pgbuf_unfix (thread_p, N);
	  N = NULL;
	}

      /* valid point for key insertion
       * only the page P is currently locked and fetched
       */
    }
  else
    {
      goto error;
    }

key_insertion:

  /* a leaf page is reached, make the actual insertion in this page.
   * Because of the specific top-down splitting algorithm, there will be
   * no need to go up to parent pages, and it will always be possible to
   * make the insertion in this leaf page.
   */
  add_key = 0;

  if (logtb_is_current_active (thread_p) && BTREE_IS_UNIQUE (&btid_int))
    {
      if (op_type == SINGLE_ROW_INSERT || op_type == MULTI_ROW_INSERT ||
	  op_type == SINGLE_ROW_UPDATE)
	{
	  do_unique_check = true;
	}
      else
	{			/* SINGLE_ROW_MODIFY || MULTI_ROW_UPDATE */
	  do_unique_check = false;
	}
    }
  else
    {				/* in recovery || non-unique index */
      do_unique_check = false;
    }

  if (btree_insert_into_leaf (thread_p, &btid_int, P, key, &class_oid, oid,
			      &P_vpid, &add_key, do_unique_check) != NO_ERROR)
    {
      goto error;
    }

  pgbuf_unfix (thread_p, P);
  P = NULL;

  /* update root header statistics if its a Btree for uniques.
   * this only wants to be done if the transaction is active.  That
   * is, if we are aborting a transaction the statistics are "rolled
   * back by their own logging.
   */
  if (logtb_is_current_active (thread_p) && BTREE_IS_UNIQUE (&btid_int))
    {
      if (op_type == SINGLE_ROW_INSERT || op_type == SINGLE_ROW_UPDATE ||
	  op_type == SINGLE_ROW_MODIFY)
	{
	  copy_rec.area_size = DB_PAGESIZE;
	  copy_rec1.area_size = DB_PAGESIZE;
	  copy_rec.data = (char *) malloc (DB_PAGESIZE);
	  if (copy_rec.data == NULL)
	    {
	      goto error;
	    }
	  copy_rec1.data = (char *) malloc (DB_PAGESIZE);
	  if (copy_rec1.data == NULL)
	    {
	      goto error;
	    }

	  P_vpid.volid = btid->vfid.volid;
	  P_vpid.pageid = btid->root_pageid;
	  P = pgbuf_fix (thread_p, &P_vpid, OLD_PAGE, PGBUF_LATCH_WRITE,
			 PGBUF_UNCONDITIONAL_LATCH);
	  if (P == NULL)
	    {
	      goto error;
	    }

	  /* read the header record */
	  if (spage_get_record (P, HEADER, &peek_rec, PEEK) != S_SUCCESS)
	    {
	      goto error;
	    }

	  btree_read_root_header (&peek_rec, &root_header);

	  /* save root head for undo purposes */
	  btree_rv_save_root_head (root_header.node.max_key_len, 0, -1,
				   -1 * add_key, &copy_rec1);

	  /* update the root header */
	  root_header.num_oids++;
	  if (add_key)
	    {
	      root_header.num_keys++;
	    }
	  btree_write_root_header (&copy_rec, &root_header);

	  log_append_undoredo_data2 (thread_p, RVBT_ROOTHEADER_UPD,
				     &btid->vfid, P, HEADER, copy_rec1.length,
				     copy_rec.length, copy_rec1.data,
				     copy_rec.data);

	  if (spage_update (thread_p, P, HEADER, &copy_rec) != SP_SUCCESS)
	    {
	      goto error;
	    }

	  pgbuf_set_dirty (thread_p, P, FREE);
	  P = NULL;
	  free_and_init (copy_rec.data);
	  free_and_init (copy_rec1.data);
	}
      else
	{
	  if (unique_stat_info == NULL)
	    {
	      goto error;
	    }
	  unique_stat_info->num_oids++;
	  if (add_key)
	    {
	      unique_stat_info->num_keys++;
	    }
	}
    }

  if (NextLockFlag == true)
    {
      (void) lock_unlock_object (thread_p, &N_oid, &N_class_oid, NX_LOCK,
				 true);
    }

  return key;

error:

  if (P)
    {
      pgbuf_unfix (thread_p, P);
      P = NULL;
    }
  if (Q)
    {
      pgbuf_unfix (thread_p, Q);
      Q = NULL;
    }
  if (R)
    {
      pgbuf_unfix (thread_p, R);
      R = NULL;
    }
  if (N)
    {
      pgbuf_unfix (thread_p, N);
      N = NULL;
    }
  if (NextLockFlag)
    {
      lock_unlock_object (thread_p, &N_oid, &N_class_oid, X_LOCK, true);
    }
  if (copy_rec.data)
    {
      free_and_init (copy_rec.data);
    }
  if (copy_rec1.data)
    {
      free_and_init (copy_rec1.data);
    }
  if (top_op_active)
    {
      log_end_system_op (thread_p, LOG_RESULT_TOPOP_ABORT);
    }

  return NULL;

}

/*
 * btree_update () -
 *   return: NO_ERROR
 *   btid(in): B+tree index identifier
 *   old_key(in): Old key value
 *   new_key(in): New key value
 *   cls_oid(in):
 *   oid(in): Object identifier to be updated
 *   op_type(in):
 *   unique_stat_info(in):
 *   unique(in):
 *
 * Note: Deletes the <old_key, oid> key-value pair from the B+tree
 * index and inserts the <new_key, oid> key-value pair to the
 * B+tree index which results in the update of the specified
 * index entry for the given object identifier.
 */
int
btree_update (THREAD_ENTRY * thread_p, BTID * btid, DB_VALUE * old_key,
	      DB_VALUE * new_key, OID * cls_oid, OID * oid, int op_type,
	      BTREE_UNIQUE_STATS * unique_stat_info, int *unique)
{
  int ret = NO_ERROR;

  if (btree_delete (thread_p, btid, old_key, cls_oid, oid, unique,
		    op_type, unique_stat_info) == NULL)
    {
      /* if the btree we are updating is a btree for unique attributes
       * it is possible that the btree update has already been performed
       * via the template unique checking.
       * In this case, we will ignore the error from btree_delete
       */
      if (*unique && er_errid () == ER_BTREE_UNKNOWN_KEY)
	{
	  goto end;
	}

      goto exit_on_error;
    }

  if (btree_insert (thread_p, btid, new_key, cls_oid, oid,
		    op_type, unique_stat_info, unique) == NULL)
    {
      goto exit_on_error;
    }

end:

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
 * btree_reflect_unique_statistics () -
 *   return: NO_ERROR
 *   unique_stat_info(in):
 *
 * Note: This function reflects the given local statistical
 * information into the global statistical information
 * saved in a root page of corresponding unique index.
 */
int
btree_reflect_unique_statistics (THREAD_ENTRY * thread_p,
				 BTREE_UNIQUE_STATS * unique_stat_info)
{
  VPID Root_vpid;
  PAGE_PTR Root = NULL;
  BTREE_ROOT_HEADER root_header;
  RECDES root_rec;
  RECDES undo_rec;
  RECDES redo_rec;
  char undo_data[ROOT_HEADER_FIXED_SIZE];
  char *redo_data = NULL;
  int ret = NO_ERROR;

  /* check if unique_stat_info is NULL */
  if (unique_stat_info == NULL)
    {
      goto exit_on_error;
    }

  redo_data = (char *) malloc (DB_PAGESIZE);
  if (redo_data == NULL)
    {
      goto exit_on_error;
    }

  /* fix the root page */
  Root_vpid.pageid = unique_stat_info->btid.root_pageid;
  Root_vpid.volid = unique_stat_info->btid.vfid.volid;
  Root = pgbuf_fix (thread_p, &Root_vpid, OLD_PAGE, PGBUF_LATCH_WRITE,
		    PGBUF_UNCONDITIONAL_LATCH);
  if (Root == NULL)
    {
      goto exit_on_error;
    }

  /* get the header record of the root page */
  if (spage_get_record (Root, HEADER, &root_rec, PEEK) != S_SUCCESS)
    {
      goto exit_on_error;
    }

  /* read the root information */
  btree_read_root_header (&root_rec, &root_header);

  if (logtb_is_current_active (thread_p) && (root_header.num_nulls != -1))
    {
      /* update header information */
      root_header.num_nulls += unique_stat_info->num_nulls;
      root_header.num_oids += unique_stat_info->num_oids;
      root_header.num_keys += unique_stat_info->num_keys;

      /* prepare memory space for logging root header information */
      undo_rec.area_size = ROOT_HEADER_FIXED_SIZE;
      undo_rec.data = &(undo_data[0]);
      redo_rec.area_size = DB_PAGESIZE;
      redo_rec.data = &(redo_data[0]);

      /* save root header for undo purposes */
      btree_rv_save_root_head (root_header.node.max_key_len,
			       -(unique_stat_info->num_nulls),
			       -(unique_stat_info->num_oids),
			       -(unique_stat_info->num_keys), &undo_rec);

      /* update the root header */
      btree_write_root_header (&redo_rec, &root_header);

      /* log the update with undo-redo record */
      log_append_undoredo_data2 (thread_p, RVBT_ROOTHEADER_UPD,
				 &(unique_stat_info->btid.vfid), Root, HEADER,
				 undo_rec.length, redo_rec.length,
				 undo_rec.data, redo_rec.data);

      /* update root header record */
      if (spage_update (thread_p, Root, HEADER, &redo_rec) != SP_SUCCESS)
	{
	  goto exit_on_error;
	}

      /* set the root page as dirty page */
      pgbuf_set_dirty (thread_p, Root, DONT_FREE);
    }

  /* free the root page */
  pgbuf_unfix (thread_p, Root);
  Root = NULL;

  /* deallocate the redo_data */
  free_and_init (redo_data);

end:

  return ret;

exit_on_error:

  if (Root != NULL)
    {
      pgbuf_unfix (thread_p, Root);
      Root = NULL;
    }
  if (redo_data != NULL)
    {
      free_and_init (redo_data);
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

/*
 * btree_locate_key () - Locate a key node and position
 *   return: int true: key_found, false: key_not found
 *               (if error, false and slot_id = NULL_SLOTID)
 *   btid_int(in): B+tree index identifier
 *   key(in): Key to locate
 *   pg_vpid(out): Set to the page identifier that contains the key or should
 *                 contain the key if the key was to be inserted.
 *   slot_id(out): Set to the number (position) of the record that contains the
 *                 key or would contain the key if the key was to be inserted.
 *   found(in):
 *
 * Note: Searchs the B+tree index to locate the page and record that
 * contains the key, or would contain the key if the key was to be located.
 */
static PAGE_PTR
btree_locate_key (THREAD_ENTRY * thread_p, BTID_INT * btid_int,
		  DB_VALUE * key, VPID * pg_vpid, INT16 * slot_id, int *found)
{
  PAGE_PTR P = NULL, Q = NULL;
  VPID P_vpid, Q_vpid;
  INT16 p_slot_id;
  char *header_ptr;
  INT16 node_type;
  RECDES Rec;

  *found = false;
  *slot_id = NULL_SLOTID;
#if defined(BTREE_DEBUG)
  if (key == NULL || db_value_is_null (key)
      || btree_multicol_key_is_null (key))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_BTREE_NULL_KEY, 0);
      goto error;
    }

  if (BTREE_INVALID_INDEX_ID (btid))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_BTREE_INVALID_INDEX_ID, 3,
	      btid->vfid.fileid, btid->vfid.volid, btid->root_pageid);
      goto error;
    }
#endif /* BTREE_DEBUG */

  P_vpid.volid = btid_int->sys_btid->vfid.volid;	/* read the root page */
  P_vpid.pageid = btid_int->sys_btid->root_pageid;
  P = pgbuf_fix (thread_p, &P_vpid, OLD_PAGE, PGBUF_LATCH_READ,
		 PGBUF_UNCONDITIONAL_LATCH);
  if (P == NULL)
    {
      goto error;
    }

  btree_get_header_ptr (P, &header_ptr);
  node_type = BTREE_GET_NODE_TYPE (header_ptr);

  while (node_type == NON_LEAF_NODE)
    {
      /* get the child page to follow */
      if (btree_search_nonleaf_page
	  (thread_p, btid_int, P, key, &p_slot_id, &Q_vpid) != NO_ERROR)
	{
	  goto error;
	}

      Q = pgbuf_fix (thread_p, &Q_vpid, OLD_PAGE, PGBUF_LATCH_READ,
		     PGBUF_UNCONDITIONAL_LATCH);
      if (Q == NULL)
	{
	  goto error;
	}

      pgbuf_unfix (thread_p, P);
      P = NULL;

      /* read the header record */
      btree_get_header_ptr (Q, &header_ptr);
      node_type = BTREE_GET_NODE_TYPE (header_ptr);

      P = Q;
      Q = NULL;
      P_vpid = Q_vpid;
    }

  /* leaf page is reached */
  *found = btree_search_leaf_page (thread_p, btid_int, P, key, slot_id);
  *pg_vpid = P_vpid;

  /* NOTE that we do NOT release the page latch on P here */
  return P;

error:

  if (P)
    {
      pgbuf_unfix (thread_p, P);
      P = NULL;
    }
  if (Q)
    {
      pgbuf_unfix (thread_p, Q);
      Q = NULL;
    }

  return NULL;
}

/*
 * btree_find_first_leaf () -
 *   return: page pointer
 *   btid(in):
 *   pg_vpid(in):
 *
 * Note: Find the page identifier for the first leaf page of the B+tree index.
 */
static PAGE_PTR
btree_find_first_leaf (THREAD_ENTRY * thread_p, BTID * btid, VPID * pg_vpid)
{
  PAGE_PTR P = NULL, Q = NULL;
  VPID P_vpid, Q_vpid;
  char *header_ptr;
  INT16 node_type;
  NON_LEAF_REC nleaf;
  RECDES Rec;

  VPID_SET_NULL (pg_vpid);

  /* read the root page */
  P_vpid.volid = btid->vfid.volid;
  P_vpid.pageid = btid->root_pageid;
  P = pgbuf_fix (thread_p, &P_vpid, OLD_PAGE, PGBUF_LATCH_READ,
		 PGBUF_UNCONDITIONAL_LATCH);
  if (P == NULL)
    {
      goto error;
    }

  btree_get_header_ptr (P, &header_ptr);
  node_type = BTREE_GET_NODE_TYPE (header_ptr);

  while (node_type == NON_LEAF_NODE)
    {
      /* get the first child page to follow */

      if (spage_number_of_records (P) <= 1)
	{			/* node record underflow */
	  er_log_debug (ARG_FILE_LINE, "btree_find_first_leaf: node key count"
			" underflow: %d.Operation Ignored.",
			spage_number_of_records (P) - 1);
	  goto error;
	}

      /* get the first record */
      if (spage_get_record (P, 1, &Rec, PEEK) != S_SUCCESS)
	{
	  goto error;
	}
      btree_read_fixed_portion_of_non_leaf_record (&Rec, &nleaf);
      Q_vpid = nleaf.pnt;
      Q = pgbuf_fix (thread_p, &Q_vpid, OLD_PAGE, PGBUF_LATCH_READ,
		     PGBUF_UNCONDITIONAL_LATCH);
      if (Q == NULL)
	{
	  goto error;
	}
      pgbuf_unfix (thread_p, P);
      P = NULL;

      btree_get_header_ptr (Q, &header_ptr);
      node_type = BTREE_GET_NODE_TYPE (header_ptr);

      P = Q;
      Q = NULL;
      P_vpid = Q_vpid;
    }

  /* leaf page is reached */
  *pg_vpid = P_vpid;

  /* NOTE that we do NOT release the page latch on P here */
  return P;

error:

  if (P)
    {
      pgbuf_unfix (thread_p, P);
      P = NULL;
    }
  if (Q)
    {
      pgbuf_unfix (thread_p, Q);
      Q = NULL;
    }

  return NULL;
}

/*
 * btree_find_last_leaf () -
 *   return: page pointer
 *   btid(in):
 *   pg_vpid(in):
 *
 * Note: Find the page identifier for the last leaf page of the B+tree index.
 */
static PAGE_PTR
btree_find_last_leaf (THREAD_ENTRY * thread_p, BTID * btid, VPID * pg_vpid)
{
  PAGE_PTR P = NULL, Q = NULL;
  VPID P_vpid, Q_vpid;
  char *header_ptr;
  INT16 node_type;
  NON_LEAF_REC nleaf;
  RECDES Rec;

  VPID_SET_NULL (pg_vpid);

  /* read the root page */
  P_vpid.volid = btid->vfid.volid;
  P_vpid.pageid = btid->root_pageid;
  P = pgbuf_fix (thread_p, &P_vpid, OLD_PAGE, PGBUF_LATCH_READ,
		 PGBUF_UNCONDITIONAL_LATCH);
  if (P == NULL)
    {
      goto error;
    }

  btree_get_header_ptr (P, &header_ptr);
  node_type = BTREE_GET_NODE_TYPE (header_ptr);

  while (node_type == NON_LEAF_NODE)
    {
      /* get the first child page to follow */

      if (spage_number_of_records (P) <= 1)
	{			/* node record underflow */
	  er_log_debug (ARG_FILE_LINE, "btree_find_first_leaf: node key count"
			" underflow: %d.Operation Ignored.",
			spage_number_of_records (P) - 1);
	  goto error;
	}

      /* get the last record */
      if (spage_get_record (P, spage_number_of_records (P) - 1, &Rec, PEEK) !=
	  S_SUCCESS)
	{
	  goto error;
	}
      btree_read_fixed_portion_of_non_leaf_record (&Rec, &nleaf);
      Q_vpid = nleaf.pnt;
      Q = pgbuf_fix (thread_p, &Q_vpid, OLD_PAGE, PGBUF_LATCH_READ,
		     PGBUF_UNCONDITIONAL_LATCH);
      if (Q == NULL)
	{
	  goto error;
	}
      pgbuf_unfix (thread_p, P);
      P = NULL;

      btree_get_header_ptr (Q, &header_ptr);
      node_type = BTREE_GET_NODE_TYPE (header_ptr);

      P = Q;
      Q = NULL;
      P_vpid = Q_vpid;
    }

  /* leaf page is reached */
  *pg_vpid = P_vpid;

  /* NOTE that we do NOT release the page latch on P here */
  return P;

error:

  if (P)
    {
      pgbuf_unfix (thread_p, P);
      P = NULL;
    }
  if (Q)
    {
      pgbuf_unfix (thread_p, Q);
      Q = NULL;
    }

  return NULL;
}

/*
 * btree_keyval_search () -
 *   return: the number of object identifiers in the set pointed
 *           at by oids_ptr, or -1 if an error occurs. Since there can be
 *           many object identifiers for the given key, to avoid main
 *           memory limitations, the set of object identifiers are returned
 *           iteratively. At each call, the btree_scan is modified, to
 *           remember the old search position.
 *   btid(in):
 *      btid: B+tree index identifier
 *   readonly_purpose(in):
 *   btree_scan(in):
 *   btree_scan: Btree range search scan structure
 *   key(in): Key to be searched for its object identifier set
 *   class_oid(in):
 *   oids_ptr(in): Points to the already allocated storage area to store oids
 *   oids_size(in): Size of allocated area for oid set storage
 *   filter(in):
 *   isidp(in):
 *   is_all_class_srch(in):
 *
 * Note: Finds the set of object identifiers for the given key.
 * if the key is not found, a 0 count is returned. Otherwise,
 * the area pointed at by oids_ptr is filled with one group of
 * object identifiers.
 *
 * Note: the btree_scan structure must first be initialized by using the macro
 * BTREE_INIT_SCAN() defined in bt.h
 *
 * Note: After the first iteration, caller can use BTREE_END_OF_SCAN() macro
 * defined in bt.h to understand the end of range.
 */
int
btree_keyval_search (THREAD_ENTRY * thread_p, BTID * btid,
		     int readonly_purpose, BTREE_SCAN * btree_scan,
		     DB_VALUE * key, OID * class_oid, OID * oids_ptr,
		     int oids_size, FILTER_INFO * filter,
		     INDX_SCAN_ID * isidp, bool is_all_class_srch)
{
  /* this is just a GE_LE range search with the same key */
  DB_VALUE *copied;
  int rc;
  LOCK class_lock = NULL_LOCK;
  int scanid_bit = -1;
  int num_classes;

  copied = db_value_copy (key);

  rc =
    lock_scan (thread_p, class_oid, true, LOCKHINT_NONE, &class_lock,
	       &scanid_bit);
  if (rc != LK_GRANTED)
    {
      db_value_free (copied);
      return -1;
    }

  isidp->scan_cache.scanid_bit = scanid_bit;

  /* check if the search is based on all classes contained in the class hierarchy. */
  num_classes = (is_all_class_srch) ? 0 : 1;

  rc = btree_range_search (thread_p, btid, readonly_purpose, LOCKHINT_NONE,
			   btree_scan, key, copied, GE_LE,
			   num_classes, class_oid, oids_ptr, oids_size,
			   filter, isidp, true, false);

  lock_unlock_scan (thread_p, class_oid, scanid_bit, END_SCAN);

  (void) db_value_free (copied);

  return rc;
}

/*
 * btree_coerce_key () -
 *   return: 0 for success, 1 for failure. Fills dest_keyp.
 *   src_keyp(in): a key to be searched for
 *   dest_keyp(out): a possibly-coerced key that will match the actual Btree
 *                   domain
 *   keysize(in): term# associated with index key range
 *   btid(in): B+tree index identifier
 *   key_minmax(in): MIN_VALUE or MAX_VALUE
 *   clear(out): output parameter: true if dst_key will need to be cleared
 *
 * Note: Makes dst_key point to a value that matches the Btree domain.
 * Does NOT actually copy values for strings, because the string
 * comparison functions already do the right thing here (they
 * don't really care about the distinction between CHAR and VARCHAR).
 */

enum
{ BTREE_COERCE_KEY_WITH_MIN_VALUE = 1, BTREE_COERCE_KEY_WITH_MAX_VALUE = 2 };

static int
btree_coerce_key (DB_VALUE * src_keyp, DB_VALUE * dest_keyp, int keysize,
		  BTID_INT * btid, int key_minmax, bool * clear)
{
  TP_DOMAIN *btree_domainp = btid->key_type;
  DB_TYPE stype, dtype;
  int err, ssize, dsize;
  TP_DOMAIN *dp;
  DB_VALUE value;
  DB_MIDXKEY *midxkey;
  TP_DOMAIN *partial_dom;
  int minmax;

  /* assuming all parameters are not NULL pointer, and 'src_key' is not NULL
     value */

  stype = DB_VALUE_TYPE (src_keyp);
  dtype = btree_domainp->type->id;

  if (stype == DB_TYPE_MIDXKEY && dtype == DB_TYPE_MIDXKEY)
    {
      /* if multi-column index */
      /* The type of B+tree key domain can be DB_TYPE_MIDXKEY only in the
         case of multi-column index. And, if it is, query optimizer makes
         the search key('src_key') as sequence type even if partial key was
         specified. One more assumption is that query optimizer make the
         search key(either complete or partial) in the same order (of
         sequence) of B+tree key domain. */

      /* get number of elements of sequence type of the 'src_key' */
      midxkey = DB_GET_MIDXKEY (src_keyp);
      ssize = midxkey->ncolumns;

      /* count number of elements of sequence type of the B+tree key domain */
      for (dp = btree_domainp->setdomain, dsize = 0; dp;
	   dp = dp->next, dsize++)
	{
	  ;
	}

      if (ssize < 0 || ssize > dsize || dsize == 0 || ssize > keysize)
	{
	  /* something wrong with making search key in query optimizer */
	  err = 1;		/* error */
	}
      else if (ssize == dsize)
	{
	  if (midxkey->domain == NULL)	/* checkdb */
	    {
	      midxkey->domain = btree_domainp;
	    }

	  /* direct bitwise copying */
	  *dest_keyp = *src_keyp;
	  return 0;
	}
      else
	{
	  /* do coercing, append min or max value of the coressponding domain
	     type to the partial search key value */
	  DB_VALUE *dbvals;
	  int num_dbvals;

	  num_dbvals = dsize - ssize;

	  if (num_dbvals == 1)
	    {
	      dbvals = &value;
	    }
	  else if (num_dbvals > 1)
	    {
	      dbvals = (DB_VALUE *) db_private_alloc (NULL,
						      num_dbvals *
						      sizeof (DB_VALUE));
	    }
	  else if (num_dbvals < 0)
	    {
	      fprintf (stderr, "Error: btree_coerce_key(num_dbval %d)\n",
		       num_dbvals);
	      return 1;
	    }

	  for (dp = btree_domainp->setdomain, dsize = 0; dp && dsize < ssize;
	       dp = dp->next, dsize++)
	    {
	      ;
	    }

	  num_dbvals = 0;
	  partial_dom = dp;

	  for (err = 0; dp && !err; dp = dp->next, dsize++)
	    {
	      /* server doesn't treat DB_TYPE_OBJECT, so that convert it to
	         DB_TYPE_OID */
	      DB_TYPE type = (dp->type->id == DB_TYPE_OBJECT) ? DB_TYPE_OID
		: dp->type->id;

	      /* set minmax accoring to the asc/desc info;

	         +- part_key_desc
	         |
	         |    +- dp->is_desc
	         |    |
	         \|/  \|/
	         CASE 1: (Asc, Asc)
	         (1 1) (1 2) (2 1) (2 2) (3 1) (3 2)
	         MIN_VALUE -> min, MAX_VALUE -> max

	         CASE 2: (Asc, Desc)
	         (1 2) (1 1) (2 2) (2 1) (3 2) (3 1)
	         MIN_VALUE -> max, MAX_VALUE -> min

	         CASE 3: (Desc, Asc)
	         (3 1) (3 2) (2 1) (2 2) (1 1) (1 2)
	         MIN_VALUE -> max, MAX_VALUE -> min

	         CASE 4: (Desc, Desc) <-- include reverse index
	         (3 2) (3 1) (2 2) (2 1) (1 2) (1 1)
	         MIN_VALUE -> min, MAX_VALUE -> max

	         if partial-key is desc(i.e., CASE 3, 4),
	         swap lower value and upper value in btree_range_search()
	       */
	      minmax = key_minmax;	/* init */
	      if (minmax == BTREE_COERCE_KEY_WITH_MIN_VALUE)
		{
		  if (!BTREE_IS_PART_KEY_DESC (btid))
		    {		/* CASE 1, 2 */
		      if (dp->is_desc != true)
			{	/* CASE 1 */
			  ;	/* nop */
			}
		      else
			{	/* CASE 2 */
			  minmax = BTREE_COERCE_KEY_WITH_MAX_VALUE;
			}
		    }
		  else
		    {		/* CASE 3, 4 */
		      if (dp->is_desc != true)
			{	/* CASE 3 */
			  minmax = BTREE_COERCE_KEY_WITH_MAX_VALUE;
			}
		      else
			{	/* CASE 4 */
			  ;	/* nop */
			}
		    }
		}
	      else if (minmax == BTREE_COERCE_KEY_WITH_MAX_VALUE)
		{
		  if (!BTREE_IS_PART_KEY_DESC (btid))
		    {		/* CASE 1, 2 */
		      if (dp->is_desc != true)
			{	/* CASE 1 */
			  ;	/* nop */
			}
		      else
			{	/* CASE 2 */
			  minmax = BTREE_COERCE_KEY_WITH_MIN_VALUE;
			}
		    }
		  else
		    {		/* CASE 3, 4 */
		      if (dp->is_desc != true)
			{	/* CASE 3 */
			  minmax = BTREE_COERCE_KEY_WITH_MIN_VALUE;
			}
		      else
			{	/* CASE 4 */
			  ;	/* nop */
			}
		    }
		}

	      if (minmax == BTREE_COERCE_KEY_WITH_MIN_VALUE)
		{
		  if (dsize < keysize)
		    {
		      err = (db_value_domain_min (&dbvals[num_dbvals], type,
						  dp->precision,
						  dp->scale) != NO_ERROR);
		    }
		  else
		    {
		      err = (db_value_domain_init (&dbvals[num_dbvals], type,
						   dp->precision,
						   dp->scale) != NO_ERROR);
		    }
		}
	      else if (minmax == BTREE_COERCE_KEY_WITH_MAX_VALUE)
		{
		  err = (db_value_domain_max (&dbvals[num_dbvals], type,
					      dp->precision,
					      dp->scale) != NO_ERROR);
		}
	      else
		{
		  err = 1;
		}

	      num_dbvals++;
	    }

	  if (!err)
	    {
	      err = (set_midxkey_add_elements (src_keyp, dbvals, num_dbvals,
					       partial_dom,
					       btree_domainp) != NO_ERROR);
	    }
	  if (!err)
	    {
	      /* direct bitwise copying */
	      *dest_keyp = *src_keyp;
	      /* don't touch '*clear'
	         for special handling of 'a < 10' case */
	      /* *clear = 0; *//* the caller not need to clear(free) 'dest_key' */
	    }

	  if (num_dbvals > 1)
	    {
	      db_private_free_and_init (NULL, dbvals);
	    }
	}

    }
  else if (
	    /* check if they are string or bit type */
	    /* compatible if two types are same (except for sequence type) */
	    (stype == dtype) ||
	    /* CHAR type and VARCHAR type are compatible with each other */
	    ((stype == DB_TYPE_CHAR || stype == DB_TYPE_VARCHAR) &&
	     (dtype == DB_TYPE_CHAR || dtype == DB_TYPE_VARCHAR)) ||
	    /* NCHAR type and VARNCHAR type are compatible with each other */
	    ((stype == DB_TYPE_NCHAR || stype == DB_TYPE_VARNCHAR) &&
	     (dtype == DB_TYPE_NCHAR || dtype == DB_TYPE_VARNCHAR)) ||
	    /* BIT type and VARBIT type are compatible with each other */
	    ((stype == DB_TYPE_BIT || stype == DB_TYPE_VARBIT) &&
	     (dtype == DB_TYPE_BIT || dtype == DB_TYPE_VARBIT)) ||
	    /* OID type and OBJECT type are compatible with each other */
	    /* Keys can come in with a type of DB_TYPE_OID, but the B+tree domain
	       itself will always be a DB_TYPE_OBJECT. The comparison routines
	       can handle OID and OBJECT as compatible type with each other . */
	    (stype == DB_TYPE_OID || stype == DB_TYPE_OBJECT))
    {

      /* direct bitwise copying */
      *dest_keyp = *src_keyp;
      *clear = false;		/* the caller not need to clear(free) 'dest_key' */
      err = 0;			/* no error */

    }
  else
    {
      /* the other case, do real coercing using 'tp_value_coerce()' */
      err = (tp_value_coerce (src_keyp, dest_keyp, btree_domainp) !=
	     DOMAIN_COMPATIBLE);
      *clear = true;		/* the caller need to clear(free) 'dest_key' */
    }

  if (err)
    {
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      *clear = false;		/* no clear in the error case */
    }

  /* return result */
  return err;
}

/*
 * btree_initialize_bts () -
 *   return: NO_ERROR
 *   bts(in): pointer to B+-tree scan structure
 *   btid(in): B+-tree identifier
 *   readonly_purpose(in):
 *   lock_hint(in):
 *   cls_oid(in): class oid (NULL_OID or valid OID)
 *   key1(in): the lower bound key value of key range
 *   key2(in): the upper bound key value of key range
 *   range(in): the range of key range
 *   filter(in): key filter
 *   need_construct_btid_int(in):
 *   copy_buf(in):
 *   copy_buf_len(in):
 *
 * Note: Initialize a new B+-tree scan structure for an index scan.
 */
static int
btree_initialize_bts (THREAD_ENTRY * thread_p, BTREE_SCAN * bts, BTID * btid,
		      int readonly_purpose, int lock_hint,
		      OID * class_oid,
		      DB_VALUE * key1, DB_VALUE * key2,
		      RANGE range, FILTER_INFO * filter,
		      bool need_construct_btid_int, char *copy_buf,
		      int copy_buf_len)
{
  VPID Root_vpid;
  PAGE_PTR Root = NULL;
  RECDES Rec;
  BTREE_ROOT_HEADER root_header;
  int i;
  int ret = NO_ERROR;

  /* initialize page related fields */
  /* previous leaf page, current leaf page, overflow page */
  bts->P_vpid.pageid = NULL_PAGEID;
  bts->P_page = NULL;
  bts->C_vpid.pageid = NULL_PAGEID;
  bts->C_page = NULL;
  bts->O_vpid.pageid = NULL_PAGEID;
  bts->O_page = NULL;

  /* initialize current key related fields */
  bts->clear_cur_key = false;

  /* initialize key range */
  bts->key_range.clear_lower = false;
  bts->key_range.clear_upper = false;

  /* cache transaction isolation level */
  bts->tran_isolation = logtb_find_current_isolation (thread_p);

  bts->read_uncommitted =
    ((bts->tran_isolation == TRAN_REP_CLASS_UNCOMMIT_INSTANCE
      || bts->tran_isolation == TRAN_COMMIT_CLASS_UNCOMMIT_INSTANCE)
     && readonly_purpose) || (lock_hint & LOCKHINT_READ_UNCOMMITTED);

  if (need_construct_btid_int == true)
    {
      /* construct BTID_INT structure */
      Root_vpid.pageid = btid->root_pageid;
      Root_vpid.volid = btid->vfid.volid;
      Root = pgbuf_fix (thread_p, &Root_vpid, OLD_PAGE, PGBUF_LATCH_READ,
			PGBUF_UNCONDITIONAL_LATCH);
      if (Root == NULL)
	{
	  goto exit_on_error;
	}
      if (spage_get_record (Root, HEADER, &Rec, PEEK) != S_SUCCESS)
	{
	  goto exit_on_error;
	}

      btree_read_root_header (&Rec, &root_header);
      pgbuf_unfix (thread_p, Root);
      Root = NULL;

      bts->btid_int.sys_btid = btid;
      ret = btree_glean_root_header_info (&root_header, &bts->btid_int);
      if (ret != NO_ERROR)
	{
	  goto exit_on_error;
	}
    }

  /*
   * set index key copy_buf info;
   * is allocated at btree_keyval_search() or scan_open_index_scan()
   */
  bts->btid_int.copy_buf = copy_buf;
  bts->btid_int.copy_buf_len = copy_buf_len;

  /* initialize the key range with given information */
  /*
   * Set up the keys and make sure that they have the proper domain
   * (by coercing, if necessary). Open-ended searches will have one or
   * both of key1 or key2 set to NULL so that we no longer have to do
   * db_value_is_null() tests on them.
   */
  /* to fix multi-column index NULL problem */

  /* only used for mulit-column index with PRM_ORACLE_STYLE_EMPTY_STRING,
   * otherwise set as zero
   */
  bts->keysize = 0;

  if (key1 && DB_VALUE_TYPE (key1) == DB_TYPE_MIDXKEY)
    {
      bts->keysize = key1->data.midxkey.ncolumns;
    }

  if (key2 && DB_VALUE_TYPE (key2) == DB_TYPE_MIDXKEY)
    {
      bts->keysize = MAX (bts->keysize, key2->data.midxkey.ncolumns);

      if (key1 == NULL)
	{
	  /* special handling of 'a < 10' case */
	  DB_MIDXKEY midxkey;

	  key1 = &bts->key_range.lower_value;

	  midxkey.size = 0;
	  midxkey.ncolumns = 0;
	  midxkey.domain = bts->btid_int.key_type;
	  midxkey.buf = NULL;

	  db_make_midxkey (key1, &midxkey);
	  key1->need_clear = true;

	  bts->key_range.clear_lower = true;
	}
    }

  /* re-check for partial-key domain is desc */
  if (!BTREE_IS_PART_KEY_DESC (&(bts->btid_int)))
    {
      TP_DOMAIN *dom;

      dom = bts->btid_int.key_type;
      if (dom->type->id == DB_TYPE_MIDXKEY)
	{
	  dom = dom->setdomain;
	}

      /* get the last domain element of partial-key */
      for (i = 1; i < bts->keysize && dom; i++, dom = dom->next)
	;			/* nop */

      if (i < bts->keysize || dom == NULL)
	{
	  goto exit_on_error;
	}

      bts->btid_int.part_key_desc = dom->is_desc;
    }

  /* code "(bts->key_range.clear_lower == false &&
     btree_multicol_key_is_null(key1))" means that
     it is not special case of 'a < 10' */
  if (key1 == NULL
      || db_value_is_null (key1)
      || (bts->key_range.clear_lower == false
	  && btree_multicol_key_is_null (key1)))
    {
      key1 = NULL;
    }
  else if (btree_coerce_key (key1, &bts->key_range.lower_value,
			     bts->keysize, &(bts->btid_int),
			     ((range == GT_INF
			       || range == GT_LE
			       || range == GT_LT)
			      ? BTREE_COERCE_KEY_WITH_MAX_VALUE :
			      BTREE_COERCE_KEY_WITH_MIN_VALUE),
			     &bts->key_range.clear_lower))
    {
      goto exit_on_error;
    }
  else
    {
      key1 = &bts->key_range.lower_value;
    }

  if (key2 == NULL
      || db_value_is_null (key2) || btree_multicol_key_is_null (key2))
    {
      key2 = NULL;
    }
  else if (btree_coerce_key (key2, &bts->key_range.upper_value,
			     bts->keysize, &(bts->btid_int),
			     ((range == INF_LT
			       || range == GE_LT
			       || range == GT_LT)
			      ? BTREE_COERCE_KEY_WITH_MIN_VALUE :
			      BTREE_COERCE_KEY_WITH_MAX_VALUE),
			     &bts->key_range.clear_upper))
    {
      goto exit_on_error;
    }
  else
    {
      key2 = &bts->key_range.upper_value;
    }

  /* lower bound key and upper bound key */
  bts->key_range.lower_key = key1;
  bts->key_range.upper_key = key2;

  /* range type */
  bts->key_range.range = range;

  if (PRM_ORACLE_STYLE_EMPTY_STRING)
    {
      int j, ids_size;

      if (filter)
	{
	  ids_size = 0;		/* init */
	  for (i = 0; i < bts->keysize; i++)
	    {
	      filter->vstr_ids[i] = -1;	/* init to false */
	      for (j = 0; j < filter->scan_attrs->num_attrs; j++)
		{
		  if (filter->btree_attr_ids[i] ==
		      filter->scan_attrs->attr_ids[j])
		    {
		      filter->vstr_ids[i] = filter->btree_attr_ids[i];
		      ids_size = i + 1;
		      break;
		    }
		}
	    }

	  /* reset num of variable string attr in key range */
	  *(filter->num_vstr_ptr) = ids_size;
	}
    }

  /* initialize key fileter */
  bts->key_filter = filter;	/* valid pointer or NULL */

#if defined(SERVER_MODE)
  bts->key_range_max_value_equal = false;

  /* cache class OID and memory address to class lock mode */
  if (BTREE_IS_UNIQUE (&(bts->btid_int)))
    {
	/****** UNIQUE INDEX CLASSIFICATION ****************************
         * the unique index can be classified into following two types
         * according to the class hierarchy on which the unique index based:
         * (1) single class index and (2) class hierarchy index.
         * if the given unique index has single class index type,
         * bts->cls_oid might have the corresponding class OID information and
         * bts->cls_lock_ptr might have memory address to the class lock mode.
         * currently, the type of an unique index is not identified.
         * so, bts->cls_oid has NULL_OID and bts->cls_lock_ptr has NULL.
         */
      OID_SET_NULL (&bts->cls_oid);
      bts->cls_lock_ptr = NULL;
    }
  else
    {				/* non-unique index */
      /*
       * The non-unique index is always a single class index
       * 'db_user' class has non-unique index, but btree_find_unique request
       * can be called. In such case class_oid is NULL
       */
      if (class_oid == NULL)
	{
	  OID_SET_NULL (&bts->cls_oid);
	}
      else
	{
	  COPY_OID (&bts->cls_oid, class_oid);
	}

      if (OID_ISNULL (&bts->cls_oid))
	{
	  bts->cls_lock_ptr = NULL;
	}
      else
	{
	  int tran_index;

	  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);

	  bts->cls_lock_ptr = lock_get_class_lock (&bts->cls_oid, tran_index);
	  if (bts->cls_lock_ptr == NULL)
	    {
	      /*
	       * The corresponding class lock must be acquired
	       * before an index scan is requested.
	       */
	      er_log_debug (ARG_FILE_LINE,
			    "bts->cls_lock_ptr == NULL in btree_initialize_bts()\n"
			    "bts->cls_oid = <%d,%d,%d>\n",
			    bts->cls_oid.volid, bts->cls_oid.pageid,
			    bts->cls_oid.slotid);
	      goto exit_on_error;
	    }
	}
    }

  /* initialize bts->class_lock_map_count */
  bts->class_lock_map_count = 0;

  /****** INDEX SCAN PURPOSE and INSTANCE LOCK MODE ******************
   * During an index scan, the lock mode of instance locks that are
   * to be held is different according to the purpose of the index scan.
   * The relationship between the scan purpose and instance lock mode
   * is like followings. (default: S_LOCK)
   * - read-only scan :
   *      all isolation levels       : S_LOCK
   * - updatable scan (for DELETE or UPDATE)
   *      all isolation levels       : U_LOCK
   * If the purpose of an index scan is given through an argument,
   * bts->lock_mode can be set according to the given purpose.
   * bts->escalated_mode represents the class lock mode
   * when many instance locks are escalated to a class lock.
   * bts->escalated_mode is used to find out if instance level locking
   * is really needed at each instance lock request time.
   */
  if (readonly_purpose)
    {
      bts->lock_mode = S_LOCK;
      bts->escalated_mode = S_LOCK;
    }
  else
    {
      bts->lock_mode = U_LOCK;
      bts->escalated_mode = X_LOCK;
    }

  bts->prev_ovfl_vpid.pageid = NULL_PAGEID;
#endif /* SERVER_MODE */

  return ret;

exit_on_error:
  if (Root != NULL)
    {
      pgbuf_unfix (thread_p, Root);
      Root = NULL;
    }

  if (ret == NO_ERROR)
    {
      ret = er_errid ();
      if (ret == NO_ERROR)
	{
	  ret = ER_FAILED;
	}
    }

  return ret;
}

/*
 * btree_find_next_index_record () -
 *   return: NO_ERROR
 *   bts(in):
 *
 * Note: This functions finds the next index record(or slot).
 * Then, it adjusts the slot_id and oid_pos information
 * about the oid-set contained in the found index slot.
 */
static int
btree_find_next_index_record (THREAD_ENTRY * thread_p, BTREE_SCAN * bts)
{
  char *header_ptr;
  int key_cnt;
  PAGE_PTR temp_page = NULL;
  int ret = NO_ERROR;

  /*
   * Assumptions : last accessed leaf page is fixed.
   *    - bts->C_page != NULL
   *    - bts->O_page : NULL or NOT NULL
   *    - bts->P_page == NULL
   */

  /* unfix the overflow page if it is fixed. */
  if (bts->O_page != NULL)
    {
      pgbuf_unfix (thread_p, bts->O_page);
      bts->O_page = NULL;
      bts->O_vpid.pageid = NULL_PAGEID;
    }

  /* unfix the previous leaf page if it is fixed. */
  if (bts->P_page != NULL)
    {
      pgbuf_unfix (thread_p, bts->P_page);
      bts->P_page = NULL;
      bts->P_vpid.pageid = NULL_PAGEID;
    }

  /* get header information (key_cnt) from the current leaf page */
  btree_get_header_ptr (bts->C_page, &header_ptr);
  key_cnt = BTREE_GET_NODE_KEY_CNT (header_ptr);

  /*
   * If the next index record exists in the current leaf page,
   * the next index record(slot) and OID position can be identified easily.
   */
  if (bts->slot_id < key_cnt)
    {
      bts->slot_id += 1;
      bts->oid_pos = 0;
      goto end;			/* OK */
    }

  /*
   * bts->slot_id >= key_cnt
   * The next index record exists in the next leaf page.
   */
  bts->P_vpid = bts->C_vpid;
  bts->P_page = bts->C_page;
  bts->C_page = NULL;

again:

  /* fix the next leaf page and set slot_id and oid_pos if it exists. */
  BTREE_GET_NODE_NEXT_VPID (header_ptr, &bts->C_vpid);
  if (bts->C_vpid.pageid != NULL_PAGEID)
    {
      bts->C_page = pgbuf_fix (thread_p, &bts->C_vpid, OLD_PAGE,
			       PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
      if (bts->C_page == NULL)
	{
	  goto exit_on_error;
	}
      bts->slot_id = 1;
      bts->oid_pos = 0;

      /* unfix the previous leaf page if it is fixed. */
      if (bts->P_page != NULL)
	{
	  pgbuf_unfix (thread_p, bts->P_page);
	  bts->P_page = NULL;
	  /* do not clear bts->P_vpid for UNCONDITIONAL lock request handling */
	}
    }

  /* If bts->C_vpid.pageid == NULL_PAGEID, then bts->C_page == NULL. */

  if (temp_page != NULL)
    {
      pgbuf_unfix (thread_p, temp_page);
      temp_page = NULL;
    }

  /* check if the current leaf page has valid slots */
  if (bts->C_page != NULL)
    {
      btree_get_header_ptr (bts->C_page, &header_ptr);
      key_cnt = BTREE_GET_NODE_KEY_CNT (header_ptr);
      if (key_cnt <= 0)
	{			/* empty page */
	  temp_page = bts->C_page;
	  bts->C_page = NULL;
	  goto again;
	}
    }

  /*
   * ABOUT FIXED INDEX PAGES
   *
   * the fixed index leaf pages are like the followings.
   * 1) If the next index record is found in the current leaf page,
   *    bts->C_page != NULL.
   * 2) If the next index record is found in the next leaf page,
   *    bts->P_page == NULL && bts->C_page != NULL.
   * 3) If the next index record does not exist,
   *    bts->P_page != NULL.
   */

end:
  return ret;

exit_on_error:
  if (temp_page != NULL)
    {
      pgbuf_unfix (thread_p, temp_page);
      temp_page = NULL;
    }

  if (ret == NO_ERROR)
    {
      ret = er_errid ();
      if (ret == NO_ERROR)
	{
	  ret = ER_FAILED;
	}
    }

  return ret;
}

/*
 * btree_get_next_oidset_pos () - Get the next oid-set position info.
 *   return: NO_ERROR
 *   bts(in): pointer to B+-tree scan structure
 *   first_ovfl_vpid(in): the pageid of the first OID overflow page
 *                        of the current index slot.
 *
 * Note: This function finds the next oid-set to be scanned.
 * It fixes the needed index pages, and
 * sets the slot_id and oid_pos information of the next oid-set.
 */
static int
btree_get_next_oidset_pos (THREAD_ENTRY * thread_p, BTREE_SCAN * bts,
			   VPID * first_ovfl_vpid)
{
  char *header_ptr;
  int ret = NO_ERROR;

  /*
   * Assumptions : last accessed leaf page is fixed.
   *    - bts->C_page != NULL
   *    - bts->O_page : NULL or NOT NULL
   *    - bts->P_page == NULL
   */

  if (bts->O_page != NULL)
    {
      /*
       * Assumption :
       * bts->oid_pos >= # of OIDs contained in the overflow page
       */
      /* get the pageid of the next overflow page */
      btree_get_header_ptr (bts->O_page, &header_ptr);
      btree_get_next_overflow_vpid (header_ptr, &bts->O_vpid);

      /* unfix current overflow page */
      pgbuf_unfix (thread_p, bts->O_page);
      bts->O_page = NULL;
    }
  else
    {
      /*
       * Assumption :
       * bts->oid_pos >= # of OIDs contained in the index entry
       */
      /*
       * get the pageid of the first overflow page
       * requirements : first_ovfl_vpid != NULL
       * first_ovfl_vpid : either NULL_VPID or valid VPID
       */
      bts->O_vpid = *first_ovfl_vpid;
    }

  /* fix the next overflow page or the next leaf page */
  if (bts->O_vpid.pageid != NULL_PAGEID)
    {
      /* fix the next overflow page */
      bts->O_page = pgbuf_fix (thread_p, &bts->O_vpid, OLD_PAGE,
			       PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
      if (bts->O_page == NULL)
	{
	  goto exit_on_error;
	}
      bts->oid_pos = 0;
      /* bts->slot_id is not changed */
    }
  else
    {
      /* find the next index record */
      ret = btree_find_next_index_record (thread_p, bts);
      if (ret != NO_ERROR)
	{
	  goto exit_on_error;
	}
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

  return ret;
}

/*
 * btree_prepare_first_search () - Prepare for the first index scan
 *   return: NO_ERROR
 *   bts(in): pointer to B+-tree scan structure
 *
 * Note: This function finds the first oid-set to be scanned.
 * This function is invoked in the first index scan.
 * Then, it searches down the B+-tree, fixes the needed index pages,
 * and sets the slot_id and oid_pos information of the first index scan.
 */
static int
btree_prepare_first_search (THREAD_ENTRY * thread_p, BTREE_SCAN * bts)
{
  int found;
  int key_cnt;
  char *header_ptr;
  int ret = NO_ERROR;

  /* search down the tree to find the first oidset */
  /*
   * Following information must be gotten.
   * bts->C_vpid, bts->C_page, bts->slot_id, bts->oid_pos
   */

  /*
   * If the key range does not have a lower bound key value,
   * the first key of the index is used as the lower bound key value.
   */
  if (bts->key_range.lower_key == NULL)
    {				/* The key range has no bottom */
      /* fix the first leaf page */
      bts->C_page = btree_find_first_leaf (thread_p, bts->btid_int.sys_btid,
					   &bts->C_vpid);
      if (bts->C_page == NULL)
	{
	  goto exit_on_error;
	}

      /* set slot id and OID position */
      bts->slot_id = 1;

      /* get header information (key_cnt) */
      btree_get_header_ptr (bts->C_page, &header_ptr);
      key_cnt = BTREE_GET_NODE_KEY_CNT (header_ptr);

      if (bts->slot_id > key_cnt)
	{
	  /* empty leaf page */
	  ret = btree_find_next_index_record (thread_p, bts);
	  if (ret != NO_ERROR)
	    {
	      goto exit_on_error;
	    }
	}
      else
	{
	  bts->oid_pos = 0;
	}

      return ret;		/* OK */
    }

  /*
   * bts->key_range.lower_key != NULL
   * Find out and fix the first leaf page
   * that contains the given lower bound key value.
   */
  bts->C_page =
    btree_locate_key (thread_p, &bts->btid_int, bts->key_range.lower_key,
		      &bts->C_vpid, &bts->slot_id, &found);

  if (!found)
    {
      if (bts->slot_id == NULL_SLOTID)
	{
	  goto exit_on_error;
	}

      /* get header information (key_cnt) */
      btree_get_header_ptr (bts->C_page, &header_ptr);
      key_cnt = BTREE_GET_NODE_KEY_CNT (header_ptr);

      if (bts->slot_id > key_cnt)
	{
	  /*
	   * The lower bound key does not exist in the current leaf page.
	   * Therefore, get the first slot of the next leaf page.
	   */
	  ret = btree_find_next_index_record (thread_p, bts);
	  if (ret != NO_ERROR)
	    {
	      goto exit_on_error;
	    }
	}
      else
	{
	  bts->oid_pos = 0;
	}
    }
  else
    {
      if (!BTREE_IS_LAST_KEY_DESC (&(bts->btid_int)))
	{			/* normal index */
	  if (bts->key_range.range == GT_LT
	      || bts->key_range.range == GT_LE
	      || bts->key_range.range == GT_INF)
	    {
	      /* get the next index record */
	      ret = btree_find_next_index_record (thread_p, bts);
	      if (ret != NO_ERROR)
		{
		  goto exit_on_error;
		}
	    }
	  else
	    {
	      bts->oid_pos = 0;
	    }
	}
      else
	{			/* last key element domain is desc */
	  if (bts->key_range.range == GE_LT
	      || bts->key_range.range == GT_LT
	      || bts->key_range.range == INF_LT)
	    {
	      /* get the next index record */
	      ret = btree_find_next_index_record (thread_p, bts);
	      if (ret != NO_ERROR)
		{
		  goto exit_on_error;
		}
	    }
	  else
	    {
	      bts->oid_pos = 0;
	    }
	}
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

  return ret;
}

/*
 * btree_prepare_next_search () - Prepare for the next index scan
 *   return: NO_ERROR
 *   bts(in): pointer to B+-tree scan structure
 *
 * Note: This function finds the next oid-set to be scaned.
 * This function is invoked by the next index scan.
 * Then it fixes the needed index pages, and sets
 * the slot_id and oid_pos information of the next index scan.
 */
static int
btree_prepare_next_search (THREAD_ENTRY * thread_p, BTREE_SCAN * bts)
{
#if defined(SERVER_MODE)
  char *header_ptr;
  int key_cnt;
  int found;
#endif /* SERVER_MODE */
  int ret = NO_ERROR;

  /*
   * Assumptions :
   * 1. bts->C_vpid.pageid != NULL_PAGEID
   * 2. bts->O_vpid.pageid is NULL_PAGEID or not NULL_PAGEID.
   * 3. bts->P_vpid.pageid == NULL_PAGEID
   * 4. bts->slot_id indicates the last accessed slot
   * 5. 1 < bts->oid_pos <= (last oid position + 1)
   */

  /* fix the current leaf page */
  bts->C_page = pgbuf_fix (thread_p, &bts->C_vpid, OLD_PAGE, PGBUF_LATCH_READ,
			   PGBUF_UNCONDITIONAL_LATCH);
  if (bts->C_page == NULL)
    {
      goto exit_on_error;
    }

#if defined(SERVER_MODE)
  /* check if the current leaf page has been changed */
  if (!LSA_EQ (&bts->cur_leaf_lsa, pgbuf_get_lsa (bts->C_page)))
    {
      /*
       * The current leaf page has been changed.
       * unfix the current leaf page
       */
      pgbuf_unfix (thread_p, bts->C_page);
      bts->C_page = NULL;

      /* find out the last accessed index record */
      bts->C_page = btree_locate_key (thread_p, &bts->btid_int, &bts->cur_key,
				      &bts->C_vpid, &bts->slot_id, &found);

      if (!found)
	{
	  if (bts->slot_id == NULL_SLOTID)
	    {
	      goto exit_on_error;
	    }

	  if (bts->tran_isolation == TRAN_REP_CLASS_UNCOMMIT_INSTANCE
	      || bts->tran_isolation == TRAN_COMMIT_CLASS_UNCOMMIT_INSTANCE)
	    {
	      /*
	       * Uncommitted Read
	       * bts->cur_key might be deleted.
	       * get header information (key_cnt)
	       */
	      btree_get_header_ptr (bts->C_page, &header_ptr);
	      key_cnt = BTREE_GET_NODE_KEY_CNT (header_ptr);

	      if (bts->slot_id > key_cnt)
		{
		  ret = btree_find_next_index_record (thread_p, bts);
		  if (ret != NO_ERROR)
		    {
		      goto exit_on_error;
		    }
		}
	      /* In case of bts->slot_id <= key_cnt, everything is OK. */
	    }
	  else
	    {
	      /*
	       * transaction isolation level >= Committed Read
	       * Since one or more OIDs associated with bts->cur_key
	       * have been locked, bts->cur_key must be found in the index.
	       */
	      goto exit_on_error;
	    }
	}
      /* if found, everything is OK. */
    }
#endif /* SERVER_MODE */

  /*
   * If the current leaf page has not been changed,
   * bts->slot_id and bts->oid_pos are still valid.
   */

  /*
   * If bts->O_vpid.pageid != NULL_PAGEID, fix the overflow page.
   * When bts->O_vpid.pageid != NULL_PAGEID, bts->oid_pos indicates
   * any one OID among OIDs contained in the overflow page. And,
   * The OIDs positioned before bts->oid_pos cannot be removed.
   * Because, instance locks on the OIDs are held.
   * Therefore, the overflow page is still existent in the index.
   * bts->slot_id and bts->oid_pos are still valid.
   */
  if (bts->O_vpid.pageid != NULL_PAGEID)
    {
      /* fix the current overflow page */
      bts->O_page = pgbuf_fix (thread_p, &bts->O_vpid, OLD_PAGE,
			       PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
      if (bts->O_page == NULL)
	{
	  goto exit_on_error;
	}
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

  return ret;
}

/*
 * btree_apply_key_range_and_filter () - Apply key range and key filter condition
 *   return: NO_ERROR
 *   bts(in): pointer to B+-tree scan structure
 *   is_key_range_satisfied(out): true, or false
 *   is_key_filter_satisfied(out): true, or false
 *
 * Note: This function applies key range condition and key filter condition
 * to the current key value saved in B+-tree scan structure.
 * The results of the evaluation of the given conditions are
 * returned throught key_range_satisfied and key_filter_satisfied.
 */
static int
btree_apply_key_range_and_filter (THREAD_ENTRY * thread_p, BTREE_SCAN * bts,
				  bool * is_key_range_satisfied,
				  bool * is_key_filter_satisfied)
{
  int c;			/* comparison result */
  DB_LOGICAL ev_res;		/* evaluation result */
  DB_MIDXKEY *mkey;		/* midxkey ptr */
  DB_VALUE ep;			/* element ptr */
  bool is_empty_string;
  DB_TYPE type;
  int ret = NO_ERROR;

  *is_key_range_satisfied = *is_key_filter_satisfied = true;	/* init as true  */
#if defined(SERVER_MODE)
  bts->key_range_max_value_equal = false;	/* init as false */
#endif /* SERVER_MODE */

  if (bts->key_filter		/* caller: sc_get_index_oidset() */
      && DB_VALUE_DOMAIN_TYPE (&bts->cur_key) == DB_TYPE_MIDXKEY)
    {
      mkey = &(bts->cur_key.data.midxkey);
      /* get the last element from key range elements */
      ret = set_midxkey_get_element_nocopy (mkey, bts->keysize - 1, &ep,
					    NULL, NULL);
      if (ret != NO_ERROR)
	{
	  goto exit_on_error;
	}

      if (DB_IS_NULL (&ep))
	{
	  is_empty_string = false;	/* init */
	  if (PRM_ORACLE_STYLE_EMPTY_STRING)
	    {
	      if (ep.need_clear)
		{		/* need to check */
		  type = DB_VALUE_DOMAIN_TYPE (&ep);
		  if (QSTR_IS_ANY_CHAR_OR_BIT (type)
		      && ep.data.ch.medium.buf != NULL)
		    {
		      is_empty_string = true;	/* is Empty-string */
		    }
		}
	    }

	  if (!is_empty_string)
	    {
	      *is_key_filter_satisfied = false;
	      goto end;		/* give up */
	    }
	}
    }

  /* Key Range Checking */
  if (bts->key_range.upper_key == NULL)
    {
      c = 1;
    }
  else
    {
      c = (*(bts->btid_int.key_type->type->cmpval)) (bts->key_range.upper_key,
						     &bts->cur_key,
						     bts->btid_int.key_type,
						     bts->btid_int.reverse, 0,
						     1, NULL);
    }

  if (c < 0)
    {
      *is_key_range_satisfied = false;
    }
  else if (c == 0)
    {
      if (!BTREE_IS_LAST_KEY_DESC (&(bts->btid_int)))
	{			/* normal index */
	  if (bts->key_range.range != GT_LE
	      && bts->key_range.range != GE_LE
	      && bts->key_range.range != INF_LE)
	    {
	      *is_key_range_satisfied = false;
	    }
	  else
	    {
	      *is_key_range_satisfied = true;
#if defined(SERVER_MODE)
	      bts->key_range_max_value_equal = true;
#endif /* SERVER_MODE */
	    }
	}
      else
	{			/* last key element domain is desc */
	  if (bts->key_range.range != GE_LT
	      && bts->key_range.range != GE_LE
	      && bts->key_range.range != GE_INF)
	    {
	      *is_key_range_satisfied = false;
	    }
	  else
	    {
	      *is_key_range_satisfied = true;
#if defined(SERVER_MODE)
	      bts->key_range_max_value_equal = true;
#endif /* SERVER_MODE */
	    }
	}
    }
  else
    {
      *is_key_range_satisfied = true;
    }

  if (*is_key_range_satisfied)
    {
      /*
       * Only in case that key_range_satisfied is true,
       * the key filter can be applied to the current key value.
       */
      if (bts->key_filter && bts->key_filter->scan_pred->regu_list)
	{
	  ev_res = eval_key_filter (thread_p, &bts->cur_key, bts->key_filter);
	  if (ev_res == V_ERROR)
	    {
	      goto exit_on_error;
	    }

	  if (ev_res == V_TRUE)
	    {
	      *is_key_filter_satisfied = true;
	    }
	  else
	    {			/* V_FALSE || V_UNKNOWN */
	      *is_key_filter_satisfied = false;
	    }
	}
    }

end:
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

  return ret;
}

#if 0				/* TODO: currently, unused */
#if defined(SERVER_MODE)
/*
 * btree_get_prev_keyvalue () - Get the previous key value
 *   return: NO_ERROR, or ER_FAILED
 *   bts(in): pointer to B+-tree scan structure
 *   prev_key(out): pointer to previous key value
 *   clear_prev_key(out): flag representing if prev_key must be cleared.
 *
 * Note: This function gets the previous key value. This function is called
 * immediately before the unconditional locking in TRAN_SERIALIZABLE.
 * The saved previous key value is used to find out the next key to be
 * scanned after the unconditional instance locking.
 */
static int
btree_get_prev_keyvalue (BTREE_SCAN * bts, DB_VALUE * prev_key,
			 int *clear_prev_key)
{
  char *header_ptr;
  int key_cnt;
  RECDES Rec;
  LEAF_REC dummy_leaf_pnt;
  int offset;

  if (bts->P_page != NULL)
    {
      /*
       * The previous leaf page exists.
       * get header information (key_cnt)
       */
      btree_get_header_ptr (bts->P_page, &header_ptr);
      key_cnt = BTREE_GET_NODE_KEY_CNT (header_ptr);

      if (spage_get_record (bts->P_page, key_cnt, &Rec, PEEK) != S_SUCCESS)
	{
	  goto error;
	}
    }
  else
    {
      if (bts->slot_id == 1)
	{
	  goto error;
	}

      /* The previous leaf page does not exist. */
      if (spage_get_record (bts->C_page, (bts->slot_id - 1), &Rec, PEEK)
	  != S_SUCCESS)
	{
	  goto error;
	}
    }

  btree_read_record (thread_p, &bts->btid_int, &Rec, prev_key,
		     &dummy_leaf_pnt, true, clear_prev_key, &offset, 1);

  return NO_ERROR;

error:

  return ER_FAILED;
}
#endif /* SERVER_MODE */
#endif /* 0 */

#if defined(SERVER_MODE)
/*
 * btree_handle_prev_leaf_after_locking () -
 *      The handling after unconditional instance locking
 *      in case that the previous leaf page exists.
 *   return: NO_ERROR
 *   bts(in): pointer to B+-tree scan structure
 *   oid_idx(in): current OID position in OID-set to be scanned
 *   prev_leaf_lsa(in): the page LSA of the previous leaf page
 *   prev_key(in): pointer to previous key value
 *   which_action(out): BTREE_CONTINUE
 *                      BTREE_GETOID_AGAIN_WITH_CHECK
 *                      BTREE_SEARCH_AGAIN_WITH_CHECK
 *
 * Note: This function is invoked after the unconditional instance locking
 * in case that the previous leaf page was existent.
 * The purpose of this function is to check the validation
 * of the unconditionally acquired lock and then to adjust
 * the next processing based on the validation result.
 */
static int
btree_handle_prev_leaf_after_locking (THREAD_ENTRY * thread_p,
				      BTREE_SCAN * bts, int oid_idx,
				      LOG_LSA * prev_leaf_lsa,
				      DB_VALUE * prev_key, int *which_action)
{
  char *header_ptr;
  int key_cnt;
  int found;
  int ret = NO_ERROR;

  /*
   * Following conditions are satisfied.
   * 1. The second argument, oid_idx, is always 0(zero).
   * 2. bts->O_vpid.pageid == NULL_PAGEID.
   */

  /* fix the previous leaf page */
  bts->P_page = pgbuf_fix (thread_p, &bts->P_vpid, OLD_PAGE, PGBUF_LATCH_READ,
			   PGBUF_UNCONDITIONAL_LATCH);
  if (bts->P_page == NULL)
    {
      goto exit_on_error;
    }

  /* check if the previous leaf page has been changed */
  if (LSA_EQ (prev_leaf_lsa, pgbuf_get_lsa (bts->P_page)))
    {
      /*
       * The previous leaf page has not been changed
       */
      if (bts->prev_ovfl_vpid.pageid != NULL_PAGEID)
	{
	  /*
	   * The previous key value has its associated OID overflow pages.
	   * It also means prev_KF_satisfied == true
	   * find the last locked OID and then the next OID.
	   * Because, some updates such as the insertion of new OID
	   * might be occurred in the OID overflow page.
	   */

	  /* fix the current (last) leaf page */
	  bts->C_page = bts->P_page;
	  bts->P_page = NULL;
	  bts->C_vpid = bts->P_vpid;
	  bts->P_vpid.pageid = NULL_PAGEID;

	  /* get bts->slot_id */
	  btree_get_header_ptr (bts->C_page, &header_ptr);
	  key_cnt = BTREE_GET_NODE_KEY_CNT (header_ptr);
	  bts->slot_id = key_cnt;

	  /* fix the overflow page */
	  bts->O_vpid = bts->prev_ovfl_vpid;
	  bts->O_page = pgbuf_fix (thread_p, &bts->O_vpid, OLD_PAGE,
				   PGBUF_LATCH_READ,
				   PGBUF_UNCONDITIONAL_LATCH);
	  if (bts->O_page == NULL)
	    {
	      goto exit_on_error;
	    }

	  bts->oid_pos = bts->prev_oid_pos + 1;

	  /*
	   * prev_ovfl_vpid, prev_oid_pos has garbage data, now.
	   * Their values must be set when bts->oid_pos becomes 0.
	   */

	  /* The locked OID must be checked again. */
	  *which_action = BTREE_GETOID_AGAIN_WITH_CHECK;

	  return ret;		/* NO_ERROR */
	}

      /*
       * bts->prev_ovfl_vpid.pageid == NULL_PAGEID
       * It has one meaning of the following two.
       * (1) The previous key value does not have
       *     its associated OID overflow pages.
       * (2) The previous key value does not satisfy
       *     the key filter condition.
       */
      if (bts->C_vpid.pageid == NULL_PAGEID)
	{
	  /*
	   * The last pseudo oid has been locked correctly.
	   * unfix the previous leaf page
	   */
	  pgbuf_unfix (thread_p, bts->P_page);
	  bts->P_page = NULL;
	  bts->P_vpid.pageid = NULL_PAGEID;

	  /* Note : some special case (the last key) */
	  *which_action = BTREE_CONTINUE;
	  return ret;		/* NO_ERROR */
	}

      /*
       * (bts->C_vpid.pageid != NULL_PAGEID)
       * fix the current leaf page
       */
      bts->C_page = pgbuf_fix (thread_p, &bts->C_vpid, OLD_PAGE,
			       PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
      if (bts->C_page == NULL)
	{
	  goto exit_on_error;
	}

      if (LSA_EQ (&bts->cur_leaf_lsa, pgbuf_get_lsa (bts->C_page)))
	{
	  /*
	   * The last instance locking is performed correctly.
	   * unfix the previous leaf page
	   */
	  pgbuf_unfix (thread_p, bts->P_page);
	  bts->P_page = NULL;
	  bts->P_vpid.pageid = NULL_PAGEID;

	  /* the locking is correct */
	  *which_action = BTREE_CONTINUE;
	  return ret;		/* NO_ERROR */
	}

      /*
       * !LSA_EQ(&bts->cur_leaf_lsa, pgbuf_get_lsa(bts->C_page))
       * (bts->oid_pos + oid_idx) == 0
       * That is, the current leaf page has been changed.
       * However, the previous leaf page has not been changed.
       * From above facts, we can deduce following facts.
       * 1. The current leaf page cannot be deallocated.
       * 2. Overflow pages cannot be added into the previous slot.
       *    => The previous slot has not been changed.
       * The locked OID must be checked again.
       * The previous leaf page have to be prserved.
       */
      *which_action = BTREE_GETOID_AGAIN_WITH_CHECK;

      return ret;		/* NO_ERROR */
    }

  /*
   * ! LSA_EQ(prev_leaf_lsa, pgbuf_get_lsa(bts->P_page))
   * The previous leaf page has been changed.
   * At worst case, the previous leaf page might have been deallocated
   * by the merge operation of other transactions.
   */

  /* unfix the previous leaf page */
  pgbuf_unfix (thread_p, bts->P_page);
  bts->P_page = NULL;
  bts->P_vpid.pageid = NULL_PAGEID;
  bts->C_vpid.pageid = NULL_PAGEID;

  if (bts->prev_oid_pos == -1)
    {
      /*
       * This is the first request.
       * All index pages has been unfixed, now.
       */
      *which_action = BTREE_SEARCH_AGAIN_WITH_CHECK;

      return ret;		/* NO_ERROR */
    }

  /* search the previous index entry */
  bts->C_page = btree_locate_key (thread_p, &bts->btid_int, prev_key,
				  &bts->C_vpid, &bts->slot_id, &found);

  if (!found)
    {
      pgbuf_unfix (thread_p, bts->C_page);
      bts->C_page = NULL;
      *which_action = BTREE_SEARCH_AGAIN_WITH_CHECK;
      return NO_ERROR;
    }

  /* found */
  if (bts->prev_KF_satisfied == true
      || bts->tran_isolation == TRAN_SERIALIZABLE)
    {
      if (bts->prev_ovfl_vpid.pageid != NULL_PAGEID)
	{
	  bts->O_vpid = bts->prev_ovfl_vpid;
	  bts->O_page = pgbuf_fix (thread_p, &bts->O_vpid, OLD_PAGE,
				   PGBUF_LATCH_READ,
				   PGBUF_UNCONDITIONAL_LATCH);
	  if (bts->O_page == NULL)
	    {
	      goto exit_on_error;
	    }
	}

      bts->oid_pos = bts->prev_oid_pos + 1;
      /*
       * bts->oid_pos is the OID position information.
       * If the previous key has the OID overflow page, it indicates
       * the postion after the last OID position on the OID overflow page.
       * Otherwise, it indicates the position after the last OID position
       * on the oidset of the previous index entry.
       */
    }
  else
    {
      /* bts->prev_KF_satisfied == false */
      ret = btree_find_next_index_record (thread_p, bts);
      if (ret != NO_ERROR)
	{
	  goto exit_on_error;
	}
      /* bts->C_vpid.pageid can be NULL_PAGEID. */
    }

  /* The locked OID must be checked again. */
  *which_action = BTREE_GETOID_AGAIN_WITH_CHECK;

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

  return ret;
}

/*
 * btree_handle_curr_leaf_after_locking () -
 *      The handling after unconditional instance locking
 *      in case that the previous leaf page does not exist.
 *   return: NO_ERROR
 *   bts(in): pointer to B+-tree scan structure
 *   oid_idx(in): current OID position in OID-set to be scanned
 *   ovfl_page_lsa(in):
 *   prev_key(in): pointer to previous key value
 *   prev_oid_ptr(in): pointer to previously locked inst OID
 *   which_action(out): BTREE_CONTINUE
 *                      BTREE_GETOID_AGAIN
 *                      BTREE_GETOID_AGAIN_WITH_CHECK
 *                      BTREE_SEARCH_AGAIN_WITH_CHECK
 *
 * Note: This function is invoked after the unconditional instance locking
 * in case that the previous leaf page is not existent.
 * The purpose of this function is to check the validation
 * of the unconditionally acquired lock and then to adjust
 * the next processing based on the validation result.
 */
static int
btree_handle_curr_leaf_after_locking (THREAD_ENTRY * thread_p,
				      BTREE_SCAN * bts, int oid_idx,
				      LOG_LSA * ovfl_page_lsa,
				      DB_VALUE * prev_key, OID * prev_oid_ptr,
				      int *which_action)
{
  int found;
  int leaf_not_change;
  int ret = NO_ERROR;

  /*
   * Following conditions are satisfied.
   * 1. bts->P_vpid.pageid == NULL_PAGEID.
   */

  /* fix the current leaf page again */
  bts->C_page = pgbuf_fix (thread_p, &bts->C_vpid, OLD_PAGE, PGBUF_LATCH_READ,
			   PGBUF_UNCONDITIONAL_LATCH);
  if (bts->C_page == NULL)
    {
      goto exit_on_error;
    }

  /* check if the current leaf page has been changed */
  if (LSA_EQ (&bts->cur_leaf_lsa, pgbuf_get_lsa (bts->C_page)))
    {
      /* The current leaf page has not been changed. */
      if (bts->O_vpid.pageid == NULL_PAGEID)
	{
	  if (bts->prev_ovfl_vpid.pageid != NULL_PAGEID
	      && bts->oid_pos + oid_idx == 0)
	    {
	      /*
	       * The previous key value has its associated OID overflow pages.
	       * It also means prev_KF_satisfied == true.
	       */
	      bts->slot_id -= 1;

	      bts->O_vpid = bts->prev_ovfl_vpid;
	      bts->O_page = pgbuf_fix (thread_p, &bts->O_vpid, OLD_PAGE,
				       PGBUF_LATCH_READ,
				       PGBUF_UNCONDITIONAL_LATCH);
	      if (bts->O_page == NULL)
		{
		  goto exit_on_error;
		}
	      bts->oid_pos = bts->prev_oid_pos + 1;

	      /* The locked OID must be checked again. */
	      *which_action = BTREE_GETOID_AGAIN_WITH_CHECK;

	      return ret;	/* NO_ERROR */
	    }

	  /*
	   * bts->prev_ovfl_vpid.pageid == NULL_PAGEID
	   * || (bts->oid_pos + oid_idx) > 0
	   */
	  /* The current OID has not been changed. */
	  *which_action = BTREE_CONTINUE;

	  return ret;		/* NO_ERROR */
	}

      leaf_not_change = true;
    }
  else
    {
      /*
       * ! LSA_EQ(&bts->cur_page_lsa, pgbuf_get_lsa(bts->C_page))
       * The current leaf page has been changed.
       * find the current locked <key, oid> pair
       * Be careful that the locked OID can be deleted.
       */

      /* unfix the current leaf page */
      pgbuf_unfix (thread_p, bts->C_page);
      bts->C_page = NULL;
      bts->C_vpid.pageid = NULL_PAGEID;

      if ((bts->oid_pos + oid_idx) == 0 && bts->O_vpid.pageid == NULL_PAGEID)
	{
	  /* When the first OID of each key is locked */
	  if (bts->prev_oid_pos == -1)
	    {			/* the first request */
	      /* All index pages has been unfixed, now. */
	      *which_action = BTREE_SEARCH_AGAIN_WITH_CHECK;
	      return ret;	/* NO_ERROR */
	    }

	  /*
	   * bts->prev_oid_pos != -1
	   * find the previous key value again
	   */
	  bts->C_page = btree_locate_key (thread_p, &bts->btid_int, prev_key,
					  &bts->C_vpid, &bts->slot_id,
					  &found);

	  if (!found)
	    {
	      if (prev_oid_ptr->pageid == NULL_PAGEID)
		{
		  if (bts->O_vpid.pageid == NULL_PAGEID)
		    {
		      pgbuf_unfix (thread_p, bts->C_page);
		      bts->C_page = NULL;
		      *which_action = BTREE_SEARCH_AGAIN_WITH_CHECK;
		      return NO_ERROR;
		    }
		  else
		    {
		      goto search_overflow_page;
		    }
		}

	      /*
	       * Since one or more OIDs associated with
	       * the previous key value have already been locked,
	       * the previous key value must be found in the index.
	       */
	      goto exit_on_error;
	    }

	  /* found */
	  if (bts->prev_KF_satisfied == true
	      || bts->tran_isolation == TRAN_SERIALIZABLE)
	    {
	      if (bts->prev_ovfl_vpid.pageid != NULL_PAGEID)
		{
		  bts->O_vpid = bts->prev_ovfl_vpid;
		  bts->O_page = pgbuf_fix (thread_p, &bts->O_vpid, OLD_PAGE,
					   PGBUF_LATCH_READ,
					   PGBUF_UNCONDITIONAL_LATCH);
		  if (bts->O_page == NULL)
		    {
		      goto exit_on_error;
		    }
		}

	      bts->oid_pos = bts->prev_oid_pos + 1;
	      /*
	       * bts->oid_pos is the OID position information.
	       * If the previous key has the OID overflow page,
	       * it indicates the position after
	       * the last OID position on the OID overflow page.
	       * Otherwise, it indicates the position after
	       * the last OID position
	       * on the oidset of the previous index entry.
	       */
	    }
	  else
	    {
	      /*
	       * bts->prev_KF_satisfied == false
	       * bts->O_vpid.pageid must be NULL_PAGEID.
	       */
	      ret = btree_find_next_index_record (thread_p, bts);
	      if (ret != NO_ERROR)
		{
		  goto exit_on_error;
		}
	      /* bts->C_vpid.pageid can be NULL_PAGEID */
	    }

	  /* The locked OID must be checked again. */
	  *which_action = BTREE_GETOID_AGAIN_WITH_CHECK;

	  return ret;		/* NO_ERROR */
	}

      /* (bts->oid_pos+oid_idx) > 0 || bts->O_vpid.pageid != NULL_PAGEID */
      /* The locked OID is not the first OID in the index entry */
      bts->C_page = btree_locate_key (thread_p, &bts->btid_int, &bts->cur_key,
				      &bts->C_vpid, &bts->slot_id, &found);

      if (!found)
	{
	  /*
	   * Since one or more OIDs associated with
	   * the current key value have already been locked,
	   * the current key value must be found in the index.
	   */
	  goto exit_on_error;
	}

      /* found */
      if (bts->O_vpid.pageid == NULL_PAGEID)
	{
	  /* The locked OID must be checked again. */
	  *which_action = BTREE_GETOID_AGAIN_WITH_CHECK;

	  return ret;		/* NO_ERROR */
	}

    search_overflow_page:

      /* bts->O_vpid.pageid != NULL_PAGEID : go through */
      leaf_not_change = false;
    }

  /*
   * bts->O_vpid.pageid != NULL_PAGEID
   * fix the overflow page again
   */
  bts->O_page = pgbuf_fix (thread_p, &bts->O_vpid, OLD_PAGE, PGBUF_LATCH_READ,
			   PGBUF_UNCONDITIONAL_LATCH);
  if (bts->O_page == NULL)
    {
      goto exit_on_error;
    }

  /* check if the overflow page has been changed */
  if (LSA_EQ (ovfl_page_lsa, pgbuf_get_lsa (bts->O_page)))
    {
      /*
       * The current overflow page has not been changed
       * the locking is correct
       */
      *which_action = BTREE_CONTINUE;

      return ret;		/* NO_ERROR */
    }

  /*
   * !LSA_EQ(ovfl_page_lsa, pgbuf_get_lsa(bts->O_page))
   * The image of the overflow page has been changed.
   */
  if ((bts->oid_pos + oid_idx) > 0)
    {
      /*
       * One or more OIDs have already been locked correctly.
       * Hence, the set of locked OIDs could not be changed.
       * This means following two things.
       * 1) Any OID contained in the OID set could not be deleted.
       * 2) Any new OID could not be inserted in the OID set.
       * Only the currently locked OID and the remaining OIDs
       * could be deleted and also any new OID could be inserted in
       * the OID set of the currently locked OID and remaining OIDs.
       * Therefore, there is no possibility that the current
       * overflow page has been freed during the lock acquisition.
       */
      /* The locked OID must be checked again. */
      *which_action = BTREE_GETOID_AGAIN_WITH_CHECK;

      return ret;		/* NO_ERROR */
    }

  /*
   * (bts->oid_pos+idx) == 0 : idx == 0 && bts->oid_pos == 0
   */
  if (leaf_not_change == true && bts->prev_ovfl_vpid.pageid == NULL_PAGEID)
    {
      /* In this case, the overflow page is the first OID overflow page
       * that is connected from the current index entry.
       * If the corresponding leaf page has not been changed,
       * it is guaranteed that the OID overflow page is not deallocated.
       * The OID overflow page is still the first OID overflow page.
       */
      /* The locked OID must be checked again. */
      *which_action = BTREE_GETOID_AGAIN_WITH_CHECK;

      return ret;		/* NO_ERROR */
    }

  /* unfix the overflow page */
  pgbuf_unfix (thread_p, bts->O_page);
  bts->O_page = NULL;
  bts->O_vpid.pageid = NULL_PAGEID;

  /* find the next OID again */
  if (bts->prev_ovfl_vpid.pageid != NULL_PAGEID)
    {
      bts->O_vpid = bts->prev_ovfl_vpid;
      bts->O_page = pgbuf_fix (thread_p, &bts->O_vpid, OLD_PAGE,
			       PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
      if (bts->O_page == NULL)
	{
	  goto exit_on_error;
	}
    }

  bts->oid_pos = bts->prev_oid_pos + 1;

  /* bts->oid_pos is the OID position information.
   * If the previous OID overflow page exists, it indicates
   * the postion after the last OID position on the OID overflow page.
   * Otherwise, it indicates the position after the last OID position
   * on the oidset of the current index entry.
   */

  /* The locked OID must be checked again. */
  *which_action = BTREE_GETOID_AGAIN_WITH_CHECK;

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

  return ret;
}
#endif /* SERVER_MODE */

/*
 * btree_range_search () -
 *   return: OIDs count
 *   btid(in): B+-tree identifier
 *   readonly_purpose(in):
 *   lock_hint(in):
 *   bts(in): B+-tree scan structure
 *   key1(in): the lower bound key value of key range
 *   key2(in): the upper bound key value of key range
 *   range(in): the range of key range
 *   num_classes(in): number of classes contained in class_oids_ptr
 *   class_oids_ptr(in): target classes that are queried
 *   oids_ptr(in): memory space for stroing scanned OIDs
 *   oids_size(in): the size of the memory space(oids_ptr)
 *   filter(in): key filter
 *   isidp(in):
 *   need_construct_btid_int(in):
 *   need_count_only(in):
 *
 * Note: This functions performs key range search function.
 * Instance level locking function is added in this function.
 */
int
btree_range_search (THREAD_ENTRY * thread_p, BTID * btid,
		    int readonly_purpose, int lock_hint, BTREE_SCAN * bts,
		    DB_VALUE * key1, DB_VALUE * key2, RANGE range,
		    int num_classes, OID * class_oids_ptr, OID * oids_ptr,
		    int oids_size, FILTER_INFO * filter,
		    INDX_SCAN_ID * index_scan_id_p,
		    bool need_construct_btid_int, bool need_count_only)
{
  OID *mem_oid_ptr;
  int pg_oid_cnt;
  int oids_cnt = 0;
  int oid_size;
  int inst_oid_offset;
  int cp_oid_cnt;
  int rec_oid_cnt;
  char *rec_oid_ptr;
  bool is_key_range_satisfied = true;
  bool is_key_filter_satisfied = true;
  bool is_condition_satisfied = true;
  RECDES rec;
  LEAF_REC Leaf_Pnt;
  int offset;
  bool dummy_clear;
  int i, j;
  int unsatisfied_cnt;
  OID class_oid;
#if defined(SERVER_MODE)
  int CLS_satisfied = true;
  int lock_ret;
  OID inst_oid;
  OID saved_class_oid;
  OID saved_inst_oid;
  OID temp_oid;
  char oid_space[2 * OR_OID_SIZE];
  int which_action;
  bool clear_prev_key = false;
  DB_VALUE prev_key;
  LOG_LSA prev_leaf_lsa;
  LOG_LSA ovfl_page_lsa;
  int tran_index, s;
  bool keep_on_copying = false;
  int new_size;
  char *new_ptr;
  bool read_cur_key = false;
#endif /* SERVER_MODE */

#if defined(BTREE_DEBUG)
  if (BTREE_INVALID_INDEX_ID (btid))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_BTREE_INVALID_INDEX_ID,
	      3, btid->vfid.fileid, btid->vfid.volid, btid->root_pageid);
      return -1;
    }
#endif /* BTREE_DEBUG */

  /* initialize key filter */
  bts->key_filter = filter;	/* valid pointer or NULL */

#if defined(SERVER_MODE)
  db_make_null (&prev_key);
#endif /* SERVER_MODE */

  /* The first request of btree_range_search() */
  if (bts->C_vpid.pageid == NULL_PAGEID)
    {
#if defined(BTREE_DEBUG)
      /* check oids_size */
      if (oids_size < OR_OID_SIZE)
	{
	  er_log_debug (ARG_FILE_LINE,
			"btree_range_search: Not enough area to store oid set.\n");
	  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_GENERIC_ERROR, 0);
	  return -1;
	}
#endif /* BTREE_DEBUG */

      /* check range */
      switch (range)
	{
	case EQ_NA:
	case GT_LT:
	case GT_LE:
	case GE_LT:
	case GE_LE:
	case GE_INF:
	case GT_INF:
	case INF_LE:
	case INF_LT:
	case INF_INF:
	  break;
	default:
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_BTREE_INVALID_RANGE,
		  0);
	  return -1;
	}

      /* initialize the bts */
      if (btree_initialize_bts
	  (thread_p, bts, btid, readonly_purpose, lock_hint, class_oids_ptr,
	   key1, key2, range, filter, need_construct_btid_int,
	   index_scan_id_p->copy_buf,
	   index_scan_id_p->copy_buf_len) != NO_ERROR)
	{
	  goto error;
	}

      /* check lower value and upper value */
      /* if partial-key is desc, swap lower value and upper value */
      if (BTREE_IS_PART_KEY_DESC (&(bts->btid_int)))
	{
	  bool tmp_clear;
	  DB_VALUE *tmp_key;

	  tmp_clear = bts->key_range.clear_lower;
	  tmp_key = bts->key_range.lower_key;

	  bts->key_range.clear_lower = bts->key_range.clear_upper;
	  bts->key_range.lower_key = bts->key_range.upper_key;

	  bts->key_range.clear_upper = tmp_clear;
	  bts->key_range.upper_key = tmp_key;
	}
    }

  /* number of OIDs that can be packed in the supplied area */
  mem_oid_ptr = oids_ptr;
  pg_oid_cnt = oids_size / OR_OID_SIZE;
  oids_cnt = 0;			/* # of copied OIDs */

  /* get the size of each OID information in the index */
  if (BTREE_IS_UNIQUE (&(bts->btid_int)))
    {
      oid_size = (2 * OR_OID_SIZE);
      inst_oid_offset = OR_OID_SIZE;
    }
  else
    {
      oid_size = OR_OID_SIZE;
      inst_oid_offset = 0;
    }

#if defined(SERVER_MODE)
  /* intialize saved_inst_oid */
  saved_inst_oid.pageid = NULL_PAGEID;

search_again:
#endif /* SERVER_MODE */
  if (bts->C_vpid.pageid == NULL_PAGEID)
    {
#if defined(SERVER_MODE)
      /* initialize 'prev_oid_pos' and 'prev_ovfl_vpid' */
      bts->prev_oid_pos = -1;
      bts->prev_ovfl_vpid.pageid = NULL_PAGEID;
#endif /* SERVER_MODE */

      /* the first request */
      if (btree_prepare_first_search (thread_p, bts) != NO_ERROR)
	{
	  goto error;
	}
    }
  else
    {
      /* not the first request */
      if (btree_prepare_next_search (thread_p, bts) != NO_ERROR)
	{
	  goto error;
	}
    }

get_oidcnt_and_oidptr:
  /* get 'rec_oid_cnt' and 'rec_oid_ptr' */

  if (bts->C_vpid.pageid == NULL_PAGEID)
    {
      /* It reached at the end of leaf level */
#if defined(SERVER_MODE)
      if (bts->read_uncommitted)
	{
	  goto end_of_scan;
	}

      is_key_range_satisfied = false;
      is_condition_satisfied = false;

      /*
       * If bts->key_range_max_value_equal is true,
       * lock on the next key is not required,
       * even if the index is non-unique
       */
      if (bts->key_range_max_value_equal)
	{
	  goto end_of_scan;
	}

      bts->oid_pos = 0;
      rec_oid_cnt = 1;
      if (BTREE_IS_UNIQUE (&(bts->btid_int)))
	{			/* unique index */
	  temp_oid.volid = bts->btid_int.sys_btid->vfid.volid;
	  temp_oid.pageid = bts->btid_int.sys_btid->root_pageid;
	  temp_oid.slotid = 0;
	  rec_oid_ptr = &oid_space[0];
	  OR_PUT_OID (rec_oid_ptr, &temp_oid);
	  temp_oid.slotid = -1;
	  OR_PUT_OID (rec_oid_ptr + OR_OID_SIZE, &temp_oid);
	}
      else
	{			/* non-unique index */
	  temp_oid.volid = bts->btid_int.sys_btid->vfid.volid;
	  temp_oid.pageid = bts->btid_int.sys_btid->root_pageid;
	  temp_oid.slotid = -1;
	  rec_oid_ptr = &oid_space[0];
	  OR_PUT_OID (rec_oid_ptr, &temp_oid);
	}

      goto start_locking;
#else /* SERVER_MODE */
      goto end_of_scan;
#endif /* SERVER_MODE */
    }

  /* Find the position of OID list to be searched in the index entry */
  if (bts->O_page != NULL)
    {
      /*
       * bts->O_page != NULL : current overflow page
       * Why COPY method be used in reading an index record ?
       * PEEK method can be used instead of COPY method.
       * The reason is described when which_action is BTREE_CONTINUE
       * after the unconditional locking.
       */
      if (spage_get_record (bts->O_page, 1, &rec, PEEK) != S_SUCCESS)
	{
	  goto error;
	}

      /* # of OIDs contained in the overflow page */
      rec_oid_cnt = CEIL_PTVDIV (rec.length, oid_size);
      if (bts->oid_pos < rec_oid_cnt)
	{
	  rec_oid_ptr = rec.data + (bts->oid_pos * oid_size);
	}
      else
	{
#if defined(SERVER_MODE)
	  bts->prev_oid_pos = rec_oid_cnt - 1;
	  bts->prev_ovfl_vpid = bts->O_vpid;
#endif /* SERVER_MODE */

	  /* the 2nd argument, first_ovfl_vpid, is NULL */
	  if (btree_get_next_oidset_pos (thread_p, bts, (VPID *) NULL) !=
	      NO_ERROR)
	    {
	      goto error;
	    }

	  goto get_oidcnt_and_oidptr;
	}
    }
  else
    {
      /* bts->O_page == NULL : current leaf page */
      if (spage_get_record (bts->C_page, bts->slot_id, &rec, PEEK)
	  != S_SUCCESS)
	{
	  goto error;
	}

      if (bts->oid_pos > 0)
	{			/* same key value */
	  /* The key range and key filter checking is not needed. */
	  (void) btree_read_record (thread_p, &bts->btid_int, &rec, NULL,
				    &Leaf_Pnt, true, &dummy_clear, &offset,
				    0);

	  /* get 'rec_oid_cnt' and 'rec_oid_ptr' */
	  rec_oid_cnt = CEIL_PTVDIV (rec.length - offset, oid_size);
	  if (bts->oid_pos < rec_oid_cnt)
	    {
	      rec_oid_ptr = rec.data + offset + (bts->oid_pos * oid_size);
	    }
	  else
	    {
#if defined(SERVER_MODE)
	      bts->prev_oid_pos = rec_oid_cnt - 1;
	      bts->prev_ovfl_vpid.pageid = NULL_PAGEID;
#endif /* SERVER_MODE */

	      /*
	       * check if next oidset is in overflow page
	       * or next index record. bts->oid_pos, slot_id may be changed
	       */
	      if (btree_get_next_oidset_pos (thread_p, bts, &Leaf_Pnt.ovfl) !=
		  NO_ERROR)
		{
		  goto error;
		}

	      goto get_oidcnt_and_oidptr;
	    }
	}
      else
	{			/* new key value */
#if defined(SERVER_MODE)
	  if (!bts->read_uncommitted && read_cur_key)
	    {
	      btree_clear_key_value (&clear_prev_key, &prev_key);

	      pr_clone_value (&bts->cur_key, &prev_key);
	      /* pr_clone_value allocates and copies a DB_VALUE */
	      clear_prev_key = bts->clear_cur_key;

	      read_cur_key = false;	/* reset read_cur_key */
	    }
#endif /* SERVER_MODE */

	  btree_clear_key_value (&bts->clear_cur_key, &bts->cur_key);

	  (void) btree_read_record (thread_p, &bts->btid_int, &rec,
				    &bts->cur_key, &Leaf_Pnt, true,
				    &bts->clear_cur_key, &offset, 1);
	  /* the last argument means that key value must be copied. */

#if defined(SERVER_MODE)
	  read_cur_key = true;
#endif /* SERVER_MODE */

	  /* get 'rec_oid_cnt' and 'rec_oid_ptr' */
	  rec_oid_cnt = CEIL_PTVDIV (rec.length - offset, oid_size);
	  rec_oid_ptr = rec.data + offset;

#if defined(SERVER_MODE)
	  /* save the result of key filtering on the previous key value */
	  if (saved_inst_oid.pageid == NULL_PAGEID)
	    bts->prev_KF_satisfied = is_key_filter_satisfied;
#endif /* SERVER_MODE */

	  /* apply key range and key filter to the new key value */
	  if (btree_apply_key_range_and_filter
	      (thread_p, bts, &is_key_range_satisfied,
	       &is_key_filter_satisfied) != NO_ERROR)
	    {
	      goto error;
	    }

	  if (is_key_range_satisfied == false)
	    {
	      is_condition_satisfied = false;
	      if (bts->read_uncommitted)
		{
		  goto end_of_scan;
		}
	    }
	  else
	    {
	      if (is_key_filter_satisfied == false)
		{
		  is_condition_satisfied = false;
		  if (bts->read_uncommitted)
		    {
#if defined(SERVER_MODE)
		      /* clear 'prev_oid_pos' and 'prev_ovfl_vpid' */
		      bts->prev_oid_pos = 0;
		      bts->prev_ovfl_vpid.pageid = NULL_PAGEID;
#endif /* SERVER_MODE */

		      if (btree_find_next_index_record (thread_p, bts) !=
			  NO_ERROR)
			{
			  goto error;
			}

		      goto get_oidcnt_and_oidptr;
		    }
		}
	      else
		{
		  is_condition_satisfied = true;
		}
	    }
	}
    }

start_locking:

  /* check the validity for locking and copying OIDs */
  if (rec_oid_cnt <= 0)
    {
      er_log_debug (ARG_FILE_LINE,
		    "index inconsistency..(rec_oid_cnt(%d) <= 0)\n",
		    rec_oid_cnt);
      goto error;
    }

  if ((rec_oid_cnt - bts->oid_pos) < 0)
    {
      goto locking_done;
    }

#if defined(SERVER_MODE)
  if (bts->read_uncommitted)
    {
      /*
       * NOTE) In Uncommitted Read transation isolation level,
       * even if the oids contained in one index entry is so many,
       * all of the oids must be copied in one btree_range_search() execution.
       * The reason is caused from the key deletion operation of CUBRID.
       * After deleting one OID in an index entry in key deletion operation,
       * CUBRID moves the last OID of the index entry into the position
       * on which the deleted OID was existent. Therefore,
       * the range search without locking might not copy the OID.
       */

      if (keep_on_copying)
	{
	  /* Curently, it is copying OID set whose size is so long. */
	  if (bts->O_vpid.pageid == NULL_PAGEID && bts->oid_pos == 0)
	    {
	      /* when the first OID of the next key is found */

#if defined(SERVER_MODE)
	      LSA_COPY (&bts->cur_leaf_lsa, pgbuf_get_lsa (bts->C_page));
	      btree_clear_key_value (&clear_prev_key, &prev_key);
#endif /* SERVER_MODE */

	      /* do not clear bts->cur_key here for btree_prepare_next_search */

	      goto resume_next_search;
	    }
	}

      while (1)
	{
	  if (need_count_only == true
	      || (rec_oid_cnt - bts->oid_pos) <= (pg_oid_cnt - oids_cnt))
	    {
	      /* OID memory space is sufficient */
	      cp_oid_cnt = rec_oid_cnt - bts->oid_pos;
	      break;
	    }

	  if (pg_oid_cnt < 10)
	    {
	      /*
	       * some special purpose :
	       * It's purpose is to find out uniqueness in unique index.
	       * Therefore, copying only part of OID set(more than 1 OID)
	       * is sufficient. That is, the caller can identify
	       * the uniqueness through the number of copied OIDs.
	       */
	      cp_oid_cnt = pg_oid_cnt - oids_cnt;
	      break;
	    }

	  /* OID memory space is insufficient */
	  if (bts->O_vpid.pageid == NULL_PAGEID && bts->oid_pos == 0)
	    {
	      /* when the first OID of each key is found */
	      if (oids_cnt > 0)
		{
#if defined(SERVER_MODE)
		  LSA_COPY (&bts->cur_leaf_lsa, pgbuf_get_lsa (bts->C_page));
		  btree_clear_key_value (&clear_prev_key, &prev_key);
#endif /* SERVER_MODE */

		  /* do not clear bts->cur_key for btree_prepare_next_search */

		  goto resume_next_search;
		}

	      /* oids_cnt == 0 */
	      er_log_debug (ARG_FILE_LINE,
			    "btree_range_search() : OID memory space is too small");
	    }

	  new_size = (pg_oid_cnt * OR_OID_SIZE) + oids_size;
	  new_ptr = (char *) realloc (oids_ptr, new_size);
	  if (new_ptr == NULL)
	    {
	      /* memory space allocation has failed. */
	      er_log_debug (ARG_FILE_LINE,
			    "btree_range_search() : Part of OIDs are copied in Uncommitted Read"
			    " or The size of OID set is so large");
	      /* copy some of the remaining OIDs */
	      cp_oid_cnt = pg_oid_cnt - oids_cnt;
	      break;
	    }

	  /* The memory space allocation has succeeded. */
	  index_scan_id_p->oid_list.oidp = oids_ptr = (OID *) new_ptr;
	  pg_oid_cnt = new_size / OR_OID_SIZE;
	  mem_oid_ptr = oids_ptr + oids_cnt;
	  index_scan_id_p->curr_oidp = (OID *) new_ptr;
	  keep_on_copying = true;
	}

      /* copy corresponding OIDs */
      if (!BTREE_IS_UNIQUE (&(bts->btid_int)) || num_classes == 0)
	{
	  /*
	   * 1. current index is a non-unique index. or
	   * 2. current index is an unique index. &&
	   *    current query is based on all classes
	   *    contained in the class hierarchy.
	   */
	  for (i = 0; i < cp_oid_cnt; i++)
	    {
	      if (need_count_only == false)
		{
		  /* normal scan - copy OID */
		  OR_GET_OID (rec_oid_ptr + inst_oid_offset, mem_oid_ptr);
		  mem_oid_ptr++;
		}
	      rec_oid_ptr += oid_size;
	    }

	  /* update counts */
	  bts->oid_pos += cp_oid_cnt;
	  oids_cnt += cp_oid_cnt;
	}
      else
	{
	  /*
	   * current index is an unique index. &&
	   * current query is based on some classes
	   * contained in the class hierarchy.
	   */
	  unsatisfied_cnt = 0;
	  for (i = 0; i < cp_oid_cnt; i++)
	    {
	      /* The class oid comparison must be performed. */
	      OR_GET_OID (rec_oid_ptr, &class_oid);
	      for (j = 0; j < num_classes; j++)
		{
		  if (OID_EQ (&class_oid, &class_oids_ptr[j]))
		    {
		      break;
		    }
		}

	      if (j < num_classes)
		{		/* satisfying OID */
		  if (need_count_only == false)
		    {
		      /* normal scan - copy OID */
		      OR_GET_OID (rec_oid_ptr + OR_OID_SIZE, mem_oid_ptr);
		      mem_oid_ptr++;
		    }
		  rec_oid_ptr += oid_size;
		}
	      else
		{		/* unsatisfying OID */
		  rec_oid_ptr += oid_size;
		  unsatisfied_cnt++;
		}
	    }

	  /* update counts */
	  bts->oid_pos += cp_oid_cnt;
	  oids_cnt += (cp_oid_cnt - unsatisfied_cnt);
	}

      goto locking_done;
    }

  /*
   * General method of next-key locking is used in the current code.
   * Both unique index and non-unique index are concerned.
   * In case of non-unique index,
   * the class OID information may not be included in bts structure.
   * In this case, class OID information can be gotten from
   * the first found key and the memory address of the class lock mode
   * can be gotten through the class OID information.
   * After getting the information, save the information in bts structure.
   */
  if (!BTREE_IS_UNIQUE (&(bts->btid_int)) && OID_ISNULL (&bts->cls_oid))
    {
      OR_GET_OID (rec_oid_ptr, &temp_oid);
      if (heap_get_class_oid (thread_p, &temp_oid, &bts->cls_oid) == NULL)
	{
	  goto error;
	}

      tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);

      bts->cls_lock_ptr = lock_get_class_lock (&bts->cls_oid, tran_index);
      if (bts->cls_lock_ptr == NULL)
	{
	  /*
	   * CLASS LOCK MUST BE ACQUIRED
	   *
	   * The corresponding class lock has not been acquired currently.
	   * The class lock must be acquired before an index scan based on
	   * the class is requested.
	   */
	  er_log_debug (ARG_FILE_LINE,
			"bts->cls_lock_ptr == NULL in btree_initialize_bts()\n"
			"bts->cls_oid = <%d,%d,%d>\n",
			bts->cls_oid.volid, bts->cls_oid.pageid,
			bts->cls_oid.slotid);
	  goto error;
	}
    }

  /*
   * bts->tran_isolation :
   * TRAN_REP_CLASS_COMMIT_INSTANCE, TRAN_COMMIT_CLASS_COMMIT_INSTANCE
   * TRAN_REP_CLASS_REP_INSTANCE
   * TRAN_SERIALIZABLE
   */

  if (saved_inst_oid.pageid != NULL_PAGEID)
    {
      /*
       * The instance level locking on current key had already
       * been performed. Now, check if the locking is valid.
       * get the current instance OID
       */
      OR_GET_OID (rec_oid_ptr + inst_oid_offset, &inst_oid);

      /* compare the OID with the unconditionally locked OID */
      if (OID_EQ (&saved_inst_oid, &inst_oid))
	{
	  /* clear saved OID information */
	  saved_inst_oid.pageid = NULL_PAGEID;

	  /* unfix the previous leaf page if it is fixed */
	  /* It is possible in a isolation of TRAN_SERIALIZABLE */
	  if (bts->P_page != NULL)
	    {
	      pgbuf_unfix (thread_p, bts->P_page);
	      bts->P_page = NULL;
	      bts->P_vpid.pageid = NULL_PAGEID;
	    }

	  /*
	   * In an isolation level of TRAN_SERIALIZABLE,
	   * 'is_condition_satisfied' flag might be false.
	   */
	  if (is_condition_satisfied == false)
	    {
	      if (bts->tran_isolation != TRAN_SERIALIZABLE)
		{
		  goto locking_done;
		}

	      rec_oid_ptr += oid_size;
	      bts->oid_pos += 1;
	    }
	  else
	    {
	      if (CLS_satisfied == true)
		{
		  /* copy the OID */
		  if (need_count_only == false)
		    {
		      /* normal scan - copy OID */
		      COPY_OID (mem_oid_ptr, &inst_oid);
		      mem_oid_ptr++;
		    }

		  rec_oid_ptr += oid_size;

		  /* update counts */
		  bts->oid_pos += 1;
		  oids_cnt += 1;
		}
	      else
		{
		  rec_oid_ptr += oid_size;
		  bts->oid_pos += 1;
		}
	    }

	  /*
	   * If the current index is an unique index,
	   * check if the current index is consistent.
	   */
	  if (BTREE_IS_UNIQUE (&(bts->btid_int)))
	    {
	      if (bts->oid_pos > 1)
		{
		  er_log_debug (ARG_FILE_LINE,
				"index inconsistency..(unique violation)\n");
		  goto error;
		}
	      /* bts->oid_pos == 1 */

	      if (rec_oid_cnt == 1)
		{
		  /* (rec_oid_cnt - bts->oid_pos) == 0 */
		  goto locking_done;
		}

	      /*
	       * If rec_oid_cnt > 1, other OIDs are in uncommitted state.
	       * In this case, do locking to wait uncommitted transactions.
	       */
	    }
	}
      else
	{
	  /* unlock the instance lock */
	  if (bts->cls_lock_ptr != NULL)
	    {
	      /*
	       * single class index
	       * In case of non-unique index (CURRENT VERSION)
	       */
	      if (bts->cls_lock_ptr->granted_mode < bts->escalated_mode)
		{
		  lock_unlock_object (thread_p, &saved_inst_oid,
				      &saved_class_oid, bts->lock_mode, true);
		}

	      /*
	       * If class lock has been escalated,
	       * the release of corresponding instance lock is not needed.
	       */
	    }
	  else
	    {
	      /* class hierarchy index */
	      /* In case of unique index of class hierarchy form */
	      for (s = 0; s < bts->class_lock_map_count; s++)
		{
		  if (OID_EQ (&saved_class_oid, &bts->class_lock_map[s].oid))
		    {
		      break;
		    }
		}

	      if (s < bts->class_lock_map_count)
		{
		  if (bts->class_lock_map[s].lock_ptr->granted_mode
		      < bts->escalated_mode)
		    {
		      lock_unlock_object (thread_p, &saved_inst_oid,
					  &saved_class_oid,
					  bts->lock_mode, true);
		    }

		  /*
		   * If the class lock has been escalated,
		   * the release of the instance lock is not needed.
		   */
		}
	      else
		{
		  lock_unlock_object (thread_p, &saved_inst_oid,
				      &saved_class_oid, bts->lock_mode, true);
		  /*
		   * Note the implementation of lock_unlock_object().
		   * Even though certain class lock has been escalated,
		   * the request for releaing instance lock of the class
		   * must be processed correctly.
		   */
		}
	    }

	  /* clear saved OID information */
	  saved_inst_oid.pageid = NULL_PAGEID;
	}
    }

  /* compute 'cp_oid_cnt' */
  if (is_condition_satisfied == false
      && bts->tran_isolation != TRAN_SERIALIZABLE)
    {
      /* only in case of TRAN_SERIALIZABLE */
      cp_oid_cnt = 1;
    }
  else
    {
      if (need_count_only == true)
	{			/* do not concern buffer size */
	  cp_oid_cnt = rec_oid_cnt - bts->oid_pos;
	}
      else
	{
	  cp_oid_cnt = MIN (pg_oid_cnt - oids_cnt,
			    rec_oid_cnt - bts->oid_pos);
	}

      if (cp_oid_cnt <= 0)
	{			/* for uncommitted read */
	  goto locking_done;
	}
    }

  /*
   * If S_LOCK or more strong lock(SIX_LOCK or X_LOCK) has been held
   * on the class, the instance level locking is not needed.
   */
  if (bts->cls_lock_ptr != NULL
      && bts->cls_lock_ptr->granted_mode >= bts->escalated_mode)
    {
      /* single class index */

      /* In TRAN_SERIALIZABLE, is_condition_satisfied can be false. */
      if (is_condition_satisfied == false)
	{
	  bts->oid_pos += cp_oid_cnt;
	  goto locking_done;
	}

      /*
       * Current index is either a non-unique index or
       * an unique index of single class index form.
       */
#if defined(BTREE_DEBUG)
      if (BTREE_IS_UNIQUE (&(bts->btid_int)))
	{
	  /* In case of unique index */
	  /* check the consistency of current index entry. */
	  /* NOT IMPLEMENTED */
	  if (cp_oid_cnt > 1)
	    {
	      er_log_debug (ARG_FILE_LINE,
			    "cp_oid_cnt > 1 in an unique index\n"
			    "index inconsistency..(unique violation)\n");
	      goto error;
	    }
	  /* cp_oid_cnt == 1 */
	}
#endif /* BTREE_DEBUG */

      /* copy all the OIDs */
      for (j = 0; j < cp_oid_cnt; j++)
	{
	  if (need_count_only == false)
	    {
	      /* normal scan - copy OID */
	      OR_GET_OID (rec_oid_ptr + inst_oid_offset, mem_oid_ptr);
	      mem_oid_ptr++;
	    }

	  rec_oid_ptr += oid_size;
	}

      /* update counts */
      bts->oid_pos += cp_oid_cnt;
      oids_cnt += cp_oid_cnt;

      goto locking_done;
    }

  /*
   * If bts->key_range_max_value_equal is true,
   * lock on the next key is not required
   */
  if (is_key_range_satisfied == false && bts->key_range_max_value_equal)
    {
      goto end_of_scan;
    }

  /*
   * locking and copying corresponding OIDs
   */

  unsatisfied_cnt = 0;
  for (i = 0; i < cp_oid_cnt; i++)
    {
      /* reset CLS_satisfied flag to true */
      CLS_satisfied = true;

      /* checking phase */
      if (bts->cls_lock_ptr != NULL)
	{
	  /*
	   * Single class index
	   * Current index is one of the following two indexes.
	   * 1. non-unique index
	   * 2. unique index that having single class index form
	   * In current implementation,
	   * only non-unique index can be in this situation.
	   */
	  /* check if instance level locking is needed. */
	  if (bts->cls_lock_ptr->granted_mode >= bts->escalated_mode)
	    {
	      /*
	       * The class lock has been escalated to S_LOCK, SIX_LOCK,
	       * or X_LOCK mode. Therefore, there is no need to acquire
	       * locks on the scanned instances.
	       */

	      if (is_condition_satisfied == false)
		{
		  bts->oid_pos += cp_oid_cnt;
		  goto locking_done;
		}

#if defined(BTREE_DEBUG)
	      if (BTREE_IS_UNIQUE (&(bts->btid_int)))
		{
		  /* In case of unique index
		   * check the consistency of current index entry.
		   * NOT IMPLEMENTED */
		  if (cp_oid_cnt > 1)
		    {
		      er_log_debug (ARG_FILE_LINE,
				    "cp_oid_cnt > 1 in an unique index\n"
				    "index inconsistency..(unique violation)\n");
		      goto error;
		    }
		  /* 'cp_oid_cnt == 1' is guaranteed. */
		}
#endif

	      /* copy the remaining OIDs */
	      for (j = i; j < cp_oid_cnt; j++)
		{
		  if (need_count_only == false)
		    {
		      /* normal scan - copy OID */
		      OR_GET_OID (rec_oid_ptr + inst_oid_offset, mem_oid_ptr);
		      mem_oid_ptr++;
		    }
		  rec_oid_ptr += oid_size;
		}

	      /* update counts */
	      bts->oid_pos += cp_oid_cnt;
	      oids_cnt += cp_oid_cnt;

	      /* In this case, unsatisfied_cnt is 0(zero). */
	      goto locking_done;
	    }

	  /*
	   * bts->cls_lock_ptr) < bts->escalated_mode :
	   * instance level locking must be performed.
	   */
	  /* get current class OID and instance OID */
	  if (BTREE_IS_UNIQUE (&(bts->btid_int)))
	    {
	      OR_GET_OID (rec_oid_ptr, &class_oid);
	      OR_GET_OID (rec_oid_ptr + OR_OID_SIZE, &inst_oid);
	    }
	  else
	    {
	      COPY_OID (&class_oid, &bts->cls_oid);
	      OR_GET_OID (rec_oid_ptr, &inst_oid);
	    }
	}
      else
	{
	  /*
	   * Class hierarchy index
	   * Current index is an unique index
	   * that having class hierarchy index form.
	   */
	  /* get current class OID */
	  OR_GET_OID (rec_oid_ptr, &class_oid);

	  /* check if the current class OID is query-based class */
	  if (num_classes > 0 && is_condition_satisfied == true)
	    {
	      /*
	       * Current unique index is a class hierarchy index and
	       * current scan is based on some classes of the class hierarchy.
	       * In this case, some satisfying OIDs will be scaned.
	       */
	      for (j = 0; j < num_classes; j++)
		{
		  if (OID_EQ (&class_oid, &class_oids_ptr[j]))
		    {
		      break;
		    }
		}
	      if (j >= num_classes)
		{
		  CLS_satisfied = false;
		}
	    }

	  /*
	   * check the class lock mode to find out
	   * if the instance level locking should be performed.
	   */
	  for (s = 0; s < bts->class_lock_map_count; s++)
	    {
	      if (OID_EQ (&class_oid, &bts->class_lock_map[s].oid))
		{
		  break;
		}
	    }

	  if (s == bts->class_lock_map_count)
	    {			/* not found */
	      if (s < BTREE_CLASS_LOCK_MAP_MAX_COUNT)
		{
		  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);

		  bts->class_lock_map[s].lock_ptr =
		    lock_get_class_lock (&class_oid, tran_index);
		  if (bts->class_lock_map[s].lock_ptr != NULL)
		    {
		      COPY_OID (&bts->class_lock_map[s].oid, &class_oid);
		      bts->class_lock_map_count++;
		    }
		}
	    }

	  if (s < bts->class_lock_map_count)
	    {
	      if (bts->class_lock_map[s].lock_ptr->granted_mode >=
		  bts->escalated_mode)
		{
		  /* the instance level locking is not needed. */
		  if (is_condition_satisfied && CLS_satisfied)
		    {
		      if (need_count_only == false)
			{
			  /* normal scan - copy OID */
			  OR_GET_OID (rec_oid_ptr + OR_OID_SIZE, mem_oid_ptr);
			  mem_oid_ptr++;
			}

		      rec_oid_ptr += oid_size;
		    }
		  else
		    {
		      rec_oid_ptr += oid_size;
		      unsatisfied_cnt++;
		    }

		  continue;
		}
	      /* instance level locking must be performed */
	    }

	  /* get current instance OID */
	  OR_GET_OID (rec_oid_ptr + OR_OID_SIZE, &inst_oid);
	}

      /*
       * CONDITIONAL lock request (current waitsecs : 0)
       */
      lock_ret =
	lock_object_on_iscan (thread_p, &inst_oid, &class_oid, bts->lock_mode,
			      LK_COND_LOCK,
			      index_scan_id_p->scan_cache.scanid_bit);
      if (lock_ret == LK_GRANTED)
	{
	  if (is_condition_satisfied && CLS_satisfied)
	    {
	      if (need_count_only == false)
		{
		  /* normal scan - copy OID */
		  OR_GET_OID (rec_oid_ptr + inst_oid_offset, mem_oid_ptr);
		  mem_oid_ptr++;
		}

	      rec_oid_ptr += oid_size;
	    }
	  else
	    {
	      rec_oid_ptr += oid_size;
	      unsatisfied_cnt++;
	    }

	  continue;
	}
      else if (lock_ret == LK_NOTGRANTED_DUE_ABORTED)
	{
	  goto error;
	}

      /* unfix all the index pages */
      if (bts->P_page != NULL)
	{
	  LSA_COPY (&prev_leaf_lsa, pgbuf_get_lsa (bts->P_page));
	  pgbuf_unfix (thread_p, bts->P_page);
	  bts->P_page = NULL;
	}

      if (bts->C_page != NULL)
	{
	  LSA_COPY (&bts->cur_leaf_lsa, pgbuf_get_lsa (bts->C_page));
	  pgbuf_unfix (thread_p, bts->C_page);
	  bts->C_page = NULL;
	}

      if (bts->O_page != NULL)
	{
	  LSA_COPY (&ovfl_page_lsa, pgbuf_get_lsa (bts->O_page));
	  pgbuf_unfix (thread_p, bts->O_page);
	  bts->O_page = NULL;
	}

      /*
       * Following page ids are maintained.
       * bts->P_vpid, bts->C_vpid, bts->O_vpid
       */

      /* UNCONDITIONAL lock request */

      lock_ret =
	lock_object_on_iscan (thread_p, &inst_oid, &class_oid, bts->lock_mode,
			      LK_UNCOND_LOCK,
			      index_scan_id_p->scan_cache.scanid_bit);
      if (lock_ret != LK_GRANTED)
	{
	  /* LK_NOTGRANTED_DUE_ABORTED, LK_NOTGRANTED_DUE_TIMEOUT */
	  goto error;
	}

      if (bts->P_vpid.pageid != NULL_PAGEID)
	{
	  /* The previous leaf page does exist. */
	  if (btree_handle_prev_leaf_after_locking
	      (thread_p, bts, i, &prev_leaf_lsa, &prev_key,
	       &which_action) != NO_ERROR)
	    {
	      goto error;
	    }
	}
      else
	{
	  /* The previous leaf page does not exist. */
	  if (btree_handle_curr_leaf_after_locking
	      (thread_p, bts, i, &ovfl_page_lsa, &prev_key, &saved_inst_oid,
	       &which_action) != NO_ERROR)
	    {
	      goto error;
	    }
	}

      /*
       * if max_value_equal is true
       * then compare previous key value and max_value of range
       * if two aren't equal, reset max_value_equal as false
       */

      if (which_action != BTREE_CONTINUE && bts->key_range_max_value_equal)
	{
	  if (bts->prev_oid_pos == -1)
	    {
	      bts->key_range_max_value_equal = false;
	    }
	  else if ((!BTREE_IS_LAST_KEY_DESC (&(bts->btid_int))
		    && (bts->key_range.range == GT_LE
			|| bts->key_range.range == GE_LE
			|| bts->key_range.range == INF_LE))
		   || (BTREE_IS_LAST_KEY_DESC (&(bts->btid_int))
		       && (bts->key_range.range == GE_LT
			   || bts->key_range.range == GE_LE
			   || bts->key_range.range == GE_INF)))
	    {
	      int c;
	      BTREE_KEYRANGE *range;
	      BTID_INT *btid_int;

	      range = &bts->key_range;
	      btid_int = &bts->btid_int;
	      c =
		(*(bts->btid_int.key_type->type->cmpval)) (range->upper_key,
							   &prev_key,
							   btid_int->key_type,
							   btid_int->reverse,
							   0, 1, NULL);

	      /* EQUALITY test only - doesn't care the reverse index */
	      if (c != 0)
		{
		  bts->key_range_max_value_equal = false;
		}
	    }
	}

      if (which_action == BTREE_CONTINUE)
	{
	  /*
	   * The variable 'rec_oid_ptr' must be re-computed.
	   * The variable 'rec_oid_ptr' is pointing to some position
	   * within page image that is fixed in the buffer space.
	   * That is, PEEK method is used in getting an index record.
	   * By using PEEK method, the index page image might
	   * be moved to another buffer space even if it has not been
	   * changed during the unconditional instance locking.
	   * Therefore, rec_oid_ptr must be re-computed.
	   * If COPY method is used in getting an index record,
	   * 'rec_oid_ptr' cannot be re-computed.
	   *
	   * The variable 'rec_oid_cnt' is still correct
	   * if the index page image has not been changed.
	   *
	   * 3 cases are possible.
	   * 1) bts->P_page != NULL && bts->C_page == NULL
	   *    : the end of index scan (no OID copy)
	   * 2) bts->P_page != NULL && bts->C_page != NULL
	   *    : rec_oid_ptr must be re-computed
	   *    : bts->slot_id = 1
	   * 3) bts->P_page == NULL && bts->C_page != NULL
	   *    : rec_oid_ptr must be re-computed
	   *    : bts->slot_id = (??)
	   */
	  if (bts->O_page != NULL)
	    {
	      if (spage_get_record (bts->O_page, 1, &rec, PEEK) != S_SUCCESS)
		{
		  goto error;
		}

	      rec_oid_ptr = rec.data + ((bts->oid_pos + i) * oid_size);
	    }
	  else if (bts->C_page != NULL)
	    {
	      if (spage_get_record (bts->C_page, bts->slot_id, &rec, PEEK)
		  != S_SUCCESS)
		{
		  goto error;
		}

	      (void) btree_read_record (thread_p, &bts->btid_int, &rec, NULL,
					&Leaf_Pnt, true, &dummy_clear,
					&offset, 0);

	      rec_oid_ptr = (rec.data + offset
			     + ((bts->oid_pos + i) * oid_size));
	    }

	  if (is_condition_satisfied && CLS_satisfied)
	    {
	      if (need_count_only == false)
		{
		  /* normal scan - copy OID */
		  OR_GET_OID (rec_oid_ptr + inst_oid_offset, mem_oid_ptr);
		  mem_oid_ptr++;
		}

	      rec_oid_ptr += oid_size;
	    }
	  else
	    {
	      rec_oid_ptr += oid_size;
	      unsatisfied_cnt++;
	    }

	  continue;
	}

      /*
       * other values of which_action :
       * BTREE_GETOID_AGAIN_WITH_CHECK
       * BTREE_SEARCH_AGAIN_WITH_CHECK
       */
      /* update counts */
      if (i > 0)
	{
	  bts->oid_pos += i;
	  oids_cnt += (i - unsatisfied_cnt);
	}

      /*
       * The current key value had been saved in bts->cur_key.
       * The current class_oid and inst_oid will be saved
       * in saved_class_oid and saved_inst_oid, respectively.
       */
      COPY_OID (&saved_class_oid, &class_oid);
      COPY_OID (&saved_inst_oid, &inst_oid);

      if (which_action == BTREE_GETOID_AGAIN_WITH_CHECK)
	{
	  goto get_oidcnt_and_oidptr;
	}
      else
	{
	  goto search_again;
	}
    }

  if (i == cp_oid_cnt)
    {
      if (is_condition_satisfied == false)
	{
	  if (bts->tran_isolation == TRAN_SERIALIZABLE)
	    {
	      bts->oid_pos += i;
	    }

	  goto locking_done;
	}

      /* update counts */
      bts->oid_pos += i;
      oids_cnt += (i - unsatisfied_cnt);
    }

#else /* SERVER_MODE */
  if (is_condition_satisfied == false)
    {
      goto locking_done;
    }

  if (need_count_only == true)
    {
      /* do not concern buffer size */
      cp_oid_cnt = rec_oid_cnt - bts->oid_pos;
    }
  else
    {
      cp_oid_cnt = MIN (pg_oid_cnt - oids_cnt, rec_oid_cnt - bts->oid_pos);
    }

  if (!BTREE_IS_UNIQUE (&(bts->btid_int)) || num_classes == 0)
    {
      /*
       * 1. current index is a non-unique index. or
       * 2. current index is an unique index. &&
       *    current query is based on all classes
       *    contained in the class hierarchy.
       */
      for (i = 0; i < cp_oid_cnt; i++)
	{
	  if (need_count_only == false)
	    {
	      /* normal scan - copy OID */
	      OR_GET_OID (rec_oid_ptr + inst_oid_offset, mem_oid_ptr);
	      mem_oid_ptr++;
	    }

	  rec_oid_ptr += oid_size;
	}

      /* update counts */
      bts->oid_pos += cp_oid_cnt;
      oids_cnt += cp_oid_cnt;
    }
  else
    {
      /*
       * current index is an unique index. &&
       * current query is based on some classes
       * contained in the class hierarchy.
       */
      if (cp_oid_cnt > 1)
	{
	  er_log_debug (ARG_FILE_LINE,
			"cp_oid_cnt > 1 in an unique index\n"
			"index inconsistency..(unique violation)\n");
	}

      unsatisfied_cnt = 0;
      for (i = 0; i < cp_oid_cnt; i++)
	{
	  /* The class oid comparison must be performed. */
	  OR_GET_OID (rec_oid_ptr, &class_oid);
	  for (j = 0; j < num_classes; j++)
	    {
	      if (OID_EQ (&class_oid, &class_oids_ptr[j]))
		{
		  break;
		}
	    }

	  if (j < num_classes)
	    {			/* satisfying OID */
	      if (need_count_only == false)
		{
		  /* normal scan - copy OID */
		  OR_GET_OID (rec_oid_ptr + OR_OID_SIZE, mem_oid_ptr);
		  mem_oid_ptr++;
		}

	      rec_oid_ptr += oid_size;
	    }
	  else
	    {			/* unsatisfying OID */
	      rec_oid_ptr += oid_size;
	      unsatisfied_cnt++;
	    }
	}

      /* update counts */
      bts->oid_pos += cp_oid_cnt;
      oids_cnt += (cp_oid_cnt - unsatisfied_cnt);
    }
#endif /* SERVER_MODE */

locking_done:

  if (!bts->read_uncommitted)
    {
      /* if key range condition is not satisfied */
      if (is_key_range_satisfied == false)
	{
	  goto end_of_scan;
	}

      /* if key filter condtion is not satisfied */
      if (is_key_filter_satisfied == false
	  && bts->tran_isolation != TRAN_SERIALIZABLE)
	{
#if defined(SERVER_MODE)
	  /* clear 'prev_oid_pos' and 'prev_ovfl_vpid' */
	  bts->prev_oid_pos = 0;
	  bts->prev_ovfl_vpid.pageid = NULL_PAGEID;
#endif /* SERVER_MODE */

	  if (btree_find_next_index_record (thread_p, bts) != NO_ERROR)
	    {
	      goto error;
	    }

	  goto get_oidcnt_and_oidptr;
	}
      /* SERIALIZABLE : go downward */
    }

  if (need_count_only == false && oids_cnt == pg_oid_cnt)
    {
      /* We have no more room. */

#if defined(SERVER_MODE)
      LSA_COPY (&bts->cur_leaf_lsa, pgbuf_get_lsa (bts->C_page));
      btree_clear_key_value (&clear_prev_key, &prev_key);
#endif /* SERVER_MODE */

      /* do not clear bts->cur_key for btree_prepare_next_search */

      goto resume_next_search;
    }

  /* oids_cnt < pg_oid_cnt : We have more room in the oidset space. */
  if (need_count_only == false && bts->oid_pos < rec_oid_cnt)
    {
      /*
       * All OIDs in the index entry are locked and copied.
       * But, when the space for keeping the OIDs is insufficient,
       * a part of the OIDs is locked and copied.
       * Therefore, the truth that 'bts->oid_pos' is smaller
       * than 'rec_oid_cnt' means following things:
       * Some page update has occurred during the unconditional
       * instance locking and the currently locked OID has been changed.
       */
      goto start_locking;
    }
  else
    {
#if defined(SERVER_MODE)
      bts->prev_oid_pos = rec_oid_cnt - 1;
      bts->prev_ovfl_vpid = bts->O_vpid;
#endif /* SERVER_MODE */

      /* bts->oid_pos >= rec_oid_cnt */
      /* Leaf_Pnt is still having valid values. */
      if (btree_get_next_oidset_pos (thread_p, bts, &Leaf_Pnt.ovfl) !=
	  NO_ERROR)
	{
	  goto error;
	}

      goto get_oidcnt_and_oidptr;
    }

error:

  oids_cnt = -1;

  /*
   * we need to make sure that
   * BTREE_END_OF_SCAN() return true in the error cases.
   */

  /* fall through */

end_of_scan:

#if defined(SERVER_MODE)
  btree_clear_key_value (&clear_prev_key, &prev_key);
#endif /* SERVER_MODE */

  /* clear all the used keys */
  btree_scan_clear_key (bts);

  /* set the end of scan */
  bts->C_vpid.pageid = NULL_PAGEID;
  bts->O_vpid.pageid = NULL_PAGEID;

resume_next_search:

  /* unfix all the index pages */
  if (bts->P_page != NULL)
    {
      pgbuf_unfix (thread_p, bts->P_page);
      bts->P_page = NULL;
    }

  if (bts->C_page != NULL)
    {
      pgbuf_unfix (thread_p, bts->C_page);
      bts->C_page = NULL;
    }

  if (bts->O_page != NULL)
    {
      pgbuf_unfix (thread_p, bts->O_page);
      bts->O_page = NULL;
    }

  return oids_cnt;
}

/*
 * btree_find_min_or_max_key () -
 *   return: NO_ERROR
 *   btid(in):
 *   key(in):
 *   find_min_key(in):
 */
int
btree_find_min_or_max_key (THREAD_ENTRY * thread_p, BTID * btid,
			   DB_VALUE * key, int find_min_key)
{
  VPID vpid, root_vpid;
  PAGE_PTR page = NULL;
  PAGE_PTR root_page_ptr = NULL;
  INT16 slot_id;
  int key_cnt, offset;
  bool clear_key;
  DB_VALUE temp_key;
  BTREE_ROOT_HEADER root_header;
  RECDES rec;
  LEAF_REC Leaf_Pnt;
  BTID_INT btid_int;
  char *header_ptr;
  int ret = NO_ERROR;

  if (key == NULL)
    {
      return NO_ERROR;
    }

  root_vpid.pageid = btid->root_pageid;
  root_vpid.volid = btid->vfid.volid;

  root_page_ptr = pgbuf_fix (thread_p, &root_vpid, OLD_PAGE, PGBUF_LATCH_READ,
			     PGBUF_UNCONDITIONAL_LATCH);
  if (root_page_ptr == NULL)
    {
      goto exit_on_error;
    }

  if (spage_get_record (root_page_ptr, HEADER, &rec, PEEK) != S_SUCCESS)
    {
      goto exit_on_error;
    }

  btree_read_root_header (&rec, &root_header);

  pgbuf_unfix (thread_p, root_page_ptr);
  root_page_ptr = NULL;

  btid_int.sys_btid = btid;
  ret = btree_glean_root_header_info (&root_header, &btid_int);
  if (ret != NO_ERROR)
    {
      goto exit_on_error;
    }

  db_make_null (key);

  /*
   * in case of reverse index,
   * we have to find the min/max key in opposite order.
   */
  if (btid_int.reverse)
    {
      find_min_key = !find_min_key;
    }

  if (find_min_key)
    {
      page = btree_find_first_leaf (thread_p, btid, &vpid);
      if (page == NULL)
	{
	  goto exit_on_error;
	}

      /* first index record */
      slot_id = 1;
    }
  else
    {
      page = btree_find_last_leaf (thread_p, btid, &vpid);
      if (page == NULL)
	{
	  goto exit_on_error;
	}

      /* last index record */
      slot_id = spage_number_of_records (page) - 1;
    }

  /* get header information (key_cnt) */
  btree_get_header_ptr (page, &header_ptr);
  key_cnt = BTREE_GET_NODE_KEY_CNT (header_ptr);

  if (slot_id <= key_cnt)
    {
      if (spage_get_record (page, slot_id, &rec, PEEK) != S_SUCCESS)
	{
	  goto exit_on_error;
	}

      btree_read_record (thread_p, &btid_int, &rec, &temp_key, &Leaf_Pnt,
			 true, &clear_key, &offset, 0);

      if (DB_IS_NULL (&temp_key))
	{
	  goto exit_on_error;
	}

      (void) pr_clone_value (&temp_key, key);

      if (clear_key)
	{
	  pr_clear_value (&temp_key);
	}
    }

  pgbuf_unfix (thread_p, page);
  page = NULL;

  return ret;

exit_on_error:

  if (page)
    {
      pgbuf_unfix (thread_p, page);
      page = NULL;
    }

  if (root_page_ptr)
    {
      pgbuf_unfix (thread_p, root_page_ptr);
      root_page_ptr = NULL;
    }

  if (ret == NO_ERROR)
    {
      ret = er_errid ();
      if (ret == NO_ERROR)
	{
	  ret = ER_FAILED;
	}
    }

  return ret;
}

/*
 * Recovery functions
 */

/*
 * btree_rv_util_save_page_records () - Save a set of page records
 *   return: int
 *   page_ptr(in): Page Pointer
 *   first_slotid(in): First Slot identifier to be saved
 *   rec_cnt(in): Number of slots to be saved
 *   ins_slotid(in): First Slot identifier to reinsert set of records
 *   data(in): Data area where the records will be stored
 *             (Enough space(DB_PAGESIZE) must have been allocated by caller
 *   length(in): Effective length of the data area after save is completed
 *
 * Note: Copy the set of records to designated data area.
 *
 * Note: This is a UTILITY routine, but not an actual recovery routine
 */
int
btree_rv_util_save_page_records (PAGE_PTR page_ptr, INT16 first_slotid,
				 int rec_cnt, INT16 ins_slotid,
				 char *data, int *length)
{
  RECDES rec;
  int i, offset, wasted;
  char *datap;

  *length = 0;
  datap = (char *) data + sizeof (RECSET_HEADER);
  offset = sizeof (RECSET_HEADER);

  DB_WASTED_ALIGN (offset, MAX_ALIGNMENT, wasted);
  datap += wasted;
  offset += wasted;

  for (i = 0; i < rec_cnt; i++)
    {
      if (spage_get_record (page_ptr, first_slotid + i, &rec, PEEK) !=
	  S_SUCCESS)
	{
	  return er_errid ();
	}

      *(INT16 *) datap = rec.length;
      datap += 2;
      offset += 2;

      *(INT16 *) datap = rec.type;
      datap += 2;
      offset += 2;

      memcpy (datap, rec.data, rec.length);
      datap += rec.length;
      offset += rec.length;

      DB_WASTED_ALIGN (offset, MAX_ALIGNMENT, wasted);
      datap += wasted;
      offset += wasted;
    }

  datap = data;
  ((RECSET_HEADER *) datap)->rec_cnt = rec_cnt;
  ((RECSET_HEADER *) datap)->first_slotid = ins_slotid;
  *length = offset;

  return NO_ERROR;
}

/*
 * btree_rv_save_keyval () - Save a < key, value > pair for logical log purposes
 *   return: NO_ERROR
 *   btid(in): B+tree index identifier
 *   key(in): Key to be saved
 *   cls_oid(in):
 *   oid(in): Associated OID, i.e. value
 *   data(in): Data area where the above fields will be stored
 *             (Note: The caller should FREE the allocated area.)
 *   length(in): Length of the data area after save is completed
 *
 * Note: Copy the adequate key-value information to the data area and
 * return this data area.
 *
 * Note: This is a UTILITY routine, but not an actual recovery routine
 *
 * Warning: This routine assumes that the keyval is from a leaf page and
 * not a nonleaf page.  Because of this assumption, we use the
 * index domain and not the nonleaf domain to write out the key
 * value.  Currently all calls to this routine are from leaf
 * pages.  Be careful if you add a call to this routine.
 */
static int
btree_rv_save_keyval (BTID_INT * btid, DB_VALUE * key, OID * cls_oid,
		      OID * oid, char **data, int *length)
{
  char *datap;
  int key_len;
  OR_BUF buf;
  PR_TYPE *pr_type;
  int ret = NO_ERROR;

  *length = 0;

  key_len = (int) btree_get_key_length (key);
  *data = (char *) db_private_alloc (NULL,
				     OR_BTID_SIZE + (2 * OR_OID_SIZE) +
				     key_len + MAX_ALIGNMENT + MAX_ALIGNMENT);
  if (*data == NULL)
    {
      goto exit_on_error;
    }
  datap = (char *) (*data);
  datap = or_pack_btid (datap, btid->sys_btid);
  datap = PTR_ALIGN (datap, OR_INT_SIZE);
  if (BTREE_IS_UNIQUE (btid))
    {
      OR_PUT_OID (datap, cls_oid);
      datap += OR_OID_SIZE;
    }
  OR_PUT_OID (datap, oid);
  datap += OR_OID_SIZE;
  datap = PTR_ALIGN (datap, OR_INT_SIZE);

  or_init (&buf, datap, key_len);
  pr_type = btid->key_type->type;

  if ((*(pr_type->writeval)) (&buf, key) != NO_ERROR)
    {
      db_private_free_and_init (NULL, *data);
      *data = NULL;
      goto exit_on_error;
    }
  datap += key_len;

  *length = datap - *data;

end:

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
 * btree_rv_save_root_head () - Save root head stats FOR LOGICAL LOG PURPOSES
 *   return:
 *   max_key_len(in):
 *   null_delta(in):
 *   oid_delta(in):
 *   key_delta(in):
 *   recdes(in):
 *
 * Note: Copy the root header statistics to the data area provided.
 *
 * Note: This is a UTILITY routine, but not an actual recovery routine.
 */
static void
btree_rv_save_root_head (int max_key_len, int null_delta,
			 int oid_delta, int key_delta, RECDES * recdes)
{
  char *datap;

  recdes->length = 0;
  datap = (char *) recdes->data;
  OR_PUT_INT (datap, max_key_len);
  datap += OR_INT_SIZE;
  OR_PUT_INT (datap, null_delta);
  datap += OR_INT_SIZE;
  OR_PUT_INT (datap, oid_delta);
  datap += OR_INT_SIZE;
  OR_PUT_INT (datap, key_delta);
  datap += OR_INT_SIZE;

  recdes->length = datap - recdes->data;

}

/*
 * btree_rv_util_dump_leafrec () -
 *   return: nothing
 *   btid(in):
 *   Rec(in): Leaf Record
 *
 * Note: Dump a Tree Leaf Node Record
 *
 * Note: This is a UTILITY routine, but not an actual recovery routine
 */
void
btree_rv_util_dump_leafrec (THREAD_ENTRY * thread_p, BTID_INT * btid,
			    RECDES * Rec)
{
  btree_dump_leaf_record (thread_p, btid, Rec, 2);
}

/*
 * btree_rv_util_dump_nleafrec () -
 *   return: nothing
 *   btid(in):
 *   Rec(in): NonLeaf Record
 *
 * Note: Dump a Tree NonLeaf Node Record
 *
 * Note: This is a UTILITY routine, but not an actual recovery routine
 */
void
btree_rv_util_dump_nleafrec (THREAD_ENTRY * thread_p, BTID_INT * btid,
			     RECDES * Rec)
{
  btree_dump_non_leaf_record (thread_p, btid, Rec, 2, 1);
}

/*
 * btree_rv_roothdr_undo_update () -
 *   return: int
 *   recv(in): Recovery structure
 *
 * Note: Recover the root header statistics for undo purposes.
 */
int
btree_rv_roothdr_undo_update (THREAD_ENTRY * thread_p, LOG_RCV * recv)
{
  char *header_ptr;
  INT32 num_nulls;
  INT32 num_oids;
  INT32 num_keys;
  char *datap;


  if (recv->length < 4 * OR_INT_SIZE)
    {
      goto error;
    }

  btree_get_header_ptr (recv->pgptr, &header_ptr);

  num_nulls = BTREE_GET_NUM_NULLS (header_ptr);
  num_oids = BTREE_GET_NUM_OIDS (header_ptr);
  num_keys = BTREE_GET_NUM_KEYS (header_ptr);

  /* unpack the root statistics */
  datap = (char *) recv->data;
  BTREE_PUT_NODE_MAX_KEY_LEN (header_ptr, OR_GET_INT (datap));
  datap += OR_INT_SIZE;
  BTREE_PUT_NUM_NULLS (header_ptr, num_nulls + OR_GET_INT (datap));
  datap += OR_INT_SIZE;
  BTREE_PUT_NUM_OIDS (header_ptr, num_oids + OR_GET_INT (datap));
  datap += OR_INT_SIZE;
  BTREE_PUT_NUM_KEYS (header_ptr, num_keys + OR_GET_INT (datap));

  return NO_ERROR;

error:

  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);

  return ER_GENERIC_ERROR;
}

/*
 * btree_rv_roothdr_dump () -
 *   return:
 *   length(in):
 *   data(in):
 *
 * Note: Dump the root header statistics recovery information.
 */
void
btree_rv_roothdr_dump (int length, void *data)
{
  char *datap;
  int max_key_len, null_delta, oid_delta, key_delta;

  /* unpack the root statistics */
  datap = (char *) data;
  max_key_len = OR_GET_INT (datap);
  datap += OR_INT_SIZE;
  null_delta = OR_GET_INT (datap);
  datap += OR_INT_SIZE;
  oid_delta = OR_GET_INT (datap);
  datap += OR_INT_SIZE;
  key_delta = OR_GET_INT (datap);
  datap += OR_INT_SIZE;

  fprintf (stdout,
	   "\nMAX_KEY_LEN: %d NUM NULLS DELTA: %d NUM OIDS DELTA: %4d NUM KEYS DELTA: %d\n\n",
	   max_key_len, null_delta, oid_delta, key_delta);
}

/*
 * btree_rv_ovfid_undoredo_update () -
 *   return: int
 *   recv(in): Recovery structure
 *
 * Note: Recover the overflow VFID in the root header
 */
int
btree_rv_ovfid_undoredo_update (THREAD_ENTRY * thread_p, LOG_RCV * recv)
{
  char *header_ptr;
  VFID ovfid;

  if (recv->length < (int) sizeof (VFID))
    {
      goto error;
    }

  btree_get_header_ptr (recv->pgptr, &header_ptr);

  ovfid = *((VFID *) recv->data);	/* structure copy */
  BTREE_PUT_OVFID (header_ptr, &ovfid);

  return NO_ERROR;

error:

  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);

  return ER_GENERIC_ERROR;
}

/*
 * btree_rv_ovfid_dump () -
 *   return:
 *   length(in):
 *   data(in):
 *
 * Note: Dump the overflow VFID for the root header.
 */
void
btree_rv_ovfid_dump (int length, void *data)
{
  VFID ovfid;

  ovfid = *((VFID *) data);	/* structure copy */

  fprintf (stdout,
	   "\nOverflow key file VFID: %d|%d\n\n", ovfid.fileid, ovfid.volid);
}

/*
 * btree_rv_nodehdr_undoredo_update () - Recover an update to a node header. used either for
 *                         undo or redo
 *   return: int
 *   recv(in): Recovery structure
 *
 * Note: Recover the update to a node header
 */
int
btree_rv_nodehdr_undoredo_update (THREAD_ENTRY * thread_p, LOG_RCV * recv)
{
  RECDES Rec;
  int sp_success;

  Rec.area_size = Rec.length = recv->length;
  Rec.type = REC_HOME;
  Rec.data = (char *) recv->data;
  sp_success = spage_update (thread_p, recv->pgptr, HEADER, &Rec);
  if (sp_success != SP_SUCCESS)
    {
      if (sp_success != SP_ERROR)
	{
	  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR,
		  0);
	}
      return er_errid ();
    }
  pgbuf_set_dirty (thread_p, recv->pgptr, DONT_FREE);

  return NO_ERROR;
}

/*
 * btree_rv_nodehdr_redo_insert () - Recover a node header insertion. used for redo
 *   return: int
 *   recv(in): Recovery structure
 *
 * Note: Recover a node header insertion by reinserting the node header
 * for redo purposes.
 */
int
btree_rv_nodehdr_redo_insert (THREAD_ENTRY * thread_p, LOG_RCV * recv)
{
  RECDES Rec;
  int sp_success;

  Rec.area_size = Rec.length = recv->length;
  Rec.type = REC_HOME;
  Rec.data = (char *) recv->data;
  sp_success = spage_insert_at (thread_p, recv->pgptr, HEADER, &Rec);
  if (sp_success != SP_SUCCESS)
    {
      if (sp_success != SP_ERROR)
	{
	  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR,
		  0);
	}
      return er_errid ();
    }
  pgbuf_set_dirty (thread_p, recv->pgptr, DONT_FREE);

  return NO_ERROR;
}

/*
 * btree_rv_nodehdr_undo_insert () - Recover a node header insertion. used for undo
 *   return: int
 *   recv(in): Recovery structure
 *
 * Note: Recover a node header insertion by deletion  the node header
 * for undo purposes.
 */
int
btree_rv_nodehdr_undo_insert (THREAD_ENTRY * thread_p, LOG_RCV * recv)
{
  (void) spage_delete (thread_p, recv->pgptr, HEADER);
  pgbuf_set_dirty (thread_p, recv->pgptr, DONT_FREE);
  return NO_ERROR;
}

/*
 * btree_rv_nodehdr_dump () - Dump node header recovery information
 *   return: int
 *   length(in): Length of Recovery Data
 *   data(in): The data being logged
 *
 * Note: Dump node header recovery information
 */
void
btree_rv_nodehdr_dump (int length, void *data)
{
  char *header_ptr;
  VPID next_vpid;

  header_ptr = (char *) data;
  BTREE_GET_NODE_NEXT_VPID (header_ptr, &next_vpid);

  fprintf (stdout,
	   "\nNODE_TYPE: %s KEY_CNT: %4d MAX_KEY_LEN: %4d "
	   "NEXT_PAGEID: {%4d , %4d} \n\n",
	   (BTREE_GET_NODE_TYPE (header_ptr) ==
	    LEAF_NODE) ? "LEAF " : "NON_LEAF ",
	   BTREE_GET_NODE_KEY_CNT (header_ptr),
	   BTREE_GET_NODE_MAX_KEY_LEN (header_ptr), next_vpid.volid,
	   next_vpid.pageid);
}

/*
 * btree_rv_noderec_undoredo_update () - Recover an update to a node record. used either
 *                         for undo or redo
 *   return:
 *   return: int
 *   recv(in): Recovery structure
 *
 * Note: Recover the update to a node record
 */
int
btree_rv_noderec_undoredo_update (THREAD_ENTRY * thread_p, LOG_RCV * recv)
{
  RECDES Rec;
  INT16 slotid;
  int sp_success;

  slotid = recv->offset;
  Rec.type = *(INT16 *) ((char *) recv->data + OFFS2);
  Rec.area_size = Rec.length = recv->length - OFFS3;
  Rec.data = (char *) (recv->data) + OFFS3;

  sp_success = spage_update (thread_p, recv->pgptr, slotid, &Rec);
  if (sp_success != SP_SUCCESS)
    {
      if (sp_success != SP_ERROR)
	{
	  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR,
		  0);
	}
      return er_errid ();
    }

  pgbuf_set_dirty (thread_p, recv->pgptr, DONT_FREE);

  return NO_ERROR;
}

/*
 * btree_rv_noderec_redo_insert () - Recover a node record insertion. used for redo
 *   return: int
 *   recv(in): Recovery structure
 *
 * Note: Recover a node record insertion by reinserting the record for
 * redo purposes
 */
int
btree_rv_noderec_redo_insert (THREAD_ENTRY * thread_p, LOG_RCV * recv)
{
  RECDES Rec;
  INT16 slotid;
  int sp_success;

  slotid = recv->offset;
  Rec.type = *(INT16 *) ((char *) recv->data + OFFS2);
  Rec.area_size = Rec.length = recv->length - OFFS3;
  Rec.data = (char *) (recv->data) + OFFS3;

  sp_success = spage_insert_at (thread_p, recv->pgptr, slotid, &Rec);
  if (sp_success != SP_SUCCESS)
    {
      if (sp_success != SP_ERROR)
	{
	  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR,
		  0);
	}
      return er_errid ();
    }
  pgbuf_set_dirty (thread_p, recv->pgptr, DONT_FREE);

  return NO_ERROR;
}

/*
 * btree_rv_noderec_undo_insert () - Recover a node record insertion. used for undo
 *   return: int
 *   recv(in): Recovery structure
 *
 * Note: Recover a node record insertion by deleting the record for
 * undo purposes
 */
int
btree_rv_noderec_undo_insert (THREAD_ENTRY * thread_p, LOG_RCV * recv)
{
  INT16 slotid;

  slotid = recv->offset;
  (void) spage_delete_for_recovery (thread_p, recv->pgptr, slotid);
  pgbuf_set_dirty (thread_p, recv->pgptr, DONT_FREE);

  return NO_ERROR;
}

/*
 * btree_rv_noderec_dump () - Dump node record recovery information
 *   return: int
 *   length(in): Length of Recovery Data
 *   data(in): The data being logged
 *
 * Note: Dump node record recovery information
 */
void
btree_rv_noderec_dump (int length, void *data)
{
#if 0
  /* This needs to be fixed.  The easiest way is for the btid to be packed and
   * sent, but this increases the log record.  We may want to allow this
   * routine to know the layout of a node record.  TODO: ???
   */

  int Node_Type;
  RECDES Rec;

  Node_Type = *(INT16 *) ((char *) data + OFFS1);
  Rec.type = *(INT16 *) ((char *) data + OFFS2);
  Rec.area_size = DB_PAGESIZE;
  Rec.data = (char *) malloc (DB_PAGESIZE);
  memcpy (Rec.data, (char *) data + OFFS3, Rec.length);

  if (Node_Type == 0)
    {
      btree_rv_util_dump_leafrec (btid, &Rec);
    }
  else
    {
      btree_rv_util_dump_nleafrec (btid, &Rec);
    }

  free_and_init (Rec.data);
#endif

}

/*
 * btree_rv_noderec_dump_slot_id () -
 *   return: int
 *   length(in): Length of Recovery Data
 *   data(in): The data being logged
 *
 * Note: Dump the slot id for the slot to be deleted for undo purposes
 */

void
btree_rv_noderec_dump_slot_id (int length, void *data)
{

  fprintf (stdout, " Slot_id: %d \n", *(INT16 *) data);

}

/*
 * btree_rv_pagerec_insert () -
 *   return: int
 *   recv(in): Recovery structure
 *
 * Note: Put a set of records to the page
 */
int
btree_rv_pagerec_insert (THREAD_ENTRY * thread_p, LOG_RCV * recv)
{
  RECDES Rec;
  RECSET_HEADER *recset_header;
  char *datap;
  int i, offset, wasted;
  int sp_success;

  /* initialization */
  recset_header = (RECSET_HEADER *) recv->data;

  /* insert back saved records */
  datap = (char *) recv->data + sizeof (RECSET_HEADER);
  offset = sizeof (RECSET_HEADER);
  DB_WASTED_ALIGN (offset, MAX_ALIGNMENT, wasted);
  datap += wasted;
  offset += wasted;
  for (i = 0; i < recset_header->rec_cnt; i++)
    {
      Rec.area_size = Rec.length = *(INT16 *) datap;
      datap += 2;
      offset += 2;
      Rec.type = *(INT16 *) datap;
      datap += 2;
      offset += 2;
      Rec.data = datap;
      datap += Rec.length;
      offset += Rec.length;
      DB_WASTED_ALIGN (offset, MAX_ALIGNMENT, wasted);
      datap += wasted;
      offset += wasted;
      sp_success = spage_insert_at (thread_p, recv->pgptr,
				    recset_header->first_slotid + i, &Rec);
      if (sp_success != SP_SUCCESS)
	{
	  if (sp_success != SP_ERROR)
	    {
	      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_GENERIC_ERROR, 0);
	    }
	  goto error;
	}			/* if */
    }				/* for */

  pgbuf_set_dirty (thread_p, recv->pgptr, DONT_FREE);

  return NO_ERROR;

error:

  return er_errid ();

}

/*
 * btree_rv_pagerec_delete () -
 *   return: int
 *   recv(in): Recovery structure
 *
 * Note: Delete a set of records from the page for undo or redo purpose
 */
int
btree_rv_pagerec_delete (THREAD_ENTRY * thread_p, LOG_RCV * recv)
{
  RECSET_HEADER *recset_header;
  int i;

  /* initialization */
  recset_header = (RECSET_HEADER *) recv->data;

  /* delete all specified records from the page */
  for (i = 0; i < recset_header->rec_cnt; i++)
    {
      if (spage_delete (thread_p, recv->pgptr, recset_header->first_slotid)
	  != recset_header->first_slotid)
	{
	  return er_errid ();
	}			/* if */
    }				/* for */

  pgbuf_set_dirty (thread_p, recv->pgptr, DONT_FREE);

  return NO_ERROR;
}

/*
 * btree_rv_redo_truncate_oid () -
 *   return: int
 *   recv(in):
 *
 * Note: Truncates the last OID off of a node record.
 */
int
btree_rv_redo_truncate_oid (THREAD_ENTRY * thread_p, LOG_RCV * recv)
{
  RECDES copy_rec;
  int oid_size;

  /* initialization */
  oid_size = *(int *) recv->data;

  copy_rec.area_size = DB_PAGESIZE;
  copy_rec.data = (char *) malloc (DB_PAGESIZE);
  if (copy_rec.data == NULL)
    {
      goto error;
    }

  if ((spage_get_record (recv->pgptr, recv->offset, &copy_rec, COPY)
       != S_SUCCESS))
    {
      goto error;
    }

  /* truncate the last OID */
  copy_rec.length -= oid_size;

  /* write it out */
  if (spage_update (thread_p, recv->pgptr, recv->offset, &copy_rec) !=
      SP_SUCCESS)
    {
      goto error;
    }

  free_and_init (copy_rec.data);
  pgbuf_set_dirty (thread_p, recv->pgptr, DONT_FREE);

  return NO_ERROR;

error:

  if (copy_rec.data)
    {
      free_and_init (copy_rec.data);
    }
  return er_errid ();

}

/*
 * btree_rv_newpage_redo_init () -
 *   return: int
 *   recv(in): Recovery structure
 *
 * Note: Initialize a B+tree page.
 */
int
btree_rv_newpage_redo_init (THREAD_ENTRY * thread_p, LOG_RCV * recv)
{
  /* Initialize page */
  spage_initialize (thread_p, recv->pgptr, UNANCHORED_KEEP_SEQUENCE,
		    INT_ALIGNMENT, DONT_SAFEGUARD_RVSPACE);

  return NO_ERROR;
}

/*
 * btree_rv_newpage_undo_alloc () -
 *   return: int
 *   recv(in): Recovery structure
 *
 * Note: Undo a new page allocation
 */
int
btree_rv_newpage_undo_alloc (THREAD_ENTRY * thread_p, LOG_RCV * recv)
{
  PAGEID_STRUCT *pageid_struct;
  int ret = NO_ERROR;

  pageid_struct = (PAGEID_STRUCT *) recv->data;

  ret =
    file_dealloc_page (thread_p, &pageid_struct->vfid, &pageid_struct->vpid);

  return NO_ERROR;
}

/*
 * btree_rv_newpage_dump_undo_alloc () -
 *   return: int
 *   length(in): Length of Recovery Data
 *   data(in): The data being logged
 *
 * Note: Dump undo information of new page creation
 */
void
btree_rv_newpage_dump_undo_alloc (int length, void *data)
{
  PAGEID_STRUCT *pageid_struct = (PAGEID_STRUCT *) data;

  fprintf (stdout,
	   "Deallocating page from Volid = %d, Fileid = %d\n",
	   pageid_struct->vfid.volid, pageid_struct->vfid.fileid);
}

/*
 * btree_rv_read_keyval_info_nocopy () -
 *   return:
 *   datap(in):
 *   data_size(in):
 *   btid(in):
 *   cls_oid(in):
 *   oid(in):
 *   key(in):
 *
 * Note: read the keyval info from a recovery record.
 *
 * Warning: This assumes that the keyvalue has the index's domain and
 * not the nonleaf domain.  This should be the case since this
 * is a logical operation and not a physical one.
 */
static void
btree_rv_read_keyval_info_nocopy (THREAD_ENTRY * thread_p, char *datap,
				  int data_size, BTID_INT * btid,
				  OID * cls_oid, OID * oid, DB_VALUE * key)
{
  OR_BUF buf;
  PR_TYPE *pr_type;
  VPID Root_vpid;
  PAGE_PTR Root = NULL;
  RECDES Rec;
  BTREE_ROOT_HEADER root_header;
  int key_size = -1;
  char *start = datap;

  /* extract the stored btid, key, oid data */
  datap = or_unpack_btid (datap, btid->sys_btid);
  datap = PTR_ALIGN (datap, OR_INT_SIZE);

  Root_vpid.pageid = btid->sys_btid->root_pageid;	/* read root page */
  Root_vpid.volid = btid->sys_btid->vfid.volid;
  Root = pgbuf_fix (thread_p, &Root_vpid, OLD_PAGE, PGBUF_LATCH_READ,
		    PGBUF_UNCONDITIONAL_LATCH);
  if (Root == NULL
      || (spage_get_record (Root, HEADER, &Rec, PEEK) != S_SUCCESS))
    {
      goto error;
    }

  btree_read_root_header (&Rec, &root_header);
  if (btree_glean_root_header_info (&root_header, btid) != NO_ERROR)
    {
      goto error;
    }

  pgbuf_unfix (thread_p, Root);
  Root = NULL;

  if (BTREE_IS_UNIQUE (btid))
    {				/* only in case of an unique index */
      OR_GET_OID (datap, cls_oid);
      datap += OR_OID_SIZE;
    }
  else
    {
      OID_SET_NULL (cls_oid);
    }
  OR_GET_OID (datap, oid);
  datap += OR_OID_SIZE;
  datap = PTR_ALIGN (datap, OR_INT_SIZE);

  or_init (&buf, datap, data_size - (datap - start));
  pr_type = btid->key_type->type;

  /* Do not copy the string--just use the pointer.  The pr_ routines
   * for strings and sets have different semantics for length.
   */
  if (pr_type->id == DB_TYPE_MIDXKEY)
    key_size = buf.endptr - buf.ptr;

  (*(pr_type->readval)) (&buf, key, btid->key_type, key_size,
			 false /* not copy */ ,
			 NULL, 0);

  return;

error:

  if (Root)
    {
      pgbuf_unfix (thread_p, Root);
      Root = NULL;
    }

}

/*
 * btree_rv_keyval_undo_insert () -
 *   return: int
 *   recv(in): Recovery structure
 *
 * Note: Undo the insertion of a <key, val> pair to the B+tree,
 * by deleting the <key, val> pair from the tree.
 */
int
btree_rv_keyval_undo_insert (THREAD_ENTRY * thread_p, LOG_RCV * recv)
{
  BTID_INT btid;
  BTID sys_btid;
  DB_VALUE key;
  OID cls_oid;
  OID oid;
  char *datap;
  int dummy;
  int datasize;

  /* btid needs a place to unpack the sys_btid into.  We'll use stack space. */
  btid.sys_btid = &sys_btid;

  /* extract the stored btid, key, oid data */
  datap = (char *) recv->data;
  datasize = recv->length;
  btree_rv_read_keyval_info_nocopy (thread_p, datap, datasize, &btid,
				    &cls_oid, &oid, &key);

  if (btree_delete (thread_p, btid.sys_btid, &key, &cls_oid, &oid, &dummy,
		    SINGLE_ROW_MODIFY, (BTREE_UNIQUE_STATS *) NULL) == NULL)
    {
      return er_errid ();
    }				/* if */

  return NO_ERROR;
}

/*
 * btree_rv_keyval_undo_delete () -
 *   return: int
 *   recv(in): Recovery structure
 *
 * Note: undo the deletion of a <key, val> pair to the B+tree,
 * by inserting the <key, val> pair to the tree.
 */
int
btree_rv_keyval_undo_delete (THREAD_ENTRY * thread_p, LOG_RCV * recv)
{
  BTID_INT btid;
  BTID sys_btid;
  DB_VALUE key;
  OID cls_oid;
  OID oid;
  char *datap;
  int datasize;

  /* btid needs a place to unpack the sys_btid into.  We'll use stack space. */
  btid.sys_btid = &sys_btid;

  /* extract the stored btid, key, oid data */
  datap = (char *) recv->data;
  datasize = recv->length;
  btree_rv_read_keyval_info_nocopy (thread_p, datap, datasize, &btid,
				    &cls_oid, &oid, &key);

  if (btree_insert (thread_p, btid.sys_btid, &key, &cls_oid, &oid,
		    SINGLE_ROW_MODIFY, (BTREE_UNIQUE_STATS *) NULL,
		    NULL) == NULL)
    {
      return er_errid ();
    }				/* if */

  return NO_ERROR;
}

/*
 * btree_rv_keyval_dump () -
 *   return: int
 *   length(in): Length of Recovery Data
 *   data(in): The data being logged
 *
 * Note: Dump undo information <key-value> insertion
 */
void
btree_rv_keyval_dump (int length, void *data)
{
  BTID_INT btid;
  BTID sys_btid;
  DB_VALUE key;
  OID cls_oid;
  OID oid;

  /* btid needs a place to unpack the sys_btid into.  We'll use stack space. */
  btid.sys_btid = &sys_btid;

  /* extract the stored btid, key, oid data */
  btree_rv_read_keyval_info_nocopy (NULL, (char *) data, length, &btid,
				    &cls_oid, &oid, &key);

  fprintf (stdout, " BTID = { { %d , %d }, %d, %s } \n ",
	   btid.sys_btid->vfid.volid, btid.sys_btid->vfid.fileid,
	   btid.sys_btid->root_pageid,
	   pr_type_name (btid.key_type->type->id));

  fprintf (stdout, " KEY = ");
  btree_dump_key (&key);
  fprintf (stdout, "\n");

  if (BTREE_IS_UNIQUE (&btid))
    {				/* unique index */
      fprintf (stdout, " Class OID = { %d, %d, %d }, ",
	       cls_oid.volid, cls_oid.pageid, cls_oid.slotid);
    }
  else
    {				/* non-unique index */
      fprintf (stdout, " Class OID = None, ");
    }

  fprintf (stdout, " OID = { %d, %d, %d } \n", oid.volid,
	   oid.pageid, oid.slotid);
}

/*
 * btree_rv_undoredo_copy_page () -
 *   return: int
 *   recv(in): Recovery structure
 *
 * Note: Copy a whole page back for undo or redo purposes
 */
int
btree_rv_undoredo_copy_page (THREAD_ENTRY * thread_p, LOG_RCV * recv)
{

  (void) memcpy (recv->pgptr, recv->data, DB_PAGESIZE);

  pgbuf_set_dirty (thread_p, recv->pgptr, DONT_FREE);
  return NO_ERROR;
}

/*
 * btree_rv_leafrec_redo_delete () -
 *   return: int
 *   recv(in): Recovery structure
 *
 * Note: Recover a leaf record deletion for redo purposes
 */
int
btree_rv_leafrec_redo_delete (THREAD_ENTRY * thread_p, LOG_RCV * recv)
{
  RECDES rec;
  INT16 slotid;
  int sp_success;

  slotid = recv->offset;
  rec.length = recv->length;
  rec.area_size = recv->length;
  rec.data = (char *) recv->data;

  /* redo the deletion of the btree slot */
  if (spage_delete (thread_p, recv->pgptr, slotid) != slotid)
    {
      goto error;
    }

  /* update the page header */
  sp_success = spage_update (thread_p, recv->pgptr, HEADER, &rec);
  if (sp_success != SP_SUCCESS)
    {
      if (sp_success != SP_ERROR)
	{
	  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR,
		  0);
	}
      goto error;
    }

  pgbuf_set_dirty (thread_p, recv->pgptr, DONT_FREE);

  return NO_ERROR;

error:

  return er_errid ();

}

/*
 * btree_rv_leafrec_redo_insert_key () -
 *   return: int
 *   recv(in): Recovery structure
 *
 * Note: Recover a leaf record key insertion for redo purposes
 */
int
btree_rv_leafrec_redo_insert_key (THREAD_ENTRY * thread_p, LOG_RCV * recv)
{
  char *header_ptr;
  RECDES Rec;
  INT16 key_cnt;
  INT16 slotid;
  int key_len;
  BTREE_NODE_HEADER Header;
  int sp_success;

  Rec.data = NULL;

  slotid = recv->offset;
  key_len = *(INT16 *) ((char *) recv->data + LOFFS1);
  Rec.type = *(INT16 *) ((char *) recv->data + LOFFS3);
  Rec.area_size = Rec.length = recv->length - LOFFS4;
  Rec.data = (char *) (recv->data) + LOFFS4;

  /* insert the new record */
  sp_success = spage_insert_at (thread_p, recv->pgptr, slotid, &Rec);
  if (sp_success != SP_SUCCESS)
    {
      if (sp_success != SP_ERROR)
	{
	  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR,
		  0);
	}
      goto error;
    }

  btree_get_header_ptr (recv->pgptr, &header_ptr);

  key_cnt = BTREE_GET_NODE_KEY_CNT (header_ptr);
  key_cnt++;
  BTREE_PUT_NODE_KEY_CNT (header_ptr, key_cnt);
  if (BTREE_GET_NODE_MAX_KEY_LEN (header_ptr) < key_len)
    {
      BTREE_PUT_NODE_MAX_KEY_LEN (header_ptr, key_len);
    }

  pgbuf_set_dirty (thread_p, recv->pgptr, DONT_FREE);

  return NO_ERROR;

error:

  return er_errid ();
}

/*
 * btree_rv_leafrec_undo_insert_key () -
 *   return: int
 *   recv(in): Recovery structure
 *
 * Note: Recover a leaf record key insertion for undo purposes
 */
int
btree_rv_leafrec_undo_insert_key (THREAD_ENTRY * thread_p, LOG_RCV * recv)
{
  char *header_ptr;
  INT16 key_cnt;
  INT16 slotid;

  slotid = recv->offset;

  /* delete the new record */
  (void) spage_delete_for_recovery (thread_p, recv->pgptr, slotid);

  btree_get_header_ptr (recv->pgptr, &header_ptr);

  key_cnt = BTREE_GET_NODE_KEY_CNT (header_ptr);
  key_cnt--;
  BTREE_PUT_NODE_KEY_CNT (header_ptr, key_cnt);

  pgbuf_set_dirty (thread_p, recv->pgptr, DONT_FREE);

  return NO_ERROR;
}

/*
 * btree_rv_leafrec_redo_insert_oid () -
 *   return: int
 *   recv(in): Recovery structure
 *
 * Note: Recover a leaf record oid insertion for redo purposes
 */
int
btree_rv_leafrec_redo_insert_oid (THREAD_ENTRY * thread_p, LOG_RCV * recv)
{
  RECDES Rec;
  INT16 slotid = recv->offset;
  RECINS_STRUCT *recins = (RECINS_STRUCT *) recv->data;
  int sp_success;
  LEAF_REC leaf_pnt;

  Rec.area_size = DB_PAGESIZE;
  Rec.data = (char *) malloc (DB_PAGESIZE);
  if (Rec.data == NULL)
    {
      goto error;
    }

  if (recins->rec_type == REGULAR)
    {
      /* read the record */
      if (spage_get_record (recv->pgptr, slotid, &Rec, COPY) != S_SUCCESS)
	{
	  goto error;
	}

      btree_read_fixed_portion_of_leaf_record (&Rec, &leaf_pnt);

      if (recins->oid_inserted == true)
	{
	  char *ptr = Rec.data + Rec.length;

	  if (!OID_ISNULL (&recins->class_oid))
	    {			/* unique index */
	      OR_PUT_OID (ptr, &recins->class_oid);
	      ptr += OR_OID_SIZE;
	      Rec.length += OR_OID_SIZE;
	    }
	  OR_PUT_OID (ptr, &recins->oid);
	  Rec.length += OR_OID_SIZE;
	}

      if (recins->ovfl_changed == true)
	{
	  leaf_pnt.ovfl = recins->ovfl_vpid;
	}

      btree_write_fixed_portion_of_leaf_record (&Rec, &leaf_pnt);
      sp_success = spage_update (thread_p, recv->pgptr, slotid, &Rec);
      if (sp_success != SP_SUCCESS)
	{
	  if (sp_success != SP_ERROR)
	    {
	      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_GENERIC_ERROR, 0);
	    }
	  goto error;
	}

    }
  else
    {
      if (recins->new_ovflpg == true)
	{
	  VPID next_vpid;

	  Rec.type = REC_HOME;

	  /* new page is the last overflow page, no following page */
	  Rec.length = 2 * OR_INT_SIZE + OR_SHORT_SIZE;
	  VPID_SET_NULL (&next_vpid);
	  btree_write_overflow_header (&Rec, &next_vpid);

	  if (spage_insert_at (thread_p, recv->pgptr, HEADER, &Rec) !=
	      SP_SUCCESS)
	    {
	      goto error;
	    }

	  /* insert the value in the new overflow page */
	  if (!OID_ISNULL (&recins->class_oid))
	    {			/* unique index */
	      Rec.length = 2 * OR_OID_SIZE;
	      OR_PUT_OID (Rec.data, &recins->class_oid);
	      OR_PUT_OID (Rec.data + OR_OID_SIZE, &recins->oid);
	    }
	  else
	    {
	      Rec.length = OR_OID_SIZE;
	      OR_PUT_OID (Rec.data, &recins->oid);
	    }

	  sp_success = spage_insert_at (thread_p, recv->pgptr, 1, &Rec);
	  if (sp_success != SP_SUCCESS)
	    {
	      if (sp_success != SP_ERROR)
		{
		  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE,
			  ER_GENERIC_ERROR, 0);
		}
	      goto error;
	    }

	}
      else
	{
	  if (recins->oid_inserted == true)
	    {
	      char *ptr;

	      /* read the record */
	      if (spage_get_record (recv->pgptr, slotid, &Rec, COPY) !=
		  S_SUCCESS)
		{
		  goto error;
		}

	      ptr = Rec.data + Rec.length;
	      if (!OID_ISNULL (&recins->class_oid))
		{		/* unique index */
		  OR_PUT_OID (ptr, &recins->class_oid);
		  ptr += OR_OID_SIZE;
		  Rec.length += OR_OID_SIZE;
		}
	      OR_PUT_OID (ptr, &recins->oid);
	      Rec.length += OR_OID_SIZE;

	      sp_success = spage_update (thread_p, recv->pgptr, slotid, &Rec);
	      if (sp_success != SP_SUCCESS)
		{
		  if (sp_success != SP_ERROR)
		    {
		      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE,
			      ER_GENERIC_ERROR, 0);
		    }
		  goto error;
		}
	    }			/* if */

	  if (recins->ovfl_changed == true)
	    {
	      RECDES peek_rec;

	      if (spage_get_record (recv->pgptr, HEADER, &peek_rec, PEEK) !=
		  S_SUCCESS)
		{
		  goto error;
		}

	      btree_write_overflow_header (&peek_rec, &recins->ovfl_vpid);
	    }
	}
    }

  pgbuf_set_dirty (thread_p, recv->pgptr, DONT_FREE);
  free_and_init (Rec.data);

  return NO_ERROR;

error:

  if (Rec.data)
    {
      free_and_init (Rec.data);
    }

  return er_errid ();
}

/*
 * btree_rv_leafrec_dump_insert_oid () -
 *   return: nothing
 *   length(in): Length of Recovery Data
 *   data(in): The data being logged
 *
 * Note: Dump recovery data of leaf record oid insertion
 */
void
btree_rv_leafrec_dump_insert_oid (int length, void *data)
{
  RECINS_STRUCT *recins = (RECINS_STRUCT *) data;

  fprintf (stdout, "LEAF RECORD OID INSERTION STRUCTURE: \n");
  fprintf (stdout, "Class OID: { %d, %d, %d }\n",
	   recins->class_oid.volid,
	   recins->class_oid.pageid, recins->class_oid.slotid);
  fprintf (stdout, "OID: { %d, %d, %d } \n",
	   recins->oid.volid, recins->oid.pageid, recins->oid.slotid);
  fprintf (stdout, "RECORD TYPE: %s \n",
	   (recins->rec_type == REGULAR) ? "REGULAR" : "OVERFLOW");
  fprintf (stdout, "Overflow Page Id: {%d , %d}\n",
	   recins->ovfl_vpid.volid, recins->ovfl_vpid.pageid);
  fprintf (stdout,
	   "Oid_Inserted: %d \n Ovfl_Changed: %d \n"
	   "New_Ovfl Page: %d \n", recins->oid_inserted,
	   recins->ovfl_changed, recins->new_ovflpg);
}

/*
 * btree_rv_nop () -
 *   return: int
 *   recv(in): Recovery structure
 *
 *
 * Note: Does nothing. This routine is used for to accompany some
 * compensating redo logs which are supposed to do nothing.
 */
int
btree_rv_nop (THREAD_ENTRY * thread_p, LOG_RCV * recv)
{
  return NO_ERROR;
}

/*
 * btree_multicol_key_is_null () -
 *   return: Return true if DB_VALUE is a NULL multi-column
 *           key and false otherwise.
 *   key(in): Pointer to multi-column key
 *
 * Note: Check the multi-column key for a NULL value. In terms of the B-tree,
 * a NULL multi-column key is a sequence in which each element is NULL.
 */
int
btree_multicol_key_is_null (DB_VALUE * key)
{
  int status = 0;
  DB_MIDXKEY *midxkey;
  unsigned int *bits;
  int nwords, i;

  if (DB_VALUE_TYPE (key) == DB_TYPE_MIDXKEY)
    {
      midxkey = DB_GET_MIDXKEY (key);

      if (midxkey && midxkey->ncolumns != -1)
	{			/* ncolumns == -1 means already constructing step */
	  bits = (unsigned int *) midxkey->buf;
	  nwords = OR_BOUND_BIT_WORDS (midxkey->ncolumns);
	  for (i = 0; i < nwords; i++)
	    {
	      if (bits[i] != 0)
		{
		  return 0;
		}
	    }

	  status = 1;
	}
    }

  return status;
}

/*
 * btree_multicol_key_has_null () -
 *   return: Return true if DB_VALUE is a multi-column key
 *           and has a NULL element in it and false otherwise.
 *   key(in): Pointer to multi-column key
 *
 * Note: Check the multi-column  key has a NULL element.
 */
int
btree_multicol_key_has_null (DB_VALUE * key)
{
  int status = 0;
  DB_MIDXKEY *midxkey;
  int i;

  if (DB_VALUE_TYPE (key) == DB_TYPE_MIDXKEY)
    {

      midxkey = DB_GET_MIDXKEY (key);

      if (midxkey && midxkey->ncolumns != -1)
	{			/* ncolumns == -1 means already constructing step */
	  for (i = 0; i < midxkey->ncolumns; i++)
	    {
	      if (OR_MULTI_ATT_IS_UNBOUND (midxkey->buf, i))
		{
		  return 1;
		}
	    }

	  return 0;
	}
    }

  return status;
}

/*
 * init_boundbits () -
 *   return:
 *   bufptr(in):
 *   n_atts(in):
 */
static int
init_boundbits (char *bufptr, int n_atts)
{
  unsigned int *bits;
  int i;
  int nwords;

  nwords = OR_BOUND_BIT_WORDS (n_atts);
  bits = (unsigned int *) bufptr;

  for (i = 0; i < nwords; i++)
    {
      bits[i] = 0;
    }

  return (nwords * 4);
}

#define OR_MULTI_BOUND_BIT_BYTES(count)  ((((count) + 31) >> 5) * 4)

#define OR_MULTI_BOUND_BIT_MASK(element) (1 << ((int)(element) & 7))

#define OR_MULTI_GET_BOUND_BIT_BYTE(bitptr, element)            \
        ((char *)(bitptr) + ((int)(element) >> 3))

#define OR_MULTI_GET_BOUND_BIT(bitptr, element)                 \
        ((*OR_MULTI_GET_BOUND_BIT_BYTE(bitptr, element)) &      \
        OR_MULTI_BOUND_BIT_MASK(element))

#define OR_MULTI_ATT_IS_UNBOUND(bitptr, element)                \
        (!OR_MULTI_GET_BOUND_BIT(bitptr, element))

#if 0				/* TODO: currently not used */
#if defined(SA_MODE)
/*
 * xbtree_get_keytype_revlevel () -
 *   return:
 *   btid(in):
 *   keytype(in):
 *   revlevel(in):
 */
int
xbtree_get_keytype_revlevel (BTID * btid, DB_TYPE * keytype, int *revlevel)
{
  PAGE_PTR P = NULL;
  VPID P_vpid;
  RECDES peek_rec;
  BTREE_ROOT_HEADER root_header;
  int ret = NO_ERROR;

  P_vpid.volid = btid->vfid.volid;	/* read the root page */
  P_vpid.pageid = btid->root_pageid;
  P = pgbuf_fix (thread_p, &P_vpid, OLD_PAGE, PGBUF_LATCH_READ,
		 PGBUF_UNCONDITIONAL_LATCH);
  if (P == NULL)
    {
      goto exit_on_error;
    }

  /* read the header record */
  if (spage_get_record (P, HEADER, &peek_rec, PEEK) != S_SUCCESS)
    {
      goto exit_on_error;
    }

  btree_read_root_header (&peek_rec, &root_header);
  pgbuf_unfix (thread_p, P);
  P = NULL;

  *keytype = root_header.key_type->type->id;
  *revlevel = root_header.rev_level;

end:

  return ret;

exit_on_error:

  if (P)
    {
      pgbuf_unfix (thread_p, P);
      P = NULL;
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
#endif /* SA_MODE */
#endif

/*
 * btree_find_oid_from_rec () -
 *   return:
 *   btid(in):
 *   ptr(in):
 *   oid_cnt(in):
 *   target(in):
 */
static bool
btree_find_oid_from_rec (BTID_INT * btid, char *ptr, int oid_cnt,
			 OID * target)
{
  int i, cls_oid_size;
  OID oid;

  cls_oid_size = (BTREE_IS_UNIQUE (btid)) ? OR_OID_SIZE : 0;

  for (i = 0; i < oid_cnt; i++)
    {
      ptr += cls_oid_size;
      OR_GET_OID (ptr, &oid);
      ptr += OR_OID_SIZE;

      if (OID_EQ (&oid, target))
	{
	  return true;
	}
    }

  return false;
}

/*
 * btree_find_key_from_leaf () -
 *   return:
 *   btid(in):
 *   pg_ptr(in):
 *   key_cnt(in):
 *   oid(in):
 *   key(in):
 *   clear_key(in):
 */
static DISK_ISVALID
btree_find_key_from_leaf (THREAD_ENTRY * thread_p, BTID_INT * btid,
			  PAGE_PTR pg_ptr, int key_cnt, OID * oid,
			  DB_VALUE * key, bool * clear_key)
{
  RECDES Rec;
  LEAF_REC Leaf_Pnt;
  char *ptr;
  VPID ovfl_vpid;
  char *header_ptr;
  int i, oid_cnt, oid_size, offset;

  if (BTREE_IS_UNIQUE (btid))
    {
      oid_size = 2 * OR_OID_SIZE;
    }
  else
    {
      oid_size = OR_OID_SIZE;
    }

  for (i = 1; i <= key_cnt; i++)
    {
      if (spage_get_record (pg_ptr, i, &Rec, PEEK) != S_SUCCESS)
	{
	  return DISK_ERROR;
	}

      btree_read_record (thread_p, btid, &Rec, key, &Leaf_Pnt, true,
			 clear_key, &offset, 0);
      ovfl_vpid = Leaf_Pnt.ovfl;

      ptr = Rec.data + offset;
      oid_cnt = CEIL_PTVDIV (Rec.length - offset, oid_size);

      if (btree_find_oid_from_rec (btid, ptr, oid_cnt, oid) == true)
	{
	  return DISK_VALID;
	}

      if (ovfl_vpid.pageid != NULL_PAGEID)
	{
	  /* record has an overflow page continuation */
	  RECDES oRec;
	  PAGE_PTR ovfp = NULL;

	  do
	    {
	      ovfp = pgbuf_fix (thread_p, &ovfl_vpid, OLD_PAGE,
				PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
	      /* TODO: check ovfp return value */

	      btree_get_header_ptr (ovfp, &header_ptr);
	      btree_get_next_overflow_vpid (header_ptr, &ovfl_vpid);

	      (void) spage_get_record (ovfp, 1, &oRec, PEEK);	/* peek */
	      oid_cnt = CEIL_PTVDIV (oRec.length, oid_size);
	      ptr = (char *) oRec.data;

	      if (btree_find_oid_from_rec (btid, ptr, oid_cnt, oid) == true)
		{
		  pgbuf_unfix (thread_p, ovfp);
		  ovfp = NULL;

		  return DISK_VALID;
		}

	      pgbuf_unfix (thread_p, ovfp);
	      ovfp = NULL;
	    }
	  while (ovfl_vpid.pageid != NULL_PAGEID);
	}

      if (*clear_key)
	{
	  pr_clear_value (key);
	}
    }

  return DISK_INVALID;
}

/*
 * btree_find_key_from_nleaf () -
 *   return:
 *   btid(in):
 *   pg_ptr(in):
 *   key_cnt(in):
 *   oid(in):
 *   key(in):
 *   clear_key(in):
 */
static DISK_ISVALID
btree_find_key_from_nleaf (THREAD_ENTRY * thread_p, BTID_INT * btid,
			   PAGE_PTR pg_ptr, int key_cnt, OID * oid,
			   DB_VALUE * key, bool * clear_key)
{
  int i;
  NON_LEAF_REC NLeaf_Ptr;
  VPID page_vpid;
  PAGE_PTR page = NULL;
  RECDES Rec;
  DISK_ISVALID status = DISK_INVALID;

  for (i = 1; i <= key_cnt; i++)
    {
      if (spage_get_record (pg_ptr, i, &Rec, PEEK) != S_SUCCESS)
	{
	  return DISK_ERROR;
	}

      btree_read_fixed_portion_of_non_leaf_record (&Rec, &NLeaf_Ptr);
      page_vpid = NLeaf_Ptr.pnt;

      page = pgbuf_fix (thread_p, &page_vpid, OLD_PAGE, PGBUF_LATCH_READ,
			PGBUF_UNCONDITIONAL_LATCH);
      if (page == NULL)
	{
	  status = DISK_ERROR;
	  break;
	}

      status =
	btree_find_key_from_page (thread_p, btid, page, oid, key, clear_key);
      pgbuf_unfix (thread_p, page);
      page = NULL;

      if (status == DISK_VALID)
	{
	  break;
	}
    }

  return status;
}

/*
 * btree_find_key_from_page () -
 *   return:
 *   btid(in):
 *   pg_ptr(in):
 *   oid(in):
 *   key(in):
 *   clear_key(in):
 */
static DISK_ISVALID
btree_find_key_from_page (THREAD_ENTRY * thread_p, BTID_INT * btid,
			  PAGE_PTR pg_ptr, OID * oid, DB_VALUE * key,
			  bool * clear_key)
{
  char *header_ptr;
  INT16 node_type;
  INT16 key_cnt;
  DISK_ISVALID status;

  btree_get_header_ptr (pg_ptr, &header_ptr);
  node_type = BTREE_GET_NODE_TYPE (header_ptr);
  key_cnt = BTREE_GET_NODE_KEY_CNT (header_ptr);

  if (node_type == NON_LEAF_NODE)
    {
      status =
	btree_find_key_from_nleaf (thread_p, btid, pg_ptr, key_cnt + 1,
				   oid, key, clear_key);
    }
  else
    {
      status = btree_find_key_from_leaf (thread_p, btid, pg_ptr, key_cnt,
					 oid, key, clear_key);
    }

  return status;
}

/*
 * btree_find_key () -
 *   return:
 *   btid(in):
 *   oid(in):
 *   key(in):
 *   clear_key(in):
 */
DISK_ISVALID
btree_find_key (THREAD_ENTRY * thread_p, BTID * btid, OID * oid,
		DB_VALUE * key, bool * clear_key)
{
  VPID Root_vpid;
  PAGE_PTR Root = NULL;
  BTID_INT btid_int;
  RECDES Rec;
  BTREE_ROOT_HEADER root_header;
  DISK_ISVALID status;

  Root_vpid.pageid = btid->root_pageid;	/* read root page */
  Root_vpid.volid = btid->vfid.volid;
  Root = pgbuf_fix (thread_p, &Root_vpid, OLD_PAGE, PGBUF_LATCH_READ,
		    PGBUF_UNCONDITIONAL_LATCH);
  if (Root == NULL)
    {
      return DISK_ERROR;
    }

  if (spage_get_record (Root, HEADER, &Rec, PEEK) != S_SUCCESS)
    {
      status = DISK_ERROR;
      goto end;
    }

  btree_read_root_header (&Rec, &root_header);

  btid_int.sys_btid = btid;
  btree_glean_root_header_info (&root_header, &btid_int);

  status =
    btree_find_key_from_page (thread_p, &btid_int, Root, oid, key, clear_key);

end:

  pgbuf_unfix (thread_p, Root);
  Root = NULL;

  return status;
}

char *
btree_get_header_ptr (PAGE_PTR page_ptr, char **header_ptrptr)
{
  SPAGE_SLOT *sptr;

  sptr = (SPAGE_SLOT *) (((char *) page_ptr) + DB_PAGESIZE -
			 sizeof (SPAGE_SLOT));
  *header_ptrptr = ((char *) page_ptr) + sptr->offset_to_record;

  return *header_ptrptr;
}

static VPID *
btree_get_next_overflow_vpid (char *header_ptr, VPID * overflow_vpid_ptr)
{
  char *ptr;

  ptr = ((char *) (header_ptr) + BTREE_NEXT_OVFL_VPID_OFFSET);

  overflow_vpid_ptr->pageid = OR_GET_INT (ptr);
  ptr += OR_INT_SIZE;

  overflow_vpid_ptr->volid = OR_GET_SHORT (ptr);

  return overflow_vpid_ptr;
}
