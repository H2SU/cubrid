/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * keyword.c - SQL keyword table
 *
 */

#ident "$Id$"

#include "config.h"

#include <stdlib.h>
#include <string.h>

#include "parser.h"
#include "intl.h"
#include "dbtype.h"
#include "qp_str.h"

#define ZZ_PREFIX gr_
#include "zzpref.h"
#define INSIDE_SCAN_DOT_C
#include "sqtokens.h"

/* Do not care alphabetical order of keywords. automatically sorted! */
static KEYWORD_RECORD keywords[] = {
  {ABORT, "ABORT", 1},
  {ABS, "ABS", 1},
  {ABSOLUTE, "ABSOLUTE", 0},
  {ACTION, "ACTION", 0},
  {ACTIVE, "ACTIVE", 1},
  {ADD, "ADD", 0},
  {ADD_MONTHS, "ADD_MONTHS", 0},
  {AFTER, "AFTER", 0},
  {ALIAS, "ALIAS", 0},
  {ALL, "ALL", 0},
  {ALLOCATE, "ALLOCATE", 0},
  {ALTER, "ALTER", 0},
  {ANALYZE, "ANALYZE", 1},
  {AND, "AND", 0},
  {ANY, "ANY", 0},
  {ARE, "ARE", 0},
  {AS, "AS", 0},
  {ASC, "ASC", 0},
  {ASSERTION, "ASSERTION", 0},
  {ASYNC, "ASYNC", 0},
  {AT, "AT", 0},
  {ATTACH, "ATTACH", 0},
  {ATTRIBUTE, "ATTRIBUTE", 0},
  {AUTHORIZATION, "AUTHORIZATION", 1},	/* contradicts ANSI 92 */
  {AUTO_INCREMENT, "AUTO_INCREMENT", 1},
  {AVG, "AVG", 0},
  {BEFORE, "BEFORE", 0},
  {BEGIN, "BEGIN", 0},
  {BETWEEN, "BETWEEN", 0},
  {BIT, "BIT", 0},
  {BIT_LENGTH, "BIT_LENGTH", 0},
  {BOOLEAN, "BOOLEAN", 0},
  {BOTH, "BOTH", 0},
  {BREADTH, "BREADTH", 0},
  {BY, "BY", 0},
  {CALL, "CALL", 0},
  {CACHE, "CACHE", 1},
  {CASCADE, "CASCADE", 0},
  {CASCADED, "CASCADED", 0},
  {CASE, "CASE", 0},
  {CAST, "CAST", 0},
  {CATALOG, "CATALOG", 0},
  {CEIL, "CEIL", 1},
  {CHANGE, "CHANGE", 0},
  {Char, "CHAR", 0},
  {CHARACTER, "CHARACTER", 0},
  {CHARACTER_LENGTH, "CHARACTER_LENGTH", 0},
  {CHAR_LENGTH, "CHAR_LENGTH", 0},
  {CHECK, "CHECK", 0},
  {CHR, "CHR", 1},
  {CLASS, "CLASS", 0},
  {CLASSES, "CLASSES", 0},
  {CLOSE, "CLOSE", 0},
  {CLUSTER, "CLUSTER", 0},
  {COALESCE, "COALESCE", 0},
  {COLLATE, "COLLATE", 0},
  {COLLATION, "COLLATION", 0},
  {COLUMN, "COLUMN", 0},
  {COMMIT, "COMMIT", 0},
  {COMMITTED, "COMMITTED", 1},
  {COMPLETION, "COMPLETION", 0},
  {CONNECT, "CONNECT", 0},
  {CONNECTION, "CONNECTION", 0},
  {CONSTRAINT, "CONSTRAINT", 0},
  {CONSTRAINTS, "CONSTRAINTS", 0},
  {CONTINUE, "CONTINUE", 0},
  {CONVERT, "CONVERT", 0},
  {CORRESPONDING, "CORRESPONDING", 0},
  {COST, "COST", 1},
  {COUNT, "COUNT", 0},
  {CREATE, "CREATE", 0},
  {CROSS, "CROSS", 0},
  {CURRENT, "CURRENT", 0},
  {CURRENT_DATE, "CURRENT_DATE", 0},
  {CURRENT_TIME, "CURRENT_TIME", 0},
  {CURRENT_TIMESTAMP, "CURRENT_TIMESTAMP", 0},
  {CURRENT_USER, "CURRENT_USER", 0},
  {CURSOR, "CURSOR", 0},
  {CYCLE, "CYCLE", 0},
  {DATA, "DATA", 0},
  {DATA_TYPE, "DATA_TYPE___", 0},
  {Date, "DATE", 0},
  {DAY, "DAY", 0},
  {DEALLOCATE, "DEALLOCATE", 0},
  {NUMERIC, "DEC", 0},
  {DECAY_CONSTANT, "DECAY_CONSTANT", 1},
  {NUMERIC, "DECIMAL", 0},
  {DECLARE, "DECLARE", 0},
  {DECR, "DECR", 1},
  {DECREMENT, "DECREMENT", 1},
  {DECODE_, "DECODE", 1},
  {DEFAULT, "DEFAULT", 0},
  {DEFERRABLE, "DEFERRABLE", 0},
  {DEFERRED, "DEFERRED", 0},
  {DEFINED, "DEFINED", 1},
  {DELETE, "DELETE", 0},
  {DEPTH, "DEPTH", 0},
  {DESC, "DESC", 0},
  {DESCRIBE, "DESCRIBE", 0},
  {DESCRIPTOR, "DESCRIPTOR", 0},
  {DIAGNOSTICS, "DIAGNOSTICS", 0},
  {DICTIONARY, "DICTIONARY", 0},
  {DIFFERENCE, "DIFFERENCE", 0},
  {DIRECTORY, "DIRECTORY", 1},
  {DISCONNECT, "DISCONNECT", 0},
  {DISTINCT, "DISTINCT", 0},
  {Domain, "DOMAIN", 0},
  {Double, "DOUBLE", 0},
  {DRAND, "DRAND", 1},
  {DRANDOM, "DRANDOM", 1},
  {DROP, "DROP", 0},
  {EACH, "EACH", 0},
  {ELSE, "ELSE", 0},
  {ELSEIF, "ELSEIF", 0},
  {END, "END", 0},
  {EQUALS, "EQUALS", 0},
  {ESCAPE, "ESCAPE", 0},
  {EVALUATE, "EVALUATE", 0},
  {EVENT, "EVENT", 1},
  {EXCEPT, "EXCEPT", 0},
  {EXCEPTION, "EXCEPTION", 0},
  {EXCLUDE, "EXCLUDE", 0},
  {EXEC, "EXEC", 0},
  {EXECUTE, "EXECUTE", 0},
  {EXISTS, "EXISTS", 0},
  {EXP, "EXP", 1},
  {EXTERNAL, "EXTERNAL", 0},
  {EXTRACT, "EXTRACT", 0},
  {False, "FALSE", 0},
  {FETCH, "FETCH", 0},
  {File, "FILE", 0},
  {FIRST, "FIRST", 0},
  {Float, "FLOAT", 0},
  {FLOOR, "FLOOR", 1},
  {For, "FOR", 0},
  {FOREIGN, "FOREIGN", 0},
  {FOUND, "FOUND", 0},
  {FROM, "FROM", 0},
  {FULL, "FULL", 0},
  {FUNCTION, "FUNCTION", 0},
  {GDB, "GDB", 1},
  {GENERAL, "GENERAL", 0},
  {GET, "GET", 0},
  {GE_INF, "GE_INF", 1},
  {GE_LE, "GE_LE", 1},
  {GE_LT, "GE_LT", 1},
  {GLOBAL, "GLOBAL", 0},
  {GO, "GO", 0},
  {GOTO, "GOTO", 0},
  {GRANT, "GRANT", 0},
  {GREATEST, "GREATEST", 1},
  {GROUP, "GROUP", 0},
  {GROUPBY_NUM, "GROUPBY_NUM", 1},
  {GROUPS, "GROUPS", 1},
  {GT_INF, "GT_INF", 1},
  {GT_LE, "GT_LE", 1},
  {GT_LT, "GT_LT", 1},
  {HASH, "HASH", 1},
  {HAVING, "HAVING", 0},
  {HOST, "HOST", 1},
  {HOUR, "HOUR", 0},
  {IDENTIFIED, "IDENTIFIED", 1},
  {IDENTITY, "IDENTITY", 0},
  {IF, "IF", 0},
  {IGNORE, "IGNORE", 0},
  {IMMEDIATE, "IMMEDIATE", 0},
  {IN, "IN", 0},
  {INACTIVE, "INACTIVE", 1},
  {INCR, "INCR", 1},
  {INCREMENT, "INCREMENT", 1},
  {INDEX, "INDEX", 0},
  {INDICATOR, "INDICATOR", 0},
  {INFINITE, "INFINITE", 1},
  {INFO, "INFO", 1},
  {INF_LE, "INF_LE", 1},
  {INF_LT, "INF_LT", 1},
  {INHERIT, "INHERIT", 0},
  {INITIALLY, "INITIALLY", 0},
  {INNER, "INNER", 0},
  {INOUT, "INOUT", 0},
  {INPUT, "INPUT", 0},
  {INSENSITIVE, "INSENSITIVE", 1},
  {INSERT, "INSERT", 0},
  {INSTANCES, "INSTANCES", 1},
  {INSTR, "INSTR", 1},
  {INSTRB, "INSTRB", 1},
  {INST_NUM, "INST_NUM", 1},
  {Int, "INT", 0},
  {Integer, "INTEGER", 0},
  {INTERSECT, "INTERSECT", 0},
  {INTERSECTION, "INTERSECTION", 0},
  {INTERVAL, "INTERVAL", 0},
  {INTO, "INTO", 0},
  {INTRINSIC, "INTRINSIC", 1},
  {INVALIDATE, "INVALIDATE", 1},
  {IS, "IS", 0},
  {ISOLATION, "ISOLATION", 0},
  {JAVA, "JAVA", 1},
  {JOIN, "JOIN", 0},
  {KEY, "KEY", 0},
  {LANGUAGE, "LANGUAGE", 0},
  {LAST, "LAST", 0},
  {LAST_DAY, "LAST_DAY", 1},
  {LDB, "LDB", 0},
  {LEADING, "LEADING", 0},
  {LEAST, "LEAST", 1},
  {LEAVE, "LEAVE", 0},
  {LEFT, "LEFT", 0},
  {LENGTH, "LENGTH", 1},
  {LENGTHB, "LENGTHB", 1},
  {LESS, "LESS", 0},
  {LEVEL, "LEVEL", 0},
  {LIKE, "LIKE", 0},
  {LIMIT, "LIMIT", 0},
  {LIST, "LIST", 0},
  {LOCAL, "LOCAL", 0},
  {LOCAL_TRANSACTION_ID, "LOCAL_TRANSACTION_ID", 0},
  {LOCK, "LOCK", 1},
  {LOG, "LOG", 1},
  {LOOP, "LOOP", 0},
  {LOWER, "LOWER", 0},
  {LPAD, "LPAD", 1},
  {LTRIM, "LTRIM", 1},
  {MATCH, "MATCH", 0},
  {Max, "MAX", 0},
  {MAXIMUM, "MAXIMUM", 1},
  {MAXVALUE, "MAXVALUE", 1},
  {MAX_ACTIVE, "MAX_ACTIVE", 1},
  {MEMBERS, "MEMBERS", 1},
  {METHOD, "METHOD", 0},
  {Min, "MIN", 0},
  {MINUTE, "MINUTE", 0},
  {MINVALUE, "MINVALUE", 1},
  {MIN_ACTIVE, "MIN_ACTIVE", 1},
  {MODIFY, "MODIFY", 0},
  {MODULE, "MODULE", 0},
  {MODULUS, "MOD", 1},
  {Monetary, "MONETARY", 0},
  {MONTH, "MONTH", 0},
  {MONTHS_BETWEEN, "MONTHS_BETWEEN", 1},
  {MULTISET, "MULTISET", 0},
  {MULTISET_OF, "MULTISET_OF", 0},
  {NA, "NA", 0},
  {NAME, "NAME", 1},
  {NAMES, "NAMES", 0},
  {NATIONAL, "NATIONAL", 0},
  {NATURAL, "NATURAL", 0},
  {NCHAR, "NCHAR", 0},
  {NEW, "NEW", 1},
  {NEXT, "NEXT", 0},
  {NO, "NO", 0},
  {NOCYCLE, "NOCYCLE", 1},
  {NOMAXVALUE, "NOMAXVALUE", 1},
  {NOMINVALUE, "NOMINVALUE", 1},
  {NONE, "NONE", 0},
  {NOT, "NOT", 0},
  {Null, "NULL", 0},
  {NULLIF, "NULLIF", 0},
  {NUMERIC, "NUMERIC", 0},
  {NVL, "NVL", 1},
  {NVL2, "NVL2", 1},
  {OBJECT, "OBJECT", 0},
  {OBJECT_ID, "OBJECT_ID", 1},
  {OCTET_LENGTH, "OCTET_LENGTH", 0},
  {OF, "OF", 0},
  {OFF_, "OFF", 0},
  {OID, "OID", 0},
  {OLD, "OLD", 0},
  {ON_, "ON", 0},
  {ONLY, "ONLY", 0},
  {OPEN, "OPEN", 0},
  {OPERATION, "OPERATION", 0},
  {OPERATORS, "OPERATORS", 0},
  {OPTIMIZATION, "OPTIMIZATION", 0},
  {OPTION, "OPTION", 0},
  {OR, "OR", 0},
  {ORDER, "ORDER", 0},
  {ORDERBY_NUM, "ORDERBY_NUM", 1},
  {OTHERS, "OTHERS", 0},
  {OUT_, "OUT", 0},
  {OUTER, "OUTER", 0},
  {OUTPUT, "OUTPUT", 0},
  {OVERLAPS, "OVERLAPS", 0},
  {PARAMETERS, "PARAMETERS", 0},
  {PARTIAL, "PARTIAL", 0},
  {PARTITION, "PARTITION", 1},
  {PARTITIONING, "PARTITIONING", 1},
  {PARTITIONS, "PARTITIONS", 1},
  {PASSWORD, "PASSWORD", 1},
  {PENDANT, "PENDANT", 0},
  {POSITION, "POSITION", 0},
  {POWER, "POWER", 1},
  {PRECISION, "PRECISION", 0},
  {PREORDER, "PREORDER", 0},
  {PREPARE, "PREPARE", 0},
  {PRESERVE, "PRESERVE", 0},
  {PRIMARY, "PRIMARY", 0},
  {PRINT, "PRINT", 1},
  {PRIOR, "PRIOR", 0},
  {PRIORITY, "PRIORITY", 1},
  {Private, "PRIVATE", 0},
  {PRIVILEGES, "PRIVILEGES", 0},
  {PROXY, "PROXY", 0},
  {PROCEDURE, "PROCEDURE", 0},
  {PROTECTED, "PROTECTED", 0},
  {QUERY, "QUERY", 0},
  {RAND, "RAND", 1},
  {RANDOM, "RANDOM", 1},
  {RANGE, "RANGE", 1},
  {READ, "READ", 0},
  {Real, "REAL", 0},
  {REBUILD, "REBUILD", 1},
  {RECURSIVE, "RECURSIVE", 0},
  {REF, "REF", 0},
  {REFERENCES, "REFERENCES", 0},
  {REFERENCING, "REFERENCING", 0},
  {REGISTER, "REGISTER", 0},
  {REJECT, "REJECT", 1},
  {RELATIVE, "RELATIVE", 0},
  {REMOVE, "REMOVE", 1},
  {RENAME, "RENAME", 0},
  {REORGANIZE, "REORGANIZE", 1},
  {REPEATABLE, "REPEATABLE", 1},
  {REPLACE, "REPLACE", 0},
  {RESET, "RESET", 1},
  {RESIGNAL, "RESIGNAL", 0},
  {RESTRICT, "RESTRICT", 0},
  {RETAIN, "RETAIN", 1},
  {RETURN, "RETURN", 0},
  {RETURNS, "RETURNS", 0},
  {REVOKE, "REVOKE", 0},
  {REVERSE, "REVERSE", 1},
  {RIGHT, "RIGHT", 0},
  {ROLE, "ROLE", 0},
  {ROLLBACK, "ROLLBACK", 0},
  {ROUND, "ROUND", 1},
  {ROUTINE, "ROUTINE", 0},
  {ROW, "ROW", 0},
  {ROWNUM, "ROWNUM", 0},
  {ROWS, "ROWS", 0},
  {RPAD, "RPAD", 1},
  {RTRIM, "RTRIM", 1},
  {SAVEPOINT, "SAVEPOINT", 0},
  {SCHEMA, "SCHEMA", 0},
  {SCOPE, "SCOPE___", 0},
  {SCROLL, "SCROLL", 0},
  {SEARCH, "SEARCH", 0},
  {SECOND, "SECOND", 0},
  {SECTION, "SECTION", 0},
  {SELECT, "SELECT", 0},
  {SENSITIVE, "SENSITIVE", 0},
  {SEQUENCE, "SEQUENCE", 0},
  {SEQUENCE_OF, "SEQUENCE_OF", 0},
  {SERIAL, "SERIAL", 1},
  {SERIALIZABLE, "SERIALIZABLE", 0},
  {SESSION, "SESSION", 0},
  {SESSION_USER, "SESSION_USER", 0},
  {SET, "SET", 0},
  {SETEQ, "SETEQ", 0},
  {SETNEQ, "SETNEQ", 0},
  {SET_OF, "SET_OF", 0},
  {SHARED, "SHARED", 0},
  {SmallInt, "SHORT", 0},
  {SIGN, "SIGN", 1},
  {SIGNAL, "SIGNAL", 0},
  {SIMILAR, "SIMILAR", 0},
  {SIZE, "SIZE", 0},
  {SmallInt, "SMALLINT", 0},
  {SOME, "SOME", 0},
  {SQL, "SQL", 0},
  {SQLCODE, "SQLCODE", 0},
  {SQLERROR, "SQLERROR", 0},
  {SQLEXCEPTION, "SQLEXCEPTION", 0},
  {SQLSTATE, "SQLSTATE", 0},
  {SQLWARNING, "SQLWARNING", 0},
  {SQRT, "SQRT", 1},
  {STABILITY, "STABILITY", 1},
  {START_, "START", 1},
  {STATEMENT, "STATEMENT", 1},
  {STATISTICS, "STATISTICS", 0},
  {STATUS, "STATUS", 1},
  {STDDEV, "STDDEV", 1},
  {STOP, "STOP", 1},
  {String, "STRING", 0},
  {STRUCTURE, "STRUCTURE", 0},
  {SUBCLASS, "SUBCLASS", 0},
  {SUBSET, "SUBSET", 0},
  {SUBSETEQ, "SUBSETEQ", 0},
  {SUBSTR, "SUBSTR", 1},
  {SUBSTRB, "SUBSTRB", 1},
  {SUBSTRING, "SUBSTRING", 0},
  {SUM, "SUM", 0},
  {SUPERCLASS, "SUPERCLASS", 0},
  {SUPERSET, "SUPERSET", 0},
  {SUPERSETEQ, "SUPERSETEQ", 0},
  {SWITCH, "SWITCH", 1},
  {SYSTEM, "SYSTEM", 1},
  {SYSTEM_USER, "SYSTEM_USER", 0},
  {SYS_DATE, "SYS_DATE", 0},
  {SYS_TIME_, "SYS_TIME", 0},
  {SYS_TIMESTAMP, "SYS_TIMESTAMP", 0},
  {SYS_DATE, "SYSDATE", 0},
  {SYS_TIME_, "SYSTIME", 0},
  {SYS_TIMESTAMP, "SYSTIMESTAMP", 0},
  {SYS_USER, "SYS_USER", 0},
  {TABLE, "TABLE", 0},
  {TEMPORARY, "TEMPORARY", 0},
  {TEST, "TEST", 0},
#if 0				/* disable TEXT */
  {TEXT, "TEXT", 1},
#endif /* 0 */
  {THAN, "THAN", 1},
  {THEN, "THEN", 0},
  {THERE, "THERE", 0},
  {Time, "TIME", 0},
  {TIMEOUT, "TIMEOUT", 1},
  {TIMESTAMP, "TIMESTAMP", 0},
  {TIMEZONE_HOUR, "TIMEZONE_HOUR", 0},
  {TIMEZONE_MINUTE, "TIMEZONE_MINUTE", 0},
  {TO, "TO", 0},
  {TO_CHAR, "TO_CHAR", 1},
  {TO_DATE, "TO_DATE", 1},
  {TO_NUMBER, "TO_NUMBER", 1},
  {TO_TIME, "TO_TIME", 1},
  {TO_TIMESTAMP, "TO_TIMESTAMP", 1},
  {TRACE, "TRACE", 1},
  {TRAILING, "TRAILING", 0},
  {TRANSACTION, "TRANSACTION", 0},
  {TRANSLATE, "TRANSLATE", 0},
  {TRANSLATION, "TRANSLATION", 0},
  {TRIGGER, "TRIGGER", 0},
  {TRIGGERS, "TRIGGERS", 1},
  {TRIM, "TRIM", 0},
  {True, "TRUE", 0},
  {TRUNC, "TRUNC", 1},
  {TYPE, "TYPE", 0},
  {UNCOMMITTED, "UNCOMMITTED", 1},
  {UNDER, "UNDER", 0},
  {Union, "UNION", 0},
  {UNIQUE, "UNIQUE", 0},
  {UNKNOWN, "UNKNOWN", 0},
  {UPDATE, "UPDATE", 0},
  {UPPER, "UPPER", 0},
  {USAGE, "USAGE", 0},
  {USE, "USE", 0},
  {USER, "USER", 0},
  {USING, "USING", 0},
  {Utime, "UTIME", 0},
  {VALUE, "VALUE", 0},
  {VALUES, "VALUES", 0},
  {VARCHAR, "VARCHAR", 0},
  {VARIABLE, "VARIABLE", 0},
  {VARIANCE, "VARIANCE", 1},
  {VARYING, "VARYING", 0},
  {VCLASS, "VCLASS", 0},
  {VIEW, "VIEW", 0},
  {VIRTUAL, "VIRTUAL", 0},
  {VISIBLE, "VISIBLE", 0},
  {WAIT, "WAIT", 0},
  {WHEN, "WHEN", 0},
  {WHENEVER, "WHENEVER", 0},
  {WHERE, "WHERE", 0},
  {WHILE, "WHILE", 0},
  {WITH, "WITH", 0},
  {WITHOUT, "WITHOUT", 0},
  {WORK, "WORK", 0},
  {WORKSPACE, "WORKSPACE", 1},
  {WRITE, "WRITE", 0},
  {YEAR, "YEAR", 0},
  {ZONE, "ZONE", 0},
};

