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
#include "ui/widgets/buttons.h"
#include "styles/style_boxes.h"
#include "boxes/calendar_box.h"

namespace {

constexpr auto kMaxRestrictDelayDays = 366;

template <typename CheckboxesMap, typename DependenciesMap>
void ApplyDependencies(CheckboxesMap &checkboxes, DependenciesMap &dependencies, QPointer<Ui::Checkbox> changed) {
	auto checkAndApply = [&checkboxes](auto &&current, auto dependency, bool isChecked) {
		for (auto &&checkbox : checkboxes) {
			if ((checkbox.first & dependency) && (checkbox.second->checked() == isChecked)) {
				current->setChecked(isChecked);
				return true;
			}
		}
		return false;
	};
	auto applySomeDependency = [&checkboxes, &dependencies, &changed, checkAndApply] {
		auto result = false;
		for (auto &&entry : checkboxes) {
			if (entry.second == changed) {
				continue;
			}
			auto isChecked = entry.second->checked();
			for (auto &&dependency : dependencies) {
				if (entry.first & (isChecked ? dependency.first : dependency.second)) {
					if (checkAndApply(entry.second, (isChecked ? dependency.second : dependency.first), !isChecked)) {
						result = true;
						break;
					}
				}
			}
		}
		return result;
	};

	while (true) {
		if (!applySomeDependency()) {
			break;
		}
	};
}

} // namespace

class EditParticipantBox::Inner : public TWidget {
public:
	Inner(QWidget *parent, gsl::not_null<ChannelData*> channel, gsl::not_null<UserData*> user, bool hasAdminRights) : TWidget(parent)
	, _channel(channel)
	, _user(user)
	, _hasAdminRights(hasAdminRights) {
	}

	template <typename Widget>
	QPointer<Widget> addControl(object_ptr<Widget> row) {
		row->setParent(this);
		_rows.push_back(std::move(row));
		return static_cast<Widget*>(_rows.back().data());
	}

protected:
	int resizeGetHeight(int newWidth) override;
	void resizeEvent(QResizeEvent *e) override;
	void paintEvent(QPaintEvent *e) override;

private:
	gsl::not_null<ChannelData*> _channel;
	gsl::not_null<UserData*> _user;
	bool _hasAdminRights = false;
	std::vector<object_ptr<TWidget>> _rows;

};

int EditParticipantBox::Inner::resizeGetHeight(int newWidth) {
	auto newHeight = st::contactsPhotoSize + st::contactsPadding.bottom();
	auto rowWidth = newWidth - st::boxPadding.left() - st::boxPadding.right();
	for (auto &&row : _rows) {
		row->resizeToNaturalWidth(rowWidth);
		newHeight += row->heightNoMargins();
	}
	if (!_rows.empty()) {
		newHeight += (_rows.size() - 1) * st::boxLittleSkip;
	}
	return newHeight;
}

void EditParticipantBox::Inner::resizeEvent(QResizeEvent *e) {
	auto top = st::contactsPhotoSize + st::contactsPadding.bottom();
	for (auto &&row : _rows) {
		row->moveToLeft(st::boxPadding.left(), top);
		top += row->heightNoMargins() + st::boxLittleSkip;
	}
}

void EditParticipantBox::Inner::paintEvent(QPaintEvent *e) {
	Painter p(this);

	p.fillRect(e->rect(), st::boxBg);

	_user->paintUserpicLeft(p, st::boxPadding.left(), 0, width(), st::contactsPhotoSize);

	p.setPen(st::contactsNameFg);
	auto namex = st::contactsPadding.left() + st::contactsPhotoSize + st::contactsPadding.left();
	auto namew = width() - namex - st::contactsPadding.right();
	_user->nameText.drawLeftElided(p, namex, st::contactsNameTop, namew, width());
	auto statusText = [this] {
		if (_user->botInfo) {
			auto seesAllMessages = (_user->botInfo->readsAllHistory || _hasAdminRights);
			return lang(seesAllMessages ? lng_status_bot_reads_all : lng_status_bot_not_reads_all);
		}
		return App::onlineText(_user->onlineTill, unixtime());
	};
	p.setFont(st::contactsStatusFont);
	p.setPen(st::contactsStatusFg);
	p.drawTextLeft(namex, st::contactsStatusTop, width(), statusText());
}

