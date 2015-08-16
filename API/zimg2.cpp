#define ZIMG_API_V1
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cmath>
#include <cstring>
#include <new>
#include "Colorspace/colorspace2.h"
#include "Colorspace/colorspace_param.h"
#include "Common/align.h"
#include "Common/cpuinfo.h"
#include "Common/except.h"
#include "Common/osdep.h"
#include "Common/pixel.h"
#include "Common/static_map.h"
#include "Common/zfilter.h"
#include "Depth/depth2.h"
#include "Resize/filter.h"
#include "Resize/resize2.h"
#include "zimg2.h"

#define API_VERSION_ASSERT(x) assert((x) >= 2 && (x) <= ZIMG_API_VERSION)
#define POINTER_ALIGNMENT_ASSERT(x) assert(!(x) || reinterpret_cast<uintptr_t>(x) % zimg::ALIGNMENT == 0)
#define STRIDE_ALIGNMENT_ASSERT(x) assert(!(x) || (x) % zimg::ALIGNMENT == 0)

namespace zimg_api {;

extern std::atomic<zimg::CPUClass> g_cpu_type;
extern THREAD_LOCAL int g_last_error;
extern THREAD_LOCAL char g_last_error_msg[1024];

} // namespace zimg_api

using namespace zimg_api;


