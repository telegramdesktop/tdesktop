/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "boxes/generic_box.h"
#include "ui/widgets/checkbox.h"

namespace Data {
struct CloudTheme;
} // namespace Data

namespace Window {

class SessionController;

namespace Theme {

struct EmbeddedScheme;
struct Colorizer;

struct CloudListColors {
	QImage background;
	QColor sent;
	QColor received;
	QColor radiobuttonBg;
	QColor radiobuttonInactive;
	QColor radiobuttonActive;
};

[[nodiscard]] CloudListColors ColorsFromScheme(const EmbeddedScheme &scheme);
[[nodiscard]] CloudListColors ColorsFromScheme(
	const EmbeddedScheme &scheme,
	const Colorizer &colorizer);

class CloudListCheck final : public Ui::AbstractCheckView {
public:
	using Colors = CloudListColors;
	CloudListCheck(const Colors &colors, bool checked);

	QSize getSize() const override;
	void paint(
		Painter &p,
		int left,
		int top,
		int outerWidth) override;
	QImage prepareRippleMask() const override;
	bool checkRippleStartPosition(QPoint position) const override;

	void setColors(const Colors &colors);

private:
	void checkedChangedHook(anim::type animated) override;

	Colors _colors;
	Ui::RadioView _radio;

};

void CloudListBox(
	not_null<GenericBox*> box,
	not_null<Window::SessionController*> window,
	std::vector<Data::CloudTheme> list);

} // namespace Theme
} // namespace Window
