/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/peers/edit_participant_box.h"

#include "lang/lang_keys.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/buttons.h"
#include "ui/text_options.h"
#include "ui/special_buttons.h"
#include "boxes/calendar_box.h"
#include "boxes/peers/edit_peer_permissions_box.h"
#include "data/data_peer_values.h"
#include "data/data_channel.h"
#include "data/data_user.h"
#include "styles/style_boxes.h"

namespace {

constexpr auto kMaxRestrictDelayDays = 366;
constexpr auto kSecondsInDay = 24 * 60 * 60;
constexpr auto kSecondsInWeek = 7 * kSecondsInDay;

} // namespace

class EditParticipantBox::Inner : public TWidget {
public:
	Inner(
		QWidget *parent,
		not_null<ChannelData*> channel,
		not_null<UserData*> user,
		bool hasAdminRights);

	template <typename Widget>
	QPointer<Widget> addControl(object_ptr<Widget> widget, QMargins margin);

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
	not_null<ChannelData*> channel,
	not_null<UserData*> user,
	bool hasAdminRights)
: TWidget(parent)
, _channel(channel)
, _user(user)
, _userPhoto(
	this,
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
	auto row = ranges::find(_rows, widget, &Control::widget);
	Assert(row != _rows.end());
	row->widget.destroy();
	_rows.erase(row);
}

template <typename Widget>
QPointer<Widget> EditParticipantBox::Inner::addControl(
		object_ptr<Widget> widget,
		QMargins margin) {
	doAddControl(std::move(widget), margin);
	return static_cast<Widget*>(_rows.back().widget.data());
}

void EditParticipantBox::Inner::doAddControl(
		object_ptr<TWidget> widget,
		QMargins margin) {
	widget->setParent(this);
	_rows.push_back({ std::move(widget), margin });
	_rows.back().widget->show();
}

int EditParticipantBox::Inner::resizeGetHeight(int newWidth) {
	_userPhoto->moveToLeft(
		st::rightsPhotoMargin.left(),
		st::rightsPhotoMargin.top());
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
	_userName.drawLeftElided(
		p,
		namex,
		st::rightsPhotoMargin.top() + st::rightsNameTop,
		namew,
		width());
	auto statusText = [this] {
		if (_user->botInfo) {
			const auto seesAllMessages = _user->botInfo->readsAllHistory
				|| _hasAdminRights;
			return lang(seesAllMessages
				? lng_status_bot_reads_all
				: lng_status_bot_not_reads_all);
		}
		return Data::OnlineText(_user->onlineTill, unixtime());
	};
	p.setFont(st::contactsStatusFont);
	p.setPen(st::contactsStatusFg);
	p.drawTextLeft(
		namex,
		st::rightsPhotoMargin.top() + st::rightsStatusTop,
		width(),
		statusText());
}

EditParticipantBox::EditParticipantBox(
	QWidget*,
	not_null<ChannelData*> channel,
	not_null<UserData*> user,
	bool hasAdminRights)
: _channel(channel)
, _user(user)
, _hasAdminRights(hasAdminRights) {
}

void EditParticipantBox::prepare() {
	_inner = setInnerWidget(object_ptr<Inner>(
		this,
		_channel,
		_user,
		hasAdminRights()));
}

template <typename Widget>
QPointer<Widget> EditParticipantBox::addControl(
		object_ptr<Widget> widget,
		QMargins margin) {
	Expects(_inner != nullptr);

	return _inner->addControl(std::move(widget), margin);
}

void EditParticipantBox::removeControl(QPointer<TWidget> widget) {
	Expects(_inner != nullptr);

	return _inner->removeControl(widget);
}

void EditParticipantBox::resizeToContent() {
	_inner->resizeToWidth(st::boxWideWidth);
	setDimensions(
		_inner->width(),
		qMin(_inner->height(), st::boxMaxListHeight));
}

EditAdminBox::EditAdminBox(
	QWidget*,
	not_null<ChannelData*> channel,
	not_null<UserData*> user,
	const MTPChatAdminRights &rights)
: EditParticipantBox(
	nullptr,
	channel,
	user,
	(rights.c_chatAdminRights().vflags.v != 0))
