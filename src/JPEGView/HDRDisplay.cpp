// Compiled without the precompiled header: stdafx.h targets Windows XP and hides the display configuration API
#define WINVER 0x0A00
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include "HDRDisplay.h"
#include "HDRImage.h"
#include <d3d11.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <atomic>
#include <mutex>
#include <vector>
#include <math.h>
#include <stdio.h>

// Direct3D, DXGI and the shader compiler are loaded when an HDR photo is shown for the first time,
// they are never loaded for normal images.

namespace HDR {

namespace {

	typedef HRESULT(WINAPI* PFN_CreateDXGIFactory1)(REFIID riid, void** ppFactory);
	typedef HRESULT(WINAPI* PFN_D3DCompile)(LPCVOID pSrcData, SIZE_T SrcDataSize, LPCSTR pSourceName, const D3D_SHADER_MACRO* pDefines,
		ID3DInclude* pInclude, LPCSTR pEntrypoint, LPCSTR pTarget, UINT Flags1, UINT Flags2, ID3DBlob** ppCode, ID3DBlob** ppErrorMsgs);

	const wchar_t* SURFACE_CLASS = L"JPEGViewHDRSurface";
	const float SCRGB_NITS = 80.0f; // scRGB 1.0

	template<class T> void SafeRelease(T*& p) {
		if (p != NULL) {
			p->Release();
			p = NULL;
		}
	}

	HMODULE LoadSystemLibrary(const wchar_t* sName) {
		return ::LoadLibraryExW(sName, NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
	}

	struct CMonitorState {
		HMONITOR Monitor;
		bool Valid;
		bool HDR;           // Windows HDR is on
		float MaxNits;      // peak brightness of the display, 0 if unknown
		float SDRWhiteNits; // brightness of SDR white (Windows setting for SDR content)
	};

	std::atomic<int> s_eMode(DISPLAY_AUTO);
	std::atomic<HWND> s_hMainWnd(NULL);
	std::mutex s_lock;
	CMonitorState s_monitor = { NULL, false, false, 0.0f, SCRGB_NITS };

	float QuerySDRWhiteNits(HMONITOR hMonitor) {
		MONITORINFOEXW monitorInfo;
		monitorInfo.cbSize = sizeof(monitorInfo);
		if (!::GetMonitorInfoW(hMonitor, &monitorInfo)) {
			return SCRGB_NITS;
		}
		UINT32 nPaths = 0, nModes = 0;
		if (::GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &nPaths, &nModes) != ERROR_SUCCESS || nPaths == 0) {
			return SCRGB_NITS;
		}
		std::vector<DISPLAYCONFIG_PATH_INFO> paths(nPaths);
		std::vector<DISPLAYCONFIG_MODE_INFO> modes(nModes > 0 ? nModes : 1);
		if (::QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &nPaths, paths.data(), &nModes, modes.data(), NULL) != ERROR_SUCCESS) {
			return SCRGB_NITS;
		}
		for (UINT32 i = 0; i < nPaths; i++) {
			DISPLAYCONFIG_SOURCE_DEVICE_NAME source = {};
			source.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
			source.header.size = sizeof(source);
			source.header.adapterId = paths[i].sourceInfo.adapterId;
			source.header.id = paths[i].sourceInfo.id;
			if (::DisplayConfigGetDeviceInfo(&source.header) != ERROR_SUCCESS || wcscmp(source.viewGdiDeviceName, monitorInfo.szDevice) != 0) {
				continue;
			}
			DISPLAYCONFIG_SDR_WHITE_LEVEL white = {};
			white.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SDR_WHITE_LEVEL;
			white.header.size = sizeof(white);
			white.header.adapterId = paths[i].targetInfo.adapterId;
			white.header.id = paths[i].targetInfo.id;
			if (::DisplayConfigGetDeviceInfo(&white.header) == ERROR_SUCCESS && white.SDRWhiteLevel > 0) {
				return white.SDRWhiteLevel / 1000.0f * SCRGB_NITS; // 1000 means 80 nits
			}
		}
		return SCRGB_NITS;
	}

