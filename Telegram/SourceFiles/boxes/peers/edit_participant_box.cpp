/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/peers/edit_participant_box.h"

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
#include "settings/settings_privacy_security.h"
#include "ui/boxes/choose_date_time.h"
#include "ui/boxes/confirm_box.h"
#include "boxes/passcode_box.h"
#include "boxes/peers/add_bot_to_chat_box.h"
#include "boxes/peers/edit_peer_permissions_box.h"
#include "boxes/peers/edit_peer_info_box.h"
#include "data/data_peer_values.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_user.h"
#include "core/core_cloud_password.h"
#include "base/unixtime.h"
#include "apiwrap.h"
#include "api/api_cloud_password.h"
#include "main/main_session.h"
#include "styles/style_layers.h"
#include "styles/style_boxes.h"
#include "styles/style_info.h"

namespace {

constexpr auto kMaxRestrictDelayDays = 366;
constexpr auto kSecondsInDay = 24 * 60 * 60;
constexpr auto kSecondsInWeek = 7 * kSecondsInDay;
constexpr auto kAdminRoleLimit = 16;

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

protected:
	int resizeGetHeight(int newWidth) override;
	void paintEvent(QPaintEvent *e) override;

private:
	not_null<PeerData*> _peer;
	not_null<UserData*> _user;
	object_ptr<Ui::UserpicButton> _userPhoto;
	Ui::Text::String _userName;
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
, _userPhoto(this, _user, st::rightsPhotoButton)
, _hasAdminRights(hasAdminRights)
, _rows(this) {
	_rows->heightValue(
	) | rpl::start_with_next([=] {
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
			| Flag::ManageCall) }
		: ChatAdminRightsInfo{ (Flag::ChangeInfo
			| Flag::PostMessages
			| Flag::EditMessages
			| Flag::DeleteMessages
			| Flag::PostStories
			| Flag::EditStories
			| Flag::DeleteStories
			| Flag::InviteByLinkOrAdd
			| Flag::ManageCall
			| Flag::ManageDirect) };
}

void EditAdminBox::prepare() {
	using namespace rpl::mappers;
	using Flag = ChatAdminRight;
	using Flags = ChatAdminRights;

	EditParticipantBox::prepare();

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
		) | rpl::start_with_next([=](bool checked) {
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
					? Ui::Text::Link(_by->name(), 1)
					: TextWithEntities{ QString::fromUtf8("\U0001F47B") }),
				lt_date,
				rpl::single(TextWithEntities{ langDateTimeFull(parsed) }),
				Ui::Text::WithEntities));
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
	auto [checkboxes, getChecked, changes] = CreateEditAdminRights(
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
			Ui::AddSkip(emptyAboutAddAdminsInner->entity());
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
		_rank = hasRank ? addRankInput(inner).get() : nullptr;
		_finishSave = [=, value = getChecked] {
			const auto newFlags = (value() | ChatAdminRight::Other)
				& ((!channel || channel->amCreator())
					? ~Flags(0)
					: channel->adminRights());
			_saveCallback(
				_oldRights,
				ChatAdminRightsInfo(newFlags),
				_rank ? _rank->getLastText().trimmed() : QString());
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
						Ui::Text::Bold(peer()->name()),
						Ui::Text::WithEntities),
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

not_null<Ui::InputField*> EditAdminBox::addRankInput(
		not_null<Ui::VerticalLayout*> container) {
	// Ui::AddDivider(container);

	container->add(
		object_ptr<Ui::FlatLabel>(
			container,
			tr::lng_rights_edit_admin_rank_name(),
			st::rightsHeaderLabel),
		st::rightsHeaderMargin);

	const auto isOwner = [&] {
		if (user()->isSelf() && amCreator()) {
			return true;
		} else if (const auto chat = peer()->asChat()) {
			return chat->creator == peerToUser(user()->id);
		} else if (const auto channel = peer()->asChannel()) {
			return channel->mgInfo && channel->mgInfo->creator == user();
		}
		Unexpected("Peer type in EditAdminBox::addRankInput.");
	}();
	const auto result = container->add(
		object_ptr<Ui::InputField>(
			container,
			st::customBadgeField,
			(isOwner ? tr::lng_owner_badge : tr::lng_admin_badge)(),
			TextUtilities::RemoveEmoji(_oldRank)),
		st::rightsAboutMargin);
	result->setMaxLength(kAdminRoleLimit);
	result->setInstantReplaces(Ui::InstantReplaces::TextOnly());
	result->changes(
	) | rpl::start_with_next([=] {
		const auto text = result->getLastText();
		const auto removed = TextUtilities::RemoveEmoji(text);
		if (removed != text) {
			result->setText(removed);
		}
	}, result->lifetime());

	Ui::AddSkip(container);
	Ui::AddDividerText(
		container,
		tr::lng_rights_edit_admin_rank_about(
			lt_title,
			(isOwner ? tr::lng_owner_badge : tr::lng_admin_badge)()));
	Ui::AddSkip(container);

	return result;
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
	if (_checkTransferRequestId) {
		return;
	}

	const auto channel = peer()->isChannel()
		? peer()->asChannel()->inputChannel
		: MTP_inputChannelEmpty();
	const auto api = &peer()->session().api();
	api->cloudPassword().reload();
	_checkTransferRequestId = api->request(MTPchannels_EditCreator(
		channel,
		MTP_inputUserEmpty(),
		MTP_inputCheckPasswordEmpty()
	)).fail([=](const MTP::Error &error) {
		_checkTransferRequestId = 0;
		if (!handleTransferPasswordError(error.type())) {
			const auto callback = crl::guard(this, [=](Fn<void()> &&close) {
				transferOwnershipChecked();
				close();
			});
			getDelegate()->show(Ui::MakeConfirmBox({
				.text = tr::lng_rights_transfer_about(
					tr::now,
					lt_group,
					Ui::Text::Bold(peer()->name()),
					lt_user,
					Ui::Text::Bold(user()->shortName()),
					Ui::Text::RichLangValue),
				.confirmed = callback,
				.confirmText = tr::lng_rights_transfer_sure(),
			}));
		}
	}).send();
}

bool EditAdminBox::handleTransferPasswordError(const QString &error) {
	const auto session = &user()->session();
	auto about = tr::lng_rights_transfer_check_about(
		tr::now,
		lt_user,
		Ui::Text::Bold(user()->shortName()),
		Ui::Text::WithEntities);
	if (auto box = PrePasswordErrorBox(error, session, std::move(about))) {
		getDelegate()->show(std::move(box));
		return true;
	}
	return false;
}

void EditAdminBox::transferOwnershipChecked() {
	if (const auto chat = peer()->asChatNotMigrated()) {
		peer()->session().api().migrateChat(chat, crl::guard(this, [=](
				not_null<ChannelData*> channel) {
			requestTransferPassword(channel);
		}));
	} else if (const auto channel = peer()->asChannelOrMigrated()) {
		requestTransferPassword(channel);
	} else {
		Unexpected("Peer in SaveAdminCallback.");
	}

}

void EditAdminBox::requestTransferPassword(not_null<ChannelData*> channel) {
	peer()->session().api().cloudPassword().state(
	) | rpl::take(
		1
	) | rpl::start_with_next([=](const Core::CloudPasswordState &state) {
		auto fields = PasscodeBox::CloudFields::From(state);
		fields.customTitle = tr::lng_rights_transfer_password_title();
		fields.customDescription
			= tr::lng_rights_transfer_password_description(tr::now);
		fields.customSubmitButton = tr::lng_passcode_submit();
		fields.customCheckCallback = crl::guard(this, [=](
				const Core::CloudPasswordResult &result,
				base::weak_qptr<PasscodeBox> box) {
			sendTransferRequestFrom(box, channel, result);
		});
		getDelegate()->show(Box<PasscodeBox>(&channel->session(), fields));
	}, lifetime());
}

void EditAdminBox::sendTransferRequestFrom(
		base::weak_qptr<PasscodeBox> box,
		not_null<ChannelData*> channel,
		const Core::CloudPasswordResult &result) {
	if (_transferRequestId) {
		return;
	}
	const auto weak = base::make_weak(this);
	const auto user = this->user();
	const auto api = &channel->session().api();
	_transferRequestId = api->request(MTPchannels_EditCreator(
		channel->inputChannel,
		user->inputUser,
		result.result
	)).done([=](const MTPUpdates &result) {
		api->applyUpdates(result);
		if (!box && !weak) {
			return;
		}
		const auto show = box ? box->uiShow() : weak->uiShow();
		show->showToast(
			(channel->isBroadcast()
				? tr::lng_rights_transfer_done_channel
				: tr::lng_rights_transfer_done_group)(
					tr::now,
					lt_user,
					user->shortName()));
		show->hideLayer();
	}).fail(crl::guard(this, [=](const MTP::Error &error) {
		if (weak) {
			_transferRequestId = 0;
		}
		if (box && box->handleCustomCheckError(error)) {
			return;
		}

		const auto &type = error.type();
		const auto problem = [&] {
			if (type == u"CHANNELS_ADMIN_PUBLIC_TOO_MUCH"_q) {
				return tr::lng_channels_too_much_public_other(tr::now);
			} else if (type == u"CHANNELS_ADMIN_LOCATED_TOO_MUCH"_q) {
				return tr::lng_channels_too_much_located_other(tr::now);
			} else if (type == u"ADMINS_TOO_MUCH"_q) {
				return (channel->isBroadcast()
					? tr::lng_error_admin_limit_channel
					: tr::lng_error_admin_limit)(tr::now);
			} else if (type == u"CHANNEL_INVALID"_q) {
				return (channel->isBroadcast()
					? tr::lng_channel_not_accessible
					: tr::lng_group_not_accessible)(tr::now);
			}
			return Lang::Hard::ServerError();
		}();
		const auto recoverable = [&] {
			return (type == u"PASSWORD_MISSING"_q)
				|| type.startsWith(u"PASSWORD_TOO_FRESH_"_q)
				|| type.startsWith(u"SESSION_TOO_FRESH_"_q);
		}();
		const auto weak = base::make_weak(this);
		getDelegate()->show(Ui::MakeInformBox(problem));
		if (box) {
			box->closeBox();
		}
		if (weak && !recoverable) {
			closeBox();
		}
	})).handleFloodErrors().send();
}

EditRestrictedBox::EditRestrictedBox(
	QWidget*,
	not_null<PeerData*> peer,
	not_null<UserData*> user,
	bool hasAdminRights,
	ChatRestrictionsInfo rights,
	UserData *by,
	TimeId since)
: EditParticipantBox(nullptr, peer, user, hasAdminRights)
, _oldRights(rights)
, _by(by)
, _since(since) {
}

void EditRestrictedBox::prepare() {
	using Flag = ChatRestriction;
	using Flags = ChatRestrictions;

	EditParticipantBox::prepare();

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
	auto [checkboxes, getRestrictions, changes] = CreateEditRestrictions(
		this,
		prepareFlags,
		disabledMessages,
		{ .isForum = peer()->isForum() });
	addControl(std::move(checkboxes), QMargins());

	_until = prepareRights.until;
	addControl(
		object_ptr<Ui::FixedHeightWidget>(this, st::defaultVerticalListSkip));
	Ui::AddDivider(verticalLayout());
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
						? Ui::Text::Link(_by->name(), 1)
						: TextWithEntities{ QString::fromUtf8("\U0001F47B") }),
					lt_date,
					rpl::single(TextWithEntities{ langDateTimeFull(parsed) }),
					Ui::Text::WithEntities));
		if (_by) {
			label->setLink(1, _by->createOpenLink());
		}
	}

	if (canSave()) {
		const auto save = [=, value = getRestrictions] {
			if (!_saveCallback) {
				return;
			}
			_saveCallback(
				_oldRights,
				ChatRestrictionsInfo{ value(), getRealUntilValue() });
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
