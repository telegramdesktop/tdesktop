/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/chat/chat_theme.h"

#include "ui/image/image_prepare.h"
#include "ui/ui_utility.h"
#include "ui/chat/message_bubble.h"

#include <crl/crl_async.h>
#include <QtGui/QGuiApplication>

namespace Ui {
namespace {

constexpr auto kMaxChatEntryHistorySize = 50;
constexpr auto kCacheBackgroundTimeout = 1 * crl::time(1000);
constexpr auto kCacheBackgroundFastTimeout = crl::time(200);
constexpr auto kBackgroundFadeDuration = crl::time(200);
constexpr auto kMinimumTiledSize = 512;
constexpr auto kMaxSize = 2960;

[[nodiscard]] QColor DefaultBackgroundColor() {
	return QColor(213, 223, 233);
}

[[nodiscard]] CacheBackgroundResult CacheBackground(
		const CacheBackgroundRequest &request) {
	Expects(!request.area.isEmpty());

	const auto gradient = request.background.gradientForFill.isNull()
		? QImage()
		: (request.gradientRotationAdd != 0)
		? Images::GenerateGradient(
			request.background.gradientForFill.size(),
			request.background.colors,
			(request.background.gradientRotation
				+ request.gradientRotationAdd) % 360)
		: request.background.gradientForFill;
	if (request.background.isPattern
		|| request.background.tile
		|| request.background.prepared.isNull()) {
		auto result = gradient.isNull()
			? QImage(
				request.area * style::DevicePixelRatio(),
				QImage::Format_ARGB32_Premultiplied)
			: gradient.scaled(
				request.area * style::DevicePixelRatio(),
				Qt::IgnoreAspectRatio,
				Qt::SmoothTransformation);
		result.setDevicePixelRatio(style::DevicePixelRatio());
		if (!request.background.prepared.isNull()) {
			QPainter p(&result);
			if (!gradient.isNull()) {
				if (request.background.patternOpacity >= 0.) {
					p.setCompositionMode(QPainter::CompositionMode_SoftLight);
					p.setOpacity(request.background.patternOpacity);
				} else {
					p.setCompositionMode(
						QPainter::CompositionMode_DestinationIn);
				}
			}
			const auto tiled = request.background.isPattern
				? request.background.prepared.scaled(
					request.area.height() * style::DevicePixelRatio(),
					request.area.height() * style::DevicePixelRatio(),
					Qt::KeepAspectRatio,
					Qt::SmoothTransformation)
				: request.background.preparedForTiled;
			const auto w = tiled.width() / float(style::DevicePixelRatio());
			const auto h = tiled.height() / float(style::DevicePixelRatio());
			const auto cx = int(std::ceil(request.area.width() / w));
			const auto cy = int(std::ceil(request.area.height() / h));
			const auto rows = cy;
			const auto cols = request.background.isPattern
				? (((cx / 2) * 2) + 1)
				: cx;
			const auto xshift = request.background.isPattern
				? (request.area.width() - cols * w) / 2
				: 0;
			for (auto y = 0; y != rows; ++y) {
				for (auto x = 0; x != cols; ++x) {
					p.drawImage(QPointF(xshift + x * w, y * h), tiled);
				}
			}
			if (!gradient.isNull()
				&& request.background.patternOpacity < 0.
				&& request.background.patternOpacity > -1.) {
				p.setCompositionMode(QPainter::CompositionMode_SourceOver);
				p.setOpacity(1. + request.background.patternOpacity);
				p.fillRect(QRect(QPoint(), request.area), Qt::black);
			}
		}
		return {
			.image = std::move(result).convertToFormat(
				QImage::Format_ARGB32_Premultiplied),
			.gradient = gradient,
			.area = request.area,
		};
	} else {
		const auto rects = ComputeChatBackgroundRects(
			request.area,
			request.background.prepared.size());
		auto result = request.background.prepared.copy(rects.from).scaled(
			rects.to.width() * style::DevicePixelRatio(),
			rects.to.height() * style::DevicePixelRatio(),
			Qt::IgnoreAspectRatio,
			Qt::SmoothTransformation);
		result.setDevicePixelRatio(style::DevicePixelRatio());
		return {
			.image = std::move(result).convertToFormat(
				QImage::Format_ARGB32_Premultiplied),
			.gradient = gradient,
			.area = request.area,
			.x = rects.to.x(),
			.y = rects.to.y(),
		};
	}
}

} // namespace

bool operator==(const ChatThemeBackground &a, const ChatThemeBackground &b) {
	return (a.prepared.cacheKey() == b.prepared.cacheKey())
		&& (a.gradientForFill.cacheKey() == b.gradientForFill.cacheKey())
		&& (a.tile == b.tile)
		&& (a.patternOpacity == b.patternOpacity);
}

bool operator!=(const ChatThemeBackground &a, const ChatThemeBackground &b) {
	return !(a == b);
}

bool operator==(
		const CacheBackgroundRequest &a,
		const CacheBackgroundRequest &b) {
	return (a.background == b.background)
		&& (a.area == b.area)
		&& (a.gradientRotationAdd == b.gradientRotationAdd)
		&& (a.gradientProgress == b.gradientProgress);
}

bool operator!=(
		const CacheBackgroundRequest &a,
		const CacheBackgroundRequest &b) {
	return !(a == b);
}

CachedBackground::CachedBackground(CacheBackgroundResult &&result)
: pixmap(PixmapFromImage(std::move(result.image)))
, area(result.area)
, x(result.x)
, y(result.y) {
}

ChatTheme::ChatTheme() {
}

// Runs from background thread.
ChatTheme::ChatTheme(ChatThemeDescriptor &&descriptor)
: _id(descriptor.id)
, _palette(std::make_unique<style::palette>()) {
	descriptor.preparePalette(*_palette);
	setBackground(descriptor.prepareBackground());
}

void ChatTheme::setBackground(ChatThemeBackground &&background) {
	_mutableBackground = std::move(background);
	_backgroundState = {};
	_backgroundNext = {};
	_backgroundFade.stop();
	if (_cacheBackgroundTimer) {
		_cacheBackgroundTimer->cancel();
	}
	_repaintBackgroundRequests.fire({});
}

void ChatTheme::updateBackgroundImageFrom(ChatThemeBackground &&background) {
	_mutableBackground.prepared = std::move(background.prepared);
	_mutableBackground.preparedForTiled = std::move(
		background.preparedForTiled);
	if (!_backgroundState.now.pixmap.isNull()) {
		if (_cacheBackgroundTimer) {
			_cacheBackgroundTimer->cancel();
		}
		cacheBackgroundNow();
	}
}

uint64 ChatTheme::key() const {
	return _id;
}

void ChatTheme::setBubblesBackground(QImage image) {
	_bubblesBackgroundPrepared = std::move(image);
	if (!_bubblesBackground.area.isEmpty()) {
		_bubblesBackground = CacheBackground({
			.background = {
				.prepared = _bubblesBackgroundPrepared,
			},
			.area = _bubblesBackground.area,
		});
	}
	if (!_bubblesBackgroundPattern) {
		_bubblesBackgroundPattern = PrepareBubblePattern(palette());
	}
	_bubblesBackgroundPattern->pixmap = _bubblesBackground.pixmap;
	_repaintBackgroundRequests.fire({});
}

ChatPaintContext ChatTheme::preparePaintContext(
		not_null<const ChatStyle*> st,
		QRect viewport,
		QRect clip) {
	_bubblesBackground.area = viewport.size();
	//if (!_bubblesBackgroundPrepared.isNull()
	//	&& _bubblesBackground.area != viewport.size()
	//	&& !viewport.isEmpty()) {
	//	// #TODO bubbles delayed caching
	//	_bubblesBackground = CacheBackground({
	//		.prepared = _bubblesBackgroundPrepared,
	//		.area = viewport.size(),
	//	});
	//	_bubblesBackgroundPattern->pixmap = _bubblesBackground.pixmap;
	//}
	return {
		.st = st,
		.bubblesPattern = _bubblesBackgroundPattern.get(),
		.viewport = viewport,
		.clip = clip,
		.now = crl::now(),
	};
}

const BackgroundState &ChatTheme::backgroundState(QSize area) {
	if (!_cacheBackgroundTimer) {
		_cacheBackgroundTimer.emplace([=] { cacheBackground(); });
	}
	_backgroundState.shown = _backgroundFade.value(1.);
	if (_backgroundState.now.pixmap.isNull()
		&& !background().gradientForFill.isNull()) {
		// We don't support direct painting of patterned gradients.
		// So we need to sync-generate cache image here.
		_willCacheForArea = area;
		setCachedBackground(CacheBackground(currentCacheRequest(area)));
		_cacheBackgroundTimer->cancel();
	} else if (_backgroundState.now.area != area) {
		if (_willCacheForArea != area
			|| (!_cacheBackgroundTimer->isActive()
				&& !_backgroundCachingRequest)) {
			_willCacheForArea = area;
			_lastAreaChangeTime = crl::now();
			_cacheBackgroundTimer->callOnce(kCacheBackgroundFastTimeout);
		}
	}
	generateNextBackgroundRotation();
	return _backgroundState;
}

bool ChatTheme::readyForBackgroundRotation() const {
	Expects(_cacheBackgroundTimer.has_value());

	return !anim::Disabled()
		&& !_backgroundFade.animating()
		&& !_cacheBackgroundTimer->isActive()
		&& !_backgroundState.now.pixmap.isNull();
}

void ChatTheme::generateNextBackgroundRotation() {
	if (_backgroundCachingRequest
		|| !_backgroundNext.image.isNull()
		|| !readyForBackgroundRotation()) {
		return;
	}
	if (background().colors.size() < 3) {
		return;
	}
	constexpr auto kAddRotation = 315;
	const auto request = currentCacheRequest(
		_backgroundState.now.area,
		kAddRotation);
	if (!request) {
		return;
	}
	cacheBackgroundAsync(request, [=](CacheBackgroundResult &&result) {
		const auto forRequest = base::take(_backgroundCachingRequest);
		if (!readyForBackgroundRotation()) {
			return;
		}
		const auto request = currentCacheRequest(
			_backgroundState.now.area,
			kAddRotation);
		if (forRequest == request) {
			_mutableBackground.gradientRotation
				= (_mutableBackground.gradientRotation + kAddRotation) % 360;
			_backgroundNext = std::move(result);
		}
	});
}

auto ChatTheme::currentCacheRequest(QSize area, int addRotation) const
-> CacheBackgroundRequest {
	if (background().colorForFill) {
		return {};
	}
	return {
		.background = background(),
		.area = area,
		.gradientRotationAdd = addRotation,
//		.recreateGradient = (addRotation != 0),
	};
}

void ChatTheme::cacheBackground() {
	Expects(_cacheBackgroundTimer.has_value());

	const auto now = crl::now();
	if (now - _lastAreaChangeTime < kCacheBackgroundTimeout
		&& QGuiApplication::mouseButtons() != 0) {
		_cacheBackgroundTimer->callOnce(kCacheBackgroundFastTimeout);
		return;
	}
	cacheBackgroundNow();
}

void ChatTheme::cacheBackgroundNow() {
	if (!_backgroundCachingRequest) {
		if (const auto request = currentCacheRequest(_willCacheForArea)) {
			cacheBackgroundAsync(request);
		}
	}
}

void ChatTheme::cacheBackgroundAsync(
		const CacheBackgroundRequest &request,
		Fn<void(CacheBackgroundResult&&)> done) {
	_backgroundCachingRequest = request;
	const auto weak = base::make_weak(this);
	crl::async([=] {
		if (!weak) {
			return;
		}
		crl::on_main(weak, [=, result = CacheBackground(request)]() mutable {
			if (done) {
				done(std::move(result));
			} else if (const auto request = currentCacheRequest(
					_willCacheForArea)) {
				if (_backgroundCachingRequest != request) {
					cacheBackgroundAsync(request);
				} else {
					_backgroundCachingRequest = {};
					setCachedBackground(std::move(result));
				}
			}
		});
	});
}

void ChatTheme::setCachedBackground(CacheBackgroundResult &&cached) {
	_backgroundNext = {};

	if (background().gradientForFill.isNull()
		|| _backgroundState.now.pixmap.isNull()
		|| anim::Disabled()) {
		_backgroundFade.stop();
		_backgroundState.shown = 1.;
		_backgroundState.now = std::move(cached);
		return;
	}
	// #TODO themes compose several transitions.
	_backgroundState.was = std::move(_backgroundState.now);
	_backgroundState.now = std::move(cached);
	_backgroundState.shown = 0.;
	const auto callback = [=] {
		if (!_backgroundFade.animating()) {
			_backgroundState.was = {};
			_backgroundState.shown = 1.;
		}
		_repaintBackgroundRequests.fire({});
	};
	_backgroundFade.start(
		callback,
		0.,
		1.,
		kBackgroundFadeDuration);
}

rpl::producer<> ChatTheme::repaintBackgroundRequests() const {
	return _repaintBackgroundRequests.events();
}

void ChatTheme::rotateComplexGradientBackground() {
	if (!_backgroundFade.animating() && !_backgroundNext.image.isNull()) {
		if (_mutableBackground.gradientForFill.size()
			== _backgroundNext.gradient.size()) {
			_mutableBackground.gradientForFill
				= std::move(_backgroundNext.gradient);
		}
		setCachedBackground(base::take(_backgroundNext));
	}
}

ChatBackgroundRects ComputeChatBackgroundRects(
		QSize fillSize,
		QSize imageSize) {
	if (uint64(imageSize.width()) * fillSize.height()
		> uint64(imageSize.height()) * fillSize.width()) {
		const auto pxsize = fillSize.height() / float64(imageSize.height());
		auto takewidth = int(std::ceil(fillSize.width() / pxsize));
		if (takewidth > imageSize.width()) {
			takewidth = imageSize.width();
		} else if ((imageSize.width() % 2) != (takewidth % 2)) {
			++takewidth;
		}
		return {
			.from = QRect(
				(imageSize.width() - takewidth) / 2,
				0,
				takewidth,
				imageSize.height()),
			.to = QRect(
				int((fillSize.width() - takewidth * pxsize) / 2.),
				0,
				int(std::ceil(takewidth * pxsize)),
				fillSize.height()),
		};
	} else {
		const auto pxsize = fillSize.width() / float64(imageSize.width());
		auto takeheight = int(std::ceil(fillSize.height() / pxsize));
		if (takeheight > imageSize.height()) {
			takeheight = imageSize.height();
		} else if ((imageSize.height() % 2) != (takeheight % 2)) {
			++takeheight;
		}
		return {
			.from = QRect(
				0,
				(imageSize.height() - takeheight) / 2,
				imageSize.width(),
				takeheight),
			.to = QRect(
				0,
				int((fillSize.height() - takeheight * pxsize) / 2.),
				fillSize.width(),
				int(std::ceil(takeheight * pxsize))),
		};
	}
}

QColor CountAverageColor(const QImage &image) {
	Expects(image.format() == QImage::Format_ARGB32_Premultiplied
		|| image.format() == QImage::Format_RGB32);

	uint64 components[3] = { 0 };
	const auto w = image.width();
	const auto h = image.height();
	const auto size = w * h;
	if (const auto pix = image.constBits()) {
		for (auto i = 0, l = size * 4; i != l; i += 4) {
			components[2] += pix[i + 0];
			components[1] += pix[i + 1];
			components[0] += pix[i + 2];
		}
	}
	if (size) {
		for (auto i = 0; i != 3; ++i) {
			components[i] /= size;
		}
	}
	return QColor(components[0], components[1], components[2]);
}

QColor ThemeAdjustedColor(QColor original, QColor background) {
	return QColor::fromHslF(
		background.hslHueF(),
		background.hslSaturationF(),
		original.lightnessF(),
		original.alphaF()
	).toRgb();
}

QImage PreprocessBackgroundImage(QImage image) {
	if (image.isNull()) {
		return image;
	}
	if (image.format() != QImage::Format_ARGB32_Premultiplied) {
		image = std::move(image).convertToFormat(
			QImage::Format_ARGB32_Premultiplied);
	}
	if (image.width() > 40 * image.height()) {
		const auto width = 40 * image.height();
		const auto height = image.height();
		image = image.copy((image.width() - width) / 2, 0, width, height);
	} else if (image.height() > 40 * image.width()) {
		const auto width = image.width();
		const auto height = 40 * image.width();
		image = image.copy(0, (image.height() - height) / 2, width, height);
	}
	if (image.width() > kMaxSize || image.height() > kMaxSize) {
		image = image.scaled(
			kMaxSize,
			kMaxSize,
			Qt::KeepAspectRatio,
			Qt::SmoothTransformation);
	}
	return image;
}

std::optional<QColor> CalculateImageMonoColor(const QImage &image) {
	Expects(image.bytesPerLine() == 4 * image.width());

	if (image.isNull()) {
		return std::nullopt;
	}
	const auto bits = reinterpret_cast<const uint32*>(image.constBits());
	const auto first = bits[0];
	for (auto i = 0; i < image.width() * image.height(); i++) {
		if (first != bits[i]) {
			return std::nullopt;
		}
	}
	return image.pixelColor(QPoint());
}

QImage PrepareImageForTiled(const QImage &prepared) {
	const auto width = prepared.width();
	const auto height = prepared.height();
	const auto isSmallForTiled = (width > 0 && height > 0)
		&& (width < kMinimumTiledSize || height < kMinimumTiledSize);
	if (!isSmallForTiled) {
		return prepared;
	}
	const auto repeatTimesX = (kMinimumTiledSize + width - 1) / width;
	const auto repeatTimesY = (kMinimumTiledSize + height - 1) / height;
	auto result = QImage(
		width * repeatTimesX,
		height * repeatTimesY,
		QImage::Format_ARGB32_Premultiplied);
	result.setDevicePixelRatio(prepared.devicePixelRatio());
	auto imageForTiledBytes = result.bits();
	auto bytesInLine = width * sizeof(uint32);
	for (auto timesY = 0; timesY != repeatTimesY; ++timesY) {
		auto imageBytes = prepared.constBits();
		for (auto y = 0; y != height; ++y) {
			for (auto timesX = 0; timesX != repeatTimesX; ++timesX) {
				memcpy(imageForTiledBytes, imageBytes, bytesInLine);
				imageForTiledBytes += bytesInLine;
			}
			imageBytes += prepared.bytesPerLine();
			imageForTiledBytes += result.bytesPerLine() - (repeatTimesX * bytesInLine);
		}
	}
	return result;
}

[[nodiscard]] QImage ReadBackgroundImage(
		const QString &path,
		const QByteArray &content,
		bool gzipSvg) {
	return Images::Read({
		.path = path,
		.content = content,
		.maxSize = QSize(kMaxSize, kMaxSize),
		.gzipSvg = gzipSvg,
	}).image;
}

QImage GenerateBackgroundImage(
		QSize size,
		const std::vector<QColor> &bg,
		int gradientRotation,
		float64 patternOpacity,
		Fn<void(QPainter&)> drawPattern) {
	auto result = bg.empty()
		? Images::GenerateGradient(size, { DefaultBackgroundColor() })
		: Images::GenerateGradient(size, bg, gradientRotation);
	if (bg.size() > 1 && (!drawPattern || patternOpacity >= 0.)) {
		result = Images::DitherImage(std::move(result));
	}
	if (drawPattern) {
		auto p = QPainter(&result);
		if (patternOpacity >= 0.) {
			p.setCompositionMode(QPainter::CompositionMode_SoftLight);
			p.setOpacity(patternOpacity);
		} else {
			p.setCompositionMode(QPainter::CompositionMode_DestinationIn);
		}
		drawPattern(p);
		if (patternOpacity < 0. && patternOpacity > -1.) {
			p.setCompositionMode(QPainter::CompositionMode_SourceOver);
			p.setOpacity(1. + patternOpacity);
			p.fillRect(QRect{ QPoint(), size }, Qt::black);
		}
	}

	return std::move(result).convertToFormat(
		QImage::Format_ARGB32_Premultiplied);
}

QImage PreparePatternImage(
		QImage pattern,
		const std::vector<QColor> &bg,
		int gradientRotation,
		float64 patternOpacity) {
	auto result = GenerateBackgroundImage(
		pattern.size(),
		bg,
		gradientRotation,
		patternOpacity,
		[&](QPainter &p) {
			p.drawImage(QRect(QPoint(), pattern.size()), pattern);
		});

	pattern = QImage();
	return result;
}

QImage PrepareBlurredBackground(QImage image) {
	constexpr auto kSize = 900;
	constexpr auto kRadius = 24;
	if (image.width() > kSize || image.height() > kSize) {
		image = image.scaled(
			kSize,
			kSize,
			Qt::KeepAspectRatio,
			Qt::SmoothTransformation);
	}
	return Images::BlurLargeImage(image, kRadius);
}

QImage GenerateDitheredGradient(
		const std::vector<QColor> &colors,
		int rotation) {
	constexpr auto kSize = 512;
	const auto size = QSize(kSize, kSize);
	if (colors.empty()) {
		return Images::GenerateGradient(size, { DefaultBackgroundColor() });
	}
	auto result = Images::GenerateGradient(size, colors, rotation);
	if (colors.size() > 1) {
		result = Images::DitherImage(std::move(result));
	}
	return result;
}

ChatThemeBackground PrepareBackgroundImage(
		const QString &path,
		const QByteArray &bytes,
		bool gzipSvg,
		const std::vector<QColor> &colors,
		bool isPattern,
		float64 patternOpacity,
		bool isBlurred) {
	auto prepared = (isPattern || colors.empty())
		? PreprocessBackgroundImage(ReadBackgroundImage(path, bytes, gzipSvg))
		: QImage();
	if (isPattern && !prepared.isNull()) {
		if (colors.size() < 2) {
			const auto gradientRotation = 0; // No gradient here.
			prepared = PreparePatternImage(
				std::move(prepared),
				colors,
				gradientRotation,
				patternOpacity);
		}
		prepared.setDevicePixelRatio(style::DevicePixelRatio());
	} else if (colors.empty()) {
		prepared.setDevicePixelRatio(style::DevicePixelRatio());
	}
	const auto imageMonoColor = (colors.size() < 2)
		? CalculateImageMonoColor(prepared)
		: std::nullopt;
	if (!prepared.isNull() && !isPattern && isBlurred) {
		prepared = PrepareBlurredBackground(std::move(prepared));
	}
	return ChatThemeBackground{
		.prepared = prepared,
		.preparedForTiled = PrepareImageForTiled(prepared),
		.colorForFill = (!prepared.isNull()
			? imageMonoColor
			: (colors.size() > 1 || colors.empty())
			? std::nullopt
			: std::make_optional(colors.front())),
		.colors = colors,
		.patternOpacity = patternOpacity,
		.isPattern = isPattern,
	};
}

} // namespace Window::Theme
