/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/peers/edit_peer_type_box.h"

#include "apiwrap.h"
#include "api/api_invite_links.h"
#include "main/main_session.h"
#include "boxes/add_contact_box.h"
#include "boxes/confirm_box.h"
#include "boxes/peer_list_controllers.h"
#include "boxes/peers/edit_participants_box.h"
#include "boxes/peers/edit_peer_info_box.h" // CreateButton.
#include "boxes/peers/edit_peer_invite_link.h"
#include "boxes/peers/edit_peer_invite_links.h"
#include "chat_helpers/emoji_suggestions_widget.h"
#include "core/application.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_changes.h"
#include "info/profile/info_profile_values.h"
#include "lang/lang_keys.h"
#include "mainwindow.h"
#include "mtproto/sender.h"
#include "ui/rp_widget.h"
#include "ui/special_buttons.h"
#include "ui/toast/toast.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/input_fields.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/box_content_divider.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/special_fields.h"
#include "window/window_session_controller.h"
#include "settings/settings_common.h"
#include "styles/style_layers.h"
#include "styles/style_boxes.h"
#include "styles/style_info.h"
#include "styles/style_settings.h"

#include <QtGui/QGuiApplication>
#include <QtGui/QClipboard>

#include <rpl/flatten_latest.h>

namespace {

constexpr auto kUsernameCheckTimeout = crl::time(200);
constexpr auto kMinUsernameLength = 5;

class Controller : public base::has_weak_ptr {
public:
	Controller(
		not_null<Ui::VerticalLayout*> container,
		not_null<PeerData*> peer,
		bool useLocationPhrases,
		std::optional<Privacy> privacySavedValue,
		std::optional<QString> usernameSavedValue);

	void createContent();
	QString getUsernameInput();
	void setFocusUsername();

	rpl::producer<QString> getTitle() {
		return !_privacySavedValue
			? tr::lng_create_invite_link_title()
			: _isGroup
			? tr::lng_manage_peer_group_type()
			: tr::lng_manage_peer_channel_type();
	}

	bool isAllowSave() {
		return _isAllowSave;
	}

	Privacy getPrivacy() {
		return _controls.privacy->value();
	}

	void showError(rpl::producer<QString> text) {
		_controls.usernameInput->showError();
		showUsernameError(std::move(text));
	}

private:
	struct Controls {
		std::shared_ptr<Ui::RadioenumGroup<Privacy>> privacy;
		Ui::SlideWrap<Ui::RpWidget> *usernameWrap = nullptr;
		Ui::UsernameInput *usernameInput = nullptr;
		base::unique_qptr<Ui::FlatLabel> usernameResult;
		const style::FlatLabel *usernameResultStyle = nullptr;

		Ui::SlideWrap<Ui::RpWidget> *inviteLinkWrap = nullptr;
		Ui::FlatLabel *inviteLink = nullptr;
	};

	Controls _controls;

	object_ptr<Ui::RpWidget> createUsernameEdit();
	object_ptr<Ui::RpWidget> createInviteLinkBlock();

	void privacyChanged(Privacy value);

	void checkUsernameAvailability();
	void askUsernameRevoke();
	void usernameChanged();
	void showUsernameError(rpl::producer<QString> &&error);
	void showUsernameGood();
	void showUsernameResult(
		rpl::producer<QString> &&text,
		not_null<const style::FlatLabel*> st);

	void fillPrivaciesButtons(
		not_null<Ui::VerticalLayout*> parent,
		std::optional<Privacy> savedValue);
	void addRoundButton(
		not_null<Ui::VerticalLayout*> container,
		Privacy value,
		const QString &text,
		rpl::producer<QString> about);

	bool inviteLinkShown();
	QString inviteLinkText();

	not_null<PeerData*> _peer;
	bool _linkOnly = false;

	MTP::Sender _api;
	std::optional<Privacy> _privacySavedValue;
	std::optional<QString> _usernameSavedValue;

	bool _useLocationPhrases = false;
	bool _isGroup = false;
	bool _isAllowSave = false;