namespace {;

const unsigned VERSION_INFO[] = { 1, 90, 0 };

template <class T>
void record_exception_message(const T &e)
{
	strncpy(g_last_error_msg, e.what(), sizeof(g_last_error_msg) - 1);
	g_last_error_msg[sizeof(g_last_error_msg) - 1] = '\0';
}

int handle_exception(const zimg::ZimgException &e)
{
	int code = ZIMG_ERROR_UNKNOWN;

	try {
		throw e;
	} catch (const zimg::ZimgUnknownError &e) {
		record_exception_message(e);
		code = ZIMG_ERROR_UNKNOWN;
	} catch (const zimg::ZimgLogicError &e) {
		record_exception_message(e);
		code = ZIMG_ERROR_LOGIC;
	} catch (const zimg::ZimgOutOfMemory &e) {
		record_exception_message(e);
		code = ZIMG_ERROR_OUT_OF_MEMORY;
	} catch (const zimg::ZimgIllegalArgument &e) {
		record_exception_message(e);
		code = ZIMG_ERROR_ILLEGAL_ARGUMENT;
	} catch (const zimg::ZimgUnsupportedError &e) {
		record_exception_message(e);
		code = ZIMG_ERROR_UNSUPPORTED;
	} catch (const zimg::ZimgException &e) {
		record_exception_message(e);
		code = ZIMG_ERROR_UNKNOWN;
	}

	g_last_error = code;
	return code;
}

zimg::CPUClass translate_cpu(int cpu)
{
	static const zimg::static_int_map<zimg::CPUClass, 12> map{
		{ ZIMG_CPU_NONE,      zimg::CPUClass::CPU_NONE },
#ifdef ZIMG_X86
		{ ZIMG_CPU_AUTO,      zimg::CPUClass::CPU_X86_AUTO },
		{ ZIMG_CPU_X86_MMX,   zimg::CPUClass::CPU_NONE },
		{ ZIMG_CPU_X86_SSE,   zimg::CPUClass::CPU_NONE },
		{ ZIMG_CPU_X86_SSE2,  zimg::CPUClass::CPU_X86_SSE2 },
		{ ZIMG_CPU_X86_SSE3,  zimg::CPUClass::CPU_X86_SSE2 },
		{ ZIMG_CPU_X86_SSSE3, zimg::CPUClass::CPU_X86_SSE2 },
		{ ZIMG_CPU_X86_SSE41, zimg::CPUClass::CPU_X86_SSE2 },
		{ ZIMG_CPU_X86_SSE42, zimg::CPUClass::CPU_X86_SSE2 },
		{ ZIMG_CPU_X86_AVX,   zimg::CPUClass::CPU_X86_SSE2 },
		{ ZIMG_CPU_X86_F16C,  zimg::CPUClass::CPU_X86_SSE2 },
		{ ZIMG_CPU_X86_AVX2,  zimg::CPUClass::CPU_X86_AVX2 },
#endif
	};
	auto it = map.find(cpu);
	return it == map.end() ? throw zimg::ZimgIllegalArgument{ "invalid cpu type" } : it->second;
}

zimg::PixelType translate_pixel_type(int pixel_type)
{
	static const zimg::static_int_map<zimg::PixelType, 4> map{
		{ ZIMG_PIXEL_BYTE,  zimg::PixelType::BYTE },
		{ ZIMG_PIXEL_WORD,  zimg::PixelType::WORD },
		{ ZIMG_PIXEL_HALF,  zimg::PixelType::HALF },
		{ ZIMG_PIXEL_FLOAT, zimg::PixelType::FLOAT },
	};
	auto it = map.find(pixel_type);
	return it == map.end() ? throw zimg::ZimgIllegalArgument{ "invalid pixel type" } : it->second;
}

bool translate_pixel_range(int range)
{
	static const zimg::static_int_map<bool, 2> map{
		{ ZIMG_RANGE_LIMITED, false },
		{ ZIMG_RANGE_FULL,    true }
	};
	auto it = map.find(range);
	return it == map.end() ? throw zimg::ZimgIllegalArgument{ "invalid pixel range" } : it->second;
}

zimg::colorspace::MatrixCoefficients translate_matrix(int matrix)
{
	static const zimg::static_int_map<zimg::colorspace::MatrixCoefficients, 6> map{
		{ ZIMG_MATRIX_RGB,      zimg::colorspace::MatrixCoefficients::MATRIX_RGB },
		{ ZIMG_MATRIX_709,      zimg::colorspace::MatrixCoefficients::MATRIX_709 },
		{ ZIMG_MATRIX_470BG,    zimg::colorspace::MatrixCoefficients::MATRIX_601 },
		{ ZIMG_MATRIX_170M,     zimg::colorspace::MatrixCoefficients::MATRIX_601 },
		{ ZIMG_MATRIX_2020_NCL, zimg::colorspace::MatrixCoefficients::MATRIX_2020_NCL },
		{ ZIMG_MATRIX_2020_CL,  zimg::colorspace::MatrixCoefficients::MATRIX_2020_CL },
	};
	auto it = map.find(matrix);
	return it == map.end() ? throw zimg::ZimgIllegalArgument{ "invalid matrix coefficients" } : it->second;
}

zimg::colorspace::TransferCharacteristics translate_transfer(int transfer)
{
	static const zimg::static_int_map<zimg::colorspace::TransferCharacteristics, 5> map{
		{ ZIMG_TRANSFER_709,     zimg::colorspace::TransferCharacteristics::TRANSFER_709 },
		{ ZIMG_TRANSFER_601,     zimg::colorspace::TransferCharacteristics::TRANSFER_709 },
		{ ZIMG_TRANSFER_2020_10, zimg::colorspace::TransferCharacteristics::TRANSFER_709 },
		{ ZIMG_TRANSFER_2020_12, zimg::colorspace::TransferCharacteristics::TRANSFER_709 },
		{ ZIMG_TRANSFER_LINEAR,  zimg::colorspace::TransferCharacteristics::TRANSFER_LINEAR },
	};
	auto it = map.find(transfer);
	return it == map.end() ? throw zimg::ZimgIllegalArgument{ "invalid transfer characteristics" } : it->second;
}

zimg::colorspace::ColorPrimaries translate_primaries(int primaries)
{
	static const zimg::static_int_map<zimg::colorspace::ColorPrimaries, 4> map{
		{ ZIMG_PRIMARIES_709,  zimg::colorspace::ColorPrimaries::PRIMARIES_709 },
		{ ZIMG_PRIMARIES_170M, zimg::colorspace::ColorPrimaries::PRIMARIES_SMPTE_C },
		{ ZIMG_PRIMARIES_240M, zimg::colorspace::ColorPrimaries::PRIMARIES_SMPTE_C },
		{ ZIMG_PRIMARIES_2020, zimg::colorspace::ColorPrimaries::PRIMARIES_2020 },
	};
	auto it = map.find(primaries);
	return it == map.end() ? throw zimg::ZimgIllegalArgument{ "invalid color primaries" } : it->second;
}

zimg::depth::DitherType translate_dither(int dither)
{
	static const zimg::static_int_map<zimg::depth::DitherType, 4> map{
		{ ZIMG_DITHER_NONE,            zimg::depth::DitherType::DITHER_NONE },
		{ ZIMG_DITHER_ORDERED,         zimg::depth::DitherType::DITHER_ORDERED },
		{ ZIMG_DITHER_RANDOM,          zimg::depth::DitherType::DITHER_RANDOM },
		{ ZIMG_DITHER_ERROR_DIFFUSION, zimg::depth::DitherType::DITHER_ERROR_DIFFUSION },
	};
	auto it = map.find(dither);
	return it == map.end() ? throw zimg::ZimgIllegalArgument{ "invalid dither" } : it->second;
}

zimg::resize::Filter *translate_resize_filter(int filter_type, double param_a, double param_b)
{
	try {
		switch (filter_type) {
		case ZIMG_RESIZE_POINT:
			return new zimg::resize::PointFilter{};
		case ZIMG_RESIZE_BILINEAR:
			return new zimg::resize::BilinearFilter{};
		case ZIMG_RESIZE_BICUBIC:
			param_a = std::isnan(param_a) ? 1.0 / 3.0 : param_a;
			param_b = std::isnan(param_b) ? 1.0 / 3.0 : param_b;
			return new zimg::resize::BicubicFilter{ param_a, param_b };
		case ZIMG_RESIZE_SPLINE16:
			return new zimg::resize::Spline16Filter{};
		case ZIMG_RESIZE_SPLINE36:
			return new zimg::resize::Spline36Filter{};
		case ZIMG_RESIZE_LANCZOS:
			param_a = std::isnan(param_a) ? 3.0 : std::floor(param_a);
			return new zimg::resize::LanczosFilter{ (int)param_a };
		default:
			throw zimg::ZimgIllegalArgument{ "invalid resize filter" };
		}
	} catch (const std::bad_alloc &) {
		throw zimg::ZimgOutOfMemory{};
	}
}


const zimg::IZimgFilter *cast_filter_ptr(const zimg_filter *ptr)
{
	const zimg::IZimgFilter *filter = dynamic_cast<const zimg::IZimgFilter *>(ptr);
	assert(filter);

	return filter;
}

void export_filter_flags(const zimg::ZimgFilterFlags &src, zimg_filter_flags *dst, unsigned version)
{
	API_VERSION_ASSERT(version);

	if (version >= 2) {
		dst->version = std::min(version, (unsigned)ZIMG_API_VERSION);
		dst->has_state = src.has_state;
		dst->same_row = src.same_row;
		dst->in_place = src.in_place;
		dst->entire_row = src.entire_row;
		dst->color = src.color;
	}
}

zimg::ZimgImageBuffer import_image_buffer(const zimg_image_buffer &src)
{
	zimg::ZimgImageBuffer dst{};

	API_VERSION_ASSERT(src.version);

#define COPY_ARRAY(x, y) std::copy(std::begin(x), std::end(x), y)
	if (src.version >= 2) {
		COPY_ARRAY(src.data, dst.data);
		COPY_ARRAY(src.stride, dst.stride);
		COPY_ARRAY(src.mask, dst.mask);
	}
#undef COPY_ARRAY
	return dst;
}

} // namespace