	CMonitorState QueryMonitor(HMONITOR hMonitor) {
		CMonitorState state = { hMonitor, true, false, 0.0f, SCRGB_NITS };
		static HMODULE s_hDXGI = LoadSystemLibrary(L"dxgi.dll");
		PFN_CreateDXGIFactory1 pCreateFactory = (s_hDXGI != NULL) ? (PFN_CreateDXGIFactory1)::GetProcAddress(s_hDXGI, "CreateDXGIFactory1") : NULL;
		IDXGIFactory1* pFactory = NULL;
		if (pCreateFactory != NULL && SUCCEEDED(pCreateFactory(__uuidof(IDXGIFactory1), (void**)&pFactory))) {
			bool bFound = false;
			IDXGIAdapter1* pAdapter = NULL;
			for (UINT nAdapter = 0; !bFound && pFactory->EnumAdapters1(nAdapter, &pAdapter) != DXGI_ERROR_NOT_FOUND; nAdapter++) {
				IDXGIOutput* pOutput = NULL;
				for (UINT nOutput = 0; !bFound && pAdapter->EnumOutputs(nOutput, &pOutput) != DXGI_ERROR_NOT_FOUND; nOutput++) {
					IDXGIOutput6* pOutput6 = NULL;
					if (SUCCEEDED(pOutput->QueryInterface(__uuidof(IDXGIOutput6), (void**)&pOutput6))) {
						DXGI_OUTPUT_DESC1 desc;
						if (SUCCEEDED(pOutput6->GetDesc1(&desc)) && desc.Monitor == hMonitor) {
							state.HDR = desc.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
							state.MaxNits = desc.MaxLuminance;
							bFound = true;
						}
						pOutput6->Release();
					}
					pOutput->Release();
				}
				pAdapter->Release();
			}
			pFactory->Release();
		}
		state.SDRWhiteNits = QuerySDRWhiteNits(hMonitor);
		return state;
	}

