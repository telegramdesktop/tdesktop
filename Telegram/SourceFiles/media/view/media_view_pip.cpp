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
#include "data/data_document.h"
#include "core/application.h"
#include "ui/platform/ui_platform_utility.h"
#include "ui/widgets/buttons.h"
#include "ui/wrap/fade_wrap.h"
#include "ui/widgets/shadow.h"
#include "window/window_controller.h"
#include "styles/style_window.h"
#include "styles/style_media_view.h"
#include "styles/style_calls.h" // st::callShadow

#include <QtGui/QWindow>
#include <QtGui/QScreen>
#include <QtWidgets/QApplication>

namespace Media {
namespace View {
namespace {

constexpr auto kPipLoaderPriority = 2;
constexpr auto kSaveGeometryTimeout = crl::time(1000);

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
		RectPart by,
		RectParts attached) {
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
				((attached & RectPart::Top)
					? 0
					: (attached & RectPart::Bottom)
					? (original.height() - newSize.height())
					: (original.height() - newSize.height()) / 2)),
			newSize);
	case RectPart::Top:
		return QRect(
			original.topLeft() + QPoint(
				((attached & RectPart::Left)
					? 0
					: (attached & RectPart::Right)
					? (original.width() - newSize.width())
					: (original.width() - newSize.width()) / 2),
				0),
			newSize);
	case RectPart::Right:
		return QRect(
			original.topLeft() + QPoint(
				0,
				((attached & RectPart::Top)
					? 0
					: (attached & RectPart::Bottom)
					? (original.height() - newSize.height())
					: (original.height() - newSize.height()) / 2)),
			newSize);
	case RectPart::Bottom:
		return QRect(
			original.topLeft() + QPoint(
				((attached & RectPart::Left)
					? 0
					: (attached & RectPart::Right)
					? (original.width() - newSize.width())
					: (original.width() - newSize.width()) / 2),
				(original.height() - newSize.height())),
			newSize);
	}
	Unexpected("RectPart in PiP Constrained.");
}

[[nodiscard]] QByteArray Serialize(const PipPanel::Position &position) {
	auto result = QByteArray();
	auto stream = QDataStream(&result, QIODevice::WriteOnly);
	stream.setVersion(QDataStream::Qt_5_3);
	stream
		<< qint32(position.attached.value())
		<< qint32(position.snapped.value())
		<< position.screen
		<< position.geometry;
	stream.device()->close();

	return result;
}

