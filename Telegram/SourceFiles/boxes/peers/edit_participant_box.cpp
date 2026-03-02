/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/peers/edit_participant_box.h"

#include "boxes/peers/edit_tag_control.h"
#include "boxes/peers/edit_participants_box.h"
#include "history/view/history_view_message.h"
#include "lang/lang_keys.h"
#include "ui/controls/userpic_button.h"
#include "ui/vertical_list.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/box_content_divider.h"
#include "ui/layers/generic_box.h"
#include "ui/toast/toast.h"
#include "ui/text/text_utilities.h"
#include "ui/text/text_options.h"
#include "ui/painter.h"
#include "chat_helpers/emoji_suggestions_widget.h"
#include "settings/sections/settings_privacy_security.h"
#include "ui/boxes/choose_date_time.h"
#include "ui/boxes/confirm_box.h"
#include "boxes/peers/channel_ownership_transfer.h"
#include "boxes/peers/add_bot_to_chat_box.h"
#include "boxes/peers/edit_peer_permissions_box.h"
#include "boxes/peers/edit_peer_info_box.h"
#include "settings/settings_common.h"
#include "data/data_peer_values.h"
#include "data/data_channel.h"
#include "data/data_changes.h"
#include "data/data_chat.h"
#include "data/data_user.h"
#include "base/unixtime.h"
#include "apiwrap.h"
#include "main/main_session.h"
#include "styles/style_layers.h"
#include "styles/style_boxes.h"
#include "styles/style_info.h"
#include "styles/style_settings.h"

namespace {

constexpr auto kMaxRestrictDelayDays = 366;
constexpr auto kSecondsInDay = 24 * 60 * 60;
constexpr auto kSecondsInWeek = 7 * kSecondsInDay;

class Cover final : public Ui::FixedHeightWidget {
public:
	Cover(QWidget *parent, not_null<UserData*> user, bool hasAdminRights);

private:
	void setupChildGeometry();

	const style::InfoProfileCover &_st;
	const not_null<UserData*> _user;
	object_ptr<Ui::UserpicButton> _userpic;
	object_ptr<Ui::FlatLabel> _name = { nullptr };
	object_ptr<Ui::FlatLabel> _status = { nullptr };

};

Cover::Cover(
	QWidget *parent,
	not_null<UserData*> user,
	bool hasAdminRights)
: FixedHeightWidget(parent, st::infoEditContactCover.height)
, _st(st::infoEditContactCover)
, _user(user)
, _userpic(this, _user, _st.photo) {
	_userpic->setAttribute(Qt::WA_TransparentForMouseEvents);

	_name = object_ptr<Ui::FlatLabel>(this, _st.name);
	_name->setText(_user->name());

	const auto statusText = [&] {
		if (_user->isBot()) {
			const auto seesAllMessages = _user->botInfo->readsAllHistory
				|| hasAdminRights;
			return (seesAllMessages
				? tr::lng_status_bot_reads_all
				: tr::lng_status_bot_not_reads_all)(tr::now);
		}
		return Data::OnlineText(_user->lastseen(), base::unixtime::now());
	}();
	_status = object_ptr<Ui::FlatLabel>(this, _st.status);
	_status->setText(statusText);
	_status->setAttribute(Qt::WA_TransparentForMouseEvents);

	setupChildGeometry();
}

void Cover::setupChildGeometry() {
	widthValue(
	) | rpl::on_next([this](int newWidth) {
		_userpic->moveToLeft(_st.photoLeft, _st.photoTop, newWidth);
		auto nameWidth = newWidth - _st.nameLeft - _st.rightSkip;
		_name->resizeToNaturalWidth(nameWidth);
		_name->moveToLeft(_st.nameLeft, _st.nameTop, newWidth);
		auto statusWidth = newWidth - _st.statusLeft - _st.rightSkip;
		_status->resizeToNaturalWidth(statusWidth);
		_status->moveToLeft(_st.statusLeft, _st.statusTop, newWidth);
	}, lifetime());
}

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

