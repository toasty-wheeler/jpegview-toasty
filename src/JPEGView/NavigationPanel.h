#pragma once

#include "Panel.h"
#include <vector>

class CKeyMap;
class CThumbnailCache;

// Called when a thumbnail of the thumbnail strip is clicked, nOffset is the position relative to the current image (negative: before)
typedef void ThumbnailClickedHandler(void* pContext, int nOffset);

// Row of small previews of the images before and after the current one (like the photo apps of phones), clicking one shows it.
// The current image is in the middle, larger and with its aspect ratio, the others smaller and whole within square slots.
class CThumbnailStripCtrl : public CUICtrl {
public:
	enum { THUMBS_EACH_SIDE = 5 }; // thumbnails shown before and after the current image

	CThumbnailStripCtrl(CPanel* pPanel, CThumbnailCache* pCache);

	void SetClickedHandler(ThumbnailClickedHandler* handler, void* pContext) { m_clickedHandler = handler; m_pContext = pContext; }

	// Files in display order, nCurrent is the index of the current image in files
	void SetFiles(const std::vector<CString>& files, int nCurrent);

	// Width needed to show the maximal number of thumbnails at the given height
	static int GetWidthForHeight(int nHeight);

	// Repaints the strip, e.g. when a thumbnail has been loaded
	void Invalidate();

public:
	virtual CSize GetMinSize() { return CSize(0, 0); }
	virtual bool OnMouseLButton(EMouseEvent eMouseEvent, int nX, int nY);
	virtual bool OnMouseMove(int nX, int nY);
	virtual void OnPaint(CDC & dc, const CPoint& offset);

protected:
	virtual void Draw(CDC & dc, CRect position, bool bBlack) {}

private:
	CThumbnailCache* m_pCache;
	std::vector<CString> m_files;
	int m_nCurrent;
	int m_nHover;
	int m_nPressed;
	ThumbnailClickedHandler* m_clickedHandler;
	void* m_pContext;

	CRect GetSlotRect(int nIndex);
	CRect GetImageRect(int nIndex, const CSize& thumbSize);
	int HitTest(int nX, int nY);
};

// Navigation panel containing buttons for quick access to most important commands (e.g. next, previous).
class CNavigationPanel : public CPanel {
public:
	// IDs of the controls on this panel
	enum {
		ID_btnHome,
		ID_btnPrev,
		ID_gap1,
		ID_btnNext,
		ID_btnEnd,
		ID_gap2,
		ID_btnDelete,
		ID_gap3,
		ID_btnFitToScreen,
		ID_btnWindowMode,
		ID_gap4,
		ID_btnRotateCW,
		ID_btnRotateCCW,
		ID_btnRotateFree,
		ID_btnPerspectiveCorrection,
		ID_gap5,
		ID_btnKeepParams,
		ID_btnLandscapeMode,
		ID_gap6,
		ID_btnShowInfo,
		ID_btnSettings,
		ID_thumbnailStrip
	};
public:
	// The panel is on the given window above the image processing panel.
	// With a thumbnail cache, a thumbnail strip is shown below the buttons.
	CNavigationPanel(HWND hWnd, INotifiyMouseCapture* pNotifyMouseCapture, CPanel* pImageProcPanel, CKeyMap* keyMap, bool* pFullScreenMode,
		DecisionMethod* isCurrentImageFitToScreen, void* pDecisionMethodParam, CThumbnailCache* pThumbnailCache);

