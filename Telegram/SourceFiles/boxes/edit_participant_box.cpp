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
#include "ui/text_options.h"
#include "ui/special_buttons.h"
#include "boxes/calendar_box.h"
#include "data/data_peer_values.h"
#include "styles/style_boxes.h"

namespace {

constexpr auto kMaxRestrictDelayDays = 366;
constexpr auto kSecondsInDay = 24 * 60 * 60;
constexpr auto kSecondsInWeek = 7 * kSecondsInDay;

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
	Inner(
		QWidget *parent,
		not_null<Window::Controller*> controller,
		not_null<ChannelData*> channel,
		not_null<UserData*> user,
		bool hasAdminRights);

	template <typename Widget>
	QPointer<Widget> addControl(object_ptr<Widget> widget, QMargins margin) {
		doAddControl(std::move(widget), margin);
		return static_cast<Widget*>(_rows.back().widget.data());
	}

	void removeControl(QPointer<TWidget> widget);

protected:
	int resizeGetHeight(int newWidth) override;
	void paintEvent(QPaintEvent *e) override;

private:
	void doAddControl(object_ptr<TWidget> widget, QMargins margin);

	not_null<ChannelData*> _channel;
	not_null<UserData*> _user;
	object_ptr<Ui::UserpicButton> _userPhoto;
	Text _userName;
	bool _hasAdminRights = false;
	struct Control {
		object_ptr<TWidget> widget;
		QMargins margin;
	};
	std::vector<Control> _rows;

};

EditParticipantBox::Inner::Inner(
	QWidget *parent,
	not_null<Window::Controller*> controller,
	not_null<ChannelData*> channel,
	not_null<UserData*> user,
	bool hasAdminRights)
: TWidget(parent)
, _channel(channel)
, _user(user)
, _userPhoto(
	this,
	controller,
	_user,
	Ui::UserpicButton::Role::Custom,
	st::rightsPhotoButton)
, _hasAdminRights(hasAdminRights) {
	_userPhoto->setPointerCursor(false);
	_userName.setText(
		st::rightsNameStyle,
		App::peerName(_user),
		Ui::NameTextOptions());
}

void EditParticipantBox::Inner::removeControl(QPointer<TWidget> widget) {
	auto row = std::find_if(_rows.begin(), _rows.end(), [widget](auto &&row) {
		return (row.widget == widget);
	});
	Assert(row != _rows.end());
	row->widget.destroy();
	_rows.erase(row);
}

void EditParticipantBox::Inner::doAddControl(object_ptr<TWidget> widget, QMargins margin) {
	widget->setParent(this);
	_rows.push_back({ std::move(widget), margin });
	_rows.back().widget->show();
}

int EditParticipantBox::Inner::resizeGetHeight(int newWidth) {
	_userPhoto->moveToLeft(st::rightsPhotoMargin.left(), st::rightsPhotoMargin.top());
	auto newHeight = st::rightsPhotoMargin.top()
		+ st::rightsPhotoButton.size.height()
		+ st::rightsPhotoMargin.bottom();
	for (auto &&row : _rows) {
		auto rowWidth = newWidth - row.margin.left() - row.margin.right();
		newHeight += row.margin.top();
		row.widget->resizeToNaturalWidth(rowWidth);
		row.widget->moveToLeft(row.margin.left(), newHeight);
		newHeight += row.widget->heightNoMargins() + row.margin.bottom();
	}
	return newHeight;
}

void EditParticipantBox::Inner::paintEvent(QPaintEvent *e) {
	Painter p(this);

	p.fillRect(e->rect(), st::boxBg);

	p.setPen(st::contactsNameFg);
	auto namex = st::rightsPhotoMargin.left()
		+ st::rightsPhotoButton.size .width()
		+ st::rightsPhotoMargin.right();
	auto namew = width() - namex - st::rightsPhotoMargin.right();
	_userName.drawLeftElided(p, namex, st::rightsPhotoMargin.top() + st::rightsNameTop, namew, width());
	auto statusText = [this] {
		if (_user->botInfo) {
			auto seesAllMessages = (_user->botInfo->readsAllHistory || _hasAdminRights);
			return lang(seesAllMessages ? lng_status_bot_reads_all : lng_status_bot_not_reads_all);
		}
		return Data::OnlineText(_user->onlineTill, unixtime());
	};
	p.setFont(st::contactsStatusFont);
	p.setPen(st::contactsStatusFg);
	p.drawTextLeft(namex, st::rightsPhotoMargin.top() + st::rightsStatusTop, width(), statusText());
}

