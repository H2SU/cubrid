#ifndef	__ODBC_CATALOG_HEADER		/* to avoid multiple inclusion */
#define	__ODBC_CATALOG_HEADER

typedef struct tagODBC_COL_INFO {
	const char		*name;
	short			type;
	int				length;
	short			precision;
	short			scale;
} ODBC_COL_INFO;

typedef struct tagODBC_TABLE_VALUE {
	char*			table_name;
	char*			table_type;
} ODBC_TABLE_VALUE;

typedef struct tagODBC_COLUMN_VALUE {
	char*			table_name;
	char*			column_name;
	short			concise_data_type;
	char*			type_name;
	int				column_size;
	int				buffer_length;
	short			decimal_digits;
	short			num_prec_radix;
	short			nullable;
	char*			default_value;
	short			verbose_data_type;
	short			subcode;
	int				octet_length;
	int				ordinal_position;
} ODBC_COLUMN_VALUE;

typedef struct tagODBC_STAT_VALUE {
    char        *table_name;
    short       type;           /* SQL_TABLE_STAT, SQL_INDEX_BTREE */
    short       non_unique;
    char        *index_name;
    char        *column_name;
    int         ordinal_position;
} ODBC_STAT_VALUE;

typedef struct tagODBC_SPECIAL_COLUMN_VALUE {
    char		*column_name;
    short       concise_data_type;
    char		*type_name;
    int         column_size;
    int         buffer_length;
    short       decimal_digits;
} ODBC_SP_COLUMN_VALUE;

typedef struct tagODBC_TYPE_INFO_VALUE
{
        char			*type_name;
        short           data_type;
        int             column_size;
        char			*literal_prefix;
        char			*literal_suffix;
        char			*create_params;
        short           nullable;
        short           case_sensitive;
        short           searchable;
        short           unsigned_attribute;
        short           fixed_prec_scale;
        short           auto_unique_value;
        char			*local_type_name;
        short           minimum_scale;
        short           maximum_scale;
        short           sql_data_type;
        short           sql_datetime_sub;
        short           num_prec_radix;
        short           interval_precision;
} ODBC_TYPE_INFO_VALUE;

PUBLIC RETCODE odbc_tables(ODBC_STATEMENT *stmt,
							char	*catalog_name,
							char	*schema_name,
							char	*table_name,
							char	*table_type);
PUBLIC ODBC_TABLE_VALUE* create_table_value(void);
PUBLIC void free_table_value(ODBC_TABLE_VALUE* value);
PUBLIC RETCODE odbc_columns(ODBC_STATEMENT *stmt,
							char	*catalog_name,
							char	*schema_name,
							char	*table_name,
							char	*column_name);
PUBLIC void free_column_value(ODBC_COLUMN_VALUE* value);
PUBLIC ODBC_COLUMN_VALUE* create_column_value(void);
PUBLIC RETCODE odbc_statistics(ODBC_STATEMENT *stmt,
							char	*catalog_name,
							char	*schema_name,
							char	*table_name,
							unsigned short	unique,
							unsigned short	reversed);
PUBLIC ODBC_STAT_VALUE* create_stat_value(void);
PUBLIC void free_stat_value(ODBC_STAT_VALUE* value);

PUBLIC RETCODE odbc_special_columns(ODBC_STATEMENT	*stmt,
							short		identifier_type,
							char		*catalog_name,
							char		*schema_name,
							char		*table_name,
							short		scope,
							short		nullable);
PUBLIC ODBC_SP_COLUMN_VALUE* create_sp_column_value(void);
PUBLIC void free_sp_column_value(ODBC_SP_COLUMN_VALUE* value);
PUBLIC RETCODE odbc_get_type_info(ODBC_STATEMENT *stmt,
								  short			 data_type);
PUBLIC ODBC_TYPE_INFO_VALUE* create_type_info_value(void);
PUBLIC void free_type_info_value(ODBC_TYPE_INFO_VALUE* value);
PUBLIC RETCODE odbc_get_catalog_data(ODBC_STATEMENT *stmt, 
									 short			col_index, 
									 VALUE_CONTAINER *c_value);
PUBLIC void free_catalog_result(ST_LIST *result, RESULT_TYPE type);

#endif	/* ! __ODBC_CATALOG_HEADER */
