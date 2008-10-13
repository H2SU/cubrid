package cubridmanager.query.action;

import java.sql.Connection;
import java.sql.DriverManager;
import java.sql.SQLException;

import org.eclipse.jface.action.Action;

import cubrid.jdbc.driver.CUBRIDConnection;
import cubrid.jdbc.driver.CUBRIDConnectionKey;
import cubrid.jdbc.driver.CUBRIDKeyTable;
import cubridmanager.Application;
import cubridmanager.CommonTool;
import cubridmanager.MainRegistry;
import cubridmanager.Messages;
import cubridmanager.cubrid.view.CubridView;
import cubridmanager.query.dialog.OIDNavigator;

public class OidNaviAction extends Action {
	public OidNaviAction(String text, String img) {
		super(text);
		// The id is used to refer to the action in a menu or toolbar
		setId("OidNaviAction");
		if (img != null)
			setImageDescriptor(cubridmanager.CubridmanagerPlugin
					.getImageDescriptor(img));
		setToolTipText("TOOLTIP." + text);
	}

	public void run() {
		if (CubridView.Current_db == null || CubridView.Current_db.length() < 1)
			return;

		Connection con = null;
		QueryEditorConnection connector;
		try {
			Class.forName("cubrid.jdbc.driver.CUBRIDDriver");
			connector = new QueryEditorConnection(MainRegistry
					.getDBUserInfo(CubridView.Current_db));
			con = DriverManager.getConnection(connector.getConnectionStr());
			if (MainRegistry.isProtegoBuild()) {
				CUBRIDConnectionKey conKey = ((CUBRIDConnection) con)
						.Login(MainRegistry.UserSignedData);
				CUBRIDKeyTable.putValue(conKey);
			}
			con.setTransactionIsolation(Connection.TRANSACTION_READ_UNCOMMITTED);
			((CUBRIDConnection) con).setLockTimeout(1);
			OIDNavigator oidnavi = new OIDNavigator(Application.mainwindow
					.getShell(), con);
			oidnavi.doModal();
		} catch (SQLException e) {
			CommonTool.ErrorBox(e.getErrorCode()
					+ System.getProperty("line.separator") + e.getMessage());
			CommonTool.debugPrint(e);
			return;
		} catch (ClassNotFoundException e) {
			CommonTool.ErrorBox(Messages.getString("QEDIT.SETCONNERR")
					+ System.getProperty("line.separator") + e.getMessage());
			CommonTool.debugPrint(e);
			return;
		}
	}
}
