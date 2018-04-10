/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
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
	setTitleText([] { return qsl("Telegram Desktop"); });
	setDescriptionText(langFactory(lng_intro_about));
	show();
}

void StartWidget::submit() {
	goNext(new Intro::PhoneWidget(parentWidget(), getData()));
}

QString StartWidget::nextButtonText() const {
	return lang(lng_start_msgs);
}

} // namespace Intro
