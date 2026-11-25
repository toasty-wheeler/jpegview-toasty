#include "StdAfx.h"
#include "HDRImage.h"
#include <math.h>
#include <string.h>
#include <vector>
#include <new>
#include <ppl.h>

namespace HDR {

unsigned int CLinearImage::NextId() {
	static volatile LONG s_nId = 0;
	return (unsigned int)::InterlockedIncrement(&s_nId);
}

// SMPTE ST 2084 constants
static const double PQ_M1 = 2610.0 / 16384.0;
static const double PQ_M2 = 2523.0 / 4096.0 * 128.0;
static const double PQ_C1 = 3424.0 / 4096.0;
static const double PQ_C2 = 2413.0 / 4096.0 * 32.0;
static const double PQ_C3 = 2392.0 / 4096.0 * 32.0;

// ARIB STD-B67 constants
static const double HLG_A = 0.17883277;
static const double HLG_B = 1.0 - 4.0 * HLG_A;
static const double HLG_C = 0.5 - HLG_A * log(4.0 * HLG_A);

static const float SCRGB_NITS = 80.0f; // scRGB 1.0

ETransfer TransferFromH273(int transferCharacteristics) {
	switch (transferCharacteristics) {
	case 16: return TRANSFER_PQ;
	case 18: return TRANSFER_HLG;
	default: return TRANSFER_SDR;
	}
}

EPrimaries PrimariesFromH273(int colorPrimaries) {
	switch (colorPrimaries) {
	case 9: return PRIMARIES_BT2020;
	case 11: // DCI-P3 (white point differs slightly, treated as D65)
	case 12: return PRIMARIES_P3;
	default: return PRIMARIES_BT709;
	}
}

float PQToNits(float codeValue) {
	if (codeValue <= 0.0f) return 0.0f;
	double p = pow((double)codeValue, 1.0 / PQ_M2);
	double num = p - PQ_C1;
	if (num < 0.0) num = 0.0;
	double den = PQ_C2 - PQ_C3 * p;
	if (den <= 0.0) return 10000.0f;
	return (float)(10000.0 * pow(num / den, 1.0 / PQ_M1));
}

float NitsToPQ(float nits) {
	if (nits <= 0.0f) return 0.0f;
	double y = pow((double)nits / 10000.0, PQ_M1);
	return (float)pow((PQ_C1 + PQ_C2 * y) / (1.0 + PQ_C3 * y), PQ_M2);
}

float HLGToSceneLinear(float codeValue) {
	if (codeValue <= 0.0f) return 0.0f;
	if (codeValue <= 0.5f) return codeValue * codeValue / 3.0f;
	return (float)((exp((codeValue - HLG_C) / HLG_A) + HLG_B) / 12.0);
}

float EffectivePeakNits(const CSourceInfo& info) {
	if (info.Transfer == TRANSFER_HLG) return HLG_PEAK_NITS;
	float peak = (info.PeakNits > 0.0f) ? info.PeakNits : DEFAULT_PEAK_NITS;
	if (peak < SDR_WHITE_NITS) peak = SDR_WHITE_NITS;
	if (peak > 10000.0f) peak = 10000.0f;
	return peak;
}

float ToneMapNits(float fNits, float fSourcePeakNits, float fTargetPeakNits) {
	if (fNits <= 0.0f) return 0.0f;
	if (fSourcePeakNits <= fTargetPeakNits) {
		return (fNits < fTargetPeakNits) ? fNits : fTargetPeakNits;
	}
	// ITU-R BT.2390 EETF (without black level lift), evaluated in the PQ domain
	float srcPQ = NitsToPQ(fSourcePeakNits);
	float e1 = NitsToPQ(fNits) / srcPQ;
	if (e1 > 1.0f) e1 = 1.0f;
	float maxLum = NitsToPQ(fTargetPeakNits) / srcPQ;
	float ks = 1.5f * maxLum - 0.5f;
	if (ks < 0.0f) ks = 0.0f;
	float e2 = e1;
	if (e1 > ks) {
		float t = (e1 - ks) / (1.0f - ks);
		float t2 = t * t, t3 = t2 * t;
		e2 = (2 * t3 - 3 * t2 + 1) * ks + (t3 - 2 * t2 + t) * (1.0f - ks) + (-2 * t3 + 3 * t2) * maxLum;
	}
	float result = PQToNits(e2 * srcPQ);
	return (result < fTargetPeakNits) ? result : fTargetPeakNits;
}

uint16_t FloatToHalf(float f) {
	uint32_t x;
	memcpy(&x, &f, 4);
	uint32_t sign = (x >> 16) & 0x8000;
	int32_t exponent = (int32_t)((x >> 23) & 0xFF) - 127 + 15;
	uint32_t mantissa = x & 0x007FFFFF;
	if (((x >> 23) & 0xFF) == 0xFF) return (uint16_t)(sign | 0x7C00 | (mantissa ? 0x200 : 0)); // inf / nan
	if (exponent >= 31) return (uint16_t)(sign | 0x7BFF); // clamp to the largest finite value
	if (exponent <= 0) {
		if (exponent < -10) return (uint16_t)sign;
		mantissa |= 0x00800000; // denormal
		uint32_t shift = (uint32_t)(14 - exponent);
		uint32_t half = mantissa >> shift;
		if ((mantissa >> (shift - 1)) & 1) half++; // round
		return (uint16_t)(sign | half);
	}
	uint32_t half = sign | ((uint32_t)exponent << 10) | (mantissa >> 13);
	if (mantissa & 0x00001000) half++; // round to nearest (carry into the exponent is correct)
	return (uint16_t)half;
}

float HalfToFloat(uint16_t h) {
	uint32_t sign = (uint32_t)(h & 0x8000) << 16;
	uint32_t exponent = (h >> 10) & 0x1F;
	uint32_t mantissa = h & 0x3FF;
	uint32_t bits;
	if (exponent == 0) {
		if (mantissa == 0) {
			bits = sign;
		} else { // denormal
			exponent = 127 - 15 + 1;
			while ((mantissa & 0x400) == 0) { mantissa <<= 1; exponent--; }
			mantissa &= 0x3FF;
			bits = sign | (exponent << 23) | (mantissa << 13);
		}
	} else if (exponent == 31) {
		bits = sign | 0x7F800000 | (mantissa << 13);
	} else {
		bits = sign | ((exponent - 15 + 127) << 23) | (mantissa << 13);
	}
	float f;
	memcpy(&f, &bits, 4);
	return f;
}

namespace {