, _oldRights(rights) {
}

MTPChatAdminRights EditAdminBox::Defaults(not_null<ChannelData*> channel) {
	const auto defaultRights = channel->isMegagroup()
		? (Flag::f_change_info
			| Flag::f_delete_messages
			| Flag::f_ban_users
			| Flag::f_invite_users
			| Flag::f_pin_messages)
		: (Flag::f_change_info
			| Flag::f_post_messages
			| Flag::f_edit_messages
			| Flag::f_delete_messages
			| Flag::f_invite_users);
	return MTP_chatAdminRights(MTP_flags(defaultRights));
}

void EditAdminBox::prepare() {
	using namespace rpl::mappers;

	EditParticipantBox::prepare();

	auto hadRights = _oldRights.c_chatAdminRights().vflags.v;
	setTitle(langFactory(hadRights
		? lng_rights_edit_admin
		: lng_channel_add_admin));

	addControl(object_ptr<BoxContentDivider>(this), QMargins());

	const auto prepareRights = hadRights ? _oldRights : Defaults(channel());
	const auto filterByMyRights = canSave()
		&& !hadRights
		&& !channel()->amCreator();
	const auto prepareFlags = prepareRights.c_chatAdminRights().vflags.v
		& (filterByMyRights ? channel()->adminRights() : ~Flag(0));

	const auto disabledFlags = canSave()
		? (channel()->amCreator()
			? Flags(0)
			: ~channel()->adminRights())
		: ~Flags(0);

	auto [checkboxes, getChecked, changes] = CreateEditAdminRights(
		this,
		lng_rights_edit_admin_header,
		prepareFlags,
		disabledFlags,
		channel()->isMegagroup(),
		channel()->anyoneCanAddMembers());
	addControl(std::move(checkboxes), QMargins());

	_aboutAddAdmins = addControl(
		object_ptr<Ui::FlatLabel>(this, st::boxLabel),
		st::rightsAboutMargin);
	rpl::single(
		getChecked()
	) | rpl::then(std::move(
		changes
	)) | rpl::map(
		(_1 & Flag::f_add_admins) != 0
	) | rpl::distinct_until_changed(
	) | rpl::start_with_next([=](bool checked) {
		refreshAboutAddAdminsText(checked);
	}, lifetime());

	if (canSave()) {
		addButton(langFactory(lng_settings_save), [=, value = getChecked] {
			if (!_saveCallback) {
				return;
			}
			const auto newFlags = value()
				& (channel()->amCreator()
					? ~Flags(0)
					: channel()->adminRights());
			_saveCallback(
				_oldRights,
				MTP_chatAdminRights(MTP_flags(newFlags)));
		});
		addButton(langFactory(lng_cancel), [this] { closeBox(); });
	} else {
		addButton(langFactory(lng_box_ok), [this] { closeBox(); });
	}

	resizeToContent();
}

void EditAdminBox::refreshAboutAddAdminsText(bool canAddAdmins) {
	_aboutAddAdmins->setText([&] {
		if (!canSave()) {
			return lang(lng_rights_about_admin_cant_edit);
		} else if (canAddAdmins) {
			return lang(lng_rights_about_add_admins_yes);
		}
		return lang(lng_rights_about_add_admins_no);
	}());
	resizeToContent();
}

EditRestrictedBox::EditRestrictedBox(
	QWidget*,
	not_null<ChannelData*> channel,
	not_null<UserData*> user,
	bool hasAdminRights,
	const MTPChatBannedRights &rights)
: EditParticipantBox(nullptr, channel, user, hasAdminRights)
, _oldRights(rights) {
}

