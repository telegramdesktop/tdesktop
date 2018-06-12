/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "platform/platform_window_title.h"

namespace Ui {
class IconButton;
class PlainShadow;
} // namespace Ui

namespace Window {
namespace Theme {

int DefaultPreviewTitleHeight();
void DefaultPreviewWindowFramePaint(QImage &preview, const style::palette &palette, QRect body, int outerWidth);

} // namespace Theme
} // namespace Window

namespace Platform {

class TitleWidget : public Window::TitleWidget, private base::Subscriber {
	Q_OBJECT

public:
	TitleWidget(QWidget *parent);

	void init() override;

	Window::HitTestResult hitTest(const QPoint &p) const override;

public slots:
	void onWindowStateChanged(Qt::WindowState state = Qt::WindowNoState);
	void updateControlsVisibility();

protected:
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

private:
	void updateButtonsState();
	void updateControlsPosition();

	object_ptr<Ui::IconButton> _minimize;
	object_ptr<Ui::IconButton> _maximizeRestore;
	object_ptr<Ui::IconButton> _close;
	object_ptr<Ui::PlainShadow> _shadow;

	bool _maximizedState = false;
	bool _activeState = false;

};

inline object_ptr<Window::TitleWidget> CreateTitleWidget(QWidget *parent) {
	return object_ptr<TitleWidget>(parent);
}

inline int PreviewTitleHeight() {
	return Window::Theme::DefaultPreviewTitleHeight();
}

inline void PreviewWindowFramePaint(QImage &preview, const style::palette &palette, QRect body, int outerWidth) {
	return Window::Theme::DefaultPreviewWindowFramePaint(preview, palette, body, outerWidth);
}

} // namespace Platform
