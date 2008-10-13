/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * iqcmd.c : SQLX main module
 *
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <fcntl.h>
#include <limits.h>
#if defined(WINDOWS)
#include <direct.h>
#include <io.h>
#else /* !WINDOWS */
#include <sys/time.h>
#include <sys/errno.h>
#include <signal.h>
#include <wctype.h>
#endif /* !WINDOWS */
#if defined(GNU_Readline)
#include <readline/readline.h>
#include <readline/history.h>
#else /* !GNU_Readline */
#if !defined(WINDOWS)
#include "readline.h"
#endif /* !WINDOWS */
#endif /* GNU_Readline */

#include "sqlx.h"
#include "system_parameter.h"
#include "message_catalog.h"
#include "porting.h"
#include "release_string.h"
#include "error_manager.h"
#include "language_support.h"
#include "network.h"
#include "schema_manager_3.h"
#include "qo.h"
#include "environment_variable.h"
#include "tcp.h"
#include "db.h"
#include "parser.h"
#include "network_interface_sky.h"
#include "utility.h"

#if defined(WINDOWS)
#include "file_io.h"		/* needed for _wyield() */
#include "pccurses.h"
#endif /* WINDOWS */

/* input type specification for csql_execute_statements() */
enum
{
  FILE_INPUT = 0,		/* FILE stream */
  STRING_INPUT = 1,		/* null-terminated string */
  EDITOR_INPUT = 2		/* command buffer */
};

#define CSQL_SESSION_COMMAND_PREFIX(C)	(((C) == ';') || ((C) == '!'))
#define SQLXRC_FILE_NAME        ".sqlxrc"

/* size of input buffer */
#define LINE_BUFFER_SIZE         (4000)

#if !defined(GNU_Readline)
#if !defined(WINDOWS)
/* current max keyword is 16 + nul char + 3 for expansion */

static int csql_Keyword_num;
static KEYWORD_RECORD *csql_Keyword_list;
#endif /* !WINDOWS */
#endif /* !GNU_Readline */

int csql_Row_count;
int csql_Num_failures;

/* command editor lines */
int csql_Line_lwm = -1;

/* default environment command names */
char csql_Print_cmd[PATH_MAX] = "lpr";
char csql_Pager_cmd[PATH_MAX] = "more";	/* PAGER does not work on WINDOWS */
#if defined(WINDOWS)
char csql_Editor_cmd[PATH_MAX] = "notepad";
#else
char csql_Editor_cmd[PATH_MAX] = "vi";
#endif

#if defined(WINDOWS)
char csql_Shell_cmd[PATH_MAX] = "command.com";
#else
char csql_Shell_cmd[PATH_MAX] = "csh";
#endif

/* tty file stream which is used for conversation with users.
 * In batch mode, this will be set to "/dev/null"
 */
static FILE *csql_Tty_fp = NULL;

/* scratch area to make a message text to be displayed.
 * NOTE: Never put chars more than sizeof(csql_Scratch_text).
 */
char csql_Scratch_text[SCRATCH_TEXT_LEN];

int csql_Error_code = NO_ERROR;

static char csql_Sqlx_prompt[100];
static char csql_Sqlx_name[100];
static char csql_Sqlx_product[100];


/*
 * Handles for the various files
 */
FILE *csql_Input_fp = NULL;
FILE *csql_Output_fp = NULL;
FILE *csql_Error_fp = NULL;

/*
 * Global longjmp environment to terminate the sqlx() interpreter in the
 * event of fatal error.  This should be used rather than calling
 * exit(), primarily for the Windows version of the interpreter.
 *
 * Set csql_Exit_status to the numeric status code to be returned from
 * the sqlx() function after the longjmp has been performed.
 */
static jmp_buf csql_Exit_env;
static int csql_Exit_status = EXIT_SUCCESS;

/* this is non-zero if there is a dangling connection to a database */
static bool csql_Database_connected = false;

static bool csql_Is_interactive = false;
static bool csql_Is_echo_on = false;
static bool csql_Is_histo_on = false;
static bool csql_Is_time_on = false;

static jmp_buf csql_Jmp_buf;

#if !defined(GNU_Readline) && !defined(WINDOWS)
static char *csql_keyword_generator (const char *text, int state);
static char **csql_cmd_completion_handler (const char *text, int start,
					   int end);
static void init_readline ();
#endif /* ! GNU_Readline && ! WINDOWS */
static void csql_pipe_handler (int sig_no);
static void display_buffer (void);
static void csql_execute_rcfile (CSQL_ARGUMENT * csql_arg);
static void start_csql (CSQL_ARGUMENT * csql_arg);
static void csql_read_file (const char *file_name);
static void csql_write_file (const char *file_name, int append_flag);
static void display_error (DB_SESSION * session, int stmt_start_line_no);
static void free_attr_spec (DB_QUERY_TYPE ** attr_spec);
static void csql_set_sys_param (const char *arg_str);
static void csql_get_sys_param (const char *arg_str);
static void csql_exit_init (void);
static void csql_exit_cleanup (void);
static void csql_print_buffer (void);
static void csql_change_working_directory (const char *dirname);
static void csql_exit_session (int error);

static int csql_execute_statements (const CSQL_ARGUMENT * csql_arg, int type,
				    const void *stream);

#if !defined(GNU_Readline)
#if !defined(WINDOWS)
/*
 * for readline keyword completion
 */
/*
 * csql_keyword_generator()
 *   return: char*
 *   text(in)
 *   state(in)
 */
static char *
csql_keyword_generator (const char *text, int state)
{
  static int list_index, len;

  /* If this is a new word to complete, initialize now.  This
     includes saving the length of TEXT for efficiency, and
     initializing the index variable to 0. */
  if (!state)
    {
      list_index = 0;
      len = strlen (text);
    }

  if (len == 0)
    {
      return ((char *) NULL);
    }
  if (csql_Keyword_list == NULL)
    {
      return ((char *) NULL);
    }

  /* Return the next name which partially matches
     from the keyword list. */
  while (list_index < csql_Keyword_num)
    {
      if (strncasecmp ((csql_Keyword_list + list_index)->keyword, text, len)
	  == 0)
	{
	  char *ret_str = strdup ((csql_Keyword_list + list_index)->keyword);
	  list_index++;
	  return ret_str;
	}

      list_index++;
    }

  /* If no keyword matched, then return NULL. */
  return ((char *) NULL);
}

/*
 * csql_cmd_completion_handler()
 *   return: char**
 *   text(in)
 *   start(in)
 *   end(in)
 */
static char **
csql_cmd_completion_handler (const char *text, int start, int end)
{
  char **matches;

  matches = (char **) NULL;
  matches = completion_matches (text, csql_keyword_generator);
  rl_attempted_completion_over = 1;

  return (matches);
}

/*
 * init_readline() - initialize libedit module
 *   return: none
 */
static void
init_readline ()
{
  rl_attempted_completion_function = csql_cmd_completion_handler;
}
#endif /* !WINDOWS */
#endif /* !GNU_Readline */

/*
 * csql_display_msg() - displays the given msg to output device
 *   return: none
 *   string(in)
 */
void
csql_display_msg (const char *string)
{
  csql_fputs ("\n", csql_Tty_fp);
  csql_fputs (string, csql_Tty_fp);
  csql_fputs ("\n", csql_Tty_fp);
}

/*
 * csql_pipe_handler() generic longjmp'ing signal handler used
 *                   where we need to catch broken pipe.
 *   return: none
 *   sig_no(in)
 */
static void
csql_pipe_handler (int sig_no)
{
  longjmp (csql_Jmp_buf, 1);
}

/*
 * display_buffer() - display command buffer into stdout
 *   return: none
 */
static void
display_buffer (void)
{
  int l = 1;
  FILE *pf;
#if !defined(WINDOWS)
  void (*csql_pipe_save) (int);
#endif /* !WINDOWS */

#if !defined(WINDOWS)
  /* There is no SIGPIPE on WINDOWS */
  csql_pipe_save = os_set_signal_handler (SIGPIPE, &csql_pipe_handler);
#endif /* !WINDOWS */
  if (setjmp (csql_Jmp_buf) == 0)
    {
      char *edit_contents, *p;
      pf = csql_popen (csql_Pager_cmd, csql_Output_fp);

      edit_contents = csql_edit_contents_get ();

      putc ('\n', pf);
      while (edit_contents != NULL && *edit_contents != '\0')
	{
	  fprintf (pf, "%4d  ", l++);
	  p = strchr (edit_contents, '\n');
	  if (p)
	    {
	      fwrite (edit_contents, 1, p - edit_contents, pf);
	      edit_contents = p + 1;
	    }
	  else
	    {
	      fwrite (edit_contents, 1, strlen (edit_contents), pf);
	      edit_contents = NULL;
	    }
	  fprintf (pf, "\n");
	}
      putc ('\n', pf);

      csql_pclose (pf, csql_Output_fp);
    }
#if !defined(WINDOWS)
  (void) os_set_signal_handler (SIGPIPE, csql_pipe_save);
#endif /* !WINDOWS */
}

