/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/peers/edit_peer_info_box.h"

#include "apiwrap.h"
#include "api/api_peer_photo.h"
#include "api/api_user_names.h"
#include "main/main_session.h"
#include "ui/boxes/confirm_box.h"
#include "boxes/peers/edit_participants_box.h"
#include "boxes/peers/edit_peer_color_box.h"
#include "boxes/peers/edit_peer_common.h"
#include "boxes/peers/edit_peer_type_box.h"
#include "boxes/peers/edit_peer_history_visibility_box.h"
#include "boxes/peers/edit_peer_permissions_box.h"
#include "boxes/peers/edit_peer_invite_links.h"
#include "boxes/peers/edit_linked_chat_box.h"
#include "boxes/peers/edit_peer_requests_box.h"
#include "boxes/peers/edit_peer_reactions.h"
#include "boxes/peers/replace_boost_box.h"
#include "boxes/peer_list_controllers.h"
#include "boxes/stickers_box.h"
#include "boxes/username_box.h"
#include "chat_helpers/emoji_suggestions_widget.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_changes.h"
#include "data/data_message_reactions.h"
#include "data/data_peer_values.h"
#include "data/data_premium_limits.h"
#include "data/data_user.h"
#include "history/admin_log/history_admin_log_section.h"
#include "info/channel_statistics/boosts/info_boosts_widget.h"
#include "info/profile/info_profile_values.h"
#include "info/info_memento.h"
#include "lang/lang_keys.h"
#include "mtproto/sender.h"
#include "main/main_app_config.h"
#include "settings/settings_common.h"
#include "ui/boxes/boost_box.h"
#include "ui/controls/userpic_button.h"
#include "ui/rp_widget.h"
#include "ui/vertical_list.h"
#include "ui/toast/toast.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "api/api_invite_links.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_boxes.h"
#include "styles/style_info.h"

namespace {

constexpr auto kBotManagerUsername = "BotFather"_cs;

[[nodiscard]] auto ToPositiveNumberString() {
	return rpl::map([](int count) {
		return count ? QString::number(count) : QString();
	});
}

[[nodiscard]] int EnableForumMinMembers(not_null<PeerData*> peer) {
	return peer->session().appConfig().get<int>(
		u"forum_upgrade_participants_min"_q,
		200);
}

void AddSkip(
		not_null<Ui::VerticalLayout*> container,
		int top = st::editPeerTopButtonsLayoutSkip,
		int bottom = st::editPeerTopButtonsLayoutSkipToBottom) {
	Ui::AddSkip(container, top);
	Ui::AddDivider(container);
	Ui::AddSkip(container, bottom);
}

void AddButtonWithCount(
		not_null<Ui::VerticalLayout*> parent,
		rpl::producer<QString> &&text,
		rpl::producer<QString> &&count,
		Fn<void()> callback,
		Settings::IconDescriptor &&descriptor) {
	parent->add(EditPeerInfoBox::CreateButton(
		parent,
		std::move(text),
		std::move(count),
		std::move(callback),
		st::manageGroupButton,
		std::move(descriptor)));
}

not_null<Ui::SettingsButton*> AddButtonWithText(
		not_null<Ui::VerticalLayout*> parent,
		rpl::producer<QString> &&text,
		rpl::producer<QString> &&label,
		Fn<void()> callback,
		Settings::IconDescriptor &&descriptor) {
	return parent->add(EditPeerInfoBox::CreateButton(
		parent,
		std::move(text),
		std::move(label),
		std::move(callback),
		st::manageGroupTopButtonWithText,
		std::move(descriptor)));
}

void AddButtonDelete(
		not_null<Ui::VerticalLayout*> parent,
		rpl::producer<QString> &&text,
		Fn<void()> callback) {
	parent->add(EditPeerInfoBox::CreateButton(
		parent,
		std::move(text),
		rpl::single(QString()),
		std::move(callback),
		st::manageDeleteGroupButton,
		{}));
}

void SaveDefaultRestrictions(
		not_null<PeerData*> peer,
		ChatRestrictions rights,
		Fn<void()> done) {
	const auto api = &peer->session().api();
	const auto key = Api::RequestKey("default_restrictions", peer->id);

	const auto requestId = api->request(
		MTPmessages_EditChatDefaultBannedRights(
			peer->input,
			MTP_chatBannedRights(
				MTP_flags(
					MTPDchatBannedRights::Flags::from_raw(uint32(rights))),
				MTP_int(0)))
	).done([=](const MTPUpdates &result) {
		api->clearModifyRequest(key);
		api->applyUpdates(result);
		done();
	}).fail([=](const MTP::Error &error) {
		api->clearModifyRequest(key);
		if (error.type() != u"CHAT_NOT_MODIFIED"_q) {
			return;
		}
		if (const auto chat = peer->asChat()) {
			chat->setDefaultRestrictions(rights);
		} else if (const auto channel = peer->asChannel()) {
			channel->setDefaultRestrictions(rights);
		} else {
			Unexpected("Peer in ApiWrap::saveDefaultRestrictions.");
		}
		done();
	}).send();

	api->registerModifyRequest(key, requestId);
}

void SaveSlowmodeSeconds(
		not_null<ChannelData*> channel,
		int seconds,
		Fn<void()> done) {
	const auto api = &channel->session().api();
	const auto key = Api::RequestKey("slowmode_seconds", channel->id);

	const auto requestId = api->request(MTPchannels_ToggleSlowMode(
		channel->inputChannel,
		MTP_int(seconds)
	)).done([=](const MTPUpdates &result) {
		api->clearModifyRequest(key);
		api->applyUpdates(result);
		channel->setSlowmodeSeconds(seconds);
		done();
	}).fail([=](const MTP::Error &error) {
		api->clearModifyRequest(key);
		if (error.type() != u"CHAT_NOT_MODIFIED"_q) {
			return;
		}
		channel->setSlowmodeSeconds(seconds);
		done();
	}).send();

	api->registerModifyRequest(key, requestId);
}

void SaveBoostsUnrestrict(
		not_null<ChannelData*> channel,
		int boostsUnrestrict,
		Fn<void()> done) {
	const auto api = &channel->session().api();
	const auto key = Api::RequestKey("boosts_unrestrict", channel->id);
	const auto requestId = api->request(
		MTPchannels_SetBoostsToUnblockRestrictions(
			channel->inputChannel,
			MTP_int(boostsUnrestrict))
	).done([=](const MTPUpdates &result) {
		api->clearModifyRequest(key);
		api->applyUpdates(result);
		channel->setBoostsUnrestrict(
			channel->boostsApplied(),
			boostsUnrestrict);
		done();
	}).fail([=](const MTP::Error &error) {
		api->clearModifyRequest(key);
		if (error.type() != u"CHAT_NOT_MODIFIED"_q) {
			return;
		}
		channel->setBoostsUnrestrict(
			channel->boostsApplied(),
			boostsUnrestrict);
		done();
	}).send();

	api->registerModifyRequest(key, requestId);
}

void ShowEditPermissions(
		not_null<Window::SessionNavigation*> navigation,
		not_null<PeerData*> peer) {
	auto createBox = [=](not_null<Ui::GenericBox*> box) {
		const auto saving = box->lifetime().make_state<int>(0);
		const auto save = [=](
				not_null<PeerData*> peer,
				EditPeerPermissionsBoxResult result) {
			Expects(result.slowmodeSeconds == 0 || peer->isChannel());

			const auto close = crl::guard(box, [=] { box->closeBox(); });
			SaveDefaultRestrictions(
				peer,
				result.rights,
				close);
			if (const auto channel = peer->asChannel()) {
				SaveSlowmodeSeconds(channel, result.slowmodeSeconds, close);
				SaveBoostsUnrestrict(
					channel,
					result.boostsUnrestrict,
					close);
			}
		};
		auto done = [=](EditPeerPermissionsBoxResult result) {
			if (*saving) {
				return;
			}
			*saving = true;

			const auto saveFor = peer->migrateToOrMe();
			const auto chat = saveFor->asChat();
			if (!chat
				|| (!result.slowmodeSeconds && !result.boostsUnrestrict)) {
				save(saveFor, result);
				return;
			}
			const auto api = &peer->session().api();
			api->migrateChat(chat, [=](not_null<ChannelData*> channel) {
				save(channel, result);
			}, [=](const QString &) {
				*saving = false;
			});
		};
		ShowEditPeerPermissionsBox(box, navigation, peer, std::move(done));
	};
	navigation->parentController()->show(Box(std::move(createBox)));
}

class Controller : public base::has_weak_ptr {
public:
	Controller(
		not_null<Window::SessionNavigation*> navigation,
		not_null<Ui::BoxContent*> box,
		not_null<PeerData*> peer);
	~Controller();

