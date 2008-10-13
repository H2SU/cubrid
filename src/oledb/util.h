#pragma once

void show_error(char *msg, int code, T_CCI_ERROR *error);

namespace Util {

// 북마크 배열에서 주어진 값을 가지는 곳의 인덱스를 구한다.
//DBROWCOUNT FindBookmark(const CAtlArray<DBROWCOUNT> &rgBookmarks, DBROWCOUNT iRowset);

// IDBProperties를 통해 DataSource 정보를 얻어낸 후
// server와 연결해서 그 handle을 *phConn에 반환한다.
HRESULT Connect(IDBProperties *pDBProps, int *phConn);

// server와 연결을 끊고 *phConn 값을 0으로 만든다.
HRESULT Disconnect(int *phConn);

// 테이블이 존재하면 S_OK, 존재하지 않으면 S_FALSE
HRESULT DoesTableExist(int hConn, char *szTableName);

// 테이블을 열고, req handle과 result count를 반환한다.
HRESULT OpenTable(int hConn, const CComBSTR &strTableName, int *phReq, int *pcResult, char flag, bool bAsynch=false, int maxrows=0);

HRESULT GetUniqueTableName(CComBSTR& strTableName);
HRESULT GetTableNames(int hConn, CAtlArray<CStringA> &rgTableNames);
HRESULT GetIndexNamesInTable(int hConn, char* table_name, CAtlArray<CStringA> &rgIndexNames, CAtlArray<int> &rgIndexTypes);

// SQL 문에서 테이블 이름을 뽑아낸다.
void ExtractTableName(const CComBSTR &strCommandText, CComBSTR &strTableName);

//요청된 인터페이스가 CCI_PREPARE_UPDATABLE를 필요로 하는지 체크한다.
bool RequestedRIIDNeedsUpdatability(REFIID riid);
//bool RequestedRIIDNeedsOID(REFIID riid);
//bool CheckOIDFromProperties(ULONG cSets, const DBPROPSET rgSets[]);
bool CheckUpdatabilityFromProperties(ULONG cSets, const DBPROPSET rgSets[]);

// IColumnsInfo를 위한 정보를 제공하는 클래스
class CColumnsInfo
{
public:
	int m_cColumns;
	ATLCOLUMNINFO *m_pInfo;
	CAtlArray<CStringA>* m_defaultVal;

	CColumnsInfo() : m_cColumns(0), m_pInfo(0), m_defaultVal(0){}
	~CColumnsInfo() { FreeColumnInfo(); }

	// m_cColumns, m_pInfo 값을 채운다.
	// 주의) 이미 값이 있는지 여부는 검사하지 않는다.
	HRESULT GetColumnInfo(T_CCI_COL_INFO* info, T_CCI_SQLX_CMD cmd_type, int cCol, bool bBookmarks=false, ULONG ulMaxLen=0);
	HRESULT GetColumnInfo(int hReq, bool bBookmarks=false, ULONG ulMaxLen=0);
	HRESULT GetColumnInfoCommon(T_CCI_COL_INFO* info, T_CCI_SQLX_CMD cmd_type, bool bBookmarks=false, ULONG ulMaxLen=0);

	// m_pInfo의 메모리를 해제하고, 모든 변수를 초기화한다.
	void FreeColumnInfo();
};

// Commit이나 Abort 됐을 때 불린다.
class ITxnCallback
{
public:
	virtual void TxnCallback(const ITxnCallback *pOwner) = 0;
};

}