	base::unique_qptr<Ui::VerticalLayout> _wrap;
	base::Timer _checkUsernameTimer;
	mtpRequestId _checkUsernameRequestId = 0;
	UsernameState _usernameState = UsernameState::Normal;
	rpl::event_stream<rpl::producer<QString>> _usernameResultTexts;

	rpl::lifetime _lifetime;

};

Controller::Controller(
	not_null<Ui::VerticalLayout*> container,
	not_null<PeerData*> peer,
	bool useLocationPhrases,
	std::optional<Privacy> privacySavedValue,
	std::optional<QString> usernameSavedValue)
: _peer(peer)
, _linkOnly(!privacySavedValue.has_value())
, _api(&_peer->session().mtp())
, _privacySavedValue(privacySavedValue)
, _usernameSavedValue(usernameSavedValue)
, _useLocationPhrases(useLocationPhrases)
, _isGroup(_peer->isChat() || _peer->isMegagroup())
, _isAllowSave(!_usernameSavedValue.value_or(QString()).isEmpty())
, _wrap(container)
, _checkUsernameTimer([=] { checkUsernameAvailability(); }) {
	_peer->updateFull();
}

void Controller::createContent() {
	_controls = Controls();

	fillPrivaciesButtons(_wrap, _privacySavedValue);
	// Skip.
	if (!_linkOnly) {
		_wrap->add(object_ptr<Ui::BoxContentDivider>(_wrap));
	}
	//
	_wrap->add(createInviteLinkBlock());
	if (!_linkOnly) {
		_wrap->add(createUsernameEdit());
	}

	using namespace Settings;
	AddSkip(_wrap.get());
	_wrap->add(EditPeerInfoBox::CreateButton(
		_wrap.get(),
		tr::lng_group_invite_manage(),
		rpl::single(QString()),
		[=] { Ui::show(
			Box(ManageInviteLinksBox, _peer, _peer->session().user(), 0, 0),
			Ui::LayerOption::KeepOther);
		},
		st::manageGroupButton,
		&st::infoIconInviteLinks));
	AddSkip(_wrap.get());
	AddDividerText(_wrap.get(), tr::lng_group_invite_manage_about());

	if (_linkOnly) {
		_controls.inviteLinkWrap->show(anim::type::instant);
	} else {
		if (_controls.privacy->value() == Privacy::NoUsername) {
			checkUsernameAvailability();
		}
		const auto forShowing = _privacySavedValue.value_or(Privacy::NoUsername);
		_controls.inviteLinkWrap->toggle(
			(forShowing != Privacy::HasUsername),
			anim::type::instant);
		_controls.usernameWrap->toggle(
			(forShowing == Privacy::HasUsername),
			anim::type::instant);
	}
}

void Controller::addRoundButton(
		not_null<Ui::VerticalLayout*> container,
		Privacy value,
		const QString &text,
		rpl::producer<QString> about) {
	container->add(object_ptr<Ui::Radioenum<Privacy>>(
		container,
		_controls.privacy,
		value,
		text,
		st::editPeerPrivacyBoxCheckbox));
	container->add(object_ptr<Ui::PaddingWrap<Ui::FlatLabel>>(
		container,
		object_ptr<Ui::FlatLabel>(
			container,
			std::move(about),
			st::editPeerPrivacyLabel),
		st::editPeerPrivacyLabelMargins));
	container->add(object_ptr<Ui::FixedHeightWidget>(
			container,
			st::editPeerPrivacyBottomSkip));
};

void Controller::fillPrivaciesButtons(
		not_null<Ui::VerticalLayout*> parent,
		std::optional<Privacy> savedValue) {
	const auto canEditUsername = [&] {
		if (const auto chat = _peer->asChat()) {
			return chat->canEditUsername();
		} else if (const auto channel = _peer->asChannel()) {
			return channel->canEditUsername();
		}
		Unexpected("Peer type in Controller::createPrivaciesEdit.");
	}();
	if (!canEditUsername || _linkOnly) {
		return;
	}

	const auto result = parent->add(
			object_ptr<Ui::PaddingWrap<Ui::VerticalLayout>>(
				parent,
				object_ptr<Ui::VerticalLayout>(parent),
				st::editPeerPrivaciesMargins));
	const auto container = result->entity();

	const auto isPublic = _peer->isChannel()
		&& _peer->asChannel()->hasUsername();
	_controls.privacy = std::make_shared<Ui::RadioenumGroup<Privacy>>(
		savedValue.value_or(
			isPublic ? Privacy::HasUsername : Privacy::NoUsername));

	addRoundButton(
		container,
		Privacy::HasUsername,
		(_useLocationPhrases
			? tr::lng_create_permanent_link_title
			: _isGroup
			? tr::lng_create_public_group_title
			: tr::lng_create_public_channel_title)(tr::now),
		(_isGroup
			? tr::lng_create_public_group_about
			: tr::lng_create_public_channel_about)());
	addRoundButton(
		container,
		Privacy::NoUsername,
		(_useLocationPhrases
			? tr::lng_create_invite_link_title
			: _isGroup
			? tr::lng_create_private_group_title
			: tr::lng_create_private_channel_title)(tr::now),
		(_useLocationPhrases
			? tr::lng_create_invite_link_about
			: _isGroup
			? tr::lng_create_private_group_about
			: tr::lng_create_private_channel_about)());

	_controls.privacy->setChangedCallback([=](Privacy value) {
		privacyChanged(value);
	});
}

void Controller::setFocusUsername() {
	if (_controls.usernameInput) {
		_controls.usernameInput->setFocus();
	}
}

QString Controller::getUsernameInput() {
	return _controls.usernameInput->getLastText().trimmed();
}

QString Controller::inviteLinkText() {
	if (const auto channel = _peer->asChannel()) {
		return channel->inviteLink();
	} else if (const auto chat = _peer->asChat()) {
		return chat->inviteLink();
	}
	return QString();
}

object_ptr<Ui::RpWidget> Controller::createUsernameEdit() {
	Expects(_wrap != nullptr);

	const auto channel = _peer->asChannel();
	const auto username =
		_usernameSavedValue.value_or(channel ? channel->username : QString());

	auto result = object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
		_wrap,
		object_ptr<Ui::VerticalLayout>(_wrap));
	_controls.usernameWrap = result.data();

