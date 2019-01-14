/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/peers/edit_participant_box.h"

#include "lang/lang_keys.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/buttons.h"
#include "ui/text_options.h"
#include "ui/special_buttons.h"
#include "boxes/calendar_box.h"
#include "boxes/peers/edit_peer_permissions_box.h"
#include "data/data_peer_values.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_user.h"
#include "styles/style_boxes.h"

namespace {

constexpr auto kMaxRestrictDelayDays = 366;
constexpr auto kSecondsInDay = 24 * 60 * 60;
constexpr auto kSecondsInWeek = 7 * kSecondsInDay;

} // namespace

class EditParticipantBox::Inner : public Ui::RpWidget {
public:
	Inner(
		QWidget *parent,
		not_null<PeerData*> peer,
		not_null<UserData*> user,
		bool hasAdminRights);

	template <typename Widget>
	Widget *addControl(object_ptr<Widget> widget, QMargins margin);

protected:
	int resizeGetHeight(int newWidth) override;
	void paintEvent(QPaintEvent *e) override;

private:
	not_null<PeerData*> _peer;
	not_null<UserData*> _user;
	object_ptr<Ui::UserpicButton> _userPhoto;
	Text _userName;
	bool _hasAdminRights = false;
	object_ptr<Ui::VerticalLayout> _rows;

};

EditParticipantBox::Inner::Inner(
	QWidget *parent,
	not_null<PeerData*> peer,
	not_null<UserData*> user,
	bool hasAdminRights)
: RpWidget(parent)
, _peer(peer)
, _user(user)
, _userPhoto(
	this,
	_user,
	Ui::UserpicButton::Role::Custom,
	st::rightsPhotoButton)
, _hasAdminRights(hasAdminRights)
, _rows(this) {
	_rows->heightValue(
	) | rpl::start_with_next([=] {
		resizeToWidth(width());
	}, lifetime());

	_userPhoto->setPointerCursor(false);
	_userName.setText(
		st::rightsNameStyle,
		App::peerName(_user),
		Ui::NameTextOptions());
}

template <typename Widget>
Widget *EditParticipantBox::Inner::addControl(
		object_ptr<Widget> widget,
		QMargins margin) {
	return _rows->add(std::move(widget), margin);
}

int EditParticipantBox::Inner::resizeGetHeight(int newWidth) {
	_userPhoto->moveToLeft(
		st::rightsPhotoMargin.left(),
		st::rightsPhotoMargin.top());
	const auto rowsTop = st::rightsPhotoMargin.top()
		+ st::rightsPhotoButton.size.height()
		+ st::rightsPhotoMargin.bottom();
	_rows->resizeToWidth(newWidth);
	_rows->moveToLeft(0, rowsTop, newWidth);
	return rowsTop + _rows->heightNoMargins();
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
	not_null<PeerData*> peer,
	not_null<UserData*> user,
	bool hasAdminRights)
: _peer(peer)
, _user(user)
, _hasAdminRights(hasAdminRights) {
}

void EditParticipantBox::prepare() {
	_inner = setInnerWidget(object_ptr<Inner>(
		this,
		_peer,
		_user,
		hasAdminRights()));
	setDimensionsToContent(st::boxWideWidth, _inner);
}

template <typename Widget>
Widget *EditParticipantBox::addControl(
		object_ptr<Widget> widget,
		QMargins margin) {
	Expects(_inner != nullptr);

	return _inner->addControl(std::move(widget), margin);
}

EditAdminBox::EditAdminBox(
	QWidget*,
	not_null<PeerData*> peer,
	not_null<UserData*> user,
	const MTPChatAdminRights &rights)
: EditParticipantBox(
	nullptr,
	peer,
	user,
	(rights.c_chatAdminRights().vflags.v != 0))
, _oldRights(rights) {
}

