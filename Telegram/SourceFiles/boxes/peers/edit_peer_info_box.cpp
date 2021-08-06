/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/peers/edit_peer_info_box.h"

#include "apiwrap.h"
#include "main/main_session.h"
#include "boxes/add_contact_box.h"
#include "boxes/confirm_box.h"
#include "boxes/peer_list_controllers.h"
#include "boxes/peers/edit_participants_box.h"
#include "boxes/peers/edit_peer_type_box.h"
#include "boxes/peers/edit_peer_history_visibility_box.h"
#include "boxes/peers/edit_peer_permissions_box.h"
#include "boxes/peers/edit_peer_invite_links.h"
#include "boxes/peers/edit_linked_chat_box.h"
#include "boxes/stickers_box.h"
#include "ui/boxes/single_choice_box.h"
#include "chat_helpers/emoji_suggestions_widget.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_changes.h"
#include "history/admin_log/history_admin_log_section.h"
#include "info/profile/info_profile_values.h"
#include "lang/lang_keys.h"
#include "mainwidget.h"
#include "mainwindow.h"
#include "mtproto/sender.h"
#include "ui/rp_widget.h"
#include "ui/special_buttons.h"
#include "ui/toast/toast.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/input_fields.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/box_content_divider.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "info/profile/info_profile_icon.h"
#include "app.h"
#include "apiwrap.h"
#include "api/api_invite_links.h"
#include "facades.h"
#include "styles/style_layers.h"
#include "styles/style_boxes.h"
#include "styles/style_info.h"

namespace {

auto ToPositiveNumberString() {
	return rpl::map([](int count) {
		return count ? QString::number(count) : QString();
	});
}

auto ToPositiveNumberStringRestrictions() {
	return rpl::map([](int count) {
		return QString::number(count)
		+ QString("/")
		+ QString::number(int(Data::ListOfRestrictions().size()));
	});
}

void AddSkip(
		not_null<Ui::VerticalLayout*> container,
		int top = st::editPeerTopButtonsLayoutSkip,
		int bottom = st::editPeerTopButtonsLayoutSkipToBottom) {
	container->add(object_ptr<Ui::FixedHeightWidget>(
		container,
		top));
	container->add(object_ptr<Ui::BoxContentDivider>(container));
	container->add(object_ptr<Ui::FixedHeightWidget>(
		container,
		bottom));
}

void AddButtonWithCount(
		not_null<Ui::VerticalLayout*> parent,
		rpl::producer<QString> &&text,
		rpl::producer<QString> &&count,
		Fn<void()> callback,
		const style::icon &icon) {
	parent->add(EditPeerInfoBox::CreateButton(
		parent,
		std::move(text),
		std::move(count),
		std::move(callback),
		st::manageGroupButton,
		&icon));
}

object_ptr<Ui::SettingsButton> CreateButtonWithText(
		not_null<QWidget*> parent,
		rpl::producer<QString> &&text,
		rpl::producer<QString> &&label,
		Fn<void()> callback) {
	return EditPeerInfoBox::CreateButton(
		parent,
		std::move(text),
		std::move(label),
		std::move(callback),
		st::manageGroupTopButtonWithText,
		nullptr);
}

Ui::SettingsButton *AddButtonWithText(
		not_null<Ui::VerticalLayout*> parent,
		rpl::producer<QString> &&text,
		rpl::producer<QString> &&label,
		Fn<void()> callback) {
	return parent->add(CreateButtonWithText(
		parent,
		std::move(text),
		std::move(label),
		std::move(callback)));
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
		nullptr));
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
		if (error.type() != qstr("CHAT_NOT_MODIFIED")) {
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
		if (error.type() != qstr("CHAT_NOT_MODIFIED")) {
			return;
		}
		channel->setSlowmodeSeconds(seconds);
		done();
	}).send();

	api->registerModifyRequest(key, requestId);
}

