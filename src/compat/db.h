/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * db.h - Stubs for the SQLX interface layer.
 *
 */

#ifndef _DB_H_
#define _DB_H_

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include "error_manager.h"
#include "dbi.h"
#include "dbtype.h"
#include "dbdef.h"
#include "intl.h"
#include "db_date.h"
#include "db_query.h"
#include "object_representation.h"
#include "object_domain.h"
#if !defined(SERVER_MODE)
#include "authenticate.h"
#include "trigger_manager.h"
#endif
#include "logcp.h"
#include "parser.h"

#define db_locate_numeric(value) ((value)->data.num.d.buf)

/* MACROS FOR ERROR CHECKING */
/* These should be used at the start of every db_ function so we can check
   various validations before executing. */
#define CHECK_CONNECT_VOID()      \
  do {                            \
    if (db_Connect_status == 0) { \
      er_set(ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OBJ_NO_CONNECT, 0); \
      return;                     \
    }                             \
  } while (0)

#define CHECK_CONNECT_AND_RETURN_EXPR(return_expr_)                   \
  do {                                                                \
    if (db_Connect_status == 0)                                       \
    {                                                                 \
      er_set(ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OBJ_NO_CONNECT, 0); \
      return (return_expr_);                                          \
    }                                                                 \
  } while (0)

#define CHECK_CONNECT_ERROR()     \
  CHECK_CONNECT_AND_RETURN_EXPR((DB_TYPE) ER_OBJ_NO_CONNECT)

#define CHECK_CONNECT_NULL()      \
  CHECK_CONNECT_AND_RETURN_EXPR(NULL)

#define CHECK_CONNECT_ZERO()      \
  CHECK_CONNECT_AND_RETURN_EXPR(0)

#define CHECK_CONNECT_ZERO_TYPE(TYPE)      \
  CHECK_CONNECT_AND_RETURN_EXPR((TYPE)0)

#define CHECK_CONNECT_MINUSONE()  \
  CHECK_CONNECT_AND_RETURN_EXPR(-1)

#define CHECK_CONNECT_FALSE()     \
  CHECK_CONNECT_AND_RETURN_EXPR(false)

#define CHECK_MODIFICATION_VOID()   \
  do {                              \
    if (db_Disable_modifications) { \
      er_set(ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_DB_NO_MODIFICATIONS, 0); \
      return;                       \
    }                               \
  } while (0)

#define CHECK_MODIFICATION_AND_RETURN_EXPR(return_expr_) \
  if (db_Disable_modifications) {    \
    er_set(ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_DB_NO_MODIFICATIONS, 0); \
    return (return_expr_); \
  }

#define CHECK_MODIFICATION_ERROR()   \
  CHECK_MODIFICATION_AND_RETURN_EXPR(ER_DB_NO_MODIFICATIONS)

#define CHECK_MODIFICATION_NULL()   \
  CHECK_MODIFICATION_AND_RETURN_EXPR(NULL)

#define CHECK_MODIFICATION_MINUSONE() \
  CHECK_MODIFICATION_AND_RETURN_EXPR(-1)

/* Argument checking macros */
#define CHECK_1ARG_RETURN_EXPR(obj, expr)                                      \
  do {                                                                         \
    if((obj) == NULL) {                                                        \
      er_set(ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ARGUMENTS, 0); \
      return (expr);                                                           \
    }                                                                          \
  } while (0)

#define CHECK_2ARGS_RETURN_EXPR(obj1, obj2, expr)                              \
  do {                                                                         \
    if((obj1) == NULL || (obj2) == NULL) {                                     \
      er_set(ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ARGUMENTS, 0); \
      return (expr);                                                           \
    }                                                                          \
  } while (0)

#define CHECK_3ARGS_RETURN_EXPR(obj1, obj2, obj3, expr)                        \
  do {                                                                         \
    if((obj1) == NULL || (obj2) == NULL || (obj3) == NULL) {                   \
      er_set(ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ARGUMENTS, 0); \
      return (expr);                                                           \
    }                                                                          \
  } while (0)

#define CHECK_1ARG_NULL(obj)        \
  CHECK_1ARG_RETURN_EXPR(obj, NULL)

#define CHECK_2ARGS_NULL(obj1, obj2)    \
  CHECK_2ARGS_RETURN_EXPR(obj1,obj2,NULL)

#define CHECK_3ARGS_NULL(obj1, obj2, obj3) \
  CHECK_3ARGS_RETURN_EXPR(obj1,obj2,obj3,NULL)

#define CHECK_1ARG_FALSE(obj)  \
  CHECK_1ARG_RETURN_EXPR(obj,false)

#define CHECK_1ARG_TRUE(obj)   \
  CHECK_1ARG_RETURN_EXPR(obj, true)

#define CHECK_1ARG_ERROR(obj)  \
  CHECK_1ARG_RETURN_EXPR(obj,ER_OBJ_INVALID_ARGUMENTS)

#define CHECK_1ARG_ERROR_WITH_TYPE(obj, TYPE)  \
  CHECK_1ARG_RETURN_EXPR(obj,(TYPE)ER_OBJ_INVALID_ARGUMENTS)

#define CHECK_1ARG_MINUSONE(obj) \
  CHECK_1ARG_RETURN_EXPR(obj,-1)

#define CHECK_2ARGS_ERROR(obj1, obj2)   \
  CHECK_2ARGS_RETURN_EXPR(obj1, obj2, ER_OBJ_INVALID_ARGUMENTS)

#define CHECK_3ARGS_ERROR(obj1, obj2, obj3) \
  CHECK_3ARGS_RETURN_EXPR(obj1, obj2, obj3, ER_OBJ_INVALID_ARGUMENTS)