	const auto container = result->entity();

	using namespace Settings;
	AddSkip(container);
	container->add(
		object_ptr<Ui::FlatLabel>(
			container,
			tr::lng_create_group_link(),
			st::settingsSubsectionTitle),
		st::settingsSubsectionTitlePadding);

	const auto placeholder = container->add(
		object_ptr<Ui::RpWidget>(container),
		st::editPeerUsernameFieldMargins);
	placeholder->setAttribute(Qt::WA_TransparentForMouseEvents);
	_controls.usernameInput = Ui::AttachParentChild(
		container,
		object_ptr<Ui::UsernameInput>(
			container,
			st::setupChannelLink,
			nullptr,
			username,
			_peer->session().createInternalLink(QString())));
	_controls.usernameInput->heightValue(
	) | rpl::start_with_next([placeholder](int height) {
		placeholder->resize(placeholder->width(), height);
	}, placeholder->lifetime());
	placeholder->widthValue(
	) | rpl::start_with_next([this](int width) {
		_controls.usernameInput->resize(
			width,
			_controls.usernameInput->height());
	}, placeholder->lifetime());
	_controls.usernameInput->move(placeholder->pos());

	AddDividerText(
		container,
		tr::lng_create_channel_link_about());

	QObject::connect(
		_controls.usernameInput,
		&Ui::UsernameInput::changed,
		[this] { usernameChanged(); });

	const auto shown = (_controls.privacy->value() == Privacy::HasUsername);
	result->toggle(shown, anim::type::instant);

	return result;
}