/*
 * csql_execute_rcfile() - execute commands from rc file
 *   return: none
 *   csql_arg(in/out)
 */
static void
csql_execute_rcfile (CSQL_ARGUMENT * csql_arg)
{
  char line[LINE_MAX], *s, *t, path[PATH_MAX + 1];
  int len, cmd;
  FILE *fp;

  fp = fopen (SQLXRC_FILE_NAME, "r");
  if (!fp)
    {
      snprintf (path, PATH_MAX, "%s/%s", getenv ("HOME"), SQLXRC_FILE_NAME);
      fp = fopen (path, "r");
      if (!fp)
	{
	  return;
	}
    }

  while (!feof (fp))
    {
      if (fgets (line, LINE_MAX, fp))
	{
	  len = strlen (line);
	  if (!len)
	    {
	      continue;
	    }

	  s = line;
	  t = line + len - 1;
	  while (isspace (*s) && s < t)
	    {
	      s++;
	    }
	  while (isspace (*t) && s < t)
	    {
	      t--;
	    }
	  *(t + 1) = '\0';
	  if (*s == '#' || *s == '\n')
	    {
	      continue;
	    }

	  if (CSQL_SESSION_COMMAND_PREFIX (*s))
	    {
	      if (csql_Is_echo_on)
		{
		  fprintf (csql_Output_fp, "%s\n", s);
		}

	      s = strtok (++s, " \t\n");
	      t = strtok (NULL, " \t\n");
	      if (s == NULL || t == NULL || *s == '\0' ||
		  (cmd = csql_get_session_cmd_no (s)) < 0)
		{
		  continue;
		}

	      switch ((SESSION_CMD) cmd)
		{
		case S_CMD_CD:
		  csql_change_working_directory (t);
		  break;
		case S_CMD_COMMIT:
		  (void) db_commit_transaction ();
		  break;
		case S_CMD_ROLLBACK:
		  (void) db_abort_transaction ();
		  break;
		case S_CMD_AUTOCOMMIT:
		  if (!strcasecmp (t, "on"))
		    {
		      csql_arg->auto_commit = true;
		    }
		  else if (!strcasecmp (t, "off"))
		    {
		      csql_arg->auto_commit = false;
		    }
		  break;
		case S_CMD_SHELL_CMD:
		  strncpy (csql_Shell_cmd, t, PATH_MAX - 1);
		  break;
		case S_CMD_EDIT_CMD:
		  strncpy (csql_Editor_cmd, t, PATH_MAX - 1);
		  break;
		case S_CMD_PRINT_CMD:
		  strncpy (csql_Print_cmd, t, PATH_MAX - 1);
		  break;
		case S_CMD_PAGER_CMD:
		  strncpy (csql_Pager_cmd, t, PATH_MAX - 1);
		  break;
		case S_CMD_NOPAGER_CMD:
		  csql_Pager_cmd[0] = '\0';
		  break;
		case S_CMD_SCHEMA:
		  csql_help_schema ((*t) ? t : NULL);
		  break;
		case S_CMD_TRIGGER:
		  csql_help_trigger ((*t) ? t : NULL);
		  break;
		case S_CMD_DATABASE:
		  fprintf (csql_Output_fp, "\n\t%s\n\n", csql_arg->db_name);
		  break;
		case S_CMD_SET_PARAM:
		  csql_set_sys_param (t);
		  break;
		case S_CMD_GET_PARAM:
		  csql_get_sys_param (t);
		  break;
		case S_CMD_ECHO:
		  if (!strcasecmp (t, "on"))
		    {
		      csql_Is_echo_on = true;
		    }
		  if (!strcasecmp (t, "off"))
		    {
		      csql_Is_echo_on = false;
		    }
		  break;
		case S_CMD_DATE:
		  {
		    time_t tloc = time (NULL);
		    struct tm *tmloc;
		    char str[80];
		    tmloc = localtime (&tloc);
		    strftime (str, 80, "%a %B %d %H:%M:%S %Z %Y", tmloc);
		    fprintf (csql_Output_fp, "\n\t%s\n", str);
		  }
		  break;
		case S_CMD_TIME:
		  if (!strcasecmp (t, "on"))
		    {
		      csql_Is_time_on = true;
		    }
		  if (!strcasecmp (t, "off"))
		    {
		      csql_Is_time_on = false;
		    }
		  break;
		case S_CMD_HISTO:
		  if (histo_is_supported ())
		    {
		      if (!strcasecmp (t, "on"))
			{
			  csql_Is_histo_on = true;
			  histo_start ();
			}
		      else if (!strcasecmp (t, "off"))
			{
			  csql_Is_histo_on = false;
			  histo_stop ();
			}
		    }
		  break;
		case S_CMD_CLR_HISTO:
		  if (histo_is_supported ())
		    {
		      if (csql_Is_histo_on)
			{
			  histo_clear ();
			}
		    }
		  break;
		case S_CMD_DUMP_HISTO:
		  if (histo_is_supported ())
		    {
		      if (csql_Is_histo_on)
			{
			  histo_print ();
			  fprintf (csql_Output_fp, "\n");
			}
		    }
		  break;
		case S_CMD_DUMP_CLR_HISTO:
		  if (histo_is_supported ())
		    {
		      if (csql_Is_histo_on)
			{
			  histo_print ();
			  fprintf (csql_Output_fp, "\n");
			  histo_clear ();
			}
		    }
		  break;
		default:
		  break;
		}
	    }
	  else
	    {
	      (void) csql_execute_statements (csql_arg, STRING_INPUT, s);
	    }
	}
    }
  fclose (fp);
}

/*
 * start_csql()
 *   return: none
 *   sql_arg(in/out): CSQL_ARGUMENT structure
 *
 * Note:
 * There are four file pointers associated
 *      stdin     - input source
 *      stdout    - output file stream
 *      stderr    - error message file stream
 *      tty_fp    - conversation terminal file stream.
 *                  either NULL or stderr
 *
 * if -o is given, the output file descriptor is duplicated to STDOU_FILENO.
 * Also, if -i is given, -c is given or stdin is not a tty,
 *      `tty_fp' will be set to NULL. (No conversational messages)
 * Otherwise, `tty_fp' will be set to stderr
 *
 * If `single_line_execution' is true, it attemts to execute as soon as
 * it get a line. There is command buffer associated. This is effective
 * only when INTERACTIVE mode (stdin is tty).
 * If `command' is not NULL, it'll execute the command and exit and
 * `-i' option, preceding pipe (if any), `-s' option had no effect.
 */