void ShowEditPermissions(
		not_null<Window::SessionNavigation*> navigation,
		not_null<PeerData*> peer) {
	auto content = Box<EditPeerPermissionsBox>(navigation, peer);
	const auto box = QPointer<EditPeerPermissionsBox>(content.data());
	navigation->parentController()->show(
		std::move(content),
		Ui::LayerOption::KeepOther);
	const auto saving = box->lifetime().make_state<int>(0);
	const auto save = [=](
			not_null<PeerData*> peer,
			EditPeerPermissionsBox::Result result) {
		Expects(result.slowmodeSeconds == 0 || peer->isChannel());

		const auto close = crl::guard(box, [=] { box->closeBox(); });
		SaveDefaultRestrictions(
			peer,
			result.rights,
			close);
		if (const auto channel = peer->asChannel()) {
			SaveSlowmodeSeconds(channel, result.slowmodeSeconds, close);
		}
	};
	box->saveEvents(
	) | rpl::start_with_next([=](EditPeerPermissionsBox::Result result) {
		if (*saving) {
			return;
		}
		*saving = true;

		const auto saveFor = peer->migrateToOrMe();
		const auto chat = saveFor->asChat();
		if (!result.slowmodeSeconds || !chat) {
			save(saveFor, result);
			return;
		}
		const auto api = &peer->session().api();
		api->migrateChat(chat, [=](not_null<ChannelData*> channel) {
			save(channel, result);
		}, [=](const MTP::Error &error) {
			*saving = false;
		});
	}, box->lifetime());
}

} // namespace

namespace {

constexpr auto kMaxGroupChannelTitle = 128; // See also add_contact_box.
constexpr auto kMaxChannelDescription = 255; // See also add_contact_box.

class Controller : public base::has_weak_ptr {
public:
	Controller(
		not_null<Window::SessionNavigation*> navigation,
		not_null<Ui::BoxContent*> box,
		not_null<PeerData*> peer);

	object_ptr<Ui::VerticalLayout> createContent();
	void setFocus();

private:
	struct Controls {
		Ui::InputField *title = nullptr;
		Ui::InputField *description = nullptr;
		Ui::UserpicButton *photo = nullptr;
		rpl::lifetime initialPhotoImageWaiting;
		Ui::VerticalLayout *buttonsLayout = nullptr;
		Ui::SlideWrap<> *historyVisibilityWrap = nullptr;
	};
	struct Saving {
		std::optional<QString> username;
		std::optional<QString> title;
		std::optional<QString> description;
		std::optional<bool> hiddenPreHistory;
		std::optional<bool> signatures;
		std::optional<ChannelData*> linkedChat;
	};

	object_ptr<Ui::RpWidget> createPhotoAndTitleEdit();
	object_ptr<Ui::RpWidget> createTitleEdit();
	object_ptr<Ui::RpWidget> createPhotoEdit();
	object_ptr<Ui::RpWidget> createDescriptionEdit();
	object_ptr<Ui::RpWidget> createManageGroupButtons();
	object_ptr<Ui::RpWidget> createStickersEdit();

	bool canEditInformation() const;
	void refreshHistoryVisibility();
	void showEditPeerTypeBox(
		std::optional<rpl::producer<QString>> error = {});
	void showEditLinkedChatBox();
	void fillPrivacyTypeButton();
	void fillLinkedChatButton();
	//void fillInviteLinkButton();
	void fillSignaturesButton();
	void fillHistoryVisibilityButton();
	void fillManageSection();

	void submitTitle();
	void submitDescription();
	void deleteWithConfirmation();
	void deleteChannel();

	std::optional<Saving> validate() const;
	bool validateUsername(Saving &to) const;
	bool validateLinkedChat(Saving &to) const;
	bool validateTitle(Saving &to) const;
	bool validateDescription(Saving &to) const;
	bool validateHistoryVisibility(Saving &to) const;
	bool validateSignatures(Saving &to) const;

	void save();
	void saveUsername();
	void saveLinkedChat();
	void saveTitle();
	void saveDescription();
	void saveHistoryVisibility();
	void saveSignatures();
	void savePhoto();
	void pushSaveStage(FnMut<void()> &&lambda);
	void continueSave();
	void cancelSave();

	void togglePreHistoryHidden(
		not_null<ChannelData*> channel,
		bool hidden,
		Fn<void()> done,
		Fn<void()> fail);

	void subscribeToMigration();
	void migrate(not_null<ChannelData*> channel);

	std::optional<Privacy> _privacySavedValue;
	std::optional<ChannelData*> _linkedChatSavedValue;
	ChannelData *_linkedChatOriginalValue = nullptr;
	bool _channelHasLocationOriginalValue = false;
	std::optional<HistoryVisibility> _historyVisibilitySavedValue;
	std::optional<QString> _usernameSavedValue;
	std::optional<bool> _signaturesSavedValue;

