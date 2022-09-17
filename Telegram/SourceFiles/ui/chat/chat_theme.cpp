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
#include "ui/chat/chat_style.h"
#include "ui/color_contrast.h"
#include "ui/style/style_core_palette.h"
#include "ui/style/style_palette_colorizer.h"

#include <crl/crl_async.h>
#include <QtGui/QGuiApplication>

namespace Ui {
namespace {

constexpr auto kCacheBackgroundTimeout = 1 * crl::time(1000);
constexpr auto kCacheBackgroundFastTimeout = crl::time(200);
constexpr auto kBackgroundFadeDuration = crl::time(200);
constexpr auto kMinimumTiledSize = 512;
constexpr auto kMaxSize = 2960;
constexpr auto kMaxContrastValue = 21.;
constexpr auto kMinAcceptableContrast = 1.14;// 4.5;

[[nodiscard]] QColor DefaultBackgroundColor() {
	return QColor(213, 223, 233);
}

[[nodiscard]] int ComputeRealRotation(const CacheBackgroundRequest &request) {
	if (request.background.colors.size() < 3) {
		return request.background.gradientRotation;
	}
	const auto doubled = (request.background.gradientRotation
		+ request.gradientRotationAdd) % 720;
	return (((doubled % 2) ? (doubled - 45) : doubled) / 2) % 360;
}

[[nodiscard]] double ComputeRealProgress(
		const CacheBackgroundRequest &request) {
	if (request.background.colors.size() < 3) {
		return 1.;
	}
	const auto doubled = (request.background.gradientRotation
		+ request.gradientRotationAdd) % 720;
	return (doubled % 2) ? 0.5 : 1.;
}

[[nodiscard]] CacheBackgroundResult CacheBackgroundByRequest(
		const CacheBackgroundRequest &request) {
	Expects(!request.area.isEmpty());

	const auto ratio = style::DevicePixelRatio();
	const auto gradient = request.background.gradientForFill.isNull()
		? QImage()
		: (request.gradientRotationAdd != 0)
		? Images::GenerateGradient(
			request.background.gradientForFill.size(),
			request.background.colors,
			ComputeRealRotation(request),
			ComputeRealProgress(request))
		: request.background.gradientForFill;
	if (request.background.isPattern
		|| request.background.tile
		|| request.background.prepared.isNull()) {
		auto result = gradient.isNull()
			? QImage(
				request.area * ratio,
				QImage::Format_ARGB32_Premultiplied)
			: gradient.scaled(
				request.area * ratio,
				Qt::IgnoreAspectRatio,
				Qt::SmoothTransformation);
		result.setDevicePixelRatio(ratio);
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
					request.area.height() * ratio,
					request.area.height() * ratio,
					Qt::KeepAspectRatio,
					Qt::SmoothTransformation)
				: request.background.preparedForTiled;
			const auto w = tiled.width() / float(ratio);
			const auto h = tiled.height() / float(ratio);
			const auto cx = int(std::ceil(request.area.width() / w));
			const auto cy = int(std::ceil(request.area.height() / h));
			const auto rows = cy;
			const auto cols = request.background.isPattern
				? (((cx / 2) * 2) + 1)
				: cx;
			const auto xshift = request.background.isPattern
				? (request.area.width() * ratio - cols * tiled.width()) / 2
				: 0;
			const auto useshift = xshift / float(ratio);
			for (auto y = 0; y != rows; ++y) {
				for (auto x = 0; x != cols; ++x) {
					p.drawImage(QPointF(useshift + x * w, y * h), tiled);
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
			.waitingForNegativePattern
				= request.background.waitingForNegativePattern()
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

[[nodiscard]] QImage PrepareBubblesBackground(
		const ChatThemeBubblesData &data) {
	if (data.colors.size() < 2) {
		return QImage();
	}
	constexpr auto kSize = 512;
	return Images::GenerateLinearGradient(QSize(kSize, kSize), data.colors);
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

CacheBackgroundResult CacheBackground(
		const CacheBackgroundRequest &request) {
	return CacheBackgroundByRequest(request);
}

CachedBackground::CachedBackground(CacheBackgroundResult &&result)
: pixmap(PixmapFromImage(std::move(result.image)))
, area(result.area)
, x(result.x)
, y(result.y)
, waitingForNegativePattern(result.waitingForNegativePattern) {
}

ChatTheme::ChatTheme() {
}

// Runs from background thread.
ChatTheme::ChatTheme(ChatThemeDescriptor &&descriptor)
: _key(descriptor.key)
, _palette(std::make_unique<style::palette>()) {
	descriptor.preparePalette(*_palette);
	setBackground(PrepareBackgroundImage(descriptor.backgroundData));
	setBubblesBackground(PrepareBubblesBackground(descriptor.bubblesData));
	adjustPalette(descriptor);
}

ChatTheme::~ChatTheme() = default;

void ChatTheme::adjustPalette(const ChatThemeDescriptor &descriptor) {
	auto &p = *_palette;
	const auto overrideOutBg = (descriptor.bubblesData.colors.size() == 1);
	if (overrideOutBg) {
		set(p.msgOutBg(), descriptor.bubblesData.colors.front());
	}
	const auto &background = descriptor.backgroundData.colors;
	if (!background.empty()) {
		const auto average = CountAverageColor(background);
		adjust(p.msgServiceBg(), average);
		adjust(p.msgServiceBgSelected(), average);
		adjust(p.historyScrollBg(), average);
		adjust(p.historyScrollBgOver(), average);
		adjust(p.historyScrollBarBg(), average);
		adjust(p.historyScrollBarBgOver(), average);
	}
	const auto bubblesAccent = descriptor.bubblesData.accent
		? descriptor.bubblesData.accent
		: (!descriptor.bubblesData.colors.empty())
		? ThemeAdjustedColor(
			p.msgOutReplyBarColor()->c,
			CountAverageColor(descriptor.bubblesData.colors))
		: std::optional<QColor>();
	if (bubblesAccent) {
		// First set hue/saturation the same for all those colors from accent.
		const auto by = *bubblesAccent;
		if (!overrideOutBg) {
			adjust(p.msgOutBg(), by);
		}
		adjust(p.msgOutShadow(), by);
		adjust(p.msgOutServiceFg(), by);
		adjust(p.msgOutDateFg(), by);
		adjust(p.msgFileThumbLinkOutFg(), by);
		adjust(p.msgFileOutBg(), by);
		adjust(p.msgOutReplyBarColor(), by);
		adjust(p.msgWaveformOutActive(), by);
		adjust(p.msgWaveformOutInactive(), by);
		adjust(p.historyFileOutRadialFg(), by); // historyFileOutIconFg
		adjust(p.mediaOutFg(), by);

		adjust(p.historyLinkOutFg(), by);
		adjust(p.msgOutMonoFg(), by);
		adjust(p.historyOutIconFg(), by);
		adjust(p.historySendingOutIconFg(), by);
		adjust(p.historyCallArrowOutFg(), by);
		adjust(p.historyFileOutIconFg(), by); // msgOutBg

		// After make msgFileOutBg exact accent and adjust some others.
		const auto colorizer = bubblesAccentColorizer(by);
		adjust(p.msgOutServiceFg(), colorizer);
		adjust(p.msgOutDateFg(), colorizer);
		adjust(p.msgFileThumbLinkOutFg(), colorizer);
		adjust(p.msgFileOutBg(), colorizer);
		adjust(p.msgOutReplyBarColor(), colorizer);
		adjust(p.msgWaveformOutActive(), colorizer);
		adjust(p.msgWaveformOutInactive(), colorizer);
		adjust(p.mediaOutFg(), colorizer);
		adjust(p.historyLinkOutFg(), colorizer);
		adjust(p.historyOutIconFg(), colorizer);
		adjust(p.historySendingOutIconFg(), colorizer);
		adjust(p.historyCallArrowOutFg(), colorizer);

		if (!descriptor.basedOnDark) {
			adjust(p.msgOutBgSelected(), by);
			adjust(p.msgOutShadowSelected(), by);
			adjust(p.msgOutServiceFgSelected(), by);
			adjust(p.msgOutDateFgSelected(), by);
			adjust(p.msgFileThumbLinkOutFgSelected(), by);
			adjust(p.msgFileOutBgSelected(), by);
			adjust(p.msgOutReplyBarSelColor(), by);
			adjust(p.msgWaveformOutActiveSelected(), by);
			adjust(p.msgWaveformOutInactiveSelected(), by);
			adjust(p.historyFileOutRadialFgSelected(), by);
			adjust(p.mediaOutFgSelected(), by);

			adjust(p.historyLinkOutFgSelected(), by);
			adjust(p.msgOutMonoFgSelected(), by);
			adjust(p.historyOutIconFgSelected(), by);
			// adjust(p.historySendingOutIconFgSelected(), by);
			adjust(p.historyCallArrowOutFgSelected(), by);
			adjust(p.historyFileOutIconFgSelected(), by); // msgOutBg

			adjust(p.msgOutServiceFgSelected(), colorizer);
			adjust(p.msgOutDateFgSelected(), colorizer);
			adjust(p.msgFileThumbLinkOutFgSelected(), colorizer);
			adjust(p.msgFileOutBgSelected(), colorizer);
			adjust(p.msgOutReplyBarSelColor(), colorizer);
			adjust(p.msgWaveformOutActiveSelected(), colorizer);
			adjust(p.msgWaveformOutInactiveSelected(), colorizer);
			adjust(p.mediaOutFgSelected(), colorizer);
			adjust(p.historyLinkOutFgSelected(), colorizer);
			adjust(p.historyOutIconFgSelected(), colorizer);
			//adjust(p.historySendingOutIconFgSelected(), colorizer);
			adjust(p.historyCallArrowOutFgSelected(), colorizer);
		}
	}
	auto outBgColors = descriptor.bubblesData.colors;
	if (outBgColors.empty()) {
		outBgColors.push_back(p.msgOutBg()->c);
	}
	const auto colors = {
		p.msgOutServiceFg(),
		p.msgOutDateFg(),
		p.msgFileThumbLinkOutFg(),
		p.msgFileOutBg(),
		p.msgOutReplyBarColor(),
		p.msgWaveformOutActive(),
		p.historyTextOutFg(),
		p.mediaOutFg(),
		p.historyLinkOutFg(),
		p.msgOutMonoFg(),
		p.historyOutIconFg(),
		p.historyCallArrowOutFg(),
	};
	const auto minimal = [&](const QColor &with) {
		auto result = kMaxContrastValue;
		for (const auto &color : colors) {
			result = std::min(result, Ui::CountContrast(color->c, with));
		}
		return result;
	};
	const auto withBg = [&](auto &&count) {
		auto result = kMaxContrastValue;
		for (const auto &bg : outBgColors) {
			result = std::min(result, count(bg));
		}
		return result;
	};
	//const auto singleWithBg = [&](const QColor &c) {
	//	return withBg([&](const QColor &with) {
	//		return Ui::CountContrast(c, with);
	//	});
	//};
	if (withBg(minimal) < kMinAcceptableContrast) {
		const auto white = QColor(255, 255, 255);
		const auto black = QColor(0, 0, 0);
		// This one always gives black :)
		//const auto now = (singleWithBg(white) >= singleWithBg(black))
		//	? white
		//	: black;
		const auto now = descriptor.basedOnDark ? white : black;
		for (const auto &color : colors) {
			set(color, now);
		}
	}
}

style::colorizer ChatTheme::bubblesAccentColorizer(
		const QColor &accent) const {
	const auto color = [](const QColor &value) {
		auto hue = 0;
		auto saturation = 0;
		auto lightness = 0;
		value.getHsv(&hue, &saturation, &lightness);
		return style::colorizer::Color{ hue, saturation, lightness };
	};
	return {
		.hueThreshold = 255,
		.was = color(_palette->msgFileOutBg()->c),
		.now = color(accent),
	};
}

void ChatTheme::set(const style::color &my, const QColor &color) {
	auto r = 0, g = 0, b = 0, a = 0;
	color.getRgb(&r, &g, &b, &a);
	my.set(uchar(r), uchar(g), uchar(b), uchar(a));
}

void ChatTheme::adjust(const style::color &my, const QColor &by) {
	set(my, ThemeAdjustedColor(my->c, by));
}

void ChatTheme::adjust(const style::color &my, const style::colorizer &by) {
	if (const auto adjusted = style::colorize(my->c, by)) {
		set(my, *adjusted);
	}
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
	} else {
		_repaintBackgroundRequests.fire({});
	}
}

ChatThemeKey ChatTheme::key() const {
	return _key;
}

void ChatTheme::setBubblesBackground(QImage image) {
	if (image.isNull() && _bubblesBackgroundPrepared.isNull()) {
		return;
	}
	_bubblesBackgroundPrepared = std::move(image);
	if (_bubblesBackgroundPrepared.isNull()) {
		_bubblesBackgroundPattern = nullptr;
		// setBubblesBackground called only from background thread.
		//_repaintBackgroundRequests.fire({});
		return;
	}
	_bubblesBackground = CacheBackground({
		.background = {
			.prepared = _bubblesBackgroundPrepared,
		},
		.area = (_bubblesBackground.area.isEmpty()
			? _bubblesBackgroundPrepared.size()
			: _bubblesBackground.area),
	});
	if (!_bubblesBackgroundPattern) {
		_bubblesBackgroundPattern = PrepareBubblePattern(palette());
	}
	_bubblesBackgroundPattern->pixmap = _bubblesBackground.pixmap;
	// setBubblesBackground called only from background thread.
	//_repaintBackgroundRequests.fire({});
}

void ChatTheme::finishCreateOnMain() {
	if (_bubblesBackgroundPattern) {
		FinishBubblePatternOnMain(_bubblesBackgroundPattern.get());
	}
}

ChatPaintContext ChatTheme::preparePaintContext(
		not_null<const ChatStyle*> st,
		QRect viewport,
		QRect clip,
		bool paused) {
	const auto area = viewport.size();
	const auto now = crl::now();
	if (!_bubblesBackgroundPrepared.isNull()
		&& _bubblesBackground.area != area) {
		if (!_cacheBubblesTimer) {
			_cacheBubblesTimer.emplace([=] { cacheBubbles(); });
		}
		if (_cacheBubblesArea != area
			|| (!_cacheBubblesTimer->isActive()
				&& !_bubblesCachingRequest)) {
			_cacheBubblesArea = area;
			_lastBubblesAreaChangeTime = now;
			_cacheBubblesTimer->callOnce(kCacheBackgroundFastTimeout);
		}
	}
	return {
		.st = st,
		.bubblesPattern = _bubblesBackgroundPattern.get(),
		.viewport = viewport,
		.clip = clip,
		.paused = paused,
		.now = now,
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
		_cacheBackgroundArea = area;
		setCachedBackground(CacheBackground(cacheBackgroundRequest(area)));
		_cacheBackgroundTimer->cancel();
	} else if (_backgroundState.now.area != area) {
		if (_cacheBackgroundArea != area
			|| (!_cacheBackgroundTimer->isActive()
				&& !_backgroundCachingRequest)) {
			_cacheBackgroundArea = area;
			_lastBackgroundAreaChangeTime = crl::now();
			_cacheBackgroundTimer->callOnce(kCacheBackgroundFastTimeout);
		}
	}
	generateNextBackgroundRotation();
	return _backgroundState;
}

void ChatTheme::clearBackgroundState() {
	_backgroundState = BackgroundState();
	_backgroundFade.stop();
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
	constexpr auto kAddRotationDoubled = (720 - 45);
	const auto request = cacheBackgroundRequest(
		_backgroundState.now.area,
		kAddRotationDoubled);
	if (!request) {
		return;
	}
	cacheBackgroundAsync(request, [=](CacheBackgroundResult &&result) {
		const auto forRequest = base::take(_backgroundCachingRequest);
		if (!readyForBackgroundRotation()) {
			return;
		}
		const auto request = cacheBackgroundRequest(
			_backgroundState.now.area,
			kAddRotationDoubled);
		if (forRequest == request) {
			_mutableBackground.gradientRotation
				= (_mutableBackground.gradientRotation
					+ kAddRotationDoubled) % 720;
			_backgroundNext = std::move(result);
		}
	});
}

auto ChatTheme::cacheBackgroundRequest(QSize area, int addRotation) const
-> CacheBackgroundRequest {
	if (background().colorForFill) {
		return {};
	}
	return {
		.background = background(),
		.area = area,
		.gradientRotationAdd = addRotation,
	};
}

void ChatTheme::cacheBackground() {
	Expects(_cacheBackgroundTimer.has_value());

	const auto now = crl::now();
	if (now - _lastBackgroundAreaChangeTime < kCacheBackgroundTimeout
		&& QGuiApplication::mouseButtons() != 0) {
		_cacheBackgroundTimer->callOnce(kCacheBackgroundFastTimeout);
		return;
	}
	cacheBackgroundNow();
}

void ChatTheme::cacheBackgroundNow() {
	if (!_backgroundCachingRequest) {
		if (const auto request = cacheBackgroundRequest(
				_cacheBackgroundArea)) {
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
			} else if (const auto request = cacheBackgroundRequest(
					_cacheBackgroundArea)) {
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

auto ChatTheme::cacheBubblesRequest(QSize area) const
-> CacheBackgroundRequest {
	if (_bubblesBackgroundPrepared.isNull()) {
		return {};
	}
	return {
		.background = {
			.gradientForFill = _bubblesBackgroundPrepared,
		},
		.area = area,
	};
}

void ChatTheme::cacheBubbles() {
	Expects(_cacheBubblesTimer.has_value());

	const auto now = crl::now();
	if (now - _lastBubblesAreaChangeTime < kCacheBackgroundTimeout
		&& QGuiApplication::mouseButtons() != 0) {
		_cacheBubblesTimer->callOnce(kCacheBackgroundFastTimeout);
		return;
	}
	cacheBubblesNow();
}

void ChatTheme::cacheBubblesNow() {
	if (!_bubblesCachingRequest) {
		if (const auto request = cacheBackgroundRequest(
				_cacheBubblesArea)) {
			cacheBubblesAsync(request);
		}
	}
}

void ChatTheme::cacheBubblesAsync(
		const CacheBackgroundRequest &request) {
	_bubblesCachingRequest = request;
	const auto weak = base::make_weak(this);
	crl::async([=] {
		if (!weak) {
			return;
		}
		crl::on_main(weak, [=, result = CacheBackground(request)]() mutable {
			if (const auto request = cacheBubblesRequest(
					_cacheBubblesArea)) {
				if (_bubblesCachingRequest != request) {
					cacheBubblesAsync(request);
				} else {
					_bubblesCachingRequest = {};
					_bubblesBackground = std::move(result);
					_bubblesBackgroundPattern->pixmap
						= _bubblesBackground.pixmap;
				}
			}
		});
	});
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
		for (auto &component : components) {
			component /= size;
		}
	}
	return QColor(components[0], components[1], components[2]);
}

QColor CountAverageColor(const std::vector<QColor> &colors) {
	Expects(colors.size() < (std::numeric_limits<int>::max() / 256));

	int components[3] = { 0 };
	auto r = 0;
	auto g = 0;
	auto b = 0;
	for (const auto &color : colors) {
		color.getRgb(&r, &g, &b);
		components[0] += r;
		components[1] += g;
		components[2] += b;
	}
	if (const auto size = colors.size()) {
		for (auto &component : components) {
			component /= size;
		}
	}
	return QColor(components[0], components[1], components[2]);
}

bool IsPatternInverted(
		const std::vector<QColor> &background,
		float64 patternOpacity) {
	return (patternOpacity > 0.)
		&& (CountAverageColor(background).toHsv().valueF() <= 0.3);
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
		Fn<void(QPainter&,bool)> drawPattern) {
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
		drawPattern(p, IsPatternInverted(bg, patternOpacity));
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
		[&](QPainter &p, bool inverted) {
			if (inverted) {
				pattern = InvertPatternImage(std::move(pattern));
			}
			p.drawImage(QRect(QPoint(), pattern.size()), pattern);
		});

	pattern = QImage();
	return result;
}

QImage InvertPatternImage(QImage pattern) {
	pattern = std::move(pattern).convertToFormat(
		QImage::Format_ARGB32_Premultiplied);
	const auto w = pattern.bytesPerLine() / 4;
	auto ints = reinterpret_cast<uint32*>(pattern.bits());
	for (auto y = 0, h = pattern.height(); y != h; ++y) {
		for (auto x = 0; x != w; ++x) {
			const auto value = (*ints >> 24);
			*ints++ = (value << 24)
				| (value << 16)
				| (value << 8)
				| value;
		}
	}
	return pattern;
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
	return Images::BlurLargeImage(std::move(image), kRadius);
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
		const ChatThemeBackgroundData &data) {
	auto prepared = (data.isPattern || data.colors.empty())
		? PreprocessBackgroundImage(
			ReadBackgroundImage(data.path, data.bytes, data.gzipSvg))
		: QImage();
	if (data.isPattern && !prepared.isNull()) {
		if (data.colors.size() < 2) {
			prepared = PreparePatternImage(
				std::move(prepared),
				data.colors,
				data.gradientRotation,
				data.patternOpacity);
		} else if (IsPatternInverted(data.colors, data.patternOpacity)) {
			prepared = InvertPatternImage(std::move(prepared));
		}
		prepared.setDevicePixelRatio(style::DevicePixelRatio());
	} else if (data.colors.empty()) {
		prepared.setDevicePixelRatio(style::DevicePixelRatio());
	}
	const auto imageMonoColor = (data.colors.size() < 2)
		? CalculateImageMonoColor(prepared)
		: std::nullopt;
	if (!prepared.isNull() && !data.isPattern && data.isBlurred) {
		prepared = PrepareBlurredBackground(std::move(prepared));
	}
	auto gradientForFill = (data.generateGradient && data.colors.size() > 1)
		? Ui::GenerateDitheredGradient(data.colors, data.gradientRotation)
		: QImage();
	return ChatThemeBackground{
		.prepared = prepared,
		.preparedForTiled = PrepareImageForTiled(prepared),
		.gradientForFill = std::move(gradientForFill),
		.colorForFill = (!prepared.isNull()
			? imageMonoColor
			: (data.colors.size() > 1 || data.colors.empty())
			? std::nullopt
			: std::make_optional(data.colors.front())),
		.colors = data.colors,
		.patternOpacity = data.patternOpacity,
		.gradientRotation = data.generateGradient ? data.gradientRotation : 0,
		.isPattern = data.isPattern,
	};
}

} // namespace Window::Theme
