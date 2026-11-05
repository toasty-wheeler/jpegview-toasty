#pragma once

#include <vector>

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

	// Name of the camera, e.g. "FLIR Edge Pro"
	const CString& GetCameraModel() const { return m_sCameraModel; }

private:
	CFlirThermalData();

	std::vector<unsigned char> m_photoJPEG;
	std::vector<float> m_temperatures; // row by row, m_nRawWidth x m_nRawHeight
	int m_nRawWidth, m_nRawHeight;
	float m_fMinTemperature, m_fMaxTemperature;
	CString m_sCameraModel;
	std::vector<unsigned char> m_thumbThermal, m_thumbPhoto;
	CSize m_thumbThermalSize, m_thumbPhotoSize;

	bool ParseFFF(const std::vector<unsigned char>& fff);
	bool ReadRawData(const unsigned char* pRecord, int nLength, bool bLittleEndian, std::vector<unsigned short>& raw);
	static bool DecodeThumbnail(const void* pJPEG, int nSize, std::vector<unsigned char>& pixels, CSize& size);
};
