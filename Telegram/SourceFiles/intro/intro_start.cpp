/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "intro/intro_start.h"

#include "lang/lang_keys.h"
#include "intro/intro_qr.h"
#include "intro/intro_phone.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "main/main_account.h"
#include "main/main_app_config.h"

namespace Intro {
namespace details {

StartWidget::StartWidget(
	QWidget *parent,
	not_null<Main::Account*> account,
	not_null<Data*> data)
: Step(parent, account, data, true) {
	setMouseTracking(true);
	setTitleText(rpl::single(qsl("Telegram Desktop")));
	setDescriptionText(tr::lng_intro_about());
	show();
}

void StartWidget::submit() {
	account().destroyStaleAuthorizationKeys();
	const auto qrLogin = account().appConfig().get<QString>(
		"qr_login_code",
		"[not-set]");
	DEBUG_LOG(("qr_login_code: %1").arg(qrLogin));
	if (qrLogin == "primary") {
		goNext<QrWidget>();
	} else {
		goNext<PhoneWidget>();
	}
}

rpl::producer<QString> StartWidget::nextButtonText() const {
	return tr::lng_start_msgs();
}

} // namespace details
} // namespace Intro
