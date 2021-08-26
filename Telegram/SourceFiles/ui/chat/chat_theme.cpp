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
constexpr auto kCacheBackgroundTimeout = 3 * crl::time(1000);
constexpr auto kCacheBackgroundFastTimeout = crl::time(200);
constexpr auto kBackgroundFadeDuration = crl::time(200);

[[nodiscard]] CacheBackgroundResult CacheBackground(
		const CacheBackgroundRequest &request) {
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
			const auto w = tiled.width() / style::DevicePixelRatio();
			const auto h = tiled.height() / style::DevicePixelRatio();
			const auto cx = int(std::ceil(request.area.width() / w));
			const auto cy = int(std::ceil(request.area.height() / h));
			const auto rows = cy;
			const auto cols = request.background.isPattern ? (((cx / 2) * 2) + 1) : cx;
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
: pixmap(Ui::PixmapFromImage(std::move(result.image)))
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
		_bubblesBackgroundPattern = Ui::PrepareBubblePattern();
	}
	_bubblesBackgroundPattern->pixmap = _bubblesBackground.pixmap;
	_repaintBackgroundRequests.fire({});
}

ChatPaintContext ChatTheme::preparePaintContext(
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
		.st = _palette ? _palette.get() : style::main_palette::get(),
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

} // namespace Window::Theme
