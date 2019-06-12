/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/peers/edit_contact_box.h"

#include "boxes/generic_box.h"
#include "data/data_user.h"
#include "data/data_session.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/input_fields.h"
#include "ui/text/text_utilities.h"
#include "info/profile/info_profile_cover.h"
#include "lang/lang_keys.h"
#include "window/window_controller.h"
#include "ui/toast/toast.h"
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

class Builder {
public:
	Builder(
		not_null<GenericBox*> box,
		not_null<Window::Controller*> window,
		not_null<UserData*> user);

	void build();

private:
	void setupContent();
	void setupCover();
	void setupNameFields();
	void setupWarning();
	void initNameFields(
		not_null<Ui::InputField*> first,
		not_null<Ui::InputField*> last,
		bool inverted);

	not_null<GenericBox*> _box;
	not_null<Window::Controller*> _window;
	not_null<UserData*> _user;
	QString _phone;
	Fn<void()> _focus;
	Fn<void()> _save;

};

Builder::Builder(
	not_null<GenericBox*> box,
	not_null<Window::Controller*> window,
	not_null<UserData*> user)
: _box(box)
, _window(window)
, _user(user)
, _phone(UserPhone(user)) {
}

void Builder::build() {
	const auto box = _box;

	setupContent();

	box->setTitle(langFactory(_user->isContact()
		? lng_edit_contact_title
		: lng_enter_contact_data));

	box->addButton(langFactory(lng_box_done), _save);
	box->addButton(langFactory(lng_cancel), [=] { box->closeBox(); });
	box->setFocusCallback(_focus);
}

void Builder::setupContent() {
	setupCover();
	setupNameFields();
	setupWarning();
}

void Builder::setupCover() {
	_box->addRow(
		object_ptr<Info::Profile::Cover>(
			_box,
			_user,
			_window->sessionController(),
			(_phone.isEmpty()
				? Lang::Viewer(lng_contact_mobile_hidden)
				: rpl::single(App::formatPhone(_phone)))),
		style::margins())->setAttribute(Qt::WA_TransparentForMouseEvents);
}

void Builder::setupNameFields() {
	const auto inverted = langFirstNameGoesSecond();
	const auto first = _box->addRow(
		object_ptr<Ui::InputField>(
			_box,
			st::defaultInputField,
			langFactory(lng_signup_firstname),
			_user->firstName),
		st::addContactFieldMargin);
	auto preparedLast = object_ptr<Ui::InputField>(
		_box,
		st::defaultInputField,
		langFactory(lng_signup_lastname),
		_user->lastName);
	const auto last = inverted
		? _box->insertRow(
			_box->rowsCount() - 1,
			std::move(preparedLast),
			st::addContactFieldMargin)
		: _box->addRow(std::move(preparedLast), st::addContactFieldMargin);

	initNameFields(first, last, inverted);
}

void Builder::initNameFields(
		not_null<Ui::InputField*> first,
		not_null<Ui::InputField*> last,
		bool inverted) {
	const auto box = _box;
	const auto phone = _phone;
	const auto user = _user;
	const auto getValue = [](not_null<Ui::InputField*> field) {
		return TextUtilities::SingleLine(field->getLastText()).trimmed();
	};

	if (inverted) {
		box->setTabOrder(last, first);
	}
	const auto focus = [=] {
		const auto firstValue = getValue(first);
		const auto lastValue = getValue(last);
		const auto empty = firstValue.isEmpty() && lastValue.isEmpty();
		const auto focusFirst = (inverted != empty);
		(focusFirst ? first : last)->setFocusFast();
	};
	const auto save = [=] {
		const auto firstValue = getValue(first);
		const auto lastValue = getValue(last);
		const auto empty = firstValue.isEmpty() && lastValue.isEmpty();
		if (empty) {
			focus();
			(inverted ? last : first)->showError();
			return;
		}
		const auto wasContact = user->isContact();
		const auto weak = make_weak(box);
		user->session().api().request(MTPcontacts_AddContact(
			MTP_flags(0),
			user->inputUser,
			MTP_string(firstValue),
			MTP_string(lastValue),
			MTP_string(phone)
		)).done([=](const MTPUpdates &result) {
			user->setName(
				firstValue,
				lastValue,
				user->nameOrPhone,
				user->username);
			user->session().api().applyUpdates(result);
			if (const auto settings = user->settings()) {
				using Flag = MTPDpeerSettings::Flag;
				const auto flags = Flag::f_add_contact
					| Flag::f_block_contact
					| Flag::f_report_spam;
				user->setSettings(*settings & ~flags);
			}
			if (weak) {
				weak->closeBox();
			}
			if (!wasContact) {
				Ui::Toast::Show(
					lng_new_contact_add_done(lt_user, firstValue));
			}
		}).fail([=](const RPCError &error) {
		}).send();
	};
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
	QObject::connect(first, &Ui::InputField::submitted, [=] { submit(); });
	QObject::connect(last, &Ui::InputField::submitted, [=] { submit(); });

	_focus = focus;
	_save = save;
}

void Builder::setupWarning() {
	if (_user->isContact()) {
		return;
	}
	const auto name = _user->shortName();
	const auto nameWithEntities = TextWithEntities{ name };
	const auto text = _phone.isEmpty()
		? lng_contact_phone_after__generic<TextWithEntities>(
			lt_user,
			nameWithEntities,
			lt_visible,
			Ui::Text::Bold(lang(lng_contact_phone_visible)),
			lt_name,
			nameWithEntities)
		: lng_contact_phone_show__generic<TextWithEntities>(
			lt_button,
			Ui::Text::Bold(lang(lng_box_done).toUpper()),
			lt_user,
			TextWithEntities{ name });
	_box->addRow(
		object_ptr<Ui::FlatLabel>(
			_box,
			rpl::single(text),
			st::changePhoneLabel),
		st::addContactWarningMargin);
}

} // namespace

void EditContactBox(
		not_null<GenericBox*> box,
		not_null<Window::Controller*> window,
		not_null<UserData*> user) {
	Builder(box, window, user).build();
}
