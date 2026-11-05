#include "StdAfx.h"
#include "ThumbnailCache.h"
#include "MessageDef.h"
#include <algorithm>

// IShellItemImageFactory (Windows Vista and later), declared here as the project targets an older Windows version
namespace {
	MIDL_INTERFACE("bcc18b79-ba16-442f-80c4-8a59c30c463b")
	IJVShellItemImageFactory : public IUnknown {
	public:
		virtual HRESULT STDMETHODCALLTYPE GetImage(SIZE size, int flags, HBITMAP* phbm) = 0;
	};
	const IID IID_JVShellItemImageFactory = { 0xbcc18b79, 0xba16, 0x442f, { 0x80, 0xc4, 0x8a, 0x59, 0xc3, 0x0c, 0x46, 0x3b } };
	const int JV_SIIGBF_BIGGERSIZEOK = 0x1;
	typedef HRESULT (WINAPI *SHCreateItemFromParsingNameProc)(PCWSTR pszPath, IBindCtx* pbc, REFIID riid, void** ppv);

	// Background behind transparent parts of thumbnails (icons, PNGs)
	const int BACKGROUND_GRAY = 40;
}

CThumbnailCache::CThumbnailCache(HWND hWnd, int nThumbnailSize) {
	m_hWnd = hWnd;
	m_nSize = nThumbnailSize;
	m_bTerminate = false;
	::InitializeCriticalSection(&m_cs);
	m_hWakeEvent = ::CreateEvent(NULL, FALSE, FALSE, NULL);
	m_hThread = ::CreateThread(NULL, 0, ThreadProc, this, 0, NULL);
	if (m_hThread != NULL) {
		::SetThreadPriority(m_hThread, THREAD_PRIORITY_BELOW_NORMAL); // never slow down loading the image itself
	}
}

CThumbnailCache::~CThumbnailCache() {
	Stop(INFINITE);
	std::map<CString, CEntry>::iterator iter;
	for (iter = m_entries.begin(); iter != m_entries.end(); iter++) {
		if (iter->second.Bitmap != NULL) ::DeleteObject(iter->second.Bitmap);
	}
	::CloseHandle(m_hWakeEvent);
	::DeleteCriticalSection(&m_cs);
}

bool CThumbnailCache::Stop(DWORD nTimeoutMs) {
	if (m_hThread == NULL) {
		return true;
	}
	m_bTerminate = true;
	::SetEvent(m_hWakeEvent);
	if (::WaitForSingleObject(m_hThread, nTimeoutMs) != WAIT_OBJECT_0) {
		return false;
	}
	::CloseHandle(m_hThread);
	m_hThread = NULL;
	return true;
}

void CThumbnailCache::SetWantedFiles(const std::vector<CString>& files) {
	::EnterCriticalSection(&m_cs);
	m_wanted = files;
	std::map<CString, CEntry>::iterator iter = m_entries.begin();
	while (iter != m_entries.end()) {
		if (!IsWanted(iter->first)) {
			if (iter->second.Bitmap != NULL) ::DeleteObject(iter->second.Bitmap);
			iter = m_entries.erase(iter);
		} else {
			iter++;
		}
	}
	::LeaveCriticalSection(&m_cs);
	::SetEvent(m_hWakeEvent);
}

HBITMAP CThumbnailCache::GetThumbnail(LPCTSTR sFileName, CSize* pSize) {
	HBITMAP hBitmap = NULL;
	::EnterCriticalSection(&m_cs);
	std::map<CString, CEntry>::const_iterator iter = m_entries.find(CString(sFileName));
	if (iter != m_entries.end()) {
		hBitmap = iter->second.Bitmap;
		if (pSize != NULL) *pSize = iter->second.Size;
	}
	::LeaveCriticalSection(&m_cs);
	return hBitmap;
}

bool CThumbnailCache::IsWanted(const CString& sFileName) const {
	return std::find(m_wanted.begin(), m_wanted.end(), sFileName) != m_wanted.end();
}

DWORD WINAPI CThumbnailCache::ThreadProc(void* pParameter) {
	((CThumbnailCache*)pParameter)->Run();
	return 0;
}

