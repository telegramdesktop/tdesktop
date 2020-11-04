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
#include "media/view/media_view_playback_progress.h"
#include "media/audio/media_audio.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_file_origin.h"
#include "data/data_session.h"
#include "data/data_media_rotation.h"
#include "main/main_account.h"
#include "main/main_session.h"
#include "core/application.h"
#include "platform/platform_specific.h"
#include "base/platform/base_platform_info.h"
#include "ui/platform/ui_platform_utility.h"
#include "ui/widgets/buttons.h"
#include "ui/wrap/fade_wrap.h"
#include "ui/widgets/shadow.h"
#include "ui/text/format_values.h"
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
constexpr auto kMsInSecond = 1000;

[[nodiscard]] bool IsWindowControlsOnLeft() {
	return Platform::IsMac();
}

[[nodiscard]] QRect ScreenFromPosition(QPoint point) {
	const auto screen = [&]() -> QScreen* {
		for (const auto screen : QGuiApplication::screens()) {
			if (screen->geometry().contains(point)) {
				return screen;
			}
		}
		return nullptr;
	}();
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

Streaming::FrameRequest UnrotateRequest(
		const Streaming::FrameRequest &request,
		int rotation) {
	if (!rotation) {
		return request;
	}
	const auto unrotatedCorner = [&](RectPart corner) {
		if (!(request.corners & corner)) {
			return RectPart(0);
		}
		switch (corner) {
		case RectPart::TopLeft:
			return (rotation == 90)
				? RectPart::BottomLeft
				: (rotation == 180)
				? RectPart::BottomRight
				: RectPart::TopRight;
		case RectPart::TopRight:
			return (rotation == 90)
				? RectPart::TopLeft
				: (rotation == 180)
				? RectPart::BottomLeft
				: RectPart::BottomRight;
		case RectPart::BottomRight:
			return (rotation == 90)
				? RectPart::TopRight
				: (rotation == 180)
				? RectPart::TopLeft
				: RectPart::BottomLeft;
		case RectPart::BottomLeft:
			return (rotation == 90)
				? RectPart::BottomRight
				: (rotation == 180)
				? RectPart::TopRight
				: RectPart::TopLeft;
		}
		Unexpected("Corner in rotateCorner.");
	};
	auto result = request;
	result.outer = FlipSizeByRotation(request.outer, rotation);
	result.resize = FlipSizeByRotation(request.resize, rotation);
	result.corners = unrotatedCorner(RectPart::TopLeft)
		| unrotatedCorner(RectPart::TopRight)
		| unrotatedCorner(RectPart::BottomRight)
		| unrotatedCorner(RectPart::BottomLeft);
	return result;
}

Qt::Edges RectPartToQtEdges(RectPart rectPart) {
	switch (rectPart) {
	case RectPart::TopLeft:
		return Qt::TopEdge | Qt::LeftEdge;
	case RectPart::TopRight:
		return Qt::TopEdge | Qt::RightEdge;
	case RectPart::BottomRight:
		return Qt::BottomEdge | Qt::RightEdge;
	case RectPart::BottomLeft:
		return Qt::BottomEdge | Qt::LeftEdge;
	case RectPart::Left:
		return Qt::LeftEdge;
	case RectPart::Top:
		return Qt::TopEdge;
	case RectPart::Right:
		return Qt::RightEdge;
	case RectPart::Bottom:
		return Qt::BottomEdge;
	}

	return Qt::Edges();
}

} // namespace

QRect RotatedRect(QRect rect, int rotation) {
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
	Unexpected("Rotation in RotatedRect.");
}

bool UsePainterRotation(int rotation) {
	return Platform::IsMac() || !(rotation % 180);
}

QSize FlipSizeByRotation(QSize size, int rotation) {
	return (((rotation / 90) % 2) == 1)
		? QSize(size.height(), size.width())
		: size;
}

QImage RotateFrameImage(QImage image, int rotation) {
	auto transform = QTransform();
	transform.rotate(rotation);
	return image.transformed(transform);
}

PipPanel::PipPanel(
	QWidget *parent,
	Fn<void(QPainter&, FrameRequest)> paint)
: PipParent(Core::App().getModalParent())
, _parent(parent)
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