void zimg2_get_version_info(unsigned *major, unsigned *minor, unsigned *micro)
{
	*major = VERSION_INFO[0];
	*minor = VERSION_INFO[1];
	*micro = VERSION_INFO[2];
}

unsigned zimg2_get_api_version(void)
{
	return ZIMG_API_VERSION;
}

#define EX_BEGIN \
  int ret = 0; \
  try {
#define EX_END \
  } catch (const zimg::ZimgException &e) { \
    ret = handle_exception(e); \
  } \
  return ret;

void zimg2_filter_free(zimg_filter *ptr)
{
	delete ptr;
}

int zimg2_filter_get_flags(const zimg_filter *ptr, zimg_filter_flags *flags, unsigned version)
{
	EX_BEGIN
	export_filter_flags(cast_filter_ptr(ptr)->get_flags(), flags, version);
	EX_END
}

int zimg2_filter_get_required_row_range(const zimg_filter *ptr, unsigned i, unsigned *first, unsigned *second)
{
	EX_BEGIN
	auto range = cast_filter_ptr(ptr)->get_required_row_range(i);
	*first = range.first;
	*second = range.second;
	EX_END
}

int zimg2_filter_get_required_col_range(const zimg_filter *ptr, unsigned left, unsigned right, unsigned *first, unsigned *second)
{
	EX_BEGIN
	auto range = cast_filter_ptr(ptr)->get_required_col_range(left, right);
	*first = range.first;
	*second = range.second;
	EX_END
}