void EditRestrictedBox::prepare() {
	EditParticipantBox::prepare();

	setTitle(langFactory(lng_rights_user_restrictions));

	addControl(object_ptr<BoxContentDivider>(this), QMargins());

	const auto prepareRights = _oldRights.c_chatBannedRights().vflags.v
		? _oldRights
		: Defaults(channel());
	const auto disabledFlags = canSave() ? Flags(0) : ~Flags(0);

	auto [checkboxes, getChecked, changes] = CreateEditRestrictions(
		this,
		lng_rights_user_restrictions_header,
		prepareRights.c_chatBannedRights().vflags.v,
		disabledFlags);
	addControl(std::move(checkboxes), QMargins());

	_until = prepareRights.c_chatBannedRights().vuntil_date.v;
	addControl(object_ptr<BoxContentDivider>(this), st::rightsUntilMargin);
	addControl(
		object_ptr<Ui::FlatLabel>(
			this,
			lang(lng_rights_chat_banned_until_header),
			Ui::FlatLabel::InitType::Simple,
			st::rightsHeaderLabel),
		st::rightsHeaderMargin);
	setRestrictUntil(_until);

	//addControl(
	//	object_ptr<Ui::LinkButton>(
	//		this,
	//		lang(lng_rights_chat_banned_block),
	//		st::boxLinkButton));

	if (canSave()) {
		addButton(langFactory(lng_settings_save), [=, value = getChecked] {
			if (!_saveCallback) {
				return;
			}
			_saveCallback(
				_oldRights,
				MTP_chatBannedRights(
					MTP_flags(value()),
					MTP_int(getRealUntilValue())));
		});
		addButton(langFactory(lng_cancel), [=] { closeBox(); });
	} else {
		addButton(langFactory(lng_box_ok), [=] { closeBox(); });
	}

	resizeToContent();
}

MTPChatBannedRights EditRestrictedBox::Defaults(
		not_null<ChannelData*> channel) {
	const auto defaultRights = Flag::f_send_messages
		| Flag::f_send_media
		| Flag::f_embed_links
		| Flag::f_send_stickers
		| Flag::f_send_gifs
		| Flag::f_send_games
		| Flag::f_send_inline;
	return MTP_chatBannedRights(MTP_flags(defaultRights), MTP_int(0));
}

void EditRestrictedBox::showRestrictUntil() {
	auto tomorrow = QDate::currentDate().addDays(1);
	auto highlighted = isUntilForever()
		? tomorrow
		: ParseDateTime(getRealUntilValue()).date();
	auto month = highlighted;
	_restrictUntilBox = Ui::show(
		Box<CalendarBox>(
			month,
			highlighted,
			[this](const QDate &date) {
				setRestrictUntil(
					static_cast<int>(QDateTime(date).toTime_t()));
			}),
		LayerOption::KeepOther);
	_restrictUntilBox->setMaxDate(
		QDate::currentDate().addDays(kMaxRestrictDelayDays));
	_restrictUntilBox->setMinDate(tomorrow);
	_restrictUntilBox->addLeftButton(
		langFactory(lng_rights_chat_banned_forever),
		[=] { setRestrictUntil(0); });
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

bool EditRestrictedBox::isUntilForever() const {
	return ChannelData::IsRestrictedForever(_until);
}

void EditRestrictedBox::clearVariants() {
	for (auto &&widget : base::take(_untilVariants)) {
		removeControl(widget.data());
	}
}

void EditRestrictedBox::createUntilGroup() {
	_untilGroup = std::make_shared<Ui::RadiobuttonGroup>(
		isUntilForever() ? 0 : _until);
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
	auto addVariant = [&](int value, const QString &text) {
		if (!canSave() && _untilGroup->value() != value) {
			return;
		}
		_untilVariants.push_back(addControl(
			object_ptr<Ui::Radiobutton>(
				this,
				_untilGroup,
				value,
				text,
				st::defaultBoxCheckbox),
			st::rightsToggleMargin));
		if (!canSave()) {
			_untilVariants.back()->setDisabled(true);
		}
	};
	auto addCustomVariant = [&](TimeId until, TimeId from, TimeId to) {
		if (!ChannelData::IsRestrictedForever(until)
			&& until > from
			&& until <= to) {
			addVariant(
				until,
				lng_rights_chat_banned_custom_date(
					lt_date,
					langDayOfMonthFull(ParseDateTime(until).date())));
		}
	};
	auto addCurrentVariant = [&](TimeId from, TimeId to) {
		auto oldUntil = _oldRights.c_chatBannedRights().vuntil_date.v;
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
