#pragma once

class CMainDlg;
class CFlirThermalData;

// Navigator shown for FLIR thermal images (top right): a preview of the other view (the photo of the visible light
// camera while the thermal view is shown, and the other way round), clicking it swaps the views. Below the preview
// the temperature under the mouse cursor and the lowest and highest temperature of the image are shown.
// In the thermal view a temperature scale below the navigator sets the temperature range the image is colored with,
// by dragging its two handles. A double click on the scale goes back to the image of the camera.
class CThermalNavigator
{
public:
	CThermalNavigator(CMainDlg* pMainDlg);

	// Rectangle in client coordinates, empty if not shown
	CRect GetRect();
	// Rectangle of the temperature scale in client coordinates, empty if not shown
	CRect GetScaleRect();

	void OnPaint(CDC& dc);
	// Returns true if the click was on the navigator (the views are swapped then) or the scale (dragging starts)
	bool OnMouseLButtonDown(int nX, int nY);
	// Returns true if a drag of the scale ended
	bool OnMouseLButtonUp();
	// Returns true if the double click was on the scale (the range is reset then)
	bool OnMouseLButtonDblClk(int nX, int nY);
	// Returns true if the mouse is over the navigator or the scale, or the scale is dragged
	bool OnMouseMove(int nX, int nY);
	bool IsDraggingScale() const { return m_nDragHandle >= 0; }
	void AfterNewImageLoaded();

private:
	CMainDlg* m_pMainDlg;
	bool m_bHover;
	bool m_bCursorTemperatureValid;
	float m_fCursorTemperature;
	int m_nDragHandle; // -1 none, 0 lower limit, 1 upper limit

	CFlirThermalData* GetThermalData();
	bool IsThermalViewShown();
	CRect GetPreviewRect(const CRect& rect);
	CRect GetInfoRect(const CRect& rect);
	CRect GetBarRect(const CRect& scaleRect);
	void GetRange(float& fLow, float& fHigh);
	int TemperatureToY(const CRect& barRect, float fTemperature);
	float YToTemperature(const CRect& barRect, int nY);
	void PaintScale(CDC& dc);
	void InvalidateInfo();
};
