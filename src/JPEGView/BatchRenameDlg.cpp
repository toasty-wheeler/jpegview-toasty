// Batch rename dialog, ported from the famomatic fork of JPEGView (https://github.com/famomatic/jpegview), GPL-2.0-or-later.
// Changes for this fork: the new name of every file is shown in the list and updated while typing, the start number
// and the check boxes update the preview, EXIF dates are read once when the dialog opens, texts are localizable.
#include "stdafx.h"
#include "BatchRenameDlg.h"
#include "Helpers.h"
#include "EXIFReader.h"
#include "NLS.h"
#include <set>

namespace {
	struct CStringNoCaseLess {
		bool operator()(const CString& left, const CString& right) const { return left.CompareNoCase(right) < 0; }
	};

	bool IsValidFileName(const CString& name, CString& reason) {
		if (name.IsEmpty() || name == _T(".") || name == _T("..")) {
			reason = CNLS::GetString(_T("The new file name is empty or reserved."));
			return false;
		}
		if (name.GetLength() > 255 || name[name.GetLength() - 1] == _T('.') || name[name.GetLength() - 1] == _T(' ')) {
			reason = CNLS::GetString(_T("File names cannot be longer than 255 characters or end with a dot or space."));
			return false;
		}
		for (int i = 0; i < name.GetLength(); i++) {
			const TCHAR ch = name[i];
			if (ch < 32 || _tcschr(_T("<>:\"/\\|?*"), ch) != NULL) {
				reason = CNLS::GetString(_T("The new file name contains a character that is not allowed in file names."));
				return false;
			}
		}
		CString base = name;
		const int dot = base.Find(_T('.'));
		if (dot >= 0) base = base.Left(dot);
		base.MakeUpper();
		if (base == _T("CON") || base == _T("PRN") || base == _T("AUX") || base == _T("NUL") ||
			(base.GetLength() == 4 && (base.Left(3) == _T("COM") || base.Left(3) == _T("LPT")) &&
				base[3] >= _T('1') && base[3] <= _T('9'))) {
			reason = CNLS::GetString(_T("The new file name is a reserved Windows device name."));
			return false;
		}
		return true;
	}

	CString JoinPath(const CString& directory, const CString& name) {
		CString path = directory;
		if (!path.IsEmpty() && path[path.GetLength() - 1] != _T('\\') && path[path.GetLength() - 1] != _T('/'))
			path += _T("\\");
		path += name;
		return path;
	}

	// EXIF date and time of a JPEG, false if it has none. Only the start of the file is read, the EXIF block is there.
	bool ReadEXIFDateTime(LPCTSTR sFileName, CString& sDate, CString& sTime) {
		CString sLower(sFileName);
		sLower.MakeLower();
		if (sLower.Right(4) != _T(".jpg") && sLower.Right(5) != _T(".jpeg")) {
			return false;
		}
		bool bFound = false;
		HANDLE hFile = ::CreateFile(sFileName, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
		if (hFile != INVALID_HANDLE_VALUE) {
			DWORD nSize = ::GetFileSize(hFile, NULL);
			DWORD nToRead = (nSize < 256 * 1024) ? nSize : (256 * 1024);
			uint8* pBuffer = new(std::nothrow) uint8[nToRead > 0 ? nToRead : 1];
			DWORD nRead = 0;
			if (pBuffer != NULL && nToRead > 0 && ::ReadFile(hFile, pBuffer, nToRead, &nRead, NULL) && nRead > 0) {
				void* pEXIFBlock = Helpers::FindEXIFBlock(pBuffer, (int)nRead);
				if (pEXIFBlock != NULL) {
					CEXIFReader reader(pEXIFBlock, IF_JPEG);
					if (reader.GetAcquisitionTimePresent()) {
						const SYSTEMTIME& st = reader.GetAcquisitionTime();
						sDate.Format(_T("%04d%02d%02d"), st.wYear, st.wMonth, st.wDay);
						sTime.Format(_T("%02d%02d%02d"), st.wHour, st.wMinute, st.wSecond);
						bFound = true;
					}
				}
			}
			delete[] pBuffer;
			::CloseHandle(hFile);
		}
		return bFound;
	}
}

CBatchRenameDlg::CBatchRenameDlg(CFileList& fileList)
	: m_fileList(fileList)
	, m_nStartNum(1)
	, m_bPopulating(false)
	, m_bRenaming(false)
{
	m_sPattern = _T("{name}_{num}");
}

CString CBatchRenameDlg::GetNewFileName(LPCTSTR sOldFileName) const {
	CString sKey(sOldFileName);
	sKey.MakeLower();
	std::map<CString, CString>::const_iterator iter = m_renamed.find(sKey);
	return (iter == m_renamed.end()) ? CString(sOldFileName) : iter->second;
}

LRESULT CBatchRenameDlg::OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	CenterWindow(GetParent());