	[[nodiscard]] object_ptr<Ui::VerticalLayout> createContent();
	void setFocus();

private:
	struct Controls {
		Ui::InputField *title = nullptr;
		Ui::InputField *description = nullptr;
		Ui::UserpicButton *photo = nullptr;
		rpl::lifetime initialPhotoImageWaiting;
		Ui::VerticalLayout *buttonsLayout = nullptr;
		Ui::SettingsButton *forumToggle = nullptr;
		bool forumToggleLocked = false;
		bool levelRequested = false;
		Ui::SlideWrap<> *historyVisibilityWrap = nullptr;
	};
	struct Saving {
		std::optional<QString> username;
		std::optional<std::vector<QString>> usernamesOrder;
		std::optional<QString> title;
		std::optional<QString> description;
		std::optional<bool> hiddenPreHistory;
		std::optional<bool> forum;
		std::optional<bool> signatures;
		std::optional<bool> noForwards;
		std::optional<bool> joinToWrite;
		std::optional<bool> requestToJoin;
		std::optional<ChannelData*> linkedChat;
	};

	[[nodiscard]] object_ptr<Ui::RpWidget> createPhotoAndTitleEdit();
	[[nodiscard]] object_ptr<Ui::RpWidget> createTitleEdit();
	[[nodiscard]] object_ptr<Ui::RpWidget> createPhotoEdit();
	[[nodiscard]] object_ptr<Ui::RpWidget> createDescriptionEdit();
	[[nodiscard]] object_ptr<Ui::RpWidget> createManageGroupButtons();
	[[nodiscard]] object_ptr<Ui::RpWidget> createStickersEdit();

	[[nodiscard]] bool canEditInformation() const;
	[[nodiscard]] bool canEditReactions() const;
	void refreshHistoryVisibility();
	void refreshForumToggleLocked();
	void showEditPeerTypeBox(
		std::optional<rpl::producer<QString>> error = {});
	void showEditLinkedChatBox();
	void fillPrivacyTypeButton();
	void fillLinkedChatButton();
	//void fillInviteLinkButton();
	void fillForumButton();
	void fillColorIndexButton();
	void fillSignaturesButton();
	void fillHistoryVisibilityButton();
	void fillManageSection();
	void fillPendingRequestsButton();

	void fillBotUsernamesButton();
	void fillBotEditIntroButton();
	void fillBotEditCommandsButton();
	void fillBotEditSettingsButton();

	void submitTitle();
	void submitDescription();
	void deleteWithConfirmation();
	void deleteChannel();
	void editReactions();

	[[nodiscard]] std::optional<Saving> validate() const;
	[[nodiscard]] bool validateUsernamesOrder(Saving &to) const;
	[[nodiscard]] bool validateUsername(Saving &to) const;
	[[nodiscard]] bool validateLinkedChat(Saving &to) const;
	[[nodiscard]] bool validateTitle(Saving &to) const;
	[[nodiscard]] bool validateDescription(Saving &to) const;
	[[nodiscard]] bool validateHistoryVisibility(Saving &to) const;
	[[nodiscard]] bool validateForum(Saving &to) const;
	[[nodiscard]] bool validateSignatures(Saving &to) const;
	[[nodiscard]] bool validateForwards(Saving &to) const;
	[[nodiscard]] bool validateJoinToWrite(Saving &to) const;
	[[nodiscard]] bool validateRequestToJoin(Saving &to) const;

	void save();
	void saveUsernamesOrder();
	void saveUsername();
	void saveLinkedChat();
	void saveTitle();
	void saveDescription();
	void saveHistoryVisibility();
	void saveForum();
	void saveSignatures();
	void saveForwards();
	void saveJoinToWrite();
	void saveRequestToJoin();
	void savePhoto();
	void pushSaveStage(FnMut<void()> &&lambda);
	void continueSave();
	void cancelSave();

	void toggleBotManager(const QString &command);

	void togglePreHistoryHidden(
		not_null<ChannelData*> channel,
		bool hidden,
		Fn<void()> done,
		Fn<void()> fail);

	void subscribeToMigration();
	void migrate(not_null<ChannelData*> channel);

	std::optional<ChannelData*> _linkedChatSavedValue;
	ChannelData *_linkedChatOriginalValue = nullptr;
	bool _channelHasLocationOriginalValue = false;
	std::optional<HistoryVisibility> _historyVisibilitySavedValue;
	std::optional<EditPeerTypeData> _typeDataSavedValue;
	std::optional<bool> _forumSavedValue;
	std::optional<bool> _signaturesSavedValue;

	const not_null<Window::SessionNavigation*> _navigation;
	const not_null<Ui::BoxContent*> _box;
	not_null<PeerData*> _peer;
	MTP::Sender _api;
	const bool _isGroup = false;
	const bool _isBot = false;

	base::unique_qptr<Ui::VerticalLayout> _wrap;
	Controls _controls;

	std::deque<FnMut<void()>> _saveStagesQueue;
	Saving _savingData;

	const rpl::event_stream<Privacy> _privacyTypeUpdates;
	const rpl::event_stream<ChannelData*> _linkedChatUpdates;
	mtpRequestId _linkedChatsRequestId = 0;

	rpl::lifetime _lifetime;

};

Controller::Controller(
	not_null<Window::SessionNavigation*> navigation,
	not_null<Ui::BoxContent*> box,
	not_null<PeerData*> peer)
: _navigation(navigation)
, _box(box)
, _peer(peer)
, _api(&_peer->session().mtp())
, _isGroup(_peer->isChat() || _peer->isMegagroup())
, _isBot(_peer->isUser() && _peer->asUser()->botInfo) {
	_box->setTitle(_isBot
		? tr::lng_edit_bot_title()
		: _isGroup
		? tr::lng_edit_group()
		: tr::lng_edit_channel_title());
	_box->addButton(tr::lng_settings_save(), [=] {
		save();
	});
	_box->addButton(tr::lng_cancel(), [=] {
		_box->closeBox();
	});
	subscribeToMigration();
	_peer->updateFull();
}

Controller::~Controller() = default;

void Controller::subscribeToMigration() {
	SubscribeToMigration(
		_peer,
		_lifetime,
		[=](not_null<ChannelData*> channel) { migrate(channel); });
}

void Controller::migrate(not_null<ChannelData*> channel) {
	_peer = channel;
	_peer->updateFull();
}

object_ptr<Ui::VerticalLayout> Controller::createContent() {
	auto result = object_ptr<Ui::VerticalLayout>(_box);
	_wrap.reset(result.data());
	_controls = Controls();

	_wrap->add(createPhotoAndTitleEdit());
	_wrap->add(createDescriptionEdit());
	_wrap->add(createManageGroupButtons());

	return result;
}

void Controller::setFocus() {
	if (_controls.title) {
		_controls.title->setFocusFast();
	}
}

object_ptr<Ui::RpWidget> Controller::createPhotoAndTitleEdit() {
	Expects(_wrap != nullptr);

	if (!canEditInformation()) {
		return nullptr;
	}

	auto result = object_ptr<Ui::RpWidget>(_wrap);
	const auto container = result.data();

	const auto photoWrap = Ui::AttachParentChild(
		container,
		createPhotoEdit());
	const auto titleEdit = Ui::AttachParentChild(
		container,
		createTitleEdit());
	photoWrap->heightValue(
	) | rpl::start_with_next([container](int height) {
		container->resize(container->width(), height);
	}, photoWrap->lifetime());
	container->widthValue(
	) | rpl::start_with_next([titleEdit](int width) {
		const auto left = st::editPeerPhotoMargins.left()
			+ st::defaultUserpicButton.size.width();
		titleEdit->resizeToWidth(width - left);
		titleEdit->moveToLeft(left, 0, width);
	}, titleEdit->lifetime());

	return result;
}

object_ptr<Ui::RpWidget> Controller::createPhotoEdit() {
	Expects(_wrap != nullptr);

	using PhotoWrap = Ui::PaddingWrap<Ui::UserpicButton>;
	auto photoWrap = object_ptr<PhotoWrap>(
		_wrap,
		object_ptr<Ui::UserpicButton>(
			_wrap,
			_navigation->parentController(),
			_peer,
			Ui::UserpicButton::Role::ChangePhoto,
			Ui::UserpicButton::Source::PeerPhoto,
			st::defaultUserpicButton),
		st::editPeerPhotoMargins);
	_controls.photo = photoWrap->entity();
	_controls.photo->showCustomOnChosen();

	return photoWrap;
}

object_ptr<Ui::RpWidget> Controller::createTitleEdit() {
	Expects(_wrap != nullptr);

	auto result = object_ptr<Ui::PaddingWrap<Ui::InputField>>(
		_wrap,
		object_ptr<Ui::InputField>(
			_wrap,
			st::defaultInputField,
			(_isBot
				? tr::lng_dlg_new_bot_name
				: _isGroup
				? tr::lng_dlg_new_group_name
				: tr::lng_dlg_new_channel_name)(),
			_peer->name()),
		st::editPeerTitleMargins);
	result->entity()->setMaxLength(Ui::EditPeer::kMaxGroupChannelTitle);
	result->entity()->setInstantReplaces(Ui::InstantReplaces::Default());
	result->entity()->setInstantReplacesEnabled(
		Core::App().settings().replaceEmojiValue());
	Ui::Emoji::SuggestionsController::Init(
		_wrap->window(),
		result->entity(),
		&_peer->session());

	result->entity()->submits(
	) | rpl::start_with_next([=] {
		submitTitle();
	}, result->entity()->lifetime());

	_controls.title = result->entity();
	return result;
}

object_ptr<Ui::RpWidget> Controller::createDescriptionEdit() {
	Expects(_wrap != nullptr);

	if (!canEditInformation()) {
		return nullptr;
	}

	auto result = object_ptr<Ui::PaddingWrap<Ui::InputField>>(
		_wrap,
		object_ptr<Ui::InputField>(
			_wrap,
			st::editPeerDescription,
			Ui::InputField::Mode::MultiLine,
			tr::lng_create_group_description(),
			_peer->about()),
		st::editPeerDescriptionMargins);
	result->entity()->setMaxLength(Ui::EditPeer::kMaxChannelDescription);
	result->entity()->setInstantReplaces(Ui::InstantReplaces::Default());
	result->entity()->setInstantReplacesEnabled(
		Core::App().settings().replaceEmojiValue());
	result->entity()->setSubmitSettings(
		Core::App().settings().sendSubmitWay());
	Ui::Emoji::SuggestionsController::Init(
		_wrap->window(),
		result->entity(),
		&_peer->session());

	result->entity()->submits(
	) | rpl::start_with_next([=] {
		submitDescription();
	}, result->entity()->lifetime());

	_controls.description = result->entity();
	return result;
}

object_ptr<Ui::RpWidget> Controller::createManageGroupButtons() {
	Expects(_wrap != nullptr);

	auto result = object_ptr<Ui::PaddingWrap<Ui::VerticalLayout>>(
		_wrap,
		object_ptr<Ui::VerticalLayout>(_wrap),
		st::editPeerBottomButtonsLayoutMargins);
	_controls.buttonsLayout = result->entity();

	fillManageSection();

	return result;
}

object_ptr<Ui::RpWidget> Controller::createStickersEdit() {
	Expects(_wrap != nullptr);

	const auto channel = _peer->asChannel();
	const auto bottomSkip = st::editPeerTopButtonsLayoutSkipCustomBottom;

	auto result = object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
		_wrap,
		object_ptr<Ui::VerticalLayout>(_wrap));
	const auto container = result->entity();

