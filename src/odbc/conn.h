#ifndef	__ODBC_CONN_HEADER		/* to avoid multiple inclusion */
#define	__ODBC_CONN_HEADER

#include		"portable.h"
#include		"diag.h"
#include		"env.h"
#include		"stmt.h"

#define			KEYWORD_DSN			"DSN"
#define			KEYWORD_FILEDSN		"FILEDSN"
#define			KEYWORD_DBNAME		"DB_NAME"
#define			KEYWORD_USER		"UID"
#define			KEYWORD_PASSWORD	"PWD"
#define			KEYWORD_SERVER		"SERVER"
#define			KEYWORD_PORT		"PORT"
#define			KEYWORD_FETCH_SIZE	"FETCH_SIZE"
#define			KEYWORD_DESCRIPTION	"DESCRIPTION"
#define			KEYWORD_SAVEFILE	"SAVEFILE"
#define			KEYWORD_DRIVER		"DRIVER"

#define		TRAN_COMMIT_CLASS_UNCOMMIT_INSTANCE		1
#define		TRAN_COMMIT_CLASS_COMMIT_INSTANCE		2
#define		TRAN_REP_CLASS_UNCOMMIT_INSTANCE		3
#define		TRAN_REP_CLASS_COMMIT_INSTANCE			4
#define		TRAN_REP_CLASS_REP_INSTANCE				5

typedef struct stCUBRIDDSNItem
{
	char		driver[ITEMBUFLEN];
	char		dsn[ITEMBUFLEN];
	char		db_name[ITEMBUFLEN];
	char		user[ITEMBUFLEN];
	char		password[ITEMBUFLEN];
	char		server[ITEMBUFLEN];
	char		port[ITEMBUFLEN];
	char		fetch_size[ITEMBUFLEN];
	char		save_file[_MAX_PATH];
	char		description[2*ITEMBUFLEN];
} CUBRIDDSNItem;

typedef struct st_odbc_connection {
	unsigned short			handle_type;
	struct st_diag				*diag;
	int						connhd;
	struct st_odbc_env			*env;
	struct st_odbc_connection	*next;
	//void					*statements;
	//void					*descriptors;
	struct st_odbc_statement	*statements;
	struct st_odbc_desc		*descriptors;  /* external descriptor */
	
	char					*data_source;		/* data source name */
	unsigned char			*server;			/* odbc server address */
	long				 	port;				/* odbc server port number */
	char					*db_name;			/* CUBRID db name */			
	char					*user;				/* CUBRID db user */
	char					*password;			/* CUBRID db password */
	int						fetch_size;			/* fetch size */
	char					db_ver[16];
	
	unsigned long			old_txn_isolation;	/* for read-only mode */

	// Maximum length of the string data type from UniCAS
	long max_string_length;

	/* ODBC connection attributes */

	unsigned long 		attr_access_mode;	/* CORE */
	unsigned long		attr_autocommit;	/* LEVEL 1 */
		//unsigned long		attr_connection_dead;	/* LEVEL 1 */
		//	attr_connection_dead는 connhd로 부터 알아 낼 수 있다.
		// if connhd > 0, alive.

	void				*attr_quiet_mode;    /* CORE */
	unsigned long		attr_metadata_id;	/* CORE */
	unsigned long		attr_odbc_cursors;	/* CORE, DM */
	unsigned long		attr_trace;			/* CORE, DM*/
	char				*attr_tracefile;		/* CORE, DM */
	unsigned long		attr_txn_isolation;	/* LEVEL 1 */
	unsigned long		attr_async_enable;	/* LEVEL 1 */
	
	/* Not supported */
	unsigned long		attr_auto_ipd;		/* LEVEL2, RDONLY */
	unsigned long		attr_connection_timeout;	/* LEVEL 2 */
	char				*attr_current_catalog;	/* LEVEL 2*/
	unsigned long		attr_login_timeout;	/* LEVEL 2*/
	unsigned long		attr_packet_size;  /* LEVEL 2 */
	char				*attr_translate_lib;	/* CORE */
	unsigned long		attr_translate_option;	/* CORE */

	/* stmt attributes */
	unsigned long		attr_max_rows;			// 1
	unsigned long		attr_query_timeout;		// 2
} ODBC_CONNECTION;

PUBLIC RETCODE odbc_alloc_connection(ODBC_ENV *env, 
									 ODBC_CONNECTION **connptr);
PUBLIC RETCODE odbc_free_connection(ODBC_CONNECTION *conn);
PUBLIC RETCODE odbc_connect(ODBC_CONNECTION *conn,
							const char *data_source,
							const char *user,
							const char *password);
PUBLIC RETCODE odbc_connect_by_filedsn(ODBC_CONNECTION *conn,
						const char*	file_dsn,
						const char*	db_name,
						const char*	user,
						const char*	password,
						const char*	server,
						const char*	port);
PUBLIC RETCODE odbc_connect_new(ODBC_CONNECTION *conn,
						const char*	data_source,
						const char*	db_name,
						const char*	user,
						const char*	password,
						const char*	server,
						int			port,
						int			fetch_size);
PUBLIC RETCODE odbc_disconnect(ODBC_CONNECTION *conn);
PUBLIC RETCODE odbc_set_connect_attr(ODBC_CONNECTION *conn,
									 long attribute,
									 void	*valueptr,
									 long stringlength);
PUBLIC RETCODE odbc_get_connect_attr(ODBC_CONNECTION	*conn,
									 long			attribute,
									 void			*value_ptr,
									 long			buffer_length,
									 long			*string_len_ptr);
PUBLIC RETCODE odbc_auto_commit(ODBC_CONNECTION *conn);
PUBLIC RETCODE odbc_native_sql(ODBC_CONNECTION *conn,
								char	*in_stmt_text,
								char	*out_stmt_text,
								long	buffer_length,
								long	*out_stmt_length);
PUBLIC RETCODE odbc_get_functions(ODBC_CONNECTION *conn, 
								  unsigned short function_id,
								  unsigned short *supported_ptr);
PUBLIC RETCODE odbc_get_info(ODBC_CONNECTION *conn,
							 unsigned short	info_type,
							 void*		info_value_ptr,
							 short		buffer_length,
							 long		*string_length_ptr);
PUBLIC int get_dsn_info(const char* dsn,
						 char*	db_name, int db_name_len,
						 char*	user,	int user_len,
						 char*	pwd,	int pwd_len,
						 char*	server, int server_len,
						 int* port,
						 int* fetch_size);

#endif	/* ! __ODBC_CONN_HEADER */