static void
start_csql (CSQL_ARGUMENT * csql_arg)
{
  unsigned char line_buf[LINE_BUFFER_SIZE];
  char *line_read = NULL;
#if defined(GNU_Readline)
  char *t;
#else /* !GNU_Readline */
#if !defined(WINDOWS)
  HIST_ENTRY *hist_entry;
  int i;
#endif /* !WINDOWS */
#endif /* GNU_Readline */
  int line_length;
  char *ptr;			/* loop pointer */
  int cmd_no;			/* session command number */
  char *sess_cmd;		/* session command pointer */
  char *argument;		/* argument str */
  DB_HELP_COMMAND sqlx_cmd_no;	/* SQL/X cmd no for syntax help */

  if (csql_arg->column_output && csql_arg->line_output)
    {
      csql_Error_code = CSQL_ERR_INVALID_ARG_COMBINATION;
      goto fatal_error;
    }

  csql_Output_fp = stdout;

  if (csql_arg->out_file_name != NULL)
    {
      csql_Output_fp = fopen (csql_arg->out_file_name, "w");
      if (csql_Output_fp == NULL)
	{
	  csql_Error_code = CSQL_ERR_OS_ERROR;
	  goto fatal_error;
	}
    }

  /* For batch file input and SQL/X command argument input */
  csql_Tty_fp = NULL;
  if (csql_arg->command)
    {
      /* command input */
      csql_exit_session (csql_execute_statements
			 (csql_arg, STRING_INPUT, csql_arg->command));
    }

  if (!csql_Is_interactive && !csql_arg->single_line_execution)
    {
      csql_exit_session (csql_execute_statements
			 (csql_arg, FILE_INPUT, csql_Input_fp));
    }

  /* Start interactive conversation or single line execution */
  if (csql_Is_interactive)
    {
      csql_Tty_fp = csql_Error_fp;
    }

  /* display product title */
  sprintf (csql_Scratch_text, "\n\t%s %s\n\n", csql_Sqlx_product,
	   csql_get_message (CSQL_INITIAL_SQLX_TITLE));
  csql_fputs (csql_Scratch_text, csql_Tty_fp);

  sprintf (csql_Scratch_text, "\n%s\n\n",
	   csql_get_message (CSQL_INITIAL_HELP_MSG));
  csql_fputs (csql_Scratch_text, csql_Tty_fp);

  csql_execute_rcfile (csql_arg);

#if !defined(GNU_Readline)
#if !defined(WINDOWS)
  if (csql_Is_interactive)
    {
      init_readline ();
      stifle_history (PRM_CSQL_HISTORY_NUM);
      using_history ();
      csql_Keyword_list = pt_get_keyword_rec (&csql_Keyword_num);

      strcat (csql_Sqlx_prompt, " ");
    }
#endif /* !WINDOWS */
#endif /* !GNU_Readline */

  for (;;)
    {
      char *line_read_alloced = NULL;
      bool flag_append_new_line;
      bool incomplete_prev_line = false;

      if (line_read_alloced)
	{
	  free (line_read_alloced);
	  line_read_alloced = NULL;
	}
      flag_append_new_line = false;

      memset (line_buf, 0, LINE_BUFFER_SIZE);
#if defined(GNU_Readline)
      if (csql_Is_interactive)
	{
	  if ((line_read = readline (csql_Sqlx_prompt)) != NULL)
	    {
	      for (t = line_read; isspace (*t); t++)
		;
	      if (*t != '\0')
		add_history (line_read);
	      line_read_alloced = line_read;
	      flag_append_new_line = true;
	    }
	}
      else
	{
	  line_read =
	    fgets ((char *) line_buf, LINE_BUFFER_SIZE, csql_Input_fp);
	}
      if (line_read == NULL)
	{
	  if (errno == EINTR && !feof (csql_Input_fp))
	    {
	      fprintf (csql_Output_fp, "\n");
	      continue;
	    }

	  if (line_read_alloced)
	    {
	      free (line_read_alloced);
	      line_read_alloced = NULL;
	    }
	  csql_edit_contents_finalize ();
	  csql_exit_session (0);
	  break;
	}
#else /* !GNU_Readline */
#if !defined(WINDOWS)
      if (csql_Is_interactive)
	{
	  if ((line_read = readline (csql_Sqlx_prompt)) != NULL)
	    {
	      line_read_alloced = line_read;
	      flag_append_new_line = true;
	    }
	}
      else
	{
	  line_read =
	    fgets ((char *) line_buf, LINE_BUFFER_SIZE, csql_Input_fp);
	}
#else /* WINDOWS */
      if (csql_Is_interactive)
	{
	  fprintf (csql_Output_fp, "%s ", csql_Sqlx_prompt);	/* display prompt */
	}

      line_read = fgets ((char *) line_buf, LINE_BUFFER_SIZE, csql_Input_fp);
#endif /* !WINDOWS */
      fflush (csql_Output_fp);

      if (line_read == NULL)
	{
	  if (errno == EINTR && !feof (csql_Input_fp))
	    {
	      fprintf (csql_Output_fp, "\n");
	      continue;
	    }

	  if (line_read_alloced)
	    {
	      free (line_read_alloced);
	      line_read_alloced = NULL;
	    }
	  csql_edit_contents_finalize ();
	  csql_exit_session (0);
	}
#endif /* GNU_Readline */

      line_length = strlen (line_read);
      for (ptr = line_read + line_length - 1; line_length > 0; ptr--)
	{
	  if (*ptr == '\n')
	    {
	      flag_append_new_line = true;
	    }
	  if (iswspace ((wint_t) * ptr))
	    {
	      *ptr = '\0';
	      line_length--;
	    }
	  else
	    {
	      break;
	    }

	}

      if (incomplete_prev_line || !CSQL_SESSION_COMMAND_PREFIX (line_read[0]))
	{
	  bool line_continuation = false;

	  if (flag_append_new_line)
	    {
	      incomplete_prev_line = false;
	    }
	  else
	    {
	      line_continuation = true;
	      incomplete_prev_line = true;
	    }
	  if (csql_arg->single_line_execution &&
	      (line_length == 0
	       || (line_length > 0 && line_read[line_length - 1] != ';')))
	    {
	      line_continuation = true;
	    }
	  if (csql_edit_contents_append (line_read, flag_append_new_line) !=
	      CSQL_SUCCESS)
	    {
	      goto error_continue;
	    }
	  if (csql_arg->single_line_execution && !line_continuation)
	    {
	      /* single-line-oriented execution */
	      (void) csql_execute_statements (csql_arg, EDITOR_INPUT, NULL);
	      csql_edit_contents_clear ();
	    }
	  continue;
	}

      /* Now, we found that this input line is started with the session
       * command prefix.
       */

      /* get session command and argument */
      ptr = line_read;
      if (csql_Is_echo_on)
	{
	  fprintf (csql_Output_fp, "%s\n", line_read);
	}

      /* 'ptr' points to the prefix char. */
      for (ptr++; *ptr != '\0' && iswspace ((wint_t) * ptr); ptr++)
	{
	  ;
	}
      sess_cmd = (char *) ptr;
      for (; *ptr != '\0' && !iswspace ((wint_t) * ptr); ptr++)
	{
	  ;
	}
      if (iswspace ((wint_t) * ptr))
	{
	  *ptr++ = '\0';	/* put null-termination */
	}
      for (; *ptr != '\0' && iswspace ((wint_t) * ptr); ptr++)
	{
	  ;
	}
      argument = (char *) ptr;

      /* Now, `sess_cmd' points to null-terminated session command name and
       * `argument' points to remaining argument (it may be '\0' if not given).
       */

      if (*sess_cmd == '\0')
	continue;
      cmd_no = csql_get_session_cmd_no (sess_cmd);
      if (cmd_no == -1)
	{
	  goto error_continue;
	}

      switch ((SESSION_CMD) cmd_no)
	{
	  /* File stuffs */

	case S_CMD_READ:	/* read a file */
	  csql_read_file (argument);
	  break;

	case S_CMD_WRITE:	/* write to a file */
	case S_CMD_APPEND:	/* append to a file */
	  csql_write_file (argument, cmd_no == S_CMD_APPEND);
	  break;

	case S_CMD_PRINT:
	  csql_print_buffer ();
	  break;

	case S_CMD_SHELL:	/* invoke shell */
	  csql_invoke_system (csql_Shell_cmd);
	  csql_fputs ("\n", csql_Tty_fp);
	  break;

	case S_CMD_CD:
	  csql_change_working_directory (argument);
	  break;

	case S_CMD_EXIT:	/* exit */
	  csql_edit_contents_finalize ();
	  csql_exit_session (0);
	  break;

	  /* Edit stuffs */

	case S_CMD_CLEAR:	/* clear editor buffer */
	  csql_edit_contents_clear ();
	  break;

	case S_CMD_EDIT:	/* invoke system editor */
	  if (csql_invoke_system_editor () != CSQL_SUCCESS)
	    {
	      goto error_continue;
	    }
	  break;

	case S_CMD_LIST:	/* display buffer */
	  display_buffer ();
	  break;

	  /* Command stuffs */
	case S_CMD_RUN:
	  (void) csql_execute_statements (csql_arg, EDITOR_INPUT, NULL);
	  break;

	case S_CMD_XRUN:
	  (void) csql_execute_statements (csql_arg, EDITOR_INPUT, NULL);
	  csql_edit_contents_clear ();
	  break;

	case S_CMD_COMMIT:
	  if (db_commit_transaction () < 0)
	    {
	      csql_display_csql_err (0, 0);
	      csql_check_server_down ();
	    }
	  else
	    {
	      csql_display_msg (csql_get_message (CSQL_STAT_COMMITTED_TEXT));
	    }
	  break;

	case S_CMD_ROLLBACK:
	  if (db_abort_transaction () < 0)
	    {
	      csql_display_csql_err (0, 0);
	      csql_check_server_down ();
	    }
	  else
	    {
	      csql_display_msg (csql_get_message (CSQL_STAT_ROLLBACKED_TEXT));
	    }
	  break;

	case S_CMD_AUTOCOMMIT:
	  if (!strcasecmp (argument, "on"))
	    {
	      csql_arg->auto_commit = true;
	    }
	  else if (!strcasecmp (argument, "off"))
	    {
	      csql_arg->auto_commit = false;
	    }

	  fprintf (csql_Output_fp, "AUTOCOMMIT IS %s\n",
		   (csql_arg->auto_commit
		    && PRM_CSQL_AUTO_COMMIT ? "ON" : "OFF"));
	  break;

	  /* Environment stuffs */

	case S_CMD_SHELL_CMD:
	case S_CMD_EDIT_CMD:
	case S_CMD_PRINT_CMD:
	case S_CMD_PAGER_CMD:
	  if (*argument == '\0')
	    {
	      fprintf (csql_Error_fp, "\n\t%s\n\n",
		       (cmd_no == S_CMD_SHELL_CMD) ? csql_Shell_cmd :
		       (cmd_no == S_CMD_EDIT_CMD) ? csql_Editor_cmd :
		       (cmd_no ==
			S_CMD_PRINT_CMD) ? csql_Print_cmd : csql_Pager_cmd);
	    }
	  else
	    {
	      strncpy ((cmd_no == S_CMD_SHELL_CMD) ? csql_Shell_cmd :
		       (cmd_no == S_CMD_EDIT_CMD) ? csql_Editor_cmd :
		       (cmd_no == S_CMD_PRINT_CMD) ? csql_Print_cmd :
		       csql_Pager_cmd, argument, PATH_MAX - 1);
	    }
	  break;

	case S_CMD_NOPAGER_CMD:
	  csql_Pager_cmd[0] = '\0';
	  break;

	  /* Help stuffs */
	case S_CMD_HELP:
	  csql_help_menu ();
	  break;

	case S_CMD_SCHEMA:
	  csql_help_schema ((argument[0] == '\0') ? NULL : argument);
	  if (csql_arg->auto_commit && PRM_CSQL_AUTO_COMMIT)
	    {
	      if (db_commit_transaction () < 0)
		{
		  csql_display_csql_err (0, 0);
		  csql_check_server_down ();
		}
	      else
		{
		  csql_display_msg (csql_get_message
				    (CSQL_STAT_COMMITTED_TEXT));
		}
	    }
	  break;

	case S_CMD_TRIGGER:
	  csql_help_trigger ((argument[0] == '\0') ? NULL : argument);
	  if (csql_arg->auto_commit && PRM_CSQL_AUTO_COMMIT)
	    {
	      if (db_commit_transaction () < 0)
		{
		  csql_display_csql_err (0, 0);
		  csql_check_server_down ();
		}
	      else
		{
		  csql_display_msg (csql_get_message
				    (CSQL_STAT_COMMITTED_TEXT));
		}
	    }
	  break;

	case S_CMD_SYNTAX:
	  if (csql_get_help_cmd_no ((argument[0] == '\0') ? NULL : argument,
				    &sqlx_cmd_no) == CSQL_FAILURE)
	    goto error_continue;
	  csql_help_syntax (sqlx_cmd_no);
	  break;

	case S_CMD_INFO:
	  csql_help_info ((argument[0] == '\0') ? NULL : argument,
			  csql_arg->auto_commit && PRM_CSQL_AUTO_COMMIT);
	  break;

	case S_CMD_DATABASE:
	  fprintf (csql_Error_fp, "\n\t%s\n\n", csql_arg->db_name);
	  break;

	case S_CMD_SET_PARAM:
	  csql_set_sys_param (argument);
	  break;

	case S_CMD_GET_PARAM:
	  csql_get_sys_param (argument);
	  break;

	case S_CMD_ECHO:
	  if (!strcasecmp (argument, "on"))
	    {
	      csql_Is_echo_on = true;
	    }
	  else if (!strcasecmp (argument, "off"))
	    {
	      csql_Is_echo_on = false;
	    }
	  else
	    {
	      fprintf (csql_Output_fp, "ECHO IS %s\n",
		       (csql_Is_echo_on ? "ON" : "OFF"));
	    }
	  break;

	case S_CMD_DATE:
	  {
	    time_t tloc = time (NULL);
	    struct tm *tmloc;
	    char str[80];
	    tmloc = localtime (&tloc);
	    strftime (str, 80, "%a %B %d %H:%M:%S %Z %Y", tmloc);
	    fprintf (csql_Output_fp, "\n\t%s\n", str);
	  }
	  break;

	case S_CMD_TIME:
	  if (!strcasecmp (argument, "on"))
	    {
	      csql_Is_time_on = true;
	    }
	  else if (!strcasecmp (argument, "off"))
	    {
	      csql_Is_time_on = false;
	    }
	  else
	    {
	      fprintf (csql_Output_fp, "TIME IS %s\n",
		       (csql_Is_time_on ? "ON" : "OFF"));
	    }
	  break;

	case S_CMD_HISTO:
	  if (histo_is_supported ())
	    {
	      if (!strcasecmp (argument, "on"))
		{
		  histo_start ();
		  csql_Is_histo_on = true;
		}
	      else if (!strcasecmp (argument, "off"))
		{
		  histo_stop ();
		  csql_Is_histo_on = false;
		}
	      else
		{
		  fprintf (csql_Output_fp, ".hist IS %s\n",
			   (csql_Is_histo_on ? "ON" : "OFF"));
		}
	    }
	  else
	    {
	      fprintf (csql_Output_fp, "Histogram on execution statistics "
		       "is only allowed for the server started "
		       "with `histo_enable=1'\n");
	    }
	  break;

	case S_CMD_CLR_HISTO:
	  if (histo_is_supported ())
	    {
	      if (csql_Is_histo_on)
		{
		  histo_clear ();
		}
	      else
		{
		  fprintf (csql_Output_fp, ".hist IS currently OFF\n");
		}
	    }
	  else
	    {
	      fprintf (csql_Output_fp, "Histogram on execution statistics "
		       "is only allowed for the server started "
		       "with `histo_enable=1'\n");
	    }
	  break;

	case S_CMD_DUMP_HISTO:
	  if (histo_is_supported ())
	    {
	      if (csql_Is_histo_on)
		{
		  histo_print ();
		  fprintf (csql_Output_fp, "\n");
		}
	      else
		{
		  fprintf (csql_Output_fp, ".hist IS currently OFF\n");
		}
	    }
	  else
	    {
	      fprintf (csql_Output_fp, "Histogram on execution statistics "
		       "is only allowed for the server started "
		       "with `histo_enable=1'\n");
	    }
	  break;

	case S_CMD_DUMP_CLR_HISTO:
	  if (histo_is_supported ())
	    {
	      if (csql_Is_histo_on)
		{
		  histo_print ();
		  fprintf (csql_Output_fp, "\n");
		  histo_clear ();
		}
	      else
		{
		  fprintf (csql_Output_fp, ".hist IS currently OFF\n");
		}
	    }
	  else
	    {
	      fprintf (csql_Output_fp, "Histogram on execution statistics "
		       "is only allowed for the server started "
		       "with `histo_enable=1'\n");
	    }
	  break;

	case S_CMD_HISTORY_READ:
#if !defined(GNU_Readline)
#if !defined(WINDOWS)
	  if (csql_Is_interactive)
	    {
	      if (argument[0] != '\0')
		{
		  i = atoi (argument);
		  if (i > 0)
		    {
		      HIST_ENTRY *hist;
		      hist = history_get ((history_base - 1) + i - 1);
		      if (hist != NULL)
			{
			  if (csql_edit_contents_append (hist->line, true) !=
			      CSQL_SUCCESS)
			    {
			      goto error_continue;
			    }
			}
		      else
			{
			  fprintf (csql_Error_fp,
				   "ERROR: Invalid history number(%s).\n",
				   argument);
			}
		    }
		  else
		    {
		      fprintf (csql_Error_fp,
			       "ERROR: Invalid history number\n");
		    }
		}
	      else
		{
		  fprintf (csql_Error_fp,
			   "ERROR: HISTORYRead {history_number}\n");
		}
	    }
#endif /* !WINDOWS */
#endif /* !GNU_Readline */
	  break;

	case S_CMD_HISTORY_LIST:
#if !defined(GNU_Readline)
#if !defined(WINDOWS)
	  if (csql_Is_interactive)
	    {
	      /* rewind history */
	      while (next_history ())
		{
		  ;
		}

	      for (i = 0, hist_entry = current_history (); hist_entry;
		   hist_entry = previous_history (), i++)
		{
		  fprintf (csql_Output_fp, "----< %d >----\n", i + 1);
		  fprintf (csql_Output_fp, "%s\n\n", hist_entry->line);
		}
	    }
#endif /* !WINDOWS */
#endif /* !GNU_Readline */
	  break;
	}

      continue;

    error_continue:

      nonscr_display_error (csql_Scratch_text, SCRATCH_TEXT_LEN);
    }

fatal_error:
  csql_edit_contents_finalize ();
  if (histo_is_supported ())
    {
      if (csql_Is_histo_on)
	{
	  csql_Is_histo_on = false;
	  histo_stop ();
	}
    }

  db_shutdown ();
  csql_Database_connected = false;
  nonscr_display_error (csql_Scratch_text, SCRATCH_TEXT_LEN);
  csql_exit (EXIT_FAILURE);
}