RectParts PipPanel::attached() const {
	return _attached;
}

void PipPanel::setDragDisabled(bool disabled) {
	_dragDisabled = disabled;
	if (_dragState) {
		_dragState = std::nullopt;
	}
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
	if ((!_dragState || *_dragState != RectPart::Center)
		&& !Platform::IsWayland()) {
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

	// Apply maximal size.
	const auto maximalSize = (_ratio.width() > _ratio.height())
		? QSize(fit.width(), fit.width() * _ratio.height() / _ratio.width())
		: QSize(fit.height() * _ratio.width() / _ratio.height(), fit.height());

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

	geometry += _padding;

	setGeometry(geometry);
	setMinimumSize(minimalSize);
	setMaximumSize(
		std::max(minimalSize.width(), maximalSize.width()),
		std::max(minimalSize.height(), maximalSize.height()));
	updateDecorations();
	update();
}

void PipPanel::paintEvent(QPaintEvent *e) {
	QPainter p(this);

	if (_useTransparency) {
		Ui::Platform::StartTranslucentPaint(p, e->region());
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
	updateOverState(e->pos());
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
		&& (point - _pressPoint).manhattanLength() > distance
		&& !_dragDisabled) {
		_dragState = _pressState;
		updateDecorations();
		_dragStartGeometry = geometry().marginsRemoved(_padding);
	}
	if (_dragState) {
		if (Platform::IsWayland()) {
			startSystemDrag();
		} else {
			processDrag(point);
		}
	}
}

void PipPanel::startSystemDrag() {
	Expects(_dragState.has_value());

	const auto stateEdges = RectPartToQtEdges(*_dragState);
	if (stateEdges) {
		if (!Platform::StartSystemResize(windowHandle(), stateEdges)) {
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0) || defined DESKTOP_APP_QT_PATCHED
			windowHandle()->startSystemResize(stateEdges);
#endif // Qt >= 5.15 || DESKTOP_APP_QT_PATCHED
		}
	} else {
		if (!Platform::StartSystemMove(windowHandle())) {
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0) || defined DESKTOP_APP_QT_PATCHED
			windowHandle()->startSystemMove();
#endif // Qt >= 5.15 || DESKTOP_APP_QT_PATCHED
		}
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
		const auto newGeometry = valid.marginsAdded(_padding);
		_positionAnimation.stop();
		setGeometry(newGeometry);
	}
}

