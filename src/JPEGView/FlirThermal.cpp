#include "StdAfx.h"
#include "FlirThermal.h"
#include "png.h"
#include "libjpeg-turbo\include\turbojpeg.h"
#include <map>
#include <math.h>

// FFF record types
static const int FFF_RAW_DATA = 0x01;
static const int FFF_EMBEDDED_IMAGE = 0x0E;
static const int FFF_CAMERA_INFO = 0x20;

// The thumbnails are decoded scaled down by turbojpeg, at least this high
static const int THUMBNAIL_MIN_HEIGHT = 240;

static unsigned int BE32(const unsigned char* p) { return ((unsigned int)p[0] << 24) | ((unsigned int)p[1] << 16) | ((unsigned int)p[2] << 8) | p[3]; }
static unsigned int BE16(const unsigned char* p) { return ((unsigned int)p[0] << 8) | p[1]; }

static unsigned int Read16(const unsigned char* p, bool bLittleEndian) { return bLittleEndian ? (p[0] | ((unsigned int)p[1] << 8)) : BE16(p); }

static float ReadFloat(const unsigned char* p, bool bLittleEndian) {
	unsigned char bytes[4];
	for (int i = 0; i < 4; i++) bytes[i] = bLittleEndian ? p[i] : p[3 - i];
	float f;
	memcpy(&f, bytes, 4);
	return f;
}

static int ReadInt32(const unsigned char* p, bool bLittleEndian) {
	unsigned char bytes[4];
	for (int i = 0; i < 4; i++) bytes[i] = bLittleEndian ? p[i] : p[3 - i];
	int n;
	memcpy(&n, bytes, 4);
	return n;
}

CFlirThermalData::CFlirThermalData() {
	m_nRawWidth = m_nRawHeight = 0;
	m_fMinTemperature = m_fMaxTemperature = 0.0f;
	m_thumbThermalSize = m_thumbPhotoSize = CSize(0, 0);
}

CFlirThermalData* CFlirThermalData::Create(const void* pJPEG, int nJPEGSize) {
	const unsigned char* pData = (const unsigned char*)pJPEG;
	if (pData == NULL || nJPEGSize < 4 || pData[0] != 0xFF || pData[1] != 0xD8) {
		return NULL;
	}

	// collect the FLIR APP1 segments, they hold the FFF container split into numbered chunks
	std::map<int, std::pair<int, int>> chunks; // chunk number -> offset and length of its data
	int nPos = 2;
	while (nPos + 4 <= nJPEGSize && pData[nPos] == 0xFF) {
		unsigned char marker = pData[nPos + 1];
		if (marker == 0xFF) { nPos++; continue; } // fill byte
		if (marker == 0xDA || marker == 0xD9) break; // image data starts, no more metadata
		int nLength = (int)BE16(pData + nPos + 2);
		if (nLength < 2 || nPos + 2 + nLength > nJPEGSize) break;
		if (marker == 0xE1 && nLength >= 2 + 8 && memcmp(pData + nPos + 4, "FLIR\0", 5) == 0) {
			int nChunk = pData[nPos + 4 + 6];
			chunks[nChunk] = std::make_pair(nPos + 4 + 8, nLength - 2 - 8);
		}
		nPos += 2 + nLength;
	}
	if (chunks.empty()) {
		return NULL;
	}

	std::vector<unsigned char> fff;
	for (std::map<int, std::pair<int, int>>::const_iterator iter = chunks.begin(); iter != chunks.end(); iter++) {
		fff.insert(fff.end(), pData + iter->second.first, pData + iter->second.first + iter->second.second);
	}
	CFlirThermalData* pThermalData = new CFlirThermalData();
	try {
		if (!pThermalData->ParseFFF(fff) || (!pThermalData->HasPhoto() && !pThermalData->HasTemperatures())) {
			delete pThermalData;
			return NULL;
		}
	} catch (...) {
		delete pThermalData;
		return NULL;
	}
	return pThermalData;
}