/*
 * csql_read_file() - read a file into command editor
 *   return: none
 *   file_name(in): input file name
 */
static void
csql_read_file (const char *file_name)
{
  static char current_file[PATH_MAX] = "";
  const char *p, *q;		/* pointer to string */
  FILE *fp = (FILE *) NULL;	/* file stream */

  p = csql_get_real_path (file_name);	/* get real path name */

  if (p == NULL || p[0] == '\0')
    {
      /*
       * No filename given; use the last one we were given.  If we've
       * never received one before we have a genuine error.
       */
      if (current_file[0] != '\0')
	{
	  p = current_file;
	}
      else
	{
	  csql_Error_code = CSQL_ERR_FILE_NAME_MISSED;
	  goto error;
	}
    }

  for (q = p; *q != '\0' && !iswspace ((wint_t) * q); q++)
    ;
  if (*q != '\0')
    {				/* contains more than one file name */
      csql_Error_code = CSQL_ERR_TOO_MANY_FILE_NAMES;
      goto error;
    }

  fp = fopen (p, "r");
  if (fp == NULL)
    {
      csql_Error_code = CSQL_ERR_OS_ERROR;
      goto error;
    }

  /*
   * We've successfully read the file, so remember its name for
   * subsequent reads.
   */
  strncpy (current_file, p, sizeof (current_file));

  if (csql_edit_read_file (fp) == CSQL_FAILURE)
    {
      goto error;
    }

  fclose (fp);

  csql_display_msg (csql_get_message (CSQL_STAT_READ_DONE_TEXT));

  return;

error:
  if (fp != NULL)
    {
      fclose (fp);
    }
  nonscr_display_error (csql_Scratch_text, SCRATCH_TEXT_LEN);
}