EditParticipantBox::EditParticipantBox(QWidget*, not_null<ChannelData*> channel, not_null<UserData*> user, bool hasAdminRights) : BoxContent()
, _channel(channel)
, _user(user)
, _hasAdminRights(hasAdminRights) {
}

void EditParticipantBox::prepare() {
	_inner = setInnerWidget(object_ptr<Inner>(
		this,
		controller(),
		_channel,
		_user,
		hasAdminRights()));
}

template <typename Widget>
QPointer<Widget> EditParticipantBox::addControl(object_ptr<Widget> widget, QMargins margin) {
	Expects(_inner != nullptr);
	return _inner->addControl(std::move(widget), margin);
}

void EditParticipantBox::removeControl(QPointer<TWidget> widget) {
	Expects(_inner != nullptr);
	return _inner->removeControl(widget);
}

void EditParticipantBox::resizeToContent() {
	_inner->resizeToWidth(st::boxWideWidth);
	setDimensions(_inner->width(), qMin(_inner->height(), st::boxMaxListHeight));
}

EditAdminBox::EditAdminBox(QWidget*, not_null<ChannelData*> channel, not_null<UserData*> user, const MTPChannelAdminRights &rights) : EditParticipantBox(nullptr, channel, user, (rights.c_channelAdminRights().vflags.v != 0))
, _oldRights(rights) {
	auto dependency = [this](Flag dependent, Flag dependency) {
		_dependencies.push_back(std::make_pair(dependent, dependency));
	};
	dependency(Flag::f_invite_link, Flag::f_invite_users); // invite_link <-> invite_users
	dependency(Flag::f_invite_users, Flag::f_invite_link);
}

MTPChannelAdminRights EditAdminBox::DefaultRights(not_null<ChannelData*> channel) {
	auto defaultRights = channel->isMegagroup()
		? (Flag::f_change_info | Flag::f_delete_messages | Flag::f_ban_users | Flag::f_invite_users | Flag::f_invite_link | Flag::f_pin_messages)
		: (Flag::f_change_info | Flag::f_post_messages | Flag::f_edit_messages | Flag::f_delete_messages | Flag::f_invite_users | Flag::f_invite_link);
	return MTP_channelAdminRights(MTP_flags(defaultRights));
}

void EditAdminBox::prepare() {
	EditParticipantBox::prepare();

	auto hadRights = _oldRights.c_channelAdminRights().vflags.v;
	setTitle(langFactory(hadRights ? lng_rights_edit_admin : lng_channel_add_admin));

	addControl(object_ptr<BoxContentDivider>(this), QMargins());
	addControl(object_ptr<Ui::FlatLabel>(this, lang(lng_rights_edit_admin_header), Ui::FlatLabel::InitType::Simple, st::rightsHeaderLabel), st::rightsHeaderMargin);

	auto prepareRights = (hadRights ? _oldRights : DefaultRights(channel()));
	auto addCheckbox = [this, &prepareRights](Flags flags, const QString &text) {
		auto checked = (prepareRights.c_channelAdminRights().vflags.v & flags) != 0;
		auto control = addControl(object_ptr<Ui::Checkbox>(this, text, checked, st::rightsCheckbox, st::rightsToggle), st::rightsToggleMargin);
		subscribe(control->checkedChanged, [this, control](bool checked) {
			InvokeQueued(this, [this, control] { applyDependencies(control); });
		});
		if (!channel()->amCreator()) {
			if (!(channel()->adminRights() & flags)) {
				control->setDisabled(true); // Grey out options that we don't have ourselves.
			}
		}
		if (!canSave()) {
			control->setDisabled(true);
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
		_aboutAddAdmins = addControl(object_ptr<Ui::FlatLabel>(this, st::boxLabel), st::rightsAboutMargin);
		Assert(addAdmins != _checkboxes.end());
		subscribe(addAdmins->second->checkedChanged, [this](bool checked) {
			refreshAboutAddAdminsText();
		});
		refreshAboutAddAdminsText();
	}

	if (canSave()) {
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
				newFlags &= channel()->adminRights();
			}
			_saveCallback(_oldRights, MTP_channelAdminRights(MTP_flags(newFlags)));
		});
		addButton(langFactory(lng_cancel), [this] { closeBox(); });
	} else {
		addButton(langFactory(lng_box_ok), [this] { closeBox(); });
	}

	applyDependencies(nullptr);
	for (auto &&checkbox : _checkboxes) {
		checkbox.second->finishAnimating();
	}

	resizeToContent();
}

