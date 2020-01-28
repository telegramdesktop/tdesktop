/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/view/media_view_pip.h"

#include "media/streaming/media_streaming_player.h"
#include "media/streaming/media_streaming_document.h"
#include "media/streaming/media_streaming_utility.h"
#include "media/audio/media_audio.h"
#include "main/main_session.h"
#include "main/main_settings.h"
#include "data/data_document.h"
#include "core/application.h"
#include "ui/platform/ui_platform_utility.h"
#include "ui/widgets/buttons.h"
#include "ui/wrap/fade_wrap.h"
#include "window/window_controller.h"
#include "styles/style_window.h"
#include "styles/style_mediaview.h"
#include "styles/style_layers.h" // st::boxTitleClose

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

[[nodiscard]] QRect Transformed(
		QRect original,
		QSize minimalSize,
		QSize maximalSize,
		QPoint delta,
		RectPart by) {
	const auto width = original.width();
	const auto height = original.height();
	const auto x1 = width - minimalSize.width();
	const auto x2 = maximalSize.width() - width;
	const auto y1 = height - minimalSize.height();
	const auto y2 = maximalSize.height() - height;
	switch (by) {
	case RectPart::Center: return original.translated(delta);
	case RectPart::TopLeft:
		original.setTop(original.y() + std::clamp(delta.y(), -y2, y1));
		original.setLeft(original.x() + std::clamp(delta.x(), -x2, x1));
		return original;
	case RectPart::TopRight:
		original.setTop(original.y() + std::clamp(delta.y(), -y2, y1));
		original.setWidth(original.width() + std::clamp(delta.x(), -x1, x2));
		return original;
	case RectPart::BottomRight:
		original.setHeight(
			original.height() + std::clamp(delta.y(), -y1, y2));
		original.setWidth(original.width() + std::clamp(delta.x(), -x1, x2));
		return original;
	case RectPart::BottomLeft:
		original.setHeight(
			original.height() + std::clamp(delta.y(), -y1, y2));
		original.setLeft(original.x() + std::clamp(delta.x(), -x2, x1));
		return original;
	case RectPart::Left:
		original.setLeft(original.x() + std::clamp(delta.x(), -x2, x1));
		return original;
	case RectPart::Top:
		original.setTop(original.y() + std::clamp(delta.y(), -y2, y1));
		return original;
	case RectPart::Right:
		original.setWidth(original.width() + std::clamp(delta.x(), -x1, x2));
		return original;
	case RectPart::Bottom:
		original.setHeight(
			original.height() + std::clamp(delta.y(), -y1, y2));
		return original;
	}
	return original;
	Unexpected("RectPart in PiP Transformed.");
}

[[nodiscard]] QRect Constrained(
		QRect original,
		QSize minimalSize,
		QSize maximalSize,
		QSize ratio,
		RectPart by) {
	if (by == RectPart::Center) {
		return original;
	} else if (!original.width() && !original.height()) {
		return QRect(original.topLeft(), ratio);
	}
	const auto widthLarger = (original.width() * ratio.height())
		> (original.height() * ratio.width());
	const auto desiredSize = ratio.scaled(
		original.size(),
		(((RectParts(by) & RectPart::AllCorners)
			|| ((by == RectPart::Top || by == RectPart::Bottom)
				&& widthLarger)
			|| ((by == RectPart::Left || by == RectPart::Right)
				&& !widthLarger))
			? Qt::KeepAspectRatio
			: Qt::KeepAspectRatioByExpanding));
	const auto newSize = QSize(
		std::clamp(
			desiredSize.width(),
			minimalSize.width(),
			maximalSize.width()),
		std::clamp(
			desiredSize.height(),
			minimalSize.height(),
			maximalSize.height()));
	switch (by) {
	case RectPart::TopLeft:
		return QRect(
			original.topLeft() + QPoint(
				original.width() - newSize.width(),
				original.height() - newSize.height()),
			newSize);
	case RectPart::TopRight:
		return QRect(
			original.topLeft() + QPoint(
				0,
				original.height() - newSize.height()),
			newSize);
	case RectPart::BottomRight:
		return QRect(original.topLeft(), newSize);
	case RectPart::BottomLeft:
		return QRect(
			original.topLeft() + QPoint(
				original.width() - newSize.width(),
				0),
			newSize);
	case RectPart::Left:
		return QRect(
			original.topLeft() + QPoint(
				(original.width() - newSize.width()),
				(original.height() - newSize.height()) / 2),
			newSize);
	case RectPart::Top:
		return QRect(
			original.topLeft() + QPoint(
				(original.width() - newSize.width()) / 2,
				0),
			newSize);
	case RectPart::Right:
		return QRect(
			original.topLeft() + QPoint(
				0,
				(original.height() - newSize.height()) / 2),
			newSize);
	case RectPart::Bottom:
		return QRect(
			original.topLeft() + QPoint(
				(original.width() - newSize.width()) / 2,
				(original.height() - newSize.height())),
			newSize);
	}
	Unexpected("RectPart in PiP Constrained.");
}

} // namespace

