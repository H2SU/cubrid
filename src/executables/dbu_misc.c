/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 * 
 *      dbu_misc.c: common utility functions
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <string.h>
#include <getopt.h>
#include <errno.h>
#if !defined(WINDOWS)
#include <unistd.h>
#include <dlfcn.h>
#endif
#include "error_code.h"
#include "dbu_misc.h"
#include "utility.h"

static int
util_parse_string_table (UTIL_MAP * util_map, int index, int count,
			 char **argv);
static int util_put_option_value (UTIL_MAP * util_map, int arg_ch,
				  const char *option_arg);

/*
 * utility_make_getopt_optstring - makes optstring for getopt_long()
 *    return: optstring
 *    opt_array(in): array of option structure
 *    buf(out): buffer for optstring
 */
char *
utility_make_getopt_optstring (const GETOPT_LONG * opt_array, char *buf)
{
  int i;
  char *p = buf;
  for (i = 0; opt_array[i].name; i++)
    {
      if (opt_array[i].val < 255)
	{
	  *p++ = (char) opt_array[i].val;
	  if (opt_array[i].has_arg)
	    {
	      *p++ = ':';
	    }
	}
    }
  *p = '\0';
  return buf;
}

/*
 * utility_load_library - load the shared object 
 *
 * return: error code
 * path(in): the path of shared object
 *
 * NOTE:
 */
int
utility_load_library (DSO_HANDLE * handle, const char *path)
{
  UTILITY_INIT_FUNC init_fn;

#if defined(WINDOWS)
  (*handle) = LoadLibrary (path);
#else
  (*handle) = dlopen (path, RTLD_NOW | RTLD_GLOBAL);
#endif
  if ((*handle) == 0)
    {
      return ER_GENERIC_ERROR;
    }

  /* initialize library */
  if (utility_load_symbol (*handle, (DSO_HANDLE *) (&init_fn),
			   UTILITY_INIT_FUNC_NAME) == NO_ERROR &&
      (*init_fn) () == NO_ERROR)
    {
      return NO_ERROR;
    }
  else
    {
      *handle = NULL;
      return ER_GENERIC_ERROR;
    }
}

/*
 * utility_load_symbol - load the symbol of an utility-function in runtime
 *
 * return:
 *
 * NOTE:
 */
int
utility_load_symbol (DSO_HANDLE library_handle, DSO_HANDLE * symbol_handle,
		     const char *symbol_name)
{
#if defined(WINDOWS)
  (*symbol_handle) = GetProcAddress ((HMODULE) library_handle, symbol_name);
  return (*symbol_handle) == NULL ? ER_GENERIC_ERROR : NO_ERROR;
#else
  (*symbol_handle) = dlsym (library_handle, symbol_name);
  return (*symbol_handle) == 0 ? ER_GENERIC_ERROR : NO_ERROR;
#endif
}

/*
 * utility_load_print_error - print error message that occurred during dynamic linking
 *
 * return:
 *
 * NOTE:
 */
void
utility_load_print_error (FILE * fp)
{

#if defined(WINDOWS)
  char *error;
#endif /* WINDOWS */

  if (fp == NULL)
    {
      return;
    }

#if defined(WINDOWS)
  FormatMessage (FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
		 FORMAT_MESSAGE_IGNORE_INSERTS, NULL, GetLastError (),
		 MAKELANGID (LANG_NEUTRAL, SUBLANG_DEFAULT),
		 (LPTSTR) (&error), 0, NULL);
  fprintf (fp, "%s\n", error);
  LocalFree (error);
#else /* !WINDOWS */
  fprintf (fp, "%s\n", dlerror ());
#endif /* !WINDOWS */
}

/*
 * util_parse_argument - parse arguments of an utility
 *
 * return:
 */
static const char *
util_get_option_name (GETOPT_LONG * option, int option_value)
{
  int i = 0;

  for (i = 0; option[i].name != NULL; i++)
    {
      if (option[i].val == option_value)
	{
	  return option[i].name;
	}
    }

  /* unreachable code */
  return "";
}