	Ui::AddSubsectionTitle(
		container,
		tr::lng_group_stickers(),
		{ 0, st::defaultSubsectionTitlePadding.top() - bottomSkip, 0, 0 });

	AddButtonWithCount(
		container,
		tr::lng_group_stickers_add(),
		rpl::single(QString()), //Empty count.
		[=, controller = _navigation->parentController()] {
			const auto isEmoji = false;
			controller->show(
				Box<StickersBox>(controller->uiShow(), channel, isEmoji));
		},
		{ &st::menuIconStickers });

	Ui::AddSkip(container, bottomSkip);

	Ui::AddDividerText(
		container,
		tr::lng_group_stickers_description());

	Ui::AddSkip(container, bottomSkip);

	return result;
}

bool Controller::canEditInformation() const {
	if (_isBot) {
		return _peer->asUser()->botInfo->canEditInformation;
	} else if (const auto channel = _peer->asChannel()) {
		return channel->canEditInformation();
	} else if (const auto chat = _peer->asChat()) {
		return chat->canEditInformation();
	}
	return false;
}

bool Controller::canEditReactions() const {
	if (const auto channel = _peer->asChannel()) {
		return channel->amCreator()
			|| (channel->adminRights() & ChatAdminRight::ChangeInfo);
	} else if (const auto chat = _peer->asChat()) {
		return chat->amCreator()
			|| (chat->adminRights() & ChatAdminRight::ChangeInfo);
	}
	return false;
}

void Controller::refreshHistoryVisibility() {
	if (!_controls.historyVisibilityWrap) {
		return;
	}
	const auto withUsername = _typeDataSavedValue
		&& (_typeDataSavedValue->privacy == Privacy::HasUsername);
	_controls.historyVisibilityWrap->toggle(
		(!withUsername
			&& !_channelHasLocationOriginalValue
			&& (!_linkedChatSavedValue || !*_linkedChatSavedValue)
			&& (!_forumSavedValue || !*_forumSavedValue)),
		anim::type::instant);
}

void Controller::showEditPeerTypeBox(
		std::optional<rpl::producer<QString>> error) {
	const auto boxCallback = crl::guard(this, [=](EditPeerTypeData data) {
		_privacyTypeUpdates.fire_copy(data.privacy);
		_typeDataSavedValue = data;
		refreshHistoryVisibility();
	});
	_typeDataSavedValue->hasLinkedChat
		= (_linkedChatSavedValue.value_or(nullptr) != nullptr);
	const auto box = _navigation->parentController()->show(
		Box<EditPeerTypeBox>(
			_navigation,
			_peer,
			_channelHasLocationOriginalValue,
			boxCallback,
			_typeDataSavedValue,
			error));
	box->boxClosing(
	) | rpl::start_with_next([peer = _peer] {
		peer->session().api().usernames().requestToCache(peer);
	}, box->lifetime());
}

void Controller::showEditLinkedChatBox() {
	Expects(_peer->isChannel());

	if (_forumSavedValue && *_forumSavedValue) {
		ShowForumForDiscussionError(_navigation);
		return;
	}

	const auto box = std::make_shared<QPointer<Ui::BoxContent>>();
	const auto channel = _peer->asChannel();
	const auto callback = [=](ChannelData *result) {
		if (*box) {
			(*box)->closeBox();
		}
		*_linkedChatSavedValue = result;
		_linkedChatUpdates.fire_copy(result);
		refreshHistoryVisibility();
		refreshForumToggleLocked();
	};
	const auto canEdit = channel->isBroadcast()
		? channel->canEditInformation()
		: (channel->canPinMessages()
			&& (channel->amCreator() || channel->adminRights() != 0)
			&& (!channel->hiddenPreHistory()
				|| channel->canEditPreHistoryHidden()));

	if (const auto chat = *_linkedChatSavedValue) {
		*box = _navigation->parentController()->show(EditLinkedChatBox(
			_navigation,
			channel,
			chat,
			canEdit,
			callback));
		return;
	} else if (!canEdit || _linkedChatsRequestId) {
		return;
	} else if (channel->isMegagroup()) {
		if (_forumSavedValue
			&& *_forumSavedValue
			&& _linkedChatOriginalValue) {
			ShowForumForDiscussionError(_navigation);
		} else {
			// Restore original linked channel.
			callback(_linkedChatOriginalValue);
		}
		return;
	}
	_linkedChatsRequestId = _api.request(
		MTPchannels_GetGroupsForDiscussion()
	).done([=](const MTPmessages_Chats &result) {
		_linkedChatsRequestId = 0;
		const auto list = result.match([&](const auto &data) {
			return data.vchats().v;
		});
		auto chats = std::vector<not_null<PeerData*>>();
		chats.reserve(list.size());
		for (const auto &item : list) {
			chats.emplace_back(_peer->owner().processChat(item));
		}
		*box = _navigation->parentController()->show(EditLinkedChatBox(
			_navigation,
			channel,
			std::move(chats),
			callback));
	}).fail([=] {
		_linkedChatsRequestId = 0;
	}).send();
}

void Controller::fillPrivacyTypeButton() {
	Expects(_controls.buttonsLayout != nullptr);

	// Create Privacy Button.
	const auto hasLocation = _peer->isChannel()
		&& _peer->asChannel()->hasLocation();
	_typeDataSavedValue = EditPeerTypeData{
		.privacy = ((_peer->isChannel()
			&& _peer->asChannel()->hasUsername())
			? Privacy::HasUsername
			: Privacy::NoUsername),
		.username = (_peer->isChannel()
			? _peer->asChannel()->editableUsername()
			: QString()),
		.usernamesOrder = (_peer->isChannel()
			? _peer->asChannel()->usernames()
			: std::vector<QString>()),
		.noForwards = !_peer->allowsForwarding(),
		.joinToWrite = (_peer->isMegagroup()
			&& _peer->asChannel()->joinToWrite()),
		.requestToJoin = (_peer->isMegagroup()
			&& _peer->asChannel()->requestToJoin()),
	};
	const auto isGroup = (_peer->isChat() || _peer->isMegagroup());
	AddButtonWithText(
		_controls.buttonsLayout,
		(hasLocation
			? tr::lng_manage_peer_link_type
			: isGroup
			? tr::lng_manage_peer_group_type
			: tr::lng_manage_peer_channel_type)(),
		_privacyTypeUpdates.events(
		) | rpl::map([=](Privacy flag) {
			if (flag == Privacy::HasUsername) {
				_peer->session().api().usernames().requestToCache(_peer);
			}
			return (flag == Privacy::HasUsername)
				? (hasLocation
					? tr::lng_manage_peer_link_permanent
					: isGroup
					? tr::lng_manage_public_group_title
					: tr::lng_manage_public_peer_title)()
				: (hasLocation
					? tr::lng_manage_peer_link_invite
					: isGroup
					? tr::lng_manage_private_group_title
					: tr::lng_manage_private_peer_title)();
		}) | rpl::flatten_latest(),
		[=] { showEditPeerTypeBox(); },
		{ &st::menuIconCustomize });

	_privacyTypeUpdates.fire_copy(_typeDataSavedValue->privacy);
}

