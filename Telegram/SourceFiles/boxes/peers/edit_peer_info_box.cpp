/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/peers/edit_peer_info_box.h"

#include "apiwrap.h"
#include "auth_session.h"
#include "boxes/add_contact_box.h"
#include "boxes/confirm_box.h"
#include "boxes/peer_list_controllers.h"
#include "boxes/peers/edit_participants_box.h"
#include "boxes/peers/edit_peer_group_type_box.h"
#include "boxes/peers/edit_peer_history_visibility_box.h"
#include "boxes/peers/edit_peer_permissions_box.h"
#include "boxes/stickers_box.h"
#include "chat_helpers/emoji_suggestions_widget.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_peer.h"
#include "history/admin_log/history_admin_log_section.h"
#include "info/profile/info_profile_button.h"
#include "info/profile/info_profile_values.h"
#include "lang/lang_keys.h"
#include "mainwidget.h"
#include "mainwindow.h"
#include "mtproto/sender.h"
#include "observer_peer.h"
#include "styles/style_boxes.h"
#include "styles/style_info.h"
#include "ui/rp_widget.h"
#include "ui/special_buttons.h"
#include "ui/toast/toast.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/input_fields.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_controller.h"
#include <rpl/flatten_latest.h>
#include <rpl/range.h>

namespace {

Fn<QString()> ManagePeerTitle(not_null<PeerData*> peer) {
	return langFactory((peer->isChat() || peer->isMegagroup())
		? lng_manage_group_title
		: lng_manage_channel_title);
}

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

Info::Profile::Button *AddButton(
		not_null<Ui::VerticalLayout*> parent,
		rpl::producer<QString> &&text,
		Fn<void()> callback,
		const style::icon &icon) {
	return ManagePeerBox::CreateButton(
		parent,
		std::move(text),
		rpl::single(QString()),
		std::move(callback),
		st::manageGroupButton,
		&icon);
}

void AddButtonWithCount(
		not_null<Ui::VerticalLayout*> parent,
		rpl::producer<QString> &&text,
		rpl::producer<QString> &&count,
		Fn<void()> callback,
		const style::icon &icon) {
	ManagePeerBox::CreateButton(
		parent,
		std::move(text),
		std::move(count),
		std::move(callback),
		st::manageGroupButton,
		&icon);
}

Info::Profile::Button *AddButtonWithText(
		not_null<Ui::VerticalLayout*> parent,
		rpl::producer<QString> &&text,
		rpl::producer<QString> &&label,
		Fn<void()> callback) {
	return ManagePeerBox::CreateButton(
		parent,
		std::move(text),
		std::move(label),
		std::move(callback),
		st::manageGroupTopButtonWithText,
		nullptr);
}

bool HasRecentActions(not_null<ChannelData*> channel) {
	return channel->hasAdminRights() || channel->amCreator();
}

void ShowRecentActions(
		not_null<Window::Navigation*> navigation,
		not_null<ChannelData*> channel) {
	navigation->showSection(AdminLog::SectionMemento(channel));
}

bool HasEditInfoBox(not_null<PeerData*> peer) {
	if (const auto chat = peer->asChat()) {
		if (chat->canEditInformation()) {
			return true;
		}
	} else if (const auto channel = peer->asChannel()) {
		if (channel->canEditInformation()) {
			return true;
		} else if (!channel->isPublic() && channel->canAddMembers()) {
			// Edit invite link.
			return true;
		}
	}
	return false;
}

void ShowEditPermissions(not_null<PeerData*> peer) {
	const auto box = Ui::show(
		Box<EditPeerPermissionsBox>(peer),
		LayerOption::KeepOther);
	box->saveEvents(
	) | rpl::start_with_next([=](MTPDchatBannedRights::Flags restrictions) {
		const auto callback = crl::guard(box, [=](bool success) {
			if (success) {
				box->closeBox();
			}
		});
		peer->session().api().saveDefaultRestrictions(
			peer->migrateToOrMe(),
			MTP_chatBannedRights(MTP_flags(restrictions), MTP_int(0)),
			callback);
	}, box->lifetime());
}

void FillManageChatBox(
		not_null<Window::Navigation*> navigation,
		not_null<ChatData*> chat,
		not_null<Ui::VerticalLayout*> content) {

	if (chat->canEditPermissions()) {
		AddButtonWithCount(
			content,
			Lang::Viewer(lng_manage_peer_permissions),
			Info::Profile::RestrictionsCountValue(chat)
				| ToPositiveNumberStringRestrictions(),
			[=] { ShowEditPermissions(chat); },
			st::infoIconPermissions);
	}
	if (chat->amIn()) {
		AddButtonWithCount(
			content,
			Lang::Viewer(lng_manage_peer_administrators),
			Info::Profile::AdminsCountValue(chat)
				| ToPositiveNumberString(),
			[=] {
				ParticipantsBoxController::Start(
					navigation,
					chat,
					ParticipantsBoxController::Role::Admins);
			},
			st::infoIconAdministrators);
		AddButtonWithCount(
			content,
			Lang::Viewer(lng_manage_peer_members),
			Info::Profile::MembersCountValue(chat)
				| ToPositiveNumberString(),
			[=] {
				ParticipantsBoxController::Start(
					navigation,
					chat,
					ParticipantsBoxController::Role::Members);
			},
			st::infoIconMembers);
	}
}

void FillManageChannelBox(
		not_null<Window::Navigation*> navigation,
		not_null<ChannelData*> channel,
		not_null<Ui::VerticalLayout*> content) {
	auto isGroup = channel->isMegagroup();
	if (channel->canEditPermissions()) {
		AddButtonWithCount(
			content,
			Lang::Viewer(lng_manage_peer_permissions),
			Info::Profile::RestrictionsCountValue(channel)
				| ToPositiveNumberStringRestrictions(),
			[=] { ShowEditPermissions(channel); },
			st::infoIconPermissions);
	}
	if (channel->canViewAdmins()) {
		AddButtonWithCount(
			content,
			Lang::Viewer(lng_manage_peer_administrators),
			Info::Profile::AdminsCountValue(channel)
				| ToPositiveNumberString(),
			[=] {
				ParticipantsBoxController::Start(
					navigation,
					channel,
					ParticipantsBoxController::Role::Admins);
			},
			st::infoIconAdministrators);
	}
	if (channel->canViewMembers()) {
		AddButtonWithCount(
			content,
			Lang::Viewer(lng_manage_peer_members),
			Info::Profile::MembersCountValue(channel)
				| ToPositiveNumberString(),
			[=] {
				ParticipantsBoxController::Start(
					navigation,
					channel,
					ParticipantsBoxController::Role::Members);
			},
			st::infoIconMembers);
	}
	if (!channel->isMegagroup()) {
		AddButtonWithCount(
			content,
			Lang::Viewer(lng_manage_peer_removed_users),
			Info::Profile::KickedCountValue(channel)
			| ToPositiveNumberString(),
			[=] {
				ParticipantsBoxController::Start(
					navigation,
					channel,
					ParticipantsBoxController::Role::Kicked);
			},
			st::infoIconBlacklist);
	}
	if (HasRecentActions(channel)) {
		AddButton(
			content,
			Lang::Viewer(lng_manage_peer_recent_actions),
			[=] { ShowRecentActions(navigation, channel); },
			st::infoIconRecentActions);
	}
}

} // namespace