/*
 * util_parse_argument - parse arguments of an utility
 *
 * return:
 *
 * NOTE:
 */
int
util_parse_argument (UTIL_MAP * util_map, int argc, char **argv)
{
  int status;
  int option_value;
  int option_index;
  char option_string[64];
  GETOPT_LONG *option = util_map->getopt_long;
  utility_make_getopt_optstring (option, option_string);
  status = NO_ERROR;
  while (status == NO_ERROR)
    {
      option_index = 0;
      option_value =
	getopt_long (argc, argv, option_string, option, &option_index);
      if (option_value == -1)
	{
	  break;
	}
      else if (option_value == '?' || option_value == ':')
	{
	  return status;
	}
      status = util_put_option_value (util_map, option_value, optarg);
      if (status != NO_ERROR)
	{
	  fprintf (stderr, "invalid '--%s' option value: %s\n",
		   util_get_option_name (option, option_value), optarg);
	  return ER_FAILED;
	}
    }

  status = util_parse_string_table (util_map, optind, argc, argv);
  return status;
}

/*
 * util_put_option_value - put parsed an argument's value to the utility map
 *
 * return:
 *
 * NOTE: An allocated memory may be leaked by the strdup.
 */
static int
util_put_option_value (UTIL_MAP * util_map, int arg_ch,
		       const char *option_arg)
{
  int i;
  UTIL_ARG_MAP *arg_map = util_map->arg_map;

  for (i = 0; arg_map[i].arg_ch; i++)
    {
      if (arg_map[i].arg_ch == arg_ch)
	{
	  switch (arg_map[i].value_info.value_type)
	    {
	    case ARG_BOOLEAN:
	      arg_map[i].arg_value = (void *) 1;
	      return NO_ERROR;
	    case ARG_INTEGER:
	      {
		int value;
		char *endptr;
		errno = 0;	/* to distinguish success/failure */
		value = strtol (option_arg, &endptr, 10);
		if ((errno == ERANGE
		     && (value == LONG_MAX || value == LONG_MIN))
		    || (errno != 0 && value == 0))
		  {
		    return ER_FAILED;
		  }
		if (*endptr != '\0')
		  {
		    return ER_FAILED;
		  }

		arg_map[i].arg_value = (void *) value;
		return NO_ERROR;
	      }
	    case ARG_STRING:
	      if (option_arg[0] == '-')
		{
		  return ER_FAILED;
		}

	      arg_map[i].arg_value = strdup (option_arg);
	      return NO_ERROR;
	    default:
	      return ER_FAILED;
	    }
	  return NO_ERROR;
	}
    }
  return ER_FAILED;
}

/*
 * util_parse_string_table - parse non-option arguments
 *
 * return:
 *
 * NOTE: An allocated memory may be leaked by the malloc and the strdup.
 */
static int
util_parse_string_table (UTIL_MAP * util_map, int index, int count,
			 char **argv)
{
  int i;
  int need_args_num;
  char **string_table;
  UTIL_ARG_MAP *string_table_arg = NULL;
  int num_string_args;
  for (i = 0; i < util_map->arg_map[i].arg_ch; i++)
    {
      if (util_map->arg_map[i].arg_ch == OPTION_STRING_TABLE)
	{
	  string_table_arg = &util_map->arg_map[i];
	  need_args_num = util_map->need_args_num;
	  break;
	}
    }
  if (string_table_arg == NULL)
    {
      return ER_FAILED;
    }
  num_string_args = count - index;
  string_table = (char **) malloc (sizeof (char *) * num_string_args);
  if (string_table == NULL)
    {
      return ER_FAILED;
    }
  for (i = 0; index < count; index++, i++)
    {
      string_table[i] = argv[index];
      //fprintf (stdout, "%s\n", (*string_table)[i]);
    }
  string_table_arg->arg_value = string_table;
  string_table_arg->value_info.num_strings = num_string_args;
  if (need_args_num < num_string_args)
    {
      fprintf (stderr, "'%s' argument is not needed.\n",
	       string_table[need_args_num]);
      return ER_FAILED;
    }
  return NO_ERROR;
}