	CMonitorState GetMonitorState() {
		HWND hWnd = s_hMainWnd;
		POINT origin = { 0, 0 };
		HMONITOR hMonitor = (hWnd != NULL) ? ::MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST) : ::MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY);
		{
			std::lock_guard<std::mutex> lock(s_lock);
			if (s_monitor.Valid && s_monitor.Monitor == hMonitor) {
				return s_monitor;
			}
		}
		CMonitorState state = QueryMonitor(hMonitor);
		std::lock_guard<std::mutex> lock(s_lock);
		s_monitor = state;
		return state;
	}

	LRESULT CALLBACK SurfaceWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
		switch (uMsg) {
		case WM_NCHITTEST:
			return HTTRANSPARENT; // the mouse goes to the main window below
		case WM_ERASEBKGND:
			return 1;
		case WM_PAINT:
			{
				// the swap chain keeps its content, nothing to paint
				PAINTSTRUCT ps;
				::BeginPaint(hWnd, &ps);
				::EndPaint(hWnd, &ps);
				return 0;
			}
		}
		return ::DefWindowProcW(hWnd, uMsg, wParam, lParam);
	}

	// Full screen triangle; the pixel shader composites the GDI user interface over the HDR image
	const char* SHADER_SOURCE =
		"cbuffer Params : register(b0) {\n"
		"	float2 ImagePos; float2 ImageXY;\n"
		"	float2 Scale; float2 InvTexSize;\n"
		"	float Lod; float SDRWhite; float SourcePeak; float TargetPeak;\n"
		"	float BackgroundMode; float2 CheckerShift; float Unused;\n"
		"	float4 BackgroundColor;\n"
		"};\n"
		"Texture2D<float4> UIBlack : register(t0);\n"
		"Texture2D<float4> UIWhite : register(t1);\n"
		"Texture2D<float4> Image : register(t2);\n"
		"SamplerState Smp : register(s0);\n"
		"float4 VSMain(uint id : SV_VertexID) : SV_Position {\n"
		"	float2 t = float2((id << 1) & 2, id & 2);\n"
		"	return float4(t * float2(2, -2) + float2(-1, 1), 0, 1);\n"
		"}\n"
		"float3 ToLinear(float3 c) { return (c <= 0.04045) ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4); }\n"
		"float NitsToPQ(float n) {\n"
		"	float y = pow(saturate(n / 10000.0), 0.1593017578125);\n"
		"	return pow((0.8359375 + 18.8515625 * y) / (1.0 + 18.6875 * y), 78.84375);\n"
		"}\n"
		"float PQToNits(float e) {\n"
		"	float p = pow(saturate(e), 1.0 / 78.84375);\n"
		"	return 10000.0 * pow(max(p - 0.8359375, 0.0) / (18.8515625 - 18.6875 * p), 1.0 / 0.1593017578125);\n"
		"}\n"
		// BT.2390 EETF, same as HDR::ToneMapNits()
		"float ToneMap(float n) {\n"
		"	if (SourcePeak <= TargetPeak) return min(n, TargetPeak);\n"
		"	float srcPQ = NitsToPQ(SourcePeak);\n"
		"	float e1 = min(NitsToPQ(n) / srcPQ, 1.0);\n"
		"	float maxLum = NitsToPQ(TargetPeak) / srcPQ;\n"
		"	float ks = max(1.5 * maxLum - 0.5, 0.0);\n"
		"	float e2 = e1;\n"
		"	if (e1 > ks) {\n"
		"		float t = (e1 - ks) / (1.0 - ks); float t2 = t * t; float t3 = t2 * t;\n"
		"		e2 = (2 * t3 - 3 * t2 + 1) * ks + (t3 - 2 * t2 + t) * (1 - ks) + (-2 * t3 + 3 * t2) * maxLum;\n"
		"	}\n"
		"	return min(PQToNits(e2 * srcPQ), TargetPeak);\n"
		"}\n"
		"float4 PSMain(float4 pos : SV_Position) : SV_Target {\n"
		"	int3 p = int3(pos.xy, 0);\n"
		"	float3 b = UIBlack.Load(p).rgb;\n"
		"	float3 w = UIWhite.Load(p).rgb;\n"
		"	float3 color = ToLinear(b) * SDRWhite;\n"
		"	float coverage = saturate((w.r - b.r + w.g - b.g + w.b - b.b) / 3.0);\n"
		"	if (coverage > 0.001) {\n"
		"		float2 xy = ImageXY + (pos.xy - ImagePos) * Scale;\n"
		"		float4 c = Image.SampleLevel(Smp, xy * InvTexSize, Lod);\n"
		"		float m = max(max(c.r, c.g), c.b) * 80.0;\n"
		"		if (m > 0.0) c.rgb *= ToneMap(m) / m;\n"
		// under translucent panels the image is not brighter than SDR white, so the text of the panels stays readable
		"		if (coverage < 0.995) { float mm = max(max(c.r, c.g), c.b); if (mm > SDRWhite) c.rgb *= SDRWhite / mm; }\n"
		"		float a = saturate(c.a);\n"
		"		float3 bg = BackgroundColor.rgb;\n"
		"		if (BackgroundMode > 1.5 && BackgroundMode < 2.5) {\n"
		"			int2 q = int2(pos.xy + CheckerShift);\n"
		"			bg = (((q.x >> 4) + (q.y >> 4)) & 1) ? 0.603827 : 1.0;\n" // 204 and 255 of the SDR checkerboard
		"		}\n"
		"		color += coverage * (c.rgb * a + bg * SDRWhite * (1.0 - a));\n"
		"	}\n"
		"	return float4(color, 1.0);\n"
		"}\n";

	struct CConstants {
		float ImageLeft, ImageTop, ImageX, ImageY;
		float ScaleX, ScaleY, InvTexWidth, InvTexHeight;
		float Lod, SDRWhite, SourcePeak, TargetPeak;
		float BackgroundMode, CheckerShiftX, CheckerShiftY, Unused;
		float BackgroundR, BackgroundG, BackgroundB, BackgroundA;
	};

	// sRGB component 0..255 to linear light
	float SRGBToLinear(int nValue) {
		double c = nValue / 255.0;
		return (float)((c <= 0.04045) ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4));
	}
}