PipPanel::PipPanel(
	QWidget *parent,
	Fn<void(QPainter&, FrameRequest)> paint)
: _parent(parent)
, _paint(std::move(paint)) {
	setWindowFlags(Qt::Tool
		| Qt::WindowStaysOnTopHint
		| Qt::FramelessWindowHint
		| Qt::WindowDoesNotAcceptFocus);
	setAttribute(Qt::WA_ShowWithoutActivating);
	setMouseTracking(true);
	resize(0, 0);
	show();
	Ui::Platform::IgnoreAllActivation(this);
}

void PipPanel::setAspectRatio(QSize ratio) {
	if (_ratio == ratio) {
		return;
	}
	_ratio = ratio;
	if (_ratio.isEmpty()) {
		_ratio = QSize(1, 1);
	}
	if (!size().isEmpty()) {
		setPosition(countPosition());
	}
}

void PipPanel::setPosition(Position position) {
	if (!position.screen.isEmpty()) {
		for (const auto screen : QApplication::screens()) {
			if (screen->geometry() == position.screen) {
				setPositionOnScreen(position, screen->availableGeometry());
				return;
			}
		}
	}
	setPositionDefault();
}

QScreen *PipPanel::myScreen() const {
	return windowHandle() ? windowHandle()->screen() : nullptr;
}

PipPanel::Position PipPanel::countPosition() const {
	const auto screen = myScreen();
	if (!screen) {
		return Position();
	}
	auto result = Position();
	result.screen = screen->geometry();
	result.geometry = geometry();
	const auto available = screen->availableGeometry();
	const auto skip = st::pipBorderSkip;
	const auto left = result.geometry.x();
	const auto right = left + result.geometry.width();
	const auto top = result.geometry.y();
	const auto bottom = top + result.geometry.height();
	if (left == available.x()) {
		result.attached |= RectPart::Left;
	} else if (right == available.x() + available.width()) {
		result.attached |= RectPart::Right;
	} else if (left == available.x() + skip) {
		result.snapped |= RectPart::Left;
	} else if (right == available.x() + available.width() - skip) {
		result.snapped |= RectPart::Right;
	}
	if (top == available.y()) {
		result.attached |= RectPart::Top;
	} else if (bottom == available.y() + available.height()) {
		result.attached |= RectPart::Bottom;
	} else if (top == available.y() + skip) {
		result.snapped |= RectPart::Top;
	} else if (bottom == available.y() + available.height() - skip) {
		result.snapped |= RectPart::Bottom;
	}
	return result;
}

void PipPanel::setPositionDefault() {
	const auto widgetScreen = [&](auto &&widget) -> QScreen* {
		if (auto handle = widget ? widget->windowHandle() : nullptr) {
			return handle->screen();
		}
		return nullptr;
	};
	const auto parentScreen = widgetScreen(_parent);
	const auto myScreen = widgetScreen(this);
	if (parentScreen && myScreen && myScreen != parentScreen) {
		windowHandle()->setScreen(parentScreen);
	}
	const auto screen = parentScreen
		? parentScreen
		: QGuiApplication::primaryScreen();
	auto position = Position();
	position.snapped = RectPart::Top | RectPart::Left;
	position.screen = screen->geometry();
	position.geometry = QRect(0, 0, st::pipDefaultSize, st::pipDefaultSize);
	setPositionOnScreen(position, screen->availableGeometry());
}

