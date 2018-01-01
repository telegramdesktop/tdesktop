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

namespace Window {

class Controller;
class SectionWidget;
class LayerWidget;
enum class Column;

class SectionMemento {
public:
	virtual object_ptr<SectionWidget> createWidget(
		QWidget *parent,
		not_null<Controller*> controller,
		Column column,
		const QRect &geometry) = 0;

	virtual object_ptr<LayerWidget> createLayer(
			not_null<Controller*> controller,
			const QRect &geometry) {
		return nullptr;
	}
	virtual bool instant() const {
		return false;
	}

	virtual ~SectionMemento() = default;

};

} // namespace Window