void Controller::privacyChanged(Privacy value) {
	const auto toggleInviteLink = [&] {
		_controls.inviteLinkWrap->toggle(
			(value != Privacy::HasUsername),
			anim::type::instant);
	};
	const auto toggleEditUsername = [&] {
		_controls.usernameWrap->toggle(
			(value == Privacy::HasUsername),
			anim::type::instant);
	};
	const auto refreshVisibilities = [&] {
		// Now first we need to hide that was shown.
		// Otherwise box will change own Y position.

		if (value == Privacy::HasUsername) {
			toggleInviteLink();
			toggleEditUsername();

			_controls.usernameResult = nullptr;
			checkUsernameAvailability();
		} else {
			toggleEditUsername();
			toggleInviteLink();
		}
	};
	if (value == Privacy::HasUsername) {
		if (_usernameState == UsernameState::TooMany) {
			askUsernameRevoke();
			return;
		} else if (_usernameState == UsernameState::NotAvailable) {
			_controls.privacy->setValue(Privacy::NoUsername);
			return;
		}
		refreshVisibilities();
		_controls.usernameInput->setDisplayFocused(true);
	} else {
		_api.request(base::take(_checkUsernameRequestId)).cancel();
		_checkUsernameTimer.cancel();
		refreshVisibilities();
	}
	setFocusUsername();
}

void Controller::checkUsernameAvailability() {
	if (!_controls.usernameInput) {
		return;
	}
	const auto initial = (_controls.privacy->value() != Privacy::HasUsername);
	const auto checking = initial
		? qsl(".bad.")
		: getUsernameInput();
	if (checking.size() < kMinUsernameLength) {
		return;
	}
	if (_checkUsernameRequestId) {
		_api.request(_checkUsernameRequestId).cancel();
	}
	const auto channel = _peer->migrateToOrMe()->asChannel();
	const auto username = channel ? channel->username : QString();
	_checkUsernameRequestId = _api.request(MTPchannels_CheckUsername(
		channel ? channel->inputChannel : MTP_inputChannelEmpty(),
		MTP_string(checking)
	)).done([=](const MTPBool &result) {
		_checkUsernameRequestId = 0;
		if (initial) {
			return;
		}
		if (!mtpIsTrue(result) && checking != username) {
			showUsernameError(tr::lng_create_channel_link_occupied());
		} else {
			showUsernameGood();
		}
	}).fail([=](const MTP::Error &error) {
		_checkUsernameRequestId = 0;
		const auto &type = error.type();
		_usernameState = UsernameState::Normal;
		if (type == qstr("CHANNEL_PUBLIC_GROUP_NA")) {
			_usernameState = UsernameState::NotAvailable;
			_controls.privacy->setValue(Privacy::NoUsername);
		} else if (type == qstr("CHANNELS_ADMIN_PUBLIC_TOO_MUCH")) {
			_usernameState = UsernameState::TooMany;
			if (_controls.privacy->value() == Privacy::HasUsername) {
				askUsernameRevoke();
			}
		} else if (initial) {
			if (_controls.privacy->value() == Privacy::HasUsername) {
				_controls.usernameResult = nullptr;
				setFocusUsername();
			}
		} else if (type == qstr("USERNAME_INVALID")) {
			showUsernameError(tr::lng_create_channel_link_invalid());
		} else if (type == qstr("USERNAME_OCCUPIED")
			&& checking != username) {
			showUsernameError(tr::lng_create_channel_link_occupied());
		}
	}).send();
}

void Controller::askUsernameRevoke() {
	_controls.privacy->setValue(Privacy::NoUsername);
	const auto revokeCallback = crl::guard(this, [this] {
		_usernameState = UsernameState::Normal;
		_controls.privacy->setValue(Privacy::HasUsername);
		checkUsernameAvailability();
	});
	Ui::show(
		Box<RevokePublicLinkBox>(
			&_peer->session(),
			std::move(revokeCallback)),
		Ui::LayerOption::KeepOther);
}

void Controller::usernameChanged() {
	_isAllowSave = false;
	const auto username = getUsernameInput();
	if (username.isEmpty()) {
		_controls.usernameResult = nullptr;
		_checkUsernameTimer.cancel();
		return;
	}
	const auto bad = ranges::any_of(username, [](QChar ch) {
		return (ch < 'A' || ch > 'Z')
			&& (ch < 'a' || ch > 'z')
			&& (ch < '0' || ch > '9')
			&& (ch != '_');
	});
	if (bad) {
		showUsernameError(tr::lng_create_channel_link_bad_symbols());
	} else if (username.size() < kMinUsernameLength) {
		showUsernameError(tr::lng_create_channel_link_too_short());
	} else {
		_controls.usernameResult = nullptr;
		_checkUsernameTimer.callOnce(kUsernameCheckTimeout);
	}
}