void PipPanel::setPositionOnScreen(Position position, QRect available) {
	const auto screen = available;
	const auto requestedSize = position.geometry.size();
	const auto max = std::max(requestedSize.width(), requestedSize.height());

	// Apply aspect ratio.
	const auto scaled = (_ratio.width() > _ratio.height())
		? QSize(max, max * _ratio.height() / _ratio.width())
		: QSize(max * _ratio.width() / _ratio.height(), max);

	// At least one side should not be greater than half of screen size.
	const auto byWidth = (scaled.width() * screen.height())
		> (scaled.height() * screen.width());
	const auto fit = QSize(screen.width() / 2, screen.height() / 2);
	const auto normalized = (byWidth && scaled.width() > fit.width())
		? QSize(fit.width(), fit.width() * scaled.height() / scaled.width())
		: (!byWidth && scaled.height() > fit.height())
		? QSize(
			fit.height() * scaled.width() / scaled.height(),
			fit.height())
		: scaled;

	// Apply minimal size.
	const auto min = st::pipMinimalSize;
	const auto minimalSize = (_ratio.width() > _ratio.height())
		? QSize(min * _ratio.width() / _ratio.height(), min)
		: QSize(min, min * _ratio.height() / _ratio.width());
	const auto size = QSize(
		std::max(normalized.width(), minimalSize.width()),
		std::max(normalized.height(), minimalSize.height()));

	// Apply left-right screen borders.
	const auto skip = st::pipBorderSkip;
	const auto inner = screen.marginsRemoved({ skip, skip, skip, skip });
	auto geometry = QRect(position.geometry.topLeft(), size);
	if ((position.attached & RectPart::Left)
		|| (geometry.x() < screen.x())) {
		geometry.moveLeft(screen.x());
	} else if ((position.attached & RectPart::Right)
		|| (geometry.x() + geometry.width() > screen.x() + screen.width())) {
		geometry.moveLeft(screen.x() + screen.width() - geometry.width());
	} else if (position.snapped & RectPart::Left) {
		geometry.moveLeft(inner.x());
	} else if (position.snapped & RectPart::Right) {
		geometry.moveLeft(inner.x() + inner.width() - geometry.width());
	}

	// Apply top-bottom screen borders.
	if ((position.attached & RectPart::Top) || (geometry.y() < screen.y())) {
		geometry.moveTop(screen.y());
	} else if ((position.attached & RectPart::Bottom)
		|| (geometry.y() + geometry.height()
			> screen.y() + screen.height())) {
		geometry.moveTop(
			screen.y() + screen.height() - geometry.height());
	} else if (position.snapped & RectPart::Top) {
		geometry.moveTop(inner.y());
	} else if (position.snapped & RectPart::Bottom) {
		geometry.moveTop(inner.y() + inner.height() - geometry.height());
	}

	setGeometry(geometry);
	_attached = position.attached;
	update();
}

void PipPanel::paintEvent(QPaintEvent *e) {
	QPainter p(this);

	auto request = FrameRequest();
	request.resize = request.outer = size();
	request.corners = RectPart(0)
		| ((_attached & (RectPart::Left | RectPart::Top))
			? RectPart(0)
			: RectPart::TopLeft)
		| ((_attached & (RectPart::Top | RectPart::Right))
			? RectPart(0)
			: RectPart::TopRight)
		| ((_attached & (RectPart::Right | RectPart::Bottom))
			? RectPart(0)
			: RectPart::BottomRight)
		| ((_attached & (RectPart::Bottom | RectPart::Left))
			? RectPart(0)
			: RectPart::BottomLeft);
	_paint(p, request);
}

void PipPanel::mousePressEvent(QMouseEvent *e) {
	if (e->button() != Qt::LeftButton) {
		return;
	}
	_pressState = _overState;
	_pressPoint = e->globalPos();
}

void PipPanel::mouseReleaseEvent(QMouseEvent *e) {
	if (e->button() != Qt::LeftButton || !base::take(_pressState)) {
		return;
	} else if (!base::take(_dragStartGeometry)) {
		//playbackPauseResume();
	} else {
		finishDrag(e->globalPos());
	}
}

