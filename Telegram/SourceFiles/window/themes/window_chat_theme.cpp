/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "window/themes/window_chat_theme.h"

#include "window/themes/window_theme.h"
#include "ui/image/image_prepare.h"
#include "ui/ui_utility.h"
#include "ui/chat/message_bubble.h"
#include "history/view/history_view_element.h"

#include <QtGui/QGuiApplication>

namespace Window::Theme {
namespace {

constexpr auto kMaxChatEntryHistorySize = 50;
constexpr auto kCacheBackgroundTimeout = 3 * crl::time(1000);
constexpr auto kCacheBackgroundFastTimeout = crl::time(200);
constexpr auto kBackgroundFadeDuration = crl::time(200);

[[nodiscard]] CacheBackgroundResult CacheBackground(
		const CacheBackgroundRequest &request) {
	const auto gradient = request.gradient.isNull()
		? QImage()
		: request.recreateGradient
		? Images::GenerateGradient(
			request.gradient.size(),
			request.gradientColors,
			request.gradientRotation)
		: request.gradient;
	if (request.isPattern || request.tile || request.prepared.isNull()) {
		auto result = gradient.isNull()
			? QImage(
				request.area * cIntRetinaFactor(),
				QImage::Format_ARGB32_Premultiplied)
			: gradient.scaled(
				request.area * cIntRetinaFactor(),
				Qt::IgnoreAspectRatio,
				Qt::SmoothTransformation);
		result.setDevicePixelRatio(cRetinaFactor());
		if (!request.prepared.isNull()) {
			QPainter p(&result);
			if (!gradient.isNull()) {
				if (request.patternOpacity >= 0.) {
					p.setCompositionMode(QPainter::CompositionMode_SoftLight);
					p.setOpacity(request.patternOpacity);
				} else {
					p.setCompositionMode(
						QPainter::CompositionMode_DestinationIn);
				}
			}
			const auto tiled = request.isPattern
				? request.prepared.scaled(
					request.area.height() * cIntRetinaFactor(),
					request.area.height() * cIntRetinaFactor(),
					Qt::KeepAspectRatio,
					Qt::SmoothTransformation)
				: request.preparedForTiled;
			const auto w = tiled.width() / cRetinaFactor();
			const auto h = tiled.height() / cRetinaFactor();
			const auto cx = qCeil(request.area.width() / w);
			const auto cy = qCeil(request.area.height() / h);
			const auto rows = cy;
			const auto cols = request.isPattern ? (((cx / 2) * 2) + 1) : cx;
			const auto xshift = request.isPattern
				? (request.area.width() - cols * w) / 2
				: 0;
			for (auto y = 0; y != rows; ++y) {
				for (auto x = 0; x != cols; ++x) {
					p.drawImage(QPointF(xshift + x * w, y * h), tiled);
				}
			}
			if (!gradient.isNull()
				&& request.patternOpacity < 0.
				&& request.patternOpacity > -1.) {
				p.setCompositionMode(QPainter::CompositionMode_SourceOver);
				p.setOpacity(1. + request.patternOpacity);
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
		const auto rects = ComputeBackgroundRects(
			request.area,
			request.prepared.size());
		auto result = request.prepared.copy(rects.from).scaled(
			rects.to.width() * cIntRetinaFactor(),
			rects.to.height() * cIntRetinaFactor(),
			Qt::IgnoreAspectRatio,
			Qt::SmoothTransformation);
		result.setDevicePixelRatio(cRetinaFactor());
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

bool operator==(
		const CacheBackgroundRequest &a,
		const CacheBackgroundRequest &b) {
	return (a.prepared.cacheKey() == b.prepared.cacheKey())
		&& (a.area == b.area)
		&& (a.gradientRotation == b.gradientRotation)
		&& (a.tile == b.tile)
		&& (a.recreateGradient == b.recreateGradient)
		&& (a.gradient.cacheKey() == b.gradient.cacheKey())
		&& (a.gradientProgress == b.gradientProgress)
		&& (a.patternOpacity == b.patternOpacity);
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
	Background()->updates(
	) | rpl::start_with_next([=](const BackgroundUpdate &update) {
		if (update.type == BackgroundUpdate::Type::New
			|| update.type == BackgroundUpdate::Type::Changed) {
			clearCachedBackground();
		}
	}, _lifetime);
}

// Runs from background thread.
ChatTheme::ChatTheme(const Data::CloudTheme &theme)
: _id(theme.id)
, _palette(std::make_unique<style::palette>()) {
}

uint64 ChatTheme::key() const {
	return _id;
}

void ChatTheme::setBubblesBackground(QImage image) {
	_bubblesBackgroundPrepared = std::move(image);
	if (!_bubblesBackground.area.isEmpty()) {
		_bubblesBackground = CacheBackground({
			.prepared = _bubblesBackgroundPrepared,
			.area = _bubblesBackground.area,
		});
	}
	if (!_bubblesBackgroundPattern) {
		_bubblesBackgroundPattern = Ui::PrepareBubblePattern();
	}
	_bubblesBackgroundPattern->pixmap = _bubblesBackground.pixmap;
	_repaintBackgroundRequests.fire({});
}

HistoryView::PaintContext ChatTheme::preparePaintContext(
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
		&& !Background()->gradientForFill().isNull()) {
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
	const auto background = Background();
	if (background->paper().backgroundColors().size() < 3) {
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
			_backgroundAddRotation
				= (_backgroundAddRotation + kAddRotation) % 360;
			_backgroundNext = std::move(result);
		}
	});
}

auto ChatTheme::currentCacheRequest(QSize area, int addRotation) const
-> CacheBackgroundRequest {
	const auto background = Background();
	if (background->colorForFill()) {
		return {};
	}
	const auto rotation = background->paper().gradientRotation();
	const auto gradient = background->gradientForFill();
	return {
		.prepared = background->prepared(),
		.preparedForTiled = background->preparedForTiled(),
		.area = area,
		.gradientRotation = (rotation
			+ _backgroundAddRotation
			+ addRotation) % 360,
		.tile = background->tile(),
		.isPattern = background->paper().isPattern(),
		.recreateGradient = (addRotation != 0),
		.gradient = gradient,
		.gradientColors = (gradient.isNull()
			? std::vector<QColor>()
			: background->paper().backgroundColors()),
		.gradientProgress = 1.,
		.patternOpacity = background->paper().patternOpacity(),
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

	const auto background = Background();
	if (background->gradientForFill().isNull()
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

void ChatTheme::clearCachedBackground() {
	_backgroundState = {};
	_backgroundAddRotation = 0;
	_backgroundNext = {};
	_backgroundFade.stop();
	if (_cacheBackgroundTimer) {
		_cacheBackgroundTimer->cancel();
	}
	_repaintBackgroundRequests.fire({});
}

rpl::producer<> ChatTheme::repaintBackgroundRequests() const {
	return _repaintBackgroundRequests.events();
}

void ChatTheme::rotateComplexGradientBackground() {
	if (!_backgroundFade.animating() && !_backgroundNext.image.isNull()) {
		Background()->recacheGradientForFill(
			std::move(_backgroundNext.gradient));
		setCachedBackground(base::take(_backgroundNext));
	}
}

} // namespace Window::Theme
