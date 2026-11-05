#pragma once

#include <vector>
#include <map>

// Loads small previews of image files in a background thread, using the thumbnails of Windows Explorer
// (fast, cached by Windows and available for every format Explorer can preview).
// Only the thumbnails of the currently wanted files are kept in memory.
class CThumbnailCache
{
public:
	// Posts WM_THUMBNAIL_READY to hWnd when a thumbnail has been loaded. nThumbnailSize is the requested size in pixels.
	CThumbnailCache(HWND hWnd, int nThumbnailSize);
	~CThumbnailCache();

	// Sets the files whose thumbnails are needed, most important first. Thumbnails of other files are released.
	void SetWantedFiles(const std::vector<CString>& files);

	// Gets the thumbnail of the file and its size, NULL if not loaded (yet) or no thumbnail exists.
	// The bitmap is owned by the cache and stays valid until the next SetWantedFiles() call.
	HBITMAP GetThumbnail(LPCTSTR sFileName, CSize* pSize = NULL);

	// Stops the loader thread, returns false if it did not stop in time (the object must not be deleted then)
	bool Stop(DWORD nTimeoutMs);

private:
	struct CEntry {
		HBITMAP Bitmap;
		CSize Size;
	};

	HWND m_hWnd;
	int m_nSize;
	CRITICAL_SECTION m_cs;
	HANDLE m_hWakeEvent;
	HANDLE m_hThread;
	volatile bool m_bTerminate;
	std::vector<CString> m_wanted;
	std::map<CString, CEntry> m_entries; // loaded thumbnails, Bitmap is NULL if there is none for the file

	static DWORD WINAPI ThreadProc(void* pParameter);
	void Run();
	HBITMAP LoadThumbnail(LPCTSTR sFileName, CSize& size);
	bool IsWanted(const CString& sFileName) const;
};