int zimg2_filter_get_simultaneous_lines(const zimg_filter *ptr, unsigned *out)
{
	EX_BEGIN
	*out = cast_filter_ptr(ptr)->get_simultaneous_lines();
	EX_END
}

int zimg2_filter_get_context_size(const zimg_filter *ptr, size_t *out)
{
	EX_BEGIN
	*out = cast_filter_ptr(ptr)->get_context_size();
	EX_END
}

int zimg2_filter_get_tmp_size(const zimg_filter *ptr, unsigned left, unsigned right, size_t *out)
{
	EX_BEGIN
	*out = cast_filter_ptr(ptr)->get_tmp_size(left, right);
	EX_END
}

int zimg2_filter_init_context(const zimg_filter *ptr, void *ctx)
{
	EX_BEGIN
	cast_filter_ptr(ptr)->init_context(ctx);
	EX_END
}

int zimg2_filter_process(const zimg_filter *ptr, void *ctx, const zimg_image_buffer *src, const zimg_image_buffer *dst, void *tmp, unsigned i, unsigned left, unsigned right)
{
	EX_BEGIN
	const zimg::IZimgFilter *filter_ptr = cast_filter_ptr(ptr);

	assert(src);
	assert(dst);

	POINTER_ALIGNMENT_ASSERT(src->data[0]);
	POINTER_ALIGNMENT_ASSERT(src->data[1]);
	POINTER_ALIGNMENT_ASSERT(src->data[2]);
	POINTER_ALIGNMENT_ASSERT(dst->data[0]);
	POINTER_ALIGNMENT_ASSERT(dst->data[1]);
	POINTER_ALIGNMENT_ASSERT(dst->data[2]);

	STRIDE_ALIGNMENT_ASSERT(src->stride[0]);
	STRIDE_ALIGNMENT_ASSERT(src->stride[1]);
	STRIDE_ALIGNMENT_ASSERT(src->stride[2]);
	STRIDE_ALIGNMENT_ASSERT(dst->stride[0]);
	STRIDE_ALIGNMENT_ASSERT(dst->stride[1]);
	STRIDE_ALIGNMENT_ASSERT(dst->stride[2]);

	zimg::ZimgImageBuffer src_buf = import_image_buffer(*src);
	zimg::ZimgImageBuffer dst_buf = import_image_buffer(*dst);

	filter_ptr->process(ctx, &src_buf, &dst_buf, tmp, i, left, right);
	EX_END
}

#undef EX_BEGIN
#undef EX_END


void zimg2_colorspace_params_default(zimg_colorspace_params *ptr, unsigned version)
{
	assert(ptr);
	API_VERSION_ASSERT(version);

	if (version >= 2) {
		ptr->version = version;

		ptr->width = 0;
		ptr->height = 0;

		ptr->matrix_in = 2;
		ptr->transfer_in = 2;
		ptr->primaries_in = 2;

		ptr->matrix_out = 2;
		ptr->transfer_out = 2;
		ptr->primaries_out = 2;

		ptr->pixel_type = -1;
		ptr->depth = 0;
		ptr->range = 0;
	}
}

zimg_filter *zimg2_colorspace_create(const zimg_colorspace_params *params)
{
	assert(params);
	API_VERSION_ASSERT(params->version);

	try {
		zimg::colorspace::ColorspaceDefinition csp_in{};
		zimg::colorspace::ColorspaceDefinition csp_out{};

		zimg::PixelType pixel_type{};

		if (params->version >= 2) {
			csp_in.matrix = translate_matrix(params->matrix_in);
			csp_in.transfer = translate_transfer(params->transfer_in);
			csp_in.primaries = translate_primaries(params->primaries_in);

			csp_out.matrix = translate_matrix(params->matrix_out);
			csp_out.transfer = translate_transfer(params->transfer_out);
			csp_out.primaries = translate_primaries(params->primaries_out);

			pixel_type = translate_pixel_type(params->pixel_type);
		}

		if (pixel_type != zimg::PixelType::FLOAT)
			throw zimg::ZimgUnsupportedError{ "colorspace only supports FLOAT" };

		return new(std::nothrow) zimg::colorspace::ColorspaceConversion2{ csp_in, csp_out, g_cpu_type };
	} catch (const zimg::ZimgException &e) {
		handle_exception(e);
		return nullptr;
	}
}


