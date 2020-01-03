/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/view/media_view_pip.h"

#include "media/streaming/media_streaming_player.h"
#include "media/streaming/media_streaming_document.h"
#include "core/application.h"
#include "window/window_controller.h"
#include "styles/style_window.h"
#include "styles/style_mediaview.h"

#include <QtGui/QWindow>
#include <QtGui/QScreen>
#include <QtWidgets/QApplication>

namespace Media {
namespace View {
namespace {

constexpr auto kPipLoaderPriority = 2;

[[nodiscard]] QRect ScreenFromPosition(QPoint point) {
	const auto screen = QGuiApplication::screenAt(point);
	const auto use = screen ? screen : QGuiApplication::primaryScreen();
	return use
		? use->availableGeometry()
		: QRect(0, 0, st::windowDefaultWidth, st::windowDefaultHeight);
}

} // namespace

Pip::Pip(
	std::shared_ptr<Streaming::Document> document,
	FnMut<void()> closeAndContinue)
: _instance(document, [=] { waitingAnimationCallback(); })
, _closeAndContinue(std::move(closeAndContinue)) {
	setWindowFlags(Qt::WindowStaysOnTopHint | Qt::FramelessWindowHint);
	setupSize();
	setupStreaming();
}

void Pip::setupSize() {
	_size = style::ConvertScale(_instance.info().video.size);
	if (_size.isEmpty()) {
		_size = QSize(st::windowMinWidth, st::windowMinHeight);
	}

	const auto widgetScreen = [&](auto &&widget) -> QScreen* {
		if (auto handle = widget ? widget->windowHandle() : nullptr) {
			return handle->screen();
		}
		return nullptr;
	};
	const auto window = Core::App().activeWindow()
		? Core::App().activeWindow()->widget().get()
		: nullptr;
	const auto activeWindowScreen = widgetScreen(window);
	const auto myScreen = widgetScreen(this);
	if (activeWindowScreen && myScreen && myScreen != activeWindowScreen) {
		windowHandle()->setScreen(activeWindowScreen);
	}
	const auto screen = activeWindowScreen
		? activeWindowScreen
		: QGuiApplication::primaryScreen();
	const auto available = screen->geometry();
	const auto fit = QSize(available.width() / 2, available.height() / 2);
	if (_size.width() > fit.width() || _size.height() > fit.height()) {
		const auto byHeight = (fit.width() * _size.height() > fit.height() * _size.width());
		_size = byHeight
			? QSize(_size.width() * fit.height() / _size.height(), fit.height())
			: QSize(fit.width(), _size.height() * fit.width() / _size.width());
	}
	const auto skip = st::pipBorderSkip;
	setGeometry({
		available.x() + skip,
		available.y() + skip,
		_size.width(),
		_size.height()
	});
}

void Pip::setupStreaming() {
	_instance.setPriority(kPipLoaderPriority);
	_instance.lockPlayer();

	_instance.player().updates(
	) | rpl::start_with_next_error([=](Streaming::Update &&update) {
		handleStreamingUpdate(std::move(update));
	}, [=](Streaming::Error &&error) {
		handleStreamingError(std::move(error));
	}, _instance.lifetime());
}

void Pip::handleStreamingUpdate(Streaming::Update &&update) {
	using namespace Streaming;

	update.data.match([&](Information &update) {
		setupSize();
	}, [&](const PreloadedVideo &update) {
		//updatePlaybackState();
	}, [&](const UpdateVideo &update) {
		this->update();
		Core::App().updateNonIdle();
		//updatePlaybackState();
	}, [&](const PreloadedAudio &update) {
		//updatePlaybackState();
	}, [&](const UpdateAudio &update) {
		//updatePlaybackState();
	}, [&](WaitingForData) {
	}, [&](MutedByOther) {
	}, [&](Finished) {
		//updatePlaybackState();
		close();
	});
}

void Pip::handleStreamingError(Streaming::Error &&error) {
	close();
}

void Pip::paintEvent(QPaintEvent *e) {
	QPainter p(this);

	const auto rect = QRect(QPoint(), size());
	const auto image = videoFrameForDirectPaint();
	const auto rotation = _instance.info().video.rotation;
	const auto rotated = [](QRect rect, int rotation) {
		switch (rotation) {
		case 0: return rect;
		case 90: return QRect(
			rect.y(),
			-rect.x() - rect.width(),
			rect.height(),
			rect.width());
		case 180: return QRect(
			-rect.x() - rect.width(),
			-rect.y() - rect.height(),
			rect.width(),
			rect.height());
		case 270: return QRect(
			-rect.y() - rect.height(),
			rect.x(),
			rect.height(),
			rect.width());
		}
		Unexpected("Rotation in OverlayWidget::paintTransformedVideoFrame");
	};

	PainterHighQualityEnabler hq(p);
	if (rotation) {
		p.save();
		p.rotate(rotation);
	}
	p.drawImage(rotated(rect, rotation), image);
	if (rotation) {
		p.restore();
	}
	if (_instance.player().ready()) {
		_instance.markFrameShown();
	}
}

void Pip::mousePressEvent(QMouseEvent *e) {
	if (e->button() != Qt::LeftButton) {
		return;
	}
	_pressPoint = e->globalPos();
}

void Pip::mouseReleaseEvent(QMouseEvent *e) {
	if (e->button() != Qt::LeftButton || !base::take(_pressPoint)) {
		return;
	} else if (!base::take(_dragStartPosition)) {
		playbackPauseResume();
	} else {
		finishDrag(e->globalPos());
	}
}

void Pip::mouseMoveEvent(QMouseEvent *e) {
	if (!_pressPoint) {
		return;
	}
	const auto point = e->globalPos();
	const auto distance = QApplication::startDragDistance();
	if (!_dragStartPosition
		&& (point - *_pressPoint).manhattanLength() > distance) {
		_dragStartPosition = pos();
	}
	if (_dragStartPosition) {
		updatePosition(point);
	}
}

void Pip::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Space) {
		playbackPauseResume();
	} else if (e->key() == Qt::Key_Escape) {
		crl::on_main(this, [=] {
			_closeAndContinue();
		});
	}
}