	// Lookup table indexed by the upper bits of positive floats (exponent + 10 mantissa bits):
	// relative precision of 1/1024 over the range [2^MinExp, 2^MaxExp)
	template<int MinExp, int MaxExp>
	class CFloatLUT {
	public:
		static const int SIZE = (MaxExp - MinExp) * 1024;
		std::vector<float> Values;

		template<typename F> void Build(F f) {
			Values.resize(SIZE);
			for (int i = 0; i < SIZE; i++) {
				uint32_t bits = (uint32_t)(MinExp + 127) << 23;
				bits += (uint32_t)i << 13;
				bits += 1 << 12; // middle of the bucket
				float x;
				memcpy(&x, &bits, 4);
				Values[i] = f(x);
			}
		}
		// returns -1 below the range, SIZE-1 above
		static int Index(float x) {
			uint32_t bits;
			memcpy(&bits, &x, 4);
			int idx = (int)(bits >> 13) - ((MinExp + 127) << 10);
			if (idx < 0) return -1;
			return (idx < SIZE) ? idx : SIZE - 1;
		}
	};

	// sRGB encoding of linear 0..1 to 0..255
	class CSRGBEncoder {
	public:
		CSRGBEncoder() {
			m_lut.Build([](float v) {
				if (v >= 1.0f) return 255.0f;
				float e = (v <= 0.0031308f) ? 12.92f * v : 1.055f * powf(v, 1.0f / 2.4f) - 0.055f;
				return e * 255.0f + 0.5f;
			});
		}
		uint8_t Encode(float v) const {
			if (v >= 1.0f) return 255;
			int idx = CFloatLUT<-16, 0>::Index(v);
			if (idx < 0) return (uint8_t)(v > 0.0f ? (12.92f * v * 255.0f + 0.5f) : 0.0f);
			return (uint8_t)m_lut.Values[idx];
		}
	private:
		CFloatLUT<-16, 0> m_lut;
	};

