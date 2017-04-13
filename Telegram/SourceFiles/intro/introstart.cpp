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
#include "intro/introstart.h"

#include "lang/lang_keys.h"
#include "application.h"
#include "intro/introphone.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"

namespace Intro {

StartWidget::StartWidget(QWidget *parent, Widget::Data *data) : Step(parent, data, true) {
	setMouseTracking(true);
	setTitleText(qsl("Telegram Desktop"));
	setDescriptionText(lang(lng_intro_about));
	show();
}

void StartWidget::submit() {
	goNext(new Intro::PhoneWidget(parentWidget(), getData()));
}

QString StartWidget::nextButtonText() const {
	return lang(lng_start_msgs);
}

} // namespace Intro