namespace {

constexpr auto kUsernameCheckTimeout = crl::time(200);
constexpr auto kMinUsernameLength = 5;
constexpr auto kMaxGroupChannelTitle = 255; // See also add_contact_box.
constexpr auto kMaxChannelDescription = 255; // See also add_contact_box.

class Controller
	: public base::has_weak_ptr
	, private MTP::Sender {
public:
	Controller(
		not_null<BoxContent*> box,
		not_null<PeerData*> peer);

	object_ptr<Ui::VerticalLayout> createContent();
	void setFocus();

private:
	struct Controls {
		Ui::InputField *title = nullptr;
		Ui::InputField *description = nullptr;
		Ui::UserpicButton *photo = nullptr;
		rpl::lifetime initialPhotoImageWaiting;

		std::shared_ptr<Ui::RadioenumGroup<Privacy>> privacy;
		Ui::SlideWrap<Ui::RpWidget> *usernameWrap = nullptr;
		Ui::UsernameInput *username = nullptr;
		base::unique_qptr<Ui::FlatLabel> usernameResult;
		const style::FlatLabel *usernameResultStyle = nullptr;

		Ui::SlideWrap<Ui::RpWidget> *createInviteLinkWrap = nullptr;
		Ui::SlideWrap<Ui::RpWidget> *editInviteLinkWrap = nullptr;
		Ui::FlatLabel *inviteLink = nullptr;

		Ui::SlideWrap<Ui::RpWidget> *historyVisibilityWrap = nullptr;
		std::optional<HistoryVisibility> historyVisibilitySavedValue = std::nullopt;
		std::optional<Privacy> privacySavedValue = std::nullopt;
		std::optional<QString> usernameSavedValue = std::nullopt;

		std::optional<bool> signaturesSavedValue = std::nullopt;
	};
	struct Saving {
		std::optional<QString> username;
		std::optional<QString> title;
		std::optional<QString> description;
		std::optional<bool> hiddenPreHistory;
		std::optional<bool> signatures;
	};

	Fn<QString()> computeTitle() const;
	object_ptr<Ui::RpWidget> createPhotoAndTitleEdit();
	object_ptr<Ui::RpWidget> createTitleEdit();
	object_ptr<Ui::RpWidget> createPhotoEdit();
	object_ptr<Ui::RpWidget> createDescriptionEdit();
	object_ptr<Ui::RpWidget> createPrivaciesEdit();
	object_ptr<Ui::RpWidget> createUsernameEdit();
	object_ptr<Ui::RpWidget> createInviteLinkCreate();
	object_ptr<Ui::RpWidget> createInviteLinkEdit();
	object_ptr<Ui::RpWidget> createStickersEdit();
	object_ptr<Ui::RpWidget> createDeleteButton();

	object_ptr<Ui::RpWidget> createPrivaciesButtons();
	object_ptr<Ui::RpWidget> createManageGroupButtons();

	QString inviteLinkText() const;
	void observeInviteLink();

	void submitTitle();
	void submitDescription();
	void deleteWithConfirmation();
	void deleteChannel();
	void privacyChanged(Privacy value);

	void checkUsernameAvailability();
	void askUsernameRevoke();
	void usernameChanged();
	void showUsernameError(rpl::producer<QString> &&error);
	void showUsernameGood();
	void showUsernameResult(
		rpl::producer<QString> &&text,
		not_null<const style::FlatLabel*> st);

	bool canEditInviteLink() const;
	bool inviteLinkShown() const;
	void refreshEditInviteLink();
	void refreshCreateInviteLink();
	void refreshHistoryVisibility();
	void createInviteLink();
	void revokeInviteLink();
	void exportInviteLink(const QString &confirmation);

	std::optional<Saving> validate() const;
	bool validateUsername(Saving &to) const;
	bool validateTitle(Saving &to) const;
	bool validateDescription(Saving &to) const;
	bool validateHistoryVisibility(Saving &to) const;
	bool validateSignatures(Saving &to) const;

	void save();
	void saveUsername();
	void saveTitle();
	void saveDescription();
	void saveHistoryVisibility();
	void saveSignatures();
	void savePhoto();
	void pushSaveStage(FnMut<void()> &&lambda);
	void continueSave();
	void cancelSave();

	void subscribeToMigration();
	void migrate(not_null<ChannelData*> channel);

	not_null<BoxContent*> _box;
	not_null<PeerData*> _peer;
	bool _isGroup = false;

	base::unique_qptr<Ui::VerticalLayout> _wrap;
	Controls _controls;
	base::Timer _checkUsernameTimer;
	mtpRequestId _checkUsernameRequestId = 0;
	UsernameState _usernameState = UsernameState::Normal;
	rpl::event_stream<rpl::producer<QString>> _usernameResultTexts;

	std::deque<FnMut<void()>> _saveStagesQueue;
	Saving _savingData;

	rpl::lifetime _lifetime;

};

Controller::Controller(
	not_null<BoxContent*> box,
	not_null<PeerData*> peer)
: _box(box)
, _peer(peer)
, _isGroup(_peer->isChat() || _peer->isMegagroup())
, _checkUsernameTimer([=] { checkUsernameAvailability(); }) {
	_box->setTitle(computeTitle());
	_box->addButton(langFactory(lng_settings_save), [this] {
		save();
	});
	_box->addButton(langFactory(lng_cancel), [this] {
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
	observeInviteLink();
	_peer->updateFull();
}

Fn<QString()> Controller::computeTitle() const {
	return langFactory(_isGroup
			? lng_edit_group
			: lng_edit_channel_title);
}

object_ptr<Ui::VerticalLayout> Controller::createContent() {
	auto result = object_ptr<Ui::VerticalLayout>(_box);
	_wrap.reset(result.data());
	_controls = Controls();

	const auto addSkip = [](not_null<Ui::VerticalLayout*> container) {
		container->add(object_ptr<Ui::FixedHeightWidget>(
			container,
			7 /*Create skip in style.*/));
		container->add(object_ptr<BoxContentDivider>(container));
		/*container->add(object_ptr<Ui::FixedHeightWidget>(
			container,
			st::editPeerPrivacyTopSkip));*/
	};

	_wrap->add(createPhotoAndTitleEdit());
	_wrap->add(createDescriptionEdit());

	addSkip(_wrap); // Divider.
	_wrap->add(createPrivaciesButtons());
	addSkip(_wrap); // Divider.
	_wrap->add(createManageGroupButtons());
	addSkip(_wrap); // Divider.

	_wrap->add(createPrivaciesEdit());
	_wrap->add(createInviteLinkCreate());
	_wrap->add(createInviteLinkEdit());
	_wrap->add(createStickersEdit());
	_wrap->add(createDeleteButton());

	return result;
}

void Controller::setFocus() {
	if (_controls.title) {
		_controls.title->setFocusFast();
	}
}

object_ptr<Ui::RpWidget> Controller::createPhotoAndTitleEdit() {
	Expects(_wrap != nullptr);

	const auto canEdit = [&] {
		if (const auto channel = _peer->asChannel()) {
			return channel->canEditInformation();
		} else if (const auto chat = _peer->asChat()) {
			return chat->canEditInformation();
		}
		return false;
	}();
	if (!canEdit) {
		return nullptr;
	}

	auto result = object_ptr<Ui::RpWidget>(_wrap);
	auto container = result.data();

	auto photoWrap = Ui::AttachParentChild(
		container,
		createPhotoEdit());
	auto titleEdit = Ui::AttachParentChild(
		container,
		createTitleEdit());
	photoWrap->heightValue(
	) | rpl::start_with_next([container](int height) {
		container->resize(container->width(), height);
	}, photoWrap->lifetime());
	container->widthValue(
	) | rpl::start_with_next([titleEdit](int width) {
		auto left = st::editPeerPhotoMargins.left()
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
			langFactory(_isGroup
				? lng_dlg_new_group_name
				: lng_dlg_new_channel_name),
			_peer->name),
		st::editPeerTitleMargins);
	result->entity()->setMaxLength(kMaxGroupChannelTitle);
	result->entity()->setInstantReplaces(Ui::InstantReplaces::Default());
	result->entity()->setInstantReplacesEnabled(
		Global::ReplaceEmojiValue());
	Ui::Emoji::SuggestionsController::Init(
		_wrap->window(),
		result->entity());

	QObject::connect(
		result->entity(),
		&Ui::InputField::submitted,
		[=] { submitTitle(); });

	_controls.title = result->entity();
	return std::move(result);
}

object_ptr<Ui::RpWidget> Controller::createDescriptionEdit() {
	Expects(_wrap != nullptr);

	auto result = object_ptr<Ui::PaddingWrap<Ui::InputField>>(
		_wrap,
		object_ptr<Ui::InputField>(
			_wrap,
			st::editPeerDescription,
			Ui::InputField::Mode::MultiLine,
			langFactory(lng_create_group_description),
			_peer->about()),
		st::editPeerDescriptionMargins);
	result->entity()->setMaxLength(kMaxChannelDescription);
	result->entity()->setInstantReplaces(Ui::InstantReplaces::Default());
	result->entity()->setInstantReplacesEnabled(
		Global::ReplaceEmojiValue());
	Ui::Emoji::SuggestionsController::Init(
		_wrap->window(),
		result->entity());

	QObject::connect(
		result->entity(),
		&Ui::InputField::submitted,
		[=] { submitDescription(); });

	_controls.description = result->entity();
	return std::move(result);
}

object_ptr<Ui::RpWidget> Controller::createPrivaciesEdit() {
	Expects(_wrap != nullptr);

	const auto canEditUsername = [&] {
		if (const auto chat = _peer->asChat()) {
			return chat->canEditUsername();
		} else if (const auto channel = _peer->asChannel()) {
			return channel->canEditUsername();
		}
		Unexpected("Peer type in Controller::createPrivaciesEdit.");
	}();
	if (!canEditUsername) {
		return nullptr;
	}
	auto result = object_ptr<Ui::PaddingWrap<Ui::VerticalLayout>>(
		_wrap,
		object_ptr<Ui::VerticalLayout>(_wrap),
		st::editPeerPrivaciesMargins);
	auto container = result->entity();

	const auto isPublic = _peer->isChannel()
		&& _peer->asChannel()->isPublic();
	_controls.privacy = std::make_shared<Ui::RadioenumGroup<Privacy>>(
		isPublic ? Privacy::Public : Privacy::Private);
	auto addButton = [&](
			Privacy value,
			LangKey groupTextKey,
			LangKey channelTextKey,
			LangKey groupAboutKey,
			LangKey channelAboutKey) {
		container->add(object_ptr<Ui::FixedHeightWidget>(
			container,
			st::editPeerPrivacyTopSkip));
		container->add(object_ptr<Ui::Radioenum<Privacy>>(
			container,
			_controls.privacy,
			value,
			lang(_isGroup ? groupTextKey : channelTextKey),
			st::defaultBoxCheckbox));
		container->add(object_ptr<Ui::PaddingWrap<Ui::FlatLabel>>(
			container,
			object_ptr<Ui::FlatLabel>(
				container,
				Lang::Viewer(_isGroup ? groupAboutKey : channelAboutKey),
				st::editPeerPrivacyLabel),
			st::editPeerHistoryVisibilityLabelMargins));
		container->add(object_ptr<Ui::FixedHeightWidget>(
			container,
			st::editPeerPrivacyBottomSkip));
	};
	addButton(
		Privacy::Public,
		lng_create_public_group_title,
		lng_create_public_channel_title,
		lng_create_public_group_about,
		lng_create_public_channel_about);
	addButton(
		Privacy::Private,
		lng_create_private_group_title,
		lng_create_private_channel_title,
		lng_create_private_group_about,
		lng_create_private_channel_about);
	container->add(createUsernameEdit());

	_controls.privacy->setChangedCallback([this](Privacy value) {
		privacyChanged(value);
	});
	if (!isPublic) {
		checkUsernameAvailability();
	}

	return std::move(result);
}


object_ptr<Ui::RpWidget> Controller::createPrivaciesButtons() {
	Expects(_wrap != nullptr);

	const auto canEditUsername = [&] {
		if (const auto chat = _peer->asChat()) {
			return chat->canEditUsername();
		} else if (const auto channel = _peer->asChannel()) {
			return channel->canEditUsername();
		}
		Unexpected("Peer type in Controller::createPrivaciesEdit.");
	}();
	if (!canEditUsername) {
		return nullptr;
	}

	// Bug with defaultValue here.
	const auto channel = _peer->asChannel();
	auto defaultValue = (!channel || channel->hiddenPreHistory())
		? HistoryVisibility::Hidden
		: HistoryVisibility::Visible;

	auto defaultValuePrivacy = (_peer->isChannel()
		&& _peer->asChannel()->isPublic())
		? Privacy::Public
		: Privacy::Private;

	const auto updateHistoryVisibility = std::make_shared<rpl::event_stream<HistoryVisibility>>();
	const auto updateType = std::make_shared<rpl::event_stream<Privacy>>();

	auto result = object_ptr<Ui::PaddingWrap<Ui::VerticalLayout>>(
		_wrap,
		object_ptr<Ui::VerticalLayout>(_wrap),
		st::editPeerTopButtonsLayoutMargins);
	auto resultContainer = result->entity();

	const auto boxCallback = [=](Privacy checked, QString publicLink) {
		updateType->fire(std::move(checked));
		_controls.privacySavedValue = checked;
		_controls.usernameSavedValue = publicLink;
		refreshHistoryVisibility();
	};
	const auto buttonCallback = [=]{ 
		Ui::show(Box<EditPeerGroupTypeBox>(
			_peer,
			boxCallback,
			_controls.privacySavedValue,
			_controls.usernameSavedValue
		), LayerOption::KeepOther);
	};
	AddButtonWithText(
		resultContainer,
		std::move(Lang::Viewer((_peer->isChat() || _peer->isMegagroup())
			? lng_manage_peer_group_type
			: lng_manage_peer_channel_type)),

		updateType->events(
		) | rpl::map([](Privacy flag) {
			return lang(Privacy::Public == flag
						? lng_manage_public_peer_title
						: lng_manage_private_peer_title);
		}),
		buttonCallback);
	
	const auto addHistoryVisibilityButton = [=](LangKey privacyTextKey, Ui::VerticalLayout* container) {
		const auto boxCallback = [=](HistoryVisibility checked) {
			updateHistoryVisibility->fire(std::move(checked));
			_controls.historyVisibilitySavedValue = checked;
		};
		const auto buttonCallback = [=]{ 
			Ui::show(Box<EditPeerHistoryVisibilityBox>(
				_peer,
				boxCallback,
				_controls.historyVisibilitySavedValue
			), LayerOption::KeepOther);
		};
		AddButtonWithText(
			container,
			std::move(Lang::Viewer(privacyTextKey)),
			updateHistoryVisibility->events(
			) | rpl::map([](HistoryVisibility flag) {
				return lang(HistoryVisibility::Visible == flag
						? lng_manage_history_visibility_shown
						: lng_manage_history_visibility_hidden);
			}),
			buttonCallback);
	};

	auto wrapLayout = resultContainer->add(object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
		resultContainer,
		object_ptr<Ui::VerticalLayout>(resultContainer),
		st::boxOptionListPadding)); // Empty margins.
	_controls.historyVisibilityWrap = wrapLayout;

	addHistoryVisibilityButton(lng_manage_history_visibility_title, wrapLayout->entity());
	
	updateHistoryVisibility->fire(std::move(defaultValue));
	updateType->fire(std::move(defaultValuePrivacy));
	refreshHistoryVisibility();

	// Draw Signatures toggle button.
	if (!channel
		|| !channel->canEditSignatures()
		|| channel->isMegagroup()) {
		return std::move(result);
	}
	AddButtonWithText(
		resultContainer,
		std::move(Lang::Viewer(lng_edit_sign_messages)),
		rpl::single(QString()),
		[=] {}
	)->toggleOn(rpl::single(channel->addsSignature())
	)->toggledValue(
	) | rpl::start_with_next([=](bool toggled) {
		_controls.signaturesSavedValue = toggled;
	}, resultContainer->lifetime());

	return std::move(result);
}

object_ptr<Ui::RpWidget> Controller::createManageGroupButtons() {
	Expects(_wrap != nullptr);

	auto result = object_ptr<Ui::PaddingWrap<Ui::VerticalLayout>>(
		_wrap,
		object_ptr<Ui::VerticalLayout>(_wrap),
		st::editPeerBottomButtonsLayoutMargins);
	auto container = result->entity();

	if (const auto chat = _peer->asChat()) {
		FillManageChatBox(App::wnd()->controller(), chat, container);
	} else if (const auto channel = _peer->asChannel()) {
		FillManageChannelBox(App::wnd()->controller(), channel, container);
	}
	// setDimensionsToContent(st::boxWidth, content);

	return std::move(result);
}

object_ptr<Ui::RpWidget> Controller::createUsernameEdit() {
	Expects(_wrap != nullptr);

	const auto channel = _peer->asChannel();
	const auto username = channel ? channel->username : QString();

	auto result = object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
		_wrap,
		object_ptr<Ui::VerticalLayout>(_wrap),
		st::editPeerUsernameMargins);
	_controls.usernameWrap = result.data();

	auto container = result->entity();
	container->add(object_ptr<Ui::FlatLabel>(
		container,
		Lang::Viewer(lng_create_group_link),
		st::editPeerSectionLabel));
	auto placeholder = container->add(object_ptr<Ui::RpWidget>(
		container));
	placeholder->setAttribute(Qt::WA_TransparentForMouseEvents);
	_controls.username = Ui::AttachParentChild(
		container,
		object_ptr<Ui::UsernameInput>(
			container,
			st::setupChannelLink,
			Fn<QString()>(),
			username,
			true));
	_controls.username->heightValue(
	) | rpl::start_with_next([placeholder](int height) {
		placeholder->resize(placeholder->width(), height);
	}, placeholder->lifetime());
	placeholder->widthValue(
	) | rpl::start_with_next([this](int width) {
		_controls.username->resize(
			width,
			_controls.username->height());
	}, placeholder->lifetime());
	_controls.username->move(placeholder->pos());

	QObject::connect(
		_controls.username,
		&Ui::UsernameInput::changed,
		[this] { usernameChanged(); });

	auto shown = (_controls.privacy->value() == Privacy::Public);
	result->toggle(shown, anim::type::instant);

	return std::move(result);
}

void Controller::privacyChanged(Privacy value) {
	auto toggleEditUsername = [&] {
		_controls.usernameWrap->toggle(
			(value == Privacy::Public),
			anim::type::instant);
	};
	auto refreshVisibilities = [&] {
		// First we need to show everything, then hide anything.
		// Otherwise the scroll position could jump up undesirably.

		if (value == Privacy::Public) {
			toggleEditUsername();
		}
		refreshCreateInviteLink();
		refreshEditInviteLink();
		refreshHistoryVisibility();
		if (value == Privacy::Public) {
			_controls.usernameResult = nullptr;
			checkUsernameAvailability();
		} else {
			toggleEditUsername();
		}
	};
	if (value == Privacy::Public) {
		if (_usernameState == UsernameState::TooMany) {
			askUsernameRevoke();
			return;
		} else if (_usernameState == UsernameState::NotAvailable) {
			_controls.privacy->setValue(Privacy::Private);
			return;
		}
		refreshVisibilities();
		_controls.username->setDisplayFocused(true);
		_controls.username->setFocus();
		_box->scrollToWidget(_controls.username);
	} else {
		request(base::take(_checkUsernameRequestId)).cancel();
		_checkUsernameTimer.cancel();
		refreshVisibilities();
		setFocus();
	}
}

void Controller::checkUsernameAvailability() {
	if (!_controls.username) {
		return;
	}
	auto initial = (_controls.privacy->value() != Privacy::Public);
	auto checking = initial
		? qsl(".bad.")
		: _controls.username->getLastText().trimmed();
	if (checking.size() < kMinUsernameLength) {
		return;
	}
	if (_checkUsernameRequestId) {
		request(_checkUsernameRequestId).cancel();
	}
	const auto channel = _peer->migrateToOrMe()->asChannel();
	const auto username = channel ? channel->username : QString();
	_checkUsernameRequestId = request(MTPchannels_CheckUsername(
		channel ? channel->inputChannel : MTP_inputChannelEmpty(),
		MTP_string(checking)
	)).done([=](const MTPBool &result) {
		_checkUsernameRequestId = 0;
		if (initial) {
			return;
		}
		if (!mtpIsTrue(result) && checking != username) {
			showUsernameError(
				Lang::Viewer(lng_create_channel_link_occupied));
		} else {
			showUsernameGood();
		}
	}).fail([=](const RPCError &error) {
		_checkUsernameRequestId = 0;
		const auto &type = error.type();
		_usernameState = UsernameState::Normal;
		if (type == qstr("CHANNEL_PUBLIC_GROUP_NA")) {
			_usernameState = UsernameState::NotAvailable;
			_controls.privacy->setValue(Privacy::Private);
		} else if (type == qstr("CHANNELS_ADMIN_PUBLIC_TOO_MUCH")) {
			_usernameState = UsernameState::TooMany;
			if (_controls.privacy->value() == Privacy::Public) {
				askUsernameRevoke();
			}
		} else if (initial) {
			if (_controls.privacy->value() == Privacy::Public) {
				_controls.usernameResult = nullptr;
				_controls.username->setFocus();
				_box->scrollToWidget(_controls.username);
			}
		} else if (type == qstr("USERNAME_INVALID")) {
			showUsernameError(
				Lang::Viewer(lng_create_channel_link_invalid));
		} else if (type == qstr("USERNAME_OCCUPIED")
			&& checking != username) {
			showUsernameError(
				Lang::Viewer(lng_create_channel_link_occupied));
		}
	}).send();
}

void Controller::askUsernameRevoke() {
	_controls.privacy->setValue(Privacy::Private);
	auto revokeCallback = crl::guard(this, [this] {
		_usernameState = UsernameState::Normal;
		_controls.privacy->setValue(Privacy::Public);
		checkUsernameAvailability();
	});
	Ui::show(
		Box<RevokePublicLinkBox>(std::move(revokeCallback)),
		LayerOption::KeepOther);
}

void Controller::usernameChanged() {
	auto username = _controls.username->getLastText().trimmed();
	if (username.isEmpty()) {
		_controls.usernameResult = nullptr;
		_checkUsernameTimer.cancel();
		return;
	}
	auto bad = ranges::find_if(username, [](QChar ch) {
		return (ch < 'A' || ch > 'Z')
			&& (ch < 'a' || ch > 'z')
			&& (ch < '0' || ch > '9')
			&& (ch != '_');
	}) != username.end();
	if (bad) {
		showUsernameError(
			Lang::Viewer(lng_create_channel_link_bad_symbols));
	} else if (username.size() < kMinUsernameLength) {
		showUsernameError(
			Lang::Viewer(lng_create_channel_link_too_short));
	} else {
		_controls.usernameResult = nullptr;
		_checkUsernameTimer.callOnce(kUsernameCheckTimeout);
	}
}

void Controller::showUsernameError(rpl::producer<QString> &&error) {
	showUsernameResult(std::move(error), &st::editPeerUsernameError);
}

void Controller::showUsernameGood() {
	showUsernameResult(
		Lang::Viewer(lng_create_channel_link_available),
		&st::editPeerUsernameGood);
}

void Controller::showUsernameResult(
		rpl::producer<QString> &&text,
		not_null<const style::FlatLabel*> st) {
	if (!_controls.usernameResult
		|| _controls.usernameResultStyle != st) {
		_controls.usernameResultStyle = st;
		_controls.usernameResult = base::make_unique_q<Ui::FlatLabel>(
			_controls.usernameWrap,
			_usernameResultTexts.events() | rpl::flatten_latest(),
			*st);
		auto label = _controls.usernameResult.get();
		label->show();
		label->widthValue(
		) | rpl::start_with_next([label] {
			label->moveToRight(
				st::editPeerUsernamePosition.x(),
				st::editPeerUsernamePosition.y());
		}, label->lifetime());
	}
	_usernameResultTexts.fire(std::move(text));
}

void Controller::createInviteLink() {
	exportInviteLink(lang(_isGroup
		? lng_group_invite_about
		: lng_group_invite_about_channel));
}

void Controller::revokeInviteLink() {
	exportInviteLink(lang(lng_group_invite_about_new));
}

void Controller::exportInviteLink(const QString &confirmation) {
	auto boxPointer = std::make_shared<QPointer<ConfirmBox>>();
	auto callback = crl::guard(this, [=] {
		if (const auto strong = *boxPointer) {
			strong->closeBox();
		}
		_peer->session().api().exportInviteLink(_peer->migrateToOrMe());
	});
	auto box = Box<ConfirmBox>(
		confirmation,
		std::move(callback));
	*boxPointer = Ui::show(std::move(box), LayerOption::KeepOther);
}

bool Controller::canEditInviteLink() const {
	if (const auto channel = _peer->asChannel()) {
		return channel->amCreator()
			|| (channel->adminRights() & ChatAdminRight::f_invite_users);
	} else if (const auto chat = _peer->asChat()) {
		return chat->amCreator()
			|| (chat->adminRights() & ChatAdminRight::f_invite_users);
	}
	return false;
}

bool Controller::inviteLinkShown() const {
	return !_controls.privacy
		|| (_controls.privacy->value() == Privacy::Private);
}

QString Controller::inviteLinkText() const {
	if (const auto channel = _peer->asChannel()) {
		return channel->inviteLink();
	} else if (const auto chat = _peer->asChat()) {
		return chat->inviteLink();
	}
	return QString();
}

void Controller::observeInviteLink() {
	if (!_controls.editInviteLinkWrap) {
		return;
	}
	Notify::PeerUpdateValue(
		_peer,
		Notify::PeerUpdate::Flag::InviteLinkChanged
	) | rpl::start_with_next([=] {
		refreshCreateInviteLink();
		refreshEditInviteLink();
	}, _controls.editInviteLinkWrap->lifetime());
}

object_ptr<Ui::RpWidget> Controller::createInviteLinkEdit() {
	Expects(_wrap != nullptr);

	if (!canEditInviteLink()) {
		return nullptr;
	}

	auto result = object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
		_wrap,
		object_ptr<Ui::VerticalLayout>(_wrap),
		st::editPeerInviteLinkMargins);
	_controls.editInviteLinkWrap = result.data();