	[[nodiscard]] not_null<Ui::VerticalLayout*> verticalLayout() const {
		return _rows;
	}

	void setCoverMode(bool enabled);

protected:
	int resizeGetHeight(int newWidth) override;
	void paintEvent(QPaintEvent *e) override;

private:
	not_null<PeerData*> _peer;
	not_null<UserData*> _user;
	object_ptr<Ui::UserpicButton> _userPhoto;
	Ui::Text::String _userName;
	bool _hasAdminRights = false;
	bool _coverMode = false;
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
, _userPhoto(this, _user, st::rightsPhotoButton)
, _hasAdminRights(hasAdminRights)
, _rows(this) {
	_rows->heightValue(
	) | rpl::on_next([=] {
		resizeToWidth(width());
	}, lifetime());

	_userPhoto->setAttribute(Qt::WA_TransparentForMouseEvents);
	_userName.setText(
		st::rightsNameStyle,
		_user->name(),
		Ui::NameTextOptions());
}

template <typename Widget>
Widget *EditParticipantBox::Inner::addControl(
		object_ptr<Widget> widget,
		QMargins margin) {
	return _rows->add(std::move(widget), margin);
}

void EditParticipantBox::Inner::setCoverMode(bool enabled) {
	_coverMode = enabled;
	if (enabled) {
		_userPhoto->hide();
	}
	resizeToWidth(width());
}

int EditParticipantBox::Inner::resizeGetHeight(int newWidth) {
	if (_coverMode) {
		_rows->resizeToWidth(newWidth);
		_rows->moveToLeft(0, 0, newWidth);
		return _rows->heightNoMargins();
	}
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

	if (_coverMode) {
		return;
	}

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
	const auto statusText = [&] {
		if (_user->isBot()) {
			const auto seesAllMessages = _user->botInfo->readsAllHistory
				|| _hasAdminRights;
			return (seesAllMessages
				? tr::lng_status_bot_reads_all
				: tr::lng_status_bot_not_reads_all)(tr::now);
		}
		return Data::OnlineText(_user->lastseen(), base::unixtime::now());
	}();
	p.setFont(st::contactsStatusFont);
	p.setPen(st::contactsStatusFg);
	p.drawTextLeft(
		namex,
		st::rightsPhotoMargin.top() + st::rightsStatusTop,
		width(),
		statusText);
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

not_null<Ui::VerticalLayout*> EditParticipantBox::verticalLayout() const {
	return _inner->verticalLayout();
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

bool EditParticipantBox::amCreator() const {
	if (const auto chat = _peer->asChat()) {
		return chat->amCreator();
	} else if (const auto channel = _peer->asChannel()) {
		return channel->amCreator();
	}
	Unexpected("Peer type in EditParticipantBox::Inner::amCreator.");
}

void EditParticipantBox::setCoverMode(bool enabled) {
	Expects(_inner != nullptr);

	_inner->setCoverMode(enabled);
}

EditAdminBox::EditAdminBox(
	QWidget*,
	not_null<PeerData*> peer,
	not_null<UserData*> user,
	ChatAdminRightsInfo rights,
	const QString &rank,
	TimeId promotedSince,
	UserData *by,
	std::optional<EditAdminBotFields> addingBot)
: EditParticipantBox(
	nullptr,
	peer,
	user,
	(rights.flags != 0))
, _oldRights(rights)
, _oldRank(rank)
, _promotedSince(promotedSince)
, _by(by)
, _addingBot(std::move(addingBot)) {
}

ChatAdminRightsInfo EditAdminBox::defaultRights() const {
	using Flag = ChatAdminRight;

	return peer()->isChat()
		? peer()->asChat()->defaultAdminRights(user())
		: peer()->isMegagroup()
		? ChatAdminRightsInfo{ (Flag::ChangeInfo
			| Flag::DeleteMessages
			| Flag::PostStories
			| Flag::EditStories
			| Flag::DeleteStories
			| Flag::BanUsers
			| Flag::InviteByLinkOrAdd
			| Flag::ManageTopics
			| Flag::PinMessages
			| Flag::ManageCall
			| Flag::ManageRanks) }
		: ChatAdminRightsInfo{ (Flag::ChangeInfo
			| Flag::PostMessages
			| Flag::EditMessages
			| Flag::DeleteMessages
			| Flag::PostStories
			| Flag::EditStories
			| Flag::DeleteStories
			| Flag::InviteByLinkOrAdd
			| Flag::ManageCall
			| Flag::ManageDirect
			| Flag::BanUsers) };
}

void EditAdminBox::prepare() {
	using namespace rpl::mappers;
	using Flag = ChatAdminRight;
	using Flags = ChatAdminRights;

	EditParticipantBox::prepare();

	setCoverMode(true);
	addControl(
		object_ptr<Cover>(this, user(), hasAdminRights()),
		style::margins());

	setTitle(_addingBot
		? (_addingBot->existing
			? tr::lng_rights_edit_admin()
			: tr::lng_bot_add_title())
		: _oldRights.flags
		? tr::lng_rights_edit_admin()
		: tr::lng_channel_add_admin());

	if (_addingBot
		&& !_addingBot->existing
		&& !peer()->isBroadcast()
		&& _saveCallback) {
		addControl(
			object_ptr<Ui::BoxContentDivider>(this),
			st::rightsDividerMargin / 2);
		_addAsAdmin = addControl(
			object_ptr<Ui::Checkbox>(
				this,
				tr::lng_bot_as_admin_check(tr::now),
				st::rightsCheckbox,
				std::make_unique<Ui::ToggleView>(
					st::rightsToggle,
					true)),
			st::rightsToggleMargin + (st::rightsDividerMargin / 2));
		_addAsAdmin->checkedChanges(
		) | rpl::on_next([=](bool checked) {
			_adminControlsWrap->toggle(checked, anim::type::normal);
			refreshButtons();
		}, _addAsAdmin->lifetime());
	}

	_adminControlsWrap = addControl(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			this,
			object_ptr<Ui::VerticalLayout>(this)));
	const auto inner = _adminControlsWrap->entity();

	if (_promotedSince) {
		const auto parsed = base::unixtime::parse(_promotedSince);
		const auto label = Ui::AddDividerText(
			inner,
			tr::lng_rights_about_by(
				lt_user,
				rpl::single(_by
					? tr::link(_by->name(), 1)
					: TextWithEntities{ QString::fromUtf8("\U0001F47B") }),
				lt_date,
				rpl::single(TextWithEntities{ langDateTimeFull(parsed) }),
				tr::marked));
		if (_by) {
			label->setLink(1, _by->createOpenLink());
		}
		Ui::AddSkip(inner);
	} else {
		Ui::AddDivider(inner);
		Ui::AddSkip(inner);
	}

	const auto chat = peer()->asChat();
	const auto channel = peer()->asChannel();
	const auto prepareRights = _addingBot
		? ChatAdminRightsInfo(_oldRights.flags | _addingBot->existing)
		: _oldRights.flags
		? _oldRights
		: defaultRights();
	const auto disabledByDefaults = (channel && !channel->isMegagroup())
		? ChatAdminRights()
		: DisabledByDefaultRestrictions(peer());
	const auto filterByMyRights = canSave()
		&& !_oldRights.flags
		&& channel
		&& !channel->amCreator();
	const auto prepareFlags = disabledByDefaults
		| (prepareRights.flags
			& (filterByMyRights ? channel->adminRights() : ~Flag(0)));

	const auto disabledMessages = [&] {
		auto result = base::flat_map<Flags, QString>();
		if (!canSave()) {
			result.emplace(
				~Flags(0),
				tr::lng_rights_about_admin_cant_edit(tr::now));
		} else {
			result.emplace(
				disabledByDefaults,
				tr::lng_rights_permission_for_all(tr::now));
			if (amCreator() && user()->isSelf()) {
				result.emplace(
					~Flag::Anonymous,
					tr::lng_rights_permission_cant_edit(tr::now));
			} else if (const auto channel = peer()->asChannel()) {
				if (!channel->amCreator()) {
					result.emplace(
						~channel->adminRights(),
						tr::lng_rights_permission_cant_edit(tr::now));
				}
			}
		}
		return result;
	}();

	const auto isGroup = chat || channel->isMegagroup();
	const auto anyoneCanAddMembers = chat
		? chat->anyoneCanAddMembers()
		: channel->anyoneCanAddMembers();
	const auto options = Data::AdminRightsSetOptions{
		.isGroup = isGroup,
		.isForum = peer()->isForum(),
		.anyoneCanAddMembers = anyoneCanAddMembers,
	};
	Ui::AddSubsectionTitle(inner, tr::lng_rights_edit_admin_header());
	auto [checkboxes, getChecked, changes, highlightWidget] = CreateEditAdminRights(
		inner,
		prepareFlags,
		disabledMessages,
		options);
	inner->add(std::move(checkboxes), QMargins());

	auto selectedFlags = rpl::single(
		getChecked()
	) | rpl::then(std::move(
		changes
	));

	const auto hasRank = canSave() && (chat || channel->isMegagroup());

	{
		const auto aboutAddAdminsInner = inner->add(
			object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
				inner,
				object_ptr<Ui::VerticalLayout>(inner)));
		const auto emptyAboutAddAdminsInner = inner->add(
			object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
				inner,
				object_ptr<Ui::VerticalLayout>(inner)));
		aboutAddAdminsInner->toggle(false, anim::type::instant);
		emptyAboutAddAdminsInner->toggle(false, anim::type::instant);
		Ui::AddSkip(emptyAboutAddAdminsInner->entity());
		if (hasRank) {
			Ui::AddDivider(emptyAboutAddAdminsInner->entity());
		}
		Ui::AddSkip(aboutAddAdminsInner->entity());
		Ui::AddDividerText(
			aboutAddAdminsInner->entity(),
			rpl::duplicate(
				selectedFlags
			) | rpl::map(
				(_1 & Flag::AddAdmins) != 0
			) | rpl::distinct_until_changed(
			) | rpl::map([=](bool canAddAdmins) -> rpl::producer<QString> {
				const auto empty = (amCreator() && user()->isSelf());
				aboutAddAdminsInner->toggle(!empty, anim::type::instant);
				emptyAboutAddAdminsInner->toggle(empty, anim::type::instant);
				if (empty) {
					return rpl::single(QString());
				} else if (!canSave()) {
					return tr::lng_rights_about_admin_cant_edit();
				} else if (canAddAdmins) {
					return tr::lng_rights_about_add_admins_yes();
				}
				return tr::lng_rights_about_add_admins_no();
			}) | rpl::flatten_latest());
	}