void CThumbnailCache::Run() {
	::CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
	while (!m_bTerminate) {
		// next wanted file without thumbnail, in order of importance
		CString sFileName;
		bool bFound = false;
		::EnterCriticalSection(&m_cs);
		for (size_t i = 0; i < m_wanted.size(); i++) {
			if (m_entries.find(m_wanted[i]) == m_entries.end()) {
				sFileName = m_wanted[i];
				bFound = true;
				break;
			}
		}
		::LeaveCriticalSection(&m_cs);
		if (!bFound) {
			::WaitForSingleObject(m_hWakeEvent, INFINITE);
			continue;
		}

		CSize size(0, 0);
		HBITMAP hBitmap = LoadThumbnail(sFileName, size);

		bool bStored = false;
		::EnterCriticalSection(&m_cs);
		if (IsWanted(sFileName) && m_entries.find(sFileName) == m_entries.end()) {
			CEntry entry = { hBitmap, size };
			m_entries[sFileName] = entry;
			bStored = true;
		}
		::LeaveCriticalSection(&m_cs);
		if (!bStored && hBitmap != NULL) {
			::DeleteObject(hBitmap);
		} else if (bStored && hBitmap != NULL) {
			::PostMessage(m_hWnd, WM_THUMBNAIL_READY, 0, 0);
		}
	}
	::CoUninitialize();
}

HBITMAP CThumbnailCache::LoadThumbnail(LPCTSTR sFileName, CSize& size) {
	static SHCreateItemFromParsingNameProc createItem = (SHCreateItemFromParsingNameProc)::GetProcAddress(::GetModuleHandle(_T("shell32.dll")), "SHCreateItemFromParsingName");
	if (createItem == NULL) {
		return NULL;
	}
	HBITMAP hBitmap = NULL;
	IJVShellItemImageFactory* pFactory = NULL;
	if (SUCCEEDED(createItem(sFileName, NULL, IID_JVShellItemImageFactory, (void**)&pFactory)) && pFactory != NULL) {
		SIZE requestedSize = { m_nSize, m_nSize };
		if (FAILED(pFactory->GetImage(requestedSize, JV_SIIGBF_BIGGERSIZEOK, &hBitmap))) {
			hBitmap = NULL;
		}
		pFactory->Release();
	}
	if (hBitmap == NULL) {
		return NULL;
	}

	BITMAP bm;
	::GetObject(hBitmap, sizeof(bm), &bm);
	size = CSize(bm.bmWidth, abs(bm.bmHeight));
	if (bm.bmBitsPixel == 32 && size.cx > 0 && size.cy > 0) {
		// thumbnails with transparency (premultiplied alpha) are put on a dark background, so they can be drawn without alpha blending
		BITMAPINFO bmInfo = { 0 };
		bmInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
		bmInfo.bmiHeader.biWidth = size.cx;
		bmInfo.bmiHeader.biHeight = -size.cy;
		bmInfo.bmiHeader.biPlanes = 1;
		bmInfo.bmiHeader.biBitCount = 32;
		bmInfo.bmiHeader.biCompression = BI_RGB;
		std::vector<unsigned char> pixels(size.cx * size.cy * 4);
		HDC hDC = ::GetDC(NULL);
		if (::GetDIBits(hDC, hBitmap, 0, size.cy, &pixels[0], &bmInfo, DIB_RGB_COLORS) == size.cy) {
			bool bTransparent = false, bAllZeroAlpha = true;
			for (size_t i = 3; i < pixels.size(); i += 4) {
				if (pixels[i] != 0) bAllZeroAlpha = false;
				if (pixels[i] != 255) bTransparent = true;
			}
			if (bTransparent && !bAllZeroAlpha) {
				for (size_t i = 0; i < pixels.size(); i += 4) {
					int nInvAlpha = 255 - pixels[i + 3];
					for (int c = 0; c < 3; c++) {
						pixels[i + c] = (unsigned char)min(255, pixels[i + c] + BACKGROUND_GRAY * nInvAlpha / 255);
					}
					pixels[i + 3] = 255;
				}
				::SetDIBits(hDC, hBitmap, 0, size.cy, &pixels[0], &bmInfo, DIB_RGB_COLORS);
			}
		}
		::ReleaseDC(NULL, hDC);
	}
	return hBitmap;
}
