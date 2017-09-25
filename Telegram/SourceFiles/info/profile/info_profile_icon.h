/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#pragma once

#include "ui/rp_widget.h"

namespace Info {
namespace Profile {

class FloatingIcon : public Ui::RpWidget {
public:
	FloatingIcon(
		RpWidget *parent,
		const style::icon &icon,
		QPoint position);

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	struct Tag {
	};
	FloatingIcon(
		RpWidget *parent,
		const style::icon &icon,
		QPoint position,
		const Tag &);

	not_null<const style::icon*> _icon;
	QPoint _point;

};

} // namespace Profile
} // namespace Info
