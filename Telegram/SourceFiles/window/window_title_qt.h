/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "window/window_title.h"
#include "window/window_controls_layout.h"
#include "base/object_ptr.h"

namespace style {
struct WindowTitle;
} // namespace style

namespace Ui {
class IconButton;
class PlainShadow;
} // namespace Ui

namespace Window {

// If we toggle frameless window hint in maximized window, and
// show it back too quickly, the mouse position inside the window
// won't be correct (from Qt-s point of view) until we Alt-Tab from
// that window. If we show the window back with this delay it works.
inline constexpr auto kShowAfterWindowFlagChangeDelay = crl::time(1000);

class TitleWidgetQt : public TitleWidget {
public:
	TitleWidgetQt(QWidget *parent);
	~TitleWidgetQt();

	void init() override;

protected:
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseDoubleClickEvent(QMouseEvent *e) override;

	bool eventFilter(QObject *obj, QEvent *e) override;

private:
	void windowStateChanged(Qt::WindowState state = Qt::WindowNoState);
	void visibleChanged(bool visible);
	void updateWindowExtents();
	void updateButtonsState();
	void updateControlsPosition();
	void updateControlsPositionBySide(
		const std::vector<Control> &controls,
		bool right);

	void toggleFramelessWindow(bool enabled);
	bool hasShadow() const;
	int getResizeArea(Qt::Edge edge) const;
	Qt::Edges edgesFromPos(const QPoint &pos);
	void updateCursor(Qt::Edges edges);
	void restoreCursor();
	bool startMove();
	bool startResize(Qt::Edges edges);

	const style::WindowTitle &_st;
	object_ptr<Ui::IconButton> _minimize;
	object_ptr<Ui::IconButton> _maximizeRestore;
	object_ptr<Ui::IconButton> _close;
	object_ptr<Ui::PlainShadow> _shadow;

	bool _maximizedState = false;
	bool _activeState = false;
	bool _windowWasFrameless = false;
	bool _cursorOverriden = false;
	bool _extentsSet = false;

};

} // namespace Window
