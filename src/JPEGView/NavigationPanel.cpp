#include "StdAfx.h"
#include "NavigationPanel.h"
#include "ImageProcessingPanel.h"
#include "Helpers.h"
#include "HelpersGUI.h"
#include "SettingsProvider.h"
#include "NLS.h"
#include "KeyMap.h"
#include "resource.h"
#include "ThumbnailCache.h"
#include <math.h>

#define NAV_PANEL_HEIGHT 32
#define NAV_PANEL_BORDER 6
#define NAV_PANEL_GAP 5
#define NAV_PANEL_STRIP_HEIGHT 48 // thumbnail strip below the buttons
#define NAV_PANEL_STRIP_GAP 4

#define THUMB_MAX_ASPECT 1.8 // the current image is drawn with its aspect ratio, between these width/height limits
#define THUMB_MIN_ASPECT 0.6
#define THUMB_FRAME_WIDTH 2 // outline of the current image and the one under the mouse, in pixels
#define THUMB_SIDE_SCALE (40.0 / 48.0) // the images around the current one are smaller (40 of the 48 pixels of the strip)

/////////////////////////////////////////////////////////////////////////////////////////////
// CNavigationPanel
/////////////////////////////////////////////////////////////////////////////////////////////

CNavigationPanel::CNavigationPanel(HWND hWnd, INotifiyMouseCapture* pNotifyMouseCapture, CPanel* pImageProcPanel, CKeyMap* keyMap, bool* pFullScreenMode,
		DecisionMethod* isCurrentImageFitToScreen, void* pDecisionMethodParam, CThumbnailCache* pThumbnailCache) : CPanel(hWnd, pNotifyMouseCapture) {
	m_pImageProcPanel = pImageProcPanel;
	m_clientRect = CRect(0, 0, 0, 0);
	m_fDPIScale *= CSettingsProvider::This().ScaleFactorNavPanel();
	if (pThumbnailCache != NULL) {
		m_controls[ID_thumbnailStrip] = new CThumbnailStripCtrl(this, pThumbnailCache);
	}
	SetScaledWidth(1.0f);
	m_isCurrentImageFitToScreen = isCurrentImageFitToScreen;
	m_pDecisionMethodParam = pDecisionMethodParam;
	m_keyMap = keyMap;
	m_pFullScreenMode = pFullScreenMode;

	AddUserPaintButton(ID_btnHome, GetTooltip(keyMap, _T("Show first image in folder"), IDM_FIRST), &PaintHomeBtn);
	AddUserPaintButton(ID_btnPrev, GetTooltip(keyMap, _T("Show previous image"), IDM_PREV), &PaintPrevBtn);
	AddGap(ID_gap1, 4);
	AddUserPaintButton(ID_btnNext, GetTooltip(keyMap, _T("Show next image"), IDM_NEXT), &PaintNextBtn);
	AddUserPaintButton(ID_btnEnd, GetTooltip(keyMap, _T("Show last image in folder"), IDM_LAST), &PaintEndBtn);
	AddGap(ID_gap2, 8);
	if (CSettingsProvider::This().AllowFileDeletion()) {
		AddUserPaintButton(ID_btnDelete, GetTooltip(keyMap, _T("Delete image file"),
			keyMap->GetKeyStringForCommand(IDM_MOVE_TO_RECYCLE_BIN).IsEmpty() ? IDM_MOVE_TO_RECYCLE_BIN_CONFIRM : IDM_MOVE_TO_RECYCLE_BIN), &PaintDeleteBtn); 
		AddGap(ID_gap3, 8);
	}
	AddUserPaintButton(ID_btnFitToScreen, &ZoomFitToggleTooltip, &PaintZoomFitToggleBtn, NULL, this);
	AddUserPaintButton(ID_btnWindowMode, &WindowModeTooltip, &PaintWindowModeBtn, NULL, this);
	AddGap(ID_gap4, 8);
	AddUserPaintButton(ID_btnRotateCW, GetTooltip(keyMap, _T("Rotate image 90 deg clockwise"), IDM_ROTATE_90), &PaintRotateCWBtn);
	AddUserPaintButton(ID_btnRotateCCW, GetTooltip(keyMap, _T("Rotate image 90 deg counter-clockwise"), IDM_ROTATE_270), &PaintRotateCCWBtn);
	AddUserPaintButton(ID_btnRotateFree, GetTooltip(keyMap, _T("Rotate image by user-defined angle"), IDM_ROTATE), &PaintFreeRotBtn);
	AddUserPaintButton(ID_btnPerspectiveCorrection, GetTooltip(keyMap, _T("Correct converging lines (perspective correction)"), IDM_PERSPECTIVE), &PaintPerspectiveBtn);
	AddGap(ID_gap5, 8);
	AddUserPaintButton(ID_btnKeepParams, GetTooltip(keyMap, _T("Keep processing parameters between images"), IDM_KEEP_PARAMETERS), &PaintKeepParamsBtn);
	AddUserPaintButton(ID_btnLandscapeMode, GetTooltip(keyMap, _T("Landscape picture enhancement mode"), IDM_LANDSCAPE_MODE), &PaintLandscapeModeBtn);
	AddGap(ID_gap6, 16);
	AddUserPaintButton(ID_btnShowInfo, GetTooltip(keyMap, _T("Display image (EXIF) information"), IDM_SHOW_FILEINFO), &PaintInfoBtn);
	AddUserPaintButton(ID_btnSettings, GetTooltip(keyMap, _T("Settings"), IDM_EDIT_USER_CONFIG), &PaintSettingsBtn);

	ControlsIterator iter;
	for (iter = m_controls.begin(); iter != m_controls.end(); iter++) {
		CButtonCtrl* pButton = dynamic_cast<CButtonCtrl*>(iter->second);
		if (pButton != NULL) {
			pButton->SetRound(true);
		}
	}

	m_nOptimalWidth = PanelRect().Width();
}

