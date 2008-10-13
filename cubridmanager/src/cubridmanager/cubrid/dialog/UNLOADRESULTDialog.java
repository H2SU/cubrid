package cubridmanager.cubrid.dialog;

import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.Dialog;
import org.eclipse.swt.SWT;

import cubridmanager.CommonTool;
import cubridmanager.MainRegistry;
import cubridmanager.Messages;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.widgets.Table;
import org.eclipse.jface.viewers.TableLayout;
import org.eclipse.jface.viewers.ColumnWeightData;
import org.eclipse.swt.widgets.TableColumn;
import org.eclipse.swt.widgets.TableItem;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.FillLayout;

public class UNLOADRESULTDialog extends Dialog {
	private Shell dlgShell = null;
	private Composite sShell = null;
	private Button IDOK = null;
	private Table LIST_UNLOADRESULT = null;

	public UNLOADRESULTDialog(Shell parent) {
		super(parent);
	}

	public UNLOADRESULTDialog(Shell parent, int style) {
		super(parent, style);
	}

	public int doModal() {
		createSShell();
		CommonTool.centerShell(dlgShell);
		dlgShell.setDefaultButton(IDOK);
		dlgShell.open();

		Display display = dlgShell.getDisplay();
		while (!dlgShell.isDisposed()) {
			if (!display.readAndDispatch())
				display.sleep();
		}
		return 0;
	}

	private void createSShell() {
		// dlgShell = new Shell(SWT.APPLICATION_MODAL | SWT.DIALOG_TRIM);
		dlgShell = new Shell(getParent(), SWT.APPLICATION_MODAL
				| SWT.DIALOG_TRIM);
		dlgShell.setText(Messages.getString("TITLE.UNLOADRESULTDIALOG"));
		dlgShell.setLayout(new FillLayout());
		createComposite();
	}

	private void createComposite() {
		GridData gridData1 = new org.eclipse.swt.layout.GridData();
		gridData1.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		gridData1.widthHint = 100;
		gridData1.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		sShell = new Composite(dlgShell, SWT.NONE);
		sShell.setLayout(new GridLayout());
		createTable1();
		IDOK = new Button(sShell, SWT.NONE);
		IDOK.setText(Messages.getString("BUTTON.OK"));
		IDOK.setLayoutData(gridData1);
		IDOK
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						dlgShell.dispose();
					}
				});
		dlgShell.pack();
	}

	private void createTable1() {
		GridData gridData = new org.eclipse.swt.layout.GridData();
		gridData.heightHint = 290;
		gridData.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData.widthHint = 340;
		LIST_UNLOADRESULT = new Table(sShell, SWT.FULL_SELECTION | SWT.BORDER);
		LIST_UNLOADRESULT.setLinesVisible(true);
		LIST_UNLOADRESULT.setLayoutData(gridData);
		LIST_UNLOADRESULT.setHeaderVisible(true);
		TableLayout tlayout = new TableLayout();
		tlayout.addColumnData(new ColumnWeightData(50, 30, true));
		tlayout.addColumnData(new ColumnWeightData(50, 30, true));
		LIST_UNLOADRESULT.setLayout(tlayout);

		TableColumn tblcol = new TableColumn(LIST_UNLOADRESULT, SWT.LEFT);
		tblcol.setText(Messages.getString("TABLE.CLASS"));
		tblcol = new TableColumn(LIST_UNLOADRESULT, SWT.LEFT);
		tblcol.setText(Messages.getString("TABLE.STATUS"));

		for (int ti = 0, tn = MainRegistry.Tmpchkrst.size(); ti < tn; ti += 2) {
			TableItem item = new TableItem(LIST_UNLOADRESULT, SWT.NONE);
			item.setText(0, (String) MainRegistry.Tmpchkrst.get(ti));
			item.setText(1, (String) MainRegistry.Tmpchkrst.get(ti + 1));
		}
		for (int i = 0, n = LIST_UNLOADRESULT.getColumnCount(); i < n; i++) {
			LIST_UNLOADRESULT.getColumn(i).pack();
		}
	}

}
