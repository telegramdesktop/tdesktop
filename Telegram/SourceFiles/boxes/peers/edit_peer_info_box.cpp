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
#include "mtproto/sender.h"
#include "lang/lang_keys.h"
#include "core/file_utilities.h"
#include "mainwidget.h"
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
	, private base::enable_weak_from_this {
public:
	Controller(
		not_null<BoxContent*> box,
		not_null<ChannelData*> channel);

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
	enum class UsernameState {
		Normal,
		TooMany,
		NotAvailable,
	};
	struct Controls {
		Ui::InputField *title = nullptr;
		Ui::InputArea *description = nullptr;
		Ui::NewAvatarButton *photo = nullptr;
		rpl::lifetime initialPhotoImageWaiting;

		std::shared_ptr<Ui::RadioenumGroup<Privacy>> privacy;
		Ui::SlideWrap<Ui::RpWidget> *usernameWrap = nullptr;
		Ui::UsernameInput *username = nullptr;
		base::unique_qptr<Ui::FlatLabel> usernameResult;
		const style::FlatLabel *usernameResultStyle = nullptr;

		Ui::SlideWrap<Ui::RpWidget> *createInviteLinkWrap = nullptr;
		Ui::SlideWrap<Ui::RpWidget> *editInviteLinkWrap = nullptr;
		Ui::FlatLabel *inviteLink = nullptr;

		std::shared_ptr<Ui::RadioenumGroup<Invites>> invites;
		Ui::Checkbox *signatures = nullptr;
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
	object_ptr<Ui::RpWidget> createSignaturesEdit();
	object_ptr<Ui::RpWidget> createInvitesEdit();
	object_ptr<Ui::RpWidget> createDeleteButton();

	void refreshInitialPhotoImage();
	void submitTitle();
	void submitDescription();
	void save();
	void deleteWithConfirmation();
	void choosePhotoDelayed();
	void choosePhoto();
	void suggestPhotoFile(
		const FileDialog::OpenResult &result);
	void suggestPhoto(const QImage &image);
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
	void createInviteLink();
	void revokeInviteLink();
	void exportInviteLink(const QString &confirmation);

	not_null<BoxContent*> _box;
	not_null<ChannelData*> _channel;
	bool _isGroup;

	base::unique_qptr<Ui::VerticalLayout> _wrap;
	Controls _controls;
	base::Timer _checkUsernameTimer;
	mtpRequestId _checkUsernameRequestId = 0;
	UsernameState _usernameState = UsernameState::Normal;
	rpl::event_stream<rpl::producer<QString>> _usernameResultTexts;

};

Controller::Controller(
	not_null<BoxContent*> box,
	not_null<ChannelData*> channel)
: _box(box)
, _channel(channel)
, _isGroup(_channel->isMegagroup())
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
	_wrap->add(createSignaturesEdit());
	_wrap->add(createInvitesEdit());
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

	if (!_channel->canEditInformation()) {
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
				+ st::editPeerPhotoSize;
			titleEdit->resizeToWidth(width - left);
			titleEdit->moveToLeft(left, 0, width);
		}, titleEdit->lifetime());

	return result;
}

object_ptr<Ui::RpWidget> Controller::createPhotoEdit() {
	Expects(_wrap != nullptr);

	using PhotoWrap = Ui::PaddingWrap<Ui::NewAvatarButton>;
	auto photoWrap = object_ptr<PhotoWrap>(
		_wrap,
		object_ptr<Ui::NewAvatarButton>(
			_wrap,
			st::editPeerPhotoSize,
			st::editPeerPhotoIconPosition),
		st::editPeerPhotoMargins);
	_controls.photo = photoWrap->entity();
	_controls.photo->addClickHandler([this] { choosePhotoDelayed(); });

	_controls.initialPhotoImageWaiting = base::ObservableViewer(
		Auth().downloaderTaskFinished())
		| rpl::start_with_next([=] {
			refreshInitialPhotoImage();
		});
	refreshInitialPhotoImage();

	return photoWrap;
}