CString CNavigationPanel::GetTooltip(CKeyMap* keyMap, LPCTSTR tooltip, int command)
{
	CString shortCut = keyMap->GetKeyStringForCommand(command);
	if (shortCut.IsEmpty()) {
		return CString(CNLS::GetString(tooltip));
	}
	CString result;
	result.Format(_T("%s (%s)"), CNLS::GetString(tooltip), shortCut);
	return result;
}

void CNavigationPanel::AddGap(int nID, int nGap) {
	CGapCtrl* pGapCtrl = new CGapCtrl(this, nGap);
	m_controls[nID] = pGapCtrl;
}

CRect CNavigationPanel::PanelRect() {
	if (m_nWidth == 0) {
		int nNumButtons = 0;
		int nTotalGap = 0;
		ControlsIterator iter;
		for (iter = m_controls.begin( ); iter != m_controls.end( ); iter++ ) {
			if (dynamic_cast<CButtonCtrl*>(iter->second) != NULL) {
				nNumButtons++;
			} else {
				CGapCtrl* pGapCtrl = dynamic_cast<CGapCtrl*>(iter->second);
				if (pGapCtrl != NULL) {
					nTotalGap += (int)(pGapCtrl->GapWidth()*m_fDPIScale*m_fAdditionalScale);
				}
			}
		}
		int nButtonSize = m_nHeight - m_nBorder;
		m_nWidth = m_nBorder * 2 + (nNumButtons - 1) * m_nGap + nNumButtons * nButtonSize + nTotalGap;
	}

	// the thumbnail strip takes the place where the buttons are without it, the buttons move up
	int nPanelWidth = max(m_nWidth, GetThumbnailStrip() == NULL ? 0 : CThumbnailStripCtrl::GetWidthForHeight(m_nStripHeight) + 2 * m_nBorder);
	int nPanelHeight = m_nHeight + m_nStripGap + m_nStripHeight;
	CRect sliderRect = m_pImageProcPanel->PanelRect();
	m_clientRect = CRect(CPoint((sliderRect.right + sliderRect.left - nPanelWidth)/2,
		((sliderRect.Width() < HelpersGUI::ScaleToScreen(800) || !CSettingsProvider::This().ShowBottomPanel()) ? (sliderRect.bottom - HelpersGUI::ScaleToScreen(26)) : sliderRect.top) - nPanelHeight),
		CSize(nPanelWidth, nPanelHeight));
	return m_clientRect;
}