struct CDisplayOutput::CImpl {
	HMODULE hD3D11;
	HMODULE hCompiler;
	ID3D11Device* pDevice;
	ID3D11DeviceContext* pContext;
	IDXGIFactory2* pFactory;
	IDXGISwapChain1* pSwapChain;
	ID3D11RenderTargetView* pRTV;
	ID3D11VertexShader* pVS;
	ID3D11PixelShader* pPS;
	ID3D11SamplerState* pSampler;
	ID3D11Buffer* pConstants;
	ID3D11Texture2D* pUITexture[2];
	ID3D11ShaderResourceView* pUIView[2];
	ID3D11Texture2D* pImageTexture;
	ID3D11ShaderResourceView* pImageView;
	unsigned int nImageId;
	int nSwapWidth, nSwapHeight;
	int nUIWidth, nUIHeight;
	int nMaxTextureSize;
	bool bFailed; // initialization failed, not tried again
};

void CDisplayOutput::SetMode(EDisplayMode eMode) {
	s_eMode = eMode;
}

EDisplayMode CDisplayOutput::GetMode() {
	return (EDisplayMode)(int)s_eMode;
}

void CDisplayOutput::SetMainWindow(HWND hWnd) {
	s_hMainWnd = hWnd;
}

void CDisplayOutput::InvalidateDisplayInfo() {
	std::lock_guard<std::mutex> lock(s_lock);
	s_monitor.Valid = false;
}

bool CDisplayOutput::IsOutputWanted() {
	int eMode = s_eMode;
	if (eMode == DISPLAY_OFF) return false;
	if (eMode == DISPLAY_FORCE) return true;
	return GetMonitorState().HDR;
}

CDisplayOutput::CDisplayOutput(HWND hParent) {
	m_hParent = hParent;
	m_hSurface = NULL;
	m_bVisible = false;
	m_pImpl = new CImpl();
	memset(m_pImpl, 0, sizeof(CImpl));
}

CDisplayOutput::~CDisplayOutput() {
	Release();
	if (m_hSurface != NULL) {
		::DestroyWindow(m_hSurface);
	}
	delete m_pImpl;
}

void CDisplayOutput::Release() {
	CImpl& d = *m_pImpl;
	if (d.pContext != NULL) {
		d.pContext->ClearState();
	}
	for (int i = 0; i < 2; i++) {
		SafeRelease(d.pUIView[i]);
		SafeRelease(d.pUITexture[i]);
	}
	SafeRelease(d.pImageView);
	SafeRelease(d.pImageTexture);
	SafeRelease(d.pConstants);
	SafeRelease(d.pSampler);
	SafeRelease(d.pPS);
	SafeRelease(d.pVS);
	SafeRelease(d.pRTV);
	SafeRelease(d.pSwapChain);
	SafeRelease(d.pFactory);
	SafeRelease(d.pContext);
	SafeRelease(d.pDevice);
	d.nImageId = 0;
	d.nSwapWidth = d.nSwapHeight = 0;
	d.nUIWidth = d.nUIHeight = 0;
}

