#pragma once

class CMainDlg;
class CFlirThermalData;

// Navigator shown for FLIR thermal images (top right): a preview of the other view (the photo of the visible light
// camera while the thermal view is shown, and the other way round), clicking it swaps the views. Below the preview
// the temperature under the mouse cursor and the lowest and highest temperature of the image are shown.
class CThermalNavigator
{
public:
	CThermalNavigator(CMainDlg* pMainDlg);

	// Rectangle in client coordinates, empty if not shown
	CRect GetRect();

	void OnPaint(CDC& dc);
	// Returns true if the click was on the navigator (the views are swapped then)
	bool OnMouseLButtonDown(int nX, int nY);
	// Returns true if the mouse is over the navigator
	bool OnMouseMove(int nX, int nY);
	void AfterNewImageLoaded();

private:
	CMainDlg* m_pMainDlg;
	bool m_bHover;
	bool m_bCursorTemperatureValid;
	float m_fCursorTemperature;

	CFlirThermalData* GetThermalData();
	bool IsThermalViewShown();
	CRect GetPreviewRect(const CRect& rect);
	CRect GetInfoRect(const CRect& rect);
	void InvalidateInfo();
};