bool CNavigationPanel::AdjustMaximalWidth(int nMaxWidth) {
	int nUsedWidth = PanelRect().Width();
	if (nUsedWidth > nMaxWidth || (nUsedWidth < m_nOptimalWidth && nUsedWidth < nMaxWidth)) {
		float fScale = (float)min(nMaxWidth, m_nOptimalWidth)/m_nOptimalWidth;
		SetScaledWidth(fScale);
		RequestRepositioning();
		return true;
	}
	return false;
}

void CNavigationPanel::RequestRepositioning() {
	RepositionAll();
}

void CNavigationPanel::RepositionAll() {
	m_clientRect = PanelRect();
	int nButtonSize = m_nHeight - m_nBorder;
	int nStartX = m_clientRect.left + (m_clientRect.Width() - m_nWidth) / 2 + m_nBorder;
	int nStartY = m_clientRect.top + m_nBorder/2;
	CThumbnailStripCtrl* pStrip = GetThumbnailStrip();
	if (pStrip != NULL) {
		int nStripWidth = CThumbnailStripCtrl::GetWidthForHeight(m_nStripHeight);
		pStrip->SetPosition(CRect(CPoint(m_clientRect.left + (m_clientRect.Width() - nStripWidth) / 2, m_clientRect.top + m_nHeight + m_nStripGap),
			CSize(nStripWidth, m_nStripHeight)));
	}
	ControlsIterator iter;
	for (iter = m_controls.begin( ); iter != m_controls.end( ); iter++ ) {
		CButtonCtrl* pButton = dynamic_cast<CButtonCtrl*>(iter->second);
		if (pButton != NULL) {
			pButton->SetPosition(CRect(CPoint(nStartX, nStartY), CSize(nButtonSize, nButtonSize)));
			nStartX += nButtonSize + m_nGap;
		} else {
			CGapCtrl* pGap = dynamic_cast<CGapCtrl*>(iter->second);
			if (pGap != NULL) {
				nStartX += (int)(pGap->GapWidth()*m_fDPIScale*m_fAdditionalScale);
			}
		}
	}
}

void CNavigationPanel::SetScaledWidth(float fScale) {
	m_fAdditionalScale = fScale;
	m_nWidth = 0;
	m_nHeight = (int)(NAV_PANEL_HEIGHT*m_fDPIScale*fScale);
	m_nBorder = (int)(NAV_PANEL_BORDER*m_fDPIScale*fScale);
	m_nGap = (int)(NAV_PANEL_GAP*m_fDPIScale*fScale);
	bool bStrip = GetThumbnailStrip() != NULL;
	m_nStripHeight = bStrip ? (int)(NAV_PANEL_STRIP_HEIGHT*m_fDPIScale*fScale) : 0;
	m_nStripGap = bStrip ? (int)(NAV_PANEL_STRIP_GAP*m_fDPIScale*fScale) : 0;
}

/////////////////////////////////////////////////////////////////////////////////////////////
// CThumbnailStripCtrl
/////////////////////////////////////////////////////////////////////////////////////////////

CThumbnailStripCtrl::CThumbnailStripCtrl(CPanel* pPanel, CThumbnailCache* pCache) : CUICtrl(pPanel) {
	m_pCache = pCache;
	m_nCurrent = -1;
	m_nHover = -1;
	m_nPressed = -1;
	m_clickedHandler = NULL;
	m_pContext = NULL;
}

void CThumbnailStripCtrl::SetFiles(const std::vector<CString>& files, int nCurrent) {
	m_files = files;
	m_nCurrent = nCurrent;
	m_nHover = m_nPressed = -1;
	Invalidate();
}

static int GetThumbGap(int nHeight) {
	return max(2, nHeight / 10);
}

static int GetSideThumbSize(int nHeight) {
	return (int)(nHeight * THUMB_SIDE_SCALE + 0.5);
}

int CThumbnailStripCtrl::GetWidthForHeight(int nHeight) {
	return (int)(nHeight * THUMB_MAX_ASPECT) + 2 * THUMBS_EACH_SIDE * (GetSideThumbSize(nHeight) + GetThumbGap(nHeight));
}

