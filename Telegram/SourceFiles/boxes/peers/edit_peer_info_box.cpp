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
#include "boxes/peers/edit_peer_type_box.h"
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
#include "info/profile/info_profile_icon.h"

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
	container->add(object_ptr<BoxContentDivider>(container));
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
	EditPeerInfoBox::CreateButton(
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
	return EditPeerInfoBox::CreateButton(
		parent,
		std::move(text),
		std::move(label),
		std::move(callback),
		st::manageGroupTopButtonWithText,
		nullptr);
}

void AddButtonDelete(
		not_null<Ui::VerticalLayout*> parent,
		rpl::producer<QString> &&text,
		Fn<void()> callback) {
	EditPeerInfoBox::CreateButton(
		parent,
		std::move(text),
		rpl::single(QString()),
		std::move(callback),
		st::manageDeleteGroupButton,
		nullptr);
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

} // namespace

namespace {

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
		Ui::VerticalLayout *buttonsLayout = nullptr;
		Ui::SlideWrap<Ui::RpWidget> *historyVisibilityWrap = nullptr;
	};
	struct Saving {
		std::optional<QString> username;
		std::optional<QString> title;
		std::optional<QString> description;
		std::optional<bool> hiddenPreHistory;
		std::optional<bool> signatures;
	};

	object_ptr<Ui::RpWidget> createPhotoAndTitleEdit();
	object_ptr<Ui::RpWidget> createTitleEdit();
	object_ptr<Ui::RpWidget> createPhotoEdit();
	object_ptr<Ui::RpWidget> createDescriptionEdit();
	object_ptr<Ui::RpWidget> createManageGroupButtons();
	object_ptr<Ui::RpWidget> createStickersEdit();

	bool canEditInformation() const;
	void refreshHistoryVisibility(bool instant);
	void showEditPeerTypeBox(std::optional<LangKey> error = std::nullopt);
	void fillPrivacyTypeButton();
	void fillInviteLinkButton();
	void fillSignaturesButton();
	void fillHistoryVisibilityButton();
	void fillManageSection();

	void submitTitle();
	void submitDescription();
	void deleteWithConfirmation();
	void deleteChannel();

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

	std::optional<Privacy> _privacySavedValue = {};
	std::optional<HistoryVisibility> _historyVisibilitySavedValue = {};
	std::optional<QString> _usernameSavedValue = {};
	std::optional<bool> _signaturesSavedValue = {};

	not_null<BoxContent*> _box;
	not_null<PeerData*> _peer;
	bool _isGroup = false;

	base::unique_qptr<Ui::VerticalLayout> _wrap;
	Controls _controls;

	std::deque<FnMut<void()>> _saveStagesQueue;
	Saving _savingData;

	const rpl::event_stream<Privacy> _updadePrivacyType;

	rpl::lifetime _lifetime;

};

Controller::Controller(
	not_null<BoxContent*> box,
	not_null<PeerData*> peer)