bool CFlirThermalData::ParseFFF(const std::vector<unsigned char>& fff) {
	if (fff.size() < 64 || (memcmp(&fff[0], "FFF\0", 4) != 0 && memcmp(&fff[0], "AFF\0", 4) != 0)) {
		return false;
	}
	const unsigned char* pFFF = &fff[0];
	int nSize = (int)fff.size();
	unsigned int nIndexOffset = BE32(pFFF + 24);
	unsigned int nEntries = BE32(pFFF + 28);
	if (nEntries > 1000 || nIndexOffset + nEntries * 32 > (unsigned int)nSize) {
		return false;
	}

	std::vector<unsigned short> raw;
	const unsigned char* pCameraInfo = NULL;
	int nCameraInfoLength = 0;
	for (unsigned int i = 0; i < nEntries; i++) {
		const unsigned char* pEntry = pFFF + nIndexOffset + i * 32;
		int nType = (int)BE16(pEntry);
		unsigned int nOffset = BE32(pEntry + 12);
		unsigned int nLength = BE32(pEntry + 16);
		if (nType == 0 || nLength < 32 || nOffset + nLength > (unsigned int)nSize) {
			continue;
		}
		const unsigned char* pRecord = pFFF + nOffset;
		// the byte order of a record is told by its first word (2 for raw data and camera info)
		bool bLittleEndian = pRecord[1] == 0;
		if (nType == FFF_EMBEDDED_IMAGE && pRecord[32] == 0xFF && pRecord[33] == 0xD8) {
			m_photoJPEG.assign(pRecord + 32, pRecord + nLength);
		} else if (nType == FFF_RAW_DATA) {
			ReadRawData(pRecord, (int)nLength, bLittleEndian, raw);
		} else if (nType == FFF_CAMERA_INFO && nLength >= 0x310) {
			pCameraInfo = pRecord;
			nCameraInfoLength = (int)nLength;
		}
	}

	if (pCameraInfo != NULL) {
		char model[33] = { 0 };
		memcpy(model, pCameraInfo + 0xD4, 32);
		m_sCameraModel = CString(model);
	}
	if (raw.empty() || pCameraInfo == NULL) {
		return true; // photo only
	}

	// Convert the raw sensor values to temperatures, formula of FlirImageExtractor (raw2temp), temperatures in the file are in Kelvin
	bool bLE = pCameraInfo[1] == 0;
	double E = ReadFloat(pCameraInfo + 0x20, bLE);
	double OD = ReadFloat(pCameraInfo + 0x24, bLE);
	double RTemp = ReadFloat(pCameraInfo + 0x28, bLE) - 273.15;
	double ATemp = ReadFloat(pCameraInfo + 0x2C, bLE) - 273.15;
	double IRWTemp = ReadFloat(pCameraInfo + 0x30, bLE) - 273.15;
	double IRT = ReadFloat(pCameraInfo + 0x34, bLE);
	double RH = ReadFloat(pCameraInfo + 0x3C, bLE);
	double PR1 = ReadFloat(pCameraInfo + 0x58, bLE);
	double PB = ReadFloat(pCameraInfo + 0x5C, bLE);
	double PF = ReadFloat(pCameraInfo + 0x60, bLE);
	double ATA1 = ReadFloat(pCameraInfo + 0x70, bLE);
	double ATA2 = ReadFloat(pCameraInfo + 0x74, bLE);
	double ATB1 = ReadFloat(pCameraInfo + 0x78, bLE);
	double ATB2 = ReadFloat(pCameraInfo + 0x7C, bLE);
	double ATX = ReadFloat(pCameraInfo + 0x80, bLE);
	double PO = ReadInt32(pCameraInfo + 0x308, bLE);
	double PR2 = ReadFloat(pCameraInfo + 0x30C, bLE);
	if (RH > 1.0) RH /= 100.0; // some cameras store percent
	if (E <= 0 || E > 1) E = 1.0;
	if (IRT <= 0 || IRT > 1) IRT = 1.0;
	if (OD < 0) OD = 0;
	if (PR1 == 0 || PR2 == 0 || PB == 0) {
		return true; // no usable calibration
	}

	double dEmissWind = 1 - IRT;
	double dReflWind = 0;
	double dH2O = RH * exp(1.5587 + 0.06939 * ATemp - 0.00027816 * ATemp * ATemp + 0.00000068455 * ATemp * ATemp * ATemp);
	double dSqrtDist = sqrt(OD / 2);
	double dTau1 = ATX * exp(-dSqrtDist * (ATA1 + ATB1 * sqrt(dH2O))) + (1 - ATX) * exp(-dSqrtDist * (ATA2 + ATB2 * sqrt(dH2O)));
	double dTau2 = dTau1;
	double dRawRefl1 = PR1 / (PR2 * (exp(PB / (RTemp + 273.15)) - PF)) - PO;
	double dRawRefl1Attn = (1 - E) / E * dRawRefl1;
	double dRawAtm1 = PR1 / (PR2 * (exp(PB / (ATemp + 273.15)) - PF)) - PO;
	double dRawAtm1Attn = (1 - dTau1) / E / dTau1 * dRawAtm1;
	double dRawWind = PR1 / (PR2 * (exp(PB / (IRWTemp + 273.15)) - PF)) - PO;
	double dRawWindAttn = dEmissWind / E / dTau1 / IRT * dRawWind;
	double dRawRefl2Attn = dReflWind / E / dTau1 / IRT * dRawRefl1;
	double dRawAtm2Attn = (1 - dTau2) / E / dTau1 / IRT / dTau2 * dRawAtm1;
	// raw_obj = raw * dFactor - dSubtract
	double dFactor = 1.0 / E / dTau1 / IRT / dTau2;
	double dSubtract = dRawAtm1Attn + dRawAtm2Attn + dRawWindAttn + dRawRefl1Attn + dRawRefl2Attn;

	// the same raw value always gives the same temperature, so convert each value only once
	std::vector<float> lookup(65536);
	for (int nRaw = 0; nRaw < 65536; nRaw++) {
		double dArg = PR1 / (PR2 * (nRaw * dFactor - dSubtract + PO)) + PF;
		lookup[nRaw] = (dArg > 0 && dArg != 1) ? (float)(PB / log(dArg) - 273.15) : -273.15f;
	}
	m_temperatures.resize(raw.size());
	m_fMinTemperature = 1e9f;
	m_fMaxTemperature = -1e9f;
	for (size_t i = 0; i < raw.size(); i++) {
		float fTemp = lookup[raw[i]];
		m_temperatures[i] = fTemp;
		if (fTemp < m_fMinTemperature) m_fMinTemperature = fTemp;
		if (fTemp > m_fMaxTemperature) m_fMaxTemperature = fTemp;
	}
	return true;
}