void CThumbnailStripCtrl::Invalidate() {
	CRect rect(m_position);
	rect.InflateRect(2, 2);
	::InvalidateRect(m_pPanel->GetHWND(), &rect, FALSE);
}

// Slot of a thumbnail in the strip, also its clickable area: the current image uses the full height and its aspect ratio
// (within limits), the others smaller square slots
CRect CThumbnailStripCtrl::GetSlotRect(int nIndex) {
	int nHeight = m_position.Height();
	int nSide = GetSideThumbSize(nHeight);
	int nGap = GetThumbGap(nHeight);
	// the outline of the current image is drawn around it, keep it inside the strip (the panel clips what is outside)
	int nCurrentHeight = nHeight - 2 * THUMB_FRAME_WIDTH;
	int nCurrentWidth = nCurrentHeight;
	CSize size;
	if (m_nCurrent >= 0 && m_nCurrent < (int)m_files.size() && m_pCache->GetThumbnail(m_files[m_nCurrent], &size) != NULL && size.cy > 0) {
		nCurrentWidth = (int)(nCurrentHeight * max(THUMB_MIN_ASPECT, min(THUMB_MAX_ASPECT, (double)size.cx / size.cy)) + 0.5);
	}
	int nCurrentLeft = m_position.CenterPoint().x - nCurrentWidth / 2;
	if (nIndex == m_nCurrent) {
		return CRect(nCurrentLeft, m_position.top + THUMB_FRAME_WIDTH, nCurrentLeft + nCurrentWidth, m_position.bottom - THUMB_FRAME_WIDTH);
	}
	int nTop = m_position.top + (nHeight - nSide) / 2;
	if (nIndex < m_nCurrent) {
		int nRight = nCurrentLeft - nGap - (m_nCurrent - 1 - nIndex) * (nSide + nGap);
		return CRect(nRight - nSide, nTop, nRight, nTop + nSide);
	} else {
		int nLeft = nCurrentLeft + nCurrentWidth + nGap + (nIndex - m_nCurrent - 1) * (nSide + nGap);
		return CRect(nLeft, nTop, nLeft + nSide, nTop + nSide);
	}
}

// Where the thumbnail is drawn in its slot: the images around the current one are shown whole (fitted into the square),
// the current one fills its slot
CRect CThumbnailStripCtrl::GetImageRect(int nIndex, const CSize& thumbSize) {
	CRect slot = GetSlotRect(nIndex);
	if (nIndex == m_nCurrent || thumbSize.cx <= 0 || thumbSize.cy <= 0) {
		return slot;
	}
	double dAspect = (double)thumbSize.cx / thumbSize.cy;
	int nWidth = slot.Width(), nHeight = slot.Height();
	if (dAspect > 1.0) {
		nHeight = max(1, (int)(nWidth / dAspect + 0.5));
	} else {
		nWidth = max(1, (int)(nHeight * dAspect + 0.5));
	}
	CPoint topLeft(slot.left + (slot.Width() - nWidth) / 2, slot.top + (slot.Height() - nHeight) / 2);
	return CRect(topLeft, CSize(nWidth, nHeight));
}

int CThumbnailStripCtrl::HitTest(int nX, int nY) {
	for (int i = 0; i < (int)m_files.size(); i++) {
		// the whole height of the strip is clickable
		CRect slot = GetSlotRect(i);
		slot.top = m_position.top;
		slot.bottom = m_position.bottom;
		if (slot.PtInRect(CPoint(nX, nY))) {
			return i;
		}
	}
	return -1;
}

bool CThumbnailStripCtrl::OnMouseLButton(EMouseEvent eMouseEvent, int nX, int nY) {
	int nHit = HitTest(nX, nY);
	if (eMouseEvent == MouseEvent_BtnDown) {
		m_nPressed = nHit;
		return nHit >= 0;
	} else if (eMouseEvent == MouseEvent_BtnUp) {
		int nPressed = m_nPressed;
		m_nPressed = -1;
		if (nHit >= 0 && nHit == nPressed && nHit != m_nCurrent && m_clickedHandler != NULL) {
			m_clickedHandler(m_pContext, nHit - m_nCurrent);
		}
		return nHit >= 0;
	}
	return nHit >= 0;
}

