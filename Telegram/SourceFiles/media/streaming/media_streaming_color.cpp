/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/streaming/media_streaming_color.h"

#include "media/streaming/media_streaming_common.h"

#include <crl/crl_async.h>
#include <crl/crl_semaphore.h>

#include <thread>

extern "C" {
#include <libavutil/mastering_display_metadata.h>
#include <libavutil/pixdesc.h>
} // extern "C"

namespace Media::Streaming {
namespace {

constexpr auto kChunkRows = 16;
constexpr auto kMaxChunkThreads = 8;
constexpr auto kMinParallelPixels = 512 * 512;
constexpr auto kWhiteNits = 203.;
constexpr auto kDefaultPeakNits = 1000.;
constexpr auto kMaxNits = 10000.;
constexpr auto kPqM1 = 2610. / 16384.;
constexpr auto kPqM2 = 2523. / 4096. * 128.;
constexpr auto kPqC1 = 3424. / 4096.;
constexpr auto kPqC2 = 2413. / 4096. * 32.;
constexpr auto kPqC3 = 2392. / 4096. * 32.;
constexpr auto kHlgA = 0.17883277;
constexpr auto kHlgB = 0.28466892;
constexpr auto kHlgC = 0.55991073;
constexpr auto kHlgGamma = 1.2;
constexpr auto kOutputGamma = 2.2;
constexpr auto kLutSize = 4096;

struct Coefficients {
	float64 red = 0.;
	float64 blue = 0.;
};

struct ToneMapTables {
	FrameColor::Transfer transfer = FrameColor::Transfer::SDR;
	float64 peak = 0.;
	std::vector<float> signal;
	std::vector<float> gain;
	std::vector<float> ratio;
	std::vector<uchar> encode;
};

[[nodiscard]] Coefficients MatrixCoefficients(FrameColor::Matrix matrix) {
	switch (matrix) {
	case FrameColor::Matrix::BT709: return { 0.2126, 0.0722 };
	case FrameColor::Matrix::BT2020: return { 0.2627, 0.0593 };
	}
	return { 0.299, 0.114 };
}

[[nodiscard]] float64 PqToLinear(float64 value) {
	const auto p = std::pow(std::clamp(value, 0., 1.), 1. / kPqM2);
	return std::pow(std::max(p - kPqC1, 0.) / (kPqC2 - kPqC3 * p), 1. / kPqM1);
}

[[nodiscard]] float64 LinearToPq(float64 value) {
	const auto p = std::pow(std::max(value, 0.), kPqM1);
	return std::pow((kPqC1 + kPqC2 * p) / (1. + kPqC3 * p), kPqM2);
}

[[nodiscard]] float64 HlgToSceneLinear(float64 value) {
	value = std::clamp(value, 0., 1.);
	return (value <= 0.5)
		? (value * value / 3.)
		: ((std::exp((value - kHlgC) / kHlgA) + kHlgB) / 12.);
}

[[nodiscard]] float64 PeakNits(const FrameColor &color) {
	return (color.transfer == FrameColor::Transfer::PQ && color.peak > 0)
		? std::clamp(float64(color.peak), kWhiteNits, kMaxNits)
		: kDefaultPeakNits;
}

[[nodiscard]] float64 ToneMapNits(
		float64 nits,
		float64 sourcePq,
		float64 targetRatio) {
	const auto e1 = std::min(LinearToPq(nits / kMaxNits) / sourcePq, 1.);
	const auto knee = 1.5 * targetRatio - 0.5;
	if (e1 <= knee) {
		return nits;
	}
	const auto t = (e1 - knee) / (1. - knee);
	const auto t2 = t * t;
	const auto t3 = t2 * t;
	const auto e2 = (2. * t3 - 3. * t2 + 1.) * knee
		+ (t3 - 2. * t2 + t) * (1. - knee)
		+ (-2. * t3 + 3. * t2) * targetRatio;
	return PqToLinear(e2 * sourcePq) * kMaxNits;
}

void ConvertBt2020ToBt709(float &r, float &g, float &b) {
	const auto r2 = 1.660491f * r - 0.587641f * g - 0.072850f * b;
	const auto g2 = -0.124550f * r + 1.132900f * g - 0.008349f * b;
	const auto b2 = -0.018151f * r - 0.100579f * g + 1.118730f * b;
	r = r2;
	g = g2;
	b = b2;
}

template <typename Method>
void ForEachRowChunk(int width, int height, Method method) {
	const auto chunks = (height + kChunkRows - 1) / kChunkRows;
	const auto threads = std::min(
		int(std::thread::hardware_concurrency()),
		kMaxChunkThreads);
	if (threads < 2 || chunks < 2 || width * height < kMinParallelPixels) {
		method(0, height);
		return;
	}
	struct State {
		std::atomic<int> next = 0;
		std::atomic<int> left = 0;
		crl::semaphore finished;
	};
	const auto state = std::make_shared<State>();
	state->left = chunks;
	const auto work = [=, &method] {
		while (true) {
			const auto index = state->next++;
			if (index >= chunks) {
				return;
			}
			const auto from = index * kChunkRows;
			method(from, std::min(from + kChunkRows, height));
			if (--state->left == 0) {
				state->finished.release();
			}
		}
	};
	for (auto i = 1; i != threads; ++i) {
		crl::async(work);
	}
	work();
	state->finished.acquire();
}

[[nodiscard]] const ToneMapTables &LookupToneMapTables(
		const FrameColor &color,
		float64 sourcePq,
		float64 targetRatio) {
	thread_local auto result = ToneMapTables();
	const auto peak = PeakNits(color);
	if (result.transfer == color.transfer && result.peak == peak) {
		return result;
	}
	const auto hlg = (color.transfer == FrameColor::Transfer::HLG);
	const auto relative = peak / kWhiteNits;
	const auto lutMax = float64(kLutSize - 1);
	result.transfer = color.transfer;
	result.peak = peak;
	result.signal.resize(kLutSize);
	result.gain.resize(hlg ? kLutSize : 0);
	result.ratio.resize(kLutSize);
	result.encode.resize(kLutSize);
	for (auto i = 0; i != kLutSize; ++i) {
		const auto value = i / lutMax;
		result.signal[i] = float(hlg
			? HlgToSceneLinear(value)
			: (PqToLinear(value) * kMaxNits / kWhiteNits));
		if (hlg) {
			result.gain[i] = float(kDefaultPeakNits
				/ kWhiteNits
				* std::pow(value * value, kHlgGamma - 1.));
		}
		const auto white = std::max(value * relative, 1e-6);
		result.ratio[i] = float(ToneMapNits(
			white * kWhiteNits,
			sourcePq,
			targetRatio) / kWhiteNits / white);
		result.encode[i] = uchar(base::SafeRound(
			std::pow(value * value, 1. / kOutputGamma) * 255.));
	}
	return result;
}

[[nodiscard]] bool ToneMapToARGB32(
		const FrameYUV &yuv,
		bool nv12,
		const FrameColor &color,
		QImage &storage) {
	Expects(color.hdr());

	if (storage.size() != yuv.size
		|| storage.format() != QImage::Format_ARGB32_Premultiplied
		|| !yuv.y.data
		|| !yuv.u.data
		|| (!nv12 && !yuv.v.data)) {
		return false;
	}
	const auto uniforms = PrepareColorUniforms(
		color,
		nv12,
		yuv.highBitDepth);
	const auto &m = uniforms.yuvToRgb;
	const auto &tables = LookupToneMapTables(
		color,
		uniforms.hdr[2],
		uniforms.hdr[3]);
	const auto &signal = tables.signal;
	const auto &gain = tables.gain;
	const auto &ratio = tables.ratio;
	const auto &encode = tables.encode;
	const auto index = [](float value) {
		return std::clamp(
			int(value * float(kLutSize - 1) + 0.5f),
			0,
			kLutSize - 1);
	};
	const auto [kr, kb] = MatrixCoefficients(FrameColor::Matrix::BT2020);
	const auto lumaRed = float(kr);
	const auto lumaGreen = float(1. - kr - kb);
	const auto lumaBlue = float(kb);
	const auto unit = yuv.highBitDepth ? (1.f / 65535.f) : (1.f / 255.f);
	const auto peak = float(tables.peak / kWhiteNits);
	const auto hlg = (color.transfer == FrameColor::Transfer::HLG);
	const auto wide = color.wideGamut;
	const auto width = yuv.size.width();
	const auto height = yuv.size.height();
	const auto yData = static_cast<const uchar*>(yuv.y.data);
	const auto uData = static_cast<const uchar*>(yuv.u.data);
	const auto vData = static_cast<const uchar*>(yuv.v.data);
	const auto outData = storage.bits();
	const auto outStride = storage.bytesPerLine();
	const auto convert = [&](auto sample) {
		using Sample = decltype(sample);
		ForEachRowChunk(width, height, [&](int from, int till) {
			for (auto y = from; y != till; ++y) {
				const auto yLine = reinterpret_cast<const Sample*>(
					yData + y * yuv.y.stride);
				const auto uLine = reinterpret_cast<const Sample*>(
					uData + (y / 2) * yuv.u.stride);
				const auto vLine = nv12
					? nullptr
					: reinterpret_cast<const Sample*>(
						vData + (y / 2) * yuv.v.stride);
				const auto out = reinterpret_cast<uint32*>(
					outData + y * outStride);
				for (auto x = 0; x != width; ++x) {
					const auto cx = x / 2;
					const auto ly = yLine[x] * unit;
					const auto lu = (nv12 ? uLine[cx * 2] : uLine[cx]) * unit;
					const auto lv = (nv12 ? uLine[cx * 2 + 1] : vLine[cx])
						* unit;
					const auto rs = m[0] * ly + m[4] * lu + m[8] * lv + m[12];
					const auto gs = m[1] * ly + m[5] * lu + m[9] * lv + m[13];
					const auto bs = m[2] * ly + m[6] * lu + m[10] * lv + m[14];
					auto r = signal[index(rs)];
					auto g = signal[index(gs)];
					auto b = signal[index(bs)];
					if (hlg) {
						const auto k = gain[index(std::sqrt(
							lumaRed * r + lumaGreen * g + lumaBlue * b))];
						r *= k;
						g *= k;
						b *= k;
					}
					if (wide) {
						ConvertBt2020ToBt709(r, g, b);
					}
					r = std::max(r, 0.f);
					g = std::max(g, 0.f);
					b = std::max(b, 0.f);
					const auto top = std::max({ r, g, b });
					const auto k = (top >= peak)
						? (1.f / top)
						: ratio[index(top / peak)];
					out[x] = 0xFF000000U
						| (uint32(encode[index(std::sqrt(r * k))]) << 16)
						| (uint32(encode[index(std::sqrt(g * k))]) << 8)
						| uint32(encode[index(std::sqrt(b * k))]);
				}
			}
		});
	};
	if (yuv.highBitDepth) {
		convert(uint16());
	} else {
		convert(uchar());
	}
	return true;
}

[[nodiscard]] bool ToneMapFrame(
		not_null<const AVFrame*> frame,
		const FrameColor &color,
		QImage &storage,
		FFmpeg::SwscalePointer &swscale) {
	const auto size = QSize(frame->width, frame->height);
	const auto format = frame->format;
	const auto direct = std::array{
		AV_PIX_FMT_YUV420P,
		AV_PIX_FMT_YUVJ420P,
		AV_PIX_FMT_NV12,
		AV_PIX_FMT_YUV420P10LE,
		AV_PIX_FMT_P010LE,
	};
	if (storage.size() == size && ranges::contains(direct, format)) {
		return ToneMapToARGB32(
			ExtractYUV(frame),
			(format == AV_PIX_FMT_NV12) || (format == AV_PIX_FMT_P010LE),
			color,
			storage);
	}
	auto converted = FFmpeg::MakeFramePointer();
	if (!converted) {
		return false;
	}
	converted->format = AV_PIX_FMT_P010LE;
	converted->width = storage.width();
	converted->height = storage.height();
	if (av_frame_get_buffer(converted.get(), 0) < 0) {
		return false;
	}
	swscale = FFmpeg::MakeSwscalePointer(
		size,
		format,
		storage.size(),
		AV_PIX_FMT_P010LE,
		&swscale);
	if (!swscale) {
		return false;
	}
	auto inverse = (int*)nullptr;
	auto table = (int*)nullptr;
	auto srcRange = 0;
	auto dstRange = 0;
	auto brightness = 0;
	auto contrast = 0;
	auto saturation = 0;
	const auto details = sws_getColorspaceDetails(
		swscale.get(),
		&inverse,
		&srcRange,
		&table,
		&dstRange,
		&brightness,
		&contrast,
		&saturation);
	if (details >= 0 && srcRange != dstRange) {
		sws_setColorspaceDetails(
			swscale.get(),
			inverse,
			srcRange,
			table,
			srcRange,
			brightness,
			contrast,
			saturation);
	}
	sws_scale(
		swscale.get(),
		frame->data,
		frame->linesize,
		0,
		frame->height,
		converted->data,
		converted->linesize);
	return ToneMapToARGB32(ExtractYUV(converted.get()), true, color, storage);
}

[[nodiscard]] bool ToneMappableFormat(int format) {
	const auto descriptor = av_pix_fmt_desc_get(AVPixelFormat(format));
	return descriptor
		&& !(descriptor->flags
			& (AV_PIX_FMT_FLAG_RGB
				| AV_PIX_FMT_FLAG_PAL
				| AV_PIX_FMT_FLAG_HWACCEL));
}

} // namespace

bool WideGamutPrimaries(AVColorPrimaries primaries) {
	return (primaries == AVCOL_PRI_BT2020)
		|| (primaries == AVCOL_PRI_UNSPECIFIED);
}

FrameColor ReadFrameColor(not_null<const AVFrame*> frame, int &peak) {
	auto result = FrameColor();
	switch (frame->color_trc) {
	case AVCOL_TRC_SMPTE2084:
		result.transfer = FrameColor::Transfer::PQ;
		break;
	case AVCOL_TRC_ARIB_STD_B67:
		result.transfer = FrameColor::Transfer::HLG;
		break;
	default:
		return result;
	}
	result.matrix = (frame->colorspace == AVCOL_SPC_BT709)
		? FrameColor::Matrix::BT709
		: (frame->colorspace == AVCOL_SPC_BT470BG
			|| frame->colorspace == AVCOL_SPC_SMPTE170M)
		? FrameColor::Matrix::BT601
		: FrameColor::Matrix::BT2020;
	result.wideGamut = WideGamutPrimaries(frame->color_primaries);
	result.fullRange = (frame->color_range == AVCOL_RANGE_JPEG);
	if (result.transfer != FrameColor::Transfer::PQ) {
		return result;
	}
	const auto light = av_frame_get_side_data(
		frame.get(),
		AV_FRAME_DATA_CONTENT_LIGHT_LEVEL);
	if (light && light->size >= sizeof(AVContentLightMetadata)) {
		const auto data = reinterpret_cast<const AVContentLightMetadata*>(
			light->data);
		result.peak = int(data->MaxCLL);
	}
	const auto mastering = av_frame_get_side_data(
		frame.get(),
		AV_FRAME_DATA_MASTERING_DISPLAY_METADATA);
	if (!result.peak
		&& mastering
		&& mastering->size >= sizeof(AVMasteringDisplayMetadata)) {
		const auto data = reinterpret_cast<const AVMasteringDisplayMetadata*>(
			mastering->data);
		if (data->has_luminance) {
			result.peak = int(av_q2d(data->max_luminance));
		}
	}
	if (result.peak > 0) {
		peak = result.peak;
	} else {
		result.peak = peak;
	}
	return result;
}

FrameYUV ExtractYUV(not_null<const AVFrame*> frame) {
	return {
		.size = { frame->width, frame->height },
		.chromaSize = {
			AV_CEIL_RSHIFT(frame->width, 1), // SWScale does that.
			AV_CEIL_RSHIFT(frame->height, 1)
		},
		.y = { .data = frame->data[0], .stride = frame->linesize[0] },
		.u = { .data = frame->data[1], .stride = frame->linesize[1] },
		.v = { .data = frame->data[2], .stride = frame->linesize[2] },
		.highBitDepth = (frame->format == AV_PIX_FMT_YUV420P10LE)
			|| (frame->format == AV_PIX_FMT_P010LE),
	};
}

ColorUniforms PrepareColorUniforms(
		const FrameColor &color,
		bool nv12,
		bool highBitDepth) {
	const auto bits = highBitDepth ? 10 : 8;
	const auto steps = float64((1 << bits) - 1);
	const auto unit = float64(1 << (bits - 8));
	const auto scale = !highBitDepth
		? 255.
		: nv12
		? (65535. / 64.)
		: 65535.;
	const auto lumaScale = color.fullRange
		? (scale / steps)
		: (scale / (219. * unit));
	const auto lumaShift = color.fullRange ? 0. : (-16. / 219.);
	const auto chromaScale = color.fullRange
		? (scale / steps)
		: (scale / (224. * unit));
	const auto chromaShift = color.fullRange
		? (-(1 << (bits - 1)) / steps)
		: (-128. / 224.);
	const auto [kr, kb] = MatrixCoefficients(color.matrix);
	const auto kg = 1. - kr - kb;
	const auto crToR = 2. * (1. - kr);
	const auto cbToB = 2. * (1. - kb);
	const auto cbToG = -2. * kb * (1. - kb) / kg;
	const auto crToG = -2. * kr * (1. - kr) / kg;

	auto result = ColorUniforms();
	result.yuvToRgb = {
		float(lumaScale),
		float(lumaScale),
		float(lumaScale),
		0.f,

		0.f,
		float(cbToG * chromaScale),
		float(cbToB * chromaScale),
		0.f,

		float(crToR * chromaScale),
		float(crToG * chromaScale),
		0.f,
		0.f,

		float(lumaShift + crToR * chromaShift),
		float(lumaShift + (cbToG + crToG) * chromaShift),
		float(lumaShift + cbToB * chromaShift),
		1.f,
	};
	if (color.hdr()) {
		const auto sourcePq = LinearToPq(PeakNits(color) / kMaxNits);
		result.hdr = {
			(color.transfer == FrameColor::Transfer::PQ) ? 1.f : 2.f,
			color.wideGamut ? 1.f : 0.f,
			float(sourcePq),
			float(LinearToPq(kWhiteNits / kMaxNits) / sourcePq),
		};
	}
	return result;
}

bool NeedsToneMapping(AVColorTransferCharacteristic transfer, int format) {
	return (transfer == AVCOL_TRC_SMPTE2084
			|| transfer == AVCOL_TRC_ARIB_STD_B67)
		&& ToneMappableFormat(format);
}

bool ConvertFrameToARGB32(
		not_null<const AVFrame*> frame,
		int format,
		const FrameColor &color,
		QImage &storage,
		FFmpeg::SwscalePointer &swscale) {
	if (color.hdr() && ToneMappableFormat(frame->format)) {
		return ToneMapFrame(frame, color, storage, swscale);
	}
	swscale = FFmpeg::MakeSwscalePointer(
		QSize(frame->width, frame->height),
		format,
		storage.size(),
		AV_PIX_FMT_BGRA,
		&swscale);
	if (!swscale) {
		return false;
	}
	uint8_t *data[AV_NUM_DATA_POINTERS] = { storage.bits(), nullptr };
	int linesize[AV_NUM_DATA_POINTERS] = { int(storage.bytesPerLine()), 0 };
	sws_scale(
		swscale.get(),
		frame->data,
		frame->linesize,
		0,
		frame->height,
		data,
		linesize);
	return true;
}

QImage ConvertYUVToARGB32(
		const FrameYUV &yuv,
		bool nv12,
		const FrameColor &color) {
	Expects(yuv.y.data != nullptr);
	Expects(yuv.u.data != nullptr);
	Expects(nv12 || (yuv.v.data != nullptr));
	Expects(!yuv.size.isEmpty());

	auto result = FFmpeg::CreateFrameStorage(yuv.size);
	if (result.isNull()) {
		return QImage();
	} else if (color.hdr()) {
		return ToneMapToARGB32(yuv, nv12, color, result) ? result : QImage();
	}
	const auto format = yuv.highBitDepth
		? (nv12 ? AV_PIX_FMT_P010LE : AV_PIX_FMT_YUV420P10LE)
		: (nv12 ? AV_PIX_FMT_NV12 : AV_PIX_FMT_YUV420P);
	const auto swscale = FFmpeg::MakeSwscalePointer(
		yuv.size,
		format,
		yuv.size,
		AV_PIX_FMT_BGRA);
	if (!swscale) {
		return QImage();
	}
	const uint8_t *srcData[AV_NUM_DATA_POINTERS] = {
		static_cast<const uint8_t*>(yuv.y.data),
		static_cast<const uint8_t*>(yuv.u.data),
		static_cast<const uint8_t*>(yuv.v.data),
		nullptr,
	};
	int srcLinesize[AV_NUM_DATA_POINTERS] = {
		yuv.y.stride,
		yuv.u.stride,
		yuv.v.stride,
		0,
	};
	uint8_t *dstData[AV_NUM_DATA_POINTERS] = { result.bits(), nullptr };
	int dstLinesize[AV_NUM_DATA_POINTERS] = { int(result.bytesPerLine()), 0 };

	sws_scale(
		swscale.get(),
		srcData,
		srcLinesize,
		0,
		yuv.size.height(),
		dstData,
		dstLinesize);

	return result;
}

} // namespace Media::Streaming