MTPChatAdminRights EditAdminBox::Defaults(not_null<PeerData*> peer) {
	const auto defaultRights = peer->isChat()
		? ChatData::DefaultAdminRights()
		: peer->isMegagroup()
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

	addControl(
		object_ptr<BoxContentDivider>(this),
		st::rightsDividerMargin);

	const auto chat = peer()->asChat();
	const auto channel = peer()->asChannel();
	const auto prepareRights = hadRights ? _oldRights : Defaults(peer());
	const auto disabledByDefaults = DisabledByDefaultRestrictions(peer());
	const auto filterByMyRights = canSave()
		&& !hadRights
		&& channel
		&& !channel->amCreator();
	const auto prepareFlags = disabledByDefaults
		| (prepareRights.c_chatAdminRights().vflags.v
			& (filterByMyRights ? channel->adminRights() : ~Flag(0)));

	const auto disabledFlags = canSave()
		? (disabledByDefaults
			| ((!channel || channel->amCreator())
				? Flags(0)
				: ~channel->adminRights()))
		: ~Flags(0);

	const auto anyoneCanAddMembers = chat
		? chat->anyoneCanAddMembers()
		: channel->anyoneCanAddMembers();
	auto [checkboxes, getChecked, changes] = CreateEditAdminRights(
		this,
		lng_rights_edit_admin_header,
		prepareFlags,
		disabledFlags,
		peer()->isChat() || peer()->isMegagroup(),
		anyoneCanAddMembers);
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
				& ((!channel || channel->amCreator())
					? ~Flags(0)
					: channel->adminRights());
			_saveCallback(
				_oldRights,
				MTP_chatAdminRights(MTP_flags(newFlags)));
		});
		addButton(langFactory(lng_cancel), [this] { closeBox(); });
	} else {
		addButton(langFactory(lng_box_ok), [this] { closeBox(); });
	}
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
}

EditRestrictedBox::EditRestrictedBox(
	QWidget*,
	not_null<PeerData*> peer,
	not_null<UserData*> user,
	bool hasAdminRights,
	const MTPChatBannedRights &rights)
: EditParticipantBox(nullptr, peer, user, hasAdminRights)
, _oldRights(rights) {
}

void EditRestrictedBox::prepare() {
	EditParticipantBox::prepare();

	setTitle(langFactory(lng_rights_user_restrictions));

	addControl(
		object_ptr<BoxContentDivider>(this),
		st::rightsDividerMargin);

	const auto chat = peer()->asChat();
	const auto channel = peer()->asChannel();
	const auto defaultRestrictions = chat
		? chat->defaultRestrictions()
		: channel->defaultRestrictions();
	const auto prepareRights = (_oldRights.c_chatBannedRights().vflags.v
		? _oldRights
		: Defaults(peer()));
	const auto prepareFlags = prepareRights.c_chatBannedRights().vflags.v
		| defaultRestrictions
		| ((channel && channel->isPublic())
			? (Flag::f_change_info | Flag::f_pin_messages)
			: Flags(0));
	const auto disabledFlags = canSave()
		? (defaultRestrictions
			| ((channel && channel->isPublic())
				? (Flag::f_change_info | Flag::f_pin_messages)
				: Flags(0)))
		: ~Flags(0);

	auto [checkboxes, getRestrictions, changes] = CreateEditRestrictions(
		this,
		lng_rights_user_restrictions_header,
		prepareFlags,
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
		const auto save = [=, value = getRestrictions] {
			if (!_saveCallback) {
				return;
			}
			_saveCallback(
				_oldRights,
				MTP_chatBannedRights(
					MTP_flags(value()),
					MTP_int(getRealUntilValue())));
		};
		addButton(langFactory(lng_settings_save), save);
		addButton(langFactory(lng_cancel), [=] { closeBox(); });
	} else {
		addButton(langFactory(lng_box_ok), [=] { closeBox(); });
	}
}

MTPChatBannedRights EditRestrictedBox::Defaults(not_null<PeerData*> peer) {
	return MTP_chatBannedRights(MTP_flags(0), MTP_int(0));
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
	_untilVariants.clear();
	createUntilGroup();
	createUntilVariants();
}

bool EditRestrictedBox::isUntilForever() const {
	return ChannelData::IsRestrictedForever(_until);
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
		_untilVariants.push_back(base::unique_qptr<Ui::Radiobutton>(
			addControl(
				object_ptr<Ui::Radiobutton>(
					this,
					_untilGroup,
					value,
					text,
					st::defaultBoxCheckbox),
				st::rightsToggleMargin)));
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