bool CDisplayOutput::Initialize() {
	CImpl& d = *m_pImpl;
	if (d.pSwapChain != NULL) return true;
	if (d.bFailed) return false;
	d.bFailed = true; // until everything worked

	if (d.hD3D11 == NULL) d.hD3D11 = LoadSystemLibrary(L"d3d11.dll");
	if (d.hCompiler == NULL) d.hCompiler = LoadSystemLibrary(L"d3dcompiler_47.dll");
	if (d.hD3D11 == NULL || d.hCompiler == NULL) return false;
	PFN_D3D11_CREATE_DEVICE pCreateDevice = (PFN_D3D11_CREATE_DEVICE)::GetProcAddress(d.hD3D11, "D3D11CreateDevice");
	PFN_D3DCompile pCompile = (PFN_D3DCompile)::GetProcAddress(d.hCompiler, "D3DCompile");
	if (pCreateDevice == NULL || pCompile == NULL) return false;

	// Surface window over the client area of the main window. It is transparent for the mouse.
	if (m_hSurface == NULL) {
		HINSTANCE hInstance = ::GetModuleHandle(NULL);
		WNDCLASSEXW wc = { sizeof(wc) };
		if (!::GetClassInfoExW(hInstance, SURFACE_CLASS, &wc)) {
			wc.cbSize = sizeof(wc);
			wc.lpfnWndProc = SurfaceWndProc;
			wc.hInstance = hInstance;
			wc.hCursor = NULL;
			wc.lpszClassName = SURFACE_CLASS;
			::RegisterClassExW(&wc);
		}
		m_hSurface = ::CreateWindowExW(0, SURFACE_CLASS, L"", WS_CHILD | WS_DISABLED | WS_CLIPSIBLINGS, 0, 0, 1, 1, m_hParent, NULL, hInstance, NULL);
		if (m_hSurface == NULL) return false;
	}

	const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0 };
	D3D_FEATURE_LEVEL level;
	HRESULT hr = pCreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, _countof(levels),
		D3D11_SDK_VERSION, &d.pDevice, &level, &d.pContext);
	if (hr == E_INVALIDARG) { // no 11.1 runtime
		hr = pCreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels + 1, _countof(levels) - 1,
			D3D11_SDK_VERSION, &d.pDevice, &level, &d.pContext);
	}
	if (FAILED(hr)) {
		hr = pCreateDevice(NULL, D3D_DRIVER_TYPE_WARP, NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels + 1, _countof(levels) - 1,
			D3D11_SDK_VERSION, &d.pDevice, &level, &d.pContext);
	}
	if (FAILED(hr)) {
		Release();
		return false;
	}
	d.nMaxTextureSize = (level >= D3D_FEATURE_LEVEL_11_0) ? D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION : 8192;

	IDXGIDevice* pDXGIDevice = NULL;
	IDXGIAdapter* pAdapter = NULL;
	if (SUCCEEDED(d.pDevice->QueryInterface(__uuidof(IDXGIDevice), (void**)&pDXGIDevice))) {
		if (SUCCEEDED(pDXGIDevice->GetAdapter(&pAdapter))) {
			pAdapter->GetParent(__uuidof(IDXGIFactory2), (void**)&d.pFactory);
			pAdapter->Release();
		}
		pDXGIDevice->Release();
	}
	if (d.pFactory == NULL) {
		Release();
		return false;
	}

	ID3DBlob* pCode = NULL;
	ID3DBlob* pErrors = NULL;
	size_t nSourceLength = strlen(SHADER_SOURCE);
	bool bShadersOK = false;
	if (SUCCEEDED(pCompile(SHADER_SOURCE, nSourceLength, "HDRDisplay", NULL, NULL, "VSMain", "vs_4_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &pCode, &pErrors))) {
		if (SUCCEEDED(d.pDevice->CreateVertexShader(pCode->GetBufferPointer(), pCode->GetBufferSize(), NULL, &d.pVS))) {
			SafeRelease(pCode);
			if (SUCCEEDED(pCompile(SHADER_SOURCE, nSourceLength, "HDRDisplay", NULL, NULL, "PSMain", "ps_4_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &pCode, &pErrors))) {
				bShadersOK = SUCCEEDED(d.pDevice->CreatePixelShader(pCode->GetBufferPointer(), pCode->GetBufferSize(), NULL, &d.pPS));
			}
		}
	}
	if (pErrors != NULL) {
		::OutputDebugStringA((const char*)pErrors->GetBufferPointer());
	}
	SafeRelease(pCode);
	SafeRelease(pErrors);
	if (!bShadersOK) {
		Release();
		return false;
	}

	D3D11_SAMPLER_DESC samplerDesc = {};
	samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
	samplerDesc.AddressU = samplerDesc.AddressV = samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
	samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
	D3D11_BUFFER_DESC bufferDesc = {};
	bufferDesc.ByteWidth = sizeof(CConstants);
	bufferDesc.Usage = D3D11_USAGE_DEFAULT;
	bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	if (FAILED(d.pDevice->CreateSamplerState(&samplerDesc, &d.pSampler)) || FAILED(d.pDevice->CreateBuffer(&bufferDesc, NULL, &d.pConstants))) {
		Release();
		return false;
	}

	DXGI_SWAP_CHAIN_DESC1 swapDesc = {};
	swapDesc.Width = 8;
	swapDesc.Height = 8;
	swapDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	swapDesc.SampleDesc.Count = 1;
	swapDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapDesc.BufferCount = 2;
	swapDesc.Scaling = DXGI_SCALING_STRETCH;
	swapDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	swapDesc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
	hr = d.pFactory->CreateSwapChainForHwnd(d.pDevice, m_hSurface, &swapDesc, NULL, NULL, &d.pSwapChain);
	if (FAILED(hr)) {
		swapDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
		hr = d.pFactory->CreateSwapChainForHwnd(d.pDevice, m_hSurface, &swapDesc, NULL, NULL, &d.pSwapChain);
	}
	if (FAILED(hr)) {
		Release();
		return false;
	}
	d.pFactory->MakeWindowAssociation(m_hSurface, DXGI_MWA_NO_ALT_ENTER | DXGI_MWA_NO_WINDOW_CHANGES);
	IDXGISwapChain3* pSwapChain3 = NULL;
	if (SUCCEEDED(d.pSwapChain->QueryInterface(__uuidof(IDXGISwapChain3), (void**)&pSwapChain3))) {
		pSwapChain3->SetColorSpace1(DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709); // scRGB
		pSwapChain3->Release();
	}
	d.nSwapWidth = d.nSwapHeight = 8;
	d.bFailed = false;
	return true;
}

static bool CreateTexture(ID3D11Device* pDevice, int nWidth, int nHeight, DXGI_FORMAT format, bool bMipMaps,
	ID3D11Texture2D** ppTexture, ID3D11ShaderResourceView** ppView) {
	D3D11_TEXTURE2D_DESC desc = {};
	desc.Width = nWidth;
	desc.Height = nHeight;
	desc.MipLevels = bMipMaps ? 0 : 1;
	desc.ArraySize = 1;
	desc.Format = format;
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | (bMipMaps ? D3D11_BIND_RENDER_TARGET : 0);
	desc.MiscFlags = bMipMaps ? D3D11_RESOURCE_MISC_GENERATE_MIPS : 0;
	if (FAILED(pDevice->CreateTexture2D(&desc, NULL, ppTexture))) {
		return false;
	}
	if (FAILED(pDevice->CreateShaderResourceView(*ppTexture, NULL, ppView))) {
		SafeRelease(*ppTexture);
		return false;
	}
	return true;
}

// Debug and tests: with the environment variable JPEGVIEW_HDR_DUMP set to a file name, every frame is written to this file
// (header "JVHD", width, height as 32 bit integers, then the rows of RGBA half floats)
static void DumpBackBuffer(ID3D11Device* pDevice, ID3D11DeviceContext* pContext, ID3D11Texture2D* pBackBuffer) {
	wchar_t sFileName[MAX_PATH];
	if (::GetEnvironmentVariableW(L"JPEGVIEW_HDR_DUMP", sFileName, MAX_PATH) == 0) {
		return;
	}
	D3D11_TEXTURE2D_DESC desc;
	pBackBuffer->GetDesc(&desc);
	desc.BindFlags = 0;
	desc.Usage = D3D11_USAGE_STAGING;
	desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	desc.MiscFlags = 0;
	ID3D11Texture2D* pStaging = NULL;
	if (FAILED(pDevice->CreateTexture2D(&desc, NULL, &pStaging))) {
		return;
	}
	pContext->CopyResource(pStaging, pBackBuffer);
	D3D11_MAPPED_SUBRESOURCE mapped;
	if (SUCCEEDED(pContext->Map(pStaging, 0, D3D11_MAP_READ, 0, &mapped))) {
		FILE* pFile = NULL;
		if (_wfopen_s(&pFile, sFileName, L"wb") == 0 && pFile != NULL) {
			int size[2] = { (int)desc.Width, (int)desc.Height };
			fwrite("JVHD", 4, 1, pFile);
			fwrite(size, sizeof(size), 1, pFile);
			for (UINT y = 0; y < desc.Height; y++) {
				fwrite((const uint8_t*)mapped.pData + y * mapped.RowPitch, desc.Width * 8, 1, pFile);
			}
			fclose(pFile);
		}
		pContext->Unmap(pStaging, 0);
	}
	pStaging->Release();
}

bool CDisplayOutput::Render(const CLinearImage& image, const CFrameParams& params) {
	if (params.ClientWidth <= 0 || params.ClientHeight <= 0 || params.UIBlack == NULL || params.UIWhite == NULL) {
		Hide();
		return false;
	}
	if (!Initialize()) {
		Hide();
		return false;
	}
	CImpl& d = *m_pImpl;
	if (image.Width() > d.nMaxTextureSize || image.Height() > d.nMaxTextureSize ||
		params.ClientWidth > d.nMaxTextureSize || params.ClientHeight > d.nMaxTextureSize) {
		Hide();
		return false;
	}

	// swap chain in the size of the client area
	int nWidth = params.ClientWidth, nHeight = params.ClientHeight;
	if (nWidth != d.nSwapWidth || nHeight != d.nSwapHeight) {
		d.pContext->OMSetRenderTargets(0, NULL, NULL);
		SafeRelease(d.pRTV);
		if (FAILED(d.pSwapChain->ResizeBuffers(0, nWidth, nHeight, DXGI_FORMAT_UNKNOWN, 0))) {
			Release();
			Hide();
			return false;
		}
		d.nSwapWidth = nWidth;
		d.nSwapHeight = nHeight;
	}
	if (d.pRTV == NULL) {
		ID3D11Texture2D* pBackBuffer = NULL;
		if (SUCCEEDED(d.pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&pBackBuffer))) {
			d.pDevice->CreateRenderTargetView(pBackBuffer, NULL, &d.pRTV);
			pBackBuffer->Release();
		}
		if (d.pRTV == NULL) {
			Release();
			Hide();
			return false;
		}
	}

	// the image, with mip maps for smooth downsampling
	if (d.nImageId != image.Id()) {
		SafeRelease(d.pImageView);
		SafeRelease(d.pImageTexture);
		d.nImageId = 0;
		if (!CreateTexture(d.pDevice, image.Width(), image.Height(), DXGI_FORMAT_R16G16B16A16_FLOAT, true, &d.pImageTexture, &d.pImageView)) {
			Hide();
			return false;
		}
		d.pContext->UpdateSubresource(d.pImageTexture, 0, NULL, image.Pixels(), image.Width() * 8, 0);
		d.pContext->GenerateMips(d.pImageView);
		d.nImageId = image.Id();
	}

	// the user interface
	if (nWidth != d.nUIWidth || nHeight != d.nUIHeight) {
		for (int i = 0; i < 2; i++) {
			SafeRelease(d.pUIView[i]);
			SafeRelease(d.pUITexture[i]);
		}
		d.nUIWidth = d.nUIHeight = 0;
		for (int i = 0; i < 2; i++) {
			if (!CreateTexture(d.pDevice, nWidth, nHeight, DXGI_FORMAT_B8G8R8A8_UNORM, false, &d.pUITexture[i], &d.pUIView[i])) {
				Hide();
				return false;
			}
		}
		d.nUIWidth = nWidth;
		d.nUIHeight = nHeight;
	}
	d.pContext->UpdateSubresource(d.pUITexture[0], 0, NULL, params.UIBlack, nWidth * 4, 0);
	d.pContext->UpdateSubresource(d.pUITexture[1], 0, NULL, params.UIWhite, nWidth * 4, 0);

	CMonitorState monitor = GetMonitorState();
	CConstants constants;
	constants.ImageLeft = (float)params.ImageLeft;
	constants.ImageTop = (float)params.ImageTop;
	constants.ImageX = (float)params.ImageX;
	constants.ImageY = (float)params.ImageY;
	constants.ScaleX = (float)params.ScaleX;
	constants.ScaleY = (float)params.ScaleY;
	constants.InvTexWidth = 1.0f / image.Width();
	constants.InvTexHeight = 1.0f / image.Height();
	double dScale = max(params.ScaleX, params.ScaleY);
	constants.Lod = (dScale > 1.0) ? (float)(log(dScale) / log(2.0)) : 0.0f;
	constants.SDRWhite = monitor.SDRWhiteNits / SCRGB_NITS;
	constants.SourcePeak = image.PeakNits();
	constants.TargetPeak = (monitor.HDR && monitor.MaxNits > 0.0f) ? monitor.MaxNits : DEFAULT_PEAK_NITS;
	constants.BackgroundMode = (float)params.BackgroundMode;
	constants.CheckerShiftX = (float)params.CheckerShiftX;
	constants.CheckerShiftY = (float)params.CheckerShiftY;
	constants.Unused = 0.0f;
	constants.BackgroundR = SRGBToLinear(GetRValue(params.BackgroundColor));
	constants.BackgroundG = SRGBToLinear(GetGValue(params.BackgroundColor));
	constants.BackgroundB = SRGBToLinear(GetBValue(params.BackgroundColor));
	constants.BackgroundA = 1.0f;
	d.pContext->UpdateSubresource(d.pConstants, 0, NULL, &constants, 0, 0);

	D3D11_VIEWPORT viewport = { 0.0f, 0.0f, (float)nWidth, (float)nHeight, 0.0f, 1.0f };
	ID3D11ShaderResourceView* views[3] = { d.pUIView[0], d.pUIView[1], d.pImageView };
	d.pContext->OMSetRenderTargets(1, &d.pRTV, NULL);
	d.pContext->RSSetViewports(1, &viewport);
	d.pContext->IASetInputLayout(NULL);
	d.pContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	d.pContext->VSSetShader(d.pVS, NULL, 0);
	d.pContext->PSSetShader(d.pPS, NULL, 0);
	d.pContext->PSSetShaderResources(0, 3, views);
	d.pContext->PSSetSamplers(0, 1, &d.pSampler);
	d.pContext->PSSetConstantBuffers(0, 1, &d.pConstants);
	d.pContext->Draw(3, 0);

	ID3D11Texture2D* pBackBuffer = NULL;
	if (SUCCEEDED(d.pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&pBackBuffer))) {
		DumpBackBuffer(d.pDevice, d.pContext, pBackBuffer);
		pBackBuffer->Release();
	}

	HRESULT hr = d.pSwapChain->Present(1, 0);
	if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
		Release(); // created again with the next frame
		Hide();
		return false;
	}

	// shown after the first frame is ready
	RECT rect;
	::GetWindowRect(m_hSurface, &rect);
	if (!m_bVisible || rect.right - rect.left != nWidth || rect.bottom - rect.top != nHeight) {
		::SetWindowPos(m_hSurface, HWND_TOP, 0, 0, nWidth, nHeight, SWP_NOACTIVATE | SWP_SHOWWINDOW | SWP_NOREDRAW);
		m_bVisible = true;
	}
	return true;
}

void CDisplayOutput::Hide() {
	if (m_bVisible && m_hSurface != NULL) {
		::ShowWindow(m_hSurface, SW_HIDE);
	}
	m_bVisible = false;
}

}