	m_lvFiles.Attach(GetDlgItem(IDC_BR_LIST));
	m_edtPattern.Attach(GetDlgItem(IDC_BR_PATTERN));
	m_edtStartNum.Attach(GetDlgItem(IDC_BR_STARTNUM));
	m_lblPreview.Attach(GetDlgItem(IDC_BR_PREVIEW));
	m_btnRename.Attach(GetDlgItem(IDC_BR_RENAME));
	m_btnCancel.Attach(GetDlgItem(IDC_BR_CANCEL));

	SetWindowText(CNLS::GetString(_T("Batch rename")));
	GetDlgItem(IDC_BR_PATTERN_LBL).SetWindowText(CNLS::GetString(_T("Pattern:")));
	GetDlgItem(IDC_BR_STARTNUM_LBL).SetWindowText(CNLS::GetString(_T("Start #")));
	GetDlgItem(IDC_BR_TOKENS).SetWindowText(CNLS::GetString(_T("{name} file name, {ext} extension (kept at the end if not used), {num} number, {date} EXIF or file date (yyyyMMdd), {time} EXIF or file time (HHmmss)")));
	m_btnRename.SetWindowText(CNLS::GetString(_T("Rename")));
	m_btnCancel.SetWindowText(CNLS::GetString(_T("Cancel")));

	m_lvFiles.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_CHECKBOXES);
	CRect listRect;
	m_lvFiles.GetClientRect(&listRect);
	int nColumnWidth = (listRect.Width() - ::GetSystemMetrics(SM_CXVSCROLL)) / 2;
	m_lvFiles.InsertColumn(0, CNLS::GetString(_T("File name")), LVCFMT_LEFT, nColumnWidth);
	m_lvFiles.InsertColumn(1, CNLS::GetString(_T("New name")), LVCFMT_LEFT, nColumnWidth);

	m_bPopulating = true;
	m_edtPattern.SetWindowText(m_sPattern);
	m_edtStartNum.SetWindowText(_T("1"));
	PopulateList();
	m_bPopulating = false;
	UpdatePreview();

	return TRUE;
}

LRESULT CBatchRenameDlg::OnClose(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	if (!m_bRenaming) EndDialog(IDCANCEL);
	return 0;
}

LRESULT CBatchRenameDlg::OnCancel(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	if (!m_bRenaming) EndDialog(IDCANCEL);
	return 0;
}

void CBatchRenameDlg::PopulateList() {
	m_lvFiles.DeleteAllItems();
	m_files.clear();
	std::list<CFileDesc>& files = m_fileList.GetFileList();
	for (std::list<CFileDesc>::iterator it = files.begin(); it != files.end(); ++it) {
		CFileEntry entry;
		entry.FileName = it->GetName();
		entry.Title = it->GetTitle();
		if (!ReadEXIFDateTime(entry.FileName, entry.Date, entry.Time)) {
			// modification time of the file
			FILETIME ftLocal;
			SYSTEMTIME st;
			::FileTimeToLocalFileTime(&it->GetLastModTime(), &ftLocal);
			::FileTimeToSystemTime(&ftLocal, &st);
			entry.Date.Format(_T("%04d%02d%02d"), st.wYear, st.wMonth, st.wDay);
			entry.Time.Format(_T("%02d%02d%02d"), st.wHour, st.wMinute, st.wSecond);
		}
		int nItem = m_lvFiles.InsertItem((int)m_files.size(), entry.Title);
		m_lvFiles.SetCheckState(nItem, TRUE);
		m_files.push_back(entry);
	}
}

int CBatchRenameDlg::GetNumberWidth() const {
	// at least 3 digits, more if the highest number needs them
	int nHighest = m_nStartNum + (int)m_files.size() - 1;
	int nWidth = 1;
	while (nHighest >= 10) { nWidth++; nHighest /= 10; }
	return max(3, nWidth);
}