static KEYWORD_RECORD *pt_find_keyword (const char *text);
static int keyword_cmp (const void *k1, const void *k2);


static int
keyword_cmp (const void *k1, const void *k2)
{
  return strcmp (((KEYWORD_RECORD *) k1)->keyword,
		 ((KEYWORD_RECORD *) k2)->keyword);
}

/*
 * pt_find_keyword () -
 *   return: keyword record corresponding to keyword text
 *   text(in): text to test
 */
static KEYWORD_RECORD *
pt_find_keyword (const char *text)
{
  static bool keyword_sorted = false;
  KEYWORD_RECORD *result_key;
  KEYWORD_RECORD dummy;

  if (keyword_sorted == false)
    {
      qsort (keywords,
	     (sizeof (keywords) / sizeof (keywords[0])),
	     sizeof (keywords[0]), keyword_cmp);
      keyword_sorted = true;
    }

  if (!text)
    {
      return NULL;
    }

  if (strlen (text) >= MAX_KEYWORD_SIZE)
    {
      return NULL;
    }

  intl_mbs_upper (text, dummy.keyword);

  result_key = (KEYWORD_RECORD *) bsearch
    (&dummy, keywords,
     (sizeof (keywords) / sizeof (keywords[0])),
     sizeof (KEYWORD_RECORD), keyword_cmp);

  return result_key;
}


