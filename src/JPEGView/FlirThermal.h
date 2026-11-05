#pragma once

#include <vector>
#include <functional>

// Position in an image relative to its size, (0, 0) top left, (1, 1) bottom right
struct CThermalPoint {
	double X, Y;
};

// Measuring points and temperature range set by the user on a FLIR image. The positions are relative, so they stay
// at the same place when switching between the thermal view and the photo (both have the same size).
struct CThermalMeasurements {
	std::vector<CThermalPoint> Points;
	bool ShowMax, ShowMin; // markers at the hottest and the coldest spot
	bool CustomRange; // thermal view colored from the temperatures between RangeLow and RangeHigh instead of the camera image
	float RangeLow, RangeHigh;

	CThermalMeasurements() : ShowMax(false), ShowMin(false), CustomRange(false), RangeLow(0.0f), RangeHigh(0.0f) {}
	bool HasMarkers() const { return !Points.empty() || ShowMax || ShowMin; }
};

// Maps a relative image position to a pixel position on the target the markers are drawn on
typedef std::function<CPoint(double dRelX, double dRelY)> ThermalPositionMapper;

// Thermal data of radiometric FLIR JPEGs. The JPEG itself is the thermal view, APP1 segments starting with "FLIR"
// hold an FFF container with the photo of the visible light camera, the raw sensor values and the calibration
// needed to convert them to temperatures (see https://github.com/fclairec/FlirImageExtractor).
class CFlirThermalData
{
public:
	// Index of the frame JPEGView requests to show the visible light photo instead of the thermal view
	enum { PHOTO_FRAME_INDEX = 1 };

	// Returns NULL if the JPEG has no FLIR data. Cheap for other JPEGs (only the markers before the image data are looked at).
	static CFlirThermalData* Create(const void* pJPEG, int nJPEGSize);

	// The photo of the visible light camera as JPEG, empty if there is none
	const std::vector<unsigned char>& GetPhotoJPEG() const { return m_photoJPEG; }
	bool HasPhoto() const { return !m_photoJPEG.empty(); }

	// Small previews of the thermal view and the photo for the navigator, 32 bpp BGRA top-down, NULL if not available
	void CreateThumbnails(const void* pThermalJPEG, int nThermalJPEGSize);
	const void* GetThumbnail(bool bPhoto, CSize& size) const;

	// Temperatures in degrees Celsius, only valid if HasTemperatures()
	bool HasTemperatures() const { return !m_temperatures.empty(); }
	// Temperature at the relative position (0..1, 0..1) in the thermal view
	float GetTemperature(double dRelX, double dRelY) const;
	float GetMinTemperature() const { return m_fMinTemperature; }
	float GetMaxTemperature() const { return m_fMaxTemperature; }
	// Relative positions of the coldest and the hottest spot
	CThermalPoint GetMinPosition() const { return m_minPosition; }
	CThermalPoint GetMaxPosition() const { return m_maxPosition; }

	// Measuring points and range of this image (a copy of the state kept by the main window)
	CThermalMeasurements& GetMeasurements() { return m_measurements; }

	// Thermal image of the given size colored with the iron palette, fRangeLow is black, fRangeHigh white.
	// 32 bpp BGRA, the caller deletes it with delete[]. NULL if there are no temperatures.
	uint32* RenderThermalImage(int nWidth, int nHeight, float fRangeLow, float fRangeHigh) const;
	// Color of the iron palette for a value between 0 and 1
	static COLORREF GetPaletteColor(double dValue);

	// Draws the measuring points and the hottest/coldest markers with their temperatures. nMarkerSize is the radius
	// of the marker circles in pixels, the labels are sized accordingly.
	void DrawMeasurements(HDC hDC, const ThermalPositionMapper& mapper, int nMarkerSize, const CRect& clipRect) const;
	// Same on a 32 bpp DIB, e.g. for saving
	void DrawMeasurements(void* pDIB32bpp, int nWidth, int nHeight, const ThermalPositionMapper& mapper, int nMarkerSize) const;

	// Name of the camera, e.g. "FLIR Edge Pro"
	const CString& GetCameraModel() const { return m_sCameraModel; }

private:
	CFlirThermalData();

	std::vector<unsigned char> m_photoJPEG;
	std::vector<float> m_temperatures; // row by row, m_nRawWidth x m_nRawHeight
	int m_nRawWidth, m_nRawHeight;
	float m_fMinTemperature, m_fMaxTemperature;
	CThermalPoint m_minPosition, m_maxPosition;
	CThermalMeasurements m_measurements;
	CString m_sCameraModel;
	std::vector<unsigned char> m_thumbThermal, m_thumbPhoto;
	CSize m_thumbThermalSize, m_thumbPhotoSize;

	bool ParseFFF(const std::vector<unsigned char>& fff);
	bool ReadRawData(const unsigned char* pRecord, int nLength, bool bLittleEndian, std::vector<unsigned short>& raw);
	static bool DecodeThumbnail(const void* pJPEG, int nSize, std::vector<unsigned char>& pixels, CSize& size);
	void DrawMarker(HDC hDC, CPoint center, int nSize, COLORREF color, LPCTSTR sLabel, const CRect& clipRect) const;
};
