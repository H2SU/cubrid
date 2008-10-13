/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * common.c - Definitions and data types of disk related stuffs
 *          such as pages, file structures, and so on.
 */

#ident "$Id$"

#include <stdlib.h>

#include "config.h"

#include "common.h"
#include "memory_manager_2.h"
#include "error_manager.h"
#include "system_parameter.h"
#include "environment_variable.h"
#include "file_io.h"
#include "db_date.h"

/* RESERVED_SIZE_IN_PAGE should be aligned */
#define RESERVED_SIZE_IN_PAGE   sizeof(FILEIO_PAGE_RESERVED)

#define IS_POWER_OF_2(x)        (((x) & ((x)-1)) == 0)

static PGLENGTH db_Io_page_size = IO_DEFAULT_PAGE_SIZE;
static PGLENGTH db_User_page_size =
  IO_DEFAULT_PAGE_SIZE - RESERVED_SIZE_IN_PAGE;
static int db_Max_path_len = PATH_MAX;

static PGLENGTH find_valid_page_size (PGLENGTH page_size);

/*
 * db_page_size(): returns the user page size
 *
 *   returns: user page size
 */
PGLENGTH
db_page_size (void)
{
  return db_User_page_size;
}

/*
 * db_io_page_size(): returns the IO page size
 *
 *   returns: IO page size
 */
PGLENGTH
db_io_page_size (void)
{
  return db_Io_page_size;
}

/*
 * db_max_path_len(): returns the max path length
 *
 *   returns: max path length
 */
int
db_max_path_len (void)
{
  return db_Max_path_len;
}

/*
 * db_set_page_size(): set the page size of system.
 *
 *   returns: io_page_size
 *   io_page_size(IN): the IO pagesize
 *
 * Note: Set the database pagesize to the given size. The given size
 *       must be power of 2, greater than or equal to 1K, and smaller
 *       than or equal to 16K.
 */
PGLENGTH
db_set_page_size (PGLENGTH io_page_size)
{
  PGLENGTH power2_io_page_size;

  power2_io_page_size = io_page_size;
  if (power2_io_page_size == -1)
    {
      return db_Io_page_size;
    }

  power2_io_page_size = find_valid_page_size (power2_io_page_size);

  db_Io_page_size = power2_io_page_size;
  db_User_page_size = db_Io_page_size - RESERVED_SIZE_IN_PAGE;

  return db_Io_page_size;
}

/*
 * db_network_page_size(): find the network pagesize
 *
 *   returns: network pagesize
 *
 * Note: Find the best network pagesize for C/S communications for
 *       given transaction/client.
 */
PGLENGTH
db_network_page_size (void)
{
  return db_Io_page_size;
}

/*
 * find_valid_page_size(): find the valid page size of system
 *
 *   returns: page_size
 *   page_size(IN): the page size
 *
 * Note: Find the database pagesize with the given size, where the given size
 *       must be power of 2, greater than or equal to 1K, and smaller than or
 *       equal to 16K.
 */
static PGLENGTH
find_valid_page_size (PGLENGTH page_size)
{
  PGLENGTH power2_page_size = page_size;

  if (power2_page_size < IO_MIN_PAGE_SIZE)
    {
      power2_page_size = IO_MIN_PAGE_SIZE;
    }
  else if (power2_page_size > IO_MAX_PAGE_SIZE)
    {
      power2_page_size = IO_MAX_PAGE_SIZE;
    }
  else
    {
      if (!IS_POWER_OF_2 (power2_page_size))
	{
	  /*
	   * Not a power of 2 or page size is too small
	   *
	   * Round the number to a power of two. Find smaller number that it is
	   * a power of two, and then shift to get larger number.
	   */
	  while (!IS_POWER_OF_2 (power2_page_size))
	    {
	      if (power2_page_size < IO_MIN_PAGE_SIZE)
		{
		  power2_page_size = IO_MIN_PAGE_SIZE;
		  break;
		}
	      else
		{
		  /* Turn off some bits but the left most one */
		  power2_page_size =
		    power2_page_size & (power2_page_size - 1);
		}
	    }

	  power2_page_size <<= 1;

	  if (power2_page_size < IO_MIN_PAGE_SIZE)
	    {
	      power2_page_size = IO_MIN_PAGE_SIZE;
	    }
	  else if (power2_page_size > IO_MAX_PAGE_SIZE)
	    {
	      power2_page_size = IO_MAX_PAGE_SIZE;
	    }

	  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_DTSR_BAD_PAGESIZE, 2,
		  page_size, power2_page_size);
	}
    }

  return power2_page_size;
}

void
db_print_data (DB_TYPE type, DB_DATA * data, FILE * fd)
{
  int hour, minute, second, month, day, year;

  switch (type)
    {
    case DB_TYPE_SHORT:
      fprintf (fd, "%d", data->sh);
      break;

    case DB_TYPE_INTEGER:
      fprintf (fd, "%d", data->i);
      break;

    case DB_TYPE_FLOAT:
      fprintf (fd, "%f", data->f);
      break;

    case DB_TYPE_DOUBLE:
      fprintf (fd, "%f", data->d);
      break;

    case DB_TYPE_DATE:
      db_date_decode (&data->date, &month, &day, &year);
      fprintf (fd, "%d / %d / %d", month, day, year);
      break;

    case DB_TYPE_TIME:
      db_time_decode (&data->time, &hour, &minute, &second);
      fprintf (fd, "%d:%d:%d with zone: %d", hour, minute, second, 0);
      break;

    case DB_TYPE_UTIME:
      fprintf (fd, "%d", data->utime);
      break;

    case DB_TYPE_MONETARY:
      fprintf (fd, "%f", data->money.amount);
      switch (data->money.type)
	{
	case DB_CURRENCY_DOLLAR:
	  fprintf (fd, " dollars");
	  break;
	case DB_CURRENCY_POUND:
	  fprintf (fd, " pounds");
	  break;
	case DB_CURRENCY_YEN:
	  fprintf (fd, " yens");
	  break;
	case DB_CURRENCY_WON:
	  fprintf (fd, " wons");
	  break;
	default:
	  break;
	}
      break;

    default:
      fprintf (fd, "Undefined");
      break;
    }
}

int
recdes_allocate_data_area (RECDES * rec, int size)
{
  char *data;

  data = (char *) db_private_alloc (NULL, size);
  if (data == NULL)
    {
      return ER_FAILED;
    }

  rec->data = data;
  rec->area_size = size;

  return NO_ERROR;
}

void
recdes_free_data_area (RECDES * rec)
{
  db_private_free_and_init (NULL, rec->data);
}

void
recdes_set_data_area (RECDES * rec, char *data, int size)
{
  rec->data = data;
  rec->area_size = size;
}