	if (canTransferOwnership()) {
		const auto allFlags = AdminRightsForOwnershipTransfer(options);
		setupTransferButton(
			inner,
			isGroup
		)->toggleOn(rpl::duplicate(
			selectedFlags
		) | rpl::map(
			((_1 & allFlags) == allFlags)
		))->setDuration(0);
	}

	if (canSave()) {
		if (hasRank) {
			const auto role = _oldRights.flags
				? LookupBadgeRole(peer(), user())
				: HistoryView::BadgeRole::Admin;
			_tagControl = inner->add(
				object_ptr<EditTagControl>(
					inner,
					&peer()->session(),
					user(),
					_oldRank,
					role),
				style::margins());
		}
		if (_tagControl) {
			Ui::AddSkip(inner);
			Ui::AddDividerText(
				inner,
				user()->isSelf()
					? tr::lng_rights_tag_about_self()
					: tr::lng_rights_tag_about(
						lt_name,
						rpl::single(user()->shortName())));
		}
		if (_oldRights.flags && !_addingBot) {
			const auto isTargetCreator = (LookupBadgeRole(peer(), user())
				== HistoryView::BadgeRole::Creator);
			if (!isTargetCreator) {
				if (!_tagControl) {
					Ui::AddSkip(inner);
					inner->add(
						object_ptr<Ui::BoxContentDivider>(inner),
						{ 0, st::infoProfileSkip, 0, st::infoProfileSkip });
				}
				Ui::AddSkip(inner);
				const auto dismissButton = Settings::AddButtonWithIcon(
					inner,
					tr::lng_rights_dismiss_admin(),
					st::settingsAttentionButton,
					{ nullptr });
				dismissButton->setClickedCallback([=] {
					if (const auto callback = _saveCallback) {
						const auto old = _oldRights;
						const auto confirmed = [=](Fn<void()> close) {
							callback(old, {}, {});
							close();
						};
						uiShow()->showBox(
							Ui::MakeConfirmBox({
								.text = tr::lng_profile_sure_remove_admin(
									tr::now,
									lt_user,
									user()->firstName),
								.confirmed = confirmed,
								.confirmText = tr::lng_box_remove(),
							}));
					}
				});
			}
		}
		_finishSave = [=, value = getChecked] {
			const auto newFlags = (value() | ChatAdminRight::Other)
				& ((!channel || channel->amCreator())
					? ~Flags(0)
					: channel->adminRights());
			_saveCallback(
				_oldRights,
				ChatAdminRightsInfo(newFlags),
				_tagControl
					? std::optional<QString>(_tagControl->currentRank())
					: std::nullopt);
		};
		_save = [=] {
			const auto show = uiShow();
			if (!_saveCallback) {
				return;
			} else if (_addAsAdmin && !_addAsAdmin->checked()) {
				const auto weak = base::make_weak(this);
				AddBotToGroup(show, user(), peer(), _addingBot->token);
				if (const auto strong = weak.get()) {
					strong->closeBox();
				}
				return;
			} else if (_addingBot && !_addingBot->existing) {
				const auto phrase = peer()->isBroadcast()
					? tr::lng_bot_sure_add_text_channel
					: tr::lng_bot_sure_add_text_group;
				_confirmBox = getDelegate()->show(Ui::MakeConfirmBox({
					phrase(
						tr::now,
						lt_group,
						tr::bold(peer()->name()),
						tr::marked),
					crl::guard(this, [=] { finishAddAdmin(); })
				}));
			} else {
				_finishSave();
			}
		};
	}