bool CThumbnailStripCtrl::OnMouseMove(int nX, int nY) {
	int nHit = HitTest(nX, nY);
	if (nHit != m_nHover) {
		m_nHover = nHit;
		Invalidate();
	}
	return false;
}

void CThumbnailStripCtrl::OnPaint(CDC & dc, const CPoint& offset) {
	m_pos2Screen = CPoint(-offset.x, -offset.y);
	CDC memDC;
	memDC.CreateCompatibleDC(dc);
	int nOldMode = dc.SetStretchBltMode(HALFTONE);
	::SetBrushOrgEx(dc, 0, 0, NULL);
	CBrush placeholderBrush, outlineBrush, highlightBrush, currentBrush;
	placeholderBrush.CreateSolidBrush(RGB(48, 48, 48));
	outlineBrush.CreateSolidBrush(RGB(0, 0, 0));
	highlightBrush.CreateSolidBrush(CSettingsProvider::This().ColorHighlight());
	currentBrush.CreateSolidBrush(RGB(255, 255, 255));

	for (int i = 0; i < (int)m_files.size(); i++) {
		CSize size(0, 0);
		HBITMAP hBitmap = m_pCache->GetThumbnail(m_files[i], &size);
		CRect rect = GetImageRect(i, hBitmap != NULL ? size : CSize(0, 0));
		rect.OffsetRect(offset);
		if (hBitmap != NULL && size.cx > 0 && size.cy > 0) {
			// fill the rectangle, cutting away what does not fit (center crop)
			double dTargetAspect = (double)rect.Width() / rect.Height();
			CRect source(0, 0, size.cx, size.cy);
			if ((double)size.cx / size.cy > dTargetAspect) {
				int nWidth = max(1, (int)(size.cy * dTargetAspect));
				source.left = (size.cx - nWidth) / 2;
				source.right = source.left + nWidth;
			} else {
				int nHeight = max(1, (int)(size.cx / dTargetAspect));
				source.top = (size.cy - nHeight) / 2;
				source.bottom = source.top + nHeight;
			}
			HBITMAP hOldBitmap = memDC.SelectBitmap(hBitmap);
			dc.StretchBlt(rect.left, rect.top, rect.Width(), rect.Height(), memDC, source.left, source.top, source.Width(), source.Height(), SRCCOPY);
			memDC.SelectBitmap(hOldBitmap);
		} else {
			dc.FillRect(&rect, placeholderBrush);
		}
		if (i == m_nCurrent || i == m_nHover) {
			// white for the current image, highlight color for the one under the mouse
			CBrush& frameBrush = (i == m_nCurrent) ? currentBrush : highlightBrush;
			CRect frame(rect);
			frame.InflateRect(1, 1);
			dc.FrameRect(&frame, frameBrush);
			frame.InflateRect(1, 1);
			dc.FrameRect(&frame, frameBrush);
		} else {
			CRect frame(rect);
			frame.InflateRect(1, 1);
			dc.FrameRect(&frame, outlineBrush);
		}
	}
	dc.SetStretchBltMode(nOldMode);
}

void CNavigationPanel::PaintHomeBtn(void* pContext, const CRect& rect, CDC& dc) {
	CRect r = Helpers::InflateRect(rect, 0.3f);
	
	dc.MoveTo(r.left, r.top);
	dc.LineTo(r.left, r.bottom);

	int nX = r.left + (int)(r.Width()*0.4f);

	dc.MoveTo(nX, r.top);
	dc.LineTo(nX, r.bottom);

	int nW = r.Height() / 2;
	dc.MoveTo(r.right, r.top+1);
	dc.LineTo(r.right - nW + 1, r.top + nW);
	dc.LineTo(r.right+1, r.bottom);
}

void CNavigationPanel::PaintPrevBtn(void* pContext, const CRect& rect, CDC& dc) {
	CRect r = Helpers::InflateRect(rect, 0.3f);

	int nGapX = (int)(r.Width()*0.2f);
	int nX = r.left + nGapX;

	dc.MoveTo(nX, r.top);
	dc.LineTo(nX, r.bottom);

	int nX2 = r.right - nGapX;
	int nW = r.Height() / 2;
	dc.MoveTo(nX2, r.top+1);
	dc.LineTo(nX2 - nW + 1, r.top + nW);
	dc.LineTo(nX2+1, r.bottom);
}