	struct CMatrix3 { float m[9]; };

	// Linear RGB of the source primaries -> linear BT.709 / sRGB primaries
	const CMatrix3& PrimariesToBT709(EPrimaries primaries, bool& isIdentity) {
		static const CMatrix3 identity = { { 1, 0, 0, 0, 1, 0, 0, 0, 1 } };
		static const CMatrix3 bt2020 = { { 1.660491f, -0.587641f, -0.072850f, -0.124550f, 1.132900f, -0.008349f, -0.018151f, -0.100579f, 1.118730f } };
		static const CMatrix3 p3 = { { 1.224940f, -0.224940f, 0.0f, -0.042057f, 1.042057f, 0.0f, -0.019638f, -0.078636f, 1.098274f } };
		isIdentity = (primaries == PRIMARIES_BT709);
		switch (primaries) {
		case PRIMARIES_BT2020: return bt2020;
		case PRIMARIES_P3: return p3;
		default: return identity;
		}
	}

	// Luminance coefficients of the source primaries (for the HLG OOTF)
	void LumaCoefficients(EPrimaries primaries, float& kr, float& kg, float& kb) {
		switch (primaries) {
		case PRIMARIES_BT2020: kr = 0.2627f; kg = 0.6780f; kb = 0.0593f; break;
		case PRIMARIES_P3: kr = 0.2290f; kg = 0.6917f; kb = 0.0793f; break;
		default: kr = 0.2126f; kg = 0.7152f; kb = 0.0722f; break;
		}
	}

