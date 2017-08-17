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
#include "profile/profile_section_memento.h"

#include "profile/profile_widget.h"

namespace Profile {

object_ptr<Window::SectionWidget> SectionMemento::createWidget(QWidget *parent, not_null<Window::Controller*> controller, const QRect &geometry) {
	auto result = object_ptr<Widget>(parent, controller, _peer);
	result->setInternalState(geometry, this);
	return std::move(result);
}

} // namespace Profile