: _box(box)
, _peer(peer)
, _isGroup(_peer->isChat() || _peer->isMegagroup()) {
	_box->setTitle(langFactory(_isGroup
		? lng_edit_group
		: lng_edit_channel_title));
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

	if (!canEditInformation()) {
		return nullptr;
	}

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

object_ptr<Ui::RpWidget> Controller::createManageGroupButtons() {
	Expects(_wrap != nullptr);

	auto result = object_ptr<Ui::PaddingWrap<Ui::VerticalLayout>>(
		_wrap,
		object_ptr<Ui::VerticalLayout>(_wrap),
		st::editPeerBottomButtonsLayoutMargins);
	_controls.buttonsLayout = result->entity();

	fillManageSection();

	return std::move(result);
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

bool Controller::canEditInformation() const {
	if (const auto channel = _peer->asChannel()) {
		return channel->canEditInformation();
	} else if (const auto chat = _peer->asChat()) {
		return chat->canEditInformation();
	}
	return false;
}

void Controller::refreshHistoryVisibility(bool instant = false) {
	if (!_controls.historyVisibilityWrap) {
		return;
	}
	_controls.historyVisibilityWrap->toggle(
		_privacySavedValue != Privacy::Public,
		instant ? anim::type::instant : anim::type::normal);
};

void Controller::showEditPeerTypeBox(std::optional<LangKey> error) {
	const auto boxCallback = crl::guard(this, [=](
			Privacy checked, QString publicLink) {
		_updadePrivacyType.fire(std::move(checked));
		_privacySavedValue = checked;
		_usernameSavedValue = publicLink;
		refreshHistoryVisibility();
	});
	Ui::show(
		Box<EditPeerTypeBox>(
			_peer,
			boxCallback,
			_privacySavedValue,
			_usernameSavedValue,
			error),
		LayerOption::KeepOther);
}

void Controller::fillPrivacyTypeButton() {
	Expects(_controls.buttonsLayout != nullptr);

	// Create Privacy Button.
	_privacySavedValue = (_peer->isChannel()
		&& _peer->asChannel()->isPublic())
		? Privacy::Public
		: Privacy::Private;

	AddButtonWithText(
		_controls.buttonsLayout,
		Lang::Viewer((_peer->isChat() || _peer->isMegagroup())
			? lng_manage_peer_group_type
			: lng_manage_peer_channel_type),
		_updadePrivacyType.events(
		) | rpl::map([](Privacy flag) {
			return lang(Privacy::Public == flag
				? lng_manage_public_peer_title
				: lng_manage_private_peer_title);
		}),
		[=] { showEditPeerTypeBox(); });

	_updadePrivacyType.fire_copy(*_privacySavedValue);
}

void Controller::fillInviteLinkButton() {
	Expects(_controls.buttonsLayout != nullptr);

	const auto buttonCallback = [=] {
		Ui::show(
			Box<EditPeerTypeBox>(_peer),
			LayerOption::KeepOther);
	};

	AddButtonWithText(
		_controls.buttonsLayout,
		Lang::Viewer(lng_profile_invite_link_section),
		rpl::single(QString()), //Empty text.
		buttonCallback);
}

void Controller::fillSignaturesButton() {
	Expects(_controls.buttonsLayout != nullptr);
	const auto channel = _peer->asChannel();
	if (!channel) return;

	AddButtonWithText(
		_controls.buttonsLayout,
		Lang::Viewer(lng_edit_sign_messages),
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

	const auto updateHistoryVisibility =
		std::make_shared<rpl::event_stream<HistoryVisibility>>();

	const auto boxCallback = crl::guard(this, [=](HistoryVisibility checked) {
		updateHistoryVisibility->fire(std::move(checked));
		_historyVisibilitySavedValue = checked;
	});
	const auto buttonCallback = [=] {
		Ui::show(
			Box<EditPeerHistoryVisibilityBox>(
				_peer,
				boxCallback,
				*_historyVisibilitySavedValue),
			LayerOption::KeepOther);
	};
	AddButtonWithText(
		container,
		Lang::Viewer(lng_manage_history_visibility_title),
		updateHistoryVisibility->events(
		) | rpl::map([](HistoryVisibility flag) {
			return lang((HistoryVisibility::Visible == flag)
				? lng_manage_history_visibility_shown
				: lng_manage_history_visibility_hidden);
		}),
		buttonCallback);

	updateHistoryVisibility->fire_copy(*_historyVisibilitySavedValue);

	//While appearing box we should use instant animation.
	refreshHistoryVisibility(true);
}

void Controller::fillManageSection() {
	Expects(_controls.buttonsLayout != nullptr);

	const auto navigation = App::wnd()->controller();

	const auto chat = _peer->asChat();
	const auto channel = _peer->asChannel();
	const auto isChannel = (!chat);
	if (!chat && !channel) return;

	const auto canEditUsername = [=] {
		return isChannel
			? channel->canEditUsername()
			: chat->canEditUsername();
	}();
	const auto canEditInviteLink = [=] {
		return isChannel
			? (channel->amCreator()
				|| (channel->adminRights() & ChatAdminRight::f_invite_users))
			: (chat->amCreator()
				|| (chat->adminRights() & ChatAdminRight::f_invite_users));
	}();
	const auto canEditSignatures = [=] {
		return isChannel
			? (channel->canEditSignatures() && !channel->isMegagroup())
			: false;
	}();
	const auto canEditPreHistoryHidden = [=] {
		return isChannel
			? channel->canEditPreHistoryHidden()
			: chat->canEditPreHistoryHidden();
	}();

	const auto canEditPermissions = [=] {
		return isChannel
			? channel->canEditPermissions()
			: chat->canEditPermissions();
	}();
	const auto canViewAdmins = [=] {
		return isChannel
			? channel->canViewAdmins()
			: chat->amIn();
	}();
	const auto canViewMembers = [=] {
		return isChannel
			? channel->canViewMembers()
			: chat->amIn();
	}();
	const auto canViewKicked = [=] {
		return isChannel
			? (!channel->isMegagroup())
			: false;
	}();
	const auto hasRecentActions = [=] {
		return isChannel
			? (channel->hasAdminRights() || channel->amCreator())
			: false;
	}();

	const auto canEditStickers = [=] {
		// return true;
		return isChannel
			? channel->canEditStickers()
			: false;
	}();
	const auto canDeleteChannel = [=] {
		return isChannel
			? channel->canDelete()
			: false;
	}();

	AddSkip(_controls.buttonsLayout, 0);

	if (canEditUsername) {
		fillPrivacyTypeButton();
	}
	else if (canEditInviteLink) {
		fillInviteLinkButton();
	}
	if (canEditSignatures) {
		fillSignaturesButton();
	}
	if (canEditPreHistoryHidden) {
		fillHistoryVisibilityButton();
	}
	if (canEditPreHistoryHidden
		|| canEditSignatures
		|| canEditInviteLink
		|| canEditUsername) {
		AddSkip(
			_controls.buttonsLayout,
			st::editPeerTopButtonsLayoutSkip,
			st::editPeerTopButtonsLayoutSkipCustomBottom);
	}

	if (canEditPermissions) {
		AddButtonWithCount(
			_controls.buttonsLayout,
			Lang::Viewer(lng_manage_peer_permissions),
			Info::Profile::RestrictionsCountValue(_peer)
			| ToPositiveNumberStringRestrictions(),
			[=] { ShowEditPermissions(_peer); },
			st::infoIconPermissions);
	}
	if (canViewAdmins) {
		AddButtonWithCount(
			_controls.buttonsLayout,
			Lang::Viewer(lng_manage_peer_administrators),
			Info::Profile::AdminsCountValue(_peer)
			| ToPositiveNumberString(),
			[=] {
				ParticipantsBoxController::Start(
					navigation,
					_peer,
					ParticipantsBoxController::Role::Admins);
			},
			st::infoIconAdministrators);
	}
	if (canViewMembers) {
		AddButtonWithCount(
			_controls.buttonsLayout,
			Lang::Viewer(lng_manage_peer_members),
			Info::Profile::MembersCountValue(_peer)
			| ToPositiveNumberString(),
			[=] {
				ParticipantsBoxController::Start(
					navigation,
					_peer,
					ParticipantsBoxController::Role::Members);
			},
			st::infoIconMembers);
	}
	if (canViewKicked) {
		AddButtonWithCount(
			_controls.buttonsLayout,
			Lang::Viewer(lng_manage_peer_removed_users),
			Info::Profile::KickedCountValue(channel)
			| ToPositiveNumberString(),
			[=] {
				ParticipantsBoxController::Start(
					navigation,
					_peer,
					ParticipantsBoxController::Role::Kicked);
			},
			st::infoIconBlacklist);
	}
	if (hasRecentActions) {
		AddButtonWithCount(
			_controls.buttonsLayout,
			Lang::Viewer(lng_manage_peer_recent_actions),
			rpl::single(QString()), //Empty count.
			[=] {
				navigation->showSection(AdminLog::SectionMemento(channel));
			},
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
			Lang::Viewer(_isGroup
				? lng_profile_delete_group
				: lng_profile_delete_channel),
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
	} else if (_privacySavedValue != Privacy::Public) {
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
		|| (_privacySavedValue == Privacy::Public)) {
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
				return lng_create_channel_link_occupied;
			}
			return lng_create_channel_link_invalid;
		}();
		// Very rare case.
		showEditPeerTypeBox(errorKey);
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
	Expects(_peer->isChannel());

	const auto channel = _peer->asChannel();
	const auto chat = channel->migrateFrom();

	Ui::hideLayer();
	Ui::showChatsList();
	if (chat) {
		chat->session().api().deleteConversation(chat, false);
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
	const auto controller = Ui::CreateChild<Controller>(this, this, _peer);
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

Info::Profile::Button *EditPeerInfoBox::CreateButton(
		not_null<Ui::VerticalLayout*> parent,
		rpl::producer<QString> &&text,
		rpl::producer<QString> &&count,
		Fn<void()> callback,
		const style::InfoProfileCountButton &st,
		const style::icon *icon) {
	const auto button = parent->add(
		object_ptr<Info::Profile::Button>(
			parent,
			std::move(text),
			st.button));
	button->addClickHandler(callback);
	if (icon) {
		Ui::CreateChild<Info::Profile::FloatingIcon>(
			button,
			*icon,
			st.iconPosition);
	}
	const auto label = Ui::CreateChild<Ui::FlatLabel>(
		button,
		std::move(count),
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

	return button;
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