void Controller::fillLinkedChatButton() {
	Expects(_controls.buttonsLayout != nullptr);

	_linkedChatSavedValue = _linkedChatOriginalValue = _peer->isChannel()
		? _peer->asChannel()->linkedChat()
		: nullptr;

	const auto isGroup = (_peer->isChat() || _peer->isMegagroup());
	auto text = !isGroup
		? tr::lng_manage_discussion_group()
		: rpl::combine(
			tr::lng_manage_linked_channel(),
			tr::lng_manage_linked_channel_restore(),
			_linkedChatUpdates.events()
		) | rpl::map([=](
				const QString &edit,
				const QString &restore,
				ChannelData *chat) {
			return chat ? edit : restore;
		});
	auto label = isGroup
		? _linkedChatUpdates.events(
		) | rpl::map([](ChannelData *chat) {
			return chat ? chat->name() : QString();
		}) | rpl::type_erased()
		: rpl::combine(
			tr::lng_manage_discussion_group_add(),
			_linkedChatUpdates.events()
		) | rpl::map([=](const QString &add, ChannelData *chat) {
			return chat ? chat->name() : add;
		}) | rpl::type_erased();
	AddButtonWithText(
		_controls.buttonsLayout,
		std::move(text),
		std::move(label),
		[=] { showEditLinkedChatBox(); },
		{ isGroup ? &st::menuIconChannel : &st::menuIconGroups });
	_linkedChatUpdates.fire_copy(*_linkedChatSavedValue);
}
//
//void Controller::fillInviteLinkButton() {
//	Expects(_controls.buttonsLayout != nullptr);
//
//	const auto buttonCallback = [=] {
//		Ui::show(Box<EditPeerTypeBox>(_peer), Ui::LayerOption::KeepOther);
//	};
//
//	AddButtonWithText(
//		_controls.buttonsLayout,
//		tr::lng_profile_invite_link_section(),
//		rpl::single(QString()), //Empty text.
//		buttonCallback);
//}

void Controller::fillForumButton() {
	Expects(_controls.buttonsLayout != nullptr);

	const auto button = _controls.forumToggle = _controls.buttonsLayout->add(
		EditPeerInfoBox::CreateButton(
			_controls.buttonsLayout,
			tr::lng_forum_topics_switch(),
			rpl::single(QString()),
			[] {},
			st::manageGroupTopicsButton,
			{ &st::menuIconTopics }));
	const auto unlocks = std::make_shared<rpl::event_stream<bool>>();
	button->toggleOn(
		rpl::single(_peer->isForum()) | rpl::then(unlocks->events())
	)->toggledValue(
	) | rpl::start_with_next([=](bool toggled) {
		if (_controls.forumToggleLocked && toggled) {
			unlocks->fire(false);
			if (_linkedChatSavedValue && *_linkedChatSavedValue) {
				ShowForumForDiscussionError(_navigation);
			} else {
				_navigation->showToast(
					tr::lng_forum_topics_not_enough(
						tr::now,
						lt_count,
						EnableForumMinMembers(_peer),
						Ui::Text::RichLangValue));
			}
		} else {
			_forumSavedValue = toggled;
			if (toggled) {
				_savingData.hiddenPreHistory = false;
			}
			refreshHistoryVisibility();
		}
	}, _controls.buttonsLayout->lifetime());
	refreshForumToggleLocked();
}

void Controller::refreshForumToggleLocked() {
	if (!_controls.forumToggle) {
		return;
	}
	const auto limit = EnableForumMinMembers(_peer);
	const auto chat = _peer->asChat();
	const auto channel = _peer->asChannel();
	const auto notenough = !_peer->isForum()
		&& ((chat ? chat->count : channel->membersCount()) < limit);
	const auto linked = _linkedChatSavedValue
		&& *_linkedChatSavedValue;
	const auto locked = _controls.forumToggleLocked = notenough || linked;
	_controls.forumToggle->setToggleLocked(locked);
}

void Controller::fillColorIndexButton() {
	Expects(_controls.buttonsLayout != nullptr);

	const auto show = _navigation->uiShow();
	AddPeerColorButton(_controls.buttonsLayout, show, _peer);
}

void Controller::fillSignaturesButton() {
	Expects(_controls.buttonsLayout != nullptr);

	const auto channel = _peer->asChannel();
	if (!channel) {
		return;
	}

	AddButtonWithText(
		_controls.buttonsLayout,
		tr::lng_edit_sign_messages(),
		rpl::single(QString()),
		[] {},
		{ &st::menuIconSigned }
	)->toggleOn(rpl::single(channel->addsSignature())
	)->toggledValue(
	) | rpl::start_with_next([=](bool toggled) {
		_signaturesSavedValue = toggled;
	}, _controls.buttonsLayout->lifetime());
}

void Controller::fillHistoryVisibilityButton() {
	Expects(_controls.buttonsLayout != nullptr);

	const auto wrapLayout = _controls.buttonsLayout->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			_controls.buttonsLayout,
			object_ptr<Ui::VerticalLayout>(_controls.buttonsLayout),
			st::boxOptionListPadding)); // Empty margins.
	_controls.historyVisibilityWrap = wrapLayout;

	const auto channel = _peer->asChannel();
	const auto container = wrapLayout->entity();

	_historyVisibilitySavedValue = (!channel || channel->hiddenPreHistory())
		? HistoryVisibility::Hidden
		: HistoryVisibility::Visible;
	_channelHasLocationOriginalValue = channel && channel->hasLocation();

	const auto updateHistoryVisibility =
		std::make_shared<rpl::event_stream<HistoryVisibility>>();

	const auto boxCallback = crl::guard(this, [=](HistoryVisibility checked) {
		updateHistoryVisibility->fire(std::move(checked));
		_historyVisibilitySavedValue = checked;
	});
	const auto buttonCallback = [=] {
		_peer->updateFull();
		const auto canEdit = [&] {
			if (const auto chat = _peer->asChat()) {
				return chat->canEditPreHistoryHidden();
			} else if (const auto channel = _peer->asChannel()) {
				return channel->canEditPreHistoryHidden();
			}
			Unexpected("User in HistoryVisibilityEdit.");
		}();
		if (!canEdit) {
			return;
		}
		_navigation->parentController()->show(Box(
			EditPeerHistoryVisibilityBox,
			_peer->isChat(),
			boxCallback,
			*_historyVisibilitySavedValue));
	};
	AddButtonWithText(
		container,
		tr::lng_manage_history_visibility_title(),
		updateHistoryVisibility->events(
		) | rpl::map([](HistoryVisibility flag) {
			return (HistoryVisibility::Visible == flag
				? tr::lng_manage_history_visibility_shown
				: tr::lng_manage_history_visibility_hidden)();
		}) | rpl::flatten_latest(),
		buttonCallback,
		{ &st::menuIconChatBubble });

	updateHistoryVisibility->fire_copy(*_historyVisibilitySavedValue);

	refreshHistoryVisibility();
}