/*
 * csql_write_file() - write (or append) the current content of editor into
 *                   user specified file
 *   return: none
 *   file_name(in): output file name
 *   append_flag(in): true if append
 */
static void
csql_write_file (const char *file_name, int append_flag)
{
  static char current_file[PATH_MAX] = "";
  /* the name of the last file written */
  const char *p, *q;		/* pointer to string */
  FILE *fp = (FILE *) NULL;	/* file stream */

  p = csql_get_real_path (file_name);	/* get real path name */

  if (p == NULL || p[0] == '\0')
    {
      /*
       * No filename given; use the last one we were given.  If we've
       * never received one before we have a genuine error.
       */
      if (current_file[0] != '\0')
	p = current_file;
      else
	{
	  csql_Error_code = CSQL_ERR_FILE_NAME_MISSED;
	  goto error;
	}
    }

  for (q = p; *q != '\0' && !iswspace ((wint_t) * q); q++)
    ;
  if (*q != '\0')
    {				/* contains more than one file name */
      csql_Error_code = CSQL_ERR_TOO_MANY_FILE_NAMES;
      goto error;
    }

  fp = fopen (p, (append_flag) ? "a" : "w");
  if (fp == NULL)
    {
      csql_Error_code = CSQL_ERR_OS_ERROR;
      goto error;
    }

  /*
   * We've successfully opened the file, so remember its name for
   * subsequent writes.
   */
  strncpy (current_file, p, sizeof (current_file));

  if (csql_edit_write_file (fp) == CSQL_FAILURE)
    {
      goto error;
    }

  fclose (fp);

  csql_display_msg (csql_get_message (CSQL_STAT_EDITOR_SAVED_TEXT));

  return;

error:
  if (fp != NULL)
    {
      fclose (fp);
    }
  nonscr_display_error (csql_Scratch_text, SCRATCH_TEXT_LEN);
}

/*
 * csql_print_buffer()
 *   return: none
 *
 * Note:
 *   copy command editor buffer into temporary file and
 *   invoke the user preferred print command to print
 */
static void
csql_print_buffer (void)
{
  char *cmd = NULL;
  char *fname = (char *) NULL;	/* pointer to temp file name */
  FILE *fp = (FILE *) NULL;	/* pointer to stream */

  /* create a temp file and open it */

  fname = tmpnam ((char *) NULL);
  if (fname == NULL)
    {
      csql_Error_code = CSQL_ERR_OS_ERROR;
      goto error;
    }

  fp = fopen (fname, "w");
  if (fp == NULL)
    {
      csql_Error_code = CSQL_ERR_OS_ERROR;
      goto error;
    }

  /* write the content of editor to the temp file */
  if (csql_edit_write_file (fp) == CSQL_FAILURE)
    {
      goto error;
    }

  fclose (fp);
  fp = (FILE *) NULL;

  /* invoke the print command */
  cmd = csql_get_tmp_buf (1 + strlen (csql_Print_cmd) + 3 + strlen (fname));
  if (cmd == NULL)
    {
      goto error;
    }
  /*
   * Parenthesize the print command and supply its input through stdin,
   * just in case it's a pipe or something odd.
   */
  sprintf (cmd, "(%s) <%s", csql_Print_cmd, fname);
  csql_invoke_system (cmd);

  unlink (fname);

  csql_display_msg (csql_get_message (CSQL_STAT_EDITOR_PRINTED_TEXT));

  return;

error:
  if (fp != NULL)
    {
      fclose (fp);
    }
  if (fname != NULL)
    {
      unlink (fname);
    }
  nonscr_display_error (csql_Scratch_text, SCRATCH_TEXT_LEN);
}

/*
 * csql_change_working_directory()
 *   return: none
 *   dirname(in)
 *
 * Note:
 *   cd to the named directory; if dirname is NULL, cd to
 *   the home directory.
 */
static void
csql_change_working_directory (const char *dirname)
{
  const char *msg;
  char buf[100 + PATH_MAX];

  msg = csql_get_message (CSQL_STAT_CD_TEXT);

  dirname = csql_get_real_path (dirname);

  if (dirname == NULL)
    {
      dirname = getenv ("HOME");
    }

  if (dirname == NULL || chdir (dirname) == -1)
    {
      csql_Error_code = CSQL_ERR_OS_ERROR;
      nonscr_display_error (csql_Scratch_text, SCRATCH_TEXT_LEN);
    }
  else
    {
      sprintf (buf, "\n%s %s.\n\n", msg, dirname);
      csql_fputs (buf, csql_Tty_fp);
    }
}

/*
 * display_error()
 *   return: none
 *   session(in)
 *   stmt_start_line_no(in)
 */
static void
display_error (DB_SESSION * session, int stmt_start_line_no)
{
  if (csql_Error_code == CSQL_ERR_SQLX_ERROR)
    {
      csql_display_session_err (session, stmt_start_line_no);
      csql_check_server_down ();
    }
  else
    {
      nonscr_display_error (csql_Scratch_text, SCRATCH_TEXT_LEN);

      /* let users read this message before the next overwrites */
      sleep (3);
    }
}

/*
 * csql_execute_statements() - execute statements
 *   return: >0 if some statement failed, zero otherwise
 *   csql_arg(in)
 *   type(in)
 *   stream(in)
 *
 * Note:
 *   If `type' is STRING_INPUT, it regards `stream' points to command string.
 *   If `type' is FILE_INPUT, it regards `stream' points FILE stream of input
 *   If `type' is EDITOR_INPUT, it attempts to get input string from command
 *   buffer.
 */