CString CBatchRenameDlg::ExpandPattern(const CFileEntry& file, int nSequence, int nNumberWidth) const {
	CString sName = file.Title, sExt;
	int nDot = sName.ReverseFind(_T('.'));
	if (nDot > 0) {
		sExt = sName.Mid(nDot); // with the dot
		sName = sName.Left(nDot);
	}
	CString sNum;
	sNum.Format(_T("%0*d"), nNumberWidth, m_nStartNum + nSequence);

	CString sResult = m_sPattern;
	bool bHasExtToken = sResult.Find(_T("{ext}")) >= 0;
	sResult.Replace(_T("{name}"), sName);
	sResult.Replace(_T("{ext}"), sExt);
	sResult.Replace(_T("{num}"), sNum);
	sResult.Replace(_T("{date}"), file.Date);
	sResult.Replace(_T("{time}"), file.Time);
	// never drop the extension, the renamed files must still open
	if (!bHasExtToken) {
		sResult += sExt;
	}
	return sResult;
}

void CBatchRenameDlg::UpdatePreview() {
	if (m_bPopulating) {
		return;
	}
	m_edtPattern.GetWindowText(m_sPattern);
	CString sStartNum;
	m_edtStartNum.GetWindowText(sStartNum);
	m_nStartNum = max(0, _ttoi(sStartNum));

	// new name of each checked file, numbered in list order
	int nNumberWidth = GetNumberWidth();
	int nSequence = 0;
	int nChecked = 0;
	CString sProblem;
	std::set<CString, CStringNoCaseLess> newNames;
	for (int i = 0; i < (int)m_files.size(); i++) {
		if (m_lvFiles.GetCheckState(i)) {
			CString sNewName = ExpandPattern(m_files[i], nSequence++, nNumberWidth);
			m_lvFiles.SetItemText(i, 1, sNewName);
			nChecked++;
			if (sProblem.IsEmpty()) {
				CString sReason;
				if (!IsValidFileName(sNewName, sReason)) {
					sProblem = sReason;
				} else if (!newNames.insert(sNewName).second) {
					sProblem.Format(CNLS::GetString(_T("More than one file would be named %s, add {num} to the pattern.")), (LPCTSTR)sNewName);
				}
			}
		} else {
			m_lvFiles.SetItemText(i, 1, _T(""));
		}
	}

	CString sPreview;
	if (nChecked == 0) {
		sPreview = CNLS::GetString(_T("No files selected"));
	} else if (!sProblem.IsEmpty()) {
		sPreview = sProblem;
	} else {
		sPreview.Format(CNLS::GetString(_T("%d files will be renamed.")), nChecked);
	}
	m_lblPreview.SetWindowText(sPreview);
	m_btnRename.EnableWindow(nChecked > 0 && sProblem.IsEmpty());
}

LRESULT CBatchRenameDlg::OnPatternChanged(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	UpdatePreview();
	return 0;
}

LRESULT CBatchRenameDlg::OnItemChanged(int /*idCtrl*/, LPNMHDR pnmh, BOOL& /*bHandled*/) {
	NMLISTVIEW* pListView = (NMLISTVIEW*)pnmh;
	// the check box is the state image
	if ((pListView->uChanged & LVIF_STATE) && ((pListView->uNewState ^ pListView->uOldState) & LVIS_STATEIMAGEMASK)) {
		UpdatePreview();
	}
	return 0;
}

