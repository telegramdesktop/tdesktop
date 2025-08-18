/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "intro/intro_email.h"

#include "intro/intro_code.h"
#include "intro/intro_code_input.h"
#include "lang/lang_keys.h"
#include "lottie/lottie_icon.h"
#include "settings/cloud_password/settings_cloud_password_common.h"
#include "settings/settings_common.h" // CreateLottieIcon.
#include "ui/rect.h"
#include "ui/vertical_list.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "window/window_session_controller.h"
#include "styles/style_intro.h"
#include "styles/style_settings.h"

namespace Intro {
namespace details {

EmailWidget::EmailWidget(
	QWidget *parent,
	not_null<Main::Account*> account,
	not_null<Data*> data)
: Step(parent, account, data)
, _inner(this) {
	const auto content = _inner.get();
	widthValue() | rpl::start_with_next([=](int w) {
		content->resizeToWidth(st::introNextButton.width);
		content->moveToLeft((w - content->width()) / 2, contentTop());
	}, content->lifetime());

	content->add(
		object_ptr<Ui::FlatLabel>(
			content,
			tr::lng_intro_email_setup_title(),
			st::introTitle),
		style::margins(),
		style::al_left);
	Ui::AddSkip(content, st::lineWidth * 2);
	content->add(
		object_ptr<Ui::FlatLabel>(
			content,
			tr::lng_settings_cloud_login_email_about(),
			st::introDescription),
		style::margins(),
		style::al_left);

	{
		const auto lottie = u"cloud_password/email"_q;
		const auto size = st::settingsCloudPasswordIconSize / 3 * 2;
		auto icon = Settings::CreateLottieIcon(
			content,
			{ .name = lottie, .sizeOverride = Size(size) },
			style::margins());
		content->add(std::move(icon.widget));
		_showFinished.events(
		) | rpl::start_with_next([animate = std::move(icon.animate)] {
			animate(anim::repeat::once);
		}, lifetime());
	}

	const auto newInput = Settings::CloudPassword::AddWrappedField(
		content,
		tr::lng_settings_cloud_login_email_placeholder(),
		QString());
	Ui::AddSkip(content);
	const auto error = Settings::CloudPassword::AddError(content, nullptr);
	newInput->changes() | rpl::start_with_next([=] {
		error->hide();
	}, newInput->lifetime());
	newInput->setText(getData()->email);
	if (newInput->hasText()) {
		newInput->selectAll();
	}
	_setFocus.events() | rpl::start_with_next([=] {
		newInput->setFocus();
	}, newInput->lifetime());

	_submitCallback = [=] {
		const auto send = [=](const QString &email) {
			getData()->email = email;

			const auto done = [=](int length, const QString &pattern) {
				_sentRequest = 0;
				getData()->codeLength = length;
				getData()->emailPattern = pattern;
				goNext<CodeWidget>();
			};
			const auto fail = [=](const QString &type) {
				_sentRequest = 0;

				newInput->setFocus();
				newInput->showError();
				newInput->selectAll();
				error->show();

				if (MTP::IsFloodError(type)) {
					error->setText(tr::lng_flood_error(tr::now));
				} else if (type == u"EMAIL_NOT_ALLOWED"_q) {
					error->setText(
						tr::lng_settings_error_email_not_alowed(tr::now));
				} else if (type == u"EMAIL_INVALID"_q) {
					error->setText(tr::lng_cloud_password_bad_email(tr::now));
				} else if (type == u"EMAIL_HASH_EXPIRED"_q) {
					// Show box?
					error->setText(Lang::Hard::EmailConfirmationExpired());
				} else {
					error->setText(Lang::Hard::ServerError());
				}
			};

			_sentRequest = api().request(MTPaccount_SendVerifyEmailCode(
				MTP_emailVerifyPurposeLoginSetup(
					MTP_string(getData()->phone),
					MTP_bytes(getData()->phoneHash)),
				MTP_string(email)
			)).done([=](const MTPaccount_SentEmailCode &result) {
				done(
					result.data().vlength().v,
					qs(result.data().vemail_pattern()));
			}).fail([=](const MTP::Error &error) {
				fail(error.type());
			}).send();
		};
		const auto newText = newInput->getLastText();
		if (newText.isEmpty()) {
			newInput->setFocus();
			newInput->showError();
		} else {
			send(newText);
		}
	};
}

void EmailWidget::submit() {
	if (_submitCallback) {
		_submitCallback();
	}
}

void EmailWidget::setInnerFocus() {
	_setFocus.fire({});
}

void EmailWidget::activate() {
	Step::activate();
	showChildren();
	setInnerFocus();
	_showFinished.fire({});
}

void EmailWidget::finished() {
	Step::finished();
	cancelled();
}

void EmailWidget::cancelled() {
	api().request(base::take(_sentRequest)).cancel();
}

} // namespace details
} // namespace Intro