void Controller::fillManageSection() {
	Expects(_controls.buttonsLayout != nullptr);

	if (_isBot) {
		const auto &container = _controls.buttonsLayout;

		::AddSkip(container, 0);
		fillBotUsernamesButton();
		fillBotEditIntroButton();
		fillBotEditCommandsButton();
		fillBotEditSettingsButton();
		Ui::AddSkip(
			container,
			st::editPeerTopButtonsLayoutSkipCustomBottom);
		container->add(object_ptr<Ui::DividerLabel>(
			container,
			object_ptr<Ui::FlatLabel>(
				container,
				tr::lng_manage_peer_bot_about(
					lt_bot,
					rpl::single(Ui::Text::Link(
						'@' + kBotManagerUsername.utf16(),
						_peer->session().createInternalLinkFull(
							kBotManagerUsername.utf16()))),
					Ui::Text::RichLangValue),
				st::boxDividerLabel),
			st::defaultBoxDividerLabelPadding));
		return;
	}

	const auto chat = _peer->asChat();
	const auto channel = _peer->asChannel();
	const auto isChannel = (!chat);
	if (!chat && !channel) {
		return;
	}

	const auto canEditType = isChannel
		? channel->amCreator()
		: chat->amCreator();
	const auto canEditSignatures = isChannel
		&& channel->canEditSignatures()
		&& !channel->isMegagroup();
	const auto canEditPreHistoryHidden = isChannel
		? channel->canEditPreHistoryHidden()
		: chat->canEditPreHistoryHidden();
	const auto canEditForum = isChannel
		? (channel->isMegagroup() && channel->amCreator())
		: chat->amCreator();
	const auto canEditPermissions = isChannel
		? channel->canEditPermissions()
		: chat->canEditPermissions();
	const auto canEditInviteLinks = isChannel
		? channel->canHaveInviteLink()
		: chat->canHaveInviteLink();
	const auto canViewAdmins = isChannel
		? channel->canViewAdmins()
		: chat->amIn();
	const auto canViewMembers = isChannel
		? channel->canViewMembers()
		: chat->amIn();
	const auto canViewKicked = isChannel
		&& (channel->isBroadcast() || channel->isGigagroup());
	const auto hasRecentActions = isChannel
		&& (channel->hasAdminRights() || channel->amCreator());
	const auto canEditStickers = isChannel && channel->canEditStickers();
	const auto canDeleteChannel = isChannel && channel->canDelete();
	const auto canEditColorIndex = isChannel && channel->canEditEmoji();
	const auto canViewOrEditLinkedChat = isChannel
		&& (channel->linkedChat()
			|| (channel->isBroadcast() && channel->canEditInformation()));

	::AddSkip(_controls.buttonsLayout, 0);

	if (canEditType) {
		fillPrivacyTypeButton();
	//} else if (canEditInviteLinks) {
	//	fillInviteLinkButton();
	}
	if (canViewOrEditLinkedChat) {
		fillLinkedChatButton();
	}
	if (canEditPreHistoryHidden) {
		fillHistoryVisibilityButton();
	}
	if (canEditForum) {
		fillForumButton();
	}
	if (canEditColorIndex) {
		fillColorIndexButton();
	}
	if (canEditSignatures) {
		fillSignaturesButton();
	}
	if (canEditPreHistoryHidden
		|| canEditForum
		|| canEditColorIndex
		|| canEditSignatures
		//|| canEditInviteLinks
		|| canViewOrEditLinkedChat
		|| canEditType) {
		::AddSkip(_controls.buttonsLayout);
	}

	if (canEditReactions()) {
		auto allowedReactions = Info::Profile::MigratedOrMeValue(
			_peer
		) | rpl::map([=](not_null<PeerData*> peer) {
			return peer->session().changes().peerFlagsValue(
				peer,
				Data::PeerUpdate::Flag::Reactions
			) | rpl::map([=] {
				return Data::PeerAllowedReactions(peer);
			});
		}) | rpl::flatten_latest();
		auto label = std::move(
			allowedReactions
		) | rpl::map([=](const Data::AllowedReactions &allowed) {
			const auto some = int(allowed.some.size());
			return (allowed.type != Data::AllowedReactionsType::Some)
				? tr::lng_manage_peer_reactions_on(tr::now)
				: some
				? QString::number(some)
				: tr::lng_manage_peer_reactions_off(tr::now);
		});
		AddButtonWithCount(
			_controls.buttonsLayout,
			tr::lng_manage_peer_reactions(),
			std::move(label),
			[=] { editReactions(); },
			{ &st::menuIconGroupReactions });
	}
	if (canEditPermissions) {
		AddButtonWithCount(
			_controls.buttonsLayout,
			tr::lng_manage_peer_permissions(),
			Info::Profile::MigratedOrMeValue(
				_peer
			) | rpl::map([=](not_null<PeerData*> peer) {
				return Info::Profile::RestrictionsCountValue(
					peer
				) | rpl::map([=](int count) {
					return QString::number(count)
						+ QString("/")
						+ QString::number(int(Data::ListOfRestrictions(
							{ .isForum = peer->isForum() }).size()));
				});
			}) | rpl::flatten_latest(),
			[=] { ShowEditPermissions(_navigation, _peer); },
			{ &st::menuIconPermissions });
	}
	if (canEditInviteLinks) {
		auto count = Info::Profile::MigratedOrMeValue(
			_peer
		) | rpl::map([=](not_null<PeerData*> peer) {
			peer->session().api().inviteLinks().requestMyLinks(peer);
			return peer->session().changes().peerUpdates(
				peer,
				Data::PeerUpdate::Flag::InviteLinks
			) | rpl::map([=] {
				return peer->session().api().inviteLinks().myLinks(
					peer).count;
			});
		}) | rpl::flatten_latest(
		) | rpl::start_spawning(_controls.buttonsLayout->lifetime());

		const auto wrap = _controls.buttonsLayout->add(
			object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
				_controls.buttonsLayout,
				object_ptr<Ui::VerticalLayout>(
					_controls.buttonsLayout)));
		AddButtonWithCount(
			wrap->entity(),
			tr::lng_manage_peer_invite_links(),
			rpl::duplicate(count) | ToPositiveNumberString(),
			[=] {
				_navigation->parentController()->show(Box(
					ManageInviteLinksBox,
					_peer,
					_peer->session().user(),
					0,
					0));
			},
			{ &st::menuIconLinks });
		wrap->toggle(true, anim::type::instant);
	}
	if (canViewAdmins) {
		AddButtonWithCount(
			_controls.buttonsLayout,
			tr::lng_manage_peer_administrators(),
			Info::Profile::MigratedOrMeValue(
				_peer
			) | rpl::map(
				Info::Profile::AdminsCountValue
			) | rpl::flatten_latest(
			) | ToPositiveNumberString(),
			[=] {
				ParticipantsBoxController::Start(
					_navigation,
					_peer,
					ParticipantsBoxController::Role::Admins);
			},
			{ &st::menuIconAdmin });
	}
	if (canViewMembers) {
		AddButtonWithCount(
			_controls.buttonsLayout,
			(_isGroup
				? tr::lng_manage_peer_members()
				: tr::lng_manage_peer_subscribers()),
			Info::Profile::MigratedOrMeValue(
				_peer
			) | rpl::map(
				Info::Profile::MembersCountValue
			) | rpl::flatten_latest(
			) | ToPositiveNumberString(),
			[=] {
				ParticipantsBoxController::Start(
					_navigation,
					_peer,
					ParticipantsBoxController::Role::Members);
			},
			{ &st::menuIconGroups });
	}

	fillPendingRequestsButton();

	if (canViewKicked) {
		AddButtonWithCount(
			_controls.buttonsLayout,
			tr::lng_manage_peer_removed_users(),
			Info::Profile::KickedCountValue(channel)
			| ToPositiveNumberString(),
			[=] {
				ParticipantsBoxController::Start(
					_navigation,
					_peer,
					ParticipantsBoxController::Role::Kicked);
			},
			{ &st::menuIconRemove });
	}
	if (hasRecentActions) {
		auto callback = [=] {
			_navigation->showSection(
				std::make_shared<AdminLog::SectionMemento>(channel));
		};
		AddButtonWithCount(
			_controls.buttonsLayout,
			tr::lng_manage_peer_recent_actions(),
			rpl::single(QString()), //Empty count.
			std::move(callback),
			{ &st::menuIconGroupLog });
	}

	if (canEditStickers || canDeleteChannel) {
		::AddSkip(_controls.buttonsLayout);
	}

	if (canEditStickers) {
		_controls.buttonsLayout->add(createStickersEdit());
	}

	if (canDeleteChannel) {
		AddButtonDelete(
			_controls.buttonsLayout,
			(_isGroup
				? tr::lng_profile_delete_group
				: tr::lng_profile_delete_channel)(),
			[=]{ deleteWithConfirmation(); }
		);
	}

	if (canEditStickers || canDeleteChannel) {
		::AddSkip(_controls.buttonsLayout);
	}
}

void Controller::editReactions() {
	const auto done = [=](const Data::AllowedReactions &chosen) {
		SaveAllowedReactions(_peer, chosen);
	};
	if (!_peer->isBroadcast()) {
		_navigation->uiShow()->show(Box(
			EditAllowedReactionsBox,
			EditAllowedReactionsArgs{
				.navigation = _navigation,
				.isGroup = true,
				.list = _navigation->session().data().reactions().list(
					Data::Reactions::Type::Active),
				.allowed = Data::PeerAllowedReactions(_peer),
				.save = done,
			}));
		return;
	}
	if (_controls.levelRequested) {
		return;
	}
	_controls.levelRequested = true;
	_api.request(MTPpremium_GetBoostsStatus(
		_peer->input
	)).done([=](const MTPpremium_BoostsStatus &result) {
		_controls.levelRequested = false;
		if (const auto channel = _peer->asChannel()) {
			channel->updateLevelHint(result.data().vlevel().v);
		}
		const auto link = qs(result.data().vboost_url());
		const auto weak = base::make_weak(_navigation->parentController());
		auto counters = ParseBoostCounters(result);
		counters.mine = 0; // Don't show current level as just-reached.
		const auto askForBoosts = [=](int required) {
			if (const auto strong = weak.get()) {
				const auto openStatistics = [=, peer = _peer] {
					strong->showSection(Info::Boosts::Make(peer));
				};
				strong->show(Box(Ui::AskBoostBox, Ui::AskBoostBoxData{
					.link = link,
					.boost = counters,
					.reason = { Ui::AskBoostCustomReactions{ required } },
				}, openStatistics, nullptr));
			}
		};
		_navigation->uiShow()->show(Box(
			EditAllowedReactionsBox,
			EditAllowedReactionsArgs{
				.navigation = _navigation,
				.allowedCustomReactions = counters.level,
				.customReactionsHardLimit = Data::PremiumLimits(
					&_peer->session()).maxBoostLevel(),
				.list = _navigation->session().data().reactions().list(
					Data::Reactions::Type::Active),
				.allowed = Data::PeerAllowedReactions(_peer),
				.askForBoosts = askForBoosts,
				.save = done,
			}));
	}).send();
}

void Controller::fillPendingRequestsButton() {
	auto pendingRequestsCount = Info::Profile::MigratedOrMeValue(
		_peer
	) | rpl::map(
		Info::Profile::PendingRequestsCountValue
	) | rpl::flatten_latest(
	) | rpl::start_spawning(_controls.buttonsLayout->lifetime());
	const auto wrap = _controls.buttonsLayout->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			_controls.buttonsLayout,
			object_ptr<Ui::VerticalLayout>(
				_controls.buttonsLayout)));
	AddButtonWithCount(
		wrap->entity(),
		(_isGroup
			? tr::lng_manage_peer_requests()
			: tr::lng_manage_peer_requests_channel()),
		rpl::duplicate(pendingRequestsCount) | ToPositiveNumberString(),
		[=] { RequestsBoxController::Start(_navigation, _peer); },
		{ &st::menuIconInvite });
	std::move(
		pendingRequestsCount
	) | rpl::start_with_next([=](int count) {
		wrap->toggle(count > 0, anim::type::instant);
	}, wrap->lifetime());
}