void CNavigationPanel::PaintNextBtn(void* pContext, const CRect& rect, CDC& dc) {
	CRect r = Helpers::InflateRect(rect, 0.3f);

	int nGapX = (int)(r.Width()*0.2f);
	int nX = r.left + nGapX - 1;
	int nX2 = r.right - nGapX;
	int nW = r.Height() / 2;

	dc.MoveTo(nX+1, r.top+1);
	dc.LineTo(nX + nW, r.top + nW);
	dc.LineTo(nX, r.bottom);

	dc.MoveTo(nX2, r.top);
	dc.LineTo(nX2, r.bottom);
}

void CNavigationPanel::PaintEndBtn(void* pContext, const CRect& rect, CDC& dc) {
	CRect r = Helpers::InflateRect(rect, 0.3f);

	int nW = r.Height() / 2;
	dc.MoveTo(r.left, r.top+1);
	dc.LineTo(r.left + nW - 1, r.top + nW);
	dc.LineTo(r.left - 1, r.bottom);

	int nX2 = r.right - (int)(r.Width()*0.4f);

	dc.MoveTo(nX2, r.top);
	dc.LineTo(nX2, r.bottom);

	dc.MoveTo(r.right, r.top);
	dc.LineTo(r.right, r.bottom);
}

void CNavigationPanel::PaintDeleteBtn(void* pContext, const CRect& rect, CDC& dc) {
	CRect r = Helpers::InflateRect(rect, 0.3f);

	int size = r.right - r.left;
	dc.MoveTo(r.left + 1, r.top + 1);
	dc.LineTo(r.left + size, r.top + size);

	dc.MoveTo(r.left + 1, r.top + size - 1);
	dc.LineTo(r.left + size , r.top);
}

void CNavigationPanel::PaintSettingsBtn(void* pContext, const CRect& rect, CDC& dc) {
	// gear wheel: ring, hub and eight teeth
	double dCX = (rect.left + rect.right - 1) * 0.5;
	double dCY = (rect.top + rect.bottom - 1) * 0.5;
	double dW = rect.Width();
	int nRing = Helpers::RoundToInt(dW * 0.2);
	int nHub = max(1, Helpers::RoundToInt(dW * 0.07));
	int nCX = Helpers::RoundToInt(dCX), nCY = Helpers::RoundToInt(dCY);

	dc.SelectStockBrush(HOLLOW_BRUSH);
	dc.Ellipse(nCX - nRing, nCY - nRing, nCX + nRing + 1, nCY + nRing + 1);
	dc.Ellipse(nCX - nHub, nCY - nHub, nCX + nHub + 1, nCY + nHub + 1);
	for (int i = 0; i < 8; i++) {
		double dAngle = i * 3.14159265358979 / 4;
		double dCos = cos(dAngle), dSin = sin(dAngle);
		// each tooth is two parallel strokes so it is visible at small sizes
		for (int nSide = -1; nSide <= 1; nSide += 2) {
			double dOffX = -dSin * nSide * dW * 0.025, dOffY = dCos * nSide * dW * 0.025;
			dc.MoveTo(Helpers::RoundToInt(dCX + dCos * dW * 0.2 + dOffX), Helpers::RoundToInt(dCY + dSin * dW * 0.2 + dOffY));
			dc.LineTo(Helpers::RoundToInt(dCX + dCos * dW * 0.3 + dOffX), Helpers::RoundToInt(dCY + dSin * dW * 0.3 + dOffY));
		}
	}
}

void CNavigationPanel::PaintZoomToFitBtn(void* pContext, const CRect& rect, CDC& dc) {
	CRect r = Helpers::InflateRect(rect, 0.3f);

	int nW = r.Width()/3;
	int nD = r.Width()/2 - 1;

	for (int i = 0; i < 4; i++) {
		int nX = (i < 2) ? r.left : r.right;
		int nXP = (i < 2) ? nW : -nW;
		int nXD = (i < 2) ? nD : -nD;
		int nXA = (i < 2) ? 1 : -1;
		int nY = (i & 1) ? r.bottom : r.top;
		int nYP = (i & 1) ? -nW : nW;
		int nYD = (i & 1) ? -nD : nD;
		dc.MoveTo(nX, nY+nYP);
		dc.LineTo(nX, nY);
		dc.LineTo(nX+nXP+nXA, nY);
		dc.MoveTo(nX, nY);
		dc.LineTo(nX+nXD, nY+nYD);
	}
}