void Controller::refreshInitialPhotoImage() {
	if (auto image = _channel->currentUserpic()) {
		image->load();
		if (image->loaded()) {
			_controls.photo->setImage(image->pixNoCache(
				st::editPeerPhotoSize * cIntRetinaFactor(),
				st::editPeerPhotoSize * cIntRetinaFactor(),
				Images::Option::Smooth).toImage());
			_controls.initialPhotoImageWaiting.destroy();
		}
	} else {
		_controls.initialPhotoImageWaiting.destroy();
	}
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
			_channel->name),
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

	auto result = object_ptr<Ui::PaddingWrap<Ui::InputArea>>(
		_wrap,
		object_ptr<Ui::InputArea>(
			_wrap,
			st::editPeerDescription,
			langFactory(lng_create_group_description),
			_channel->about()),
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

	if (!_channel->canEditUsername()) {
		return nullptr;
	}
	auto result = object_ptr<Ui::PaddingWrap<Ui::VerticalLayout>>(
		_wrap,
		object_ptr<Ui::VerticalLayout>(_wrap),
		st::editPeerPrivaciesMargins);
	auto container = result->entity();

	_controls.privacy = std::make_shared<Ui::RadioenumGroup<Privacy>>(
		_channel->isPublic() ? Privacy::Public : Privacy::Private);
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
	if (!_channel->isPublic()) {
		checkUsernameAvailability();
	}

	return std::move(result);
}

object_ptr<Ui::RpWidget> Controller::createUsernameEdit() {
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
			_channel->username,
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
	if (checking.size() < MinUsernameLength) {
		return;
	}
	if (_checkUsernameRequestId) {
		request(_checkUsernameRequestId).cancel();
	}
	_checkUsernameRequestId = request(MTPchannels_CheckUsername(
		_channel->inputChannel,
		MTP_string(checking)
	)).done([=](const MTPBool &result) {
		_checkUsernameRequestId = 0;
		if (initial) {
			return;
		}
		if (!mtpIsTrue(result) && checking != _channel->username) {
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
		} else if (rand() % 10 < 2 || type == qstr("CHANNELS_ADMIN_PUBLIC_TOO_MUCH")) {
			_usernameState = UsernameState::TooMany;
			if (_controls.privacy->value() == Privacy::Public) {
				askUsernameRevoke();
			}
		} else if (initial) {
			if (_controls.privacy->value() == Privacy::Public) {
				_controls.usernameResult = nullptr;
				_controls.username->setFocus();
			}
		} else if (type == qstr("USERNAME_INVALID")) {
			showUsernameError(
				Lang::Viewer(lng_create_channel_link_invalid));
		} else if (type == qstr("USERNAME_OCCUPIED")
			&& checking != _channel->username) {
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
	auto bad = base::find_if(username, [](QChar ch) {
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
		Auth().api().exportInviteLink(_channel);
	});
	auto box = Box<ConfirmBox>(
		confirmation,
		std::move(callback));
	*boxPointer = Ui::show(std::move(box), LayerOption::KeepOther);
}

bool Controller::canEditInviteLink() const {
	if (_channel->canEditUsername()) {
		return true;
	}
	return (!_channel->isPublic() && _channel->canAddMembers());
}

bool Controller::inviteLinkShown() const {
	return !_controls.privacy
		|| (_controls.privacy->value() == Privacy::Private);
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
		Application::clipboard()->setText(_channel->inviteLink());
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
		_channel,
		Notify::PeerUpdate::Flag::InviteLinkChanged)
		| rpl::start_with_next([this] {
			refreshEditInviteLink();
		}, _controls.editInviteLinkWrap->lifetime());

	return std::move(result);
}

void Controller::refreshEditInviteLink() {
	auto link = _channel->inviteLink();
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

	auto result = object_ptr<Ui::SlideWrap<Ui::LinkButton>>(
		_wrap,
		object_ptr<Ui::LinkButton>(
			_wrap,
			lang(lng_group_invite_create),
			st::editPeerInviteLinkButton),
		st::editPeerInviteLinkMargins);
	result->entity()->addClickHandler([this] {
		createInviteLink();
	});
	_controls.createInviteLinkWrap = result.data();

	Notify::PeerUpdateValue(
		_channel,
		Notify::PeerUpdate::Flag::InviteLinkChanged)
		| rpl::start_with_next([this] {
			refreshCreateInviteLink();
		}, _controls.createInviteLinkWrap->lifetime());

	return std::move(result);
}

void Controller::refreshCreateInviteLink() {
	auto link = _channel->inviteLink();
	_controls.createInviteLinkWrap->toggle(
		inviteLinkShown() && link.isEmpty(),
		anim::type::instant);
}

object_ptr<Ui::RpWidget> Controller::createSignaturesEdit() {
	Expects(_wrap != nullptr);

	if (!_channel->canEditInformation()
		|| _channel->isMegagroup()) {
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
				_channel->addsSignature(),
				st::defaultBoxCheckbox),
			st::editPeerSignaturesMargins))->entity();
	container->add(object_ptr<Ui::FixedHeightWidget>(
		container,
		st::defaultBoxCheckbox.margin.bottom()));
	return std::move(result);
}

