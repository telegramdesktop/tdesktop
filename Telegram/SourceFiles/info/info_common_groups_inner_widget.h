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

#include <rpl/producer.h>
#include "ui/rp_widget.h"

namespace Info {
namespace CommonGroups {

class Memento;

class InnerWidget final : public Ui::RpWidget {
public:
	InnerWidget(QWidget *parent, not_null<UserData*> user);

	not_null<UserData*> user() const {
		return _user;
	}

	void resizeToWidth(int newWidth, int minHeight) {
		_minHeight = minHeight;
		return RpWidget::resizeToWidth(newWidth);
	}

	void saveState(not_null<Memento*> memento);
	void restoreState(not_null<Memento*> memento);

protected:
	int resizeGetHeight(int newWidth) override;
	void visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) override;

private:
	not_null<UserData*> _user;

	int _rowsHeightFake = 0;
	int _visibleTop = 0;
	int _visibleBottom = 0;
	int _minHeight = 0;

};

} // namespace CommonGroups
} // namespace Info