void CNavigationPanel::PaintZoomTo1to1Btn(void* pContext, const CRect& rect, CDC& dc) {
	CFont font;
	font.CreatePointFont(min(rect.Width() * 3, 80), _T("Microsoft Sans Serif"), dc, true, false);
	HFONT oldFont = dc.SelectFont(font);
	dc.DrawText(_T("1:1"), 3, (LPRECT)&rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);
	dc.SelectFont(oldFont);
}

void CNavigationPanel::PaintZoomFitToggleBtn(void* pContext, const CRect& rect, CDC& dc) {
	CNavigationPanel* pNavPanel = (CNavigationPanel*)pContext;
	if (pNavPanel->m_isCurrentImageFitToScreen(pNavPanel->m_pDecisionMethodParam)) {
		PaintZoomTo1to1Btn(pContext, rect, dc);
	} else {
		PaintZoomToFitBtn(pContext, rect, dc);
	}
}

void CNavigationPanel::PaintWindowModeBtn(void* pContext, const CRect& rect, CDC& dc) {
	CRect r = Helpers::InflateRect(rect, 0.3f);

	dc.SelectStockBrush(HOLLOW_BRUSH);
	dc.Rectangle(&r);
	int nTop = r.top + r.Height()/4;
	dc.MoveTo(r.left + 1, nTop);
	dc.LineTo(r.right, nTop);
}

void CNavigationPanel::PaintRotateCWBtn(void* pContext, const CRect& rect, CDC& dc) {
	CRect r = Helpers::InflateRect(rect, 0.3f);

	int nX = r.left + (int)(r.Width()*0.65f);

	dc.MoveTo(r.left-2, r.bottom);
	dc.LineTo(nX, r.bottom);
	dc.LineTo(nX, r.bottom - (int)(r.Height()*0.4f));
	dc.LineTo(r.left-2, r.bottom);

	dc.MoveTo(nX+2, r.bottom);
	dc.LineTo(r.right+2, r.bottom);
	dc.LineTo(nX+2, r.top);
	dc.LineTo(nX+2, r.bottom);
}

void CNavigationPanel::PaintRotateCCWBtn(void* pContext, const CRect& rect, CDC& dc) {
	CRect r = Helpers::InflateRect(rect, 0.3f);

	int nX = r.left + (int)(r.Width()*0.33f);

	dc.MoveTo(r.left-2, r.bottom);
	dc.LineTo(nX, r.bottom);
	dc.LineTo(nX, r.top);
	dc.LineTo(r.left-2, r.bottom);

	dc.MoveTo(nX+2, r.bottom);
	dc.LineTo(r.right+2, r.bottom);
	dc.LineTo(nX+2, r.bottom - (int)(r.Height()*0.4f));
	dc.LineTo(nX+2, r.bottom);
}

void CNavigationPanel::PaintFreeRotBtn(void* pContext, const CRect& rect, CDC& dc) {
	CRect r = Helpers::InflateRect(rect, 0.32f);
	r.OffsetRect(0, -1);

	int nArrowStart = (int)(r.Width()*0.6f);
	int nArrowLen = (int)(nArrowStart*0.68f);
	int nX = (r.right + r.left) >> 1;
	dc.Arc(r.left, r.top, r.right, r.bottom, nX, r.bottom, nX - 2, r.top);
	dc.MoveTo(nX, r.bottom - 1);
	dc.LineTo(nX - nArrowStart, r.bottom - 1);
	dc.MoveTo(nX - nArrowStart, r.bottom - 1);
	dc.LineTo(nX - nArrowStart + nArrowLen, r.bottom - 1 + nArrowLen);
	dc.MoveTo(nX - nArrowStart, r.bottom - 1);
	dc.LineTo(nX - nArrowStart + nArrowLen, r.bottom - 1 - nArrowLen);
}