EditParticipantBox::EditParticipantBox(QWidget*, gsl::not_null<ChannelData*> channel, gsl::not_null<UserData*> user, bool hasAdminRights) : BoxContent()
, _channel(channel)
, _user(user)
, _hasAdminRights(hasAdminRights) {
}

void EditParticipantBox::prepare() {
	_inner = setInnerWidget(object_ptr<Inner>(this, _channel, _user, hasAdminRights()));
}

template <typename Widget>
QPointer<Widget> EditParticipantBox::addControl(object_ptr<Widget> row) {
	Expects(_inner != nullptr);
	return _inner->addControl(std::move(row));
}

void EditParticipantBox::resizeToContent() {
	_inner->resizeToWidth(st::boxWideWidth);
	setDimensions(_inner->width(), _inner->height());
}

EditAdminBox::EditAdminBox(QWidget*, gsl::not_null<ChannelData*> channel, gsl::not_null<UserData*> user, const MTPChannelAdminRights &rights, base::lambda<void(MTPChannelAdminRights, MTPChannelAdminRights)> callback) : EditParticipantBox(nullptr, channel, user, (rights.c_channelAdminRights().vflags.v != 0))
, _oldRights(rights)
, _saveCallback(std::move(callback)) {
	auto dependency = [this](Flag dependent, Flag dependency) {
		_dependencies.push_back(std::make_pair(dependent, dependency));
	};
	dependency(Flag::f_invite_link, Flag::f_invite_users); // invite_link <-> invite_users
	dependency(Flag::f_invite_users, Flag::f_invite_link);
}

MTPChannelAdminRights EditAdminBox::DefaultRights(gsl::not_null<ChannelData*> channel) {
	auto defaultRights = channel->isMegagroup()
		? (Flag::f_change_info | Flag::f_delete_messages | Flag::f_ban_users | Flag::f_invite_users | Flag::f_invite_link | Flag::f_pin_messages)
		: (Flag::f_change_info | Flag::f_post_messages | Flag::f_edit_messages | Flag::f_delete_messages | Flag::f_invite_users | Flag::f_invite_link);
	return MTP_channelAdminRights(MTP_flags(defaultRights));
}

void EditAdminBox::prepare() {
	EditParticipantBox::prepare();

	setTitle(langFactory(lng_rights_edit_admin));

	addControl(object_ptr<Ui::FlatLabel>(this, lang(lng_rights_edit_admin_header), Ui::FlatLabel::InitType::Simple, st::boxLabel));

	auto prepareRights = (_oldRights.c_channelAdminRights().vflags.v ? _oldRights : DefaultRights(channel()));
	auto addCheckbox = [this, &prepareRights](Flags flags, const QString &text) {
		auto checked = (prepareRights.c_channelAdminRights().vflags.v & flags) != 0;
		auto control = addControl(object_ptr<Ui::Checkbox>(this, text, checked, st::rightsCheckbox, st::rightsToggle));
		subscribe(control->checkedChanged, [this, control](bool checked) {
			InvokeQueued(this, [this, control] { applyDependencies(control); });
		});
		if (!channel()->amCreator()) {
			if (!(channel()->adminRights().vflags.v & flags)) {
				control->setDisabled(true); // Grey out options that we don't have ourselves.
			}
		}
		_checkboxes.emplace(flags, control);
	};
	if (channel()->isMegagroup()) {
		addCheckbox(Flag::f_change_info, lang(lng_rights_group_info));
		addCheckbox(Flag::f_delete_messages, lang(lng_rights_group_delete));
		addCheckbox(Flag::f_ban_users, lang(lng_rights_group_ban));
		addCheckbox(Flag::f_invite_users | Flag::f_invite_link, lang(channel()->anyoneCanAddMembers() ? lng_rights_group_invite_link : lng_rights_group_invite));
		addCheckbox(Flag::f_pin_messages, lang(lng_rights_group_pin));
		addCheckbox(Flag::f_add_admins, lang(lng_rights_add_admins));
	} else {
		addCheckbox(Flag::f_change_info, lang(lng_rights_channel_info));
		addCheckbox(Flag::f_post_messages, lang(lng_rights_channel_post));
		addCheckbox(Flag::f_edit_messages, lang(lng_rights_channel_edit));
		addCheckbox(Flag::f_delete_messages, lang(lng_rights_channel_delete));
		addCheckbox(Flag::f_invite_users | Flag::f_invite_link, lang(lng_rights_group_invite));
		addCheckbox(Flag::f_add_admins, lang(lng_rights_add_admins));
	}

	auto addAdmins = _checkboxes.find(Flag::f_add_admins);
	if (addAdmins != _checkboxes.end()) {
		_aboutAddAdmins = addControl(object_ptr<Ui::FlatLabel>(this, st::boxLabel));
		t_assert(addAdmins != _checkboxes.end());
		subscribe(addAdmins->second->checkedChanged, [this](bool checked) {
			refreshAboutAddAdminsText();
		});
		refreshAboutAddAdminsText();
	}

	addButton(langFactory(lng_settings_save), [this] {
		if (!_saveCallback) {
			return;
		}
		auto newFlags = MTPDchannelAdminRights::Flags(0);
		for (auto &&checkbox : _checkboxes) {
			if (checkbox.second->checked()) {
				newFlags |= checkbox.first;
			} else {
				newFlags &= ~checkbox.first;
			}
		}
		if (!channel()->amCreator()) {
			// Leave only rights that we have so we could save them.
			newFlags &= channel()->adminRights().vflags.v;
		}
		_saveCallback(_oldRights, MTP_channelAdminRights(MTP_flags(newFlags)));
	});
	addButton(langFactory(lng_cancel), [this] { closeBox(); });

	applyDependencies(nullptr);
	for (auto &&checkbox : _checkboxes) {
		checkbox.second->finishAnimations();
	}

	resizeToContent();
}