LRESULT CBatchRenameDlg::OnRename(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	if (m_bRenaming) return 0;
	UpdatePreview();
	m_bRenaming = true;
	m_btnRename.EnableWindow(FALSE);
	m_btnCancel.EnableWindow(FALSE);

	CString sDir = m_fileList.CurrentDirectory();
	struct RenameJob {
		CString oldPath;
		CString newPath;
		CString tempPath;
		bool changes;
	};
	std::vector<RenameJob> jobs;
	std::set<CString, CStringNoCaseLess> targets;
	CString error;

	int nNumberWidth = GetNumberWidth();
	int nSequence = 0;
	for (int i = 0; i < (int)m_files.size(); i++) {
		if (!m_lvFiles.GetCheckState(i)) continue;
		CString sNewName = ExpandPattern(m_files[i], nSequence++, nNumberWidth);
		if (!IsValidFileName(sNewName, error)) break;
		RenameJob job;
		job.oldPath = m_files[i].FileName;
		job.newPath = JoinPath(sDir, sNewName);
		job.changes = job.oldPath.Compare(job.newPath) != 0;
		if (!targets.insert(job.newPath).second) {
			error.Format(CNLS::GetString(_T("More than one file would be named %s, add {num} to the pattern.")), (LPCTSTR)sNewName);
			break;
		}
		jobs.push_back(job);
	}
	if (error.IsEmpty() && jobs.empty()) error = CNLS::GetString(_T("No files selected"));

	// renaming over a file that is renamed too is allowed (e.g. swapping numbers), over any other file not
	std::set<CString, CStringNoCaseLess> vacatedSources;
	for (size_t i = 0; i < jobs.size(); i++) if (jobs[i].changes) vacatedSources.insert(jobs[i].oldPath);
	if (error.IsEmpty()) {
		for (size_t i = 0; i < jobs.size(); i++) {
			const RenameJob& job = jobs[i];
			if (!job.changes) continue;
			if (::GetFileAttributes(job.oldPath) == INVALID_FILE_ATTRIBUTES) {
				error.Format(CNLS::GetString(_T("The file does not exist anymore:\n%s")), (LPCTSTR)job.oldPath);
				break;
			}
			if (::GetFileAttributes(job.newPath) != INVALID_FILE_ATTRIBUTES && vacatedSources.find(job.newPath) == vacatedSources.end()) {
				error.Format(CNLS::GetString(_T("A file with the new name already exists:\n%s")), (LPCTSTR)job.newPath);
				break;
			}
		}
	}
	if (!error.IsEmpty()) {
		m_bRenaming = false;
		m_btnRename.EnableWindow(TRUE);
		m_btnCancel.EnableWindow(TRUE);
		::MessageBox(m_hWnd, error, CNLS::GetString(_T("Batch rename")), MB_OK | MB_ICONWARNING);
		return 0;
	}

	// two steps over temporary names, so files can swap names; rolled back if anything fails
	static volatile LONG tempSequence = 0;
	for (size_t i = 0; i < jobs.size(); i++) {
		if (!jobs[i].changes) continue;
		do {
			CString tempName;
			tempName.Format(_T(".jpegview-rename-%lu-%ld.tmp"), ::GetCurrentProcessId(), ::InterlockedIncrement(&tempSequence));
			jobs[i].tempPath = JoinPath(sDir, tempName);
		} while (::GetFileAttributes(jobs[i].tempPath) != INVALID_FILE_ATTRIBUTES);
	}

	std::vector<size_t> staged;
	for (size_t i = 0; i < jobs.size(); i++) {
		if (!jobs[i].changes) continue;
		if (!::MoveFileEx(jobs[i].oldPath, jobs[i].tempPath, MOVEFILE_WRITE_THROUGH)) {
			error.Format(CNLS::GetString(_T("The file could not be renamed (error %lu):\n%s")), ::GetLastError(), (LPCTSTR)jobs[i].oldPath);
			break;
		}
		staged.push_back(i);
	}
	if (!error.IsEmpty()) {
		for (std::vector<size_t>::reverse_iterator it = staged.rbegin(); it != staged.rend(); ++it)
			::MoveFileEx(jobs[*it].tempPath, jobs[*it].oldPath, MOVEFILE_WRITE_THROUGH);
	} else {
		std::vector<size_t> committed;
		for (size_t n = 0; n < staged.size(); n++) {
			size_t index = staged[n];
			if (!::MoveFileEx(jobs[index].tempPath, jobs[index].newPath, MOVEFILE_WRITE_THROUGH)) {
				error.Format(CNLS::GetString(_T("The file could not be renamed (error %lu):\n%s")), ::GetLastError(), (LPCTSTR)jobs[index].newPath);
				break;
			}
			committed.push_back(index);
		}
		if (!error.IsEmpty()) {
			bool bRollbackOk = true;
			for (size_t n = 0; n < committed.size(); n++)
				if (!::MoveFileEx(jobs[committed[n]].newPath, jobs[committed[n]].tempPath, MOVEFILE_WRITE_THROUGH)) bRollbackOk = false;
			for (std::vector<size_t>::reverse_iterator it = staged.rbegin(); it != staged.rend(); ++it)
				if (::GetFileAttributes(jobs[*it].tempPath) != INVALID_FILE_ATTRIBUTES &&
					!::MoveFileEx(jobs[*it].tempPath, jobs[*it].oldPath, MOVEFILE_WRITE_THROUGH)) bRollbackOk = false;
			if (!bRollbackOk) {
				error += _T("\n");
				error += CNLS::GetString(_T("Not all files could be renamed back, some .jpegview-rename temporary files may remain."));
			}
		}
	}

	m_bRenaming = false;
	m_btnRename.EnableWindow(TRUE);
	m_btnCancel.EnableWindow(TRUE);
	if (!error.IsEmpty()) {
		::MessageBox(m_hWnd, error, CNLS::GetString(_T("Batch rename")), MB_OK | MB_ICONERROR);
		return 0;
	}

	for (size_t i = 0; i < jobs.size(); i++) {
		if (jobs[i].changes) {
			CString sKey = jobs[i].oldPath;
			sKey.MakeLower();
			m_renamed[sKey] = jobs[i].newPath;
		}
	}
	EndDialog(IDOK);
	return 0;
}