void Pip::playbackPauseResume() {
	if (_instance.player().finished() || !_instance.player().active()) {
		//restartAtSeekPosition(0);
	} else if (_instance.player().paused()) {
		_instance.resume();
			//updatePlaybackState();
			//playbackPauseMusic();
	} else {
		_instance.pause();
		//updatePlaybackState();
	}
}

[[nodiscard]] QPoint ClampToEdges(QRect screen, QRect inner) {
	const auto skip = st::pipBorderSkip;
	const auto area = st::pipBorderSnapArea;
	const auto sleft = screen.x() + skip;
	const auto stop = screen.y() + skip;
	const auto sright = screen.x() + screen.width() - skip;
	const auto sbottom = screen.y() + screen.height() - skip;
	const auto ileft = inner.x();
	const auto itop = inner.y();
	const auto iright = inner.x() + inner.width();
	const auto ibottom = inner.y() + inner.height();
	auto shiftx = 0;
	auto shifty = 0;
	if (iright + shiftx >= sright - area && iright + shiftx < sright + area) {
		shiftx += (sright - iright);
	}
	if (ileft + shiftx >= sleft - area && ileft + shiftx < sleft + area) {
		shiftx += (sleft - ileft);
	}
	if (ibottom + shifty >= sbottom - area && ibottom + shifty < sbottom + area) {
		shifty += (sbottom - ibottom);
	}
	if (itop + shifty >= stop - area && itop + shifty < stop + area) {
		shifty += (stop - itop);
	}
	return inner.topLeft() + QPoint(shiftx, shifty);
}

void Pip::updatePosition(QPoint point) {
	Expects(_dragStartPosition.has_value());

	const auto position = *_dragStartPosition + (point - *_pressPoint);
	const auto screen = ScreenFromPosition(point);
	const auto clamped = ClampToEdges(screen, QRect(position, size()));
	if (clamped != position) {
		moveAnimated(clamped);
	} else {
		_positionAnimation.stop();
		move(position);
	}
}

void Pip::finishDrag(QPoint point) {
	const auto screen = ScreenFromPosition(point);
	const auto position = pos();
	const auto clamped = [&] {
		auto result = position;
		if (result.x() > screen.x() + screen.width() - width()) {
			result.setX(screen.x() + screen.width() - width());
		}
		if (result.x() < screen.x()) {
			result.setX(screen.x());
		}
		if (result.y() > screen.y() + screen.height() - height()) {
			result.setY(screen.y() + screen.height() - height());
		}
		if (result.y() < screen.y()) {
			result.setY(screen.y());
		}
		return result;
	}();
	if (position != clamped) {
		moveAnimated(clamped);
	} else {
		_positionAnimation.stop();
	}
}

void Pip::updatePositionAnimated() {
	const auto progress = _positionAnimation.value(1.);
	if (!_positionAnimation.animating()) {
		move(_positionAnimationTo);
		return;
	}
	const auto from = QPointF(_positionAnimationFrom);
	const auto to = QPointF(_positionAnimationTo);
	move((from + (to - from) * progress).toPoint());
}

void Pip::moveAnimated(QPoint to) {
	if (_positionAnimation.animating() && _positionAnimationTo == to) {
		return;
	}
	_positionAnimationTo = to;
	_positionAnimationFrom = pos();
	_positionAnimation.stop();
	_positionAnimation.start(
		[=] { updatePositionAnimated(); },
		0.,
		1.,
		st::slideWrapDuration,
		anim::easeOutCirc);
}

QImage Pip::videoFrame() const {
	return _instance.player().ready()
		? _instance.frame(Streaming::FrameRequest())
		: _instance.info().video.cover;
}

QImage Pip::videoFrameForDirectPaint() const {
	const auto result = videoFrame();

#ifdef USE_OPENGL_OVERLAY_WIDGET
	const auto bytesPerLine = result.bytesPerLine();
	if (bytesPerLine == result.width() * 4) {
		return result;
	}

	// On macOS 10.8+ we use QOpenGLWidget as OverlayWidget base class.
	// The OpenGL painter can't paint textures where byte data is with strides.
	// So in that case we prepare a compact copy of the frame to render.
	//
	// See Qt commit ed557c037847e343caa010562952b398f806adcd
	//
	auto &cache = _streamed->frameForDirectPaint;
	if (cache.size() != result.size()) {
		cache = QImage(result.size(), result.format());
	}
	const auto height = result.height();
	const auto line = cache.bytesPerLine();
	Assert(line == result.width() * 4);
	Assert(line < bytesPerLine);

	auto from = result.bits();
	auto to = cache.bits();
	for (auto y = 0; y != height; ++y) {
		memcpy(to, from, line);
		to += line;
		from += bytesPerLine;
	}
	return cache;
#endif // USE_OPENGL_OVERLAY_WIDGET

	return result;
}

void Pip::waitingAnimationCallback() {
}

} // namespace View
} // namespace Media