void zimg2_depth_params_default(zimg_depth_params *ptr, unsigned version)
{
	assert(ptr);
	API_VERSION_ASSERT(version);

	if (version >= 2) {
		ptr->version = version;

		ptr->width = 0;
		ptr->height = 0;

		ptr->dither_type = ZIMG_DITHER_NONE;
		ptr->chroma = 0;

		ptr->pixel_in = -1;
		ptr->depth_in = 0;
		ptr->range_in = ZIMG_RANGE_LIMITED;

		ptr->pixel_out = -1;
		ptr->depth_out = 0;
		ptr->range_out = ZIMG_RANGE_LIMITED;
	}
}

zimg_filter *zimg2_depth_create(const zimg_depth_params *params)
{
	assert(params);
	API_VERSION_ASSERT(params->version);

	try {
		zimg::PixelFormat pixel_in{};
		zimg::PixelFormat pixel_out{};
		zimg::depth::DitherType dither = zimg::depth::DitherType::DITHER_NONE;
		unsigned width = 0;

		if (params->version >= 2) {
			width = params->width;
			dither = translate_dither(params->dither_type);

			pixel_in.type = translate_pixel_type(params->pixel_in);
			pixel_in.chroma = !!params->chroma;

			if (pixel_in.type == zimg::PixelType::BYTE || pixel_in.type == zimg::PixelType::WORD) {
				pixel_in.depth = params->depth_in;
				pixel_in.fullrange = translate_pixel_range(params->range_in);
			}

			pixel_out.type = translate_pixel_type(params->pixel_out);
			pixel_out.chroma = !!params->chroma;

			if (pixel_in.type == zimg::PixelType::BYTE || pixel_in.type == zimg::PixelType::WORD) {
				pixel_out.depth = params->depth_out;
				pixel_out.fullrange = translate_pixel_range(params->range_out);
			}
		}

		return new(std::nothrow) zimg::depth::Depth2{ dither, width, pixel_in, pixel_out, g_cpu_type };
	} catch (const zimg::ZimgException &e) {
		handle_exception(e);
		return nullptr;
	}
}


void zimg2_resize_params_default(zimg_resize_params *ptr, unsigned version)
{
	assert(ptr);
	API_VERSION_ASSERT(version);

	if (version >= 2) {
		ptr->version = version;

		ptr->src_width = 0;
		ptr->src_height = 0;
		ptr->dst_width = 0;
		ptr->dst_height = 0;

		ptr->pixel_type = -1;

		ptr->shift_w = 0;
		ptr->shift_h = 0;
		ptr->subheight = NAN;
		ptr->subheight = NAN;

		ptr->filter_type = ZIMG_RESIZE_POINT;
		ptr->filter_param_a = NAN;
		ptr->filter_param_b = NAN;
	}
}

zimg_filter *zimg2_resize_create(const zimg_resize_params *params)
{
	assert(params);
	API_VERSION_ASSERT(params->version);

	try {
		zimg::PixelType pixel_type{};
		std::unique_ptr<zimg::resize::Filter> filter;

		int src_width = 0;
		int src_height = 0;
		int dst_width = 0;
		int dst_height = 0;

		double shift_w = 0;
		double shift_h = 0;

		double subwidth = NAN;
		double subheight = NAN;

		if (params->version >= 2) {
			src_width = params->src_width;
			src_height = params->src_height;
			dst_width = params->dst_width;
			dst_height = params->dst_height;

			pixel_type = translate_pixel_type(params->pixel_type);

			shift_w = params->shift_w;
			shift_h = params->shift_h;

			subwidth = std::isnan(params->subwidth) ? src_width : params->subwidth;
			subheight = std::isnan(params->subheight) ? src_height : params->subheight;
		}

		return new(std::nothrow) zimg::resize::Resize2{ *filter, pixel_type, src_width, src_height, dst_width, dst_height, shift_w, shift_h, subwidth, subheight, g_cpu_type };
	} catch (const zimg::ZimgException &e) {
		handle_exception(e);
		return nullptr;
	}
}