void PipPanel::finishDrag(QPoint point) {
	const auto screen = ScreenFromPosition(point);
	const auto inner = geometry().marginsRemoved(_padding);
	const auto position = pos();
	const auto clamped = [&] {
		auto result = position;
		if (Platform::IsWayland()) {
			return result;
		}
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
	not_null<DocumentData*> data,
	FullMsgId contextId,
	std::shared_ptr<Streaming::Document> shared,
	FnMut<void()> closeAndContinue,
	FnMut<void()> destroy)
: _delegate(delegate)
, _data(data)
, _contextId(contextId)
, _instance(std::move(shared), [=] { waitingAnimationCallback(); })
, _panel(
	_delegate->pipParentWidget(),
	[=](QPainter &p, const FrameRequest &request) { paint(p, request); })
, _playbackProgress(std::make_unique<PlaybackProgress>())
, _rotation(data->owner().mediaRotation().get(data))
, _roundRect(ImageRoundRadius::Large, st::radialBg)
, _closeAndContinue(std::move(closeAndContinue))
, _destroy(std::move(destroy)) {
	setupPanel();
	setupButtons();
	setupStreaming();

	_data->session().account().sessionChanges(
	) | rpl::start_with_next([=] {
		_destroy();
	}, _panel.lifetime());
}

Pip::~Pip() = default;

void Pip::setupPanel() {
	const auto size = [&] {
		if (!_instance.info().video.size.isEmpty()) {
			return _instance.info().video.size;
		}
		const auto media = _data->activeMediaView();
		if (media) {
			media->goodThumbnailWanted();
		}
		const auto good = media ? media->goodThumbnail() : nullptr;
		const auto original = good ? good->size() : _data->dimensions;
		return original.isEmpty() ? QSize(1, 1) : original;
	}();
	_panel.setAspectRatio(FlipSizeByRotation(size, _rotation));
	_panel.setPosition(Deserialize(_delegate->pipLoadGeometry()));
	_panel.show();

	_panel.saveGeometryRequests(
	) | rpl::start_with_next([=] {
		saveGeometry();
	}, _panel.lifetime());

	_panel.events(
	) | rpl::start_with_next([=](not_null<QEvent*> e) {
		const auto mousePosition = [&] {
			return static_cast<QMouseEvent*>(e.get())->pos();
		};
		const auto mouseButton = [&] {
			return static_cast<QMouseEvent*>(e.get())->button();
		};
		switch (e->type()) {
		case QEvent::Close: handleClose(); break;
		case QEvent::Leave: handleLeave(); break;
		case QEvent::MouseMove:
			handleMouseMove(mousePosition());
			break;
		case QEvent::MouseButtonPress:
			handleMousePress(mousePosition(), mouseButton());
			break;
		case QEvent::MouseButtonRelease:
			handleMouseRelease(mousePosition(), mouseButton());
			break;
		case QEvent::MouseButtonDblClick:
			handleDoubleClick(mouseButton());
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
	seekUpdate(position);
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
	if (!_pressed) {
		updateActiveState(was);
	}
	_panel.update();
}

void Pip::setPressedState(std::optional<OverState> state) {
	if (_pressed == state) {
		return;
	}
	const auto was = activeState();
	_pressed = state;
	updateActiveState(was);
}

Pip::OverState Pip::activeState() const {
	return _pressed.value_or(_over);
}

float64 Pip::activeValue(const Button &button) const {
	return button.active.value((activeState() == button.state) ? 1. : 0.);
}

void Pip::updateActiveState(OverState was) {
	const auto check = [&](Button &button) {
		const auto now = (activeState() == button.state);
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
	check(_playback);
}

void Pip::handleMousePress(QPoint position, Qt::MouseButton button) {
	if (button != Qt::LeftButton) {
		return;
	}
	_pressed = _over;
	if (_over == OverState::Playback) {
		_panel.setDragDisabled(true);
	}
	seekUpdate(position);
}

void Pip::handleMouseRelease(QPoint position, Qt::MouseButton button) {
	if (button != Qt::LeftButton) {
		return;
	}
	seekUpdate(position);
	const auto pressed = base::take(_pressed);
	if (pressed && *pressed == OverState::Playback) {
		_panel.setDragDisabled(false);
		seekFinish(_playbackProgress->value());
		return;
	} else if (_panel.dragging() || !pressed || *pressed != _over) {
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

void Pip::seekUpdate(QPoint position) {
	if (!_pressed || *_pressed != OverState::Playback) {
		return;
	}
	const auto unbound = (position.x() - _playback.icon.x())
		/ float64(_playback.icon.width());
	const auto progress = std::clamp(unbound, 0., 1.);
	seekProgress(progress);
}

void Pip::seekProgress(float64 value) {
	if (!_lastDurationMs) {
		return;
	}

	_playbackProgress->setValue(value, false);

	const auto positionMs = std::clamp(
		static_cast<crl::time>(value * _lastDurationMs),
		crl::time(0),
		_lastDurationMs);
	if (_seekPositionMs != positionMs) {
		_seekPositionMs = positionMs;
		if (!_instance.player().paused()
			&& !_instance.player().finished()) {
			_pausedBySeek = true;
			playbackPauseResume();
		}
		updatePlaybackTexts(_seekPositionMs, _lastDurationMs, kMsInSecond);
	}
}

void Pip::seekFinish(float64 value) {
	if (!_lastDurationMs) {
		return;
	}

	const auto positionMs = std::clamp(
		static_cast<crl::time>(value * _lastDurationMs),
		crl::time(0),
		_lastDurationMs);
	_seekPositionMs = -1;
	_startPaused = !_pausedBySeek && !_instance.player().finished();
	restartAtSeekPosition(positionMs);
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
		_enlarge.area = QRect(
			_close.area.x() + _close.area.width(),
			rect.y(),
			st::pipEnlargeIcon.width() + 2 * skip,
			st::pipEnlargeIcon.height() + 2 * skip);
		if (!IsWindowControlsOnLeft()) {
			_close.area.moveLeft(rect.x()
				+ rect.width()
				- (_close.area.x() - rect.x())
				- _close.area.width());
			_enlarge.area.moveLeft(rect.x()
				+ rect.width()
				- (_enlarge.area.x() - rect.x())
				- _enlarge.area.width());
		}
		_close.icon = _close.area.marginsRemoved({ skip, skip, skip, skip });
		_enlarge.icon = _enlarge.area.marginsRemoved(
			{ skip, skip, skip, skip });
		_play.icon = QRect(
			rect.x() + (rect.width() - st::pipPlayIcon.width()) / 2,
			rect.y() + (rect.height() - st::pipPlayIcon.height()) / 2,
			st::pipPlayIcon.width(),
			st::pipPlayIcon.height());
		const auto playbackSkip = st::pipPlaybackSkip;
		const auto playbackHeight = 2 * playbackSkip + st::pipPlaybackWide;
		_playback.area = QRect(
			rect.x(),
			rect.y() + rect.height() - playbackHeight,
			rect.width(),
			playbackHeight);
		_playback.icon = _playback.area.marginsRemoved(
			{ playbackSkip, playbackSkip, playbackSkip, playbackSkip });
	}, _panel.lifetime());

	_playbackProgress->setValueChangedCallback([=](
			float64 value,
			float64 receivedTill) {
		_panel.update(_playback.area);
	});
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
	updatePlaybackState();
}

void Pip::paint(QPainter &p, FrameRequest request) {
	const auto image = videoFrameForDirectPaint(
		UnrotateRequest(request, _rotation));
	const auto inner = _panel.inner();
	const auto rect = QRect{
		inner.topLeft(),
		request.outer / style::DevicePixelRatio()
	};
	if (UsePainterRotation(_rotation)) {
		if (_rotation) {
			p.save();
			p.rotate(_rotation);
		}
		p.drawImage(RotatedRect(rect, _rotation), image);
		if (_rotation) {
			p.restore();
		}
	} else {
		p.drawImage(rect, RotateFrameImage(image, _rotation));
	}
	if (canUseVideoFrame()) {
		_instance.markFrameShown();
	}
	paintRadialLoading(p);
	paintControls(p);
}

void Pip::paintControls(QPainter &p) const {
	const auto shown = _controlsShown.value(
		(_over != OverState::None) ? 1. : 0.);
	if (!shown) {
		return;
	}
	p.setOpacity(shown);
	paintFade(p);
	paintButtons(p);
	paintPlayback(p);
	paintPlaybackTexts(p);
}

void Pip::paintFade(QPainter &p) const {
	using Part = RectPart;
	const auto sides = _panel.attached();
	const auto rounded = RectPart(0)
		| ((sides & (Part::Top | Part::Left)) ? Part(0) : Part::TopLeft)
		| ((sides & (Part::Top | Part::Right)) ? Part(0) : Part::TopRight)
		| ((sides & (Part::Bottom | Part::Right))
			? Part(0)
			: Part::BottomRight)
		| ((sides & (Part::Bottom | Part::Left))
			? Part(0)
			: Part::BottomLeft);
	_roundRect.paintSomeRounded(
		p,
		_panel.inner(),
		rounded | Part::NoTopBottom | Part::Top | Part::Bottom);
}

void Pip::paintButtons(QPainter &p) const {
	const auto opacity = p.opacity();
	const auto outer = _panel.width();
	const auto drawOne = [&](
			const Button &button,
			const style::icon &icon,
			const style::icon &iconOver) {
		const auto over = activeValue(button);
		if (over < 1.) {
			icon.paint(p, button.icon.x(), button.icon.y(), outer);
		}
		if (over > 0.) {
			p.setOpacity(over * opacity);
			iconOver.paint(p, button.icon.x(), button.icon.y(), outer);
			p.setOpacity(opacity);
		}
	};
	drawOne(
		_play,
		_showPause ? st::pipPauseIcon : st::pipPlayIcon,
		_showPause ? st::pipPauseIconOver : st::pipPlayIconOver);
	drawOne(_close, st::pipCloseIcon, st::pipCloseIconOver);
	drawOne(_enlarge, st::pipEnlargeIcon, st::pipEnlargeIconOver);
}

void Pip::paintPlayback(QPainter &p) const {
	const auto radius = _playback.icon.height() / 2;
	const auto shown = activeValue(_playback);
	const auto progress = _playbackProgress->value();
	const auto width = _playback.icon.width();
	const auto height = anim::interpolate(
		st::pipPlaybackWidth,
		_playback.icon.height(),
		activeValue(_playback));
	const auto left = _playback.icon.x();
	const auto top = _playback.icon.y() + _playback.icon.height() - height;
	const auto done = int(std::round(width * progress));
	PainterHighQualityEnabler hq(p);
	p.setPen(Qt::NoPen);
	if (done > 0) {
		p.setBrush(st::mediaviewPipPlaybackActive);
		p.setClipRect(left, top, done, height);
		p.drawRoundedRect(
			left,
			top,
			std::min(done + radius, width),
			height,
			radius,
			radius);
	}
	if (done < width) {
		const auto from = std::max(left + done - radius, left);
		p.setBrush(st::mediaviewPipPlaybackInactive);
		p.setClipRect(left + done, top, width - done, height);
		p.drawRoundedRect(
			from,
			top,
			left + width - from,
			height,
			radius,
			radius);
	}
	p.setClipping(false);
}

void Pip::paintPlaybackTexts(QPainter &p) const {
	const auto left = _playback.area.x() + st::pipPlaybackTextSkip;
	const auto right = _playback.area.x()
		+ _playback.area.width()
		- st::pipPlaybackTextSkip;
	const auto top = _playback.icon.y()
		- st::pipPlaybackFont->height
		+ st::pipPlaybackFont->ascent;

	p.setFont(st::pipPlaybackFont);
	p.setPen(st::mediaviewPipControlsFgOver);
	p.drawText(left, top, _timeAlready);
	p.drawText(right - _timeLeftWidth, top, _timeLeft);
}

void Pip::handleStreamingUpdate(Streaming::Update &&update) {
	using namespace Streaming;

	v::match(update.data, [&](Information &update) {
		_panel.setAspectRatio(
			FlipSizeByRotation(update.video.size, _rotation));
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
	updatePlayPauseResumeState(state);
	if (state.position == kTimeUnknown
		|| state.length == kTimeUnknown
		|| _pausedBySeek) {
		return;
	}
	_playbackProgress->updateState(state);

	qint64 position = 0, length = state.length;
	if (Player::IsStoppedAtEnd(state.state)) {
		position = state.length;
	} else if (!Player::IsStoppedOrStopping(state.state)) {
		position = state.position;
	} else {
		position = 0;
	}
	const auto playFrequency = state.frequency;
	_lastDurationMs = (state.length * crl::time(1000)) / playFrequency;

	if (_seekPositionMs < 0) {
		updatePlaybackTexts(position, state.length, playFrequency);
	}
}

void Pip::updatePlaybackTexts(
		int64 position,
		int64 length,
		int64 frequency) {
	const auto playAlready = position / frequency;
	const auto playLeft = (length / frequency) - playAlready;
	const auto already = Ui::FormatDurationText(playAlready);
	const auto minus = QChar(8722);
	const auto left = minus + Ui::FormatDurationText(playLeft);
	if (_timeAlready == already && _timeLeft == left) {
		return;
	}
	_timeAlready = already;
	_timeLeft = left;
	_timeLeftWidth = st::pipPlaybackFont->width(_timeLeft);
	_panel.update(QRect(
		_playback.area.x(),
		_playback.icon.y() - st::pipPlaybackFont->height,
		_playback.area.width(),
		st::pipPlaybackFont->height));
}

void Pip::handleStreamingError(Streaming::Error &&error) {
	_panel.close();
}

void Pip::playbackPauseResume() {
	if (_instance.player().failed()) {
		_panel.close();
	} else if (_instance.player().finished()
		|| !_instance.player().active()) {
		_startPaused = false;
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
	if (_startPaused) {
		_instance.pause();
	}
	_pausedBySeek = false;
	updatePlaybackState();
}

bool Pip::canUseVideoFrame() const {
	return _instance.player().ready()
		&& !_instance.info().video.cover.isNull();
}

QImage Pip::videoFrame(const FrameRequest &request) const {
	if (canUseVideoFrame()) {
		_preparedCoverStorage = QImage();
		return _instance.frame(request);
	}
	const auto &cover = _instance.info().video.cover;

	const auto media = _data->activeMediaView();
	const auto use = media
		? media
		: _data->inlineThumbnailBytes().isEmpty()
		? nullptr
		: _data->createMediaView();
	if (use) {
		use->goodThumbnailWanted();
	}
	const auto good = use ? use->goodThumbnail() : nullptr;
	const auto thumb = use ? use->thumbnail() : nullptr;
	const auto blurred = use ? use->thumbnailInline() : nullptr;

	const auto state = !cover.isNull()
		? ThumbState::Cover
		: good
		? ThumbState::Good
		: thumb
		? ThumbState::Thumb
		: blurred
		? ThumbState::Inline
		: ThumbState::Empty;
	if (_preparedCoverStorage.isNull()
		|| _preparedCoverRequest != request
		|| _preparedCoverState < state) {
		_preparedCoverRequest = request;
		_preparedCoverState = state;
		if (state == ThumbState::Cover) {
			_preparedCoverStorage = Streaming::PrepareByRequest(
				_instance.info().video.cover,
				false,
				_instance.info().video.rotation,
				request,
				std::move(_preparedCoverStorage));
		} else if (!request.resize.isEmpty()) {
			using Option = Images::Option;
			const auto options = Option::Smooth
				| (good ? Option(0) : Option::Blurred)
				| Option::RoundedLarge
				| ((request.corners & RectPart::TopLeft)
					? Option::RoundedTopLeft
					: Option(0))
				| ((request.corners & RectPart::TopRight)
					? Option::RoundedTopRight
					: Option(0))
				| ((request.corners & RectPart::BottomRight)
					? Option::RoundedBottomRight
					: Option(0))
				| ((request.corners & RectPart::BottomLeft)
					? Option::RoundedBottomLeft
					: Option(0));
			_preparedCoverStorage = (good
				? good
				: thumb
				? thumb
				: blurred
				? blurred
				: Image::BlankMedia().get())->pixNoCache(
					request.resize.width(),
					request.resize.height(),
					options,
					request.outer.width(),
					request.outer.height()).toImage();
		}
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

void Pip::paintRadialLoading(QPainter &p) const {
	const auto inner = countRadialRect();
#ifdef USE_OPENGL_OVERLAY_WIDGET
	{
		if (_radialCache.size() != inner.size() * cIntRetinaFactor()) {
			_radialCache = QImage(
				inner.size() * cIntRetinaFactor(),
				QImage::Format_ARGB32_Premultiplied);
			_radialCache.setDevicePixelRatio(cRetinaFactor());
		}
		_radialCache.fill(Qt::transparent);

		Painter q(&_radialCache);
		paintRadialLoadingContent(q, inner.translated(-inner.topLeft()));
	}
	p.drawImage(inner.topLeft(), _radialCache);
#else // USE_OPENGL_OVERLAY_WIDGET
	paintRadialLoadingContent(p, inner);
#endif // USE_OPENGL_OVERLAY_WIDGET
}

void Pip::paintRadialLoadingContent(QPainter &p, const QRect &inner) const {
	if (!_instance.waitingShown()) {
		return;
	}
	const auto arc = inner.marginsRemoved(QMargins(
		st::radialLine,
		st::radialLine,
		st::radialLine,
		st::radialLine));
	p.setOpacity(_instance.waitingOpacity());
	p.setPen(Qt::NoPen);
	p.setBrush(st::radialBg);
	{
		PainterHighQualityEnabler hq(p);
		p.drawEllipse(inner);
	}
	p.setOpacity(1.);
	Ui::InfiniteRadialAnimation::Draw(
		p,
		_instance.waitingState(),
		arc.topLeft(),
		arc.size(),
		_panel.width(),
		st::radialFg,
		st::radialLine);
}

QRect Pip::countRadialRect() const {
	const auto outer = _panel.inner();
	return {
		outer.x() + (outer.width() - st::radialSize.width()) / 2,
		outer.y() + (outer.height() - st::radialSize.height()) / 2,
		st::radialSize.width(),
		st::radialSize.height()
	};
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
	_panel.update(countRadialRect());
}

} // namespace View
} // namespace Media