	refreshButtons();
}

void EditAdminBox::finishAddAdmin() {
	_finishSave();
	if (_confirmBox) {
		_confirmBox->closeBox();
	}
}

void EditAdminBox::refreshButtons() {
	clearButtons();
	if (canSave()) {
		addButton((!_addingBot || _addingBot->existing)
			? tr::lng_settings_save()
			: _adminControlsWrap->toggled()
			? tr::lng_bot_add_as_admin()
			: tr::lng_bot_add_as_member(), _save);
		addButton(tr::lng_cancel(), [=] { closeBox(); });
	} else {
		addButton(tr::lng_box_ok(), [=] { closeBox(); });
	}
}

bool EditAdminBox::canTransferOwnership() const {
	if (user()->isInaccessible() || user()->isBot() || user()->isSelf()) {
		return false;
	} else if (const auto chat = peer()->asChat()) {
		return chat->amCreator();
	} else if (const auto channel = peer()->asChannel()) {
		return channel->amCreator();
	}
	Unexpected("Chat type in EditAdminBox::canTransferOwnership.");
}

not_null<Ui::SlideWrap<Ui::RpWidget>*> EditAdminBox::setupTransferButton(
		not_null<Ui::VerticalLayout*> container,
		bool isGroup) {
	const auto wrap = container->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			container,
			object_ptr<Ui::VerticalLayout>(container)));

	const auto inner = wrap->entity();

	inner->add(
		object_ptr<Ui::BoxContentDivider>(inner),
		{ 0, st::infoProfileSkip, 0, st::infoProfileSkip });
	inner->add(EditPeerInfoBox::CreateButton(
		inner,
		(isGroup
			? tr::lng_rights_transfer_group
			: tr::lng_rights_transfer_channel)(),
		rpl::single(QString()),
		[=] { transferOwnership(); },
		st::peerPermissionsButton,
		{}));

	return wrap;
}