void Controller::fillBotUsernamesButton() {
	Expects(_isBot);

	const auto user = _peer->asUser();

	auto localUsernames = rpl::single(
		user->usernames()
	) | rpl::map([](const std::vector<QString> &usernames) {
		return ranges::views::all(
			usernames
		) | ranges::views::transform([](const QString &u) {
			return Data::Username{ u };
		}) | ranges::to_vector;
	});
	auto usernamesValue = std::move(
		localUsernames
	) | rpl::then(
		_peer->session().api().usernames().loadUsernames(_peer)
	);
	auto rightLabel = rpl::duplicate(
		usernamesValue
	) | rpl::map([=](const Data::Usernames &usernames) {
		if (usernames.size() <= 1) {
			return user->session().createInternalLink(user->username());
		} else {
			const auto active = ranges::count_if(
				usernames,
				[](const Data::Username &u) { return u.active; });
			return u"%1/%2"_q.arg(active).arg(usernames.size());
		}
	});
	auto leftLabel = std::move(
		usernamesValue
	) | rpl::map([=](const Data::Usernames &usernames) {
		return (usernames.size() <= 1)
			? tr::lng_manage_peer_bot_public_link()
			: tr::lng_manage_peer_bot_public_links();
	}) | rpl::flatten_latest();

	_controls.buttonsLayout->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			_controls.buttonsLayout,
			object_ptr<Ui::VerticalLayout>(
				_controls.buttonsLayout)));
	AddButtonWithCount(
		_controls.buttonsLayout,
		std::move(leftLabel),
		std::move(rightLabel),
		[=] {
			_navigation->uiShow()->showBox(Box(UsernamesBox, user));
		},
		{ &st::menuIconLinks });
}

void Controller::fillBotEditIntroButton() {
	Expects(_isBot);

	const auto user = _peer->asUser();
	AddButtonWithCount(
		_controls.buttonsLayout,
		tr::lng_manage_peer_bot_edit_intro(),
		rpl::never<QString>(),
		[=] { toggleBotManager(u"%1-intro"_q.arg(user->username())); },
		{ &st::menuIconEdit });
}

void Controller::fillBotEditCommandsButton() {
	Expects(_isBot);

	const auto user = _peer->asUser();
	AddButtonWithCount(
		_controls.buttonsLayout,
		tr::lng_manage_peer_bot_edit_commands(),
		rpl::never<QString>(),
		[=] { toggleBotManager(u"%1-commands"_q.arg(user->username())); },
		{ &st::menuIconBotCommands });
}

void Controller::fillBotEditSettingsButton() {
	Expects(_isBot);

	const auto user = _peer->asUser();
	AddButtonWithCount(
		_controls.buttonsLayout,
		tr::lng_manage_peer_bot_edit_settings(),
		rpl::never<QString>(),
		[=] { toggleBotManager(user->username()); },
		{ &st::menuIconSettings });
}

void Controller::submitTitle() {
	Expects(_controls.title != nullptr);

	if (_controls.title->getLastText().isEmpty()) {
		_controls.title->showError();
		_box->scrollToWidget(_controls.title);
	} else if (_controls.description) {
		_controls.description->setFocus();
		_box->scrollToWidget(_controls.description);
	}
}

void Controller::submitDescription() {
	Expects(_controls.title != nullptr);
	Expects(_controls.description != nullptr);

	if (_controls.title->getLastText().isEmpty()) {
		_controls.title->showError();
		_box->scrollToWidget(_controls.title);
	} else {
		save();
	}
}

std::optional<Controller::Saving> Controller::validate() const {
	auto result = Saving();
	if (validateUsernamesOrder(result)
		&& validateUsername(result)
		&& validateLinkedChat(result)
		&& validateTitle(result)
		&& validateDescription(result)
		&& validateHistoryVisibility(result)
		&& validateForum(result)
		&& validateSignatures(result)
		&& validateForwards(result)
		&& validateJoinToWrite(result)
		&& validateRequestToJoin(result)) {
		return result;
	}
	return {};
}

bool Controller::validateUsernamesOrder(Saving &to) const {
	if (!_typeDataSavedValue) {
		return true;
	} else if (_typeDataSavedValue->privacy != Privacy::HasUsername) {
		to.usernamesOrder = std::vector<QString>();
		return true;
	}
	to.usernamesOrder = _typeDataSavedValue->usernamesOrder;
	return true;
}

bool Controller::validateUsername(Saving &to) const {
	if (!_typeDataSavedValue) {
		return true;
	} else if (_typeDataSavedValue->privacy != Privacy::HasUsername) {
		to.username = QString();
		return true;
	}
	const auto username = _typeDataSavedValue->username;
	if (username.isEmpty()) {
		to.username = QString();
		return true;
	}
	to.username = username;
	return true;
}

bool Controller::validateLinkedChat(Saving &to) const {
	if (!_linkedChatSavedValue) {
		return true;
	}
	to.linkedChat = *_linkedChatSavedValue;
	return true;
}

bool Controller::validateTitle(Saving &to) const {
	if (!_controls.title) {
		return true;
	}
	const auto title = _controls.title->getLastText().trimmed();
	if (title.isEmpty()) {
		_controls.title->showError();
		_box->scrollToWidget(_controls.title);
		return false;
	}
	to.title = title;
	return true;
}

bool Controller::validateDescription(Saving &to) const {
	if (!_controls.description) {
		return true;
	}
	to.description = _controls.description->getLastText().trimmed();
	return true;
}

bool Controller::validateHistoryVisibility(Saving &to) const {
	if (!_controls.historyVisibilityWrap
		|| !_controls.historyVisibilityWrap->toggled()
		|| _channelHasLocationOriginalValue
		|| (_typeDataSavedValue
			&& _typeDataSavedValue->privacy == Privacy::HasUsername)) {
		return true;
	}
	to.hiddenPreHistory
		= (_historyVisibilitySavedValue == HistoryVisibility::Hidden);
	return true;
}

bool Controller::validateForum(Saving &to) const {
	if (!_forumSavedValue.has_value()) {
		return true;
	}
	to.forum = _forumSavedValue;
	return true;
}

bool Controller::validateSignatures(Saving &to) const {
	if (!_signaturesSavedValue.has_value()) {
		return true;
	}
	to.signatures = _signaturesSavedValue;
	return true;
}

bool Controller::validateForwards(Saving &to) const {
	if (!_typeDataSavedValue) {
		return true;
	}
	to.noForwards = _typeDataSavedValue->noForwards;
	return true;
}

bool Controller::validateJoinToWrite(Saving &to) const {
	if (!_typeDataSavedValue) {
		return true;
	}
	to.joinToWrite = _typeDataSavedValue->joinToWrite;
	return true;
}

bool Controller::validateRequestToJoin(Saving &to) const {
	if (!_typeDataSavedValue) {
		return true;
	}
	to.requestToJoin = _typeDataSavedValue->requestToJoin;
	return true;
}

void Controller::save() {
	Expects(_wrap != nullptr);

	if (!_saveStagesQueue.empty()) {
		return;
	}
	if (const auto saving = validate()) {
		_savingData = *saving;
		pushSaveStage([=] { saveUsernamesOrder(); });
		pushSaveStage([=] { saveUsername(); });
		pushSaveStage([=] { saveLinkedChat(); });
		pushSaveStage([=] { saveTitle(); });
		pushSaveStage([=] { saveDescription(); });
		pushSaveStage([=] { saveHistoryVisibility(); });
		pushSaveStage([=] { saveForum(); });
		pushSaveStage([=] { saveSignatures(); });
		pushSaveStage([=] { saveForwards(); });
		pushSaveStage([=] { saveJoinToWrite(); });
		pushSaveStage([=] { saveRequestToJoin(); });
		pushSaveStage([=] { savePhoto(); });
		continueSave();
	}
}

void Controller::pushSaveStage(FnMut<void()> &&lambda) {
	_saveStagesQueue.push_back(std::move(lambda));
}

void Controller::continueSave() {
	if (!_saveStagesQueue.empty()) {
		auto next = std::move(_saveStagesQueue.front());
		_saveStagesQueue.pop_front();
		next();
	}
}

void Controller::cancelSave() {
	_saveStagesQueue.clear();
}