	CButtonCtrl* GetBtnHome() { return GetControl<CButtonCtrl*>(ID_btnHome); }
	CButtonCtrl* GetBtnPrev() { return GetControl<CButtonCtrl*>(ID_btnPrev); }
	CButtonCtrl* GetBtnNext() { return GetControl<CButtonCtrl*>(ID_btnNext); }
	CButtonCtrl* GetBtnEnd() { return GetControl<CButtonCtrl*>(ID_btnEnd); }
	CButtonCtrl* GetBtnDelete() { return GetControl<CButtonCtrl*>(ID_btnDelete); }
	CButtonCtrl* GetBtnFitToScreen() { return GetControl<CButtonCtrl*>(ID_btnFitToScreen); }
	CButtonCtrl* GetBtnWindowMode() { return GetControl<CButtonCtrl*>(ID_btnWindowMode); }
	CButtonCtrl* GetBtnRotateCW() { return GetControl<CButtonCtrl*>(ID_btnRotateCW); }
	CButtonCtrl* GetBtnRotateCCW() { return GetControl<CButtonCtrl*>(ID_btnRotateCCW); }
	CButtonCtrl* GetBtnRotateFree() { return GetControl<CButtonCtrl*>(ID_btnRotateFree); }
	CButtonCtrl* GetBtnPerspectiveCorrection() { return GetControl<CButtonCtrl*>(ID_btnPerspectiveCorrection); }
	CButtonCtrl* GetBtnKeepParams() { return GetControl<CButtonCtrl*>(ID_btnKeepParams); }
	CButtonCtrl* GetBtnLandscapeMode() { return GetControl<CButtonCtrl*>(ID_btnLandscapeMode); }
	CButtonCtrl* GetBtnShowInfo() { return GetControl<CButtonCtrl*>(ID_btnShowInfo); }
	CButtonCtrl* GetBtnSettings() { return GetControl<CButtonCtrl*>(ID_btnSettings); }
	CThumbnailStripCtrl* GetThumbnailStrip() { return GetControl<CThumbnailStripCtrl*>(ID_thumbnailStrip); } // NULL if not shown

	virtual CRect PanelRect();
	virtual void RequestRepositioning();

	bool AdjustMaximalWidth(int nMaxWidth); // return if the width has been adjusted

protected:
	virtual void RepositionAll();

private:
	// Painting handlers for the buttons
	static void PaintHomeBtn(void* pContext, const CRect& rect, CDC& dc);
	static void PaintPrevBtn(void* pContext, const CRect& rect, CDC& dc);
	static void PaintNextBtn(void* pContext, const CRect& rect, CDC& dc);
	static void PaintEndBtn(void* pContext, const CRect& rect, CDC& dc);
	static void PaintDeleteBtn(void* pContext, const CRect& rect, CDC& dc);
	static void PaintSettingsBtn(void* pContext, const CRect& rect, CDC& dc);
	static void PaintZoomToFitBtn(void* pContext, const CRect& rect, CDC& dc);
	static void PaintZoomTo1to1Btn(void* pContext, const CRect& rect, CDC& dc);
	static void PaintZoomFitToggleBtn(void* pContext, const CRect& rect, CDC& dc);
	static void PaintWindowModeBtn(void* pContext, const CRect& rect, CDC& dc);
	static void PaintRotateCWBtn(void* pContext, const CRect& rect, CDC& dc);
	static void PaintRotateCCWBtn(void* pContext, const CRect& rect, CDC& dc);
	static void PaintFreeRotBtn(void* pContext, const CRect& rect, CDC& dc);
	static void PaintPerspectiveBtn(void* pContext, const CRect& rect, CDC& dc);
	static void PaintInfoBtn(void* pContext, const CRect& rect, CDC& dc);
	static void PaintKeepParamsBtn(void* pContext, const CRect& rect, CDC& dc);
	static void PaintLandscapeModeBtn(void* pContext, const CRect& rect, CDC& dc);

	static LPCTSTR WindowModeTooltip(void* pContext);
	static LPCTSTR ZoomFitToggleTooltip(void* pContext);

	CString GetTooltip(CKeyMap* keyMap, LPCTSTR tooltip, int command);
	void AddGap(int nID, int nGap);
	void SetScaledWidth(float fScale);

	CKeyMap* m_keyMap;
	bool* m_pFullScreenMode;
	DecisionMethod* m_isCurrentImageFitToScreen;
	void* m_pDecisionMethodParam;
	CPanel* m_pImageProcPanel;
	CRect m_clientRect;
	int m_nWidth, m_nHeight; // of the button row
	int m_nStripHeight, m_nStripGap; // thumbnail strip below the buttons, zero if there is none
	int m_nBorder;
	int m_nGap;
	int m_nOptimalWidth;
	float m_fAdditionalScale;
};
