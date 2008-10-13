/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * schema_info.c -
 */

#ident "$Id$"

#include <stdio.h>
#ifdef WIN32
#include <winsock2.h>
#include <windows.h>
#endif

#include "cas.h"
#include "schema_info.h"
#include "cas_common.h"
#include "net_buf.h"

void
schema_table_meta (T_NET_BUF * net_buf)
{
  net_buf_cp_int (net_buf, 2, NULL);
  net_buf_column_info_set (net_buf, CCI_U_TYPE_STRING, 0, SCH_STR_LEN,
			   "NAME");
  net_buf_column_info_set (net_buf, CCI_U_TYPE_SHORT, 0, 0, "TYPE");
}

void
schema_query_spec_meta (T_NET_BUF * net_buf)
{
  net_buf_cp_int (net_buf, 1, NULL);
  net_buf_column_info_set (net_buf, CCI_U_TYPE_STRING, 0, SCH_STR_LEN,
			   "QUERY_SPEC");
}

void
schema_attr_meta (T_NET_BUF * net_buf)
{
  net_buf_cp_int (net_buf, 13, NULL);
  net_buf_column_info_set (net_buf, CCI_U_TYPE_STRING, 0, SCH_STR_LEN,
			   "ATTR_NAME");
  net_buf_column_info_set (net_buf, CCI_U_TYPE_SHORT, 0, 0, "DOMAIN");
  net_buf_column_info_set (net_buf, CCI_U_TYPE_SHORT, 0, 0, "SCALE");
  net_buf_column_info_set (net_buf, CCI_U_TYPE_INT, 0, 0, "PRECISION");
  net_buf_column_info_set (net_buf, CCI_U_TYPE_SHORT, 0, 0, "INDEXED");
  net_buf_column_info_set (net_buf, CCI_U_TYPE_SHORT, 0, 0, "NON_NULL");
  net_buf_column_info_set (net_buf, CCI_U_TYPE_SHORT, 0, 0, "SHARED");
  net_buf_column_info_set (net_buf, CCI_U_TYPE_SHORT, 0, 0, "UNIQUE");
  net_buf_column_info_set (net_buf, CCI_U_TYPE_NULL, 0, 0, "DEFAULT");
  net_buf_column_info_set (net_buf, CCI_U_TYPE_INT, 0, 0, "ATTR_ORDER");
  net_buf_column_info_set (net_buf, CCI_U_TYPE_STRING, 0, SCH_STR_LEN,
			   "CLASS_NAME");
  net_buf_column_info_set (net_buf, CCI_U_TYPE_STRING, 0, SCH_STR_LEN,
			   "SOURCE_CLASS");
  net_buf_column_info_set (net_buf, CCI_U_TYPE_SHORT, 0, 0, "IS_KEY");
}

void
schema_method_meta (T_NET_BUF * net_buf)
{
  net_buf_cp_int (net_buf, 3, NULL);
  net_buf_column_info_set (net_buf, CCI_U_TYPE_STRING, 0, SCH_STR_LEN,
			   "NAME");
  net_buf_column_info_set (net_buf, CCI_U_TYPE_SHORT, 0, 0, "RET_DOMAIN");
  net_buf_column_info_set (net_buf, CCI_U_TYPE_STRING, 0, SCH_STR_LEN,
			   "ARG_DOMAIN");
}

void
schema_methodfile_meta (T_NET_BUF * net_buf)
{
  net_buf_cp_int (net_buf, 1, NULL);
  net_buf_column_info_set (net_buf, CCI_U_TYPE_STRING, 0, SCH_STR_LEN,
			   "METHOD_FILE");
}

void
schema_superclasss_meta (T_NET_BUF * net_buf)
{
  net_buf_cp_int (net_buf, 2, NULL);
  net_buf_column_info_set (net_buf, CCI_U_TYPE_STRING, 0, SCH_STR_LEN,
			   "CLASS_NAME");
  net_buf_column_info_set (net_buf, CCI_U_TYPE_SHORT, 0, 0, "TYPE");
}

