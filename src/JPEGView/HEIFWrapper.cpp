#include "stdafx.h"

#include "HEIFWrapper.h"
#include "MaxImageDef.h"
#include "ICCProfileTransform.h"
#include "HDRImage.h"

void * HeifReader::ReadImage(int &width,
					   int &height,
					   int &nchannels,
					   int &frame_count,
					   void* &exif_chunk,
					   bool &outOfMemory,
					   int frame_index,
					   const void *buffer,
					   int sizebytes,
					   HDR::CLinearImage** ppLinear)
{
	if (ppLinear != NULL) *ppLinear = NULL;
	outOfMemory = false;
	width = height = 0;
	nchannels = 4;

	unsigned char* pPixelData = NULL;
	exif_chunk = NULL;

	heif::Context context;
	context.read_from_memory_without_copy(buffer, sizebytes);
	frame_count = context.get_number_of_top_level_images();
	heif_item_id item_id = context.get_list_of_top_level_image_IDs().at(frame_index);
	heif::ImageHandle handle = context.get_image_handle(item_id);
	// height = handle.get_height();
	// width = handle.get_width();

	// HDR images (PQ or HLG) are decoded with full bit depth and tone-mapped
	pPixelData = (unsigned char*)ReadHDRImage(handle.get_raw_image_handle(), width, height, outOfMemory, ppLinear);
	if (pPixelData != NULL || outOfMemory) {
		ReadExif(handle, exif_chunk);
		return pPixelData;
	}

	heif::Image image = handle.decode_image(heif_colorspace_RGB, heif_chroma_interleaved_RGBA);
	int stride;
	uint8_t* data = image.get_plane(heif_channel_interleaved, &stride);
	width = image.get_width(heif_channel_interleaved);
	height = image.get_height(heif_channel_interleaved);

	if (width > MAX_IMAGE_DIMENSION || height > MAX_IMAGE_DIMENSION)
		return NULL;
	if (abs((double)width * height) > MAX_IMAGE_PIXELS) {
		outOfMemory = true;
		return NULL;
	}
	if (width < 1 || height < 1 || width * nchannels > stride)
		return NULL;

	int size = width * nchannels * height;
	pPixelData = new(std::nothrow) unsigned char[size];
	if (pPixelData == NULL) {
		outOfMemory = true;
		return NULL;
	}
	std::vector<uint8_t> iccp = image.get_raw_color_profile();
	void* transform = ICCProfileTransform::CreateTransform(iccp.data(), iccp.size(), ICCProfileTransform::FORMAT_RGBA);
	size_t i, j;
	if (!ICCProfileTransform::DoTransform(transform, data, pPixelData, width, height, stride=stride)) {
		unsigned int* o = (unsigned int*)pPixelData;
		for (i = 0; i < height; i++) {
			unsigned int* p = (unsigned int*)(data + i * stride);
			for (j = 0; j < width; j++) {
				// RGBA -> BGRA conversion
				*o = _rotr(_byteswap_ulong(*p), 8);
				p++;
				o++;
			}
		}
	}
	ICCProfileTransform::DeleteTransform(transform);

	ReadExif(handle, exif_chunk);
	return (void*)pPixelData;
}