// The raw data is a 16 bit gray PNG (with byte swapped values) or plain 16 bit values after the 32 byte record header
bool CFlirThermalData::ReadRawData(const unsigned char* pRecord, int nLength, bool bLittleEndian, std::vector<unsigned short>& raw) {
	int nWidth = (int)Read16(pRecord + 2, bLittleEndian);
	int nHeight = (int)Read16(pRecord + 4, bLittleEndian);
	if (nWidth <= 0 || nHeight <= 0 || nWidth > 4096 || nHeight > 4096) {
		return false;
	}
	const unsigned char* pData = pRecord + 32;
	int nDataLength = nLength - 32;
	static const unsigned char pngSignature[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };

	if (nDataLength >= 8 && memcmp(pData, pngSignature, 8) == 0) {
		png_structp png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
		png_infop info_ptr = png_ptr ? png_create_info_struct(png_ptr) : NULL;
		if (info_ptr == NULL) {
			if (png_ptr) png_destroy_read_struct(&png_ptr, NULL, NULL);
			return false;
		}
		struct CMemoryReader { const unsigned char* Data; int Length; int Position; } reader = { pData, nDataLength, 0 };
		std::vector<unsigned char> row;
		if (setjmp(png_jmpbuf(png_ptr))) {
			png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
			raw.clear();
			return false;
		}
		png_set_read_fn(png_ptr, &reader, [](png_structp png_ptr, png_bytep outbuffer, png_size_t sizebytes) {
			CMemoryReader* pReader = (CMemoryReader*)png_get_io_ptr(png_ptr);
			if (pReader->Position + (int)sizebytes > pReader->Length) png_error(png_ptr, "read out of bounds");
			memcpy(outbuffer, pReader->Data + pReader->Position, sizebytes);
			pReader->Position += (int)sizebytes;
		});
		png_read_info(png_ptr, info_ptr);
		if (png_get_bit_depth(png_ptr, info_ptr) != 16 || png_get_color_type(png_ptr, info_ptr) != PNG_COLOR_TYPE_GRAY ||
			(int)png_get_image_width(png_ptr, info_ptr) != nWidth || (int)png_get_image_height(png_ptr, info_ptr) != nHeight ||
			png_get_interlace_type(png_ptr, info_ptr) != PNG_INTERLACE_NONE) {
			png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
			return false;
		}
		raw.resize(nWidth * nHeight);
		row.resize(nWidth * 2);
		for (int y = 0; y < nHeight; y++) {
			png_read_row(png_ptr, &row[0], NULL);
			// the PNG stores the sensor values with swapped bytes
			for (int x = 0; x < nWidth; x++) {
				raw[y * nWidth + x] = (unsigned short)(row[2 * x] | (row[2 * x + 1] << 8));
			}
		}
		png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
		m_nRawWidth = nWidth;
		m_nRawHeight = nHeight;
		return true;
	}

	if (nDataLength >= nWidth * nHeight * 2) {
		raw.resize(nWidth * nHeight);
		for (int i = 0; i < nWidth * nHeight; i++) {
			raw[i] = (unsigned short)Read16(pData + 2 * i, bLittleEndian);
		}
		m_nRawWidth = nWidth;
		m_nRawHeight = nHeight;
		return true;
	}
	return false;
}