	auto container = result->entity();
	container->add(object_ptr<Ui::FlatLabel>(
		container,
		Lang::Viewer(lng_profile_invite_link_section),
		st::editPeerSectionLabel));
	container->add(object_ptr<Ui::FixedHeightWidget>(
		container,
		st::editPeerInviteLinkSkip));

	_controls.inviteLink = container->add(object_ptr<Ui::FlatLabel>(
		container,
		st::editPeerInviteLink));
	_controls.inviteLink->setSelectable(true);
	_controls.inviteLink->setContextCopyText(QString());
	_controls.inviteLink->setBreakEverywhere(true);
	_controls.inviteLink->setClickHandlerFilter([=](auto&&...) {
		QApplication::clipboard()->setText(inviteLinkText());
		Ui::Toast::Show(lang(lng_group_invite_copied));
		return false;
	});

	container->add(object_ptr<Ui::FixedHeightWidget>(
		container,
		st::editPeerInviteLinkSkip));
	container->add(object_ptr<Ui::LinkButton>(
		container,
		lang(lng_group_invite_create_new),
		st::editPeerInviteLinkButton)
	)->addClickHandler([=] { revokeInviteLink(); });

	observeInviteLink();

	return std::move(result);
}

void Controller::refreshEditInviteLink() {
	auto link = inviteLinkText();
	auto text = TextWithEntities();
	if (!link.isEmpty()) {
		text.text = link;
		auto remove = qstr("https://");
		if (text.text.startsWith(remove)) {
			text.text.remove(0, remove.size());
		}
		text.entities.push_back(EntityInText(
			EntityInTextCustomUrl,
			0,
			text.text.size(),
			link));
	}
	_controls.inviteLink->setMarkedText(text);

	// Hack to expand FlatLabel width to naturalWidth again.
	_controls.editInviteLinkWrap->resizeToWidth(st::boxWideWidth);

	_controls.editInviteLinkWrap->toggle(
		inviteLinkShown() && !link.isEmpty(),
		anim::type::instant);
}

