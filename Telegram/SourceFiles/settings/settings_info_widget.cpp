/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/settings_info_widget.h"

#include "styles/style_settings.h"
#include "lang/lang_keys.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/slide_wrap.h"
#include "boxes/username_box.h"
#include "boxes/add_contact_box.h"
#include "boxes/change_phone_box.h"
#include "data/data_session.h"
#include "observer_peer.h"
#include "messenger.h"
#include "auth_session.h"

namespace Settings {

using UpdateFlag = Notify::PeerUpdate::Flag;

InfoWidget::InfoWidget(QWidget *parent, UserData *self) : BlockWidget(parent, self, lang(lng_settings_section_info)) {
	auto observeEvents = UpdateFlag::UsernameChanged | UpdateFlag::UserPhoneChanged | UpdateFlag::AboutChanged;
	subscribe(Notify::PeerUpdated(), Notify::PeerUpdatedHandler(observeEvents, [this](const Notify::PeerUpdate &update) {
		notifyPeerUpdated(update);
	}));

	createControls();
}

void InfoWidget::createControls() {
	style::margins margin(0, 0, 0, 0);
	style::margins slidedPadding(0, 0, 0, 0);
	createChildRow(_mobileNumber, margin, slidedPadding, st::settingsBlockOneLineTextPart);
	createChildRow(_username, margin, slidedPadding, st::settingsBlockOneLineTextPart);
	createChildRow(_bio, margin, slidedPadding, st::settingsBioValue);
	refreshControls();
}

void InfoWidget::refreshControls() {
	refreshMobileNumber();
	refreshUsername();
	refreshBio();
}

void InfoWidget::refreshMobileNumber() {
	TextWithEntities phoneText;
	if (const auto user = self()->asUser()) {
		phoneText.text = Auth().data().findContactPhone(user);
	}
	setLabeledText(
		_mobileNumber,
		lang(lng_profile_mobile_number),
		phoneText,
		TextWithEntities(),
		lang(lng_profile_copy_phone));
	if (auto text = _mobileNumber->entity()->textLabel()) {
		text->setRichText(textcmdLink(1, phoneText.text));
		text->setLink(1, std::make_shared<LambdaClickHandler>([] {
			Ui::show(Box<ChangePhoneBox>());
		}));
	}
}

void InfoWidget::refreshUsername() {
	TextWithEntities usernameText;
	QString copyText;
	if (self()->username.isEmpty()) {
		usernameText.text = lang(lng_settings_choose_username);
	} else {
		usernameText.text = '@' + self()->username;
		copyText = lang(lng_context_copy_mention);
	}
	usernameText.entities.push_back(EntityInText(
		EntityInTextCustomUrl,
		0,
		usernameText.text.size(),
		Messenger::Instance().createInternalLinkFull(
			self()->username)));
	setLabeledText(
		_username,
		lang(lng_profile_username),
		usernameText,
		TextWithEntities(),
		copyText);
	if (auto text = _username->entity()->textLabel()) {
		text->setClickHandlerHook([](const ClickHandlerPtr &handler, Qt::MouseButton button) {
			Ui::show(Box<UsernameBox>());
			return false;
		});
	}
}

void InfoWidget::refreshBio() {
	TextWithEntities bioText;
	auto aboutText = self()->about();
	if (self()->about().isEmpty()) {
		bioText.text = lang(lng_settings_empty_bio);
	} else {
		bioText.text = aboutText;
	}
	bioText.entities.push_back(EntityInText(
		EntityInTextCustomUrl,
		0,
		bioText.text.size(),
		QString("internal:edit_bio")));
	setLabeledText(
		_bio,
		lang(lng_profile_bio),
		bioText,
		TextWithEntities(),
		QString());
	if (auto text = _bio->entity()->textLabel()) {
		text->setClickHandlerHook([](const ClickHandlerPtr &handler, Qt::MouseButton button) {
			Ui::show(Box<EditBioBox>(App::self()));
			return false;
		});
	}
}

void InfoWidget::setLabeledText(
		LabeledWrap *row,
		const QString &label,
		const TextWithEntities &textWithEntities,
		const TextWithEntities &shortTextWithEntities,
		const QString &copyText) {
	auto nonEmptyText = !textWithEntities.text.isEmpty();
	if (nonEmptyText) {
		row->entity()->setLabeledText(
			label,
			textWithEntities,
			shortTextWithEntities,
			copyText,
			width());
	}
	row->toggle(nonEmptyText, anim::type::normal);
}

InfoWidget::LabeledWidget::LabeledWidget(QWidget *parent, const style::FlatLabel &valueSt) : RpWidget(parent)
, _valueSt(valueSt) {
}

void InfoWidget::LabeledWidget::setLabeledText(
		const QString &label,
		const TextWithEntities &textWithEntities,
		const TextWithEntities &shortTextWithEntities,
		const QString &copyText,
		int availableWidth) {
	_label.destroy();
	_text.destroy();
	_shortText.destroy();
	if (textWithEntities.text.isEmpty()) return;

	_label.create(this, label, Ui::FlatLabel::InitType::Simple, st::settingsBlockLabel);
	_label->show();
	setLabelText(_text, textWithEntities, copyText);
	setLabelText(_shortText, shortTextWithEntities, copyText);
	resizeToNaturalWidth(availableWidth);
}

Ui::FlatLabel *InfoWidget::LabeledWidget::textLabel() const {
	return _text;
}

Ui::FlatLabel *InfoWidget::LabeledWidget::shortTextLabel() const {
	return _shortText;
}

void InfoWidget::LabeledWidget::setLabelText(object_ptr<Ui::FlatLabel> &text, const TextWithEntities &textWithEntities, const QString &copyText) {
	text.destroy();
	if (textWithEntities.text.isEmpty()) return;

	text.create(this, QString(), Ui::FlatLabel::InitType::Simple, _valueSt);
	text->show();
	text->setMarkedText(textWithEntities);
	text->setContextCopyText(copyText);
	text->setSelectable(true);
	text->setDoubleClickSelectsParagraph(true);
}

void InfoWidget::notifyPeerUpdated(const Notify::PeerUpdate &update) {
	if (update.peer != self()) {
		return;
	}

	if (update.flags & UpdateFlag::UsernameChanged) {
		refreshUsername();
	}
	if (update.flags & (UpdateFlag::UserPhoneChanged)) {
		refreshMobileNumber();
	}
	if (update.flags & UpdateFlag::AboutChanged) {
		refreshBio();
	}
}

int InfoWidget::LabeledWidget::naturalWidth() const {
	if (!_text) return -1;
	return _label->naturalWidth() + st::normalFont->spacew + _text->naturalWidth();
}

int InfoWidget::LabeledWidget::resizeGetHeight(int newWidth) {
	if (!_label) return 0;

	_label->moveToLeft(
		0,
		st::settingsBlockOneLineTextPart.margin.top(),
		newWidth);
	_label->resizeToNaturalWidth(newWidth);

	int textLeft = _label->width() + st::normalFont->spacew;
	int textWidth = _text->naturalWidth();
	int availableWidth = newWidth - textLeft;
	bool doesNotFit = (textWidth > availableWidth);
	accumulate_min(textWidth, availableWidth);
	accumulate_min(textWidth, st::msgMaxWidth);
	if (textWidth < 0) {
		textWidth = 0;
	}
	_text->resizeToWidth(textWidth);
	_text->moveToLeft(
		textLeft,
		st::settingsBlockOneLineTextPart.margin.top(),
		newWidth);
	if (_shortText) {
		_shortText->resizeToWidth(textWidth);
		_shortText->moveToLeft(
			textLeft,
			st::settingsBlockOneLineTextPart.margin.top(),
			newWidth);
		if (doesNotFit) {
			_shortText->show();
			_text->hide();
		} else {
			_shortText->hide();
			_text->show();
		}
	}
	return st::settingsBlockOneLineTextPart.margin.top()
		+ qMax(_label->heightNoMargins(), _text->heightNoMargins())
		+ st::settingsBlockOneLineTextPart.margin.bottom();
}

} // namespace Settings
