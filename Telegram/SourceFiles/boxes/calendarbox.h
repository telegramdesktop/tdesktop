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

#include "abstractbox.h"

namespace Ui {
class IconButton;
} // namespace Ui

class CalendarBox : public BoxContent {
public:
	CalendarBox(QWidget*, QDate month, QDate highlighted, base::lambda<void(QDate date)> callback);
	~CalendarBox();

protected:
	void prepare() override;

	void resizeEvent(QResizeEvent *e) override;

private:
	void monthChanged(QDate month);

	class Context;
	std::unique_ptr<Context> _context;

	class Inner;
	object_ptr<Inner> _inner;

	class Title;
	object_ptr<Title> _title;
	object_ptr<Ui::IconButton> _left;
	object_ptr<Ui::IconButton> _right;

	base::lambda<void(QDate date)> _callback;

};