static int
csql_execute_statements (const CSQL_ARGUMENT * csql_arg, int type,
			 const void *stream)
{
  char *stmts = NULL;		/* statements string */
  int num_stmts = 0;		/* # of stmts executed */
  int stmt_start_line_no = 0;	/* starting line no of each stmt */
  DB_SESSION *session = NULL;	/* query compilation session id */
  DB_QUERY_TYPE *attr_spec = NULL;	/* result attribute spec. */
  int total;			/* number of statements to execute */

  csql_Num_failures = 0;
  er_clear ();
  db_set_interrupt (0);

  if (type == FILE_INPUT)
    {				/* FILE * input */
      if (!(session = db_open_file ((FILE *) stream)))
	{
	  csql_Error_code = CSQL_ERR_SQLX_ERROR;
	  goto error;
	}
    }
  else if (type == STRING_INPUT)
    {				/* string pointer input */
      if (!(session = db_open_buffer ((const char *) stream)))
	{
	  csql_Error_code = CSQL_ERR_SQLX_ERROR;
	  goto error;
	}
      if (csql_Is_echo_on)
	{
	  fprintf (csql_Output_fp, "%s\n", (char *) stream);
	}
    }
  else
    {				/* command buffer input */
      stmts = csql_edit_contents_get ();
      if (!(session = db_open_buffer (stmts)))
	{
	  csql_Error_code = CSQL_ERR_SQLX_ERROR;
	  goto error;
	}
      if (csql_Is_echo_on)
	{
	  fprintf (csql_Output_fp, "%s\n", stmts);
	}
    }

  /*
   * Make sure that there weren't any syntax errors; if there were, the
   * entire concept of "compile next statement" doesn't make sense, and
   * you run the risk of getting stuck in an infinite loop in the
   * following section (especially if the '-e' switch is on).
   */
  if (db_get_errors (session))
    {
      csql_Error_code = CSQL_ERR_SQLX_ERROR;
      goto error;
    }
  else
    {
      total = db_statement_count (session);
#if !defined(GNU_Readline)
#if !defined(WINDOWS)
      if ((total >= 1) && (stmts != NULL) && (csql_Is_interactive))
	{
	  add_history (stmts);
	}
#endif /* !WINDOWS */
#endif /* !GNU_Readline */

      /* It is assumed we must always enter the for loop below */
      total = MAX (total, 1);
    }

  /* execute the statements one-by-one */

  for (num_stmts = 0; num_stmts < total; num_stmts++)
    {
      struct timeval start_time, end_time;
      float elapsed_time;
      int stmt_id;
      SQLX_CMD_TYPE stmt_type;	/* statement type */
      DB_QUERY_RESULT *result = NULL;	/* result pointer */
      int db_error;

      /* Start the execution of stms */
      if (csql_Is_time_on)
	{
	  (void) gettimeofday (&start_time, NULL);
	}
      stmt_id = db_compile_statement (session);

      if (stmt_id < 0)
	{
	  /* compilation error */
	  csql_Error_code = CSQL_ERR_SQLX_ERROR;
	  /* Do not continue if there are no statments in the buffer */
	  if (csql_arg->continue_on_error &&
	      (db_error_code () != ER_IT_EMPTY_STATEMENT))
	    {
	      display_error (session, 0);
	      csql_Num_failures += 1;
	      continue;
	    }
	  else
	    {
	      goto error;
	    }
	}

      if (stmt_id == 0)		/* done */
	{
	  break;
	}

      stmt_start_line_no = db_get_start_line (session, stmt_id);
      attr_spec = db_get_query_type_list (session, stmt_id);
      stmt_type = (SQLX_CMD_TYPE) db_get_statement_type (session, stmt_id);


#if defined(CS_MODE)
      if (PRM_QUERY_MODE_SYNC)
	{
	  db_set_session_mode_sync (session);
	}
      else
	{
	  db_set_session_mode_async (session);
	}
#else /* !CS_MODE */
      db_set_session_mode_sync (session);
#endif /* CS_MODE */

      db_error = db_execute_statement (session, stmt_id, &result);

      if (db_error < 0)
	{
	  csql_Error_code = CSQL_ERR_SQLX_ERROR;
	  if (csql_arg->auto_commit && PRM_CSQL_AUTO_COMMIT &&
	      stmt_type != SQLX_CMD_ROLLBACK_WORK)
	    {
	      db_abort_transaction ();
	    }
	  if (csql_arg->continue_on_error)
	    {
	      display_error (session, stmt_start_line_no);
	      csql_Num_failures += 1;

	      free_attr_spec (&attr_spec);

	      continue;
	    }
	  goto error;
	}

      /* NOTE: the display of result is dependent on the statement type and
       * existence of result as follows;
       *
       * case INSERT:      throw the result away because it's not
       *                   helpful to users at all.
       * case CALL:        If there is no result, just skip it. If there is,
       *                   display the result.
       * case SELECT:      in any case, display it. if there is no result,
       *                   csql_results() is responsible for the notification of
       *                   empty result.
       * case GET_XACTION: display the result.
       * case GET_OPT_LVL: display the result.
       * case EVALUATE:    display the result.
       */

      csql_Row_count = 0;
      switch (stmt_type)
	{
	case SQLX_CMD_SELECT:
	  csql_results (csql_arg, result, attr_spec, stmt_start_line_no,
			stmt_type);

#if defined(CS_MODE)
	  if (PRM_QUERY_MODE_SYNC)
	    csql_Row_count = db_error;
#else /* !CS_MODE */
	  csql_Row_count = db_error;
#endif /* CS_MODE */

	  sprintf (csql_Scratch_text, csql_get_message (CSQL_ROWS),
		   csql_Row_count, "selected");

	  csql_display_msg (csql_Scratch_text);
	  break;

	case SQLX_CMD_CALL:
	case SQLX_CMD_EVALUATE:
	  if (result != NULL)
	    {
	      csql_results (csql_arg, result, db_get_query_type_ptr (result),
			    stmt_start_line_no, stmt_type);
	    }
	  break;

	case SQLX_CMD_GET_ISO_LVL:
	case SQLX_CMD_GET_TIMEOUT:
	case SQLX_CMD_GET_OPT_LVL:
	case SQLX_CMD_GET_TRIGGER:
	case SQLX_CMD_GET_LDB:
	case SQLX_CMD_GET_STATS:
	  csql_results (csql_arg, result, db_get_query_type_ptr (result),
			stmt_start_line_no, stmt_type);
	  break;

	case SQLX_CMD_UPDATE:
	  sprintf (csql_Scratch_text, csql_get_message (CSQL_ROWS), db_error,
		   "updated");
	  csql_display_msg (csql_Scratch_text);
	  break;

	case SQLX_CMD_DELETE:
	  sprintf (csql_Scratch_text, csql_get_message (CSQL_ROWS), db_error,
		   "deleted");
	  csql_display_msg (csql_Scratch_text);
	  break;

	case SQLX_CMD_INSERT:
	  sprintf (csql_Scratch_text, csql_get_message (CSQL_ROWS), db_error,
		   "inserted");
	  csql_display_msg (csql_Scratch_text);
	  break;

	default:
	  break;
	}

      free_attr_spec (&attr_spec);

      if (result != NULL)
	{
	  db_query_end (result);
	  result = NULL;
	}
      else
	{
	  /*
	   * Even though there are no results, a query may have been
	   * run implicitly by the statement.  If so, we need to end the
	   * query on the server.
	   */
	  db_free_query (session);
	}

      if (csql_Is_time_on)
	{
	  (void) gettimeofday (&end_time, NULL);
	  elapsed_time =
	    ((float) (end_time.tv_sec * 1000000 + end_time.tv_usec) -
	     (float) (start_time.tv_sec * 1000000 + start_time.tv_usec))
	    / 1000000;
	  fprintf (csql_Output_fp,
		   "SQL statement execution time: %10.6f sec\n",
		   elapsed_time);
	}

      if (csql_arg->auto_commit && PRM_CSQL_AUTO_COMMIT &&
	  stmt_type != SQLX_CMD_COMMIT_WORK &&
	  stmt_type != SQLX_CMD_ROLLBACK_WORK)
	{
	  db_error = db_commit_transaction ();
	  if (db_error < 0)
	    {
	      csql_Error_code = CSQL_ERR_SQLX_ERROR;
	      db_abort_transaction ();

	      if (csql_arg->continue_on_error)
		{
		  display_error (session, stmt_start_line_no);
		  csql_Num_failures += 1;
		  continue;
		}
	      goto error;
	    }
	  else
	    csql_display_msg (csql_get_message (CSQL_STAT_COMMITTED_TEXT));
	}
      db_drop_statement (session, stmt_id);
    }

  sprintf (csql_Scratch_text, csql_get_message (CSQL_EXECUTE_END_MSG_FORMAT),
	   num_stmts - csql_Num_failures);
  csql_display_msg (csql_Scratch_text);

  db_close_session (session);

  return csql_Num_failures;

error:

  display_error (session, stmt_start_line_no);

  /* Finish... */
  sprintf (csql_Scratch_text, csql_get_message (CSQL_EXECUTE_END_MSG_FORMAT),
	   num_stmts - csql_Num_failures);
  csql_display_msg (csql_Scratch_text);

  if (session)
    db_close_session (session);

  free_attr_spec (&attr_spec);

  return 1;
}

