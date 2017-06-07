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
#include "boxes/edit_participant_box.h"

#include "lang/lang_keys.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/labels.h"
#include "styles/style_boxes.h"

EditParticipantBox::EditParticipantBox(QWidget*, gsl::not_null<ChannelData*> channel, gsl::not_null<UserData*> user) : BoxContent()
, _channel(channel)
, _user(user) {
}

void EditParticipantBox::resizeToContent() {
	auto newWidth = st::boxWideWidth;
	auto newHeight = 0;
	auto rowWidth = newWidth - st::boxPadding.left() - st::boxPadding.right();
	for (auto &&row : _rows) {
		row->resizeToNaturalWidth(rowWidth);
		newHeight += row->heightNoMargins();
	}
	if (!_rows.empty()) {
		newHeight += (_rows.size() - 1) * st::boxLittleSkip;
	}
	setDimensions(st::boxWideWidth, newHeight);
}

void EditParticipantBox::resizeEvent(QResizeEvent *e) {
	auto top = 0;
	for (auto &&row : _rows) {
		row->moveToLeft(st::boxPadding.left(), top);
		top += row->heightNoMargins() + st::boxLittleSkip;
	}
}

void EditParticipantBox::paintEvent(QPaintEvent *e) {
	Painter p(this);

	p.fillRect(e->rect(), st::boxBg);
}

EditAdminBox::EditAdminBox(QWidget*, gsl::not_null<ChannelData*> channel, gsl::not_null<UserData*> user, const MTPChannelAdminRights &rights, base::lambda<void(MTPChannelAdminRights)> callback) : EditParticipantBox(nullptr, channel, user)
, _rights(rights) {
}

void EditAdminBox::prepare() {
	setTitle(langFactory(lng_rights_edit_admin));

	addControl(object_ptr<Ui::FlatLabel>(this, lang(lng_rights_edit_admin_header), Ui::FlatLabel::InitType::Simple, st::boxLabel));

	auto addCheckbox = [this](Flag flag, const QString &text) {
		auto checked = (_rights.c_channelAdminRights().vflags.v & flag) != 0;
		auto control = addControl(object_ptr<Ui::Checkbox>(this, text, checked, st::defaultBoxCheckbox));
		_checkboxes.emplace(flag, control);
	};
	if (channel()->isMegagroup()) {
		addCheckbox(Flag::f_change_info, lang(lng_rights_group_info));
		addCheckbox(Flag::f_delete_messages, lang(lng_rights_delete));
		addCheckbox(Flag::f_ban_users, lang(lng_rights_group_ban));
		addCheckbox(Flag::f_invite_users, lang(lng_rights_group_invite));
		addCheckbox(Flag::f_invite_link, lang(lng_rights_group_invite_link));
		addCheckbox(Flag::f_pin_messages, lang(lng_rights_group_pin));
		addCheckbox(Flag::f_add_admins, lang(lng_rights_add_admins));
	} else {
		addCheckbox(Flag::f_change_info, lang(lng_rights_channel_info));
		addCheckbox(Flag::f_post_messages, lang(lng_rights_channel_post));
		addCheckbox(Flag::f_edit_messages, lang(lng_rights_channel_edit));
		addCheckbox(Flag::f_delete_messages, lang(lng_rights_delete));
		addCheckbox(Flag::f_add_admins, lang(lng_rights_add_admins));
	}

	_aboutAddAdmins = addControl(object_ptr<Ui::FlatLabel>(this, st::boxLabel));
	auto addAdmins = _checkboxes.find(Flag::f_add_admins);
	t_assert(addAdmins != _checkboxes.end());
	connect(addAdmins->second, &Ui::Checkbox::changed, this, [this] {
		refreshAboutAddAdminsText();
	});
	refreshAboutAddAdminsText();

	resizeToContent();
}

void EditAdminBox::refreshAboutAddAdminsText() {
	auto addAdmins = _checkboxes.find(Flag::f_add_admins);
	t_assert(addAdmins != _checkboxes.end());
	_aboutAddAdmins->setText(lang(addAdmins->second->checked() ? lng_rights_about_add_admins_yes : lng_rights_about_add_admins_no));

	resizeToContent();
}

EditRestrictedBox::EditRestrictedBox(QWidget*, gsl::not_null<ChannelData*> channel, gsl::not_null<UserData*> user, const MTPChannelBannedRights &rights, base::lambda<void(MTPChannelBannedRights)> callback) : EditParticipantBox(nullptr, channel, user) {
}

void EditRestrictedBox::prepare() {
	setTitle(langFactory(lng_rights_user_restrictions));

	addControl(object_ptr<Ui::FlatLabel>(this, lang(lng_rights_edit_admin_header), Ui::FlatLabel::InitType::Simple, st::boxLabel));

	auto addCheckbox = [this](Flag flag, const QString &text) {
		auto checked = (_rights.c_channelBannedRights().vflags.v & flag) != 0;
		auto control = addControl(object_ptr<Ui::Checkbox>(this, text, checked, st::defaultBoxCheckbox));
		_checkboxes.emplace(flag, control);
	};
	addCheckbox(Flag::f_view_messages, lang(lng_rights_chat_read));
	addCheckbox(Flag::f_send_messages, lang(lng_rights_chat_send_text));
	addCheckbox(Flag::f_send_media, lang(lng_rights_chat_send_media));
	addCheckbox(Flag::f_send_stickers, lang(lng_rights_chat_send_stickers));
	addCheckbox(Flag::f_embed_links, lang(lng_rights_chat_send_links));
	resizeToContent();
}
