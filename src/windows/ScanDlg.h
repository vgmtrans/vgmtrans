#if !defined(AFX_SCANDLG_H__BFE5B011_C81B_4F2E_B032_576C7117FDC4__INCLUDED_)
#define AFX_SCANDLG_H__BFE5B011_C81B_4F2E_B032_576C7117FDC4__INCLUDED_

typedef struct _ScanInfo
{
	int nNumScanners;
	int nCurScanner;
	wchar_t* sScannerName;	
} ScanInfo;

class CScanDlg : public CDialogImpl<CScanDlg>
{
public:
	enum { IDD = IDD_SCANDLG };

	BEGIN_MSG_MAP(CScanDlg)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		COMMAND_ID_HANDLER(IDOK, OnCloseCmd)
		COMMAND_ID_HANDLER(IDCANCEL, OnCloseCmd)
	END_MSG_MAP()

// Handler prototypes (uncomment arguments if needed):
//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

	LRESULT OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnCloseCmd(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
};

#endif // !defined(AFX_ABOUTDLG_H__BFE5B011_C81B_4F2E_B032_576C7117FDC4__INCLUDED_)
