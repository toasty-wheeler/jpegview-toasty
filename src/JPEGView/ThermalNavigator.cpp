#include "StdAfx.h"
#include "resource.h"
#include "ThermalNavigator.h"
#include "MainDlg.h"
#include "JPEGImage.h"
#include "FlirThermal.h"
#include "PanelMgr.h"
#include "HelpersGUI.h"
#include "SettingsProvider.h"
#include <math.h>

static const int PREVIEW_HEIGHT = 150;
static const int MIN_WIDTH = 140; // the temperature texts must fit
static const int INFO_LINE_HEIGHT = 17;
static const int MARGIN = 12;
static const int TOP = 44; // below the window buttons
static const int SCALE_GAP = 14; // between navigator and temperature scale
static const int SCALE_HEIGHT = 300;
static const int SCALE_BAR_WIDTH = 18;
static const int SCALE_LABEL_WIDTH = 64;

CThermalNavigator::CThermalNavigator(CMainDlg* pMainDlg) {
	m_pMainDlg = pMainDlg;
	m_bHover = false;
	m_bCursorTemperatureValid = false;
	m_fCursorTemperature = 0.0f;
	m_nDragHandle = -1;
}

CRect CThermalNavigator::GetScaleRect() {
	CFlirThermalData* pThermalData = GetThermalData();
	CRect navigatorRect = GetRect();
	if (navigatorRect.IsRectEmpty() || pThermalData == NULL || !pThermalData->HasTemperatures() || !IsThermalViewShown() ||
		!m_pMainDlg->CanUseThermalTools() || pThermalData->GetMaxTemperature() - pThermalData->GetMinTemperature() < 0.1f) {
		return CRect(0, 0, 0, 0);
	}
	const CRect& clientRect = m_pMainDlg->ClientRect();
	int nTop = navigatorRect.bottom + HelpersGUI::ScaleToScreen(SCALE_GAP);
	int nHeight = min(HelpersGUI::ScaleToScreen(SCALE_HEIGHT), clientRect.bottom - HelpersGUI::ScaleToScreen(80) - nTop);
	if (nHeight < HelpersGUI::ScaleToScreen(100)) {
		return CRect(0, 0, 0, 0); // window too small
	}
	int nWidth = HelpersGUI::ScaleToScreen(SCALE_BAR_WIDTH + SCALE_LABEL_WIDTH);
	return CRect(navigatorRect.right - nWidth, nTop, navigatorRect.right, nTop + nHeight);
}

CRect CThermalNavigator::GetBarRect(const CRect& scaleRect) {
	// the labels are left of the bar, half a text line of room at the top and the bottom for them
	int nMargin = HelpersGUI::ScaleToScreen(8);
	return CRect(scaleRect.right - HelpersGUI::ScaleToScreen(SCALE_BAR_WIDTH), scaleRect.top + nMargin, scaleRect.right, scaleRect.bottom - nMargin);
}

void CThermalNavigator::GetRange(float& fLow, float& fHigh) {
	CFlirThermalData* pThermalData = GetThermalData();
	const CThermalMeasurements& measurements = m_pMainDlg->GetThermalMeasurements();
	if (measurements.CustomRange) {
		fLow = measurements.RangeLow;
		fHigh = measurements.RangeHigh;
	} else {
		fLow = pThermalData->GetMinTemperature();
		fHigh = pThermalData->GetMaxTemperature();
	}
}

// The bar shows the temperatures of the image, the hottest at the top
int CThermalNavigator::TemperatureToY(const CRect& barRect, float fTemperature) {
	CFlirThermalData* pThermalData = GetThermalData();
	float fMin = pThermalData->GetMinTemperature(), fMax = pThermalData->GetMaxTemperature();
	return barRect.bottom - 1 - Helpers::RoundToInt((fTemperature - fMin) / (fMax - fMin) * (barRect.Height() - 1));
}

float CThermalNavigator::YToTemperature(const CRect& barRect, int nY) {
	CFlirThermalData* pThermalData = GetThermalData();
	float fMin = pThermalData->GetMinTemperature(), fMax = pThermalData->GetMaxTemperature();
	float fRel = (float)(barRect.bottom - 1 - nY) / (barRect.Height() - 1);
	return fMin + max(0.0f, min(1.0f, fRel)) * (fMax - fMin);
}