	// Shared conversion; GetSample(row pointer, index) returns the code value as index into the decode LUT (0..nMaxCode)
	template<typename TSample, typename TToIndex>
	bool Convert(const TSample* pSource, int nWidth, int nHeight, int nChannels, int nMaxCode, size_t nStrideBytes,
		const CSourceInfo& info, uint8_t* pBGRA, CLinearImage** ppLinear, TToIndex toIndex) {

		if (ppLinear != NULL) *ppLinear = NULL;
		if (nChannels != 3 && nChannels != 4) return false;

		// code value -> linear (PQ: display nits, HLG: scene linear 0..1)
		std::vector<float> decodeLUT;
		try {
			decodeLUT.resize((size_t)nMaxCode + 1);
		} catch (...) {
			return false;
		}
		bool isPQ = info.Transfer == TRANSFER_PQ;
		for (int i = 0; i <= nMaxCode; i++) {
			float c = (float)i / nMaxCode;
			decodeLUT[i] = isPQ ? PQToNits(c) : HLGToSceneLinear(c);
		}

		float peakNits = EffectivePeakNits(info);
		// tone mapping: max(R,G,B) in nits -> scale factor to multiply R, G, B with, result 0..1 for SDR white
		CFloatLUT<-14, 14> toneLUT;
		toneLUT.Build([peakNits](float nits) { return ToneMapNits(nits, peakNits, SDR_WHITE_NITS) / nits / SDR_WHITE_NITS; });
		const float darkScale = 1.0f / SDR_WHITE_NITS; // tone curve is linear in the deep shadows

		// HLG OOTF: Fd = Lw * Ys^(gamma - 1) * E with gamma 1.2 for a 1000 nits display
		CFloatLUT<-24, 0> hlgGainLUT;
		float kr = 0, kg = 0, kb = 0;
		if (!isPQ) {
			hlgGainLUT.Build([](float ys) { return HLG_PEAK_NITS * powf(ys, 0.2f); });
			LumaCoefficients(info.Primaries, kr, kg, kb);
		}

		bool isIdentity;
		const CMatrix3& mat = PrimariesToBT709(info.Primaries, isIdentity);
		static const CSRGBEncoder s_encoder;

		uint16_t* pHalf = NULL;
		if (ppLinear != NULL) {
			pHalf = new(std::nothrow) uint16_t[(size_t)nWidth * nHeight * 4];
			// without memory for the HDR output the SDR image is still produced
		}

		const int ROWS_PER_TASK = 16;
		int nTasks = (nHeight + ROWS_PER_TASK - 1) / ROWS_PER_TASK;
		concurrency::parallel_for(0, nTasks, [&](int task) {
			int yEnd = min(nHeight, (task + 1) * ROWS_PER_TASK);
			for (int y = task * ROWS_PER_TASK; y < yEnd; y++) {
				const TSample* pRow = (const TSample*)((const uint8_t*)pSource + y * nStrideBytes);
				uint8_t* pOut = pBGRA + (size_t)y * nWidth * 4;
				uint16_t* pHalfOut = pHalf ? pHalf + (size_t)y * nWidth * 4 : NULL;
				for (int x = 0; x < nWidth; x++) {
					const TSample* s = pRow + (size_t)x * nChannels;
					float r = decodeLUT[toIndex(s[0])];
					float g = decodeLUT[toIndex(s[1])];
					float b = decodeLUT[toIndex(s[2])];
					float alpha = (nChannels == 4) ? (float)toIndex(s[3]) / nMaxCode : 1.0f;
					if (!isPQ) {
						float ys = kr * r + kg * g + kb * b;
						int gi = CFloatLUT<-24, 0>::Index(ys);
						float gain = (gi < 0) ? 0.0f : hlgGainLUT.Values[gi];
						r *= gain; g *= gain; b *= gain;
					}
					if (!isIdentity) {
						float r2 = mat.m[0] * r + mat.m[1] * g + mat.m[2] * b;
						float g2 = mat.m[3] * r + mat.m[4] * g + mat.m[5] * b;
						float b2 = mat.m[6] * r + mat.m[7] * g + mat.m[8] * b;
						r = r2; g = g2; b = b2;
					}
					if (pHalfOut) {
						pHalfOut[0] = FloatToHalf(r / SCRGB_NITS);
						pHalfOut[1] = FloatToHalf(g / SCRGB_NITS);
						pHalfOut[2] = FloatToHalf(b / SCRGB_NITS);
						pHalfOut[3] = FloatToHalf(alpha);
						pHalfOut += 4;
					}
					// out of gamut colors are clipped for SDR
					if (r < 0.0f) r = 0.0f;
					if (g < 0.0f) g = 0.0f;
					if (b < 0.0f) b = 0.0f;
					float m = max(r, max(g, b));
					int ti = CFloatLUT<-14, 14>::Index(m);
					float scale = (ti < 0) ? darkScale : toneLUT.Values[ti];
					pOut[0] = s_encoder.Encode(b * scale);
					pOut[1] = s_encoder.Encode(g * scale);
					pOut[2] = s_encoder.Encode(r * scale);
					pOut[3] = (uint8_t)(alpha * 255.0f + 0.5f);
					pOut += 4;
				}
			}
		});

		if (pHalf != NULL) {
			*ppLinear = new(std::nothrow) CLinearImage(nWidth, nHeight, pHalf, peakNits);
			if (*ppLinear == NULL) delete[] pHalf;
		}
		return true;
	}
}

bool ConvertToSDR(const uint16_t* pSource, int nWidth, int nHeight, int nChannels, int nMaxCode, size_t nStrideBytes,
	const CSourceInfo& info, uint8_t* pBGRA, CLinearImage** ppLinear) {
	if (nMaxCode < 1 || nMaxCode > 65535) return false;
	return Convert(pSource, nWidth, nHeight, nChannels, nMaxCode, nStrideBytes, info, pBGRA, ppLinear,
		[nMaxCode](uint16_t v) { return (v <= nMaxCode) ? (int)v : nMaxCode; });
}

bool ConvertToSDR(const float* pSource, int nWidth, int nHeight, int nChannels, size_t nStrideBytes,
	const CSourceInfo& info, uint8_t* pBGRA, CLinearImage** ppLinear) {
	// float code values are quantized to 16 bits, well below the precision of any HDR source
	static const int MAX_CODE = 65535;
	return Convert(pSource, nWidth, nHeight, nChannels, MAX_CODE, nStrideBytes, info, pBGRA, ppLinear,
		[](float v) { return (v <= 0.0f) ? 0 : (v >= 1.0f) ? MAX_CODE : (int)(v * MAX_CODE + 0.5f); });
}

}
