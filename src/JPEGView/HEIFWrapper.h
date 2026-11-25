#pragma once

#include "libheif/heif_cxx.h"

namespace HDR { class CLinearImage; }

class HeifReader
{
public:
	// Returns data in the form 4-byte BGRA
	// HDR images (PQ or HLG transfer) are tone-mapped; with ppLinear != NULL the linear light pixels
	// are returned there too (or NULL for SDR images), the caller deletes them.
	static void * ReadImage(int &width,   // width of the image loaded.
						 int &height,  // height of the image loaded.
						 int &bpp,     // BYTES (not bits) PER PIXEL.
						 int &frame_count, // number of top-level images
					     void* &exif_chunk, // Pointer to Exif data (must be freed by caller)
						 bool &outOfMemory, // set to true when no memory to read image
						 int frame_index, // index of requested frame
						 const void *buffer, // memory address containing heic compressed data.
						 int sizebytes, // size of heic compressed data.
						 HDR::CLinearImage** ppLinear = NULL);
private:
	// Returns NULL when the image is not HDR (or decoding fails, then the normal 8 bit path is used)
	static void* ReadHDRImage(heif_image_handle* handle, int& width, int& height, bool& outOfMemory, HDR::CLinearImage** ppLinear);
	static void ReadExif(heif::ImageHandle& handle, void*& exif_chunk);
};