[[nodiscard]] PipPanel::Position Deserialize(const QByteArray &data) {
	auto stream = QDataStream(data);
	auto result = PipPanel::Position();
	auto attached = qint32(0);
	auto snapped = qint32(0);
	stream >> attached >> snapped >> result.screen >> result.geometry;
	if (stream.status() != QDataStream::Ok) {
		return {};
	}
	result.attached = RectParts::from_raw(attached);
	result.snapped = RectParts::from_raw(snapped);
	return result;
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
	setAttribute(Qt::WA_MacAlwaysShowToolWindow);
	setAttribute(Qt::WA_NoSystemBackground);
	setAttribute(Qt::WA_TranslucentBackground);
	Ui::Platform::IgnoreAllActivation(this);
	Ui::Platform::InitOnTopPanel(this);
	setMouseTracking(true);
	resize(0, 0);
	show();
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

QRect PipPanel::inner() const {
	return rect().marginsRemoved(_padding);
}

bool PipPanel::dragging() const {
	return _dragState.has_value();
}

rpl::producer<> PipPanel::saveGeometryRequests() const {
	return _saveGeometryRequests.events();
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
	result.geometry = geometry().marginsRemoved(_padding);
	const auto available = screen->availableGeometry();
	const auto skip = st::pipBorderSkip;
	const auto left = result.geometry.x();
	const auto right = left + result.geometry.width();
	const auto top = result.geometry.y();
	const auto bottom = top + result.geometry.height();
	if (!_dragState || *_dragState != RectPart::Center) {
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

	setGeometry(geometry.marginsAdded(_padding));
	updateDecorations();
	update();
}

void PipPanel::paintEvent(QPaintEvent *e) {
	QPainter p(this);

	if (_useTransparency) {
		Ui::Platform::StartTranslucentPaint(p, e->region().rects());
	}

	auto request = FrameRequest();
	const auto inner = this->inner();
	request.resize = request.outer = inner.size() * style::DevicePixelRatio();
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
	request.radius = ImageRoundRadius::Large;
	if (_useTransparency) {
		const auto sides = RectPart::AllSides & ~_attached;
		Ui::Shadow::paint(p, inner, width(), st::callShadow);
	}
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
	} else if (!base::take(_dragState)) {
		//playbackPauseResume();
	} else {
		finishDrag(e->globalPos());
	}
}

void PipPanel::updateOverState(QPoint point) {
	const auto size = st::pipResizeArea;
	const auto ignore = _attached | _snapped;
	const auto count = [&](RectPart side, int padding) {
		return (ignore & side) ? 0 : padding ? padding : size;
	};
	const auto left = count(RectPart::Left, _padding.left());
	const auto top = count(RectPart::Top, _padding.top());
	const auto right = count(RectPart::Right, _padding.right());
	const auto bottom = count(RectPart::Bottom, _padding.bottom());
	const auto overState = [&] {
		if (point.x() < left) {
			if (point.y() < top) {
				return RectPart::TopLeft;
			} else if (point.y() >= height() - bottom) {
				return RectPart::BottomLeft;
			} else {
				return RectPart::Left;
			}
		} else if (point.x() >= width() - right) {
			if (point.y() < top) {
				return RectPart::TopRight;
			} else if (point.y() >= height() - bottom) {
				return RectPart::BottomRight;
			} else {
				return RectPart::Right;
			}
		} else if (point.y() < top) {
			return RectPart::Top;
		} else if (point.y() >= height() - bottom) {
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
				return style::cur_pointer;
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
	if (!_dragState
		&& (point - _pressPoint).manhattanLength() > distance) {
		_dragState = _pressState;
		updateDecorations();
		_dragStartGeometry = geometry().marginsRemoved(_padding);
	}
	if (_dragState) {
		processDrag(point);
	}
}

void PipPanel::processDrag(QPoint point) {
	Expects(_dragState.has_value());

	const auto dragPart = *_dragState;
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
		_dragStartGeometry,
		minimalSize,
		maximalSize,
		point - _pressPoint,
		dragPart);
	const auto valid = Constrained(
		geometry,
		minimalSize,
		maximalSize,
		_ratio,
		dragPart,
		_attached);
	const auto clamped = (dragPart == RectPart::Center)
		? ClampToEdges(screen, valid)
		: valid.topLeft();
	if (clamped != valid.topLeft()) {
		moveAnimated(clamped);
	} else {
		_positionAnimation.stop();
		setGeometry(valid.marginsAdded(_padding));
	}
}

void PipPanel::finishDrag(QPoint point) {
	const auto screen = ScreenFromPosition(point);
	const auto inner = geometry().marginsRemoved(_padding);
	const auto position = pos();
	const auto clamped = [&] {
		auto result = position;
		if (result.x() > screen.x() + screen.width() - inner.width()) {
			result.setX(screen.x() + screen.width() - inner.width());
		}
		if (result.x() < screen.x()) {
			result.setX(screen.x());
		}
		if (result.y() > screen.y() + screen.height() - inner.height()) {
			result.setY(screen.y() + screen.height() - inner.height());
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
		updateDecorations();
	}
}

void PipPanel::updatePositionAnimated() {
	const auto progress = _positionAnimation.value(1.);
	if (!_positionAnimation.animating()) {
		move(_positionAnimationTo - QPoint(_padding.left(), _padding.top()));
		if (!_dragState) {
			updateDecorations();
		}
		return;
	}
	const auto from = QPointF(_positionAnimationFrom);
	const auto to = QPointF(_positionAnimationTo);
	move((from + (to - from) * progress).toPoint()
		- QPoint(_padding.left(), _padding.top()));
}

void PipPanel::moveAnimated(QPoint to) {
	if (_positionAnimation.animating() && _positionAnimationTo == to) {
		return;
	}
	_positionAnimationTo = to;
	_positionAnimationFrom = pos() + QPoint(_padding.left(), _padding.top());
	_positionAnimation.stop();
	_positionAnimation.start(
		[=] { updatePositionAnimated(); },
		0.,
		1.,
		st::slideWrapDuration,
		anim::easeOutCirc);
}

void PipPanel::updateDecorations() {
	const auto guard = gsl::finally([&] {
		if (!_dragState) {
			_saveGeometryRequests.fire({});
		}
	});
	const auto position = countPosition();
	const auto center = position.geometry.center();
	const auto use = Ui::Platform::TranslucentWindowsSupported(center);
	const auto full = use ? st::callShadow.extend : style::margins();
	const auto padding = style::margins(
		(position.attached & RectPart::Left) ? 0 : full.left(),
		(position.attached & RectPart::Top) ? 0 : full.top(),
		(position.attached & RectPart::Right) ? 0 : full.right(),
		(position.attached & RectPart::Bottom) ? 0 : full.bottom());
	_snapped = position.snapped;
	if (_padding == padding && _attached == position.attached) {
		return;
	}
	const auto newGeometry = position.geometry.marginsAdded(padding);
	_attached = position.attached;
	_padding = padding;
	_useTransparency = use;
	setAttribute(Qt::WA_OpaquePaintEvent, !_useTransparency);
	setGeometry(newGeometry);
	update();
}

Pip::Pip(
	not_null<Delegate*> delegate,
	std::shared_ptr<Streaming::Document> document,
	FnMut<void()> closeAndContinue,
	FnMut<void()> destroy)
: _delegate(delegate)
, _instance(document, [=] { waitingAnimationCallback(); })
, _panel(
	_delegate->pipParentWidget(),
	[=](QPainter &p, const FrameRequest &request) { paint(p, request); })
, _closeAndContinue(std::move(closeAndContinue))
, _destroy(std::move(destroy)) {
	setupPanel();
	setupButtons();
	setupStreaming();
}

void Pip::setupPanel() {
	const auto size = style::ConvertScale(_instance.info().video.size);
	if (size.isEmpty()) {
		_panel.setAspectRatio(QSize(1, 1));
	} else {
		_panel.setAspectRatio(size);
	}
	_panel.setPosition(Deserialize(_delegate->pipLoadGeometry()));
	_panel.show();

	_panel.saveGeometryRequests(
	) | rpl::start_with_next([=] {
		saveGeometry();
	}, _panel.lifetime());

	_panel.events(
	) | rpl::start_with_next([=](not_null<QEvent*> e) {
		switch (e->type()) {
		case QEvent::Close: handleClose(); break;
		case QEvent::Leave: handleLeave(); break;
		case QEvent::MouseMove:
			handleMouseMove(static_cast<QMouseEvent*>(e.get())->pos());
			break;
		case QEvent::MouseButtonPress:
			handleMousePress(static_cast<QMouseEvent*>(e.get())->button());
			break;
		case QEvent::MouseButtonRelease:
			handleMouseRelease(static_cast<QMouseEvent*>(e.get())->button());
			break;
		case QEvent::MouseButtonDblClick:
			handleDoubleClick(static_cast<QMouseEvent*>(e.get())->button());
			break;
		}
	}, _panel.lifetime());
}

void Pip::handleClose() {
	crl::on_main(&_panel, [=] {
		_destroy();
	});
}

void Pip::handleLeave() {
	setOverState(OverState::None);
}

void Pip::handleMouseMove(QPoint position) {
	setOverState(computeState(position));
}

void Pip::setOverState(OverState state) {
	if (_over == state) {
		return;
	}
	const auto was = _over;
	_over = state;
	const auto nowShown = (_over != OverState::None);
	if ((was != OverState::None) != nowShown) {
		_controlsShown.start(
			[=] { _panel.update(); },
			nowShown ? 0. : 1.,
			nowShown ? 1. : 0.,
			st::fadeWrapDuration,
			anim::linear);
	}
	const auto check = [&](Button &button) {
		const auto now = (_over == button.state);
		if ((was == button.state) != now) {
			button.active.start(
				[=, &button] { _panel.update(button.icon); },
				now ? 0. : 1.,
				now ? 1. : 0.,
				st::fadeWrapDuration,
				anim::linear);
		}
	};
	check(_close);
	check(_enlarge);
	check(_play);
	_panel.update();
}

void Pip::handleMousePress(Qt::MouseButton button) {
	if (button != Qt::LeftButton) {
		return;
	}
	_pressed = _over;
}

void Pip::handleMouseRelease(Qt::MouseButton button) {
	const auto pressed = base::take(_pressed);
	if (button != Qt::LeftButton
		|| _panel.dragging()
		|| !pressed
		|| *pressed != _over) {
		_lastHandledPress = std::nullopt;
		return;
	}

	_lastHandledPress = _over;
	switch (_over) {
	case OverState::Close: _panel.close(); break;
	case OverState::Enlarge: _closeAndContinue(); break;
	case OverState::Other: playbackPauseResume(); break;
	}
}

void Pip::handleDoubleClick(Qt::MouseButton button) {
	if (_over != OverState::Other
		|| !_lastHandledPress
		|| *_lastHandledPress != _over) {
		return;
	}
	playbackPauseResume(); // Un-click the first click.
	_closeAndContinue();
}

void Pip::setupButtons() {
	_close.state = OverState::Close;
	_enlarge.state = OverState::Enlarge;
	_playback.state = OverState::Playback;
	_play.state = OverState::Other;
	_panel.sizeValue(
	) | rpl::map([=] {
		return _panel.inner();
	}) | rpl::start_with_next([=](QRect rect) {
		const auto skip = st::pipControlSkip;
		_close.area = QRect(
			rect.x(),
			rect.y(),
			st::pipCloseIcon.width() + 2 * skip,
			st::pipCloseIcon.height() + 2 * skip);
		_close.icon = _close.area.marginsRemoved({ skip, skip, skip, skip });
		_enlarge.area = _enlarge.icon = QRect(
			rect.x() + rect.width() - 2 * skip - st::pipEnlargeIcon.width(),
			rect.y(),
			st::pipEnlargeIcon.width() + 2 * skip,
			st::pipEnlargeIcon.height() + 2 * skip);
		_enlarge.icon = _enlarge.area.marginsRemoved(
			{ skip, skip, skip, skip });
		_play.icon = QRect(
			rect.x() + (rect.width() - st::pipPlayIcon.width()) / 2,
			(rect.y()
				+ rect.height()
				- st::pipPlayBottom
				- st::pipPlayIcon.height()),
			st::pipPlayIcon.width(),
			st::pipPlayIcon.height());
		const auto playbackHeight = 2 * st::pipPlaybackSkip
			+ st::pipPlaybackWidth;
		_playback.area = QRect(
			rect.x(),
			rect.y() + rect.height() - playbackHeight,
			rect.width(),
			playbackHeight);
	}, _panel.lifetime());
}

void Pip::saveGeometry() {
	_delegate->pipSaveGeometry(Serialize(_panel.countPosition()));
}

void Pip::updatePlayPauseResumeState(const Player::TrackState &state) {
	auto showPause = Player::ShowPauseIcon(state.state);
	if (showPause != _showPause) {
		_showPause = showPause;
		_panel.update();
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
	p.drawImage(
		QRect{
			_panel.inner().topLeft(),
			request.outer / style::DevicePixelRatio() },
		image);
	if (_instance.player().ready()) {
		_instance.markFrameShown();
	}
	paintControls(p);
}

void Pip::paintControls(QPainter &p) {
	const auto shown = _controlsShown.value(
		(_over != OverState::None) ? 1. : 0.);
	if (!shown) {
		return;
	}
	p.setOpacity(shown);

	const auto outer = _panel.width();
	const auto drawOne = [&](
			const Button &button,
			const style::icon &icon,
			const style::icon &iconOver) {
		const auto over = button.active.value(
			(_over == button.state) ? 1. : 0.);
		if (over < 1.) {
			icon.paint(p, button.icon.x(), button.icon.y(), outer);
		}
		if (over > 0.) {
			p.setOpacity(over * shown);
			iconOver.paint(p, button.icon.x(), button.icon.y(), outer);
			p.setOpacity(shown);
		}
	};
	drawOne(
		_play,
		_showPause ? st::pipPauseIcon : st::pipPlayIcon,
		_showPause ? st::pipPauseIconOver : st::pipPlayIconOver);
	drawOne(_close, st::pipCloseIcon, st::pipCloseIconOver);
	drawOne(_enlarge, st::pipEnlargeIcon, st::pipEnlargeIconOver);
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
	options.speed = _delegate->pipPlaybackSpeed();
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

Pip::OverState Pip::computeState(QPoint position) const {
	if (!_panel.inner().contains(position)) {
		return OverState::None;
	} else if (_close.area.contains(position)) {
		return OverState::Close;
	} else if (_enlarge.area.contains(position)) {
		return OverState::Enlarge;
	} else if (_playback.area.contains(position)) {
		return OverState::Playback;
	} else {
		return OverState::Other;
	}
}

void Pip::waitingAnimationCallback() {
}

} // namespace View
} // namespace Media
