/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "tgw/tgw_painter_filters.h"

#include "core/src/filters.h"

#include <QtGui/QPainter>

namespace Tgw {
namespace {

constexpr auto kMaxChain = size_t(32);
constexpr auto kMaxLength = 32767.;
constexpr auto kMaxFactor = 1e6;
constexpr auto kMaxPixels = int64(1) << 24;
constexpr auto kPixelScratch = int64(4 * sizeof(float));
constexpr auto kMinSigma = 0.5f;
constexpr auto kPi = 3.14159265f;
constexpr auto kBlurSlack = 3.;
constexpr auto kOffsetSlack = 1.;
constexpr auto kReducedSigma = 12.f;
constexpr auto kResamplePixelWork = int64(2);

[[nodiscard]] bool UsableLength(float64 value) {
	return (value >= 0.) && (value <= kMaxLength);
}

[[nodiscard]] bool UsableFilter(const twlite::Filter &filter) {
	using Filter = twlite::Filter;
	const auto amount = float64(filter.amount);
	if (!std::isfinite(amount)
		|| !std::isfinite(filter.x)
		|| !std::isfinite(filter.y)) {
		return false;
	}
	switch (filter.kind) {
	case Filter::Blur: return UsableLength(amount);
	case Filter::DropShadow:
		return UsableLength(amount)
			&& (std::abs(filter.x) <= kMaxLength)
			&& (std::abs(filter.y) <= kMaxLength);
	case Filter::HueRotate: return (std::abs(amount) <= kMaxFactor);
	case Filter::Brightness:
	case Filter::Contrast:
	case Filter::Grayscale:
	case Filter::Invert:
	case Filter::Opacity:
	case Filter::Saturate:
	case Filter::Sepia: return (amount >= 0.) && (amount <= kMaxFactor);
	}
	return false;
}

[[nodiscard]] std::optional<float> NarrowScale(float64 scale) {
	if (!(scale > 0.) || !(scale <= std::numeric_limits<float>::max())) {
		return std::nullopt;
	}
	const auto result = float(scale);
	return (std::isfinite(result) && (result > 0.f))
		? std::make_optional(result)
		: std::nullopt;
}

[[nodiscard]] bool ScaledLength(float value, float scaled, float64 scale) {
	return (std::abs(value * scale) <= kMaxLength)
		&& (std::abs(scaled) <= kMaxLength);
}

[[nodiscard]] int64 GaussianWork(int64 width, int64 height, float sigma) {
	if (sigma < kMinSigma) {
		return 0;
	}
	const auto d = int(std::floor(sigma * 3 * std::sqrt(2 * kPi) / 4 + 0.5f));
	const auto r = int64(d / 2 + 1); // gaussian() takes max(1, d / 2).
	return width * height
		+ 3 * (height * (width + 2 * r + 1) + width * (height + 2 * r + 1));
}

// WHY: a lone large blur leaves no detail a smaller copy would lose. Any other
// chain can keep or sharpen detail, so it stays at full size, and the shorter
// side caps the divisor so that padding cannot outgrow a tiny layer.
[[nodiscard]] std::optional<int> ReducedDivisor(
		const twlite::Filters &filters,
		QSize size,
		float64 scale,
		float narrowed) {
	const auto width = int64(size.width());
	const auto height = int64(size.height());
	if (width <= 0
		|| height <= 0
		|| width * height > kMaxPixels
		|| !UsableFilters(filters)) {
		return std::nullopt;
	}
	for (const auto &filter : filters) {
		const auto radius = filter.amount * narrowed;
		if (filter.kind == twlite::Filter::Blur) {
			if (!ScaledLength(filter.amount, radius, scale)) {
				return std::nullopt;
			}
		} else if (filter.kind == twlite::Filter::DropShadow) {
			if (!ScaledLength(filter.amount, radius, scale)
				|| !ScaledLength(filter.x, filter.x * narrowed, scale)
				|| !ScaledLength(filter.y, filter.y * narrowed, scale)) {
				return std::nullopt;
			}
		}
	}
	if (filters.size() != 1 || filters.front().kind != twlite::Filter::Blur) {
		return 1;
	}
	const auto sigma = filters.front().amount * narrowed;
	const auto side = std::min(size.width(), size.height());
	auto result = 1;
	while ((result <= side / 2)
		&& (sigma / float(result * 2) >= kReducedSigma)) {
		result *= 2;
	}
	return result;
}

[[nodiscard]] QSize ReducedSize(QSize size, int divisor) {
	return QSize(
		(size.width() + divisor - 1) / divisor,
		(size.height() + divisor - 1) / divisor);
}

[[nodiscard]] bool RunPass(
		QImage &image,
		const twlite::Filters &filters,
		float scale) {
	constexpr auto bgra = (Q_BYTE_ORDER == Q_LITTLE_ENDIAN);
	if constexpr (!bgra) {
		// filter_pixels() reads alpha last, big-endian ARGB32 stores it first.
		image.convertTo(QImage::Format_RGBA8888_Premultiplied);
	}
	try {
		twlite::filter_pixels(
			image.bits(),
			image.width(),
			image.height(),
			size_t(image.bytesPerLine()),
			bgra,
			filters,
			scale);
	} catch (const std::bad_alloc &) {
		return false;
	}
	if constexpr (!bgra) {
		image.convertTo(QImage::Format_ARGB32_Premultiplied);
	}
	return true;
}

} // namespace

bool UsableFilters(const twlite::Filters &filters) {
	return (filters.size() <= kMaxChain)
		&& ranges::all_of(filters, UsableFilter);
}

QMarginsF FilterReach(const twlite::Filters &filters, float64 scale) {
	auto result = QMarginsF();
	for (const auto &filter : filters) {
		const auto amount = std::max(float64(filter.amount), 0.) * scale;
		if (filter.kind == twlite::Filter::Blur) {
			const auto spread = 3. * amount + kBlurSlack;
			result += QMarginsF(spread, spread, spread, spread);
		} else if (filter.kind == twlite::Filter::DropShadow) {
			const auto spread = 1.5 * amount + kBlurSlack + kOffsetSlack;
			const auto x = filter.x * scale;
			const auto y = filter.y * scale;
			result += QMarginsF(
				std::max(spread - x, 0.),
				std::max(spread - y, 0.),
				std::max(spread + x, 0.),
				std::max(spread + y, 0.));
		}
	}
	return result;
}

// WHY: twlite::filter_pixels() bounds nothing itself: its buffers and loops
// follow its arguments, so each of its passes, zero fills included, is priced
// here in its own float arithmetic before the executor lets it run.
std::optional<FilterCost> MeasureFilters(
		const twlite::Filters &filters,
		QSize size,
		float64 scale) {
	const auto narrowed = NarrowScale(scale);
	const auto divisor = narrowed
		? ReducedDivisor(filters, size, scale, *narrowed)
		: std::nullopt;
	if (!divisor) {
		return std::nullopt;
	}
	const auto reduced = ReducedSize(size, *divisor);
	const auto width = int64(reduced.width());
	const auto height = int64(reduced.height());
	const auto pixels = width * height;
	const auto passScale = *narrowed / float(*divisor);
	auto work = 3 * pixels;
	auto blur = false;
	auto shadow = false;
	for (const auto &filter : filters) {
		const auto radius = filter.amount * passScale;
		if (filter.kind == twlite::Filter::Blur) {
			work += GaussianWork(width, height, radius);
			blur = true;
		} else if (filter.kind == twlite::Filter::DropShadow) {
			work += 3 * pixels + GaussianWork(width, height, radius / 2);
			shadow = true;
		} else {
			work += pixels;
		}
	}
	const auto copies = std::max(blur ? 1 : 0, shadow ? 2 : 0);
	auto result = FilterCost{
		.work = work,
		.scratch = kPixelScratch * pixels * (1 + copies),
	};
	if (*divisor > 1) {
		const auto padded = pixels * *divisor * *divisor;
		result.work += kResamplePixelWork * padded;
		result.scratch += int64(4) * (padded + pixels);
	}
	return result;
}

bool ApplyFilters(
		QImage &image,
		const twlite::Filters &filters,
		float64 scale) {
	const auto narrowed = NarrowScale(scale);
	if (!narrowed || image.format() != QImage::Format_ARGB32_Premultiplied) {
		return false;
	}
	const auto divisor = ReducedDivisor(
		filters,
		image.size(),
		scale,
		*narrowed);
	if (!divisor) {
		return false;
	} else if (*divisor == 1) {
		return RunPass(image, filters, *narrowed);
	}
	const auto reduced = ReducedSize(image.size(), *divisor);
	// Whole low pixels keep the ratio exact, and the image clips them back.
	const auto padded = QRect(
		0,
		0,
		reduced.width() * *divisor,
		reduced.height() * *divisor);
	auto low = image.copy(padded).scaled(
		reduced,
		Qt::IgnoreAspectRatio,
		Qt::SmoothTransformation);
	if (low.isNull() || !RunPass(low, filters, *narrowed / float(*divisor))) {
		return false;
	}
	auto painter = QPainter();
	if (!painter.begin(&image)) {
		return false;
	}
	painter.setCompositionMode(QPainter::CompositionMode_Source);
	painter.setRenderHint(QPainter::SmoothPixmapTransform);
	painter.drawImage(QRectF(padded), low);
	return true;
}

} // namespace Tgw