	const not_null<Window::SessionNavigation*> _navigation;
	const not_null<Ui::BoxContent*> _box;
	not_null<PeerData*> _peer;
	MTP::Sender _api;
	const bool _isGroup = false;

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
, _isGroup(_peer->isChat() || _peer->isMegagroup()) {
	_box->setTitle(_isGroup
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
			&_navigation->parentController()->window(),
			_peer,
			Ui::UserpicButton::Role::ChangePhoto,
			st::defaultUserpicButton),
		st::editPeerPhotoMargins);
	_controls.photo = photoWrap->entity();

	return photoWrap;
}

object_ptr<Ui::RpWidget> Controller::createTitleEdit() {
	Expects(_wrap != nullptr);

	auto result = object_ptr<Ui::PaddingWrap<Ui::InputField>>(
		_wrap,
		object_ptr<Ui::InputField>(
			_wrap,
			st::defaultInputField,
			(_isGroup
				? tr::lng_dlg_new_group_name
				: tr::lng_dlg_new_channel_name)(),
			_peer->name),
		st::editPeerTitleMargins);
	result->entity()->setMaxLength(kMaxGroupChannelTitle);
	result->entity()->setInstantReplaces(Ui::InstantReplaces::Default());
	result->entity()->setInstantReplacesEnabled(
		Core::App().settings().replaceEmojiValue());
	Ui::Emoji::SuggestionsController::Init(
		_wrap->window(),
		result->entity(),
		&_peer->session());

	QObject::connect(
		result->entity(),
		&Ui::InputField::submitted,
		[=] { submitTitle(); });

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
	result->entity()->setMaxLength(kMaxChannelDescription);
	result->entity()->setInstantReplaces(Ui::InstantReplaces::Default());
	result->entity()->setInstantReplacesEnabled(
		Core::App().settings().replaceEmojiValue());
	result->entity()->setSubmitSettings(Core::App().settings().sendSubmitWay());
	Ui::Emoji::SuggestionsController::Init(
		_wrap->window(),
		result->entity(),
		&_peer->session());

	QObject::connect(
		result->entity(),
		&Ui::InputField::submitted,
		[=] { submitDescription(); });

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

	auto result = object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
		_wrap,
		object_ptr<Ui::VerticalLayout>(_wrap),
		st::editPeerInvitesMargins);
	const auto container = result->entity();

	container->add(object_ptr<Ui::FlatLabel>(
		container,
		tr::lng_group_stickers(),
		st::editPeerSectionLabel));
	container->add(object_ptr<Ui::FixedHeightWidget>(
		container,
		st::editPeerInviteLinkSkip));

	container->add(object_ptr<Ui::FlatLabel>(
		container,
		tr::lng_group_stickers_description(),
		st::editPeerPrivacyLabel));
	container->add(object_ptr<Ui::FixedHeightWidget>(
		container,
		st::editPeerInviteLinkSkip));

	container->add(object_ptr<Ui::LinkButton>(
		_wrap,
		tr::lng_group_stickers_add(tr::now),
		st::editPeerInviteLinkButton)
	)->addClickHandler([=] {
		_navigation->parentController()->show(
			Box<StickersBox>(_navigation->parentController(), channel),
			Ui::LayerOption::KeepOther);
	});

	return result;
}

bool Controller::canEditInformation() const {
	if (const auto channel = _peer->asChannel()) {
		return channel->canEditInformation();
	} else if (const auto chat = _peer->asChat()) {
		return chat->canEditInformation();
	}
	return false;
}

void Controller::refreshHistoryVisibility() {
	if (!_controls.historyVisibilityWrap) {
		return;
	}
	_controls.historyVisibilityWrap->toggle(
		(_privacySavedValue != Privacy::HasUsername
			&& !_channelHasLocationOriginalValue
			&& (!_linkedChatSavedValue || !*_linkedChatSavedValue)),
		anim::type::instant);
}

void Controller::showEditPeerTypeBox(
		std::optional<rpl::producer<QString>> error) {
	const auto boxCallback = crl::guard(this, [=](
			Privacy checked, QString publicLink) {
		_privacyTypeUpdates.fire(std::move(checked));
		_privacySavedValue = checked;
		_usernameSavedValue = publicLink;
		refreshHistoryVisibility();
	});
	_navigation->parentController()->show(
		Box<EditPeerTypeBox>(
			_peer,
			_channelHasLocationOriginalValue,
			boxCallback,
			_privacySavedValue,
			_usernameSavedValue,
			error),
		Ui::LayerOption::KeepOther);
}