object_ptr<Ui::RpWidget> Controller::createInviteLinkCreate() {
	Expects(_wrap != nullptr);

	if (!canEditInviteLink()) {
		return nullptr;
	}

	auto result = object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
		_wrap,
		object_ptr<Ui::VerticalLayout>(_wrap),
		st::editPeerInviteLinkMargins);
	auto container = result->entity();

	container->add(object_ptr<Ui::FlatLabel>(
		container,
		Lang::Viewer(lng_profile_invite_link_section),
		st::editPeerSectionLabel));
	container->add(object_ptr<Ui::FixedHeightWidget>(
		container,
		st::editPeerInviteLinkSkip));

	container->add(object_ptr<Ui::LinkButton>(
		_wrap,
		lang(lng_group_invite_create),
		st::editPeerInviteLinkButton)
	)->addClickHandler([this] {
		createInviteLink();
	});
	_controls.createInviteLinkWrap = result.data();

	observeInviteLink();

	return std::move(result);
}

void Controller::refreshCreateInviteLink() {
	_controls.createInviteLinkWrap->toggle(
		inviteLinkShown() && inviteLinkText().isEmpty(),
		anim::type::instant);
}

void Controller::refreshHistoryVisibility() {
	if (!_controls.historyVisibilityWrap) {
		return;
	}
	auto historyVisibilityShown = !_controls.privacy
		|| (_controls.privacy->value() == Privacy::Private)
		|| (_controls.privacySavedValue == Privacy::Private);
	_controls.historyVisibilityWrap->toggle(
		historyVisibilityShown,
		anim::type::normal);
}