/*
 * free_attr_spec()
 *   return: none
 *   attr_spec(in/out)
 *
 * Note: Free memory alloced for attr_spec and set pointer to NULL
 */
static void
free_attr_spec (DB_QUERY_TYPE ** attr_spec)
{
  if (*attr_spec != NULL)
    {
      db_query_format_free (*attr_spec);
      *attr_spec = NULL;
    }
}

/*
 * csql_set_sys_param()
 *   return: none
 *   arg_str(in)
 *
 * Note: Parse the arg string to find out what system parameter to
 *       clobber, then clobber it.  Originally introduced to allow us
 *       to fiddle with optimizer parameters.
 */
static void
csql_set_sys_param (const char *arg_str)
{
  char plantype[128];
  char val[128];
  char ans[128];
  int level;

  if (arg_str == NULL)
    return;

  if (strncmp (arg_str, "cost", 4) == 0 &&
      sscanf (arg_str, "cost %s %s", plantype, val) == 2)
    {
      if (qo_plan_set_cost_fn (plantype, val[0]))
	{
	  snprintf (ans, 128, "cost %s: %s", plantype, val);
	}
      else
	{
	  snprintf (ans, 128, "error: unknown cost parameter %s", plantype);
	}
    }
  else if (strncmp (arg_str, "level", 5) == 0 &&
	   sscanf (arg_str, "level %d", &level) == 1)
    {
      qo_set_optimization_param (NULL, QO_PARAM_LEVEL, level);
      snprintf (ans, 128, "level %d", level);
    }
  else
    {
      strncpy (ans, arg_str, 127);
      if (db_set_system_parameters (ans) != NO_ERROR)
	{
	  snprintf (ans, 128, "error: set %s", arg_str);
	}
    }

  csql_append_more_line (0, ans);
  csql_display_more_lines ("Set Param Input");
  csql_free_more_lines ();
}

/*
 * csql_get_sys_param()
 *   return:
 *   arg_str(in)
 */
static void
csql_get_sys_param (const char *arg_str)
{
  char plantype[128];
  int cost;
  char ans[128];
  int level;

  if (arg_str == NULL)
    return;

  if (strncmp (arg_str, "cost", 4) == 0 &&
      sscanf (arg_str, "cost %s", plantype) == 1)
    {
      cost = qo_plan_get_cost_fn (plantype);
      if (cost == 'u')
	{
	  snprintf (ans, 128, "error: unknown cost parameter %s", arg_str);
	}
      else
	{
	  snprintf (ans, 128, "cost %s: %c", arg_str, (char) cost);
	}
    }
  else if (strncmp (arg_str, "level", 5) == 0 &&
	   sscanf (arg_str, "level") == 0)
    {
      qo_get_optimization_param (&level, QO_PARAM_LEVEL);
      snprintf (ans, 128, "level %d", level);
    }
  else
    {
      strncpy (ans, arg_str, 127);
      if (db_get_system_parameters (ans, 127) != NO_ERROR)
	{
	  snprintf (ans, 128, "error: get %s", arg_str);
	}
    }

  csql_append_more_line (0, ans);
  csql_display_more_lines ("Get Param Input");
  csql_free_more_lines ();
}

/*
 * signal_intr() - Interrupt handler for sqlx
 *   return: none
 *   sig_no(in)
 */
#if defined(WINDOWS)
static BOOL WINAPI
#else /* !WINDOWS */
static void
#endif				/* WINDOWS */
signal_intr (int sig_no)
{
  if (csql_Is_interactive)
    {
      db_set_interrupt (1);
    }

#if defined(WINDOWS)
  if (sig_no == CTRL_C_EVENT)
    return TRUE;
#endif /* WINDOWS */
}

/*
 * signal_stop()
 *   return: none
 *   sig_no(in)
 *
 * Note: Interrupt handler for ^Z. This is needed since the terminal
 *       must be changed from raw to cooked. After we return, it must
 *       be set back.
 */
static void
signal_stop (int sig_no)
{
  static int cont = 0;

#if defined(WINDOWS)
  /* there is no SIGSTP on NT */
  cont = 1;
#else /* !WINDOWS */
  if (sig_no == SIGTSTP)
    {
      cont = 0;
      (void) os_set_signal_handler (SIGTSTP, SIG_DFL);

      /* send the signal to ourselves */
      os_send_signal (SIGTSTP);

      /* Wait for SIGCONT */
      while (cont == 0)
	{
	  pause ();
	}
      (void) os_set_signal_handler (SIGTSTP, signal_stop);
    }
  else
    {
      /* Continue */
      cont = 1;
    }
#endif /* !WINDOWS */
}

/*
 * csql_exit_session() - handling the default action of the last outstanding
 *                     transaction (i.e., commit or abort)
 *   return:  none
 *   error(in)
 *
 * Note: this function never return.
 */
static void
csql_exit_session (int error)
{
  char line_buf[LINE_BUFFER_SIZE];
  bool commit_on_shutdown = false;

  if (!db_commit_is_needed ())
    {
      /* when select statements exist only in session,
         marks end of transaction to flush audit records
         for those statements */
      db_abort_transaction ();
    }

  if (csql_Is_interactive && !PRM_COMMIT_ON_SHUTDOWN
      && db_commit_is_needed () && !feof (csql_Input_fp))
    {
      FILE *tf;

      tf = csql_Error_fp;

      /* interactive, default action is abort but there was update */
      fprintf (tf, csql_get_message (CSQL_TRANS_TERMINATE_PROMPT_TEXT));
      fflush (tf);
      for (; fgets (line_buf, LINE_BUFFER_SIZE, csql_Input_fp) != NULL;)
	{
	  if (line_buf[0] == 'y' || line_buf[0] == 'Y')
	    {
	      commit_on_shutdown = true;
	      break;
	    }
	  if (line_buf[0] == 'n' || line_buf[0] == 'N')
	    {
	      commit_on_shutdown = false;
	      break;
	    }

	  fprintf (tf,
		   csql_get_message (CSQL_TRANS_TERMINATE_PROMPT_RETRY_TEXT));
	  fflush (tf);
	}

      if (commit_on_shutdown && db_commit_transaction () < 0)
	{
	  nonscr_display_error (csql_Scratch_text, SCRATCH_TEXT_LEN);
	  error = 1;
	}
    }

  if (histo_is_supported ())
    {
      if (csql_Is_histo_on)
	{
	  csql_Is_histo_on = false;
	  histo_stop ();
	}
    }

  if (db_shutdown () < 0)
    {
      csql_Database_connected = false;
      nonscr_display_error (csql_Scratch_text, SCRATCH_TEXT_LEN);
      csql_exit (EXIT_FAILURE);
    }
  else
    {
      csql_Database_connected = false;
      csql_exit (error ? EXIT_FAILURE : EXIT_SUCCESS);
    }
}

/*
 * csql_exit_init()
 *   return: none
 *
 * Note:
 *    Initialize various state variables we keep to let us know what
 *    cleanup operations need to be performed when the sqlx() function
 *    exits.  This should properly initialize everything that is tested
 *    by the csql_exit_cleanup function.
 */
static void
csql_exit_init (void)
{
  csql_Exit_status = EXIT_SUCCESS;
  csql_Database_connected = false;

  csql_Input_fp = stdin;
  csql_Output_fp = stdout;
  csql_Error_fp = stderr;

  /* Sigh, these are necessary for the catalog & args parsing
   * modules.  Would be nice to have a better encapsulation of
   * the dependencies for arg parsing.
   */
  lang_init ();			/* for catalog location */
}

/*
 * csql_exit_cleanup()
 *   return: none
 *
 * Note:
 *    Called by sqlx() when the exit longjmp has been taken.
 *    Examine the various state variables we keep and perform any
 *    termination cleanup operations that need to be performed.
 *    For the Windows implementation, it is especially important that the
 *    sqlx() function return cleanly.
 */