void EditAdminBox::applyDependencies(QPointer<Ui::Checkbox> changed) {
	ApplyDependencies(_checkboxes, _dependencies, changed);
}

void EditAdminBox::refreshAboutAddAdminsText() {
	auto addAdmins = _checkboxes.find(Flag::f_add_admins);
	Assert(addAdmins != _checkboxes.end());
	auto text = [this, addAdmins] {
		if (!canSave()) {
			return lang(lng_rights_about_admin_cant_edit);
		} else if (addAdmins->second->checked()) {
			return lang(lng_rights_about_add_admins_yes);
		}
		return lang(lng_rights_about_add_admins_no);
	};
	_aboutAddAdmins->setText(text());
	resizeToContent();
}

EditRestrictedBox::EditRestrictedBox(QWidget*, not_null<ChannelData*> channel, not_null<UserData*> user, bool hasAdminRights, const MTPChannelBannedRights &rights) : EditParticipantBox(nullptr, channel, user, hasAdminRights)
, _oldRights(rights) {
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

	addControl(object_ptr<BoxContentDivider>(this), QMargins());
	addControl(object_ptr<Ui::FlatLabel>(this, lang(lng_rights_user_restrictions_header), Ui::FlatLabel::InitType::Simple, st::rightsHeaderLabel), st::rightsHeaderMargin);

	auto prepareRights = (_oldRights.c_channelBannedRights().vflags.v ? _oldRights : DefaultRights(channel()));
	_until = prepareRights.c_channelBannedRights().vuntil_date.v;

	auto addCheckbox = [this, &prepareRights](Flags flags, const QString &text) {
		auto checked = (prepareRights.c_channelBannedRights().vflags.v & flags) == 0;
		auto control = addControl(object_ptr<Ui::Checkbox>(this, text, checked, st::rightsCheckbox, st::rightsToggle), st::rightsToggleMargin);
		subscribe(control->checkedChanged, [this, control](bool checked) {
			InvokeQueued(this, [this, control] { applyDependencies(control); });
		});
		if (!canSave()) {
			control->setDisabled(true);
		}
		_checkboxes.emplace(flags, control);
	};
	addCheckbox(Flag::f_view_messages, lang(lng_rights_chat_read));
	addCheckbox(Flag::f_send_messages, lang(lng_rights_chat_send_text));
	addCheckbox(Flag::f_send_media, lang(lng_rights_chat_send_media));
	addCheckbox(Flag::f_send_stickers | Flag::f_send_gifs | Flag::f_send_games | Flag::f_send_inline, lang(lng_rights_chat_send_stickers));
	addCheckbox(Flag::f_embed_links, lang(lng_rights_chat_send_links));

	addControl(object_ptr<BoxContentDivider>(this), st::rightsUntilMargin);
	addControl(object_ptr<Ui::FlatLabel>(this, lang(lng_rights_chat_banned_until_header), Ui::FlatLabel::InitType::Simple, st::rightsHeaderLabel), st::rightsHeaderMargin);
	setRestrictUntil(_until);

	//addControl(object_ptr<Ui::LinkButton>(this, lang(lng_rights_chat_banned_block), st::boxLinkButton));

	if (canSave()) {
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
			_saveCallback(_oldRights, MTP_channelBannedRights(MTP_flags(newFlags), MTP_int(getRealUntilValue())));
		});
		addButton(langFactory(lng_cancel), [this] { closeBox(); });
	} else {
		addButton(langFactory(lng_box_ok), [this] { closeBox(); });
	}

	applyDependencies(nullptr);
	for (auto &&checkbox : _checkboxes) {
		checkbox.second->finishAnimating();
	}

	resizeToContent();
}

void EditRestrictedBox::applyDependencies(QPointer<Ui::Checkbox> changed) {
	ApplyDependencies(_checkboxes, _dependencies, changed);
}

