/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "window/window_title_qt.h"

#include "platform/platform_specific.h"
#include "base/platform/base_platform_info.h"
#include "ui/platform/ui_platform_utility.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/shadow.h"
#include "core/core_settings.h"
#include "core/application.h"
#include "styles/style_window.h"
#include "styles/style_calls.h" // st::callShadow
#include "base/call_delayed.h"

#include <QtGui/QGuiApplication>
#include <QtGui/QWindow>
#include <QtWidgets/QApplication>

namespace Window {
namespace {

[[nodiscard]] style::margins ShadowExtents() {
	return st::callShadow.extend;
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

	Core::App().settings().windowControlsLayoutChanges(
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
		Platform::UnsetWindowExtents(window()->windowHandle());
	}
}

void TitleWidgetQt::toggleFramelessWindow(bool enabled) {
	// setWindowFlag calls setParent(parentWidget(), newFlags), which
	// always calls hide() explicitly, we have to show() the window back.
	const auto top = window();
	const auto hidden = top->isHidden();
	top->setWindowFlag(Qt::FramelessWindowHint, enabled);
	if (!hidden) {
		base::call_delayed(
			kShowAfterWindowFlagChangeDelay,
			top,
			[=] { top->show(); });
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
	return Platform::WindowsNeedShadow()
		&& Ui::Platform::TranslucentWindowsSupported(center);
}

void TitleWidgetQt::paintEvent(QPaintEvent *e) {
	auto active = isActiveWindow();
	if (_activeState != active) {
		_activeState = active;
		updateButtonsState();
	}
	Painter(this).fillRect(rect(), active ? _st.bgActive : _st.bg);
}

void TitleWidgetQt::updateWindowExtents() {
	if (hasShadow()) {
		if (!_maximizedState) {
			Platform::SetWindowExtents(
				window()->windowHandle(),
				ShadowExtents());
		} else {
			Platform::SetWindowExtents(
				window()->windowHandle(),
				QMargins());
		}

		_extentsSet = true;
	} else if (_extentsSet) {
		Platform::UnsetWindowExtents(window()->windowHandle());
		_extentsSet = false;
	}
}

void TitleWidgetQt::updateControlsPosition() {
	const auto controlsLayout = Core::App().settings().windowControlsLayout();
	const auto controlsLeft = controlsLayout.left;
	const auto controlsRight = controlsLayout.right;

	if (ranges::contains(controlsLeft, Control::Minimize)
		|| ranges::contains(controlsRight, Control::Minimize)) {
		_minimize->show();
	} else {
		_minimize->hide();
	}

	if (ranges::contains(controlsLeft, Control::Maximize)
		|| ranges::contains(controlsRight, Control::Maximize)) {
		_maximizeRestore->show();
	} else {
		_maximizeRestore->hide();
	}

	if (ranges::contains(controlsLeft, Control::Close)
		|| ranges::contains(controlsRight, Control::Close)) {
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
	const auto preparedControls = right
		? (ranges::view::reverse(controls) | ranges::to_vector)
		: controls;

	auto position = 0;
	for (const auto &control : preparedControls) {
		switch (control) {
		case Control::Minimize:
			if (right) {
				_minimize->moveToRight(position, 0);
			} else {
				_minimize->moveToLeft(position, 0);
			}

			position += _minimize->width();
			break;
		case Control::Maximize:
			if (right) {
				_maximizeRestore->moveToRight(position, 0);
			} else {
				_maximizeRestore->moveToLeft(position, 0);
			}

			position += _maximizeRestore->width();
			break;
		case Control::Close:
			if (right) {
				_close->moveToRight(position, 0);
			} else {
				_close->moveToLeft(position, 0);
			}

			position += _close->width();
			break;
		}
	}
}

void TitleWidgetQt::resizeEvent(QResizeEvent *e) {
	updateControlsPosition();
	_shadow->setGeometry(0, height() - st::lineWidth, width(), st::lineWidth);
}

void TitleWidgetQt::mousePressEvent(QMouseEvent *e) {
	if (e->button() == Qt::LeftButton) {
		if ((crl::now() - _pressedForMoveTime)
			< QApplication::doubleClickInterval()) {
			if (_maximizedState) {
				window()->setWindowState(Qt::WindowNoState);
			} else {
				window()->setWindowState(Qt::WindowMaximized);
			}
		} else {
			_pressedForMove = true;
			_pressedForMoveTime = crl::now();
			_pressedForMovePoint = e->windowPos().toPoint();
		}
	} else if (e->button() == Qt::RightButton) {
		Platform::ShowWindowMenu(window()->windowHandle());
	}
}

void TitleWidgetQt::mouseReleaseEvent(QMouseEvent *e) {
	if (e->button() == Qt::LeftButton) {
		_pressedForMove = false;
	}
}

bool TitleWidgetQt::eventFilter(QObject *obj, QEvent *e) {
	if (e->type() == QEvent::MouseMove
		|| e->type() == QEvent::MouseButtonPress) {
		if (window()->isAncestorOf(static_cast<QWidget*>(obj))) {
			const auto mouseEvent = static_cast<QMouseEvent*>(e);
			const auto currentPoint = mouseEvent->windowPos().toPoint();
			const auto edges = edgesFromPos(currentPoint);
			const auto dragDistance = QApplication::startDragDistance();

			if (e->type() == QEvent::MouseMove
				&& mouseEvent->buttons() == Qt::NoButton) {
				if (_pressedForMove) {
					_pressedForMove = false;
				}

				updateCursor(edges);
			}

			if (e->type() == QEvent::MouseMove
				&& _pressedForMove
				&& ((currentPoint - _pressedForMovePoint).manhattanLength()
					>= dragDistance)) {
				return startMove();
			}

			if (e->type() == QEvent::MouseButtonPress
				&& mouseEvent->button() == Qt::LeftButton
				&& !_maximizedState) {
				return startResize(edges);
			}
		}
	} else if (e->type() == QEvent::Leave) {
		if (window() == static_cast<QWidget*>(obj)) {
			restoreCursor();
		}
	} else if (e->type() == QEvent::Move
		|| e->type() == QEvent::Resize) {
		if (window() == static_cast<QWidget*>(obj)) {
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

int TitleWidgetQt::getResizeArea(Qt::Edge edge) const {
	if (!hasShadow()) {
		return st::windowResizeArea;
	}

	if (edge == Qt::LeftEdge) {
		return ShadowExtents().left();
	} else if (edge == Qt::RightEdge) {
		return ShadowExtents().right();
	} else if (edge == Qt::TopEdge) {
		return ShadowExtents().top();
	} else if (edge == Qt::BottomEdge) {
		return ShadowExtents().bottom();
	}

	return 0;
}

Qt::Edges TitleWidgetQt::edgesFromPos(const QPoint &pos) {
	if (pos.x() <= getResizeArea(Qt::LeftEdge)) {
		if (pos.y() <= getResizeArea(Qt::TopEdge)) {
			return Qt::LeftEdge | Qt::TopEdge;
		} else if (pos.y()
			>= (window()->height() - getResizeArea(Qt::BottomEdge))) {
			return Qt::LeftEdge | Qt::BottomEdge;
		}

		return Qt::LeftEdge;
	} else if (pos.x()
		>= (window()->width() - getResizeArea(Qt::RightEdge))) {
		if (pos.y() <= getResizeArea(Qt::TopEdge)) {
			return Qt::RightEdge | Qt::TopEdge;
		} else if (pos.y()
			>= (window()->height() - getResizeArea(Qt::BottomEdge))) {
			return Qt::RightEdge | Qt::BottomEdge;
		}

		return Qt::RightEdge;
	} else if (pos.y() <= getResizeArea(Qt::TopEdge)) {
		return Qt::TopEdge;
	} else if (pos.y()
		>= (window()->height() - getResizeArea(Qt::BottomEdge))) {
		return Qt::BottomEdge;
	} else {
		return Qt::Edges();
	}
}

void TitleWidgetQt::restoreCursor() {
	if (_cursorOverriden) {
		_cursorOverriden = false;
		QGuiApplication::restoreOverrideCursor();
	}
}

void TitleWidgetQt::updateCursor(Qt::Edges edges) {
	if (!edges || _maximizedState) {
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

bool TitleWidgetQt::startMove() {
	if (Platform::StartSystemMove(window()->windowHandle())) {
		return true;
	}

#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0) || defined DESKTOP_APP_QT_PATCHED
	if (window()->windowHandle()->startSystemMove()) {
		return true;
	}
#endif // Qt >= 5.15 || DESKTOP_APP_QT_PATCHED

	return false;
}

bool TitleWidgetQt::startResize(Qt::Edges edges) {
	if (edges) {
		if (Platform::StartSystemResize(window()->windowHandle(), edges)) {
			return true;
		}

#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0) || defined DESKTOP_APP_QT_PATCHED
		if (window()->windowHandle()->startSystemResize(edges)) {
			return true;
		}
#endif // Qt >= 5.15 || DESKTOP_APP_QT_PATCHED
	}

	return false;
}

} // namespace Window