void EditAdminBox::transferOwnership() {
	_ownershipTransfer = std::make_unique<ChannelOwnershipTransfer>(
		peer(),
		user(),
		uiShow(),
		[](std::shared_ptr<Ui::Show> show) { show->hideLayer(); });
	_ownershipTransfer->start();
}

EditRestrictedBox::EditRestrictedBox(
	QWidget*,
	not_null<PeerData*> peer,
	not_null<UserData*> user,
	bool hasAdminRights,
	ChatRestrictionsInfo rights,
	const QString &rank,
	UserData *by,
	TimeId since)
: EditParticipantBox(nullptr, peer, user, hasAdminRights)
, _oldRights(rights)
, _oldRank(rank)
, _by(by)
, _since(since) {
}

void EditRestrictedBox::prepare() {
	using Flag = ChatRestriction;
	using Flags = ChatRestrictions;

	EditParticipantBox::prepare();

	setCoverMode(true);
	addControl(
		object_ptr<Cover>(this, user(), hasAdminRights()),
		style::margins());

	setTitle(tr::lng_rights_user_restrictions());

	Ui::AddDivider(verticalLayout());
	Ui::AddSkip(verticalLayout());

	const auto chat = peer()->asChat();
	const auto channel = peer()->asChannel();
	const auto defaultRestrictions = chat
		? chat->defaultRestrictions()
		: channel->defaultRestrictions();
	const auto prepareRights = _oldRights.flags
		? _oldRights
		: defaultRights();
	const auto prepareFlags = FixDependentRestrictions(
		prepareRights.flags
		| defaultRestrictions
		| ((channel && channel->isPublic())
			? (Flag::ChangeInfo | Flag::PinMessages)
			: Flags(0)));
	const auto disabledMessages = [&] {
		auto result = base::flat_map<Flags, QString>();
		if (!canSave()) {
			result.emplace(
				~Flags(0),
				tr::lng_rights_about_restriction_cant_edit(tr::now));
		} else {
			const auto disabled = FixDependentRestrictions(
				defaultRestrictions
				| ((channel && channel->isPublic())
					? (Flag::ChangeInfo | Flag::PinMessages)
					: Flags(0)));
			result.emplace(
				disabled,
				tr::lng_rights_restriction_for_all(tr::now));
		}
		return result;
	}();

	Ui::AddSubsectionTitle(
		verticalLayout(),
		tr::lng_rights_user_restrictions_header());
	auto [checkboxes, getRestrictions, changes, highlightWidget] = CreateEditRestrictions(
		this,
		prepareFlags,
		disabledMessages,
		{ .isForum = peer()->isForum(), .isUserSpecific = true });
	addControl(std::move(checkboxes), QMargins());

	if (canSave() && peer()->canManageRanks()) {
		Ui::AddSkip(verticalLayout());
		Ui::AddDivider(verticalLayout());
		_tagControl = addControl(
			object_ptr<EditTagControl>(
				this,
				&peer()->session(),
				user(),
				_oldRank,
				HistoryView::BadgeRole::User),
			style::margins());
		Ui::AddSkip(verticalLayout());
		Ui::AddDividerText(
			verticalLayout(),
			user()->isSelf()
				? tr::lng_rights_tag_about_self()
				: tr::lng_rights_tag_about(
					lt_name,
					rpl::single(user()->shortName())));
	}

	_until = prepareRights.until;
	if (!_tagControl) {
		addControl(
			object_ptr<Ui::FixedHeightWidget>(this, st::defaultVerticalListSkip));
		Ui::AddDivider(verticalLayout());
	}
	addControl(
		object_ptr<Ui::FlatLabel>(
			this,
			tr::lng_rights_chat_banned_until_header(tr::now),
			st::rightsHeaderLabel),
		st::rightsHeaderMargin);
	setRestrictUntil(_until);

	//addControl(
	//	object_ptr<Ui::LinkButton>(
	//		this,
	//		tr::lng_rights_chat_banned_block(tr::now),
	//		st::boxLinkButton));

	if (_since) {
		const auto parsed = base::unixtime::parse(_since);
		const auto inner = addControl(object_ptr<Ui::VerticalLayout>(this));
		const auto isBanned = (_oldRights.flags
			& ChatRestriction::ViewMessages);
		Ui::AddSkip(inner);
		const auto label = Ui::AddDividerText(
			inner,
			(isBanned
				? tr::lng_rights_chat_banned_by
				: tr::lng_rights_chat_restricted_by)(
					lt_user,
					rpl::single(_by
						? tr::link(_by->name(), 1)
						: TextWithEntities{ QString::fromUtf8("\U0001F47B") }),
					lt_date,
					rpl::single(TextWithEntities{ langDateTimeFull(parsed) }),
					tr::marked));
		if (_by) {
			label->setLink(1, _by->createOpenLink());
		}
	}

	if (canSave()) {
		Ui::AddSkip(verticalLayout());
		Ui::AddDivider(verticalLayout());
		Ui::AddSkip(verticalLayout());

		const auto canAddAdmins = (chat && chat->canAddAdmins())
			|| (channel && channel->canAddAdmins());
		if (canAddAdmins) {
			const auto promoteButton = Settings::AddButtonWithIcon(
				verticalLayout(),
				tr::lng_rights_promote_member(),
				st::settingsButtonNoIcon,
				{ nullptr });
			promoteButton->setClickedCallback([=] {
				const auto rank = _tagControl
					? _tagControl->currentRank()
					: _oldRank;
				auto adminBox = Box<EditAdminBox>(
					peer(),
					user(),
					ChatAdminRightsInfo(),
					rank,
					TimeId(0),
					nullptr);
				const auto adminBoxWeak = QPointer<Ui::BoxContent>(
					adminBox.data());
				const auto show = uiShow();
				const auto restrictWeak = QPointer<EditRestrictedBox>(
					this);
				const auto closeBoth = [=] {
					if (adminBoxWeak) {
						adminBoxWeak->closeBox();
					}
					if (restrictWeak) {
						restrictWeak->closeBox();
					}
				};
				const auto savedUser = user();
				const auto savedPeer = peer();
				const auto done = [=](
						ChatAdminRightsInfo newRights,
						const std::optional<QString> &rank) {
					closeBoth();
					const auto effectiveRank = rank.value_or([&] {
						const auto ch = savedPeer->asChannel();
						if (!ch) {
							return QString();
						}
						const auto info = ch->mgInfo.get();
						if (!info) {
							return QString();
						}
						const auto i = info->memberRanks.find(
							peerToUser(savedUser->id));
						return (i != end(info->memberRanks))
							? i->second
							: QString();
					}());
					savedUser->session().changes().chatAdminChanged(
						savedPeer,
						savedUser,
						newRights.flags,
						effectiveRank);
				};
				const auto fail = closeBoth;
				adminBox->setSaveCallback(
					SaveAdminCallback(
						show,
						peer(),
						user(),
						done,
						fail));
				show->showBox(std::move(adminBox));
			});
		}

		const auto removeButton = Settings::AddButtonWithIcon(
			verticalLayout(),
			tr::lng_rights_remove_member(),
			st::settingsAttentionButton,
			{ nullptr });
		removeButton->setClickedCallback([=] {
			if (const auto callback = _saveCallback) {
				const auto old = _oldRights;
				const auto confirmed = [=](Fn<void()> close) {
					callback(
						old,
						ChannelData::KickedRestrictedRights(user()));
					close();
				};
				uiShow()->show(
					Ui::MakeConfirmBox({
						.text = tr::lng_profile_sure_kick(
							tr::now,
							lt_user,
							user()->firstName),
						.confirmed = confirmed,
						.confirmText = tr::lng_box_remove(),
					}));
			}
		});

		const auto save = [=, value = getRestrictions] {
			if (!_saveCallback) {
				return;
			}
			_saveCallback(
				_oldRights,
				ChatRestrictionsInfo{ value(), getRealUntilValue() });
			if (_tagControl) {
				const auto rank = _tagControl->currentRank();
				if (rank != _oldRank) {
					SaveMemberRank(
						uiShow(),
						peer(),
						user(),
						rank,
						nullptr,
						nullptr);
				}
			}
		};
		addButton(tr::lng_settings_save(), save);
		addButton(tr::lng_cancel(), [=] { closeBox(); });
	} else {
		addButton(tr::lng_box_ok(), [=] { closeBox(); });
	}
}

