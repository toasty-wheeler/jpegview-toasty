#pragma once

#include <stdint.h>

// HDR photos (HEIF/HEIC/HIF, AVIF and JPEG XL with PQ or HLG transfer):
// conversion of the decoded code values to linear light and tone mapping for SDR displays.
namespace HDR {

	enum ETransfer {
		TRANSFER_SDR = 0, // not an HDR transfer function, the normal 8 bit decoding path is used
		TRANSFER_PQ,      // SMPTE ST 2084 (H.273 transfer characteristics 16)
		TRANSFER_HLG      // ARIB STD-B67 / BT.2100 HLG (H.273 transfer characteristics 18)
	};

	enum EPrimaries {
		PRIMARIES_BT709 = 0, // also sRGB
		PRIMARIES_BT2020,
		PRIMARIES_P3         // Display P3 / DCI-P3 with D65 white point
	};

	struct CSourceInfo {
		ETransfer Transfer;
		EPrimaries Primaries;
		float PeakNits; // brightest pixel of the content if known (MaxCLL, mastering display, intensity target), 0 if unknown

		CSourceInfo() : Transfer(TRANSFER_SDR), Primaries(PRIMARIES_BT709), PeakNits(0.0f) {}
		bool IsHDR() const { return Transfer != TRANSFER_SDR; }
	};

	// Maps H.273 (ISO/IEC 23091-2) code points as used by HEIF nclx, AVIF and JPEG XL
	ETransfer TransferFromH273(int transferCharacteristics);
	EPrimaries PrimariesFromH273(int colorPrimaries);

	// Reference white of HDR content (BT.2408): shown as white (255) on SDR displays
	const float SDR_WHITE_NITS = 203.0f;
	// Assumed peak brightness when the content does not tell
	const float DEFAULT_PEAK_NITS = 1000.0f;
	// Display luminance of HLG signal 1.0
	const float HLG_PEAK_NITS = 1000.0f;

	// Linear light HDR pixels, kept for the HDR display output.
	// RGBA half floats, BT.709 primaries, scRGB scale (1.0 = 80 nits), alpha straight (not premultiplied).
	class CLinearImage {
	public:
		CLinearImage(int width, int height, uint16_t* pPixels, float peakNits)
			: m_nWidth(width), m_nHeight(height), m_pPixels(pPixels), m_fPeakNits(peakNits), m_nId(NextId()) {}
		~CLinearImage() { delete[] m_pPixels; }

		int Width() const { return m_nWidth; }
		int Height() const { return m_nHeight; }
		const uint16_t* Pixels() const { return m_pPixels; }
		float PeakNits() const { return m_fPeakNits; }
		// Unique number of this image (to know when the HDR output needs a new texture)
		unsigned int Id() const { return m_nId; }
	private:
		int m_nWidth, m_nHeight;
		uint16_t* m_pPixels;
		float m_fPeakNits;
		unsigned int m_nId;

		static unsigned int NextId();

		CLinearImage(const CLinearImage&);
		CLinearImage& operator=(const CLinearImage&);
	};

	// Converts interleaved RGB(A) code values of an HDR image.
	// pSource: 16 bit unsigned samples, nChannels 3 or 4, code values 0..nMaxCode (e.g. 1023 for 10 bit), nStrideBytes per row
	// pBGRA: receives the tone-mapped 8 bit sRGB image (width * height * 4 bytes, alpha copied)
	// ppLinear: optional, receives the linear light image for the HDR display output (caller deletes)
	// Returns false when out of memory.
	bool ConvertToSDR(const uint16_t* pSource, int nWidth, int nHeight, int nChannels, int nMaxCode, size_t nStrideBytes,
		const CSourceInfo& info, uint8_t* pBGRA, CLinearImage** ppLinear);

	// Same for 32 bit float samples with code values 0..1 (JPEG XL)
	bool ConvertToSDR(const float* pSource, int nWidth, int nHeight, int nChannels, size_t nStrideBytes,
		const CSourceInfo& info, uint8_t* pBGRA, CLinearImage** ppLinear);

	// Tone curve used for SDR output and the HDR display output (BT.2390 EETF applied to max(R,G,B)):
	// maps nits of the content (peak fSourcePeakNits) into the range 0..fTargetPeakNits
	float ToneMapNits(float fNits, float fSourcePeakNits, float fTargetPeakNits);

	// Effective peak used for tone mapping: the known content peak or the default
	float EffectivePeakNits(const CSourceInfo& info);

	// Half float conversions
	uint16_t FloatToHalf(float f);
	float HalfToFloat(uint16_t h);

	// Exposed for tests
	float PQToNits(float codeValue);   // code value 0..1 -> nits
	float NitsToPQ(float nits);         // nits -> code value 0..1
	float HLGToSceneLinear(float codeValue); // code value 0..1 -> scene linear 0..1
}