void CNavigationPanel::PaintPerspectiveBtn(void* pContext, const CRect& rect, CDC& dc) {
	CRect r = Helpers::InflateRect(rect, 0.3f);

	int nW = (int)(r.Width() * 0.15f);
	int nW2 = (int)(r.Width() * 0.4f);
	dc.MoveTo(r.left + nW, r.bottom);
	dc.LineTo(r.right - nW, r.bottom);
	dc.LineTo(r.right - nW2, r.top);
	dc.LineTo(r.left + nW2, r.top);
	dc.LineTo(r.left + nW, r.bottom);
}

void CNavigationPanel::PaintKeepParamsBtn(void* pContext, const CRect& rect, CDC& dc) {
	CRect r = Helpers::InflateRect(rect, 0.3f);
	int nX = (int)(r.Width()*0.28f);
	int nY = r.bottom - (int)(r.Height()*0.25f);
	int nXM = (r.left + r.right) >> 1;

	dc.SelectStockBrush(HOLLOW_BRUSH);
	dc.Rectangle(r.left + nX, r.top, r.right - nX, nY);
	dc.MoveTo(r.left + nX, nY - nX);
	dc.LineTo(r.right - nX, nY - nX);
	dc.MoveTo(nXM, nY);
	dc.LineTo(nXM, r.bottom + 1);
}

void CNavigationPanel::PaintLandscapeModeBtn(void* pContext, const CRect& rect, CDC& dc) {
	CRect r = Helpers::InflateRect(rect, 0.3f);

	CPoint p1(r.left-1, r.bottom);
	dc.MoveTo(p1);
	CPoint p2(r.left + (int)(r.Width()*0.25f), r.top + (int)(r.Height()*0.1f));
	dc.LineTo(p2);
	CPoint p3(r.left + (int)(r.Width()*0.5f), r.top + (int)(r.Height()*0.8f));
	dc.LineTo(p3);
	dc.LineTo(r.left + (int)(r.Width()*0.75f), r.top + (int)(r.Height()*0.5f));
	dc.LineTo(r.right+1, r.bottom);
	dc.LineTo(r.left-1, r.bottom);

	int nRad = (int)(r.Width()*0.25f + 0.5f);
	dc.Ellipse(r.right - nRad - 1, r.top - nRad + 1, r.right + nRad - 1, r.top + nRad + 1);
}

void CNavigationPanel::PaintInfoBtn(void* pContext, const CRect& rect, CDC& dc) {
	CFont font;
	font.CreatePointFont(min(rect.Width() * 6, 160), _T("Times New Roman"), dc, false, true);
	HFONT oldFont = dc.SelectFont(font);
	dc.DrawText(_T("i"), 1, (LPRECT)&rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);
	dc.SelectFont(oldFont);
}

static CString staticTooltip;

LPCTSTR CNavigationPanel::WindowModeTooltip(void* pContext) {
	CNavigationPanel* pNavPanel = (CNavigationPanel*)pContext;
	if (*(pNavPanel->m_pFullScreenMode)) {
		staticTooltip = pNavPanel->GetTooltip(pNavPanel->m_keyMap, _T("Window mode"), IDM_FULL_SCREEN_MODE);
	} else {
		staticTooltip = pNavPanel->GetTooltip(pNavPanel->m_keyMap, _T("Full screen mode"), IDM_FULL_SCREEN_MODE);
	}
	return staticTooltip;
}

LPCTSTR CNavigationPanel::ZoomFitToggleTooltip(void* pContext) {
	CNavigationPanel* pNavPanel = (CNavigationPanel*)pContext;
	if (pNavPanel->m_isCurrentImageFitToScreen(pNavPanel->m_pDecisionMethodParam)) {
		staticTooltip = pNavPanel->GetTooltip(pNavPanel->m_keyMap, _T("Actual size of image"), IDM_TOGGLE_FIT_TO_SCREEN_100_PERCENTS);
	} else {
		staticTooltip = pNavPanel->GetTooltip(pNavPanel->m_keyMap, _T("Fit image to screen"), IDM_TOGGLE_FIT_TO_SCREEN_100_PERCENTS);
	}
	return staticTooltip;
}