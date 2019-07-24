/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "intro/introstart.h"

#include "lang/lang_keys.h"
#include "intro/introphone.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"

namespace Intro {

StartWidget::StartWidget(
	QWidget *parent,
	not_null<Main::Account*> account,
	not_null<Widget::Data*> data)
: Step(parent, account, data, true) {
	setMouseTracking(true);
	setTitleText(rpl::single(qsl("Telegram Desktop")));
	setDescriptionText(tr::lng_intro_about());
	show();
}

void StartWidget::submit() {
	goNext<PhoneWidget>();
}

rpl::producer<QString> StartWidget::nextButtonText() const {
	return tr::lng_start_msgs();
}

} // namespace Intro