static void
csql_exit_cleanup ()
{
  FILE *oldout;

  if (csql_Input_fp != NULL && csql_Input_fp != stdin)
    {
      (void) fclose (csql_Input_fp);
      csql_Input_fp = NULL;
    }

  oldout = csql_Output_fp;
  if (csql_Output_fp != NULL && csql_Output_fp != stdout)
    {
      (void) fclose (csql_Output_fp);
      csql_Output_fp = NULL;
    }

  if (csql_Error_fp != NULL && csql_Error_fp != oldout &&
      csql_Error_fp != stdout && csql_Error_fp != stderr)
    {
      (void) fclose (csql_Error_fp);
      csql_Error_fp = NULL;
    }

  if (csql_Database_connected)
    {
      if (histo_is_supported ())
	{
	  if (csql_Is_histo_on)
	    {
	      csql_Is_histo_on = false;
	      histo_stop ();
	    }
	}

      csql_Database_connected = false;
      db_shutdown ();
    }

  /* Note that this closes a global resource, the "kernel" message catalog.
   * This is ok for the Unix implementation as the entire process is about
   * to exit.  For the Windows implementation, it happens to be ok since
   * the test driver application that calls sqlx() won't use this catalog.
   * If this ever changes however, we'll probably have to maintain some sort
   * of internal reference counter on this catalog so that it won't be freed
   * until all the nested users close it.
   */
  lang_final ();
}

/*
 * csql_exit()
 *   return:  none
 *   exit_status(in)
 * Note:
 *    This should be called rather than exit() any place that the code wants
 *    to terminate the sqlx interpreter program.  Rather than exit(), it
 *    will longjmp back to the sqlx() function which will clean up and
 *    return the status code to the calling function.  Usually the calling
 *    function is main() but under Windows, the caller may be a more complex
 *    application.
 */
void
csql_exit (int exit_status)
{
  csql_Exit_status = exit_status;
  longjmp (csql_Exit_env, 1);
}

/*
 * sqlx() - "main" interface function for the sqlx interpreter
 *   return: EXIT_SUCCESS, EXIT_FAILURE
 *   csql_arg(in)
 */
int
sqlx (const char *argv0, CSQL_ARGUMENT * csql_arg)
{
  const char *errlog;
  char *env;

  /* Establish a globaly accessible longjmp environment so we can terminate
   * on severe errors without calling exit(). */
  csql_exit_init ();

  if (setjmp (csql_Exit_env))
    {
      /* perform any dangling cleanup operations */
      csql_exit_cleanup ();
      return csql_Exit_status;
    }

  /* initialize message catalog for argument parsing and usage() */
  if (utility_initialize () != NO_ERROR)
    {
      csql_exit (EXIT_FAILURE);
    }

  if (LANG_VARIABLE_CHARSET (lang_charset ()))
    {
      /* set interpreter processed flag -- to avoid redundant converting
       * from two byte alphabet to ASCII alphabet
       */
      (void) sm_set_inhibit_identifier_check (1);
    }

  /* set up prompt and message fields. */
  strcpy (csql_Sqlx_prompt, csql_get_message (CSQL_LANG_PROMPT_PREFIX));
  strcpy (csql_Sqlx_name, csql_get_message (CSQL_LANG_NAME_PREFIX));
  strcpy (csql_Sqlx_product, csql_get_message (CSQL_LANG_PRODUCT_PREFIX));

  strcat (csql_Sqlx_prompt, csql_get_message (CSQL_SQLX_PROMPT_NAME));
#if defined(GNU_Readline)
  strcat (csql_Sqlx_prompt, " ");
#endif /* GNU_Readline */
  strcat (csql_Sqlx_name, csql_get_message (CSQL_SQLX_NAME));

  /* as we must use db_open_file_name() to open the input file,
   * it is necessary to be opening csql_Input_fp at this point
   */
  if (csql_arg->in_file_name != NULL)
    {
#if defined(WINDOWS)
      csql_Input_fp = fopen (csql_arg->in_file_name, "rb");
#else /* !WINDOWS */
      csql_Input_fp = fopen (csql_arg->in_file_name, "r");
#endif /* WINDOWS */
      if (csql_Input_fp == NULL)
	{
	  csql_Error_code = CSQL_ERR_OS_ERROR;
	  goto error;
	}

#if defined(WINDOWS)
      {
	char tmpchar;		/* open breaks in DLL'S */
	/*
	 * Unless an operation is done on this stream before the DLL
	 * is entered the file descriptor will be invalid.  This is a bug in
	 * MSVC compiler and/or libc.
	 */
	tmpchar = fgetc (csql_Input_fp);
	ungetc (tmpchar, csql_Input_fp);
      }
#endif /* WINDOWS */
    }

  if ((csql_arg->in_file_name == NULL) && isatty (fileno (stdin)))
    {
      csql_Is_interactive = true;
    }

  /*
   * login and restart database
   */

  if (db_login (csql_arg->user_name, csql_arg->passwd) < 0 ||
      db_restart (argv0, FALSE, csql_arg->db_name) < 0)
    {
      if (!csql_Is_interactive || csql_arg->passwd != NULL ||
	  db_error_code () != ER_AU_INVALID_PASSWORD)
	{
	  /* not INTERACTIVE mode, or password is given already, or
	   * the error code is not password related
	   */
	  csql_Error_code = CSQL_ERR_SQLX_ERROR;
	  goto error;
	}

      /* get password interactively if interactive mode */
      csql_arg->passwd =
	getpass ((char *) csql_get_message (CSQL_PASSWD_PROMPT_TEXT));
      if (csql_arg->passwd[0] == '\0')
	csql_arg->passwd = (char *) NULL;	/* to fit into db_login protocol */

      /* try again */
      if (db_login (csql_arg->user_name, csql_arg->passwd) < 0 ||
	  db_restart (argv0, FALSE, csql_arg->db_name) < 0)
	{
	  csql_Error_code = CSQL_ERR_SQLX_ERROR;
	  goto error;
	}
    }

  /* allow environmental setting of the "-s" command line flag
   * to enable automated testing */
  if (PRM_CSQL_SINGLE_LINE_MODE)
    {
      csql_arg->single_line_execution = true;
    }

  /* record the connection so we know how to clean up on exit */
  csql_Database_connected = true;

#if defined(CSQL_NO_LONGGING)
  if (csql_arg->no_logging && locator_log_force_nologging () != NO_ERROR)
    {
      csql_Error_code = CSQL_ERR_SQLX_ERROR;
      goto error;
    }
#endif /* CSQL_NO_LONGGING */

  csql_Editor_cmd[PATH_MAX - 1] = '\0';
  csql_Shell_cmd[PATH_MAX - 1] = '\0';
  csql_Print_cmd[PATH_MAX - 1] = '\0';
  csql_Pager_cmd[PATH_MAX - 1] = '\0';

  env = getenv ("EDITOR");
  if (env)
    {
      strncpy (csql_Editor_cmd, env, PATH_MAX - 1);
    }

  env = getenv ("SHELL");
  if (env)
    {
      strncpy (csql_Shell_cmd, env, PATH_MAX - 1);
    }

  if (csql_arg->nopager)
    {
      csql_Pager_cmd[0] = '\0';
    }

  errlog = er_msglog_filename ();

  if (csql_Is_interactive)
    {
      /* handling Ctrl-C */
#if defined(WINDOWS)
      SetConsoleCtrlHandler ((PHANDLER_ROUTINE) signal_intr, TRUE);
#else
      if (os_set_signal_handler (SIGINT, signal_intr) == SIG_ERR)
	{
	  csql_Error_code = CSQL_ERR_OS_ERROR;
	  goto error;
	}
#endif

#if !defined(WINDOWS)
      if (os_set_signal_handler (SIGQUIT, signal_intr) == SIG_ERR)
	{
	  csql_Error_code = CSQL_ERR_OS_ERROR;
	  goto error;
	}
#endif /* !WINDOWS */
    }

  if (er_init (errlog, ER_NEVER_EXIT) != NO_ERROR)
    {
      printf ("Failed to initialize error manager.\n");
      csql_Error_code = CSQL_ERR_OS_ERROR;;
      goto error;
    }

  start_csql (csql_arg);

  csql_exit (EXIT_SUCCESS);	/* not reachable code, actually */

error:
  nonscr_display_error (csql_Scratch_text, SCRATCH_TEXT_LEN);
  csql_exit (EXIT_FAILURE);
  return EXIT_FAILURE;		/* won't get here really */
}

/*
 * csql_get_message() - get a string of the csql-utility from the catalog
 *   return: message string
 *   message_index(in): an index of the message string
 */
const char *
csql_get_message (int message_index)
{
  return (msgcat_message (MSGCAT_CATALOG_CSQL,
			  MSGCAT_CSQL_SET_CSQL, message_index));
}