MTPChannelBannedRights EditRestrictedBox::DefaultRights(not_null<ChannelData*> channel) {
	auto defaultRights = Flag::f_send_messages | Flag::f_send_media | Flag::f_embed_links | Flag::f_send_stickers | Flag::f_send_gifs | Flag::f_send_games | Flag::f_send_inline;
	return MTP_channelBannedRights(MTP_flags(defaultRights), MTP_int(0));
}

void EditRestrictedBox::showRestrictUntil() {
	auto tomorrow = QDate::currentDate().addDays(1);
	auto highlighted = isUntilForever() ? tomorrow : date(getRealUntilValue()).date();
	auto month = highlighted;
	_restrictUntilBox = Ui::show(
		Box<CalendarBox>(
			month,
			highlighted,
			[this](const QDate &date) {
				setRestrictUntil(static_cast<int>(QDateTime(date).toTime_t()));
			}),
		LayerOption::KeepOther);
	_restrictUntilBox->setMaxDate(QDate::currentDate().addDays(kMaxRestrictDelayDays));
	_restrictUntilBox->setMinDate(tomorrow);
	_restrictUntilBox->addLeftButton(langFactory(lng_rights_chat_banned_forever), [this] { setRestrictUntil(0); });
}

void EditRestrictedBox::setRestrictUntil(TimeId until) {
	_until = until;
	if (_restrictUntilBox) {
		_restrictUntilBox->closeBox();
	}
	clearVariants();
	createUntilGroup();
	createUntilVariants();
	resizeToContent();
}

void EditRestrictedBox::clearVariants() {
	for (auto &&widget : base::take(_untilVariants)) {
		removeControl(widget.data());
	}
}

void EditRestrictedBox::createUntilGroup() {
	_untilGroup = std::make_shared<Ui::RadiobuttonGroup>(isUntilForever() ? 0 : _until);
	_untilGroup->setChangedCallback([this](int value) {
		if (value == kUntilCustom) {
			_untilGroup->setValue(_until);
			showRestrictUntil();
		} else if (_until != value) {
			_until = value;
		}
	});
}

void EditRestrictedBox::createUntilVariants() {
	auto addVariant = [this](int value, const QString &text) {
		if (!canSave() && _untilGroup->value() != value) {
			return;
		}
		_untilVariants.push_back(addControl(object_ptr<Ui::Radiobutton>(this, _untilGroup, value, text, st::defaultBoxCheckbox), st::rightsToggleMargin));
		if (!canSave()) {
			_untilVariants.back()->setDisabled(true);
		}
	};
	auto addCustomVariant = [this, addVariant](TimeId until, TimeId from, TimeId to) {
		if (!ChannelData::IsRestrictedForever(until) && until > from && until <= to) {
			addVariant(until, lng_rights_chat_banned_custom_date(lt_date, langDayOfMonthFull(date(until).date())));
		}
	};
	auto addCurrentVariant = [this, addCustomVariant](TimeId from, TimeId to) {
		auto oldUntil = _oldRights.c_channelBannedRights().vuntil_date.v;
		if (oldUntil < _until) {
			addCustomVariant(oldUntil, from, to);
		}
		addCustomVariant(_until, from, to);
		if (oldUntil > _until) {
			addCustomVariant(oldUntil, from, to);
		}
	};
	addVariant(0, lang(lng_rights_chat_banned_forever));

	auto now = unixtime();
	auto nextDay = now + kSecondsInDay;
	auto nextWeek = now + kSecondsInWeek;
	addCurrentVariant(0, nextDay);
	addVariant(kUntilOneDay, lng_rights_chat_banned_day(lt_count, 1));
	addCurrentVariant(nextDay, nextWeek);
	addVariant(kUntilOneWeek, lng_rights_chat_banned_week(lt_count, 1));
	addCurrentVariant(nextWeek, INT_MAX);
	addVariant(kUntilCustom, lang(lng_rights_chat_banned_custom));
}

TimeId EditRestrictedBox::getRealUntilValue() const {
	Expects(_until != kUntilCustom);
	if (_until == kUntilOneDay) {
		return unixtime() + kSecondsInDay;
	} else if (_until == kUntilOneWeek) {
		return unixtime() + kSecondsInWeek;
	}
	Assert(_until >= 0);
	return _until;
}
