/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "window/window_title_qt.h"

#include "ui/widgets/buttons.h"
#include "ui/widgets/shadow.h"
#include "styles/style_window.h"

#include <QtGui/QGuiApplication>
#include <QtGui/QWindow>

namespace Window {

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

	QCoreApplication::instance()->installEventFilter(this);

	window()->setWindowFlag(Qt::FramelessWindowHint, true);
	setAttribute(Qt::WA_OpaquePaintEvent);
	resize(width(), _st.height);
}

void TitleWidgetQt::init() {
	connect(
		window()->windowHandle(),
		&QWindow::windowStateChanged,
		this,
		[=](Qt::WindowState state) { windowStateChanged(state); });
	_maximizedState = (window()->windowState() & Qt::WindowMaximized);
	_activeState = isActiveWindow();
	updateButtonsState();
}

void TitleWidgetQt::paintEvent(QPaintEvent *e) {
	auto active = isActiveWindow();
	if (_activeState != active) {
		_activeState = active;
		updateButtonsState();
	}
	Painter(this).fillRect(rect(), active ? _st.bgActive : _st.bg);
}

void TitleWidgetQt::updateControlsPosition() {
	auto right = 0;
	_close->moveToRight(right, 0); right += _close->width();
	_maximizeRestore->moveToRight(right, 0); right += _maximizeRestore->width();
	_minimize->moveToRight(right, 0);
}

void TitleWidgetQt::resizeEvent(QResizeEvent *e) {
	updateControlsPosition();
	_shadow->setGeometry(0, height() - st::lineWidth, width(), st::lineWidth);
}

void TitleWidgetQt::mousePressEvent(QMouseEvent *e) {
	if (e->button() != Qt::LeftButton) {
		return;
	}

#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0) || defined DESKTOP_APP_QT_PATCHED
	window()->windowHandle()->startSystemMove();
#endif // Qt >= 5.15 || DESKTOP_APP_QT_PATCHED
}

void TitleWidgetQt::mouseDoubleClickEvent(QMouseEvent *e) {
	if (window()->windowState() == Qt::WindowMaximized) {
		window()->setWindowState(Qt::WindowNoState);
	} else {
		window()->setWindowState(Qt::WindowMaximized);
	}
}

bool TitleWidgetQt::eventFilter(QObject *obj, QEvent *e) {
	if (e->type() == QEvent::MouseMove
		|| e->type() == QEvent::MouseButtonPress) {
		if(window()->isAncestorOf(static_cast<QWidget*>(obj))) {
			const auto mouseEvent = static_cast<QMouseEvent*>(e);

			if (e->type() == QEvent::MouseMove) {
				updateCursor(mouseEvent->windowPos().toPoint());
			}

			if(e->type() == QEvent::MouseButtonPress
				&& mouseEvent->button() == Qt::LeftButton
				&& window()->windowState() != Qt::WindowMaximized) {
				return startResize(mouseEvent->windowPos().toPoint());
			}
		}
	} else if (e->type() == QEvent::Leave) {
		if (window() == static_cast<QWidget*>(obj)) {
			while (QGuiApplication::overrideCursor()) {
				QGuiApplication::restoreOverrideCursor();
			}
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

Qt::Edges TitleWidgetQt::edgesFromPos(const QPoint &pos) {
	if (pos.x() <= st::windowResizeArea) {
		if (pos.y() <= st::windowResizeArea) {
			return Qt::LeftEdge | Qt::TopEdge;
		} else if (pos.y() >= (window()->height() - st::windowResizeArea)) {
			return Qt::LeftEdge | Qt::BottomEdge;
		}

		return Qt::LeftEdge;
	} else if (pos.x() >= (window()->width() - st::windowResizeArea)) {
		if (pos.y() <= st::windowResizeArea) {
			return Qt::RightEdge | Qt::TopEdge;
		} else if (pos.y() >= (window()->height() - st::windowResizeArea)) {
			return Qt::RightEdge | Qt::BottomEdge;
		}

		return Qt::RightEdge;
	} else if (pos.y() <= st::windowResizeArea) {
		return Qt::TopEdge;
	} else if (pos.y() >= (window()->height() - st::windowResizeArea)) {
		return Qt::BottomEdge;
	} else {
		return 0;
	}
}

void TitleWidgetQt::updateCursor(const QPoint &pos) {
	const auto edges = edgesFromPos(pos);

	if (!edges || window()->windowState() == Qt::WindowMaximized) {
		while (QGuiApplication::overrideCursor()) {
			QGuiApplication::restoreOverrideCursor();
		}

		return;
	} else if (!QGuiApplication::overrideCursor()) {
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

bool TitleWidgetQt::startResize(const QPoint &pos) {
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0) || defined DESKTOP_APP_QT_PATCHED
	if (const auto edges = edgesFromPos(pos)) {
		return window()->windowHandle()->startSystemResize(edges);
	}
#endif // Qt >= 5.15 || DESKTOP_APP_QT_PATCHED

	return false;
}

} // namespace Window