void EditAdminBox::applyDependencies(QPointer<Ui::Checkbox> changed) {
	ApplyDependencies(_checkboxes, _dependencies, changed);
}

void EditAdminBox::refreshAboutAddAdminsText() {
	auto addAdmins = _checkboxes.find(Flag::f_add_admins);
	t_assert(addAdmins != _checkboxes.end());
	_aboutAddAdmins->setText(lang(addAdmins->second->checked() ? lng_rights_about_add_admins_yes : lng_rights_about_add_admins_no));

	resizeToContent();
}

EditRestrictedBox::EditRestrictedBox(QWidget*, gsl::not_null<ChannelData*> channel, gsl::not_null<UserData*> user, bool hasAdminRights, const MTPChannelBannedRights &rights, base::lambda<void(MTPChannelBannedRights, MTPChannelBannedRights)> callback) : EditParticipantBox(nullptr, channel, user, hasAdminRights)
, _oldRights(rights)
, _saveCallback(std::move(callback)) {
	auto dependency = [this](Flag dependent, Flag dependency) {
		_dependencies.push_back(std::make_pair(dependent, dependency));
	};
	dependency(Flag::f_send_gifs, Flag::f_send_stickers); // stickers <-> gifs
	dependency(Flag::f_send_stickers, Flag::f_send_gifs);
	dependency(Flag::f_send_games, Flag::f_send_stickers); // stickers <-> games
	dependency(Flag::f_send_stickers, Flag::f_send_games);
	dependency(Flag::f_send_inline, Flag::f_send_stickers); // stickers <-> inline
	dependency(Flag::f_send_stickers, Flag::f_send_inline);
	dependency(Flag::f_send_stickers, Flag::f_send_media); // stickers -> send_media
	dependency(Flag::f_embed_links, Flag::f_send_media); // embed_links -> send_media
	dependency(Flag::f_send_media, Flag::f_send_messages); // send_media- > send_messages
	dependency(Flag::f_send_messages, Flag::f_view_messages); // send_messages -> view_messages
}

