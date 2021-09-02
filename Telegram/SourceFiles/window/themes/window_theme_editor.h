/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_cloud_themes.h"
#include "ui/rp_widget.h"
#include "base/object_ptr.h"

namespace style {
struct colorizer;
} // namespace style

namespace Ui {
class FlatButton;
class ScrollArea;
class CrossButton;
class MultiSelect;
class PlainShadow;
class DropdownMenu;
class IconButton;
} // namespace Ui

namespace Window {

class Controller;

namespace Theme {

struct ParsedTheme {
	QByteArray palette;
	QByteArray background;
	bool isPng = false;
	bool tiled = false;
};

[[nodiscard]] QByteArray ColorHexString(const QColor &color);
[[nodiscard]] QByteArray ReplaceValueInPaletteContent(
	const QByteArray &content,
	const QByteArray &name,
	const QByteArray &value);
[[nodiscard]] QByteArray WriteCloudToText(const Data::CloudTheme &cloud);
[[nodiscard]] Data::CloudTheme ReadCloudFromText(const QByteArray &text);
[[nodiscard]] QByteArray StripCloudTextFields(const QByteArray &text);

class Editor : public TWidget {
public:
	Editor(
		QWidget*,
		not_null<Window::Controller*> window,
		const Data::CloudTheme &cloud);

	[[nodiscard]] static QByteArray ColorizeInContent(
		QByteArray content,
		const style::colorizer &colorizer);

protected:
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;

	void focusInEvent(QFocusEvent *e) override;

private:
	void save();
	void showMenu();
	void exportTheme();
	void importTheme();
	void closeEditor();
	void closeWithConfirmation();
	void updateControlsGeometry();

	const not_null<Window::Controller*> _window;
	const Data::CloudTheme _cloud;

	object_ptr<Ui::ScrollArea> _scroll;
	class Inner;
	QPointer<Inner> _inner;
	object_ptr<Ui::CrossButton> _close;
	object_ptr<Ui::IconButton> _menuToggle;
	base::unique_qptr<Ui::DropdownMenu> _menu;
	object_ptr<Ui::MultiSelect> _select;
	object_ptr<Ui::PlainShadow> _leftShadow;
	object_ptr<Ui::PlainShadow> _topShadow;
	object_ptr<Ui::FlatButton> _save;
	bool _saving = false;

};

} // namespace Theme
} // namespace Window