object_ptr<Ui::RpWidget> Controller::createStickersEdit() {
	Expects(_wrap != nullptr);

	auto channel = _peer->asChannel();
	if (!channel || !channel->canEditStickers()) {
		return nullptr;
	}

	auto result = object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
		_wrap,
		object_ptr<Ui::VerticalLayout>(_wrap),
		st::editPeerInviteLinkMargins);
	auto container = result->entity();

	container->add(object_ptr<Ui::FlatLabel>(
		container,
		Lang::Viewer(lng_group_stickers),
		st::editPeerSectionLabel));
	container->add(object_ptr<Ui::FixedHeightWidget>(
		container,
		st::editPeerInviteLinkSkip));

	container->add(object_ptr<Ui::FlatLabel>(
		container,
		Lang::Viewer(lng_group_stickers_description),
		st::editPeerPrivacyLabel));
	container->add(object_ptr<Ui::FixedHeightWidget>(
		container,
		st::editPeerInviteLinkSkip));

	container->add(object_ptr<Ui::LinkButton>(
		_wrap,
		lang(lng_group_stickers_add),
		st::editPeerInviteLinkButton)
	)->addClickHandler([=] {
		Ui::show(Box<StickersBox>(channel), LayerOption::KeepOther);
	});

	return std::move(result);
}