void Controller::showEditLinkedChatBox() {
	Expects(_peer->isChannel());

	const auto box = std::make_shared<QPointer<Ui::BoxContent>>();
	const auto channel = _peer->asChannel();
	const auto callback = [=](ChannelData *result) {
		if (*box) {
			(*box)->closeBox();
		}
		*_linkedChatSavedValue = result;
		_linkedChatUpdates.fire_copy(result);
		refreshHistoryVisibility();
	};
	const auto canEdit = channel->isBroadcast()
		? channel->canEditInformation()
		: (channel->canPinMessages()
			&& (channel->amCreator() || channel->adminRights() != 0)
			&& (!channel->hiddenPreHistory()
				|| channel->canEditPreHistoryHidden()));

	if (const auto chat = *_linkedChatSavedValue) {
		*box = _navigation->parentController()->show(
			EditLinkedChatBox(
				_navigation,
				channel,
				chat,
				canEdit,
				callback),
			Ui::LayerOption::KeepOther);
		return;
	} else if (!canEdit || _linkedChatsRequestId) {
		return;
	} else if (channel->isMegagroup()) {
		// Restore original linked channel.
		callback(_linkedChatOriginalValue);
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
		*box = _navigation->parentController()->show(
			EditLinkedChatBox(
				_navigation,
				channel,
				std::move(chats),
				callback),
			Ui::LayerOption::KeepOther);
	}).fail([=](const MTP::Error &error) {
		_linkedChatsRequestId = 0;
	}).send();
}

void Controller::fillPrivacyTypeButton() {
	Expects(_controls.buttonsLayout != nullptr);

	// Create Privacy Button.
	const auto hasLocation = _peer->isChannel()
		&& _peer->asChannel()->hasLocation();
	_privacySavedValue = (_peer->isChannel()
		&& _peer->asChannel()->hasUsername())
		? Privacy::HasUsername
		: Privacy::NoUsername;

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
		[=] { showEditPeerTypeBox(); });

	_privacyTypeUpdates.fire_copy(*_privacySavedValue);
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
			return chat ? chat->name : QString();
		}) | rpl::type_erased()
		: rpl::combine(
			tr::lng_manage_discussion_group_add(),
			_linkedChatUpdates.events()
		) | rpl::map([=](const QString &add, ChannelData *chat) {
			return chat
				? chat->name
				: add;
		}) | rpl::type_erased();
	AddButtonWithText(
		_controls.buttonsLayout,
		std::move(text),
		std::move(label),
		[=] { showEditLinkedChatBox(); });
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

void Controller::fillSignaturesButton() {
	Expects(_controls.buttonsLayout != nullptr);

	const auto channel = _peer->asChannel();
	if (!channel) return;

	AddButtonWithText(
		_controls.buttonsLayout,
		tr::lng_edit_sign_messages(),
		rpl::single(QString()),
		[=] {}
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
		_navigation->parentController()->show(
			Box<EditPeerHistoryVisibilityBox>(
				_peer,
				boxCallback,
				*_historyVisibilitySavedValue),
			Ui::LayerOption::KeepOther);
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
		buttonCallback);

	updateHistoryVisibility->fire_copy(*_historyVisibilitySavedValue);

	refreshHistoryVisibility();
}