ChatRestrictionsInfo EditRestrictedBox::defaultRights() const {
	return ChatRestrictionsInfo();
}

void EditRestrictedBox::showRestrictUntil() {
	uiShow()->showBox(Box([=](not_null<Ui::GenericBox*> box) {
		const auto save = [=](TimeId result) {
			if (!result) {
				return;
			}
			setRestrictUntil(result);
			box->closeBox();
		};
		const auto now = base::unixtime::now();
		const auto time = isUntilForever()
			? (now + kSecondsInDay)
			: getRealUntilValue();
		ChooseDateTimeBox(box, {
			.title = tr::lng_rights_chat_banned_until_header(),
			.submit = tr::lng_settings_save(),
			.done = save,
			.min = [=] { return now; },
			.time = time,
			.max = [=] {
				return now + kSecondsInDay * kMaxRestrictDelayDays;
			},
		});
	}));
}

void EditRestrictedBox::setRestrictUntil(TimeId until) {
	_until = until;
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
		if (!canSave() && _untilGroup->current() != value) {
			return;
		}
		_untilVariants.emplace_back(
			addControl(
				object_ptr<Ui::Radiobutton>(
					this,
					_untilGroup,
					value,
					text,
					st::defaultCheckbox),
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
				tr::lng_rights_chat_banned_custom_date(
					tr::now,
					lt_date,
					langDateTime(base::unixtime::parse(until))));
		}
	};
	auto addCurrentVariant = [&](TimeId from, TimeId to) {
		auto oldUntil = _oldRights.until;
		if (oldUntil < _until) {
			addCustomVariant(oldUntil, from, to);
		}
		addCustomVariant(_until, from, to);
		if (oldUntil > _until) {
			addCustomVariant(oldUntil, from, to);
		}
	};
	addVariant(0, tr::lng_rights_chat_banned_forever(tr::now));

	auto now = base::unixtime::now();
	auto nextDay = now + kSecondsInDay;
	auto nextWeek = now + kSecondsInWeek;
	addCurrentVariant(0, nextDay);
	addVariant(kUntilOneDay, tr::lng_rights_chat_banned_day(tr::now, lt_count, 1));
	addCurrentVariant(nextDay, nextWeek);
	addVariant(kUntilOneWeek, tr::lng_rights_chat_banned_week(tr::now, lt_count, 1));
	addCurrentVariant(nextWeek, INT_MAX);
	addVariant(kUntilCustom, tr::lng_rights_chat_banned_custom(tr::now));
}

TimeId EditRestrictedBox::getRealUntilValue() const {
	Expects(_until != kUntilCustom);
	if (_until == kUntilOneDay) {
		return base::unixtime::now() + kSecondsInDay;
	} else if (_until == kUntilOneWeek) {
		return base::unixtime::now() + kSecondsInWeek;
	}
	Assert(_until >= 0);
	return _until;
}