object_ptr<Ui::RpWidget> Controller::createDeleteButton() {
	Expects(_wrap != nullptr);

	auto channel = _peer->asChannel();
	if (!channel || !channel->canDelete()) {
		return nullptr;
	}
	auto text = lang(_isGroup
		? lng_profile_delete_group
		: lng_profile_delete_channel);
	auto result = object_ptr<Ui::PaddingWrap<Ui::LinkButton>>(
		_wrap,
		object_ptr<Ui::LinkButton>(
			_wrap,
			text,
			st::editPeerDeleteButton),
		st::editPeerDeleteButtonMargins);
	result->entity()->addClickHandler([this] {
		deleteWithConfirmation();
	});
	return std::move(result);
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
		&& validateTitle(result)
		&& validateDescription(result)
		&& validateHistoryVisibility(result)
		&& validateSignatures(result)) {
		return result;
	}
	return {};
}

bool Controller::validateUsername(Saving &to) const {
	if (!_controls.privacy) {
		return true;
	} else if (_controls.privacySavedValue == Privacy::Private) {
		to.username = QString();
		return true;
	}
	auto username = _controls.usernameSavedValue.value_or(
		_peer->isChannel()
			? _peer->asChannel()->username
			: QString()
	);
	if (username.isEmpty()) {
		_controls.username->showError();
		_box->scrollToWidget(_controls.username);
		return false;
	}
	to.username = username;
	return true;
}