void
schema_constraint_meta (T_NET_BUF * net_buf)
{
  net_buf_cp_int (net_buf, 8, NULL);
  net_buf_column_info_set (net_buf, CCI_U_TYPE_SHORT, 0, 0, "TYPE");
  net_buf_column_info_set (net_buf, CCI_U_TYPE_STRING, 0, SCH_STR_LEN,
			   "NAME");
  net_buf_column_info_set (net_buf, CCI_U_TYPE_STRING, 0, SCH_STR_LEN,
			   "ATTR_NAME");
  net_buf_column_info_set (net_buf, CCI_U_TYPE_INT, 0, 0, "NUM_PAGES");
  net_buf_column_info_set (net_buf, CCI_U_TYPE_INT, 0, 0, "NUM_KEYS");
  net_buf_column_info_set (net_buf, CCI_U_TYPE_SHORT, 0, 0, "PRIMARY_KEY");
  net_buf_column_info_set (net_buf, CCI_U_TYPE_SHORT, 0, 0, "KEY_ORDER");
  net_buf_column_info_set (net_buf, CCI_U_TYPE_STRING, 0, 0, "ASC_DESC");
}

void
schema_trigger_meta (T_NET_BUF * net_buf)
{
  net_buf_cp_int (net_buf, 10, NULL);
  net_buf_column_info_set (net_buf, CCI_U_TYPE_STRING, 0, SCH_STR_LEN,
			   "NAME");
  net_buf_column_info_set (net_buf, CCI_U_TYPE_STRING, 0, SCH_STR_LEN,
			   "STATUS");
  net_buf_column_info_set (net_buf, CCI_U_TYPE_STRING, 0, SCH_STR_LEN,
			   "EVENT");
  net_buf_column_info_set (net_buf, CCI_U_TYPE_STRING, 0, SCH_STR_LEN,
			   "TARGET_CLASS");
  net_buf_column_info_set (net_buf, CCI_U_TYPE_STRING, 0, SCH_STR_LEN,
			   "TARGET_ATTR");
  net_buf_column_info_set (net_buf, CCI_U_TYPE_STRING, 0, SCH_STR_LEN,
			   "ACTION_TIME");
  net_buf_column_info_set (net_buf, CCI_U_TYPE_STRING, 0, SCH_STR_LEN,
			   "ACTION");
  net_buf_column_info_set (net_buf, CCI_U_TYPE_FLOAT, 0, 0, "PRIORITY");
  net_buf_column_info_set (net_buf, CCI_U_TYPE_STRING, 0, SCH_STR_LEN,
			   "CONDITION_TIME");
  net_buf_column_info_set (net_buf, CCI_U_TYPE_STRING, 0, SCH_STR_LEN,
			   "CONDITION");
}

void
schema_classpriv_meta (T_NET_BUF * net_buf)
{
  net_buf_cp_int (net_buf, 3, NULL);
  net_buf_column_info_set (net_buf, CCI_U_TYPE_STRING, 0, SCH_STR_LEN,
			   "CLASS_NAME");
  net_buf_column_info_set (net_buf, CCI_U_TYPE_STRING, 0, 10, "PRIVILEGE");
  net_buf_column_info_set (net_buf, CCI_U_TYPE_STRING, 0, 5, "GRANTABLE");
}

void
schema_attrpriv_meta (T_NET_BUF * net_buf)
{
  net_buf_cp_int (net_buf, 3, NULL);
  net_buf_column_info_set (net_buf, CCI_U_TYPE_STRING, 0, SCH_STR_LEN,
			   "ATTR_NAME");
  net_buf_column_info_set (net_buf, CCI_U_TYPE_STRING, 0, 10, "PRIVILEGE");
  net_buf_column_info_set (net_buf, CCI_U_TYPE_STRING, 0, 5, "GRANTABLE");
}

void
schema_directsuper_meta (T_NET_BUF * net_buf)
{
  net_buf_cp_int (net_buf, 2, NULL);
  net_buf_column_info_set (net_buf, CCI_U_TYPE_STRING, 0, SCH_STR_LEN,
			   "CLASS_NAME");
  net_buf_column_info_set (net_buf, CCI_U_TYPE_STRING, 0, SCH_STR_LEN,
			   "SUPER_CLASS_NAME");
}

void
schema_primarykey_meta (T_NET_BUF * net_buf)
{
  net_buf_cp_int (net_buf, 4, NULL);
  net_buf_column_info_set (net_buf, CCI_U_TYPE_STRING, 0, SCH_STR_LEN,
			   "CLASS_NAME");
  net_buf_column_info_set (net_buf, CCI_U_TYPE_STRING, 0, SCH_STR_LEN,
			   "ATTR_NAME");
  net_buf_column_info_set (net_buf, CCI_U_TYPE_SHORT, 0, 0, "KEY_SEQ");
  net_buf_column_info_set (net_buf, CCI_U_TYPE_STRING, 0, SCH_STR_LEN,
			   "KEY_NAME");
}