void EditRestrictedBox::prepare() {
	EditParticipantBox::prepare();

	setTitle(langFactory(lng_rights_user_restrictions));

	addControl(object_ptr<Ui::FlatLabel>(this, lang(lng_rights_user_restrictions_header), Ui::FlatLabel::InitType::Simple, st::boxLabel));

	auto prepareRights = (_oldRights.c_channelBannedRights().vflags.v ? _oldRights : DefaultRights(channel()));
	_until = prepareRights.c_channelBannedRights().vuntil_date.v;

	auto addCheckbox = [this, &prepareRights](Flags flags, const QString &text) {
		auto checked = (prepareRights.c_channelBannedRights().vflags.v & flags) == 0;
		auto control = addControl(object_ptr<Ui::Checkbox>(this, text, checked, st::rightsCheckbox, st::rightsToggle));
		subscribe(control->checkedChanged, [this, control](bool checked) {
			InvokeQueued(this, [this, control] { applyDependencies(control); });
		});
		_checkboxes.emplace(flags, control);
	};
	addCheckbox(Flag::f_view_messages, lang(lng_rights_chat_read));
	addCheckbox(Flag::f_send_messages, lang(lng_rights_chat_send_text));
	addCheckbox(Flag::f_send_media, lang(lng_rights_chat_send_media));
	addCheckbox(Flag::f_send_stickers | Flag::f_send_gifs | Flag::f_send_games | Flag::f_send_inline, lang(lng_rights_chat_send_stickers));
	addCheckbox(Flag::f_embed_links, lang(lng_rights_chat_send_links));

	_restrictUntil = addControl(object_ptr<Ui::LinkButton>(this, QString(), st::boxLinkButton));
	_restrictUntil->setClickedCallback([this] { showRestrictUntil(); });
	setRestrictUntil(_until);

	//addControl(object_ptr<Ui::LinkButton>(this, lang(lng_rights_chat_banned_block), st::boxLinkButton));

	addButton(langFactory(lng_settings_save), [this] {
		if (!_saveCallback) {
			return;
		}
		auto newFlags = MTPDchannelBannedRights::Flags(0);
		for (auto &&checkbox : _checkboxes) {
			if (checkbox.second->checked()) {
				newFlags &= ~checkbox.first;
			} else {
				newFlags |= checkbox.first;
			}
		}
		_saveCallback(_oldRights, MTP_channelBannedRights(MTP_flags(newFlags), MTP_int(_until)));
	});
	addButton(langFactory(lng_cancel), [this] { closeBox(); });

	applyDependencies(nullptr);
	for (auto &&checkbox : _checkboxes) {
		checkbox.second->finishAnimations();
	}

	resizeToContent();
}

void EditRestrictedBox::applyDependencies(QPointer<Ui::Checkbox> changed) {
	ApplyDependencies(_checkboxes, _dependencies, changed);
}

MTPChannelBannedRights EditRestrictedBox::DefaultRights(gsl::not_null<ChannelData*> channel) {
	auto defaultRights = Flag::f_send_messages | Flag::f_send_media | Flag::f_embed_links | Flag::f_send_stickers | Flag::f_send_gifs | Flag::f_send_games | Flag::f_send_inline;
	return MTP_channelBannedRights(MTP_flags(defaultRights), MTP_int(0));
}

void EditRestrictedBox::showRestrictUntil() {
	auto tomorrow = QDate::currentDate().addDays(1);
	auto highlighted = isUntilForever() ? tomorrow : date(_until).date();
	auto month = highlighted;
	_restrictUntilBox = Ui::show(Box<CalendarBox>(month, highlighted, [this](const QDate &date) { setRestrictUntil(static_cast<int>(QDateTime(date).toTime_t())); }), KeepOtherLayers);
	_restrictUntilBox->setMaxDate(QDate::currentDate().addDays(kMaxRestrictDelayDays));
	_restrictUntilBox->setMinDate(tomorrow);
	_restrictUntilBox->addLeftButton(langFactory(lng_rights_chat_banned_forever), [this] { setRestrictUntil(0); });
}

void EditRestrictedBox::setRestrictUntil(int32 until) {
	_until = until;
	if (_restrictUntilBox) {
		_restrictUntilBox->closeBox();
	}
	auto untilText = [this] {
		if (isUntilForever()) {
			return lang(lng_rights_chat_banned_forever);
		}
		return langDayOfMonthFull(date(_until).date());
	};
	_restrictUntil->setText(lng_rights_chat_banned_until(lt_when, untilText()));
}
