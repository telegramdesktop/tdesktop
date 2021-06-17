/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "window/window_title_qt.h"

#include "ui/platform/ui_platform_utility.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/shadow.h"
#include "styles/style_widgets.h"
#include "styles/style_window.h"

#include <QtCore/QCoreApplication>
#include <QtGui/QGuiApplication>
#include <QtGui/QWindow>

namespace Window {
namespace {

[[nodiscard]] style::margins ShadowExtents() {
	return st::callShadow.extend;
}

template <typename T>
void RemoveDuplicates(std::vector<T> &v) {
	auto end = v.end();
	for (auto it = v.begin(); it != end; ++it) {
		end = std::remove(it + 1, end, *it);
	}

	v.erase(end, v.end());
}

} // namespace

TitleWidgetQt::TitleWidgetQt(QWidget *parent)
: TitleWidget(parent)
, _st(st::defaultWindowTitle)
, _minimize(this, _st.minimize)
, _maximizeRestore(this, _st.maximize)
, _close(this, _st.close)
, _shadow(this, st::titleShadow)
, _maximizedState(parent->window()->windowState() & Qt::WindowMaximized) {
	_minimize->setClickedCallback([=] {
		window()->setWindowState(
			window()->windowState() | Qt::WindowMinimized);
		_minimize->clearState();
	});
	_minimize->setPointerCursor(false);
	_maximizeRestore->setClickedCallback([=] {
		window()->setWindowState(_maximizedState
			? Qt::WindowNoState
			: Qt::WindowMaximized);
		_maximizeRestore->clearState();
	});
	_maximizeRestore->setPointerCursor(false);
	_close->setClickedCallback([=] {
		window()->close();
		_close->clearState();
	});
	_close->setPointerCursor(false);

	Ui::Platform::TitleControlsLayoutChanged(
	) | rpl::start_with_next([=] {
		updateControlsPosition();
	}, lifetime());

	QCoreApplication::instance()->installEventFilter(this);

	_windowWasFrameless = (window()->windowFlags()
		& Qt::FramelessWindowHint) != 0;

	if (!_windowWasFrameless) {
		toggleFramelessWindow(true);
	}

	setAttribute(Qt::WA_OpaquePaintEvent);
	resize(width(), _st.height);

	updateWindowExtents();
}

TitleWidgetQt::~TitleWidgetQt() {
	restoreCursor();

	if (!_windowWasFrameless) {
		toggleFramelessWindow(false);
	}

	if (_extentsSet) {
		Ui::Platform::UnsetWindowExtents(window()->windowHandle());
	}
}

void TitleWidgetQt::init() {
	connect(
		window()->windowHandle(),
		&QWindow::windowStateChanged,
		this,
		[=](Qt::WindowState state) { windowStateChanged(state); });
	connect(
		window()->windowHandle(),
		&QWindow::visibleChanged,
		this,
		[=](bool visible) { visibleChanged(visible); });
	_maximizedState = (window()->windowState() & Qt::WindowMaximized);
	_activeState = isActiveWindow();
	updateButtonsState();
}

bool TitleWidgetQt::hasShadow() const {
	const auto center = window()->geometry().center();
	return Ui::Platform::WindowExtentsSupported()
		&& Ui::Platform::TranslucentWindowsSupported(center);
}

Ui::IconButton *TitleWidgetQt::controlWidget(Control control) const {
	switch (control) {
	case Control::Minimize: return _minimize;
	case Control::Maximize: return _maximizeRestore;
	case Control::Close: return _close;
	}

	return nullptr;
}

void TitleWidgetQt::paintEvent(QPaintEvent *e) {
	auto active = isActiveWindow();
	if (_activeState != active) {
		_activeState = active;
		updateButtonsState();
	}
	Painter(this).fillRect(rect(), active ? _st.bgActive : _st.bg);
}

void TitleWidgetQt::toggleFramelessWindow(bool enabled) {
	window()->windowHandle()->setFlag(Qt::FramelessWindowHint, enabled);
}

void TitleWidgetQt::updateWindowExtents() {
	if (hasShadow()) {
		Ui::Platform::SetWindowExtents(
			window()->windowHandle(),
			resizeArea());

		_extentsSet = true;
	} else if (_extentsSet) {
		Ui::Platform::UnsetWindowExtents(window()->windowHandle());
		_extentsSet = false;
	}
}

void TitleWidgetQt::updateControlsPosition() {
	const auto controlsLayout = Ui::Platform::TitleControlsLayout();
	const auto controlsLeft = controlsLayout.left;
	const auto controlsRight = controlsLayout.right;

	const auto controlPresent = [&](Control control) {
		return ranges::contains(controlsLeft, control)
		|| ranges::contains(controlsRight, control);
	};

	if (controlPresent(Control::Minimize)) {
		_minimize->show();
	} else {
		_minimize->hide();
	}

	if (controlPresent(Control::Maximize)) {
		_maximizeRestore->show();
	} else {
		_maximizeRestore->hide();
	}

	if (controlPresent(Control::Close)) {
		_close->show();
	} else {
		_close->hide();
	}

	updateControlsPositionBySide(controlsLeft, false);
	updateControlsPositionBySide(controlsRight, true);
}

void TitleWidgetQt::updateControlsPositionBySide(
		const std::vector<Control> &controls,
		bool right) {
	auto preparedControls = right
		? (ranges::views::reverse(controls) | ranges::to_vector)
		: controls;

	RemoveDuplicates(preparedControls);

	auto position = 0;
	for (const auto &control : preparedControls) {
		const auto widget = controlWidget(control);
		if (!widget) {
			continue;
		}

		if (right) {
			widget->moveToRight(position, 0);
		} else {
			widget->moveToLeft(position, 0);
		}

		position += widget->width();
	}
}

void TitleWidgetQt::resizeEvent(QResizeEvent *e) {
	updateControlsPosition();
	_shadow->setGeometry(0, height() - st::lineWidth, width(), st::lineWidth);
}

void TitleWidgetQt::mousePressEvent(QMouseEvent *e) {
	if (e->button() == Qt::LeftButton) {
		_mousePressed = true;
	} else if (e->button() == Qt::RightButton) {
		Ui::Platform::ShowWindowMenu(window()->windowHandle());
	}
}

void TitleWidgetQt::mouseReleaseEvent(QMouseEvent *e) {
	if (e->button() == Qt::LeftButton) {
		_mousePressed = false;
	}
}

void TitleWidgetQt::mouseMoveEvent(QMouseEvent *e) {
	if (_mousePressed) {
		window()->windowHandle()->startSystemMove();
	}
}

void TitleWidgetQt::mouseDoubleClickEvent(QMouseEvent *e) {
	if (_maximizedState) {
		window()->setWindowState(Qt::WindowNoState);
	} else {
		window()->setWindowState(Qt::WindowMaximized);
	}
}

bool TitleWidgetQt::eventFilter(QObject *obj, QEvent *e) {
	if (e->type() == QEvent::MouseMove
		|| e->type() == QEvent::MouseButtonPress) {
		if (obj->isWidgetType()
			&& window()->isAncestorOf(static_cast<QWidget*>(obj))) {
			const auto mouseEvent = static_cast<QMouseEvent*>(e);
			const auto currentPoint = mouseEvent->windowPos().toPoint();
			const auto edges = edgesFromPos(currentPoint);

			if (e->type() == QEvent::MouseMove
				&& mouseEvent->buttons() == Qt::NoButton) {
				if (_mousePressed) {
					_mousePressed = false;
				}

				updateCursor(edges);
			}

			if (e->type() == QEvent::MouseButtonPress
				&& mouseEvent->button() == Qt::LeftButton
				&& edges) {
				return window()->windowHandle()->startSystemResize(edges);
			}
		}
	} else if (e->type() == QEvent::Leave) {
		if (obj->isWidgetType() && window() == static_cast<QWidget*>(obj)) {
			restoreCursor();
		}
	} else if (e->type() == QEvent::Move
		|| e->type() == QEvent::Resize) {
		if (obj->isWidgetType() && window() == static_cast<QWidget*>(obj)) {
			updateWindowExtents();
		}
	}

	return TitleWidget::eventFilter(obj, e);
}

void TitleWidgetQt::windowStateChanged(Qt::WindowState state) {
	if (state == Qt::WindowMinimized) {
		return;
	}

	const auto maximized = (state == Qt::WindowMaximized);
	if (_maximizedState != maximized) {
		_maximizedState = maximized;
		updateButtonsState();
		updateWindowExtents();
	}
}

void TitleWidgetQt::visibleChanged(bool visible) {
	if (visible) {
		updateWindowExtents();

		// workaround a bug in Qt 5.12, works ok in Qt 5.15
		// https://github.com/telegramdesktop/tdesktop/issues/10119
		if (!_windowWasFrameless) {
			toggleFramelessWindow(true);
		}
	}
}

void TitleWidgetQt::updateButtonsState() {
	_minimize->setIconOverride(_activeState
		? &_st.minimizeIconActive
		: nullptr,
		_activeState
		? &_st.minimizeIconActiveOver
		: nullptr);
	if (_maximizedState) {
		_maximizeRestore->setIconOverride(
			_activeState
			? &_st.restoreIconActive : &_st.restoreIcon,
			_activeState
			? &_st.restoreIconActiveOver
			: &_st.restoreIconOver);
	} else {
		_maximizeRestore->setIconOverride(_activeState
			? &_st.maximizeIconActive
			: nullptr,
			_activeState
			? &_st.maximizeIconActiveOver
			: nullptr);
	}
	_close->setIconOverride(_activeState
		? &_st.closeIconActive
		: nullptr,
		_activeState
		? &_st.closeIconActiveOver
		: nullptr);
}

QMargins TitleWidgetQt::resizeArea() const {
	if (_maximizedState) {
		return QMargins();
	} else if (!hasShadow()) {
		return QMargins(
			st::windowResizeArea,
			st::windowResizeArea,
			st::windowResizeArea,
			st::windowResizeArea);
	}

	return ShadowExtents();
}

Qt::Edges TitleWidgetQt::edgesFromPos(const QPoint &pos) const {
	const auto area = resizeArea();

	if (area.isNull()) {
		return Qt::Edges();
	} else if (pos.x() <= area.left()) {
		if (pos.y() <= area.top()) {
			return Qt::LeftEdge | Qt::TopEdge;
		} else if (pos.y() >= (window()->height() - area.bottom())) {
			return Qt::LeftEdge | Qt::BottomEdge;
		}

		return Qt::LeftEdge;
	} else if (pos.x() >= (window()->width() - area.right())) {
		if (pos.y() <= area.top()) {
			return Qt::RightEdge | Qt::TopEdge;
		} else if (pos.y() >= (window()->height() - area.bottom())) {
			return Qt::RightEdge | Qt::BottomEdge;
		}

		return Qt::RightEdge;
	} else if (pos.y() <= area.top()) {
		return Qt::TopEdge;
	} else if (pos.y() >= (window()->height() - area.bottom())) {
		return Qt::BottomEdge;
	}

	return Qt::Edges();
}

void TitleWidgetQt::updateCursor(Qt::Edges edges) {
	if (!edges) {
		restoreCursor();
		return;
	} else if (!QGuiApplication::overrideCursor()) {
		_cursorOverriden = false;
	}

	if (!_cursorOverriden) {
		_cursorOverriden = true;
		QGuiApplication::setOverrideCursor(QCursor());
	}

	if (((edges & Qt::LeftEdge) && (edges & Qt::TopEdge))
		|| ((edges & Qt::RightEdge) && (edges & Qt::BottomEdge))) {
		QGuiApplication::changeOverrideCursor(QCursor(Qt::SizeFDiagCursor));
	} else if (((edges & Qt::LeftEdge) && (edges & Qt::BottomEdge))
		|| ((edges & Qt::RightEdge) && (edges & Qt::TopEdge))) {
		QGuiApplication::changeOverrideCursor(QCursor(Qt::SizeBDiagCursor));
	} else if ((edges & Qt::LeftEdge) || (edges & Qt::RightEdge)) {
		QGuiApplication::changeOverrideCursor(QCursor(Qt::SizeHorCursor));
	} else if ((edges & Qt::TopEdge) || (edges & Qt::BottomEdge)) {
		QGuiApplication::changeOverrideCursor(QCursor(Qt::SizeVerCursor));
	}
}

void TitleWidgetQt::restoreCursor() {
	if (_cursorOverriden) {
		_cursorOverriden = false;
		QGuiApplication::restoreOverrideCursor();
	}
}

} // namespace Window