bool Controller::validateTitle(Saving &to) const {
	if (!_controls.title) {
		return true;
	}
	auto title = _controls.title->getLastText().trimmed();
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
	if (!_controls.historyVisibilityWrap->toggled()
		|| (_controls.privacy && _controls.privacy->value() == Privacy::Public)) {
		return true;
	}
	to.hiddenPreHistory
		= (_controls.historyVisibilitySavedValue == HistoryVisibility::Hidden);
	return true;
}

bool Controller::validateSignatures(Saving &to) const {
	if (!_controls.signaturesSavedValue.has_value()) {
		return true;
	}
	to.signatures = _controls.signaturesSavedValue;
	return true;
}

void Controller::save() {
	Expects(_wrap != nullptr);

	if (!_saveStagesQueue.empty()) {
		return;
	}
	if (auto saving = validate()) {
		_savingData = *saving;
		pushSaveStage([this] { saveUsername(); });
		pushSaveStage([this] { saveTitle(); });
		pushSaveStage([this] { saveDescription(); });
		pushSaveStage([this] { saveHistoryVisibility(); });
		pushSaveStage([this] { saveSignatures(); });
		pushSaveStage([this] { savePhoto(); });
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

	request(MTPchannels_UpdateUsername(
		channel->inputChannel,
		MTP_string(*_savingData.username)
	)).done([=](const MTPBool &result) {
		channel->setName(
			TextUtilities::SingleLine(channel->name),
			*_savingData.username);
		continueSave();
	}).fail([=](const RPCError &error) {
		const auto &type = error.type();
		if (type == qstr("USERNAME_NOT_MODIFIED")) {
			channel->setName(
				TextUtilities::SingleLine(channel->name),
				TextUtilities::SingleLine(*_savingData.username));
			continueSave();
			return;
		}
		const auto errorKey = [&] {
			if (type == qstr("USERNAME_INVALID")) {
				return lng_create_channel_link_invalid;
			} else if (type == qstr("USERNAME_OCCUPIED")
				|| type == qstr("USERNAMES_UNAVAILABLE")) {
				return lng_create_channel_link_invalid;
			}
			return lng_create_channel_link_invalid;
		}();
		_controls.username->showError();
		_box->scrollToWidget(_controls.username);
		showUsernameError(Lang::Viewer(errorKey));
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
	const auto onFail = [=](const RPCError &error) {
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
		request(MTPchannels_EditTitle(
			channel->inputChannel,
			MTP_string(*_savingData.title)
		)).done(std::move(onDone)
		).fail(std::move(onFail)
		).send();
	} else if (const auto chat = _peer->asChat()) {
		request(MTPmessages_EditChatTitle(
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
	const auto channel = _peer->asChannel();
	if (!_savingData.description
		|| *_savingData.description == _peer->about()) {
		return continueSave();
	}
	const auto successCallback = [=] {
		_peer->setAbout(*_savingData.description);
		continueSave();
	};
	request(MTPmessages_EditChatAbout(
		_peer->input,
		MTP_string(*_savingData.description)
	)).done([=](const MTPBool &result) {
		successCallback();
	}).fail([=](const RPCError &error) {
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
	request(MTPchannels_TogglePreHistoryHidden(
		channel->inputChannel,
		MTP_bool(*_savingData.hiddenPreHistory)
	)).done([=](const MTPUpdates &result) {
		// Update in the result doesn't contain the
		// channelFull:flags field which holds this value.
		// So after saving we need to update it manually.
		channel->updateFullForced();

		channel->session().api().applyUpdates(result);
		continueSave();
	}).fail([=](const RPCError &error) {
		if (error.type() == qstr("CHAT_NOT_MODIFIED")) {
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
	request(MTPchannels_ToggleSignatures(
		channel->inputChannel,
		MTP_bool(*_savingData.signatures)
	)).done([=](const MTPUpdates &result) {
		channel->session().api().applyUpdates(result);
		continueSave();
	}).fail([=](const RPCError &error) {
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

	const auto text = lang(_isGroup
		? lng_sure_delete_group
		: lng_sure_delete_channel);
	const auto deleteCallback = crl::guard(this, [=] {
		deleteChannel();
	});
	Ui::show(
		Box<ConfirmBox>(
			text,
			lang(lng_box_delete),
			st::attentionBoxButton,
			deleteCallback),
		LayerOption::KeepOther);
}

void Controller::deleteChannel() {
	const auto channel = _peer->asChannel();
	Assert(channel != nullptr);

	const auto chat = channel->migrateFrom();

	Ui::hideLayer();
	Ui::showChatsList();
	if (chat) {
		App::main()->deleteAndExit(chat);
	}
	MTP::send(
		MTPchannels_DeleteChannel(channel->inputChannel),
		App::main()->rpcDone(&MainWidget::sentUpdatesReceived),
		App::main()->rpcFail(&MainWidget::deleteChannelFailed));
}

} // namespace

EditPeerInfoBox::EditPeerInfoBox(
	QWidget*,
	not_null<PeerData*> peer)
: _peer(peer->migrateToOrMe()) {
}

void EditPeerInfoBox::prepare() {
	auto controller = Ui::CreateChild<Controller>(this, this, _peer);
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
