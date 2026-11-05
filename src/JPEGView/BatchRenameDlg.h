#pragma once

#include "resource.h"
#include "FileList.h"
#include <vector>
#include <map>

// Dialog to rename the image files of the current folder with a name pattern.
// Ported from the famomatic fork of JPEGView (https://github.com/famomatic/jpegview), GPL-2.0-or-later.
// Tokens in the pattern:
//   {name}  file name without extension
//   {ext}   extension (with the dot); without {ext} the extension is kept at the end
//   {num}   sequence number of the checked files, zero padded
//   {date}  EXIF date (yyyyMMdd) of JPEGs, otherwise the modification date
//   {time}  EXIF time (HHmmss) of JPEGs, otherwise the modification time
class CBatchRenameDlg : public CDialogImpl<CBatchRenameDlg>
{
public:
	enum { IDD = IDD_BATCH_RENAME };

	BEGIN_MSG_MAP(CBatchRenameDlg)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		MESSAGE_HANDLER(WM_CLOSE, OnClose)
		COMMAND_ID_HANDLER(IDC_BR_CANCEL, OnCancel)
		COMMAND_ID_HANDLER(IDCANCEL, OnCancel)
		COMMAND_ID_HANDLER(IDC_BR_RENAME, OnRename)
		COMMAND_HANDLER(IDC_BR_PATTERN, EN_CHANGE, OnPatternChanged)
		COMMAND_HANDLER(IDC_BR_STARTNUM, EN_CHANGE, OnPatternChanged)
		NOTIFY_HANDLER(IDC_BR_LIST, LVN_ITEMCHANGED, OnItemChanged)
	END_MSG_MAP()

	LRESULT OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnClose(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnCancel(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnRename(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnPatternChanged(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnItemChanged(int /*idCtrl*/, LPNMHDR pnmh, BOOL& /*bHandled*/);

	CBatchRenameDlg(CFileList& fileList);

	// After a successful rename (DoModal() returns IDOK): the new name (with path) of a renamed file, the file itself if not renamed
	CString GetNewFileName(LPCTSTR sOldFileName) const;

private:
	struct CFileEntry {
		CString FileName; // with path
		CString Title;
		CString Date, Time; // EXIF or modification date and time
	};

	CFileList& m_fileList;
	std::vector<CFileEntry> m_files;
	std::map<CString, CString> m_renamed; // old name -> new name, both with path, lower case keys

	CListViewCtrl m_lvFiles;
	CEdit m_edtPattern;
	CEdit m_edtStartNum;
	CStatic m_lblPreview;
	CButton m_btnRename;
	CButton m_btnCancel;

	CString m_sPattern;
	int m_nStartNum;
	bool m_bPopulating;
	bool m_bRenaming;

	void PopulateList();
	CString ExpandPattern(const CFileEntry& file, int nSequence, int nNumberWidth) const;
	int GetNumberWidth() const;
	void UpdatePreview();
};
