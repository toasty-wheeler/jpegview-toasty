#pragma once

#include <stdint.h>

// Output of HDR photos on HDR displays (Windows HDR on): the linear light pixels are shown with Direct3D
// in the scRGB color space, so highlights are brighter than SDR white. The user interface is painted with
// GDI as usual and composited on top (see CMainDlg::PaintHDR).
namespace HDR {

	class CLinearImage;

	enum EDisplayMode {
		DISPLAY_OFF = 0, // never, HDR photos are tone-mapped to SDR
		DISPLAY_AUTO,    // when the monitor of the window has Windows HDR turned on
		DISPLAY_FORCE    // always (for testing: on SDR displays the highlights are clipped)
	};

	// One frame of the HDR output
	struct CFrameParams {
		int ClientWidth, ClientHeight;
		// The window painted with GDI twice, 32 bpp top-down: once with the image area black and once white.
		// Their difference is the visibility of the image at each pixel, the black version is the user interface.
		const uint32_t* UIBlack;
		const uint32_t* UIWhite;
		int ImageLeft, ImageTop;  // client position of the top left corner of the shown part of the image
		double ImageX, ImageY;    // pixel position in the linear image at this corner
		double ScaleX, ScaleY;    // linear image pixels per client pixel
		int BackgroundMode;       // background of transparent images: 0 black, 1 white, 2 checkerboard, 3 color
		uint32_t BackgroundColor; // COLORREF of the background (not used for the checkerboard)
		int CheckerShiftX, CheckerShiftY; // added to the client position for the checkerboard pattern
	};

	class CDisplayOutput {
	public:
		// Mode from the INI file (HDRDisplay), can be toggled while viewing
		static void SetMode(EDisplayMode eMode);
		static EDisplayMode GetMode();
		// Main window, its monitor decides in mode DISPLAY_AUTO
		static void SetMainWindow(HWND hWnd);
		// Display settings changed (WM_DISPLAYCHANGE): check the monitor again
		static void InvalidateDisplayInfo();
		// Thread safe: true if HDR photos should keep their linear pixels and be shown with the HDR output
		static bool IsOutputWanted();

		CDisplayOutput(HWND hParent);
		~CDisplayOutput();

		// Renders and shows the frame. Returns false if not possible (the output is hidden then and the caller paints normally).
		bool Render(const CLinearImage& image, const CFrameParams& params);
		// Hides the output, the window shows its GDI content again
		void Hide();
		bool IsVisible() const { return m_bVisible; }

	private:
		struct CImpl;
		CImpl* m_pImpl;
		HWND m_hParent;
		HWND m_hSurface;
		bool m_bVisible;

		bool Initialize();
		void Release();

		CDisplayOutput(const CDisplayOutput&);
		CDisplayOutput& operator=(const CDisplayOutput&);
	};
}
