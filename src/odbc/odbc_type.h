#ifndef	__ODBC_TYPE_HEADER		/* to avoid multiple inclusion */
#define	__ODBC_TYPE_HEADER

#include		"portable.h"
#include		"sqlext.h"
#include		"cas_cci.h"

#define			MAX_CUBRID_CHAR_LEN			(1073741823)
#define			DEFAULT_COL_PRECISION		(256)

#define			SQL_UNI_MONETARY		30
#define			SQL_C_UNI_MONETARY		SQL_UNI_MONETARY
#define			SQL_UNI_OBJECT			31
#define			SQL_C_UNI_OBJECT		SQL_UNI_OBJECT
#define			SQL_UNI_SET				32
#define			SQL_C_UNI_SET			SQL_UNI_SET


#define		IS_STRING_TYPE(value)		( (value) == SQL_CHAR || (value) == SQL_VARCHAR || \
											(value) == SQL_LONGVARCHAR )
#define		IS_BINARY_TYPE(value)		((value) == SQL_BINARY || (value) == SQL_VARBINARY || \
											(value) == SQL_LONGVARBINARY )

typedef union tagUNI_CCI_A_TYPE {
	char*		str;
	int			i;
	float		f;
	double		d;
	T_CCI_BIT	bit;
	T_CCI_SET	set;
	T_CCI_DATE	date;
} UNI_CCI_A_TYPE;


typedef union tagUNI_C_TYPE_VALUE {
	char				c;
	short				s;
	long				l;
	float				f;
	double				d;
	char				*str;
	char				*bin;
	SQL_NUMERIC_STRUCT	num;
	SQL_DATE_STRUCT		date;
	SQL_TIME_STRUCT			time;
	SQL_TIMESTAMP_STRUCT	ts;
	void				*dummy;
} UNI_C_TYPE_VALUE;

typedef struct tagVALUE_CONTAINER {
	UNI_C_TYPE_VALUE	value;
	int					length;
	short				type;
} VALUE_CONTAINER;

PUBLIC int odbc_is_valid_type(short odbc_type);
PUBLIC int odbc_is_valid_code(short code );
PUBLIC int odbc_is_valid_date_verbose_type(short odbc_type);
PUBLIC int odbc_is_valid_interval_verbose_type(short odbc_type);
PUBLIC int odbc_is_valid_c_type(short odbc_type);
PUBLIC int odbc_is_valid_sql_type(short odbc_type);
PUBLIC int odbc_is_valid_concise_type(short odbc_type);
PUBLIC int odbc_is_valid_c_concise_type(short odbc_type);
PUBLIC int odbc_is_valid_sql_concise_type(short odbc_type);
PUBLIC int odbc_is_valid_verbose_type(short odbc_type);
PUBLIC int odbc_is_valid_c_verbose_type(short odbc_type);
PUBLIC int odbc_is_valid_sql_verbose_type(short odbc_type);
PUBLIC int odbc_is_valid_c_common_type(short c_type);
PUBLIC int odbc_is_valid_c_date_type(SQLSMALLINT c_type);
PUBLIC int odbc_is_valid_c_interval_type(short c_type);
PUBLIC int odbc_is_valid_sql_common_type(short sql_type);
PUBLIC int odbc_is_valid_sql_date_type(short sql_type);
PUBLIC int odbc_is_valid_sql_interval_type(short sql_type);
PUBLIC short odbc_concise_to_verbose_type(short	type);
PUBLIC short odbc_verbose_to_concise_type(short type,short	code);
PUBLIC short odbc_subcode_type(short type );
PUBLIC short odbc_default_c_type(short odbc_type);

PUBLIC int odbc_buffer_length(short odbc_type, int precision);
PUBLIC long odbc_size_of_by_type_id(short odbc_type);
PUBLIC int odbc_column_size(short odbc_type, int precision);
PUBLIC int odbc_decimal_digits(short odbc_type, int scale);
PUBLIC int odbc_octet_length(short odbc_type, int precision);
PUBLIC int odbc_num_prec_radix(short odbc_type);
PUBLIC int odbc_display_size(short odbc_type, int precision);
PUBLIC short odbc_type_by_cci(T_CCI_U_TYPE	cci_type, int precision);
PUBLIC const char*	odbc_type_name(short odbc_type);


PUBLIC T_CCI_U_TYPE odbc_type_to_cci_u_type(short sql_type);
PUBLIC T_CCI_A_TYPE odbc_type_to_cci_a_type(short c_type);
PUBLIC void *odbc_value_to_cci(void *c_value,short c_type, long c_length, 
							   short c_precision, short c_scale);
PUBLIC void odbc_value_to_cci2(void *sql_value_root, int index, void *c_value,short c_type, long c_length, 
							   short c_precision, short c_scale);
PUBLIC long cci_value_to_odbc(void  *c_value, short concise_type,
							  short precision, short scale, 
							  long  buffer_length, UNI_CCI_A_TYPE *cci_value, 
							  T_CCI_A_TYPE a_type);
PUBLIC VALUE_CONTAINER* create_value_container();
PUBLIC void clear_value_container(VALUE_CONTAINER *value);
PUBLIC void free_value_container(VALUE_CONTAINER *value);
PUBLIC RETCODE odbc_value_converter(VALUE_CONTAINER* target_value, 
									VALUE_CONTAINER* src_value);

PUBLIC short odbc_date_type_backward(short type );

#endif	/* ! __ODBC_TYPE_HEADER */