void Controller::fillManageSection() {
	Expects(_controls.buttonsLayout != nullptr);

	const auto chat = _peer->asChat();
	const auto channel = _peer->asChannel();
	const auto isChannel = (!chat);
	if (!chat && !channel) return;

	const auto canEditUsername = [&] {
		return isChannel
			? channel->canEditUsername()
			: chat->canEditUsername();
	}();
	const auto canEditSignatures = [&] {
		return isChannel
			? (channel->canEditSignatures() && !channel->isMegagroup())
			: false;
	}();
	const auto canEditPreHistoryHidden = [&] {
		return isChannel
			? channel->canEditPreHistoryHidden()
			: chat->canEditPreHistoryHidden();
	}();

	const auto canEditPermissions = [&] {
		return isChannel
			? channel->canEditPermissions()
			: chat->canEditPermissions();
	}();
	const auto canEditInviteLinks = [&] {
		return isChannel
			? channel->canHaveInviteLink()
			: chat->canHaveInviteLink();
	}();
	const auto canViewAdmins = [&] {
		return isChannel
			? channel->canViewAdmins()
			: chat->amIn();
	}();
	const auto canViewMembers = [&] {
		return isChannel
			? channel->canViewMembers()
			: chat->amIn();
	}();
	const auto canViewKicked = [&] {
		return isChannel
			? (channel->isBroadcast() || channel->isGigagroup())
			: false;
	}();
	const auto hasRecentActions = [&] {
		return isChannel
			? (channel->hasAdminRights() || channel->amCreator())
			: false;
	}();

	const auto canEditStickers = [&] {
		// return true;
		return isChannel
			? channel->canEditStickers()
			: false;
	}();
	const auto canDeleteChannel = [&] {
		return isChannel
			? channel->canDelete()
			: false;
	}();

	const auto canViewOrEditLinkedChat = [&] {
		return !isChannel
			? false
			: channel->linkedChat()
			? true
			: (channel->isBroadcast() && channel->canEditInformation());
	}();

	AddSkip(_controls.buttonsLayout, 0);

	if (canEditUsername) {
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
	if (canEditSignatures) {
		fillSignaturesButton();
	}
	if (canEditPreHistoryHidden
		|| canEditSignatures
		//|| canEditInviteLinks
		|| canViewOrEditLinkedChat
		|| canEditUsername) {
		AddSkip(
			_controls.buttonsLayout,
			st::editPeerTopButtonsLayoutSkip,
			st::editPeerTopButtonsLayoutSkipCustomBottom);
	}

	if (canEditPermissions) {
		AddButtonWithCount(
			_controls.buttonsLayout,
			tr::lng_manage_peer_permissions(),
			Info::Profile::MigratedOrMeValue(
				_peer
			) | rpl::map(
				Info::Profile::RestrictionsCountValue
			) | rpl::flatten_latest(
			) | ToPositiveNumberStringRestrictions(),
			[=] { ShowEditPermissions(_navigation, _peer); },
			st::infoIconPermissions);
	}
	if (canEditInviteLinks
		&& (canEditUsername
			|| !_peer->isChannel()
			|| !_peer->asChannel()->hasUsername())) {
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
				_navigation->parentController()->show(
					Box(
						ManageInviteLinksBox,
						_peer,
						_peer->session().user(),
						0,
						0),
					Ui::LayerOption::KeepOther);
			},
			st::infoIconInviteLinks);

		if (_privacySavedValue) {
			_privacyTypeUpdates.events_starting_with_copy(
				*_privacySavedValue
			) | rpl::start_with_next([=](Privacy flag) {
				wrap->toggle(
					flag != Privacy::HasUsername,
					anim::type::instant);
			}, wrap->lifetime());
		}
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
			st::infoIconAdministrators);
	}
	if (canViewMembers) {
		AddButtonWithCount(
			_controls.buttonsLayout,
			(_isGroup ? tr::lng_manage_peer_members() : tr::lng_manage_peer_subscribers()),
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
			st::infoIconMembers);
	}
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
			st::infoIconBlacklist);
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
			st::infoIconRecentActions);
	}

	if (canEditStickers || canDeleteChannel) {
		AddSkip(_controls.buttonsLayout,
			st::editPeerTopButtonsLayoutSkipCustomTop);
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
	if (validateUsername(result)
		&& validateLinkedChat(result)
		&& validateTitle(result)
		&& validateDescription(result)
		&& validateHistoryVisibility(result)
		&& validateSignatures(result)) {
		return result;
	}
	return {};
}

bool Controller::validateUsername(Saving &to) const {
	if (!_privacySavedValue) {
		return true;
	} else if (_privacySavedValue != Privacy::HasUsername) {
		to.username = QString();
		return true;
	}
	const auto username = _usernameSavedValue.value_or(
		_peer->isChannel()
			? _peer->asChannel()->username
			: QString()
	);
	if (username.isEmpty()) {
		return false;
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
		|| (_privacySavedValue == Privacy::HasUsername)) {
		return true;
	}
	to.hiddenPreHistory
		= (_historyVisibilitySavedValue == HistoryVisibility::Hidden);
	return true;
}

bool Controller::validateSignatures(Saving &to) const {
	if (!_signaturesSavedValue.has_value()) {
		return true;
	}
	to.signatures = _signaturesSavedValue;
	return true;
}