void PipPanel::updateOverState(QPoint point) {
	const auto size = st::pipResizeArea;
	const auto overState = [&] {
		if (point.x() < size) {
			if (point.y() < size) {
				return RectPart::TopLeft;
			} else if (point.y() >= height() - size) {
				return RectPart::BottomLeft;
			} else {
				return RectPart::Left;
			}
		} else if (point.x() >= width() - size) {
			if (point.y() < size) {
				return RectPart::TopRight;
			} else if (point.y() >= height() - size) {
				return RectPart::BottomRight;
			} else {
				return RectPart::Right;
			}
		} else if (point.y() < size) {
			return RectPart::Top;
		} else if (point.y() >= height() - size) {
			return RectPart::Bottom;
		} else {
			return RectPart::Center;
		}
	}();
	if (_overState != overState) {
		_overState = overState;
		setCursor([&] {
			switch (_overState) {
			case RectPart::Center:
				return style::cur_default;
			case RectPart::TopLeft:
			case RectPart::BottomRight:
				return style::cur_sizefdiag;
			case RectPart::TopRight:
			case RectPart::BottomLeft:
				return style::cur_sizebdiag;
			case RectPart::Left:
			case RectPart::Right:
				return style::cur_sizehor;
			case RectPart::Top:
			case RectPart::Bottom:
				return style::cur_sizever;
			}
			Unexpected("State in PipPanel::updateOverState.");
		}());
	}
}

void PipPanel::mouseMoveEvent(QMouseEvent *e) {
	if (!_pressState) {
		updateOverState(e->pos());
		return;
	}
	const auto point = e->globalPos();
	const auto distance = QApplication::startDragDistance();
	if (!_dragStartGeometry
		&& (point - _pressPoint).manhattanLength() > distance) {
		_dragStartGeometry = geometry();
	}
	if (_dragStartGeometry) {
		updatePosition(point);
	}
}

void PipPanel::updatePosition(QPoint point) {
	Expects(_dragStartGeometry.has_value());
	Expects(_pressState.has_value());

	const auto dragPart = *_pressState;
	const auto screen = (dragPart == RectPart::Center)
		? ScreenFromPosition(point)
		: myScreen()
		? myScreen()->availableGeometry()
		: QRect();
	if (screen.isEmpty()) {
		return;
	}
	const auto minimalSize = _ratio.scaled(
		st::pipMinimalSize,
		st::pipMinimalSize,
		Qt::KeepAspectRatioByExpanding);
	const auto maximalSize = _ratio.scaled(
		screen.width() / 2,
		screen.height() / 2,
		Qt::KeepAspectRatio);
	const auto geometry = Transformed(
		*_dragStartGeometry,
		minimalSize,
		maximalSize,
		point - _pressPoint,
		dragPart);
	const auto valid = Constrained(
		geometry,
		minimalSize,
		maximalSize,
		_ratio,
		dragPart);
	const auto clamped = (dragPart == RectPart::Center)
		? ClampToEdges(screen, valid)
		: valid.topLeft();
	if (clamped != valid.topLeft()) {
		moveAnimated(clamped);
	} else {
		_positionAnimation.stop();
		setGeometry(valid);
	}
}

