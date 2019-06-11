/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/peers/add_to_contacts_box.h"

#include "data/data_user.h"
#include "data/data_session.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/input_fields.h"
#include "info/profile/info_profile_cover.h"
#include "lang/lang_keys.h"
#include "window/window_controller.h"
#include "auth_session.h"
#include "apiwrap.h"
#include "styles/style_boxes.h"
#include "styles/style_info.h"

namespace {

QString UserPhone(not_null<UserData*> user) {
	const auto phone = user->phone();
	return phone.isEmpty()
		? user->owner().findContactPhone(user->bareId())
		: phone;
}

TextWithEntities BoldText(const QString &text) {
	auto result = TextWithEntities{ text };
	result.entities.push_back({ EntityType::Bold, 0, text.size() });
	return result;
}

} // namespace

AddToContactsBox::AddToContactsBox(
	QWidget*,
	not_null<Window::Controller*> window,
	not_null<UserData*> user)
: _window(window)
, _user(user)
, _phone(UserPhone(user)) {
}

void AddToContactsBox::prepare() {
	setupContent();

	setTitle(langFactory(lng_enter_contact_data));

	addButton(langFactory(lng_box_done), [=] { _save(); });
	addButton(langFactory(lng_cancel), [=] { closeBox(); });
}

void AddToContactsBox::setInnerFocus() {
	_focus();
}

void AddToContactsBox::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

	setupCover(content);
	setupNameFields(content);
	setupWarning(content);

	setDimensionsToContent(st::boxWidth, content);
}

void AddToContactsBox::setupCover(not_null<Ui::VerticalLayout*> container) {
	container->add(object_ptr<Info::Profile::Cover>(
		container,
		_user,
		_window->sessionController(),
		(_phone.isEmpty()
			? Lang::Viewer(lng_contact_mobile_hidden)
			: rpl::single(App::formatPhone(_phone))))
	)->setAttribute(Qt::WA_TransparentForMouseEvents);
}

void AddToContactsBox::setupNameFields(
		not_null<Ui::VerticalLayout*> container) {
	const auto inverted = langFirstNameGoesSecond();
	const auto first = container->add(
		object_ptr<Ui::InputField>(
			container,
			st::defaultInputField,
			langFactory(lng_signup_firstname),
			_user->firstName),
		st::addContactFieldMargin);
	auto preparedLast = object_ptr<Ui::InputField>(
		container,
		st::defaultInputField,
		langFactory(lng_signup_lastname),
		_user->lastName);
	const auto last = inverted
		? container->insert(
			container->count() - 1,
			std::move(preparedLast),
			st::addContactFieldMargin)
		: container->add(std::move(preparedLast), st::addContactFieldMargin);

	initNameFields(first, last, inverted);
}

void AddToContactsBox::initNameFields(
		not_null<Ui::InputField*> first,
		not_null<Ui::InputField*> last,
		bool inverted) {
	if (inverted) {
		setTabOrder(last, first);
	}
	const auto submit = [=] {
		const auto firstValue = first->getLastText().trimmed();
		const auto lastValue = last->getLastText().trimmed();
		const auto empty = firstValue.isEmpty() && lastValue.isEmpty();
		if (inverted ? last->hasFocus() : empty) {
			first->setFocus();
		} else if (inverted ? empty : first->hasFocus()) {
			last->setFocus();
		} else {
			_save();
		}
	};
	connect(first, &Ui::InputField::submitted, [=] { submit(); });
	connect(last, &Ui::InputField::submitted, [=] { submit(); });

	_focus = [=] {
		const auto firstValue = first->getLastText().trimmed();
		const auto lastValue = last->getLastText().trimmed();
		const auto empty = firstValue.isEmpty() && lastValue.isEmpty();
		const auto focusFirst = (inverted != empty);
		(focusFirst ? first : last)->setFocusFast();
	};
	_save = [=] {
		const auto firstValue = first->getLastText().trimmed();
		const auto lastValue = last->getLastText().trimmed();
		const auto empty = firstValue.isEmpty() && lastValue.isEmpty();
		if (empty) {
			_focus();
			(inverted ? last : first)->showError();
			return;
		}
		const auto user = _user;
		const auto box = make_weak(this);
		user->session().api().request(MTPcontacts_AddContact(
			MTP_flags(0),
			user->inputUser,
			MTP_string(firstValue),
			MTP_string(lastValue),
			MTP_string(_phone)
		)).done([=](const MTPUpdates &result) {
			user->session().api().applyUpdates(result);
			if (const auto settings = user->settings()) {
				using Flag = MTPDpeerSettings::Flag;
				const auto flags = Flag::f_add_contact
					| Flag::f_block_contact
					| Flag::f_report_spam;
				user->setSettings(*settings & ~flags);
			}
			if (box) {
				box->closeBox();
			}
		}).fail([=](const RPCError &error) {
		}).send();
	};
}

void AddToContactsBox::setupWarning(
		not_null<Ui::VerticalLayout*> container) {
	const auto name = _user->firstName.isEmpty()
		? _user->lastName
		: _user->firstName;
	const auto nameWithEntities = TextWithEntities{ name };
	const auto text = _phone.isEmpty()
		? lng_contact_phone_after__generic<TextWithEntities>(
			lt_user,
			nameWithEntities,
			lt_visible,
			BoldText(lang(lng_contact_phone_visible)),
			lt_name,
			nameWithEntities)
		: lng_contact_phone_show__generic<TextWithEntities>(
			lt_button,
			BoldText(lang(lng_box_done).toUpper()),
			lt_user,
			TextWithEntities{ name });
	container->add(
		object_ptr<Ui::FlatLabel>(
			container,
			rpl::single(text),
			st::changePhoneLabel),
		st::addContactWarningMargin);
}