void CThermalNavigator::PaintScale(CDC& dc) {
	CRect scaleRect = GetScaleRect();
	if (scaleRect.IsRectEmpty()) {
		return;
	}
	CBrush backBrush;
	backBrush.CreateSolidBrush(RGB(24, 24, 24));
	dc.FillRect(&scaleRect, backBrush);

	// palette as the image is colored with: below the range black, above white
	CRect barRect = GetBarRect(scaleRect);
	float fLow, fHigh;
	GetRange(fLow, fHigh);
	for (int y = barRect.top; y < barRect.bottom; y++) {
		float fTemperature = YToTemperature(barRect, y);
		double dValue = (fHigh > fLow) ? (fTemperature - fLow) / (fHigh - fLow) : 0.0;
		CBrush lineBrush;
		lineBrush.CreateSolidBrush(CFlirThermalData::GetPaletteColor(dValue));
		CRect lineRect(barRect.left, y, barRect.right, y + 1);
		dc.FillRect(&lineRect, lineBrush);
	}
	CBrush frameBrush;
	frameBrush.CreateSolidBrush(RGB(200, 200, 200));
	dc.FrameRect(&barRect, frameBrush);

	// the two handles: triangle pointing to the bar and the temperature
	HelpersGUI::SelectDefaultGUIFont(dc);
	dc.SetBkMode(TRANSPARENT);
	int nHandleSize = HelpersGUI::ScaleToScreen(6);
	for (int nHandle = 0; nHandle < 2; nHandle++) {
		float fTemperature = (nHandle == 0) ? fLow : fHigh;
		int nY = TemperatureToY(barRect, fTemperature);
		COLORREF color = (m_nDragHandle == nHandle) ? CSettingsProvider::This().ColorHighlight() : RGB(255, 255, 255);
		CBrush handleBrush;
		handleBrush.CreateSolidBrush(color);
		CPen handlePen;
		handlePen.CreatePen(PS_SOLID, 1, RGB(0, 0, 0));
		HBRUSH hOldBrush = dc.SelectBrush(handleBrush);
		HPEN hOldPen = dc.SelectPen(handlePen);
		POINT triangle[3] = { { barRect.left - 1, nY }, { barRect.left - 1 - nHandleSize * 3 / 2, nY - nHandleSize }, { barRect.left - 1 - nHandleSize * 3 / 2, nY + nHandleSize } };
		dc.Polygon(triangle, 3);
		dc.SelectBrush(hOldBrush);
		dc.SelectPen(hOldPen);

		CString sLabel;
		sLabel.Format(_T("%.1f \x00B0") _T("C"), fTemperature);
		CRect labelRect(scaleRect.left, nY - HelpersGUI::ScaleToScreen(8), barRect.left - nHandleSize * 2, nY + HelpersGUI::ScaleToScreen(8));
		dc.SetTextColor(color);
		dc.DrawText(sLabel, sLabel.GetLength(), &labelRect, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
	}
}

CFlirThermalData* CThermalNavigator::GetThermalData() {
	CJPEGImage* pImage = m_pMainDlg->GetCurrentImage();
	return (pImage == NULL) ? NULL : pImage->GetThermalData();
}

bool CThermalNavigator::IsThermalViewShown() {
	CJPEGImage* pImage = m_pMainDlg->GetCurrentImage();
	return pImage != NULL && pImage->GetThermalData() != NULL && !pImage->IsThermalPhotoShown();
}

CRect CThermalNavigator::GetRect() {
	CFlirThermalData* pThermalData = GetThermalData();
	if (pThermalData == NULL || !pThermalData->HasPhoto() || m_pMainDlg->IsInMovieMode() || m_pMainDlg->GetPanelMgr()->IsModalPanelShown()) {
		return CRect(0, 0, 0, 0);
	}
	CSize thumbSize;
	if (pThermalData->GetThumbnail(IsThermalViewShown(), thumbSize) == NULL || thumbSize.cy <= 0) {
		return CRect(0, 0, 0, 0);
	}
	int nPreviewHeight = HelpersGUI::ScaleToScreen(PREVIEW_HEIGHT);
	int nWidth = max(HelpersGUI::ScaleToScreen(MIN_WIDTH), nPreviewHeight * thumbSize.cx / thumbSize.cy);
	int nHeight = nPreviewHeight + 2 * HelpersGUI::ScaleToScreen(INFO_LINE_HEIGHT) + HelpersGUI::ScaleToScreen(6);
	const CRect& clientRect = m_pMainDlg->ClientRect();
	int nRight = clientRect.right - HelpersGUI::ScaleToScreen(MARGIN);
	int nTop = clientRect.top + HelpersGUI::ScaleToScreen(TOP);
	if (nWidth + 2 * HelpersGUI::ScaleToScreen(MARGIN) > clientRect.Width() || nTop + nHeight > clientRect.bottom - HelpersGUI::ScaleToScreen(MARGIN)) {
		return CRect(0, 0, 0, 0); // window too small
	}
	return CRect(nRight - nWidth, nTop, nRight, nTop + nHeight);
}

CRect CThermalNavigator::GetPreviewRect(const CRect& rect) {
	return CRect(rect.left, rect.top, rect.right, rect.top + HelpersGUI::ScaleToScreen(PREVIEW_HEIGHT));
}

CRect CThermalNavigator::GetInfoRect(const CRect& rect) {
	return CRect(rect.left, rect.top + HelpersGUI::ScaleToScreen(PREVIEW_HEIGHT), rect.right, rect.bottom);
}

void CThermalNavigator::InvalidateInfo() {
	CRect rect = GetRect();
	if (!rect.IsRectEmpty()) {
		CRect infoRect = GetInfoRect(rect);
		::InvalidateRect(m_pMainDlg->GetHWND(), &infoRect, FALSE);
	}
}

void CThermalNavigator::OnPaint(CDC& dc) {
	CRect rect = GetRect();
	if (rect.IsRectEmpty()) {
		return;
	}
	CFlirThermalData* pThermalData = GetThermalData();
	bool bShowPhotoPreview = IsThermalViewShown();
	CSize thumbSize;
	const void* pThumb = pThermalData->GetThumbnail(bShowPhotoPreview, thumbSize);

	CBrush backBrush;
	backBrush.CreateSolidBrush(RGB(24, 24, 24));
	dc.FillRect(&rect, backBrush);

	// preview, keeping its aspect ratio
	CRect previewRect = GetPreviewRect(rect);
	int nPreviewWidth = min(previewRect.Width(), previewRect.Height() * thumbSize.cx / thumbSize.cy);
	CRect imageRect(CPoint(previewRect.left + (previewRect.Width() - nPreviewWidth) / 2, previewRect.top), CSize(nPreviewWidth, previewRect.Height()));
	BITMAPINFO bmInfo = { 0 };
	bmInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bmInfo.bmiHeader.biWidth = thumbSize.cx;
	bmInfo.bmiHeader.biHeight = -thumbSize.cy;
	bmInfo.bmiHeader.biPlanes = 1;
	bmInfo.bmiHeader.biBitCount = 32;
	bmInfo.bmiHeader.biCompression = BI_RGB;
	int nOldMode = dc.SetStretchBltMode(HALFTONE);
	::SetBrushOrgEx(dc, 0, 0, NULL);
	dc.StretchDIBits(imageRect.left, imageRect.top, imageRect.Width(), imageRect.Height(), 0, 0, thumbSize.cx, thumbSize.cy,
		pThumb, &bmInfo, DIB_RGB_COLORS, SRCCOPY);
	dc.SetStretchBltMode(nOldMode);

	HelpersGUI::SelectDefaultGUIFont(dc);
	dc.SetBkMode(TRANSPARENT);
	COLORREF textColor = CSettingsProvider::This().ColorGUI();
	int nPadding = HelpersGUI::ScaleToScreen(5);

	// what clicking the preview shows
	dc.SetTextColor(m_bHover ? CSettingsProvider::This().ColorHighlight() : textColor);
	CRect labelRect(imageRect.left + nPadding, imageRect.top + nPadding / 2, imageRect.right - nPadding, imageRect.top + HelpersGUI::ScaleToScreen(INFO_LINE_HEIGHT));
	HelpersGUI::DrawTextBordered(dc, bShowPhotoPreview ? _T("Photo") : _T("Thermal"), labelRect, DT_LEFT);

	// temperatures
	dc.SetTextColor(textColor);
	CRect infoRect = GetInfoRect(rect);
	infoRect.DeflateRect(nPadding, HelpersGUI::ScaleToScreen(3));
	CRect line1(infoRect.left, infoRect.top, infoRect.right, infoRect.top + HelpersGUI::ScaleToScreen(INFO_LINE_HEIGHT));
	CRect line2(infoRect.left, line1.bottom, infoRect.right, line1.bottom + HelpersGUI::ScaleToScreen(INFO_LINE_HEIGHT));
	CString sLine1, sLine2;
	if (pThermalData->HasTemperatures()) {
		if (m_bCursorTemperatureValid) {
			sLine1.Format(_T("Cursor: %.1f \x00B0") _T("C"), m_fCursorTemperature);
		} else {
			sLine1 = bShowPhotoPreview ? _T("Cursor: -") : _T("Cursor: (thermal view only)");
		}
		sLine2.Format(_T("Min %.1f \x00B0") _T("C   Max %.1f \x00B0") _T("C"), pThermalData->GetMinTemperature(), pThermalData->GetMaxTemperature());
	} else {
		sLine1 = _T("No temperature data");
	}
	dc.DrawText(sLine1, sLine1.GetLength(), &line1, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
	dc.DrawText(sLine2, sLine2.GetLength(), &line2, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

	// frame, highlighted when the mouse is over it
	CBrush frameBrush;
	frameBrush.CreateSolidBrush(m_bHover ? CSettingsProvider::This().ColorHighlight() : RGB(200, 200, 200));
	dc.FrameRect(&rect, frameBrush);

	PaintScale(dc);
}

bool CThermalNavigator::OnMouseLButtonDown(int nX, int nY) {
	CRect rect = GetRect();
	if (!rect.IsRectEmpty() && rect.PtInRect(CPoint(nX, nY))) {
		m_pMainDlg->ToggleThermalPhoto();
		return true;
	}
	CRect scaleRect = GetScaleRect();
	if (!scaleRect.IsRectEmpty() && scaleRect.PtInRect(CPoint(nX, nY))) {
		// drag the handle nearest to the mouse
		CRect barRect = GetBarRect(scaleRect);
		float fLow, fHigh;
		GetRange(fLow, fHigh);
		m_nDragHandle = (abs(nY - TemperatureToY(barRect, fHigh)) < abs(nY - TemperatureToY(barRect, fLow))) ? 1 : 0;
		OnMouseMove(nX, nY);
		return true;
	}
	return false;
}

bool CThermalNavigator::OnMouseLButtonUp() {
	if (m_nDragHandle < 0) {
		return false;
	}
	m_nDragHandle = -1;
	CRect scaleRect = GetScaleRect();
	::InvalidateRect(m_pMainDlg->GetHWND(), &scaleRect, FALSE);
	return true;
}

bool CThermalNavigator::OnMouseLButtonDblClk(int nX, int nY) {
	CRect scaleRect = GetScaleRect();
	if (!scaleRect.IsRectEmpty() && scaleRect.PtInRect(CPoint(nX, nY))) {
		m_nDragHandle = -1;
		m_pMainDlg->ResetThermalRange();
		return true;
	}
	return false;
}

bool CThermalNavigator::OnMouseMove(int nX, int nY) {
	if (m_nDragHandle >= 0) {
		CRect scaleRect = GetScaleRect();
		if (scaleRect.IsRectEmpty()) {
			m_nDragHandle = -1;
			return false;
		}
		CRect barRect = GetBarRect(scaleRect);
		float fLow, fHigh;
		GetRange(fLow, fHigh);
		float fTemperature = YToTemperature(barRect, nY);
		const float cfMinSpan = 0.2f;
		if (m_nDragHandle == 1) {
			fHigh = max(fLow + cfMinSpan, fTemperature);
		} else {
			fLow = min(fHigh - cfMinSpan, fTemperature);
		}
		m_pMainDlg->SetThermalRange(fLow, fHigh);
		return true;
	}

	CRect rect = GetRect();
	CRect scaleRect = GetScaleRect();
	bool bOverScale = !scaleRect.IsRectEmpty() && scaleRect.PtInRect(CPoint(nX, nY));
	bool bHover = !rect.IsRectEmpty() && rect.PtInRect(CPoint(nX, nY));
	if (bHover != m_bHover) {
		m_bHover = bHover;
		if (!rect.IsRectEmpty()) ::InvalidateRect(m_pMainDlg->GetHWND(), &rect, FALSE);
	}

	// temperature under the cursor, only in the thermal view and while it is not rotated or cropped (the raw data is not)
	bool bValid = false;
	float fTemperature = 0.0f;
	CFlirThermalData* pThermalData = GetThermalData();
	CJPEGImage* pImage = m_pMainDlg->GetCurrentImage();
	if (pThermalData != NULL && pThermalData->HasTemperatures() && IsThermalViewShown() && !bHover && !bOverScale &&
		m_pMainDlg->CanUseThermalTools()) {
		float fX = (float)nX, fY = (float)nY;
		if (m_pMainDlg->ScreenToImage(fX, fY)) {
			double dRelX = fX / pImage->OrigWidth();
			double dRelY = fY / pImage->OrigHeight();
			if (dRelX >= 0 && dRelX < 1 && dRelY >= 0 && dRelY < 1) {
				fTemperature = pThermalData->GetTemperature(dRelX, dRelY);
				bValid = true;
			}
		}
	}
	if (bValid != m_bCursorTemperatureValid || (bValid && fabs(fTemperature - m_fCursorTemperature) >= 0.05f)) {
		m_bCursorTemperatureValid = bValid;
		m_fCursorTemperature = fTemperature;
		InvalidateInfo();
	}
	return bHover || bOverScale;
}

void CThermalNavigator::AfterNewImageLoaded() {
	m_bCursorTemperatureValid = false;
	CPoint mousePos = m_pMainDlg->GetMousePos();
	OnMouseMove(mousePos.x, mousePos.y);
}