void* HeifReader::ReadHDRImage(heif_image_handle* handle, int& width, int& height, bool& outOfMemory, HDR::CLinearImage** ppLinear) {
	if (heif_image_handle_get_luma_bits_per_pixel(handle) <= 8) {
		return NULL;
	}
	HDR::CSourceInfo info;
	heif_color_profile_nclx* nclx = NULL;
	if (heif_image_handle_get_nclx_color_profile(handle, &nclx).code != heif_error_Ok || nclx == NULL) {
		return NULL;
	}
	info.Transfer = HDR::TransferFromH273(nclx->transfer_characteristics);
	info.Primaries = HDR::PrimariesFromH273(nclx->color_primaries);
	heif_nclx_color_profile_free(nclx);
	if (!info.IsHDR()) {
		return NULL;
	}

	bool hasAlpha = heif_image_handle_has_alpha_channel(handle) != 0;
	heif_image* image = NULL;
	if (heif_decode_image(handle, &image, heif_colorspace_RGB,
		hasAlpha ? heif_chroma_interleaved_RRGGBBAA_LE : heif_chroma_interleaved_RRGGBB_LE, NULL).code != heif_error_Ok || image == NULL) {
		return NULL;
	}
	void* pPixelData = NULL;
	int stride = 0;
	const uint8_t* data = heif_image_get_plane_readonly(image, heif_channel_interleaved, &stride);
	int w = heif_image_get_width(image, heif_channel_interleaved);
	int h = heif_image_get_height(image, heif_channel_interleaved);
	int bits = heif_image_get_bits_per_pixel_range(image, heif_channel_interleaved);
	int channels = hasAlpha ? 4 : 3;
	if (data != NULL && w > 0 && h > 0 && w <= (int)MAX_IMAGE_DIMENSION && h <= (int)MAX_IMAGE_DIMENSION &&
		bits > 8 && bits <= 16 && stride >= w * channels * 2) {
		if ((double)w * h > MAX_IMAGE_PIXELS) {
			outOfMemory = true;
		} else {
			if (heif_image_has_content_light_level(image)) {
				heif_content_light_level clli;
				heif_image_get_content_light_level(image, &clli);
				info.PeakNits = clli.max_content_light_level;
			}
			if (info.PeakNits <= 0.0f && heif_image_has_mastering_display_colour_volume(image)) {
				heif_mastering_display_colour_volume mdcv;
				heif_decoded_mastering_display_colour_volume decoded;
				heif_image_get_mastering_display_colour_volume(image, &mdcv);
				if (heif_mastering_display_colour_volume_decode(&mdcv, &decoded).code == heif_error_Ok) {
					info.PeakNits = (float)decoded.max_display_mastering_luminance;
				}
			}
			pPixelData = new(std::nothrow) unsigned char[(size_t)w * h * 4];
			if (pPixelData == NULL) {
				outOfMemory = true;
			} else if (!HDR::ConvertToSDR((const uint16_t*)data, w, h, channels, (1 << bits) - 1, stride, info, (uint8_t*)pPixelData, ppLinear)) {
				delete[] (unsigned char*)pPixelData;
				pPixelData = NULL;
			} else {
				width = w;
				height = h;
			}
		}
	}
	heif_image_release(image);
	return pPixelData;
}

void HeifReader::ReadExif(heif::ImageHandle& handle, void*& exif_chunk) {
	std::vector<heif_item_id> exif_blocks = handle.get_list_of_metadata_block_IDs("Exif");

	if (!exif_blocks.empty()) {
		std::vector<uint8_t> exif = handle.get_metadata(exif_blocks[0]);
		// 65538 magic number comes from investigations by qbnu
		// see https://github.com/sylikc/jpegview/pull/213#pullrequestreview-1494451359 for more details
		/*
		* These libraries all have their own ideas about where to start the Exif data from.
		* JPEG Exif blocks are in the format
		  FF E1 SS SS 45 78 69 66 00 00 [data]

		  The SS SS is a big-endian unsigned short representing the size of everything after FF E1.

		  libjxl gives 00 00 00 00 [data]
		  libheif gives 00 00 00 00 45 78 69 66 00 00 [data]
		  libavif, libwebp and libpng give [data], so they have different limits for size.

		  If you want I can change it to 65536 + an offset and add notes explaining why.
		*/
		if (exif.size() > 8 && exif.size() < 65538) {
			exif_chunk = malloc(exif.size());
			if (exif_chunk != NULL) {
				memcpy(exif_chunk, exif.data(), exif.size());
				*((unsigned short*)exif_chunk) = _byteswap_ushort(0xFFE1);
				*((unsigned short*)exif_chunk + 1) = _byteswap_ushort(exif.size() - 2);
			}
		}
	}
}