void Controller::save() {
	Expects(_wrap != nullptr);

	if (!_saveStagesQueue.empty()) {
		return;
	}
	if (const auto saving = validate()) {
		_savingData = *saving;
		pushSaveStage([=] { saveUsername(); });
		pushSaveStage([=] { saveLinkedChat(); });
		pushSaveStage([=] { saveTitle(); });
		pushSaveStage([=] { saveDescription(); });
		pushSaveStage([=] { saveHistoryVisibility(); });
		pushSaveStage([=] { saveSignatures(); });
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

void Controller::saveUsername() {
	const auto channel = _peer->asChannel();
	const auto username = (channel ? channel->username : QString());
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

	_api.request(MTPchannels_UpdateUsername(
		channel->inputChannel,
		MTP_string(*_savingData.username)
	)).done([=](const MTPBool &result) {
		channel->setName(
			TextUtilities::SingleLine(channel->name),
			*_savingData.username);
		continueSave();
	}).fail([=](const MTP::Error &error) {
		const auto &type = error.type();
		if (type == qstr("USERNAME_NOT_MODIFIED")) {
			channel->setName(
				TextUtilities::SingleLine(channel->name),
				TextUtilities::SingleLine(*_savingData.username));
			continueSave();
			return;
		}

		// Very rare case.
		showEditPeerTypeBox([&] {
			if (type == qstr("USERNAME_INVALID")) {
				return tr::lng_create_channel_link_invalid();
			} else if (type == qstr("USERNAME_OCCUPIED")
				|| type == qstr("USERNAMES_UNAVAILABLE")) {
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
	)).done([=](const MTPBool &result) {
		channel->setLinkedChat(*_savingData.linkedChat);
		continueSave();
	}).fail([=](const MTP::Error &error) {
		cancelSave();
	}).send();
}

void Controller::saveTitle() {
	if (!_savingData.title || *_savingData.title == _peer->name) {
		return continueSave();
	}

	const auto onDone = [=](const MTPUpdates &result) {
		_peer->session().api().applyUpdates(result);
		continueSave();
	};
	const auto onFail = [=](const MTP::Error &error) {
		const auto &type = error.type();
		if (type == qstr("CHAT_NOT_MODIFIED")
			|| type == qstr("CHAT_TITLE_NOT_MODIFIED")) {
			if (const auto channel = _peer->asChannel()) {
				channel->setName(*_savingData.title, channel->username);
			} else if (const auto chat = _peer->asChat()) {
				chat->setName(*_savingData.title);
			}
			continueSave();
			return;
		}
		_controls.title->showError();
		if (type == qstr("NO_CHAT_TITLE")) {
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
	_api.request(MTPmessages_EditChatAbout(
		_peer->input,
		MTP_string(*_savingData.description)
	)).done([=](const MTPBool &result) {
		successCallback();
	}).fail([=](const MTP::Error &error) {
		const auto &type = error.type();
		if (type == qstr("CHAT_ABOUT_NOT_MODIFIED")) {
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
		if (error.type() == qstr("CHAT_NOT_MODIFIED")) {
			apply();
		} else {
			fail();
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
		if (error.type() == qstr("CHAT_NOT_MODIFIED")) {
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
		_peer->session().api().uploadPeerPhoto(_peer, std::move(image));
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
		Box<ConfirmBox>(
			text,
			tr::lng_box_delete(tr::now),
			st::attentionBoxButton,
			deleteCallback),
		Ui::LayerOption::KeepOther);
}

void Controller::deleteChannel() {
	Expects(_peer->isChannel());

	const auto channel = _peer->asChannel();
	const auto chat = channel->migrateFrom();

	const auto session = &_peer->session();

	Ui::hideLayer();
	Ui::showChatsList(session);
	if (chat) {
		session->api().deleteConversation(chat, false);
	}
	session->api().request(MTPchannels_DeleteChannel(
		channel->inputChannel
	)).done([=](const MTPUpdates &result) {
		session->api().applyUpdates(result);
	//}).fail([=](const MTP::Error &error) {
	//	if (error.type() == qstr("CHANNEL_TOO_LARGE")) {
	//		Ui::show(Box<InformBox>(tr::lng_cant_delete_channel(tr::now)));
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
		const style::icon *icon) {
	auto result = object_ptr<Ui::SettingsButton>(
		parent,
		rpl::duplicate(text),
		st.button);
	const auto button = result.data();
	button->addClickHandler(callback);
	if (icon) {
		Ui::CreateChild<Info::Profile::FloatingIcon>(
			button,
			*icon,
			st.iconPosition);
	}

	auto labelText = rpl::combine(
		std::move(text),
		std::move(count),
		button->widthValue()
	) | rpl::map([&st](const QString &text, const QString &count, int width) {
		const auto available = width
			- st.button.padding.left()
			- (st.button.font->spacew * 2)
			- st.button.font->width(text)
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
	if (const auto chat = peer->asChat()) {
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
