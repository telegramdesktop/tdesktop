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
#include "boxes/peers/edit_peer_info_box.h"

#include <rpl/range.h>
#include <rpl/flatten_latest.h>
#include "info/profile/info_profile_button.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/widgets/input_fields.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/labels.h"
#include "ui/toast/toast.h"
#include "ui/special_buttons.h"
#include "boxes/confirm_box.h"
#include "boxes/photo_crop_box.h"
#include "boxes/add_contact_box.h"
#include "boxes/stickers_box.h"
#include "boxes/peer_list_controllers.h"
#include "mtproto/sender.h"
#include "lang/lang_keys.h"
#include "mainwidget.h"
#include "messenger.h"
#include "apiwrap.h"
#include "application.h"
#include "auth_session.h"
#include "observer_peer.h"
#include "styles/style_boxes.h"
#include "styles/style_info.h"

namespace {

constexpr auto kUsernameCheckTimeout = TimeMs(200);

class Controller
	: private MTP::Sender
	, private base::has_weak_ptr {
public:
	Controller(
		not_null<BoxContent*> box,
		not_null<PeerData*> peer);

	object_ptr<Ui::VerticalLayout> createContent();
	void setFocus();

private:
	enum class Privacy {
		Public,
		Private,
	};
	enum class Invites {
		Everyone,
		OnlyAdmins,
	};
	enum class HistoryVisibility {
		Visible,
		Hidden,
	};
	enum class UsernameState {
		Normal,
		TooMany,
		NotAvailable,
	};
	struct Controls {
		Ui::InputField *title = nullptr;
		Ui::InputArea *description = nullptr;
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

		std::shared_ptr<Ui::RadioenumGroup<HistoryVisibility>> historyVisibility;
		Ui::SlideWrap<Ui::RpWidget> *historyVisibilityWrap = nullptr;

		std::shared_ptr<Ui::RadioenumGroup<Invites>> invites;
		Ui::Checkbox *signatures = nullptr;
	};
	struct Saving {
		base::optional<QString> username;
		base::optional<QString> title;
		base::optional<QString> description;
		base::optional<bool> hiddenPreHistory;
		base::optional<bool> signatures;
		base::optional<bool> everyoneInvites;
	};

	base::lambda<QString()> computeTitle() const;
	object_ptr<Ui::RpWidget> createPhotoAndTitleEdit();
	object_ptr<Ui::RpWidget> createTitleEdit();
	object_ptr<Ui::RpWidget> createPhotoEdit();
	object_ptr<Ui::RpWidget> createDescriptionEdit();
	object_ptr<Ui::RpWidget> createPrivaciesEdit();
	object_ptr<Ui::RpWidget> createUsernameEdit();
	object_ptr<Ui::RpWidget> createInviteLinkCreate();
	object_ptr<Ui::RpWidget> createInviteLinkEdit();
	object_ptr<Ui::RpWidget> createHistoryVisibilityEdit();
	object_ptr<Ui::RpWidget> createSignaturesEdit();
	object_ptr<Ui::RpWidget> createInvitesEdit();
	object_ptr<Ui::RpWidget> createStickersEdit();
	object_ptr<Ui::RpWidget> createManageAdminsButton();
	object_ptr<Ui::RpWidget> createUpgradeButton();
	object_ptr<Ui::RpWidget> createDeleteButton();

	QString inviteLinkText() const;

	void submitTitle();
	void submitDescription();
	void deleteWithConfirmation();
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

	base::optional<Saving> validate() const;
	bool validateUsername(Saving &to) const;
	bool validateTitle(Saving &to) const;
	bool validateDescription(Saving &to) const;
	bool validateHistoryVisibility(Saving &to) const;
	bool validateInvites(Saving &to) const;
	bool validateSignatures(Saving &to) const;

	void save();
	void saveUsername();
	void saveTitle();
	void saveDescription();
	void saveHistoryVisibility();
	void saveInvites();
	void saveSignatures();
	void savePhoto();
	void pushSaveStage(base::lambda_once<void()> &&lambda);
	void continueSave();
	void cancelSave();

	not_null<BoxContent*> _box;
	not_null<PeerData*> _peer;
	bool _isGroup = false;

	base::unique_qptr<Ui::VerticalLayout> _wrap;
	Controls _controls;
	base::Timer _checkUsernameTimer;
	mtpRequestId _checkUsernameRequestId = 0;
	UsernameState _usernameState = UsernameState::Normal;
	rpl::event_stream<rpl::producer<QString>> _usernameResultTexts;

	std::deque<base::lambda_once<void()>> _saveStagesQueue;
	Saving _savingData;

};

Controller::Controller(
	not_null<BoxContent*> box,
	not_null<PeerData*> peer)
: _box(box)
, _peer(peer)
, _isGroup(_peer->isChat() || _peer->isMegagroup())
, _checkUsernameTimer([this] { checkUsernameAvailability(); }) {
	_box->setTitle(computeTitle());
	_box->addButton(langFactory(lng_settings_save), [this] {
		save();
	});
	_box->addButton(langFactory(lng_cancel), [this] {
		_box->closeBox();
	});
}

base::lambda<QString()> Controller::computeTitle() const {
	return langFactory(_isGroup
			? lng_edit_group
			: lng_edit_channel_title);
}

object_ptr<Ui::VerticalLayout> Controller::createContent() {
	auto result = object_ptr<Ui::VerticalLayout>(_box);
	_wrap.reset(result.data());
	_controls = Controls();

	_wrap->add(createPhotoAndTitleEdit());
	_wrap->add(createDescriptionEdit());
	_wrap->add(createPrivaciesEdit());
	_wrap->add(createInviteLinkCreate());
	_wrap->add(createInviteLinkEdit());
	_wrap->add(createHistoryVisibilityEdit());
	_wrap->add(createSignaturesEdit());
	_wrap->add(createInvitesEdit());
	_wrap->add(createStickersEdit());
	_wrap->add(createManageAdminsButton());
	_wrap->add(createUpgradeButton());
	_wrap->add(createDeleteButton());

	_wrap->resizeToWidth(st::boxWideWidth);

	return result;
}

void Controller::setFocus() {
	if (_controls.title) {
		_controls.title->setFocusFast();
	}
}

object_ptr<Ui::RpWidget> Controller::createPhotoAndTitleEdit() {
	Expects(_wrap != nullptr);

	auto canEdit = [&] {
		if (auto channel = _peer->asChannel()) {
			return channel->canEditInformation();
		} else if (auto chat = _peer->asChat()) {
			return chat->canEdit();
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
	photoWrap->heightValue()
		| rpl::start_with_next([container](int height) {
			container->resize(container->width(), height);
		}, photoWrap->lifetime());
	container->widthValue()
		| rpl::start_with_next([titleEdit](int width) {
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
			_box->controller(),
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

	QObject::connect(
		result->entity(),
		&Ui::InputField::submitted,
		[this] { submitTitle(); });

	_controls.title = result->entity();
	return std::move(result);
}

object_ptr<Ui::RpWidget> Controller::createDescriptionEdit() {
	Expects(_wrap != nullptr);

	auto channel = _peer->asChannel();
	if (!channel || !channel->canEditInformation()) {
		return nullptr;
	}

	auto result = object_ptr<Ui::PaddingWrap<Ui::InputArea>>(
		_wrap,
		object_ptr<Ui::InputArea>(
			_wrap,
			st::editPeerDescription,
			langFactory(lng_create_group_description),
			channel->about()),
		st::editPeerDescriptionMargins);

	QObject::connect(
		result->entity(),
		&Ui::InputArea::submitted,
		[this] { submitDescription(); });

	_controls.description = result->entity();
	return std::move(result);
}

object_ptr<Ui::RpWidget> Controller::createPrivaciesEdit() {
	Expects(_wrap != nullptr);

	auto channel = _peer->asChannel();
	if (!channel || !channel->canEditUsername()) {
		return nullptr;
	}
	auto result = object_ptr<Ui::PaddingWrap<Ui::VerticalLayout>>(
		_wrap,
		object_ptr<Ui::VerticalLayout>(_wrap),
		st::editPeerPrivaciesMargins);
	auto container = result->entity();

	_controls.privacy = std::make_shared<Ui::RadioenumGroup<Privacy>>(
		channel->isPublic() ? Privacy::Public : Privacy::Private);
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
			st::editPeerPrivacyLabelMargins));
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
	if (!channel->isPublic()) {
		checkUsernameAvailability();
	}

	return std::move(result);
}

object_ptr<Ui::RpWidget> Controller::createUsernameEdit() {
	Expects(_wrap != nullptr);

	auto channel = _peer->asChannel();
	Assert(channel != nullptr);

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
			base::lambda<QString()>(),
			channel->username,
			true));
	_controls.username->heightValue()
		| rpl::start_with_next([placeholder](int height) {
			placeholder->resize(placeholder->width(), height);
		}, placeholder->lifetime());
	placeholder->widthValue()
		| rpl::start_with_next([this](int width) {
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
	auto channel = _peer->asChannel();
	Assert(channel != nullptr);

	auto initial = (_controls.privacy->value() != Privacy::Public);
	auto checking = initial
		? qsl(".bad.")
		: _controls.username->getLastText().trimmed();
	if (checking.size() < MinUsernameLength) {
		return;
	}
	if (_checkUsernameRequestId) {
		request(_checkUsernameRequestId).cancel();
	}
	_checkUsernameRequestId = request(MTPchannels_CheckUsername(
		channel->inputChannel,
		MTP_string(checking)
	)).done([=](const MTPBool &result) {
		_checkUsernameRequestId = 0;
		if (initial) {
			return;
		}
		if (!mtpIsTrue(result) && checking != channel->username) {
			showUsernameError(
				Lang::Viewer(lng_create_channel_link_occupied));
		} else {
			showUsernameGood();
		}
	}).fail([=](const RPCError &error) {
		_checkUsernameRequestId = 0;
		auto type = error.type();
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
			&& checking != channel->username) {
			showUsernameError(
				Lang::Viewer(lng_create_channel_link_occupied));
		}
	}).send();
}

void Controller::askUsernameRevoke() {
	_controls.privacy->setValue(Privacy::Private);
	auto revokeCallback = base::lambda_guarded(this, [this] {
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
	} else if (username.size() < MinUsernameLength) {
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
		label->widthValue()
			| rpl::start_with_next([label] {
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
	auto callback = base::lambda_guarded(this, [=] {
		if (auto strong = *boxPointer) {
			strong->closeBox();
		}
		Auth().api().exportInviteLink(_peer);
	});
	auto box = Box<ConfirmBox>(
		confirmation,
		std::move(callback));
	*boxPointer = Ui::show(std::move(box), LayerOption::KeepOther);
}

bool Controller::canEditInviteLink() const {
	if (auto channel = _peer->asChannel()) {
		if (channel->canEditUsername()) {
			return true;
		}
		return (!channel->isPublic() && channel->canAddMembers());
	} else if (auto chat = _peer->asChat()) {
		return !chat->inviteLink().isEmpty() || chat->canEdit();
	}
	return false;
}

bool Controller::inviteLinkShown() const {
	return !_controls.privacy
		|| (_controls.privacy->value() == Privacy::Private);
}

QString Controller::inviteLinkText() const {
	if (auto channel = _peer->asChannel()) {
		return channel->inviteLink();
	} else if (auto chat = _peer->asChat()) {
		return chat->inviteLink();
	}
	return QString();
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
	_controls.inviteLink->setClickHandlerHook([this](auto&&...) {
		Application::clipboard()->setText(inviteLinkText());
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
	)->addClickHandler([this] { revokeInviteLink(); });

	Notify::PeerUpdateValue(
		_peer,
		Notify::PeerUpdate::Flag::InviteLinkChanged)
		| rpl::start_with_next([this] {
			refreshEditInviteLink();
		}, _controls.editInviteLinkWrap->lifetime());

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

	Notify::PeerUpdateValue(
		_peer,
		Notify::PeerUpdate::Flag::InviteLinkChanged)
		| rpl::start_with_next([this] {
			refreshCreateInviteLink();
		}, _controls.createInviteLinkWrap->lifetime());

	return std::move(result);
}

void Controller::refreshCreateInviteLink() {
	_controls.createInviteLinkWrap->toggle(
		inviteLinkShown() && inviteLinkText().isEmpty(),
		anim::type::instant);
}

object_ptr<Ui::RpWidget> Controller::createHistoryVisibilityEdit() {
	Expects(_wrap != nullptr);

	auto channel = _peer->asChannel();
	if (!channel
		|| !channel->canEditPreHistoryHidden()
		|| !channel->isMegagroup()) {
		return nullptr;
	}
	auto result = object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
		_wrap,
		object_ptr<Ui::VerticalLayout>(_wrap),
		st::editPeerInvitesMargins);
	_controls.historyVisibilityWrap = result.data();
	auto container = result->entity();

	_controls.historyVisibility
		= std::make_shared<Ui::RadioenumGroup<HistoryVisibility>>(
			channel->hiddenPreHistory()
				? HistoryVisibility::Hidden
				: HistoryVisibility::Visible);
	auto addButton = [&](
			HistoryVisibility value,
			LangKey groupTextKey,
			LangKey groupAboutKey) {
		container->add(object_ptr<Ui::FixedHeightWidget>(
			container,
			st::editPeerPrivacyTopSkip + st::editPeerPrivacyBottomSkip));
		container->add(object_ptr<Ui::Radioenum<HistoryVisibility>>(
			container,
			_controls.historyVisibility,
			value,
			lang(groupTextKey),
			st::defaultBoxCheckbox));
		container->add(object_ptr<Ui::PaddingWrap<Ui::FlatLabel>>(
			container,
			object_ptr<Ui::FlatLabel>(
				container,
				Lang::Viewer(groupAboutKey),
				st::editPeerPrivacyLabel),
			st::editPeerPrivacyLabelMargins));
	};

	container->add(object_ptr<Ui::FlatLabel>(
		container,
		Lang::Viewer(lng_manage_history_visibility_title),
		st::editPeerSectionLabel));
	addButton(
		HistoryVisibility::Visible,
		lng_manage_history_visibility_shown,
		lng_manage_history_visibility_shown_about);
	addButton(
		HistoryVisibility::Hidden,
		lng_manage_history_visibility_hidden,
		lng_manage_history_visibility_hidden_about);

	refreshHistoryVisibility();

	return std::move(result);
}

void Controller::refreshHistoryVisibility() {
	if (!_controls.historyVisibilityWrap) {
		return;
	}
	auto historyVisibilityShown = !_controls.privacy
		|| (_controls.privacy->value() == Privacy::Private);
	_controls.historyVisibilityWrap->toggle(
		historyVisibilityShown,
		anim::type::instant);
}

object_ptr<Ui::RpWidget> Controller::createSignaturesEdit() {
	Expects(_wrap != nullptr);

	auto channel = _peer->asChannel();
	if (!channel
		|| !channel->canEditSignatures()
		|| channel->isMegagroup()) {
		return nullptr;
	}
	auto result = object_ptr<Ui::VerticalLayout>(_wrap);
	auto container = result.data();
	container->add(object_ptr<Ui::FixedHeightWidget>(
		container,
		st::defaultBoxCheckbox.margin.top()));
	_controls.signatures = container->add(
		object_ptr<Ui::PaddingWrap<Ui::Checkbox>>(
			container,
			object_ptr<Ui::Checkbox>(
				container,
				lang(lng_edit_sign_messages),
				channel->addsSignature(),
				st::defaultBoxCheckbox),
			st::editPeerSignaturesMargins))->entity();
	container->add(object_ptr<Ui::FixedHeightWidget>(
		container,
		st::defaultBoxCheckbox.margin.bottom()));
	return std::move(result);
}

object_ptr<Ui::RpWidget> Controller::createInvitesEdit() {
	Expects(_wrap != nullptr);

	auto channel = _peer->asChannel();
	if (!channel
		|| !channel->canEditInvites()
		|| !channel->isMegagroup()) {
		return nullptr;
	}

	auto result = object_ptr<Ui::PaddingWrap<Ui::VerticalLayout>>(
		_wrap,
		object_ptr<Ui::VerticalLayout>(_wrap),
		st::editPeerInvitesMargins);

	auto container = result->entity();
	container->add(object_ptr<Ui::FlatLabel>(
		container,
		Lang::Viewer(lng_edit_group_who_invites),
		st::editPeerSectionLabel));

	_controls.invites = std::make_shared<Ui::RadioenumGroup<Invites>>(
		channel->anyoneCanAddMembers()
			? Invites::Everyone
			: Invites::OnlyAdmins);
	auto addButton = [&](
			Invites value,
			LangKey textKey) {
		container->add(object_ptr<Ui::FixedHeightWidget>(
			container,
			st::editPeerInvitesTopSkip + st::editPeerInvitesSkip));
		container->add(object_ptr<Ui::Radioenum<Invites>>(
			container,
			_controls.invites,
			value,
			lang(textKey),
			st::defaultBoxCheckbox));
	};
	addButton(
		Invites::Everyone,
		lng_edit_group_invites_everybody);
	addButton(
		Invites::OnlyAdmins,
		lng_edit_group_invites_only_admins);
	container->add(object_ptr<Ui::FixedHeightWidget>(
		container,
		st::editPeerInvitesSkip));

	return std::move(result);
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
	)->addClickHandler([channel] {
		Ui::show(Box<StickersBox>(channel), LayerOption::KeepOther);
	});

	return std::move(result);
}

object_ptr<Ui::RpWidget> Controller::createManageAdminsButton() {
	Expects(_wrap != nullptr);

	auto chat = _peer->asChat();
	if (!chat || !chat->amCreator() || chat->isDeactivated()) {
		return nullptr;
	}
	auto result = object_ptr<Ui::PaddingWrap<Ui::LinkButton>>(
		_wrap,
		object_ptr<Ui::LinkButton>(
			_wrap,
			lang(lng_profile_manage_admins),
			st::editPeerInviteLinkButton),
		st::editPeerDeleteButtonMargins);
	result->entity()->addClickHandler([=] {
		EditChatAdminsBoxController::Start(chat);
	});
	return std::move(result);
}

object_ptr<Ui::RpWidget> Controller::createUpgradeButton() {
	Expects(_wrap != nullptr);

	auto chat = _peer->asChat();
	if (!chat || !chat->amCreator() || chat->isDeactivated()) {
		return nullptr;
	}
	auto result = object_ptr<Ui::PaddingWrap<Ui::LinkButton>>(
		_wrap,
		object_ptr<Ui::LinkButton>(
			_wrap,
			lang(lng_profile_migrate_button),
			st::editPeerInviteLinkButton),
		st::editPeerDeleteButtonMargins);
	result->entity()->addClickHandler([=] {
		Ui::show(Box<ConvertToSupergroupBox>(chat), LayerOption::KeepOther);
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

base::optional<Controller::Saving> Controller::validate() const {
	auto result = Saving();
	if (validateUsername(result)
		&& validateTitle(result)
		&& validateDescription(result)
		&& validateHistoryVisibility(result)
		&& validateInvites(result)
		&& validateSignatures(result)) {
		return result;
	}
	return {};
}

bool Controller::validateUsername(Saving &to) const {
	if (!_controls.privacy) {
		return true;
	} else if (_controls.privacy->value() == Privacy::Private) {
		to.username = QString();
		return true;
	}
	auto username = _controls.username->getLastText().trimmed();
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
	if (!_controls.historyVisibility
		|| (_controls.privacy && _controls.privacy->value() == Privacy::Public)) {
		return true;
	}
	to.hiddenPreHistory
		= (_controls.historyVisibility->value() == HistoryVisibility::Hidden);
	return true;
}

bool Controller::validateInvites(Saving &to) const {
	if (!_controls.invites) {
		return true;
	}
	to.everyoneInvites
		= (_controls.invites->value() == Invites::Everyone);
	return true;
}

bool Controller::validateSignatures(Saving &to) const {
	if (!_controls.signatures) {
		return true;
	}
	to.signatures = _controls.signatures->checked();
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
		pushSaveStage([this] { saveInvites(); });
		pushSaveStage([this] { saveSignatures(); });
		pushSaveStage([this] { savePhoto(); });
		continueSave();
	}
}

void Controller::pushSaveStage(base::lambda_once<void()> &&lambda) {
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
	auto channel = _peer->asChannel();
	if (!_savingData.username
		|| !channel
		|| *_savingData.username == channel->username) {
		return continueSave();
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
		auto type = error.type();
		if (type == qstr("USERNAME_NOT_MODIFIED")) {
			channel->setName(
				TextUtilities::SingleLine(channel->name),
				TextUtilities::SingleLine(*_savingData.username));
			continueSave();
			return;
		}
		auto errorKey = [&] {
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

	auto onDone = [this](const MTPUpdates &result) {
		Auth().api().applyUpdates(result);
		continueSave();
	};
	auto onFail = [this](const RPCError &error) {
		auto type = error.type();
		if (type == qstr("CHAT_NOT_MODIFIED")
			|| type == qstr("CHAT_TITLE_NOT_MODIFIED")) {
			if (auto channel = _peer->asChannel()) {
				channel->setName(*_savingData.title, channel->username);
			} else if (auto chat = _peer->asChat()) {
				chat->setName(*_savingData.title);
			}
			continueSave();
			return;
		}
		if (type == qstr("NO_CHAT_TITLE")) {
			_controls.title->showError();
			_box->scrollToWidget(_controls.title);
		} else {
			_controls.title->setFocus();
		}
		cancelSave();
	};

	if (auto channel = _peer->asChannel()) {
		request(MTPchannels_EditTitle(
			channel->inputChannel,
			MTP_string(*_savingData.title)
		)).done(std::move(onDone)
		).fail(std::move(onFail)
		).send();
	} else if (auto chat = _peer->asChat()) {
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
	auto channel = _peer->asChannel();
	if (!_savingData.description
		|| !channel
		|| *_savingData.description == channel->about()) {
		return continueSave();
	}
	auto successCallback = [=] {
		channel->setAbout(*_savingData.description);
		continueSave();
	};
	request(MTPchannels_EditAbout(
		channel->inputChannel,
		MTP_string(*_savingData.description)
	)).done([=](const MTPBool &result) {
		successCallback();
	}).fail([=](const RPCError &error) {
		auto type = error.type();
		if (type == qstr("CHAT_ABOUT_NOT_MODIFIED")) {
			successCallback();
			return;
		}
		_controls.description->setFocus();
		cancelSave();
	}).send();
}

void Controller::saveHistoryVisibility() {
	auto channel = _peer->asChannel();
	if (!_savingData.hiddenPreHistory
		|| !channel
		|| *_savingData.hiddenPreHistory == channel->hiddenPreHistory()) {
		return continueSave();
	}
	request(MTPchannels_TogglePreHistoryHidden(
		channel->inputChannel,
		MTP_bool(*_savingData.hiddenPreHistory)
	)).done([=](const MTPUpdates &result) {
		// Update in the result doesn't contain the
		// channelFull:flags field which holds this value.
		// So after saving we need to update it manually.
		channel->updateFullForced();

		Auth().api().applyUpdates(result);
		continueSave();
	}).fail([this](const RPCError &error) {
		if (error.type() == qstr("CHAT_NOT_MODIFIED")) {
			continueSave();
		} else {
			cancelSave();
		}
	}).send();
}

void Controller::saveInvites() {
	auto channel = _peer->asChannel();
	if (!_savingData.everyoneInvites
		|| !channel
		|| *_savingData.everyoneInvites == channel->anyoneCanAddMembers()) {
		return continueSave();
	}
	request(MTPchannels_ToggleInvites(
		channel->inputChannel,
		MTP_bool(*_savingData.everyoneInvites)
	)).done([this](const MTPUpdates &result) {
		Auth().api().applyUpdates(result);
		continueSave();
	}).fail([this](const RPCError &error) {
		if (error.type() == qstr("CHAT_NOT_MODIFIED")) {
			continueSave();
		} else {
			cancelSave();
		}
	}).send();
}

void Controller::saveSignatures() {
	auto channel = _peer->asChannel();
	if (!_savingData.signatures
		|| !channel
		|| *_savingData.signatures == channel->addsSignature()) {
		return continueSave();
	}
	request(MTPchannels_ToggleSignatures(
		channel->inputChannel,
		MTP_bool(*_savingData.signatures)
	)).done([this](const MTPUpdates &result) {
		Auth().api().applyUpdates(result);
		continueSave();
	}).fail([this](const RPCError &error) {
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
		Messenger::Instance().uploadProfilePhoto(
			std::move(image),
			_peer->id);
	}
	_box->closeBox();
}

void Controller::deleteWithConfirmation() {
	auto channel = _peer->asChannel();
	Assert(channel != nullptr);

	auto text = lang(_isGroup
		? lng_sure_delete_group
		: lng_sure_delete_channel);
	auto deleteCallback = [=] {
		Ui::hideLayer();
		Ui::showChatsList();
		if (auto chat = channel->migrateFrom()) {
			App::main()->deleteAndExit(chat);
		}
		MTP::send(
			MTPchannels_DeleteChannel(channel->inputChannel),
			App::main()->rpcDone(&MainWidget::sentUpdatesReceived),
			App::main()->rpcFail(&MainWidget::deleteChannelFailed));
	};
	Ui::show(Box<ConfirmBox>(
		text,
		lang(lng_box_delete),
		st::attentionBoxButton,
		std::move(deleteCallback)), LayerOption::KeepOther);
}

} // namespace

EditPeerInfoBox::EditPeerInfoBox(
	QWidget*,
	not_null<PeerData*> peer)
: _peer(peer) {
}

void EditPeerInfoBox::prepare() {
	auto controller = std::make_unique<Controller>(this, _peer);
	_focusRequests.events()
		| rpl::start_with_next(
			[c = controller.get()] { c->setFocus(); },
			lifetime());
	auto content = controller->createContent();
	content->heightValue()
		| rpl::start_with_next([this](int height) {
			setDimensions(st::boxWideWidth, height);
		}, content->lifetime());
	setInnerWidget(object_ptr<Ui::IgnoreMargins>(
		this,
		std::move(content)));
	Ui::AttachAsChild(this, std::move(controller));
}
