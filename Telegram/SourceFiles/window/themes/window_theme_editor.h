/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Ui {
class FlatButton;
class ScrollArea;
class CrossButton;
class MultiSelect;
class PlainShadow;
} // namespace Ui

namespace Window {

class Controller;

namespace Theme {

bool CopyColorsToPalette(
	const QString &destination,
	const QString &themePath,
	const QByteArray &themeContent);

class Editor : public TWidget {
public:
	Editor(QWidget*, not_null<Window::Controller*> window);

protected:
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;

	void focusInEvent(QFocusEvent *e) override;

private:
	void save();
	void closeEditor();

	not_null<Window::Controller*> _window;
	object_ptr<Ui::ScrollArea> _scroll;
	class Inner;
	QPointer<Inner> _inner;
	object_ptr<Ui::CrossButton> _close;
	object_ptr<Ui::MultiSelect> _select;
	object_ptr<Ui::PlainShadow> _leftShadow;
	object_ptr<Ui::PlainShadow> _topShadow;
	object_ptr<Ui::FlatButton> _save;

};

} // namespace Theme
} // namespace Window
