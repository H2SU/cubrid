package cubridmanager.dialog;

import org.eclipse.swt.SWT;
import org.eclipse.swt.layout.FillLayout;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Dialog;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.Text;

import cubridmanager.CommonTool;
import cubridmanager.MainRegistry;
import cubridmanager.Messages;

public class UPAPortDialog extends Dialog {
	private Shell dlgShell = null;
	private Composite sShell = null;
	private Text EDIT_UPA_PORT = null;
	private Button IDOK = null;
	private Button IDCANCEL = null;
	private Label label1 = null;
	private boolean ret = false;

	public UPAPortDialog(Shell parent) {
		super(parent);
	}

	public UPAPortDialog(Shell parent, int style) {
		super(parent, style);
	}

	public boolean doModal() {
		createSShell();
		CommonTool.centerShell(dlgShell);
		dlgShell.setDefaultButton(IDOK);
		dlgShell.open();

		Display display = dlgShell.getDisplay();
		while (!dlgShell.isDisposed()) {
			if (!display.readAndDispatch())
				display.sleep();
		}
		return ret;
	}

	private void createSShell() {
		// dlgShell = new Shell(SWT.APPLICATION_MODAL | SWT.DIALOG_TRIM);
		dlgShell = new Shell(getParent(), SWT.APPLICATION_MODAL
				| SWT.DIALOG_TRIM);
		dlgShell.setText(Messages.getString("TITLE.UPAPORT"));
		dlgShell.setLayout(new FillLayout());
		createComposite();
	}

	private void createComposite() {
		GridData gridData3 = new org.eclipse.swt.layout.GridData();
		gridData3.widthHint = 70;
		gridData3.grabExcessHorizontalSpace = true;
		GridData gridData2 = new org.eclipse.swt.layout.GridData();
		gridData2.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		gridData2.widthHint = 70;
		gridData2.grabExcessHorizontalSpace = true;
		gridData2.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		GridData gridData1 = new org.eclipse.swt.layout.GridData();
		gridData1.horizontalSpan = 2;
		gridData1.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		gridData1.widthHint = 170;
		gridData1.horizontalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		GridData gridData = new org.eclipse.swt.layout.GridData();
		gridData.horizontalSpan = 2;
		gridData.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		gridData.horizontalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		GridLayout gridLayout = new GridLayout();
		gridLayout.numColumns = 2;
		gridLayout.verticalSpacing = 10;
		gridLayout.marginWidth = 10;
		gridLayout.marginHeight = 10;
		gridLayout.horizontalSpacing = 10;
		sShell = new Composite(dlgShell, SWT.NONE);
		sShell.setLayout(gridLayout);
		label1 = new Label(sShell, SWT.CENTER | SWT.WRAP);
		label1.setText(Messages.getString("ERROR.INPUTUPAPORT"));
		label1.setLayoutData(gridData);
		EDIT_UPA_PORT = new Text(sShell, SWT.BORDER | SWT.PASSWORD);
		EDIT_UPA_PORT.setLayoutData(gridData1);
		IDOK = new Button(sShell, SWT.NONE);
		IDOK.setText(Messages.getString("BUTTON.OK"));
		IDOK.setLayoutData(gridData2);
		IDOK.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						try {
							MainRegistry.upaPort = Integer
									.parseInt(EDIT_UPA_PORT.getText());
						} catch (Exception ee) {
							CommonTool.ErrorBox(dlgShell, Messages
									.getString("ERROR.INVALIDPORTNUMBER"));
							return;
						}
						ret = true;
						dlgShell.dispose();
					}
				});
		IDCANCEL = new Button(sShell, SWT.NONE);
		IDCANCEL.setText(Messages.getString("BUTTON.CANCEL"));
		IDCANCEL.setLayoutData(gridData3);
		IDCANCEL.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						ret = false;
						dlgShell.dispose();
					}
				});
		dlgShell.pack();
	}

}
