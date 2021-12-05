/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/phone_banned_box.h"

#include "ui/boxes/confirm_box.h"
#include "core/click_handler_types.h" // UrlClickHandler
#include "base/qthelp_url.h" // qthelp::url_encode
#include "base/platform/base_platform_info.h"
#include "window/window_controller.h"
#include "lang/lang_keys.h"

namespace Ui {

namespace {

void SendToBannedHelp(const QString &phone) {
	const auto version = QString::fromLatin1(AppVersionStr)
		+ (cAlphaVersion()
			? qsl(" alpha %1").arg(cAlphaVersion())
			: (AppBetaVersion ? " beta" : ""));

	const auto subject = qsl("Banned phone number: ") + phone;

	const auto body = qsl("\
I'm trying to use my mobile phone number: ") + phone + qsl("\n\
But Telegram says it's banned. Please help.\n\
\n\
App version: ") + version + qsl("\n\
OS version: ") + ::Platform::SystemVersionPretty() + qsl("\n\
Locale: ") + ::Platform::SystemLanguage();

	const auto url = "mailto:?to="
		+ qthelp::url_encode("login@stel.com")
		+ "&subject="
		+ qthelp::url_encode(subject)
		+ "&body="
		+ qthelp::url_encode(body);

	UrlClickHandler::Open(url);
}

} // namespace

void ShowPhoneBannedError(
		not_null<Window::Controller*> controller,
		const QString &phone) {
	const auto box = std::make_shared<QPointer<Ui::BoxContent>>();
	const auto close = [=] {
		if (*box) {
			(*box)->closeBox();
		}
	};
	*box = controller->show(
		Box<Ui::ConfirmBox>(
			tr::lng_signin_banned_text(tr::now),
			tr::lng_box_ok(tr::now),
			tr::lng_signin_banned_help(tr::now),
			close,
			[=] { SendToBannedHelp(phone); close(); }),
		Ui::LayerOption::CloseOther);
}

} // namespace Ui