object_ptr<Ui::RpWidget> Controller::createInvitesEdit() {
	Expects(_wrap != nullptr);

	if (!_channel->canEditInformation()
		|| !_channel->isMegagroup()) {
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
	container->add(object_ptr<Ui::FixedHeightWidget>(
		container,
		st::editPeerInvitesTopSkip));

	_controls.invites = std::make_shared<Ui::RadioenumGroup<Invites>>(
		_channel->anyoneCanAddMembers()
			? Invites::Everyone
			: Invites::OnlyAdmins);
	auto addButton = [&](
			Invites value,
			LangKey textKey) {
		container->add(object_ptr<Ui::FixedHeightWidget>(
			container,
			st::editPeerInvitesSkip));
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

	return std::move(result);
}

object_ptr<Ui::RpWidget> Controller::createDeleteButton() {
	Expects(_wrap != nullptr);

	if (!_channel->canDelete()) {
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
	Expects(_controls.description != nullptr);

	if (_controls.title->getLastText().isEmpty()) {
		_controls.title->showError();
	} else {
		_controls.description->setFocus();
	}
}

void Controller::submitDescription() {
	Expects(_controls.title != nullptr);
	Expects(_controls.description != nullptr);

	if (_controls.title->getLastText().isEmpty()) {
		_controls.title->showError();
		_controls.title->setFocus();
	} else {
		save();
	}
}

void Controller::save() {
	Expects(_wrap != nullptr);

}

void Controller::deleteWithConfirmation() {
	auto text = lang(_isGroup
		? lng_sure_delete_group
		: lng_sure_delete_channel);
	auto deleteCallback = [channel = _channel] {
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

void Controller::choosePhotoDelayed() {
	App::CallDelayed(
		st::defaultRippleAnimation.hideDuration,
		this,
		[this] { choosePhoto(); });
}

void Controller::choosePhoto() {
	auto handleChosenPhoto = base::lambda_guarded(
		_controls.photo,
		[this](auto &&result) { suggestPhotoFile(result); });

	auto imgExtensions = cImgExtensions();
	auto filter = qsl("Image files (*") + imgExtensions.join(qsl(" *")) + qsl(");;") + FileDialog::AllFilesFilter();
	FileDialog::GetOpenPath(
		lang(lng_choose_image),
		filter,
		std::move(handleChosenPhoto));
}

void Controller::suggestPhotoFile(
		const FileDialog::OpenResult &result) {
	if (result.paths.isEmpty() && result.remoteContent.isEmpty()) {
		return;
	}

	auto image = [&] {
		if (!result.remoteContent.isEmpty()) {
			return App::readImage(result.remoteContent);
		} else if (!result.paths.isEmpty()) {
			return App::readImage(result.paths.front());
		}
		return QImage();
	}();
	suggestPhoto(image);
}

void Controller::suggestPhoto(const QImage &image) {
	auto badAspect = [](int a, int b) {
		return (a >= 10 * b);
	};
	if (image.isNull()
		|| badAspect(image.width(), image.height())
		|| badAspect(image.height(), image.width())) {
		Ui::show(Box<InformBox>(lang(lng_bad_photo)));
		return;
	}

	auto callback = [this](const QImage &cropped) {
		_controls.photo->setImage(cropped);
	};
	auto box = Ui::show(
		Box<PhotoCropBox>(image, _channel),
		LayerOption::KeepOther);
	QObject::connect(
		box,
		&PhotoCropBox::ready,
		base::lambda_guarded(_controls.photo, std::move(callback)));
}

} // namespace

EditPeerInfoBox::EditPeerInfoBox(
	QWidget*,
	not_null<ChannelData*> channel)
: _channel(channel) {
}

void EditPeerInfoBox::prepare() {
	auto controller = std::make_unique<Controller>(this, _channel);
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
