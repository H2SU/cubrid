/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * isqlx.c : SQLX invocation program
 * TODO: rename this file to csql_launcher.c (?)
 */

#ident "$Id$"

#include <stdio.h>
#include <stdarg.h>
#ifdef HAVE_GETOPT_H
#include <getopt.h>
#else
#include "getopt.h"
#endif

#include "sqlx.h"
#include "message_catalog.h"
#include "environment_variable.h"
#include "intl.h"
#include "utility.h"
#include "dbu_misc.h"

typedef const char *(*CSQL_GET_MESSAGE) (int message_index);
typedef int (*SQLX) (const char *argv0, CSQL_ARGUMENT * csql_arg);

static void utility_csql_usage (void);
static void utility_csql_version (void);

/*
 * utility_csql_usage() - display csql usage
 */
static void
utility_csql_usage (void)
{
  DSO_HANDLE util_sa_library;
  CSQL_GET_MESSAGE csql_get_message;
  const char *message;

  utility_load_library (&util_sa_library, LIB_UTIL_SA_NAME);
  if (util_sa_library == NULL)
    {
      utility_load_print_error (stderr);
      return;
    }
  utility_load_symbol (util_sa_library, (DSO_HANDLE *) (&csql_get_message),
		       "csql_get_message");
  if (csql_get_message == NULL)
    {
      utility_load_print_error (stderr);
      return;
    }
  message = (*csql_get_message) (CSQL_MSG_USAGE);
  fprintf (stderr, message, VERSION, UTIL_CSQL_NAME);
}

/*
 * utility_csql_version - display a version of this utility
 *
 * return:
 *
 * NOTE:
 */
static void
utility_csql_print (int message_num, ...)
{
  typedef const char *(*GET_MESSAGE) (int message_index);

  DSO_HANDLE util_sa_library;
  DSO_HANDLE symbol;
  GET_MESSAGE get_message_fn;

  utility_load_library (&util_sa_library, LIB_UTIL_SA_NAME);
  if (util_sa_library == NULL)
    {
      utility_load_print_error (stderr);
      return;
    }
  utility_load_symbol (util_sa_library, &symbol,
		       UTILITY_GENERIC_MSG_FUNC_NAME);
  if (symbol == NULL)
    {
      utility_load_print_error (stderr);
      return;
    }

  get_message_fn = symbol;

  {
    va_list ap;

    va_start (ap, message_num);
    vfprintf (stderr, get_message_fn (message_num), ap);
    va_end (ap);
  }
}

/*
 * main() - sqlx main module. invoke usqlx with argument
 *   return: no return if no error,
 *           EXIT_FAILURE otherwise.
 */
int
main (int argc, char *argv[])
{
  char option_string[64];
  CSQL_ARGUMENT csql_arg;
  DSO_HANDLE util_library;
  SQLX sqlx;
  GETOPT_LONG csql_option[] = {
    {CSQL_SA_MODE_L, 0, 0, CSQL_SA_MODE_S},
    {CSQL_CS_MODE_L, 0, 0, CSQL_CS_MODE_S},
    {CSQL_USER_L, 1, 0, CSQL_USER_S},
    {CSQL_PASSWORD_L, 1, 0, CSQL_PASSWORD_S},
    {CSQL_ERROR_CONTINUE_L, 0, 0, CSQL_ERROR_CONTINUE_S},
    {CSQL_INPUT_FILE_L, 1, 0, CSQL_INPUT_FILE_S},
    {CSQL_OUTPUT_FILE_L, 1, 0, CSQL_OUTPUT_FILE_S},
    {CSQL_SINGLE_LINE_L, 0, 0, CSQL_SINGLE_LINE_S},
    {CSQL_COMMAND_L, 1, 0, CSQL_COMMAND_S},
    {CSQL_LINE_OUTPUT_L, 0, 0, CSQL_LINE_OUTPUT_S},
    {CSQL_NO_AUTO_COMMIT_L, 0, 0, CSQL_NO_AUTO_COMMIT_S},
    {CSQL_NO_PAGER_L, 0, 0, CSQL_NO_PAGER_S},
    {VERSION_L, 0, 0, VERSION_S},
    {0, 0, 0, 0}
  };

  memset (&csql_arg, 0, sizeof (CSQL_ARGUMENT));
  csql_arg.auto_commit = true;
  utility_make_getopt_optstring (csql_option, option_string);

  while (1)
    {
      int option_index = 0;
      int option_key;

      option_key = getopt_long (argc, argv, option_string,
				csql_option, &option_index);
      if (option_key == -1)
	{
	  break;
	}

      switch (option_key)
	{
	case CSQL_SA_MODE_S:
	  csql_arg.sa_mode = true;
	  break;
	case CSQL_CS_MODE_S:
	  csql_arg.cs_mode = true;
	  break;
	case CSQL_USER_S:
	  csql_arg.user_name = strdup (optarg);
	  break;
	case CSQL_PASSWORD_S:
	  csql_arg.passwd = strdup (optarg);
	  break;
	case CSQL_ERROR_CONTINUE_S:
	  csql_arg.continue_on_error = true;
	  break;
	case CSQL_INPUT_FILE_S:
	  csql_arg.in_file_name = strdup (optarg);
	  break;
	case CSQL_OUTPUT_FILE_S:
	  csql_arg.out_file_name = strdup (optarg);
	  break;
	case CSQL_SINGLE_LINE_S:
	  csql_arg.single_line_execution = true;
	  break;
	case CSQL_COMMAND_S:
	  csql_arg.command = strdup (optarg);
	  break;
	case CSQL_LINE_OUTPUT_S:
	  csql_arg.line_output = true;
	  break;
	case CSQL_NO_AUTO_COMMIT_S:
	  csql_arg.auto_commit = false;
	  break;
	case CSQL_NO_PAGER_S:
	  csql_arg.nopager = true;
	  break;
	case VERSION_S:
	  utility_csql_print (MSGCAT_UTIL_GENERIC_VERSION, UTIL_CSQL_NAME,
			      VERSION);
	  return;
	default:
	  goto print_usage;
	}
    }

  if (argc - optind == 1)
    {
      csql_arg.db_name = argv[optind];
    }
  else if (argc > optind)
    {
      utility_csql_print (MSGCAT_UTIL_GENERIC_ARGS_OVER, argv[optind + 1]);
      goto print_usage;
    }
  else
    {
      utility_csql_print (MSGCAT_UTIL_GENERIC_MISS_DBNAME);
      goto print_usage;
    }

  if (csql_arg.sa_mode && csql_arg.cs_mode)
    {
      /* Don't allow both at once. */
      goto print_usage;
    }
  else if (csql_arg.sa_mode)
    {
      utility_load_library (&util_library, LIB_UTIL_SA_NAME);
    }
  else
    {
      utility_load_library (&util_library, LIB_UTIL_CS_NAME);
    }

  if (util_library == NULL)
    {
      utility_load_print_error (stderr);
      return ER_GENERIC_ERROR;
    }

  utility_load_symbol (util_library, (DSO_HANDLE *) (&sqlx), "sqlx");
  if (sqlx == NULL)
    {
      utility_load_print_error (stderr);
      return ER_GENERIC_ERROR;
    }
  return (*sqlx) (argv[0], &csql_arg);

print_usage:
  utility_csql_usage ();
  return EXIT_FAILURE;
}