/*
 * pt_identifier_or_keyword () -
 *   return: token number of corresponding keyword
 *   text(in): text to test
 */
int
pt_identifier_or_keyword (const char *text)
{
  KEYWORD_RECORD *keyword_rec;

  keyword_rec = pt_find_keyword (text);

  if (keyword_rec)
    {
      return keyword_rec->value;
    }
  else
    {
      return IdName;
    }
}


/*
 * pt_is_reserved_word () -
 *   return: true if string is a keyword
 *   text(in): text to test
 */
bool
pt_is_reserved_word (const char *text)
{
  KEYWORD_RECORD *keyword_rec;

  keyword_rec = pt_find_keyword (text);

  if (!keyword_rec)
    {
      return false;
    }
  else if (keyword_rec->unreserved)
    {
      return false;
    }
  else
    {
      return true;
    }
}


/*
 * pt_is_keyword () -
 *   return:
 *   text(in):
 */
bool
pt_is_keyword (const char *text)
{
  KEYWORD_RECORD *keyword_rec;

  keyword_rec = pt_find_keyword (text);

  if (!keyword_rec)
    {
      return false;
    }
  else if (keyword_rec->value == NEW || keyword_rec->value == OLD)
    {
      return false;
    }
  else
    {
      return true;
    }
}


/*
 * pt_get_keyword_rec () -
 *   return: KEYWORD_RECORD
 *   rec_count(out): keywords record count
 */
KEYWORD_RECORD *
pt_get_keyword_rec (int *rec_count)
{
  *(rec_count) = sizeof (keywords) / sizeof (keywords[0]);

  return (KEYWORD_RECORD *) (keywords);
}