void Controller::showUsernameError(rpl::producer<QString> &&error) {
	_isAllowSave = false;
	showUsernameResult(std::move(error), &st::editPeerUsernameError);
}

void Controller::showUsernameGood() {
	_isAllowSave = true;
	showUsernameResult(
		tr::lng_create_channel_link_available(),
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
		const auto label = _controls.usernameResult.get();
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

object_ptr<Ui::RpWidget> Controller::createInviteLinkBlock() {
	Expects(_wrap != nullptr);

	auto result = object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
		_wrap,
		object_ptr<Ui::VerticalLayout>(_wrap));
	_controls.inviteLinkWrap = result.data();

	const auto container = result->entity();

	using namespace Settings;
	if (_privacySavedValue) {
		AddSkip(container);

		AddSubsectionTitle(container, tr::lng_create_permanent_link_title());
	}
	AddPermanentLinkBlock(
		container,
		_peer,
		_peer->session().user(),
		nullptr);

	AddSkip(container);

	AddDividerText(
		container,
		((_peer->isMegagroup() || _peer->asChat())
			? tr::lng_group_invite_about_permanent_group()
			: tr::lng_group_invite_about_permanent_channel()));

	return result;
}

bool Controller::inviteLinkShown() {
	return !_controls.privacy
		|| (_controls.privacy->value() == Privacy::NoUsername);
}

} // namespace

EditPeerTypeBox::EditPeerTypeBox(
	QWidget*,
	not_null<PeerData*> peer,
	bool useLocationPhrases,
	std::optional<FnMut<void(Privacy, QString)>> savedCallback,
	std::optional<Privacy> privacySaved,
	std::optional<QString> usernameSaved,
	std::optional<rpl::producer<QString>> usernameError)
: _peer(peer)
, _useLocationPhrases(useLocationPhrases)
, _savedCallback(std::move(savedCallback))
, _privacySavedValue(privacySaved)
, _usernameSavedValue(usernameSaved)
, _usernameError(usernameError) {
}

EditPeerTypeBox::EditPeerTypeBox(
	QWidget*,
	not_null<PeerData*> peer)
: EditPeerTypeBox(nullptr, peer, {}, {}, {}, {}, {}) {
}

void EditPeerTypeBox::setInnerFocus() {
	_focusRequests.fire({});
}

void EditPeerTypeBox::prepare() {
	_peer->updateFull();

	auto content = object_ptr<Ui::VerticalLayout>(this);

	const auto controller = Ui::CreateChild<Controller>(
		this,
		content.data(),
		_peer,
		_useLocationPhrases,
		_privacySavedValue,
		_usernameSavedValue);
	_focusRequests.events(
	) | rpl::start_with_next(
		[=] {
			controller->setFocusUsername();
			if (_usernameError.has_value()) {
				controller->showError(std::move(*_usernameError));
				_usernameError = std::nullopt;
			}
		},
		lifetime());
	controller->createContent();

	setTitle(controller->getTitle());

	if (_savedCallback.has_value()) {
		addButton(tr::lng_settings_save(), [=] {
			const auto v = controller->getPrivacy();
			if (!controller->isAllowSave() && (v == Privacy::HasUsername)) {
				controller->setFocusUsername();
				return;
			}

			auto local = std::move(*_savedCallback);
			local(v,
				(v == Privacy::HasUsername)
					? controller->getUsernameInput()
					: QString()); // We don't need username with private type.
			closeBox();
		});
		addButton(tr::lng_cancel(), [=] { closeBox(); });
	} else {
		addButton(tr::lng_close(), [=] { closeBox(); });
	}

	setDimensionsToContent(st::boxWideWidth, content.data());
	setInnerWidget(std::move(content));
}
