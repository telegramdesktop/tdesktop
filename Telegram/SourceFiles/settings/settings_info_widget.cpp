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
#include "settings/settings_info_widget.h"

#include "styles/style_settings.h"
#include "lang/lang_keys.h"
#include "ui/widgets/labels.h"
#include "ui/effects/widget_slide_wrap.h"
#include "boxes/username_box.h"
#include "boxes/add_contact_box.h"
#include "boxes/change_phone_box.h"
#include "observer_peer.h"
#include "messenger.h"

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
	addChildRow(_mobileNumber, margin, slidedPadding, st::settingsBlockOneLineTextPart);
	addChildRow(_username, margin, slidedPadding, st::settingsBlockOneLineTextPart);
	addChildRow(_bio, margin, slidedPadding, st::settingsBioValue);
	refreshControls();
}

void InfoWidget::refreshControls() {
	refreshMobileNumber();
	refreshUsername();
	refreshBio();
}

void InfoWidget::refreshMobileNumber() {
	TextWithEntities phoneText;
	if (auto user = self()->asUser()) {
		if (!user->phone().isEmpty()) {
			phoneText.text = App::formatPhone(user->phone());
		} else {
			phoneText.text = App::phoneFromSharedContact(peerToUser(user->id));
		}
	}
	setLabeledText(_mobileNumber, lang(lng_profile_mobile_number), phoneText, TextWithEntities(), lang(lng_profile_copy_phone));
	if (auto text = _mobileNumber->entity()->textLabel()) {
		text->setRichText(textcmdLink(1, phoneText.text));
		text->setLink(1, MakeShared<LambdaClickHandler>([] {
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
	usernameText.entities.push_back(EntityInText(EntityInTextCustomUrl, 0, usernameText.text.size(), Messenger::Instance().createInternalLinkFull(self()->username)));
	setLabeledText(_username, lang(lng_profile_username), usernameText, TextWithEntities(), copyText);
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
	bioText.entities.push_back(EntityInText(EntityInTextCustomUrl, 0, bioText.text.size(), QString()));
	setLabeledText(_bio, lang(lng_profile_bio), bioText, TextWithEntities(), QString());
	if (auto text = _bio->entity()->textLabel()) {
		text->setClickHandlerHook([](const ClickHandlerPtr &handler, Qt::MouseButton button) {
			Ui::show(Box<EditBioBox>(App::self()));
			return false;
		});
	}
}

void InfoWidget::setLabeledText(object_ptr<LabeledWrap> &row, const QString &label, const TextWithEntities &textWithEntities, const TextWithEntities &shortTextWithEntities, const QString &copyText) {
	auto nonEmptyText = !textWithEntities.text.isEmpty();
	if (nonEmptyText) {
		row->entity()->setLabeledText(label, textWithEntities, shortTextWithEntities, copyText);
	}
	row->toggleAnimated(nonEmptyText);
}

InfoWidget::LabeledWidget::LabeledWidget(QWidget *parent, const style::FlatLabel &valueSt) : TWidget(parent)
, _valueSt(valueSt) {
}

void InfoWidget::LabeledWidget::setLabeledText(const QString &label, const TextWithEntities &textWithEntities, const TextWithEntities &shortTextWithEntities, const QString &copyText) {
	_label.destroy();
	_text.destroy();
	_shortText.destroy();
	if (textWithEntities.text.isEmpty()) return;

	_label.create(this, label, Ui::FlatLabel::InitType::Simple, st::settingsBlockLabel);
	_label->show();
	setLabelText(_text, textWithEntities, copyText);
	setLabelText(_shortText, shortTextWithEntities, copyText);
	resizeToWidth(width());
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

	contentSizeUpdated();
}

int InfoWidget::LabeledWidget::naturalWidth() const {
	if (!_text) return -1;
	return _label->naturalWidth() + st::normalFont->spacew + _text->naturalWidth();
}

int InfoWidget::LabeledWidget::resizeGetHeight(int newWidth) {
	int marginLeft = st::settingsBlockOneLineTextPart.margin.left();
	int marginRight = st::settingsBlockOneLineTextPart.margin.right();

	if (!_label) return 0;

	_label->moveToLeft(0, st::settingsBlockOneLineTextPart.margin.top(), newWidth);
	auto labelNatural = _label->naturalWidth();
	Assert(labelNatural >= 0);

	_label->resize(qMin(newWidth, labelNatural), _label->height());

	int textLeft = _label->width() + st::normalFont->spacew;
	int textWidth = _text->naturalWidth();
	int availableWidth = newWidth - textLeft;
	bool doesNotFit = (textWidth > availableWidth);
	accumulate_min(textWidth, availableWidth);
	accumulate_min(textWidth, st::msgMaxWidth);
	if (textWidth + marginLeft + marginRight < 0) {
		textWidth = -(marginLeft + marginRight);
	}
	_text->resizeToWidth(textWidth + marginLeft + marginRight);
	_text->moveToLeft(textLeft - marginLeft, 0, newWidth);
	if (_shortText) {
		_shortText->resizeToWidth(textWidth + marginLeft + marginRight);
		_shortText->moveToLeft(textLeft - marginLeft, 0, newWidth);
		if (doesNotFit) {
			_shortText->show();
			_text->hide();
		} else {
			_shortText->hide();
			_text->show();
		}
	}
	return st::settingsBlockOneLineTextPart.margin.top() + qMax(_label->height(), _text->height() - st::settingsBlockOneLineTextPart.margin.top() - st::settingsBlockOneLineTextPart.margin.bottom()) + st::settingsBlockOneLineTextPart.margin.bottom();
}

} // namespace Settings
