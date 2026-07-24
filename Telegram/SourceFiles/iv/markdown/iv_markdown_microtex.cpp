/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "iv/markdown/iv_markdown_microtex.h"
#include "base/base_file_utilities.h"
#include "platform/qt/graphic_qt.h"
#include "latex.h"

#include <QtCore/QSize>
#include <QtCore/QString>
#include <QtGui/QPainter>

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <limits>
#include <memory>
#include <mutex>
#include <utility>

#ifndef Q_OS_MAC
void InitIvMarkdownMicrotexBundledResource() {
	Q_INIT_RESOURCE(bundled);
}
#endif // !Q_OS_MAC

namespace Iv::Markdown {
namespace {

constexpr auto kBytesPerPixel = int64(4);
constexpr auto kMaxFormulaImageBytes = int64(128) * 1024 * 1024;
constexpr auto kFormulaForegroundRgba = 0xFFFFFFFFU;

std::once_flag MicrotexInitOnce;
bool MicrotexInitialized = false;
QString MicrotexInitError;

struct PreparedMicrotexRequest {
	QString trimmedTex;
	MathKind kind = MathKind::Display;
	int textSize = 0;
	int renderWidthCap = 0;
	int renderHeightCap = 0;
	int metricTextSize = 0;
	int metricRenderWidthCap = 0;
	int metricRenderHeightCap = 0;
};

struct ParsedMicrotexFormula {
	std::unique_ptr<tex::TeXRender> render;
	MeasuredFormula measured;
};

[[nodiscard]] QString ExceptionText(const std::exception &exception) {
	return QString::fromUtf8(exception.what());
}

[[nodiscard]] std::wstring ToWide(const QString &value) {
	auto result = std::wstring();
	result.reserve(size_t(value.size()));
	for (const auto ch : value) {
		result.push_back(wchar_t(ch.unicode()));
	}
	return result;
}

[[nodiscard]] bool PhysicalImageRejected(int64 width, int64 height) {
	if (width <= 0 || height <= 0) {
		return true;
	}
	return (width > std::numeric_limits<int>::max())
		|| (height > std::numeric_limits<int>::max())
		|| (width * height > (kMaxFormulaImageBytes / kBytesPerPixel));
}

[[nodiscard]] bool ComputePhysicalSize(
		QSize logicalSize,
		int ratio,
		QSize *physicalSize) {
	const auto width = int64(logicalSize.width()) * ratio;
	const auto height = int64(logicalSize.height()) * ratio;
	if (PhysicalImageRejected(width, height)) {
		return false;
	}
	if (physicalSize) {
		*physicalSize = QSize(int(width), int(height));
	}
	return true;
}

// MicroTeX lays the outer alignment environments (align, gather, equation,
// ...) out to the full text width we pass as the render width cap, and
// TeXRender::getWidth() adds insets on top, so their measured width always
// lands a couple of units above the cap and the logical-size-cap check
// below rejects them no matter the content. Their inner variants (aligned,
// gathered, ...) produce the same rows at natural width, which also fits
// chat bubbles better than a forced full-width block.
[[nodiscard]] QString RewriteFullWidthEnvironments(QString tex) {
	if (!tex.contains(u"\\begin{"_q)) {
		return tex;
	}
	static const auto kRewrites = std::array<std::pair<QString, QString>, 12>{{
		{ u"align*"_q, u"aligned"_q },
		{ u"align"_q, u"aligned"_q },
		{ u"flalign*"_q, u"aligned"_q },
		{ u"flalign"_q, u"aligned"_q },
		{ u"alignat*"_q, u"alignedat"_q },
		{ u"alignat"_q, u"alignedat"_q },
		{ u"gather*"_q, u"gathered"_q },
		{ u"gather"_q, u"gathered"_q },
		{ u"multline*"_q, u"gathered"_q },
		{ u"multline"_q, u"gathered"_q },
		// MicroTeX expands equation to align, so it is full-width too.
		{ u"equation*"_q, u"gathered"_q },
		{ u"equation"_q, u"gathered"_q },
	}};
	for (const auto &[from, to] : kRewrites) {
		tex.replace(
			u"\\begin{"_q + from + u'}',
			u"\\begin{"_q + to + u'}');
		tex.replace(
			u"\\end{"_q + from + u'}',
			u"\\end{"_q + to + u'}');
	}
	return tex;
}

[[nodiscard]] QString PreparedTeX(MathKind kind, const QString &trimmedTex) {
	auto result = RewriteFullWidthEnvironments(trimmedTex);
	if (kind == MathKind::Display) {
		result = u"\\displaystyle "_q + result;
	}
	return result;
}

[[nodiscard]] bool TooLargeFailure(const QString &error) {
	return (error == u"render-width-cap-exceeded"_q)
		|| (error == u"logical-size-cap-exceeded"_q)
		|| (error == u"physical-image-cap-exceeded"_q);
}

[[nodiscard]] bool ScaleMetricValue(
		int value,
		int *scaled) {
	if (!scaled) {
		return false;
	}
	const auto scaledValue = int64(value) * kFormulaExactMetricScale;
	if (scaledValue > std::numeric_limits<int>::max()) {
		return false;
	}
	*scaled = int(scaledValue);
	return true;
}

[[nodiscard]] int RoundedLogicalMetric(int scaledValue) {
	return (scaledValue > 0)
		? ((scaledValue + kFormulaExactMetricScale - 1)
			/ kFormulaExactMetricScale)
		: 0;
}

[[nodiscard]] FormulaExactMetrics ExtractExactMetrics(
		tex::TeXRender &render) {
	const auto scaledSize = QSize(
		render.getWidth(),
		render.getHeight());
	const auto scaledAscent = std::clamp(
		int(std::lround(render.getBaseline() * scaledSize.height())),
		0,
		scaledSize.height());
	const auto insets = render.getInsets();
	return {
		.scaledSize = scaledSize,
		.scaledAscent = scaledAscent,
		.scaledInsets = QMargins(
			insets.left,
			insets.top,
			insets.right,
			insets.bottom),
	};
}

void FillMeasuredMetrics(
		MeasuredFormula *measured,
		const FormulaExactMetrics &exact) {
	if (!measured) {
		return;
	}
	measured->exact = exact;
	measured->logicalSize = QSize(
		RoundedLogicalMetric(exact.scaledSize.width()),
		RoundedLogicalMetric(exact.scaledSize.height()));
	const auto scaledDepth = std::max(
		exact.scaledSize.height() - exact.scaledAscent,
		0);
	measured->logicalDepth = std::clamp(
		RoundedLogicalMetric(scaledDepth),
		0,
		measured->logicalSize.height());
}

void FinalizeFailure(MeasuredFormula *result) {
	if (!result || result->success) {
		return;
	}
	const auto tooLarge = TooLargeFailure(result->error);
	result->overflow = tooLarge;
	result->tooLarge = tooLarge;
}

[[nodiscard]] bool PrepareRequest(
		const MicrotexMeasureRequest &request,
		PreparedMicrotexRequest *prepared,
		MeasuredFormula *result) {
	if (!prepared || !result) {
		return false;
	}
	if (!EnsureMicrotexInitialized(&result->error)) {
		return false;
	}
	if (request.textSize <= 0) {
		result->error = u"invalid-text-size"_q;
		return false;
	}
	if (request.renderWidthCap <= 0) {
		result->error = u"invalid-render-width"_q;
		return false;
	}
	if (request.renderHeightCap <= 0) {
		result->error = u"invalid-render-height"_q;
		return false;
	}
	const auto trimmedTex = request.trimmedTex.trimmed();
	if (trimmedTex.isEmpty()) {
		result->error = u"empty-tex"_q;
		return false;
	}
	auto metricTextSize = 0;
	auto metricRenderWidthCap = 0;
	auto metricRenderHeightCap = 0;
	if (!ScaleMetricValue(request.textSize, &metricTextSize)) {
		result->error = u"text-size-overflow"_q;
		return false;
	}
	if (!ScaleMetricValue(
			request.renderWidthCap,
			&metricRenderWidthCap)) {
		result->error = u"render-width-overflow"_q;
		return false;
	}
	if (!ScaleMetricValue(
			request.renderHeightCap,
			&metricRenderHeightCap)) {
		result->error = u"render-height-overflow"_q;
		return false;
	}
	*prepared = {
		.trimmedTex = trimmedTex,
		.kind = request.kind,
		.textSize = request.textSize,
		.renderWidthCap = request.renderWidthCap,
		.renderHeightCap = request.renderHeightCap,
		.metricTextSize = metricTextSize,
		.metricRenderWidthCap = metricRenderWidthCap,
		.metricRenderHeightCap = metricRenderHeightCap,
	};
	return true;
}

[[nodiscard]] ParsedMicrotexFormula ParseFormula(
		const PreparedMicrotexRequest &request) {
	auto result = ParsedMicrotexFormula();
	try {
		auto render = std::unique_ptr<tex::TeXRender>(tex::LaTeX::parse(
			ToWide(PreparedTeX(request.kind, request.trimmedTex)),
			request.metricRenderWidthCap,
			float(request.metricTextSize),
			float(request.metricTextSize) * 0.25f,
			kFormulaForegroundRgba));
		if (!render) {
			result.measured.error = u"parse-returned-null"_q;
			return result;
		}
		FillMeasuredMetrics(&result.measured, ExtractExactMetrics(*render));
		const auto logicalSize = result.measured.logicalSize;
		if (logicalSize.width() <= 0 || logicalSize.height() <= 0) {
			result.measured.error = u"invalid-render-size"_q;
			return result;
		}
		// In partial mode MicroTeX swallows parse errors inside arguments
		// (e.g. of \displaystyle) and renders an EmptyAtom, which measures
		// 1x1 scaled units. Fall back to the source text instead of
		// painting an invisible formula.
		const auto scaledSize = result.measured.exact.scaledSize;
		if (scaledSize.width() <= 1 && scaledSize.height() <= 1) {
			result.measured.error = u"empty-render"_q;
			return result;
		}
		if (logicalSize.width() > request.renderWidthCap
			|| logicalSize.height() > request.renderHeightCap) {
			result.measured.error = u"logical-size-cap-exceeded"_q;
			result.measured.overflow = true;
			result.measured.tooLarge = true;
			return result;
		}
		result.measured.success = true;
		result.render = std::move(render);
		return result;
	} catch (const std::exception &exception) {
		result.measured.error = ExceptionText(exception);
		return result;
	} catch (...) {
		result.measured.error = u"unknown-exception"_q;
		return result;
	}
}

} // namespace

bool EnsureMicrotexInitialized(QString *error) {
	std::call_once(MicrotexInitOnce, [] {
		try {
#ifdef Q_OS_MAC // Use resources from the .app bundle on macOS.

			base::RegisterBundledResources(u"external_microtex_bundled.rcc"_q);

#else // Q_OS_MAC

			InitIvMarkdownMicrotexBundledResource();

#endif // Q_OS_MAC

			tex::LaTeX::initBundled();
			MicrotexInitialized = true;
		} catch (const std::exception &exception) {
			MicrotexInitError = ExceptionText(exception);
		} catch (...) {
			MicrotexInitError = u"unknown-exception"_q;
		}
	});
	if (!MicrotexInitialized && error) {
		*error = MicrotexInitError;
	} else if (MicrotexInitialized && error) {
		error->clear();
	}
	return MicrotexInitialized;
}

MeasuredFormula MeasureWithMicrotex(const MicrotexMeasureRequest &request) {
	auto prepared = PreparedMicrotexRequest();
	auto result = MeasuredFormula();
	if (!PrepareRequest(request, &prepared, &result)) {
		FinalizeFailure(&result);
		return result;
	}
	return ParseFormula(prepared).measured;
}

MicrotexRenderResult RenderWithMicrotex(const MicrotexRenderRequest &request) {
	auto result = MicrotexRenderResult();
	auto prepared = PreparedMicrotexRequest();
	if (!PrepareRequest(
			MicrotexMeasureRequest{
				.trimmedTex = request.trimmedTex,
				.kind = request.kind,
				.textSize = request.textSize,
				.renderWidthCap = request.renderWidthCap,
				.renderHeightCap = request.renderHeightCap,
			},
			&prepared,
			&result.measured)) {
		FinalizeFailure(&result.measured);
		return result;
	}
	if (request.devicePixelRatio <= 0) {
		result.measured.error = u"invalid-device-pixel-ratio"_q;
		return result;
	}
	auto parsed = ParseFormula(prepared);
	result.measured = std::move(parsed.measured);
	if (!result.measured.success) {
		return result;
	}
	auto physicalSize = QSize();
	if (!ComputePhysicalSize(
			result.measured.logicalSize,
			request.devicePixelRatio,
			&physicalSize)) {
		result.measured.success = false;
		result.measured.error = u"physical-image-cap-exceeded"_q;
		result.measured.overflow = true;
		result.measured.tooLarge = true;
		return result;
	}
	auto image = QImage(
		physicalSize,
		QImage::Format_ARGB32_Premultiplied);
	if (image.isNull()) {
		result.measured.success = false;
		result.measured.error = u"image-allocation-failed"_q;
		return result;
	}
	image.setDevicePixelRatio(request.devicePixelRatio);
	image.fill(Qt::transparent);
	{
		QPainter painter(&image);
		painter.setRenderHint(QPainter::Antialiasing, true);
		painter.setRenderHint(QPainter::TextAntialiasing, true);
		painter.scale(
			1. / double(kFormulaExactMetricScale),
			1. / double(kFormulaExactMetricScale));
		tex::Graphics2D_qt graphics(&painter);
		parsed.render->draw(graphics, 0, 0);
	}
	result.image = std::move(image);
	return result;
}

bool MicrotexBackendLinked() {
	return true;
}

} // namespace Iv::Markdown
