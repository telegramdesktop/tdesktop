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
#include "media/view/media_view_pip_opengl.h"
#include "media/view/media_view_pip_raster.h"
#include "media/audio/media_audio.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_file_origin.h"
#include "data/data_session.h"
#include "data/data_media_rotation.h"
#include "main/main_account.h"
#include "main/main_session.h"
#include "core/application.h"
#include "base/platform/base_platform_info.h"
#include "base/power_save_blocker.h"
#include "ui/platform/ui_platform_utility.h"
#include "ui/platform/ui_platform_window_title.h"
#include "ui/widgets/buttons.h"
#include "ui/wrap/fade_wrap.h"
#include "ui/widgets/shadow.h"
#include "ui/text/format_values.h"
#include "ui/gl/gl_surface.h"
#include "ui/painter.h"
#include "ui/ui_utility.h"
#include "window/window_controller.h"
#include "styles/style_widgets.h"
#include "styles/style_window.h"
#include "styles/style_media_view.h"

#include <QtGui/QWindow>
#include <QtGui/QScreen>
#include <QtWidgets/QApplication>

namespace Media {
namespace View {
namespace {

constexpr auto kPipLoaderPriority = 2;
constexpr auto kMsInSecond = 1000;

[[nodiscard]] QRect ScreenFromPosition(QPoint point) {
	const auto screen = QGuiApplication::screenAt(point);
	const auto use = screen ? screen : QGuiApplication::primaryScreen();
	return use
		? use->availableGeometry()
		: QRect(0, 0, st::windowDefaultWidth, st::windowDefaultHeight);
}

[[nodiscard]] QSize MaxAllowedSizeForScreen(QSize screenSize) {
	// Each side should be less than screen side - 3 * st::pipBorderSkip,
	// That way it won't try to snap to both opposite sides of the screen.
	const auto skip = 3 * st::pipBorderSkip;
	return { screenSize.width() - skip, screenSize.height() - skip };
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
	return !(rotation % 180);
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
	Fn<Ui::GL::ChosenRenderer(Ui::GL::Capabilities)> renderer)
: _content(Ui::GL::CreateSurface(std::move(renderer)))
, _parent(parent) {
}

void PipPanel::init() {
	widget()->setWindowFlags(Qt::Tool
		| Qt::WindowStaysOnTopHint
		| Qt::FramelessWindowHint
		| Qt::WindowDoesNotAcceptFocus);
	widget()->setAttribute(Qt::WA_ShowWithoutActivating);
	widget()->setAttribute(Qt::WA_MacAlwaysShowToolWindow);
	widget()->setAttribute(Qt::WA_NoSystemBackground);
	widget()->setAttribute(Qt::WA_TranslucentBackground);
	Ui::Platform::IgnoreAllActivation(widget());
	Ui::Platform::InitOnTopPanel(widget());
	widget()->setMouseTracking(true);
	widget()->resize(0, 0);
	widget()->hide();

	rpl::merge(
		rp()->shownValue() | rpl::to_empty,
		rp()->paintRequest() | rpl::to_empty
	) | rpl::map([=] {
		return widget()->windowHandle()
			&& widget()->windowHandle()->isExposed();
	}) | rpl::distinct_until_changed(
	) | rpl::filter(rpl::mappers::_1) | rpl::start_with_next([=] {
		// Workaround Qt's forced transient parent.
		Ui::Platform::ClearTransientParent(widget());
	}, rp()->lifetime());

	rp()->shownValue(
	) | rpl::filter(rpl::mappers::_1) | rpl::start_with_next([=] {
		Ui::Platform::SetWindowMargins(widget(), _padding);
	}, rp()->lifetime());

	rp()->screenValue(
	) | rpl::skip(1) | rpl::start_with_next([=](not_null<QScreen*> screen) {
		handleScreenChanged(screen);
	}, rp()->lifetime());

	if (Platform::IsWayland()) {
		rp()->sizeValue(
		) | rpl::skip(1) | rpl::start_with_next([=](QSize size) {
			handleWaylandResize(size);
		}, rp()->lifetime());
	}
}

not_null<QWidget*> PipPanel::widget() const {
	return _content->rpWidget();
}

not_null<Ui::RpWidgetWrap*> PipPanel::rp() const {
	return _content.get();
}

void PipPanel::setAspectRatio(QSize ratio) {
	if (_ratio == ratio) {
		return;
	}
	_ratio = ratio;
	if (_ratio.isEmpty()) {
		_ratio = QSize(1, 1);
	}
	Ui::Platform::DisableSystemWindowResize(widget(), _ratio);
	if (!widget()->size().isEmpty()) {
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
	return widget()->rect().marginsRemoved(_padding);
}

RectParts PipPanel::attached() const {
	return _attached;
}

bool PipPanel::useTransparency() const {
	return _useTransparency;
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
	return widget()->screen();
}

PipPanel::Position PipPanel::countPosition() const {
	const auto screen = myScreen();
	if (!screen) {
		return Position();
	}
	auto result = Position();
	result.screen = screen->geometry();
	result.geometry = widget()->geometry().marginsRemoved(_padding);
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
	const auto parentScreen = _parent ? _parent->screen() : nullptr;
	const auto myScreen = widget()->screen();
	if (parentScreen && myScreen != parentScreen) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
		widget()->setScreen(parentScreen);
#else // Qt >= 6.0.0
		widget()->windowHandle()->setScreen(parentScreen);
#endif // Qt < 6.0.0
	}
	auto position = Position();
	position.snapped = RectPart::Top | RectPart::Left;
	position.screen = parentScreen->geometry();
	position.geometry = QRect(0, 0, st::pipDefaultSize, st::pipDefaultSize);
	setPositionOnScreen(position, parentScreen->availableGeometry());
}

void PipPanel::setPositionOnScreen(Position position, QRect available) {
	const auto screen = available;
	const auto requestedSize = position.geometry.size();
	const auto max = std::max(requestedSize.width(), requestedSize.height());

	// Apply aspect ratio.
	const auto scaled = (_ratio.width() > _ratio.height())
		? QSize(max, max * _ratio.height() / _ratio.width())
		: QSize(max * _ratio.width() / _ratio.height(), max);

	// Apply maximum size.
	const auto fit = MaxAllowedSizeForScreen(screen.size());
	const auto byWidth = (scaled.width() * fit.height())
		> (scaled.height() * fit.width());
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
	const auto maximalSize = byWidth
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
	widget()->setMinimumSize(minimalSize);
	widget()->setMaximumSize(
		std::max(minimalSize.width(), maximalSize.width()),
		std::max(minimalSize.height(), maximalSize.height()));
	updateDecorations();
}

void PipPanel::update() {
	widget()->update();
}

void PipPanel::setGeometry(QRect geometry) {
	widget()->setGeometry(geometry);
}

void PipPanel::handleWaylandResize(QSize size) {
	if (_inHandleWaylandResize) {
		return;
	}
	_inHandleWaylandResize = true;

	// Apply aspect ratio.
	const auto max = std::max(size.width(), size.height());
	const auto scaled = (_ratio.width() > _ratio.height())
		? QSize(max, max * _ratio.height() / _ratio.width())
		: QSize(max * _ratio.width() / _ratio.height(), max);

	// Buffer can't be bigger than the configured
	// (suggested by compositor) size.
	const auto byWidth = (scaled.width() * size.height())
		> (scaled.height() * size.width());
	const auto normalized = (byWidth && scaled.width() > size.width())
		? QSize(size.width(), size.width() * scaled.height() / scaled.width())
		: (!byWidth && scaled.height() > size.height())
		? QSize(
			size.height() * scaled.width() / scaled.height(),
			size.height())
		: scaled;

	widget()->resize(normalized);
	QResizeEvent e(normalized, size);
	QCoreApplication::sendEvent(widget()->windowHandle(), &e);
	_inHandleWaylandResize = false;
}

void PipPanel::handleScreenChanged(not_null<QScreen*> screen) {
	const auto screenGeometry = screen->availableGeometry();
	const auto minimalSize = _ratio.scaled(
		st::pipMinimalSize,
		st::pipMinimalSize,
		Qt::KeepAspectRatioByExpanding);
	const auto maximalSize = _ratio.scaled(
		MaxAllowedSizeForScreen(screenGeometry.size()),
		Qt::KeepAspectRatio);
	widget()->setMinimumSize(minimalSize);
	widget()->setMaximumSize(
		std::max(minimalSize.width(), maximalSize.width()),
		std::max(minimalSize.height(), maximalSize.height()));
}

void PipPanel::handleMousePress(QPoint position, Qt::MouseButton button) {
	if (button != Qt::LeftButton) {
		return;
	}
	updateOverState(position);
	_pressState = _overState;
	_pressPoint = QCursor::pos();
}

void PipPanel::handleMouseRelease(QPoint position, Qt::MouseButton button) {
	if (button != Qt::LeftButton || !base::take(_pressState)) {
		return;
	} else if (base::take(_dragState)) {
		finishDrag(QCursor::pos());
		updateOverState(position);
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
	const auto width = widget()->width();
	const auto height = widget()->height();
	const auto overState = [&] {
		if (point.x() < left) {
			if (point.y() < top) {
				return RectPart::TopLeft;
			} else if (point.y() >= height - bottom) {
				return RectPart::BottomLeft;
			} else {
				return RectPart::Left;
			}
		} else if (point.x() >= width - right) {
			if (point.y() < top) {
				return RectPart::TopRight;
			} else if (point.y() >= height - bottom) {
				return RectPart::BottomRight;
			} else {
				return RectPart::Right;
			}
		} else if (point.y() < top) {
			return RectPart::Top;
		} else if (point.y() >= height - bottom) {
			return RectPart::Bottom;
		} else {
			return RectPart::Center;
		}
	}();
	if (_overState != overState) {
		_overState = overState;
		widget()->setCursor([&] {
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

void PipPanel::handleMouseMove(QPoint position) {
	if (!_pressState) {
		updateOverState(position);
		return;
	}
	const auto point = QCursor::pos();
	const auto distance = QApplication::startDragDistance();
	if (!_dragState
		&& (point - _pressPoint).manhattanLength() > distance
		&& !_dragDisabled) {
		_dragState = _pressState;
		updateDecorations();
		_dragStartGeometry = widget()->geometry().marginsRemoved(_padding);
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
		widget()->windowHandle()->startSystemResize(stateEdges);
	} else {
		widget()->windowHandle()->startSystemMove();
	}

	Ui::SendSynteticMouseEvent(
		widget().get(),
		QEvent::MouseButtonRelease,
		Qt::LeftButton);
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
		MaxAllowedSizeForScreen(screen.size()),
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
	widget()->setMinimumSize(minimalSize);
	widget()->setMaximumSize(
		std::max(minimalSize.width(), maximalSize.width()),
		std::max(minimalSize.height(), maximalSize.height()));
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
	const auto inner = widget()->geometry().marginsRemoved(_padding);
	const auto position = widget()->pos();
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
		widget()->move(_positionAnimationTo
			- QPoint(_padding.left(), _padding.top()));
		if (!_dragState) {
			updateDecorations();
		}
		return;
	}
	const auto from = QPointF(_positionAnimationFrom);
	const auto to = QPointF(_positionAnimationTo);
	widget()->move((from + (to - from) * progress).toPoint()
		- QPoint(_padding.left(), _padding.top()));
}

void PipPanel::moveAnimated(QPoint to) {
	if (_positionAnimation.animating() && _positionAnimationTo == to) {
		return;
	}
	_positionAnimationTo = to;
	_positionAnimationFrom = widget()->pos()
		+ QPoint(_padding.left(), _padding.top());
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
	const auto use = Ui::Platform::TranslucentWindowsSupported();
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
	widget()->setAttribute(Qt::WA_OpaquePaintEvent, !_useTransparency);
	if (widget()->windowHandle()) {
		Ui::Platform::SetWindowMargins(widget(), _padding);
	}
	setGeometry(newGeometry);
	update();
}

Pip::Pip(
	not_null<Delegate*> delegate,
	not_null<DocumentData*> data,
	Data::FileOrigin origin,
	not_null<DocumentData*> chosenQuality,
	HistoryItem *context,
	VideoQuality quality,
	std::shared_ptr<Streaming::Document> shared,
	FnMut<void()> closeAndContinue,
	FnMut<void()> destroy)
: _delegate(delegate)
, _data(data)
, _origin(origin)
, _chosenQuality(chosenQuality)
, _context(context)
, _quality(quality)
, _instance(
	std::in_place,
	std::move(shared),
	[=] { waitingAnimationCallback(); })
, _panel(
	_delegate->pipParentWidget(),
	[=](Ui::GL::Capabilities capabilities) {
		return chooseRenderer(capabilities);
	})
, _playbackProgress(std::make_unique<PlaybackProgress>())
, _dataMedia(_data->createMediaView())
, _rotation(data->owner().mediaRotation().get(data))
, _lastPositiveVolume((Core::App().settings().videoVolume() > 0.)
	? Core::App().settings().videoVolume()
	: Core::Settings::kDefaultVolume)
, _closeAndContinue(std::move(closeAndContinue))
, _destroy(std::move(destroy)) {
	setupPanel();
	setupButtons();
	setupStreaming();

	_data->session().account().sessionChanges(
	) | rpl::start_with_next([=] {
		_destroy();
	}, _panel.rp()->lifetime());

	if (_context) {
		_data->owner().itemRemoved(
		) | rpl::start_with_next([=](not_null<const HistoryItem*> data) {
			if (_context != data) {
				_context = nullptr;
			}
		}, _panel.rp()->lifetime());
	}
}

Pip::~Pip() = default;

std::shared_ptr<Streaming::Document> Pip::shared() const {
	return _instance->shared();
}

void Pip::setupPanel() {
	_panel.init();
	const auto size = [&] {
		if (!_instance->info().video.size.isEmpty()) {
			return _instance->info().video.size;
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
	_panel.widget()->show();

	_panel.saveGeometryRequests(
	) | rpl::start_with_next([=] {
		saveGeometry();
	}, _panel.rp()->lifetime());

	_panel.rp()->events(
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
	}, _panel.rp()->lifetime());
}

void Pip::handleClose() {
	crl::on_main(_panel.widget(), [=] {
		_destroy();
	});
}

void Pip::handleLeave() {
	setOverState(OverState::None);
}

void Pip::handleMouseMove(QPoint position) {
	const auto weak = base::make_weak(_panel.widget());
	const auto guard = gsl::finally([&] {
		if (weak) {
			_panel.handleMouseMove(position);
		}
	});
	setOverState(computeState(position));
	seekUpdate(position);
	volumeControllerUpdate(position);
}

void Pip::setOverState(OverState state) {
	if (_over == state) {
		return;
	}
	const auto wasShown = ResolveShownOver(_over);
	_over = state;
	const auto nowAreShown = (ResolveShownOver(_over) != OverState::None);
	if ((wasShown != OverState::None) != nowAreShown) {
		_controlsShown.start(
			[=] { _panel.update(); },
			nowAreShown ? 0. : 1.,
			nowAreShown ? 1. : 0.,
			st::fadeWrapDuration,
			anim::linear);
	}
	if (!_pressed) {
		updateActiveState(wasShown);
	}
	_panel.update();
}

void Pip::setPressedState(std::optional<OverState> state) {
	if (_pressed == state) {
		return;
	}
	const auto wasShown = shownActiveState();
	_pressed = state;
	updateActiveState(wasShown);
}

Pip::OverState Pip::shownActiveState() const {
	return ResolveShownOver(_pressed.value_or(_over));
}

float64 Pip::activeValue(const Button &button) const {
	const auto shownState = ResolveShownOver(button.state);
	return button.active.value((shownActiveState() == shownState) ? 1. : 0.);
}

void Pip::updateActiveState(OverState wasShown) {
	const auto check = [&](Button &button) {
		const auto shownState = ResolveShownOver(button.state);
		const auto nowIsShown = (shownActiveState() == shownState);
		if ((wasShown == shownState) != nowIsShown) {
			button.active.start(
				[=, &button] { _panel.widget()->update(button.icon); },
				nowIsShown ? 0. : 1.,
				nowIsShown ? 1. : 0.,
				st::fadeWrapDuration,
				anim::linear);
		}
	};
	check(_close);
	check(_enlarge);
	check(_play);
	check(_playback);
	check(_volumeToggle);
	check(_volumeController);
}

Pip::OverState Pip::ResolveShownOver(OverState state) {
	return (state == OverState::VolumeController)
		? OverState::VolumeToggle
		: state;
}

void Pip::handleMousePress(QPoint position, Qt::MouseButton button) {
	const auto weak = base::make_weak(_panel.widget());
	const auto guard = gsl::finally([&] {
		if (weak) {
			_panel.handleMousePress(position, button);
		}
	});
	if (button != Qt::LeftButton) {
		return;
	}
	_pressed = _over;
	if (_over == OverState::Playback || _over == OverState::VolumeController) {
		_panel.setDragDisabled(true);
	}
	seekUpdate(position);
	volumeControllerUpdate(position);
}

void Pip::handleMouseRelease(QPoint position, Qt::MouseButton button) {
	const auto weak = base::make_weak(_panel.widget());
	const auto guard = gsl::finally([&] {
		if (weak) {
			_panel.handleMouseRelease(position, button);
		}
	});
	if (button != Qt::LeftButton) {
		return;
	}
	seekUpdate(position);

	volumeControllerUpdate(position);

	const auto pressed = base::take(_pressed);
	if (pressed && *pressed == OverState::Playback) {
		_panel.setDragDisabled(false);
		seekFinish(_playbackProgress->value());
	} else if (pressed && *pressed == OverState::VolumeController) {
		_panel.setDragDisabled(false);
		_panel.update();
	} else if (_panel.dragging() || !pressed || *pressed != _over) {
		_lastHandledPress = std::nullopt;
	} else {
		_lastHandledPress = _over;
		switch (_over) {
		case OverState::Close: _panel.widget()->close(); break;
		case OverState::Enlarge: _closeAndContinue(); break;
		case OverState::VolumeToggle: volumeToggled(); break;
		case OverState::Other: playbackPauseResume(); break;
		}
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
		if (!_instance->player().paused()
			&& !_instance->player().finished()) {
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
	_startPaused = !_pausedBySeek && !_instance->player().finished();
	restartAtSeekPosition(positionMs);
}

void Pip::volumeChanged(float64 volume) {
	if (volume > 0.) {
		_lastPositiveVolume = volume;
	}
	Player::mixer()->setVideoVolume(volume);
	Core::App().settings().setVideoVolume(volume);
	Core::App().saveSettingsDelayed();
}

void Pip::volumeToggled() {
	const auto volume = Core::App().settings().videoVolume();
	volumeChanged(volume ? 0. : _lastPositiveVolume);
	_panel.update();
}

void Pip::volumeControllerUpdate(QPoint position) {
	if (!_pressed || *_pressed != OverState::VolumeController) {
		return;
	}
	const auto unbound = (position.x() - _volumeController.icon.x())
		/ float64(_volumeController.icon.width());
	const auto value = std::clamp(unbound, 0., 1.);
	volumeChanged(value);
	_panel.update();
}

void Pip::setupButtons() {
	_close.state = OverState::Close;
	_enlarge.state = OverState::Enlarge;
	_playback.state = OverState::Playback;
	_volumeToggle.state = OverState::VolumeToggle;
	_volumeController.state = OverState::VolumeController;
	_play.state = OverState::Other;
	_panel.rp()->sizeValue(
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

		const auto volumeSkip = st::pipPlaybackSkip;
		const auto volumeHeight = 2 * volumeSkip + st::pipPlaybackWide;
		const auto volumeToggleWidth = st::pipVolumeIcon0.width()
			+ 2 * skip;
		const auto volumeToggleHeight = st::pipVolumeIcon0.height()
			+ 2 * skip;
		const auto volumeWidth = (((st::mediaviewVolumeWidth + 2 * skip)
			+ _close.area.width()
			+ _enlarge.area.width()
			+ volumeToggleWidth) < rect.width())
				? st::mediaviewVolumeWidth
				: 0;
		_volumeController.area = QRect(
			rect.x() + rect.width() - volumeWidth - 2 * volumeSkip,
			rect.y() + (volumeToggleHeight - volumeHeight) / 2,
			volumeWidth,
			volumeHeight);
		_volumeToggle.area = QRect(
			_volumeController.area.x()
				- st::pipVolumeIcon0.width()
				- skip,
			rect.y(),
			volumeToggleWidth,
			volumeToggleHeight);
		using Ui::Platform::TitleControlsLayout;
		if (!TitleControlsLayout::Instance()->current().onLeft()) {
			_close.area.moveLeft(rect.x()
				+ rect.width()
				- (_close.area.x() - rect.x())
				- _close.area.width());
			_enlarge.area.moveLeft(rect.x()
				+ rect.width()
				- (_enlarge.area.x() - rect.x())
				- _enlarge.area.width());
			_volumeToggle.area.moveLeft(rect.x());
			_volumeController.area.moveLeft(_volumeToggle.area.x()
				+ _volumeToggle.area.width());
		}
		_close.icon = _close.area.marginsRemoved({ skip, skip, skip, skip });
		_enlarge.icon = _enlarge.area.marginsRemoved(
			{ skip, skip, skip, skip });
		_volumeToggle.icon = _volumeToggle.area.marginsRemoved(
			{ skip, skip, skip, skip });
		_play.icon = QRect(
			rect.x() + (rect.width() - st::pipPlayIcon.width()) / 2,
			rect.y() + (rect.height() - st::pipPlayIcon.height()) / 2,
			st::pipPlayIcon.width(),
			st::pipPlayIcon.height());
		const auto volumeArea = _volumeController.area;
		_volumeController.icon = (volumeArea.width() > 2 * volumeSkip
			&& volumeArea.height() > 2 * volumeSkip)
			? volumeArea.marginsRemoved(
				{ volumeSkip, volumeSkip, volumeSkip, volumeSkip })
			: QRect();
		const auto playbackSkip = st::pipPlaybackSkip;
		const auto playbackHeight = 2 * playbackSkip + st::pipPlaybackWide;
		_playback.area = QRect(
			rect.x(),
			rect.y() + rect.height() - playbackHeight,
			rect.width(),
			playbackHeight);
		_playback.icon = _playback.area.marginsRemoved(
			{ playbackSkip, playbackSkip, playbackSkip, playbackSkip });
	}, _panel.rp()->lifetime());

	_playbackProgress->setValueChangedCallback([=](
			float64 value,
			float64 receivedTill) {
		_panel.widget()->update(_playback.area);
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
	_instance->setPriority(kPipLoaderPriority);
	_instance->lockPlayer();

	_instance->switchQualityRequests(
	) | rpl::filter([=](int quality) {
		return !_quality.manual && _quality.height != quality;
	}) | rpl::start_with_next([=](int quality) {
		applyVideoQuality({
			.manual = 0,
			.height = uint32(quality),
		});
	}, _instance->lifetime());

	_instance->player().updates(
	) | rpl::start_with_next_error([=](Streaming::Update &&update) {
		handleStreamingUpdate(std::move(update));
	}, [=](Streaming::Error &&error) {
		handleStreamingError(std::move(error));
	}, _instance->lifetime());
	updatePlaybackState();
}

void Pip::applyVideoQuality(VideoQuality value) {
	if (_quality == value
		|| !_dataMedia->canBePlayed(_context)) {
		return;
	}
	const auto resolved = _data->chooseQuality(_context, value);
	if (_chosenQuality == resolved) {
		return;
	}
	auto instance = Streaming::Instance(
		resolved,
		_data,
		_context,
		_origin,
		[=] { waitingAnimationCallback(); });
	if (!instance.valid()) {
		return;
	}

	if (_instance->ready()) {
		_qualityChangeFrame = currentVideoFrameImage();
	}
	if (!_instance->player().active()
		|| _instance->player().finished()) {
		_qualityChangeFinished = true;
	}
	_startPaused = _qualityChangeFinished || _instance->player().paused();

	_quality = value;
	Core::App().settings().setVideoQuality(value);
	Core::App().saveSettingsDelayed();
	_chosenQuality = resolved;
	_instance.emplace(std::move(instance));
	setupStreaming();
	restartAtSeekPosition(_lastUpdatePosition);
}

QImage Pip::currentVideoFrameImage() const {
	return _instance->player().ready()
		? _instance->player().currentFrameImage()
		: _instance->info().video.cover;
}

Ui::GL::ChosenRenderer Pip::chooseRenderer(
		Ui::GL::Capabilities capabilities) {
	const auto use = Platform::IsMac()
		? true
		: capabilities.transparency;
	LOG(("OpenGL: %1 (PipPanel)").arg(Logs::b(use)));
	if (use) {
		_opengl = true;
		return {
			.renderer = std::make_unique<RendererGL>(this),
			.backend = Ui::GL::Backend::OpenGL,
		};
	}
	return {
		.renderer = std::make_unique<RendererSW>(this),
		.backend = Ui::GL::Backend::Raster,
	};
}

void Pip::paint(not_null<Renderer*> renderer) const {
	const auto controlsShown = _controlsShown.value(
		(_over != OverState::None) ? 1. : 0.);
	auto geometry = ContentGeometry{
		.inner = _panel.inner(),
		.attached = (_panel.useTransparency()
			? _panel.attached()
			: RectPart::AllSides),
		.fade = controlsShown,
		.outer = _panel.widget()->size(),
		.rotation = _rotation,
		.videoRotation = _instance->info().video.rotation,
		.useTransparency = _panel.useTransparency(),
	};
	if (canUseVideoFrame()) {
		renderer->paintTransformedVideoFrame(geometry);
		_instance->markFrameShown();
	} else {
		const auto content = staticContent();
		if (_preparedCoverState == ThumbState::Cover) {
			geometry.rotation += base::take(geometry.videoRotation);
		}
		renderer->paintTransformedStaticContent(content, geometry);
	}
	if (_instance->waitingShown()) {
		renderer->paintRadialLoading(countRadialRect(), controlsShown);
	}
	if (controlsShown > 0) {
		paintButtons(renderer, controlsShown);
		paintPlayback(renderer, controlsShown);
		paintVolumeController(renderer, controlsShown);
	}
}

void Pip::paintButtons(not_null<Renderer*> renderer, float64 shown) const {
	const auto outer = _panel.widget()->width();
	const auto drawOne = [&](
			const Button &button,
			const style::icon &icon,
			const style::icon &iconOver) {
		renderer->paintButton(
			button,
			outer,
			shown,
			activeValue(button),
			icon,
			iconOver);
	};

	renderer->paintButtonsStart();
	drawOne(
		_play,
		_showPause ? st::pipPauseIcon : st::pipPlayIcon,
		_showPause ? st::pipPauseIconOver : st::pipPlayIconOver);
	drawOne(_close, st::pipCloseIcon, st::pipCloseIconOver);
	drawOne(_enlarge, st::pipEnlargeIcon, st::pipEnlargeIconOver);
	const auto volume = Core::App().settings().videoVolume();
	if (volume <= 0.) {
		drawOne(
			_volumeToggle,
			st::pipVolumeIcon0,
			st::pipVolumeIcon0Over);
	} else if (volume < 1 / 2.) {
		drawOne(
			_volumeToggle,
			st::pipVolumeIcon1,
			st::pipVolumeIcon1Over);
	} else {
		drawOne(
			_volumeToggle,
			st::pipVolumeIcon2,
			st::pipVolumeIcon2Over);
	}
}

void Pip::paintPlayback(not_null<Renderer*> renderer, float64 shown) const {
	const auto outer = QRect(
		_playback.icon.x(),
		_playback.icon.y() - st::pipPlaybackFont->height,
		_playback.icon.width(),
		st::pipPlaybackFont->height + _playback.icon.height());
	renderer->paintPlayback(outer, shown);
}

void Pip::paintPlaybackContent(
		QPainter &p,
		QRect outer,
		float64 shown) const {
	p.setOpacity(shown);
	paintPlaybackProgress(p, outer);
	paintPlaybackTexts(p, outer);
}

void Pip::paintPlaybackProgress(QPainter &p, QRect outer) const {
	const auto radius = _playback.icon.height() / 2;
	const auto progress = _playbackProgress->value();
	const auto active = activeValue(_playback);
	const auto height = anim::interpolate(
		st::pipPlaybackWidth,
		_playback.icon.height(),
		active);
	const auto rect = QRect(
		outer.x(),
		(outer.y()
			+ st::pipPlaybackFont->height
			+ _playback.icon.height()
			- height),
		outer.width(),
		height);

	paintProgressBar(p, rect, progress, radius, active);
}

void Pip::paintProgressBar(
		QPainter &p,
		const QRect &rect,
		float64 progress,
		int radius,
		float64 active) const {
	const auto done = int(base::SafeRound(rect.width() * progress));
	PainterHighQualityEnabler hq(p);
	p.setPen(Qt::NoPen);
	if (done > 0) {
		p.setBrush(anim::brush(
			st::mediaviewPipControlsFg,
			st::mediaviewPipPlaybackActive,
			active));
		p.setClipRect(rect.x(), rect.y(), done, rect.height());
		p.drawRoundedRect(
			rect.x(),
			rect.y(),
			std::min(done + radius, rect.width()),
			rect.height(),
			radius,
			radius);
	}
	if (done < rect.width()) {
		const auto from = std::max(rect.x() + done - radius, rect.x());
		p.setBrush(st::mediaviewPipPlaybackInactive);
		p.setClipRect(
			rect.x() + done,
			rect.y(),
			rect.width() - done,
			rect.height());
		p.drawRoundedRect(
			from,
			rect.y(),
			rect.x() + rect.width() - from,
			rect.height(),
			radius,
			radius);
	}
	p.setClipping(false);
}

void Pip::paintPlaybackTexts(QPainter &p, QRect outer) const {
	const auto left = outer.x()
		- _playback.icon.x()
		+ _playback.area.x()
		+ st::pipPlaybackTextSkip;
	const auto right = outer.x()
		- _playback.icon.x()
		+ _playback.area.x()
		+ _playback.area.width()
		- st::pipPlaybackTextSkip;
	const auto top = outer.y() + st::pipPlaybackFont->ascent;

	p.setFont(st::pipPlaybackFont);
	p.setPen(st::mediaviewPipControlsFgOver);
	p.drawText(left, top, _timeAlready);
	p.drawText(right - _timeLeftWidth, top, _timeLeft);
}

void Pip::paintVolumeController(
		not_null<Renderer*> renderer,
		float64 shown) const {
	if (_volumeController.icon.isEmpty()) {
		return;
	}
	renderer->paintVolumeController(_volumeController.icon, shown);
}

void Pip::paintVolumeControllerContent(
		QPainter &p,
		QRect outer,
		float64 shown) const {
	p.setOpacity(shown);

	const auto radius = _volumeController.icon.height() / 2;
	const auto volume = Core::App().settings().videoVolume();
	const auto active = activeValue(_volumeController);
	const auto height = anim::interpolate(
		st::pipPlaybackWidth,
		_volumeController.icon.height(),
		active);
	const auto rect = QRect(
		outer.x(),
		outer.y() + radius - height / 2,
		outer.width(),
		height);

	paintProgressBar(p, rect, volume, radius, active);
}

void Pip::handleStreamingUpdate(Streaming::Update &&update) {
	using namespace Streaming;

	v::match(update.data, [&](const Information &update) {
		_panel.setAspectRatio(
			FlipSizeByRotation(update.video.size, _rotation));
		_qualityChangeFrame = QImage();
	}, [&](PreloadedVideo) {
		updatePlaybackState();
	}, [&](UpdateVideo update) {
		_panel.update();
		Core::App().updateNonIdle();
		updatePlaybackState();
		_lastUpdatePosition = update.position;
	}, [&](PreloadedAudio) {
		updatePlaybackState();
	}, [&](UpdateAudio) {
		updatePlaybackState();
	}, [](WaitingForData) {
	}, [](SpeedEstimate) {
	}, [](MutedByOther) {
	}, [&](Finished) {
		updatePlaybackState();
	});
}

void Pip::updatePlaybackState() {
	const auto state = _instance->player().prepareLegacyState();
	updatePlayPauseResumeState(state);
	if (state.position == kTimeUnknown
		|| state.length == kTimeUnknown
		|| _pausedBySeek) {
		return;
	}
	_playbackProgress->updateState(state);
	updatePowerSaveBlocker(state);

	qint64 position = 0;
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

void Pip::updatePowerSaveBlocker(const Player::TrackState &state) {
	const auto block = _data->isVideoFile()
		&& !IsPausedOrPausing(state.state)
		&& !IsStoppedOrStopping(state.state);
	base::UpdatePowerSaveBlocker(
		_powerSaveBlocker,
		block,
		base::PowerSaveBlockType::PreventDisplaySleep,
		[] { return u"Video playback is active"_q; },
		[=] { return _panel.widget()->windowHandle(); });
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
	_panel.widget()->update(QRect(
		_playback.area.x(),
		_playback.icon.y() - st::pipPlaybackFont->height,
		_playback.area.width(),
		st::pipPlaybackFont->height));
}

void Pip::handleStreamingError(Streaming::Error &&error) {
	_panel.widget()->close();
}

void Pip::playbackPauseResume() {
	if (_instance->player().failed()) {
		_panel.widget()->close();
	} else if (_instance->player().finished()
		|| !_instance->player().active()) {
		_startPaused = false;
		restartAtSeekPosition(0);
	} else if (_instance->player().paused()) {
		_instance->resume();
		updatePlaybackState();
	} else {
		_instance->pause();
		updatePlaybackState();
	}
}

void Pip::restartAtSeekPosition(crl::time position) {
	_lastUpdatePosition = position;

	if (!_instance->info().video.cover.isNull()) {
		_preparedCoverStorage = QImage();
		_preparedCoverState = ThumbState::Empty;
		_instance->saveFrameToCover();
	}

	auto options = Streaming::PlaybackOptions();
	options.position = position;
	options.hwAllowed = Core::App().settings().hardwareAcceleratedVideo();
	options.audioId = _instance->player().prepareLegacyState().id;
	options.speed = _delegate->pipPlaybackSpeed();

	_instance->play(options);
	if (_startPaused) {
		_instance->pause();
	}
	_pausedBySeek = false;
	updatePlaybackState();
}

bool Pip::canUseVideoFrame() const {
	return _instance->player().ready()
		&& !_instance->info().video.cover.isNull();
}

QImage Pip::videoFrame(const FrameRequest &request) const {
	Expects(canUseVideoFrame());

	return _instance->frame(request);
}

Streaming::FrameWithInfo Pip::videoFrameWithInfo() const {
	Expects(canUseVideoFrame());

	return _instance->frameWithInfo();
}

QImage Pip::staticContent() const {
	const auto &cover = !_qualityChangeFrame.isNull()
		? _qualityChangeFrame
		: _instance->info().video.cover;
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
	if (!_preparedCoverStorage.isNull() && _preparedCoverState >= state) {
		return _preparedCoverStorage;
	}
	_preparedCoverState = state;
	if (state == ThumbState::Cover) {
		_preparedCoverStorage = cover;
	} else {
		_preparedCoverStorage = (good
			? good
			: thumb
			? thumb
			: blurred
			? blurred
			: Image::BlankMedia().get())->original();
		if (!good) {
			_preparedCoverStorage = Images::Blur(
				std::move(_preparedCoverStorage));
		}
	}
	return _preparedCoverStorage;
}

void Pip::paintRadialLoadingContent(
		QPainter &p,
		const QRect &inner,
		QColor fg) const {
	const auto arc = inner.marginsRemoved(QMargins(
		st::radialLine,
		st::radialLine,
		st::radialLine,
		st::radialLine));
	p.setOpacity(_instance->waitingOpacity());
	p.setPen(Qt::NoPen);
	p.setBrush(st::radialBg);
	{
		PainterHighQualityEnabler hq(p);
		p.drawEllipse(inner);
	}
	p.setOpacity(1.);
	Ui::InfiniteRadialAnimation::Draw(
		p,
		_instance->waitingState(),
		arc.topLeft(),
		arc.size(),
		_panel.widget()->width(),
		fg,
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
	} else if (_volumeToggle.area.contains(position)) {
		return OverState::VolumeToggle;
	} else if (_volumeController.area.contains(position)) {
		return OverState::VolumeController;
	} else {
		return OverState::Other;
	}
}

void Pip::waitingAnimationCallback() {
	_panel.widget()->update(countRadialRect());
}

} // namespace View
} // namespace Media