float CFlirThermalData::GetTemperature(double dRelX, double dRelY) const {
	if (m_temperatures.empty()) {
		return 0.0f;
	}
	int nX = max(0, min(m_nRawWidth - 1, (int)(dRelX * m_nRawWidth)));
	int nY = max(0, min(m_nRawHeight - 1, (int)(dRelY * m_nRawHeight)));
	return m_temperatures[nY * m_nRawWidth + nX];
}

void CFlirThermalData::CreateThumbnails(const void* pThermalJPEG, int nThermalJPEGSize) {
	DecodeThumbnail(pThermalJPEG, nThermalJPEGSize, m_thumbThermal, m_thumbThermalSize);
	if (HasPhoto()) {
		DecodeThumbnail(&m_photoJPEG[0], (int)m_photoJPEG.size(), m_thumbPhoto, m_thumbPhotoSize);
	}
}

const void* CFlirThermalData::GetThumbnail(bool bPhoto, CSize& size) const {
	const std::vector<unsigned char>& pixels = bPhoto ? m_thumbPhoto : m_thumbThermal;
	size = bPhoto ? m_thumbPhotoSize : m_thumbThermalSize;
	return pixels.empty() ? NULL : &pixels[0];
}

// Decodes a JPEG scaled down (fast, turbojpeg skips most of the work) to 32 bpp BGRA
bool CFlirThermalData::DecodeThumbnail(const void* pJPEG, int nSize, std::vector<unsigned char>& pixels, CSize& size) {
	tjhandle hDecoder = tj3Init(TJINIT_DECOMPRESS);
	if (hDecoder == NULL) {
		return false;
	}
	bool bSuccess = false;
	if (tj3DecompressHeader(hDecoder, (const unsigned char*)pJPEG, nSize) == 0) {
		int nWidth = tj3Get(hDecoder, TJPARAM_JPEGWIDTH);
		int nHeight = tj3Get(hDecoder, TJPARAM_JPEGHEIGHT);
		tjscalingfactor scale = { 1, 1 };
		for (int nDenominator = 8; nDenominator >= 1; nDenominator /= 2) {
			tjscalingfactor candidate = { 1, nDenominator };
			if (TJSCALED(nHeight, candidate) >= THUMBNAIL_MIN_HEIGHT || nDenominator == 1) {
				scale = candidate;
				break;
			}
		}
		if (nWidth > 0 && nHeight > 0 && tj3SetScalingFactor(hDecoder, scale) == 0) {
			int nScaledWidth = TJSCALED(nWidth, scale);
			int nScaledHeight = TJSCALED(nHeight, scale);
			pixels.resize(nScaledWidth * nScaledHeight * 4);
			if (tj3Decompress8(hDecoder, (const unsigned char*)pJPEG, nSize, &pixels[0], nScaledWidth * 4, TJPF_BGRX) == 0) {
				size = CSize(nScaledWidth, nScaledHeight);
				bSuccess = true;
			} else {
				pixels.clear();
			}
		}
	}
	tj3Destroy(hDecoder);
	return bSuccess;
}