#define CHECK_1ARG_ZERO(obj)     \
  CHECK_1ARG_RETURN_EXPR(obj, 0)

#define CHECK_1ARG_ZERO_WITH_TYPE(obj1, RETURN_TYPE)     \
  CHECK_1ARG_RETURN_EXPR(obj1, (RETURN_TYPE) 0)

#define CHECK_2ARGS_ZERO(obj1, obj2)    \
  CHECK_2ARGS_RETURN_EXPR(obj1,obj2, 0)

#define CHECK_1ARG_UNKNOWN(obj1)        \
  CHECK_1ARG_RETURN_EXPR(obj1, DB_TYPE_UNKNOWN)

#define DB_MAKE_OID(value, oid)						\
  do {									\
    if ((db_value_domain_init(value, DB_TYPE_OID, 0, 0)) == NO_ERROR)	\
	(void)db_make_oid(value, oid);				        \
  } while (0)

#define DB_GET_OID(value)		(db_get_oid(value))


/* GLOBAL STATE */
extern int db_Connect_status;
extern int db_Disable_modifications;
extern bool db_Replication_agent_mode;

#if !defined(SERVER_MODE)
extern char db_Database_name[];
#endif
extern char db_Program_name[];

extern int db_init (const char *program, int print_version,
		    const char *dbname, const char *db_path,
		    const char *vol_path,
		    const char *log_path, const char *host_name,
		    const int overwrite, const char *comments,
		    int npages, const char *addmore_vols_file,
		    int desired_pagesize, int log_npages);

/* functions from db_vdb.c */
extern int db_check_single_query (DB_SESSION * session, int stmt_no);
extern int db_parse_one_statement (DB_SESSION * session);
#ifdef __cplusplus
extern "C"
{
#endif
  extern int parse_one_statement (int state);
#ifdef __cplusplus
}
#endif
extern int db_get_parser_line_col (DB_SESSION * session, int *line, int *col);
extern int db_get_line_col_of_1st_error (DB_SESSION * session,
					 DB_QUERY_ERROR * linecol);
extern DB_VALUE *db_get_hostvars (DB_SESSION * session);
extern char **db_get_lock_classes (DB_SESSION * session);
extern void db_drop_all_statements (DB_SESSION * session);
extern PARSER_CONTEXT *db_get_parser (DB_SESSION * session);
extern DB_NODE *db_get_statement (DB_SESSION * session, int id);
extern DB_SESSION *db_make_session_for_one_statement_execution (FILE * file);
/* in db_admin.c */
extern int db_abort_to_savepoint_internal (const char *savepoint_name);

/* used by the api tester */

extern int db_error_code_test (void);

extern const char *db_error_string_test (int level);

/* in db_macro.c */
extern int db_make_oid (DB_VALUE * value, const OID * oid);
extern OID *db_get_oid (const DB_VALUE * value);
extern int db_value_alter_type (DB_VALUE * value, DB_TYPE type);

#if !defined(_DBTYPE_H_)
extern int db_value_put_encoded_time (DB_VALUE * value, const DB_TIME * time);
extern int db_value_put_encoded_date (DB_VALUE * value, const DB_DATE * date);
#endif
extern void *db_value_eh_key (DB_VALUE * value);
extern int db_value_put_db_data (DB_VALUE * value, const DB_DATA * data);
extern DB_DATA *db_value_get_db_data (DB_VALUE * value);
extern int db_make_db_char (DB_VALUE * value, INTL_CODESET codeset,
			    const char *str, const int size);
extern INTL_CODESET db_get_string_codeset (const DB_VALUE * value);

/* in db_obj.c */
extern DB_OBJECT *db_create_internal (DB_OBJECT * obj);
extern DB_OBJECT *db_create_by_name_internal (const char *name);
extern int db_put_internal (DB_OBJECT * obj, const char *name,
			    DB_VALUE * value);
extern DB_OTMPL *dbt_create_object_internal (DB_OBJECT * classobj);
extern int dbt_put_internal (DB_OTMPL * def, const char *name,
			     DB_VALUE * value);
extern int db_dput_internal (DB_OBJECT * obj,
			     DB_ATTDESC * attribute, DB_VALUE * value);
extern int dbt_dput_internal (DB_OTMPL * def,
			      DB_ATTDESC * attribute, DB_VALUE * value);
extern DB_DOMAIN *db_attdesc_domain (DB_ATTDESC * desc);

/* in db_class.c */
extern int db_add_super_internal (DB_OBJECT * classobj, DB_OBJECT * super);
extern int db_add_attribute_internal (MOP class_, const char *name,
				      const char *domain,
				      DB_VALUE * default_value,
				      SM_NAME_SPACE name_space);
extern int db_rename_internal (DB_OBJECT * classobj,
			       const char *name,
			       int class_namespace, const char *newname);
extern int db_drop_attribute_internal (DB_OBJECT * classobj,
				       const char *name);
extern void db_set_sync_flag (DB_SESSION * session, QUERY_EXEC_MODE flag);
extern DB_SESSION *db_open_buffer_local (const char *buffer);
extern int db_compile_statement_local (DB_SESSION * session);
extern int db_execute_statement_local (DB_SESSION * session,
				       int stmt, DB_QUERY_RESULT ** result);
extern void db_close_session_local (DB_SESSION * session);
extern int db_savepoint_transaction_internal (const char *savepoint_name);
extern int db_drop_set_attribute_domain (MOP class_,
					 const char *name,
					 int class_attribute,
					 const char *domain);
/* db_info.c */
extern BTID *db_constraint_index (DB_CONSTRAINT * constraint, BTID * index);

/* db_set.c */
extern int db_col_optimize (DB_COLLECTION * col);
#endif /* _DB_H_ */
