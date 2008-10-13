/**
 * Title:        CUBRID Java Client Interface<p>
 * Description:  CUBRID Java Client Interface<p>
 * @version 2.0
 */

package cubrid.jdbc.jci;

import java.math.BigDecimal;
import java.sql.Date;
import java.sql.Time;
import java.sql.Timestamp;
import java.sql.Array;
import java.sql.ResultSet;
import java.util.Map;

import cubrid.sql.CUBRIDOID;

/*
 * CUBRID의 Set Type을 Manage하기 위한 class이다. interface java.sql.Array를
 * implement하여 Set type data들을 array의 element처럼 사용할 수 있게 하였다.
 * CUBRIDArray에서 지원하는 type은 CUBRID type에 match되는 java type이다.
 * 지원되는 java type
 * byte, short, int, float, double, BigDecimal, Date, Time, Timestamp, String
 *
 * since 1.0
 */

class CUBRIDArray {

/*=======================================================================
 |	PRIVATE VARIABLES
 =======================================================================*/

private byte baseType;
private int length;
private Object internalArray[];

/*=======================================================================
 |	CONSTRUCTOR
 =======================================================================*/

CUBRIDArray(byte type, int arrayLength)
{
  baseType = type;
  length = arrayLength;
  if (length < 0)
    return;

  switch (type) {
    case UUType.U_TYPE_BIT :
    case UUType.U_TYPE_VARBIT :
      internalArray = (Object []) (new byte[length][]);
      break;
    case UUType.U_TYPE_SHORT :
      internalArray = (Object []) (new Short[length]);
      break;
    case UUType.U_TYPE_INT :
      internalArray = (Object []) (new Integer[length]);
      break;
    case UUType.U_TYPE_FLOAT :
      internalArray = (Object []) (new Float[length]);
      break;
    case UUType.U_TYPE_DOUBLE :
    case UUType.U_TYPE_MONETARY :
      internalArray = (Object []) (new Double[length]);
      break;
    case UUType.U_TYPE_NUMERIC :
      internalArray = (Object []) (new BigDecimal[length]);
      break;
    case UUType.U_TYPE_DATE :
      internalArray = (Object []) (new Date[length]);
      break;
    case UUType.U_TYPE_TIME :
      internalArray = (Object []) (new Time[length]);
      break;
    case UUType.U_TYPE_TIMESTAMP :
      internalArray = (Object []) (new Timestamp[length]);
      break;
    case UUType.U_TYPE_CHAR :
    case UUType.U_TYPE_NCHAR :
    case UUType.U_TYPE_STRING :
    case UUType.U_TYPE_VARNCHAR :
      internalArray = (Object []) (new String[length]);
      break;
    case UUType.U_TYPE_OBJECT :
      internalArray = (Object []) (new CUBRIDOID[length]);
      break;
    default:
      baseType = UUType.U_TYPE_NULL;
      internalArray = new Object[length];
      break;
  }
}

CUBRIDArray(Object values) throws UJciException
{
  if ((values instanceof Object[]) && (((Object[]) values).length == 0))
    baseType = UUType.U_TYPE_OBJECT;
  else
    baseType = UUType.getObjArrBaseDBtype(values);
  if (baseType == UUType.U_TYPE_NULL)
    throw new UJciException(UErrorCode.ER_INVALID_ARGUMENT);
  internalArray = (Object[]) ((Object[]) values).clone();
  length = ((Object[]) values).length;
}

/*=======================================================================
 |	PACKAGE ACCESS METHODS
 =======================================================================*/

/*
 * CUBRIDArray에 저장되어 있는 CUBRID Set type data들을 array로 return해 준다.
 */

Object getArray()
{
  return internalArray;
}

Object getArrayClone()
{
  if (internalArray == null)
    return null;

  Object[] obj = (Object[])  internalArray.clone();

  if (obj instanceof Date[]) {
    for (int i = 0 ; i < obj.length ; i++)
      if (obj[i] != null)
	obj[i] = ((Date) obj[i]).clone();
  }
  else if (obj instanceof Time[]) {
    for (int i = 0 ; i < obj.length ; i++)
      if (obj[i] != null)
	obj[i] = ((Time) obj[i]).clone();
  }
  else if (obj instanceof Timestamp[]) {
    for (int i = 0 ; i < obj.length ; i++)
      if (obj[i] != null)
	obj[i] = ((Timestamp) obj[i]).clone();
  }
  else if (obj instanceof byte[][]) {
    for (int i = 0 ; i < obj.length ; i++)
      if (obj[i] != null)
	obj[i] = ((byte[]) obj[i]).clone();
  }

  return obj;
}

/*
 * 현재 CUBRIDArray object에 포함된 data의 CUBRID Type을 return한다. return값은
 * class UUType의 member이다.
 */

int getBaseType()
{
  return (int) baseType;
}

/*
 * 현재 CUBRIDArray object에 포함된 data의 CUBRID Type의 name을 return한다.
String getBaseTypeName()
{
  switch (baseType) {
    case UUType.U_TYPE_BIT :
      return "Bit";
    case UUType.U_TYPE_VARBIT :
      return "Various Bit";
    case UUType.U_TYPE_SHORT :
      return "Small Integer";
    case UUType.U_TYPE_INT :
      return "Integer";
    case UUType.U_TYPE_FLOAT :
      return "Float";
    case UUType.U_TYPE_DOUBLE :
      return "Double";
    case UUType.U_TYPE_MONETARY :
      return "Monetary";
    case UUType.U_TYPE_NUMERIC :
      return "Numeric";
    case UUType.U_TYPE_DATE :
      return "Date";
    case UUType.U_TYPE_TIME :
      return "Time";
    case UUType.U_TYPE_TIMESTAMP :
      return "Timestamp";
    case UUType.U_TYPE_CHAR :
    case UUType.U_TYPE_NCHAR :
    case UUType.U_TYPE_STRING :
    case UUType.U_TYPE_VARNCHAR :
      return "Various Char";
  }
  return null;
}
*/

/*
 * CUBRIDArray에 포함되어 있는 set type data의 length를 return한다.
 */

int getLength()
{
  return length;
}

synchronized void setElement(int index, Object data)
{
  internalArray[index] = data;
}

}  // end of class CUBRIDArray