void Controller::saveUsernamesOrder() {
	const auto channel = _peer->asChannel();
	if (!_savingData.usernamesOrder || !channel) {
		return continueSave();
	}
	if (_savingData.usernamesOrder->empty()) {
		_api.request(MTPchannels_DeactivateAllUsernames(
			channel->inputChannel
		)).done([=] {
			channel->setUsernames(channel->editableUsername().isEmpty()
				? Data::Usernames()
				: Data::Usernames{
					{ channel->editableUsername(), true, true }
				});
			continueSave();
		}).send();
	} else {
		const auto lifetime = std::make_shared<rpl::lifetime>();
		const auto newUsernames = (*_savingData.usernamesOrder);
		_peer->session().api().usernames().reorder(
			_peer,
			newUsernames
		) | rpl::start_with_done([=] {
			channel->setUsernames(ranges::views::all(
				newUsernames
			) | ranges::views::transform([&](QString username) {
				const auto editable =
					(channel->editableUsername() == username);
				return Data::Username{
					.username = std::move(username),
					.active = true,
					.editable = editable,
				};
			}) | ranges::to_vector);
			continueSave();
			lifetime->destroy();
		}, *lifetime);
	}
}

void Controller::saveUsername() {
	const auto channel = _peer->asChannel();
	const auto username = (channel ? channel->editableUsername() : QString());
	if (!_savingData.username || *_savingData.username == username) {
		return continueSave();
	} else if (!channel) {
		const auto saveForChannel = [=](not_null<ChannelData*> channel) {
			if (_peer->asChannel() == channel) {
				saveUsername();
			} else {
				cancelSave();
			}
		};
		_peer->session().api().migrateChat(
			_peer->asChat(),
			crl::guard(this, saveForChannel));
		return;
	}

	const auto newUsername = (*_savingData.username);
	_api.request(MTPchannels_UpdateUsername(
		channel->inputChannel,
		MTP_string(newUsername)
	)).done([=] {
		channel->setName(
			TextUtilities::SingleLine(channel->name()),
			newUsername);
		continueSave();
	}).fail([=](const MTP::Error &error) {
		const auto &type = error.type();
		if (type == u"USERNAME_NOT_MODIFIED"_q) {
			channel->setName(
				TextUtilities::SingleLine(channel->name()),
				TextUtilities::SingleLine(*_savingData.username));
			continueSave();
			return;
		}

		// Very rare case.
		showEditPeerTypeBox([&] {
			if (type == u"USERNAME_INVALID"_q) {
				return tr::lng_create_channel_link_invalid();
			} else if (type == u"USERNAME_OCCUPIED"_q
				|| type == u"USERNAMES_UNAVAILABLE"_q) {
				return tr::lng_create_channel_link_occupied();
			}
			return tr::lng_create_channel_link_invalid();
		}());
		cancelSave();
	}).send();
}

void Controller::saveLinkedChat() {
	const auto channel = _peer->asChannel();
	if (!channel) {
		return continueSave();
	}
	if (!_savingData.linkedChat
		|| *_savingData.linkedChat == channel->linkedChat()) {
		return continueSave();
	}

	const auto chat = *_savingData.linkedChat;
	if (channel->isBroadcast() && chat && chat->hiddenPreHistory()) {
		togglePreHistoryHidden(
			chat,
			false,
			[=] { saveLinkedChat(); },
			[=] { cancelSave(); });
		return;
	}

	const auto input = *_savingData.linkedChat
		? (*_savingData.linkedChat)->inputChannel
		: MTP_inputChannelEmpty();
	_api.request(MTPchannels_SetDiscussionGroup(
		(channel->isBroadcast() ? channel->inputChannel : input),
		(channel->isBroadcast() ? input : channel->inputChannel)
	)).done([=] {
		channel->setLinkedChat(*_savingData.linkedChat);
		continueSave();
	}).fail([=] {
		cancelSave();
	}).send();
}

void Controller::saveTitle() {
	if (!_savingData.title || *_savingData.title == _peer->name()) {
		return continueSave();
	}

	const auto onDone = [=](const MTPUpdates &result) {
		_peer->session().api().applyUpdates(result);
		continueSave();
	};
	const auto onFail = [=](const MTP::Error &error) {
		const auto &type = error.type();
		if (type == u"CHAT_NOT_MODIFIED"_q
			|| type == u"CHAT_TITLE_NOT_MODIFIED"_q) {
			if (const auto channel = _peer->asChannel()) {
				channel->setName(
					*_savingData.title,
					channel->editableUsername());
			} else if (const auto chat = _peer->asChat()) {
				chat->setName(*_savingData.title);
			}
			continueSave();
			return;
		}
		_controls.title->showError();
		if (type == u"NO_CHAT_TITLE"_q) {
			_box->scrollToWidget(_controls.title);
		}
		cancelSave();
	};

	if (const auto channel = _peer->asChannel()) {
		_api.request(MTPchannels_EditTitle(
			channel->inputChannel,
			MTP_string(*_savingData.title)
		)).done(std::move(onDone)
		).fail(std::move(onFail)
		).send();
	} else if (const auto chat = _peer->asChat()) {
		_api.request(MTPmessages_EditChatTitle(
			chat->inputChat,
			MTP_string(*_savingData.title)
		)).done(std::move(onDone)
		).fail(std::move(onFail)
		).send();
	} else if (_isBot) {
		_api.request(MTPbots_GetBotInfo(
			MTP_flags(MTPbots_GetBotInfo::Flag::f_bot),
			_peer->asUser()->inputUser,
			MTPstring() // Lang code.
		)).done([=](const MTPbots_BotInfo &result) {
			const auto was = qs(result.data().vname());
			const auto now = *_savingData.title;
			if (was == now) {
				return continueSave();
			}
			using Flag = MTPbots_SetBotInfo::Flag;
			_api.request(MTPbots_SetBotInfo(
				MTP_flags(Flag::f_bot | Flag::f_name),
				_peer->asUser()->inputUser,
				MTPstring(), // Lang code.
				MTP_string(now), // Name.
				MTPstring(), // About.
				MTPstring() // Description.
			)).done([=] {
				continueSave();
			}).fail(std::move(onFail)
			).send();
		}).fail(std::move(onFail)
		).send();
	} else {
		continueSave();
	}
}

void Controller::saveDescription() {
	if (!_savingData.description
		|| *_savingData.description == _peer->about()) {
		return continueSave();
	}
	const auto successCallback = [=] {
		_peer->setAbout(*_savingData.description);
		continueSave();
	};
	if (_isBot) {
		_api.request(MTPbots_GetBotInfo(
			MTP_flags(MTPbots_GetBotInfo::Flag::f_bot),
			_peer->asUser()->inputUser,
			MTPstring() // Lang code.
		)).done([=](const MTPbots_BotInfo &result) {
			const auto was = qs(result.data().vabout());
			const auto now = *_savingData.description;
			if (was == now) {
				return continueSave();
			}
			using Flag = MTPbots_SetBotInfo::Flag;
			_api.request(MTPbots_SetBotInfo(
				MTP_flags(Flag::f_bot | Flag::f_about),
				_peer->asUser()->inputUser,
				MTPstring(), // Lang code.
				MTPstring(), // Name.
				MTP_string(now), // About.
				MTPstring() // Description.
			)).done([=] {
				successCallback();
			}).fail([=] {
				_controls.description->showError();
				cancelSave();
			}).send();
		}).fail([=] {
			continueSave();
		}).send();
		return;
	}
	_api.request(MTPmessages_EditChatAbout(
		_peer->input,
		MTP_string(*_savingData.description)
	)).done([=] {
		successCallback();
	}).fail([=](const MTP::Error &error) {
		const auto &type = error.type();
		if (type == u"CHAT_ABOUT_NOT_MODIFIED"_q) {
			successCallback();
			return;
		}
		_controls.description->showError();
		cancelSave();
	}).send();
}

void Controller::saveHistoryVisibility() {
	const auto channel = _peer->asChannel();
	const auto hidden = channel ? channel->hiddenPreHistory() : true;
	if (!_savingData.hiddenPreHistory
		|| *_savingData.hiddenPreHistory == hidden) {
		return continueSave();
	} else if (!channel) {
		const auto saveForChannel = [=](not_null<ChannelData*> channel) {
			if (_peer->asChannel() == channel) {
				saveHistoryVisibility();
			} else {
				cancelSave();
			}
		};
		_peer->session().api().migrateChat(
			_peer->asChat(),
			crl::guard(this, saveForChannel));
		return;
	}
	togglePreHistoryHidden(
		channel,
		*_savingData.hiddenPreHistory,
		[=] { continueSave(); },
		[=] { cancelSave(); });
}

void Controller::toggleBotManager(const QString &command) {
	const auto controller = _navigation->parentController();
	_api.request(MTPcontacts_ResolveUsername(
		MTP_string(kBotManagerUsername.utf16())
	)).done([=](const MTPcontacts_ResolvedPeer &result) {
		_peer->owner().processUsers(result.data().vusers());
		_peer->owner().processChats(result.data().vchats());
		const auto botPeer = _peer->owner().peerLoaded(
			peerFromMTP(result.data().vpeer()));
		if (const auto bot = botPeer ? botPeer->asUser() : nullptr) {
			const auto show = controller->uiShow();
			_peer->session().api().sendBotStart(show, bot, bot, command);
			controller->showPeerHistory(bot);
		}
	}).send();
}