void PipPanel::finishDrag(QPoint point) {
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

void PipPanel::updatePositionAnimated() {
	const auto progress = _positionAnimation.value(1.);
	if (!_positionAnimation.animating()) {
		move(_positionAnimationTo);
		return;
	}
	const auto from = QPointF(_positionAnimationFrom);
	const auto to = QPointF(_positionAnimationTo);
	move((from + (to - from) * progress).toPoint());
}

void PipPanel::moveAnimated(QPoint to) {
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

Pip::Pip(
	QWidget *parent,
	std::shared_ptr<Streaming::Document> document,
	FnMut<void()> closeAndContinue,
	FnMut<void()> destroy)
: _instance(document, [=] { waitingAnimationCallback(); })
, _panel(parent, [=](QPainter &p, const FrameRequest &request) {
	paint(p, request);
})
, _playPauseResume(
	std::in_place,
	&_panel,
	object_ptr<Ui::IconButton>(&_panel, st::mediaviewPlayButton))
, _pictureInPicture(
	std::in_place,
	&_panel,
	object_ptr<Ui::IconButton>(&_panel, st::mediaviewFullScreenButton))
, _close(
	std::in_place,
	&_panel,
	object_ptr<Ui::IconButton>(&_panel, st::boxTitleClose))
, _closeAndContinue(std::move(closeAndContinue))
, _destroy(std::move(destroy)) {
	_close->entity()->addClickHandler([=] {
		_panel.close();
	});
	_pictureInPicture->entity()->addClickHandler([=] {
		_closeAndContinue();
	});
	_playPauseResume->entity()->addClickHandler([=] {
		playbackPauseResume();
	});
	_close->show(anim::type::instant);
	_pictureInPicture->show(anim::type::instant);
	_playPauseResume->show(anim::type::instant);
	setupPanel();
	setupStreaming();
}

void Pip::setupPanel() {
	const auto size = style::ConvertScale(_instance.info().video.size);
	if (size.isEmpty()) {
		_panel.setAspectRatio(QSize(1, 1));
	} else {
		_panel.setAspectRatio(size);
	}
	_panel.setPosition(PipPanel::Position());
	_panel.show();

	_panel.sizeValue(
	) | rpl::start_with_next([=](QSize size) {
		_close->moveToLeft(0, 0, size.width());
		const auto skip = st::mediaviewFullScreenLeft;
		const auto sum = _playPauseResume->width() + skip + _pictureInPicture->width();
		const auto left = (size.width() - sum) / 2;
		const auto top = size.height() - _playPauseResume->height() - skip;
		_playPauseResume->moveToLeft(left, top);
		_pictureInPicture->moveToRight(left, top);
	}, _panel.lifetime());

	_panel.events(
	) | rpl::filter([=](not_null<QEvent*> e) {
		return e->type() == QEvent::Close;
	}) | rpl::start_with_next([=] {
		_destroy();
	}, _panel.lifetime());
}

void Pip::updatePlayPauseResumeState(const Player::TrackState &state) {
	auto showPause = Player::ShowPauseIcon(state.state);
	if (showPause != _showPause) {
		_showPause = showPause;
		_playPauseResume->entity()->setIconOverride(
			_showPause ? &st::mediaviewPauseIcon : nullptr,
			_showPause ? &st::mediaviewPauseIconOver : nullptr);
	}
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

void Pip::paint(QPainter &p, FrameRequest request) {
	const auto image = videoFrameForDirectPaint(request);
	p.drawImage(0, 0, image);
	if (_instance.player().ready()) {
		_instance.markFrameShown();
	}
}

void Pip::handleStreamingUpdate(Streaming::Update &&update) {
	using namespace Streaming;

	update.data.match([&](Information &update) {
		_panel.setAspectRatio(update.video.size);
	}, [&](const PreloadedVideo &update) {
		updatePlaybackState();
	}, [&](const UpdateVideo &update) {
		_panel.update();
		Core::App().updateNonIdle();
		updatePlaybackState();
	}, [&](const PreloadedAudio &update) {
		updatePlaybackState();
	}, [&](const UpdateAudio &update) {
		updatePlaybackState();
	}, [&](WaitingForData) {
	}, [&](MutedByOther) {
	}, [&](Finished) {
		updatePlaybackState();
	});
}

void Pip::updatePlaybackState() {
	const auto state = _instance.player().prepareLegacyState();
	if (state.position != kTimeUnknown && state.length != kTimeUnknown) {
		updatePlayPauseResumeState(state);
	}
}

void Pip::handleStreamingError(Streaming::Error &&error) {
	_panel.close();
}

void Pip::playbackPauseResume() {
	if (_instance.player().failed()) {
		_panel.close();
	} else if (_instance.player().finished()
		|| !_instance.player().active()) {
		restartAtSeekPosition(0);
	} else if (_instance.player().paused()) {
		_instance.resume();
		updatePlaybackState();
	} else {
		_instance.pause();
		updatePlaybackState();
	}
}

void Pip::restartAtSeekPosition(crl::time position) {
	if (!_instance.info().video.cover.isNull()) {
		_instance.saveFrameToCover();
	}
	auto options = Streaming::PlaybackOptions();
	options.position = position;
	options.audioId = _instance.player().prepareLegacyState().id;
	options.speed = options.audioId.audio()
		? options.audioId.audio()->session().settings().videoPlaybackSpeed()
		: 1.;
	_instance.play(options);
	updatePlaybackState();
}

QImage Pip::videoFrame(const FrameRequest &request) const {
	if (_instance.player().ready()) {
		return _instance.frame(request);
	} else if (_preparedCoverStorage.isNull()
		|| _preparedCoverRequest != request) {
		_preparedCoverRequest = request;
		_preparedCoverStorage = Streaming::PrepareByRequest(
			_instance.info().video.cover,
			_instance.info().video.rotation,
			request,
			std::move(_preparedCoverStorage));
	}
	return _preparedCoverStorage;
}

QImage Pip::videoFrameForDirectPaint(const FrameRequest &request) const {
	const auto result = videoFrame(request);

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
	auto &cache = _frameForDirectPaint;
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
