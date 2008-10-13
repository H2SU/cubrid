package cubrid.jdbc.driver;

import java.sql.*;

import cubrid.jdbc.driver.CUBRIDException;
import cubrid.jdbc.driver.CUBRIDJDBCErrorCode;
import cubrid.jdbc.jci.*;


/**
 * Title:        CUBRID JDBC Driver
 * Description:
 * @version 2.0
 */

public class CUBRIDResultSetMetaData implements ResultSetMetaData {

/*=======================================================================
 |	PRIVATE VARIABLES 
 =======================================================================*/

private String[] col_name;
private int[] col_type;
private int[] ele_type;
private String[] col_type_name;
private String[] ele_type_name;
private int[] col_prec;
private int[] col_scale;
private String[] col_table;
private int[] col_null;
private String[] col_class_name;

/*=======================================================================
 |	CONSTRUCTOR
 =======================================================================*/

protected CUBRIDResultSetMetaData(UColumnInfo[] col_info)
{
  col_name = new String[col_info.length];
  col_type = new int[col_info.length];
  ele_type = new int[col_info.length];
  col_type_name = new String[col_info.length];
  ele_type_name = new String[col_info.length];
  col_prec = new int[col_info.length];
  col_scale = new int[col_info.length];
  col_table = new String[col_info.length];
  col_null = new int[col_info.length];
  col_class_name = new String[col_info.length];

  for (int i=0 ; i<col_info.length ; i++) {
    col_name[i] = col_info[i].getColumnName();
    col_prec[i] = col_info[i].getColumnPrecision();
    col_scale[i] = col_info[i].getColumnScale();
    col_table[i] = col_info[i].getClassName();
    col_type_name[i] = null;
    col_class_name[i] = col_info[i].getFQDN();
    if (col_info[i].isNullable()) col_null[i] = columnNullable;
    else col_null[i] = columnNoNulls;

    switch (col_info[i].getColumnType()) {
    case UUType.U_TYPE_CHAR :
	  col_type_name[i] = "CHAR";
	  col_type[i] = java.sql.Types.CHAR;
	  ele_type[i] = -1;
	  break;

    case UUType.U_TYPE_VARCHAR :
	  col_type_name[i] = "VARCHAR";
	  col_type[i] = java.sql.Types.VARCHAR;
	  ele_type[i] = -1;
	  break;

    case UUType.U_TYPE_BIT :
	  if (col_prec[i] == 8) {
	    col_type_name[i] = "BIT";
	    col_type[i] = java.sql.Types.BIT;
	    ele_type[i] = -1;
	  }
	  else {
	    col_type_name[i] = "BIT";
	    col_type[i] = java.sql.Types.BINARY;
	    ele_type[i] = -1;
	  }
	  break;

    case UUType.U_TYPE_VARBIT :
	  col_type_name[i] = "BIT VARYING";
	  col_type[i] = java.sql.Types.VARBINARY;
	  ele_type[i] = -1;
	  break;

    case UUType.U_TYPE_SHORT :
	  col_type_name[i] = "SMALLINT";
	  col_type[i] = java.sql.Types.SMALLINT;
	  ele_type[i] = -1;
	  break;

    case UUType.U_TYPE_INT :
	  col_type_name[i] = "INTEGER";
	  col_type[i] = java.sql.Types.INTEGER;
	  ele_type[i] = -1;
	  break;

    case UUType.U_TYPE_FLOAT :
	  col_type_name[i] = "FLOAT";
	  col_type[i] = java.sql.Types.REAL;
	  ele_type[i] = -1;
	  break;

    case UUType.U_TYPE_DOUBLE :
	  col_type_name[i] = "DOUBLE";
	  col_type[i] = java.sql.Types.DOUBLE;
	  ele_type[i] = -1;
	  break;

    case UUType.U_TYPE_NUMERIC :
	  col_type_name[i] = "NUMERIC";
	  col_type[i] = java.sql.Types.NUMERIC;
	  ele_type[i] = -1;
	  break;

    case UUType.U_TYPE_MONETARY :
	  col_type_name[i] = "MONETARY";
	  col_type[i] = java.sql.Types.DOUBLE;
	  ele_type[i] = -1;
	  break;

    case UUType.U_TYPE_DATE :
	  col_type_name[i] = "DATE";
	  col_type[i] = java.sql.Types.DATE;
	  ele_type[i] = -1;
	  break;

    case UUType.U_TYPE_TIME :
	  col_type_name[i] = "TIME";
	  col_type[i] = java.sql.Types.TIME;
	  ele_type[i] = -1;
	  break;

    case UUType.U_TYPE_TIMESTAMP :
	  col_type_name[i] = "TIMESTAMP";
	  col_type[i] = java.sql.Types.TIMESTAMP;
	  ele_type[i] = -1;
	  break;

    case UUType.U_TYPE_NULL :
	  col_type_name[i] = "";
	  //col_type[i] = java.sql.Types.NULL;
	  col_type[i] = java.sql.Types.OTHER;
	  ele_type[i] = -1;
	  break;

    case UUType.U_TYPE_OBJECT :
	  col_type_name[i] = "CLASS";
	  col_type[i] = java.sql.Types.OTHER;
	  ele_type[i] = -1;
	  break;

    case UUType.U_TYPE_SET :
	  col_type_name[i] = "SET";

    case UUType.U_TYPE_MULTISET :
	  if (col_type_name[i] == null) {
	    col_type_name[i] = "MULTISET";
	  }

    case UUType.U_TYPE_SEQUENCE :
	  if (col_type_name[i] == null) {
	    col_type_name[i] = "SEQUENCE";
	  }
	  col_type[i] = java.sql.Types.OTHER;

	  switch (col_info[i].getCollectionBaseType()) {
	  case UUType.U_TYPE_CHAR :
		ele_type[i] = java.sql.Types.CHAR;
		ele_type_name[i] = "CHAR";
		break;
	  case UUType.U_TYPE_VARCHAR :
		ele_type[i] = java.sql.Types.VARCHAR;
		ele_type_name[i] = "VARCHAR";
		break;
	  case UUType.U_TYPE_BIT :
		if (col_info[i].getColumnPrecision() == 8) {
		  ele_type[i] = java.sql.Types.BIT;
		  ele_type_name[i] = "BIT";
		}
		else {
		  ele_type[i] = java.sql.Types.BINARY;
		  ele_type_name[i] = "BIT";
		}
		break;
	  case UUType.U_TYPE_VARBIT :
		ele_type[i] = java.sql.Types.VARBINARY;
		ele_type_name[i] = "BIT VARYING";
		break;
	  case UUType.U_TYPE_SHORT :
		ele_type[i] = java.sql.Types.SMALLINT;
		ele_type_name[i] = "SMALLINT";
		break;
	  case UUType.U_TYPE_INT :
		ele_type[i] = java.sql.Types.INTEGER;
		ele_type_name[i] = "INTEGER";
		break;
	  case UUType.U_TYPE_FLOAT :
		ele_type[i] = java.sql.Types.REAL;
		ele_type_name[i] = "FLOAT";
		break;
	  case UUType.U_TYPE_DOUBLE :
		ele_type[i] = java.sql.Types.DOUBLE;
		ele_type_name[i] = "DOUBLE";
		break;
	  case UUType.U_TYPE_NUMERIC :
		ele_type[i] = java.sql.Types.NUMERIC;
		ele_type_name[i] = "NUMERIC";
		break;
	  case UUType.U_TYPE_MONETARY :
		ele_type[i] = java.sql.Types.DOUBLE;
		ele_type_name[i] = "MONETARY";
		break;
	  case UUType.U_TYPE_DATE :
		ele_type[i] = java.sql.Types.DATE;
		ele_type_name[i] = "DATE";
		break;
	  case UUType.U_TYPE_TIME :
		ele_type[i] = java.sql.Types.TIME;
		ele_type_name[i] = "TIME";
		break;
	  case UUType.U_TYPE_TIMESTAMP :
		ele_type[i] = java.sql.Types.TIMESTAMP;
		ele_type_name[i] = "TIMESTAMP";
		break;
	  case UUType.U_TYPE_NULL :
		ele_type[i] = java.sql.Types.NULL;
		ele_type_name[i] = "";
		break;
	  case UUType.U_TYPE_OBJECT :
		ele_type[i] = java.sql.Types.OTHER;
		ele_type_name[i] = "CLASS";
		break;
	  case UUType.U_TYPE_SET :
		ele_type[i] = java.sql.Types.OTHER;
		ele_type_name[i] = "SET";
		break;
	  case UUType.U_TYPE_MULTISET :
		ele_type[i] = java.sql.Types.OTHER;
		ele_type_name[i] = "MULTISET";
		break;
	  case UUType.U_TYPE_SEQUENCE :
		ele_type[i] = java.sql.Types.OTHER;
		ele_type_name[i] = "SEQUENCE";
		break;
	  case UUType.U_TYPE_NCHAR :
		ele_type[i] = java.sql.Types.CHAR;
		ele_type_name[i] = "NCHAR";
		break;
	  case UUType.U_TYPE_VARNCHAR :
		ele_type[i] = java.sql.Types.VARCHAR;
		ele_type_name[i] = "NCHAR VARYING";
		break;
	  default :
		break;
	  }

	  break;

    case UUType.U_TYPE_NCHAR :
	  col_type_name[i] = "NCHAR";
	  col_type[i] = java.sql.Types.CHAR;
	  ele_type[i] = -1;
	  break;

    case UUType.U_TYPE_VARNCHAR :
	  col_type_name[i] = "NCHAR VARYING";
	  col_type[i] = java.sql.Types.VARCHAR;
	  ele_type[i] = -1;
	  break;

    default :
	  break;
    }
  }
}

CUBRIDResultSetMetaData(CUBRIDResultSetWithoutQuery r)
{
  col_name = r.column_name;
  col_type = new int[col_name.length];
  ele_type = new int[col_name.length];
  col_type_name = new String[col_name.length];
  col_prec = new int[col_name.length];
  col_scale = new int[col_name.length];
  col_table = new String[col_name.length];
  col_null = new int[col_name.length];
  col_class_name = new String[col_name.length];

  for (int i=0 ; i<col_name.length ; i++) {
    if (r.type[i] == UUType.U_TYPE_BIT) {
      col_type[i] = java.sql.Types.BIT;
      col_type_name[i] = "BIT";
      col_prec[i] = 1;
      col_class_name[i] = "byte[]";
    }
    if (r.type[i] == UUType.U_TYPE_INT) {
      col_type[i] = java.sql.Types.INTEGER;
      col_type_name[i] = "INTEGER";
      col_prec[i] = 10;
      col_class_name[i] = "java.lang.Integer";
    }
    if (r.type[i] == UUType.U_TYPE_SHORT) {
      col_type[i] = java.sql.Types.SMALLINT;
      col_type_name[i] = "SMALLINT";
      col_prec[i] = 5;
      col_class_name[i] = "java.lang.Short";
    }
    if (r.type[i] == UUType.U_TYPE_VARCHAR) {
      col_type[i] = java.sql.Types.VARCHAR;
      col_type_name[i] = "VARCHAR";
      col_prec[i] = 0;
      col_class_name[i] = "java.lang.String";
    }
    if (r.type[i] == UUType.U_TYPE_NULL) {
      col_type[i] = java.sql.Types.NULL;
      col_type_name[i] = "";
      col_prec[i] = 0;
      col_class_name[i] = "";
    }
    col_scale[i] = 0;
    ele_type[i] = -1;
    col_table[i] = "";
    if (r.nullable[i]) {
      col_null[i] = columnNullable;
    }
    else {
      col_null[i] = columnNoNulls;
    }
  }
}

/*=======================================================================
 |	java.sql.ResultSetMetaData interface
 =======================================================================*/

public int getColumnCount() throws SQLException
{
  return col_name.length;
}

public boolean isAutoIncrement(int column) throws SQLException
{
  checkColumnIndex(column);
  return false;
}

public boolean isCaseSensitive(int column) throws SQLException
{
  checkColumnIndex(column);

  if (col_type[column-1] == java.sql.Types.CHAR ||
      col_type[column-1] == java.sql.Types.VARCHAR ||
      col_type[column-1] == java.sql.Types.LONGVARCHAR)
  {
    return true;
  }

  return false;
}

public boolean isSearchable(int column) throws SQLException
{
  checkColumnIndex(column);
  return true;
}

public boolean isCurrency(int column) throws SQLException
{
  checkColumnIndex(column);

  if (col_type[column-1] == java.sql.Types.DOUBLE ||
      col_type[column-1] == java.sql.Types.REAL ||
      col_type[column-1] == java.sql.Types.NUMERIC)
  {
    return true;
  }

  return false;
}

public int isNullable(int column) throws SQLException
{
  checkColumnIndex(column);
  return col_null[column-1];
}

public boolean isSigned(int column) throws SQLException
{
  checkColumnIndex(column);

  if (col_type[column-1] == java.sql.Types.SMALLINT ||
      col_type[column-1] == java.sql.Types.INTEGER ||
      col_type[column-1] == java.sql.Types.NUMERIC ||
      col_type[column-1] == java.sql.Types.REAL ||
      col_type[column-1] == java.sql.Types.DOUBLE)
  {
    return true;
  }

  return false;
}

public int getColumnDisplaySize(int column) throws SQLException
{
  checkColumnIndex(column);
  return getPrecision(column);
}

public String getColumnLabel(int column) throws SQLException
{
  checkColumnIndex(column);
  return getColumnName(column);
}

public String getColumnName(int column) throws SQLException
{
  checkColumnIndex(column);
  return col_name[column-1];
}

public String getSchemaName(int column) throws SQLException
{
  checkColumnIndex(column);
  return "";
}

public int getPrecision(int column) throws SQLException
{
  checkColumnIndex(column);
  return col_prec[column-1];
}

public int getScale(int column) throws SQLException
{
  checkColumnIndex(column);
  return col_scale[column-1];
}

public String getTableName(int column) throws SQLException
{
  checkColumnIndex(column);
  return col_table[column-1];
}

public String getCatalogName(int column) throws SQLException
{
  checkColumnIndex(column);
  return "";
}

public int getColumnType(int column) throws SQLException
{
  checkColumnIndex(column);
  return col_type[column-1];
}

public String getColumnTypeName(int column) throws SQLException
{
  checkColumnIndex(column);
  return col_type_name[column-1];
}

public boolean isReadOnly(int column) throws SQLException
{
  checkColumnIndex(column);
  return false;
}

public boolean isWritable(int column) throws SQLException
{
  checkColumnIndex(column);
  return true;
}

public boolean isDefinitelyWritable(int column) throws SQLException
{
  checkColumnIndex(column);
  return false;
}

public String getColumnClassName(int column) throws SQLException
{
  checkColumnIndex(column);
  return col_class_name[column-1];
}

/*=======================================================================
 |	PUBLIC METHODS
 =======================================================================*/

public int getElementType(int column) throws SQLException
{
  checkColumnIndex(column);

  String type = getColumnTypeName(column);
  if (!type.equals("SET") && !type.equals("MULTISET") && !type.equals("SEQUENCE"))
  {
    throw new CUBRIDException(CUBRIDJDBCErrorCode.not_collection);
  }

  return ele_type[column-1];
}

public String getElementTypeName(int column) throws SQLException
{
  checkColumnIndex(column);

  String type = getColumnTypeName(column);
  if (!type.equals("SET") && !type.equals("MULTISET") && !type.equals("SEQUENCE"))
  {
    throw new CUBRIDException(CUBRIDJDBCErrorCode.not_collection);
  }

  return ele_type_name[column-1];
}

/*=======================================================================
 |	PRIVATE METHODS
 =======================================================================*/

private void checkColumnIndex(int column) throws SQLException
{
  if (column < 1 || column > col_name.length) {
    throw new CUBRIDException(CUBRIDJDBCErrorCode.invalid_index);
  }
}

}  // end of class CUBRIDResultSetMetaData