void Controller::togglePreHistoryHidden(
		not_null<ChannelData*> channel,
		bool hidden,
		Fn<void()> done,
		Fn<void()> fail) {
	const auto apply = [=] {
		// Update in the result doesn't contain the
		// channelFull:flags field which holds this value.
		// So after saving we need to update it manually.
		const auto flags = channel->flags();
		const auto flag = ChannelDataFlag::PreHistoryHidden;
		channel->setFlags(hidden ? (flags | flag) : (flags & ~flag));

		done();
	};
	_api.request(MTPchannels_TogglePreHistoryHidden(
		channel->inputChannel,
		MTP_bool(hidden)
	)).done([=](const MTPUpdates &result) {
		channel->session().api().applyUpdates(result);
		apply();
	}).fail([=](const MTP::Error &error) {
		if (error.type() == u"CHAT_NOT_MODIFIED"_q) {
			apply();
		} else {
			fail();
		}
	}).send();
}

void Controller::saveForum() {
	const auto channel = _peer->asChannel();
	if (!_savingData.forum
		|| *_savingData.forum == _peer->isForum()) {
		return continueSave();
	} else if (!channel) {
		const auto saveForChannel = [=](not_null<ChannelData*> channel) {
			if (_peer->asChannel() == channel) {
				saveForum();
			} else {
				cancelSave();
			}
		};
		_peer->session().api().migrateChat(
			_peer->asChat(),
			crl::guard(this, saveForChannel));
		return;
	}
	_api.request(MTPchannels_ToggleForum(
		channel->inputChannel,
		MTP_bool(*_savingData.forum)
	)).done([=](const MTPUpdates &result) {
		channel->session().api().applyUpdates(result);
		continueSave();
	}).fail([=](const MTP::Error &error) {
		if (error.type() == u"CHAT_NOT_MODIFIED"_q) {
			continueSave();
		} else {
			cancelSave();
		}
	}).send();
}

void Controller::saveSignatures() {
	const auto channel = _peer->asChannel();
	if (!_savingData.signatures
		|| !channel
		|| *_savingData.signatures == channel->addsSignature()) {
		return continueSave();
	}
	_api.request(MTPchannels_ToggleSignatures(
		channel->inputChannel,
		MTP_bool(*_savingData.signatures)
	)).done([=](const MTPUpdates &result) {
		channel->session().api().applyUpdates(result);
		continueSave();
	}).fail([=](const MTP::Error &error) {
		if (error.type() == u"CHAT_NOT_MODIFIED"_q) {
			continueSave();
		} else {
			cancelSave();
		}
	}).send();
}

void Controller::saveForwards() {
	if (!_savingData.noForwards
		|| *_savingData.noForwards != _peer->allowsForwarding()) {
		return continueSave();
	}
	_api.request(MTPmessages_ToggleNoForwards(
		_peer->input,
		MTP_bool(*_savingData.noForwards)
	)).done([=](const MTPUpdates &result) {
		_peer->session().api().applyUpdates(result);
		continueSave();
	}).fail([=](const MTP::Error &error) {
		if (error.type() == u"CHAT_NOT_MODIFIED"_q) {
			continueSave();
		} else {
			cancelSave();
		}
	}).send();
}

void Controller::saveJoinToWrite() {
	const auto joinToWrite = _peer->isMegagroup()
		&& _peer->asChannel()->joinToWrite();
	if (!_savingData.joinToWrite
		|| *_savingData.joinToWrite == joinToWrite) {
		return continueSave();
	}
	_api.request(MTPchannels_ToggleJoinToSend(
		_peer->asChannel()->inputChannel,
		MTP_bool(*_savingData.joinToWrite)
	)).done([=](const MTPUpdates &result) {
		_peer->session().api().applyUpdates(result);
		continueSave();
	}).fail([=](const MTP::Error &error) {
		if (error.type() == u"CHAT_NOT_MODIFIED"_q) {
			continueSave();
		} else {
			cancelSave();
		}
	}).send();
}

void Controller::saveRequestToJoin() {
	const auto requestToJoin = _peer->isMegagroup()
		&& _peer->asChannel()->requestToJoin();
	if (!_savingData.requestToJoin
		|| *_savingData.requestToJoin == requestToJoin) {
		return continueSave();
	}
	_api.request(MTPchannels_ToggleJoinRequest(
		_peer->asChannel()->inputChannel,
		MTP_bool(*_savingData.requestToJoin)
	)).done([=](const MTPUpdates &result) {
		_peer->session().api().applyUpdates(result);
		continueSave();
	}).fail([=](const MTP::Error &error) {
		if (error.type() == u"CHAT_NOT_MODIFIED"_q) {
			continueSave();
		} else {
			cancelSave();
		}
	}).send();
}

void Controller::savePhoto() {
	auto image = _controls.photo
		? _controls.photo->takeResultImage()
		: QImage();
	if (!image.isNull()) {
		_peer->session().api().peerPhoto().upload(
			_peer,
			{ std::move(image) });
	}
	_box->closeBox();
}

void Controller::deleteWithConfirmation() {
	const auto channel = _peer->asChannel();
	Assert(channel != nullptr);

	const auto text = (_isGroup
		? tr::lng_sure_delete_group
		: tr::lng_sure_delete_channel)(tr::now);
	const auto deleteCallback = crl::guard(this, [=] {
		deleteChannel();
	});
	_navigation->parentController()->show(
		Ui::MakeConfirmBox({
			.text = text,
			.confirmed = deleteCallback,
			.confirmText = tr::lng_box_delete(),
			.confirmStyle = &st::attentionBoxButton,
		}));
}

void Controller::deleteChannel() {
	Expects(_peer->isChannel());

	const auto channel = _peer->asChannel();
	const auto chat = channel->migrateFrom();

	const auto session = &_peer->session();

	_navigation->parentController()->hideLayer();
	Core::App().closeChatFromWindows(channel);
	if (chat) {
		session->api().deleteConversation(chat, false);
	}
	session->api().request(MTPchannels_DeleteChannel(
		channel->inputChannel
	)).done([=](const MTPUpdates &result) {
		session->api().applyUpdates(result);
	//}).fail([=](const MTP::Error &error) {
	//	if (error.type() == u"CHANNEL_TOO_LARGE"_q) {
	//		Ui::show(Box<Ui::InformBox>(tr::lng_cant_delete_channel(tr::now)));
	//	}
	}).send();
}

} // namespace


EditPeerInfoBox::EditPeerInfoBox(
	QWidget*,
	not_null<Window::SessionNavigation*> navigation,
	not_null<PeerData*> peer)
: _navigation(navigation)
, _peer(peer->migrateToOrMe()) {
}

void EditPeerInfoBox::prepare() {
	const auto controller = Ui::CreateChild<Controller>(
		this,
		_navigation,
		this,
		_peer);
	_focusRequests.events(
	) | rpl::start_with_next(
		[=] { controller->setFocus(); },
		lifetime());
	auto content = controller->createContent();
	setDimensionsToContent(st::boxWideWidth, content);
	setInnerWidget(object_ptr<Ui::OverrideMargins>(
		this,
		std::move(content)));
}

object_ptr<Ui::SettingsButton> EditPeerInfoBox::CreateButton(
		not_null<QWidget*> parent,
		rpl::producer<QString> &&text,
		rpl::producer<QString> &&count,
		Fn<void()> callback,
		const style::SettingsCountButton &st,
		Settings::IconDescriptor &&descriptor) {
	auto result = object_ptr<Ui::SettingsButton>(
		parent,
		rpl::duplicate(text),
		st.button);
	const auto button = result.data();
	button->addClickHandler(callback);
	if (descriptor) {
		AddButtonIcon(
			button,
			st.button,
			std::move(descriptor));
	}

	auto labelText = rpl::combine(
		std::move(text),
		std::move(count),
		button->widthValue()
	) | rpl::map([&st](const QString &text, const QString &count, int width) {
		const auto available = width
			- st.button.padding.left()
			- (st.button.style.font->spacew * 2)
			- st.button.style.font->width(text)
			- st.labelPosition.x();
		const auto required = st.label.style.font->width(count);
		return (required > available)
			? st.label.style.font->elided(count, std::max(available, 0))
			: count;
	});

	const auto label = Ui::CreateChild<Ui::FlatLabel>(
		button,
		std::move(labelText),
		st.label);
	label->setAttribute(Qt::WA_TransparentForMouseEvents);

	rpl::combine(
		button->widthValue(),
		label->widthValue()
	) | rpl::start_with_next([=, &st](int outerWidth, int width) {
		label->moveToRight(
			st.labelPosition.x(),
			st.labelPosition.y(),
			outerWidth);
	}, label->lifetime());

	return result;
}

bool EditPeerInfoBox::Available(not_null<PeerData*> peer) {
	if (const auto bot = peer->asUser()) {
		return bot->botInfo && bot->botInfo->canEditInformation;
	} else if (const auto chat = peer->asChat()) {
		return false
			|| chat->canEditInformation()
			|| chat->canEditPermissions();
	} else if (const auto channel = peer->asChannel()) {
		// canViewMembers() is removed, because in supergroups you
		// see them in profile and in channels only admins can see them.

		// canViewAdmins() is removed, because in supergroups it is
		// always true and in channels it is equal to canViewBanned().

		return false
			//|| channel->canViewMembers()
			//|| channel->canViewAdmins()
			|| channel->canViewBanned()
			|| channel->canEditInformation()
			|| channel->canEditPermissions()
			|| channel->hasAdminRights()
			|| channel->amCreator();
	} else {
		return false;
	}
}
