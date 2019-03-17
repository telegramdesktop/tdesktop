/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/peers/edit_peer_type_box.h"

#include "apiwrap.h"
#include "apiwrap.h"
#include "apiwrap.h"
#include "auth_session.h"
#include "auth_session.h"
#include "auth_session.h"
#include "boxes/add_contact_box.h"
#include "boxes/add_contact_box.h"
#include "boxes/confirm_box.h"
#include "boxes/confirm_box.h"
#include "boxes/peer_list_controllers.h"
#include "boxes/peer_list_controllers.h"
#include "boxes/peers/edit_participants_box.h"
#include "boxes/peers/edit_participants_box.h"
#include "boxes/peers/edit_participants_box.h"
#include "boxes/peers/edit_peer_type_box.h"
#include "boxes/peers/edit_peer_history_visibility_box.h"
#include "boxes/peers/edit_peer_info_box.h"
#include "boxes/peers/edit_peer_permissions_box.h"
#include "boxes/peers/edit_peer_permissions_box.h"
#include "boxes/photo_crop_box.h"
#include "boxes/stickers_box.h"
#include "boxes/stickers_box.h"
#include "chat_helpers/emoji_suggestions_widget.h"
#include "chat_helpers/emoji_suggestions_widget.h"
#include "core/application.h"
#include "data/data_channel.h"
#include "data/data_channel.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_chat.h"
#include "data/data_chat.h"
#include "data/data_peer.h"
#include "data/data_peer.h"
#include "history/admin_log/history_admin_log_section.h"
#include "history/admin_log/history_admin_log_section.h"
#include "info/profile/info_profile_button.h"
#include "info/profile/info_profile_button.h"
#include "info/profile/info_profile_button.h"
#include "info/profile/info_profile_icon.h"
#include "info/profile/info_profile_values.h"
#include "info/profile/info_profile_values.h"
#include "lang/lang_keys.h"
#include "lang/lang_keys.h"
#include "lang/lang_keys.h"
#include "mainwidget.h"
#include "mainwidget.h"
#include "mainwindow.h"
#include "mainwindow.h"
#include "mtproto/sender.h"
#include "mtproto/sender.h"
#include "observer_peer.h"
#include "styles/style_boxes.h"
#include "styles/style_boxes.h"
#include "styles/style_boxes.h"
#include "styles/style_info.h"
#include "styles/style_info.h"
#include "styles/style_info.h"
#include "ui/rp_widget.h"
#include "ui/special_buttons.h"
#include "ui/special_buttons.h"
#include "ui/toast/toast.h"
#include "ui/toast/toast.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/input_fields.h"
#include "ui/widgets/input_fields.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_controller.h"
#include "window/window_controller.h"
#include <rpl/combine.h>
#include <rpl/flatten_latest.h>
#include <rpl/flatten_latest.h>
#include <rpl/range.h>
#include <rpl/range.h>


#include "styles/style_boxes.h"
#include "styles/style_dialogs.h"
#include "lang/lang_keys.h"
#include "mtproto/sender.h"
#include "base/flat_set.h"
#include "boxes/confirm_box.h"
#include "boxes/photo_crop_box.h"
#include "boxes/peer_list_controllers.h"
#include "boxes/peers/add_participants_box.h"
#include "boxes/peers/edit_participant_box.h"
#include "boxes/peers/edit_participants_box.h"
#include "core/file_utilities.h"
#include "core/application.h"
#include "chat_helpers/emoji_suggestions_widget.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/input_fields.h"
#include "ui/widgets/labels.h"
#include "ui/toast/toast.h"
#include "ui/special_buttons.h"
#include "ui/text_options.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_user.h"
#include "data/data_session.h"
#include "mainwidget.h"
#include "mainwindow.h"
#include "apiwrap.h"
#include "observer_peer.h"
#include "auth_session.h"

namespace {


std::optional<Privacy> privacySavedValue;
std::optional<QString> usernameSavedValue;

std::shared_ptr<Ui::RadioenumGroup<Privacy>> privacyButtons;
Ui::SlideWrap<Ui::RpWidget> *usernameWrap = nullptr;
Ui::UsernameInput *usernameInput = nullptr;
base::unique_qptr<Ui::FlatLabel> usernameResult;
const style::FlatLabel *usernameResultStyle = nullptr;

Ui::SlideWrap<Ui::RpWidget> *createInviteLinkWrap = nullptr;
// Ui::SlideWrap<Ui::RpWidget> *_editInviteLinkWrap = nullptr;
Ui::FlatLabel *inviteLink = nullptr;

PeerData *peer = nullptr;

bool allowSave = false;


mtpRequestId _checkUsernameRequestId = 0;
UsernameState _usernameState = UsernameState::Normal;
rpl::event_stream<rpl::producer<QString>> _usernameResultTexts;

bool isGroup = false;

void AddRoundButton(
	not_null<Ui::VerticalLayout*> container,
	Privacy value,
	LangKey groupTextKey,
	LangKey channelTextKey,
	LangKey groupAboutKey,
	LangKey channelAboutKey) {
	container->add(object_ptr<Ui::Radioenum<Privacy>>(
		container,
		privacyButtons,
		value,
		lang(isGroup ? groupTextKey : channelTextKey),
		st::defaultBoxCheckbox));
	container->add(object_ptr<Ui::PaddingWrap<Ui::FlatLabel>>(
		container,
		object_ptr<Ui::FlatLabel>(
			container,
			Lang::Viewer(isGroup ? groupAboutKey : channelAboutKey),
			st::editPeerPrivacyLabel),
		st::editPeerPrivacyLabelMargins));
	container->add(object_ptr<Ui::FixedHeightWidget>(
			container,
			st::editPeerPrivacyBottomSkip));
};

void FillGroupedButtons(
	not_null<Ui::VerticalLayout*> parent,
	not_null<PeerData*> peer,
	std::optional<Privacy> savedValue = std::nullopt) {

	const auto canEditUsername = [&] {
		if (const auto chat = peer->asChat()) {
			return chat->canEditUsername();
		} else if (const auto channel = peer->asChannel()) {
			return channel->canEditUsername();
		}
		Unexpected("Peer type in Controller::createPrivaciesEdit.");
	}();
	if (!canEditUsername) {
		return;
	}

	const auto result = parent->add(object_ptr<Ui::PaddingWrap<Ui::VerticalLayout>>(
		parent,
		object_ptr<Ui::VerticalLayout>(parent),
		st::editPeerPrivaciesMargins));
	auto container = result->entity();

	const auto isPublic = peer->isChannel()
		&& peer->asChannel()->isPublic();
	privacyButtons = std::make_shared<Ui::RadioenumGroup<Privacy>>(
		savedValue.value_or(isPublic ? Privacy::Public : Privacy::Private));

	AddRoundButton(
		container,
		Privacy::Public,
		lng_create_public_group_title,
		lng_create_public_channel_title,
		lng_create_public_group_about,
		lng_create_public_channel_about);
	AddRoundButton(
		container,
		Privacy::Private,
		lng_create_private_group_title,
		lng_create_private_channel_title,
		lng_create_private_group_about,
		lng_create_private_channel_about);

	// privacyButtons->setChangedCallback([this](Privacy value) {
	// 	privacyChanged(value);
	// });
	if (!isPublic) {
		// checkUsernameAvailability();
	}

	// return std::move(result);
}

void FillContent(
	not_null<Ui::VerticalLayout*> parent,
	not_null<PeerData*> peer,
	std::optional<Privacy> savedValue = std::nullopt) {

	FillGroupedButtons(parent, peer, savedValue);
}

void SetFocusUsername() {
	if (usernameInput) {
		usernameInput->setFocus();
	}
}

QString GetUsernameInput() {
	return usernameInput->getLastText().trimmed();
}

bool InviteLinkShown() {
	return !privacyButtons
		|| (privacyButtons->value() == Privacy::Private);
}

QString InviteLinkText() {
	if (const auto channel = peer->asChannel()) {
		return channel->inviteLink();
	} else if (const auto chat = peer->asChat()) {
		return chat->inviteLink();
	}
	return QString();
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
		not_null<Ui::VerticalLayout*> container,
		not_null<PeerData*> peer);

	void createContent();

private:

	object_ptr<Ui::RpWidget> createPrivaciesEdit();
	object_ptr<Ui::RpWidget> createUsernameEdit();
	object_ptr<Ui::RpWidget> createInviteLinkCreate();
	object_ptr<Ui::RpWidget> createInviteLinkEdit();

	void observeInviteLink();

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
	void refreshEditInviteLink();
	void refreshCreateInviteLink();
	void createInviteLink();
	void revokeInviteLink();
	void exportInviteLink(const QString &confirmation);

	void subscribeToMigration();
	void migrate(not_null<ChannelData*> channel);

	not_null<PeerData*> _peer;
	bool _isGroup = false;

	base::unique_qptr<Ui::VerticalLayout> _wrap;
	base::Timer _checkUsernameTimer;
	mtpRequestId _checkUsernameRequestId = 0;
	UsernameState _usernameState = UsernameState::Normal;
	rpl::event_stream<rpl::producer<QString>> _usernameResultTexts;

	Ui::SlideWrap<Ui::RpWidget> *_editInviteLinkWrap = nullptr;

	rpl::lifetime _lifetime;

};

Controller::Controller(
	not_null<Ui::VerticalLayout*> container,
	not_null<PeerData*> peer)
: _peer(peer)
, _isGroup(_peer->isChat() || _peer->isMegagroup())
, _wrap(container)
, _checkUsernameTimer([=] { checkUsernameAvailability(); }) {
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

void Controller::createContent() {
	privacyButtons->setChangedCallback([this](Privacy value) {
		privacyChanged(value);
	});

	// _wrap->add(createPrivaciesEdit());
	_wrap->add(createInviteLinkCreate());
	_wrap->add(createInviteLinkEdit());
	_wrap->add(createUsernameEdit());

	if (privacyButtons->value() == Privacy::Private) {
		checkUsernameAvailability();
	}
}

object_ptr<Ui::RpWidget> Controller::createUsernameEdit() {
	Expects(_wrap != nullptr);

	const auto channel = _peer->asChannel();
	const auto username = usernameSavedValue.value_or(channel ? channel->username : QString());

	auto result = object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
		_wrap,
		object_ptr<Ui::VerticalLayout>(_wrap),
		st::editPeerUsernameMargins);
	usernameWrap = result.data();

	auto container = result->entity();
	container->add(object_ptr<Ui::FlatLabel>(
		container,
		Lang::Viewer(lng_create_group_link),
		st::editPeerSectionLabel));
	auto placeholder = container->add(object_ptr<Ui::RpWidget>(
		container));
	placeholder->setAttribute(Qt::WA_TransparentForMouseEvents);
	usernameInput = Ui::AttachParentChild(
		container,
		object_ptr<Ui::UsernameInput>(
			container,
			st::setupChannelLink,
			Fn<QString()>(),
			username,
			true));
	usernameInput->heightValue(
	) | rpl::start_with_next([placeholder](int height) {
		placeholder->resize(placeholder->width(), height);
	}, placeholder->lifetime());
	placeholder->widthValue(
	) | rpl::start_with_next([this](int width) {
		usernameInput->resize(
			width,
			usernameInput->height());
	}, placeholder->lifetime());
	usernameInput->move(placeholder->pos());

	QObject::connect(
		usernameInput,
		&Ui::UsernameInput::changed,
		[this] { usernameChanged(); });

	auto shown = (privacyButtons->value() == Privacy::Public);
	result->toggle(shown, anim::type::instant);

	return std::move(result);
}

void Controller::privacyChanged(Privacy value) {
	auto toggleEditUsername = [&] {
		usernameWrap->toggle(
			(value == Privacy::Public),
			anim::type::instant);
	};
	auto refreshVisibilities = [&] {
		// Now first we need to hide that was shown.
		// Otherwise box will change own Y position.

		if (value == Privacy::Public) {
			refreshCreateInviteLink();
			refreshEditInviteLink();
			toggleEditUsername();

			usernameResult = nullptr;
			checkUsernameAvailability();
		} else {
			toggleEditUsername();
			refreshCreateInviteLink();
			refreshEditInviteLink();
		}
	};
	if (value == Privacy::Public) {
		if (_usernameState == UsernameState::TooMany) {
			askUsernameRevoke();
			return;
		} else if (_usernameState == UsernameState::NotAvailable) {
			privacyButtons->setValue(Privacy::Private);
			return;
		}
		refreshVisibilities();
		usernameInput->setDisplayFocused(true);
		SetFocusUsername();
		// _box->scrollToWidget(usernameInput);
	} else {
		request(base::take(_checkUsernameRequestId)).cancel();
		_checkUsernameTimer.cancel();
		refreshVisibilities();
		SetFocusUsername();
	}
}

void Controller::checkUsernameAvailability() {
	if (!usernameInput) {
		return;
	}
	auto initial = (privacyButtons->value() != Privacy::Public);
	auto checking = initial
		? qsl(".bad.")
		: GetUsernameInput();
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
			privacyButtons->setValue(Privacy::Private);
		} else if (type == qstr("CHANNELS_ADMIN_PUBLIC_TOO_MUCH")) {
			_usernameState = UsernameState::TooMany;
			if (privacyButtons->value() == Privacy::Public) {
				askUsernameRevoke();
			}
		} else if (initial) {
			if (privacyButtons->value() == Privacy::Public) {
				usernameResult = nullptr;
				SetFocusUsername();
				// _box->scrollToWidget(usernameInput);
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
	privacyButtons->setValue(Privacy::Private);
	auto revokeCallback = crl::guard(this, [this] {
		_usernameState = UsernameState::Normal;
		privacyButtons->setValue(Privacy::Public);
		checkUsernameAvailability();
	});
	Ui::show(
		Box<RevokePublicLinkBox>(std::move(revokeCallback)),
		LayerOption::KeepOther);
}

void Controller::usernameChanged() {
	allowSave = false;
	auto username = GetUsernameInput();
	if (username.isEmpty()) {
		usernameResult = nullptr;
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
		usernameResult = nullptr;
		_checkUsernameTimer.callOnce(kUsernameCheckTimeout);
	}
}

void Controller::showUsernameError(rpl::producer<QString> &&error) {
	showUsernameResult(std::move(error), &st::editPeerUsernameError);
}

void Controller::showUsernameGood() {
	allowSave = true;
	showUsernameResult(
		Lang::Viewer(lng_create_channel_link_available),
		&st::editPeerUsernameGood);
}

void Controller::showUsernameResult(
		rpl::producer<QString> &&text,
		not_null<const style::FlatLabel*> st) {
	if (!usernameResult
		|| usernameResultStyle != st) {
		usernameResultStyle = st;
		usernameResult = base::make_unique_q<Ui::FlatLabel>(
			usernameWrap,
			_usernameResultTexts.events() | rpl::flatten_latest(),
			*st);
		auto label = usernameResult.get();
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

void Controller::observeInviteLink() {
	if (!_editInviteLinkWrap) {
		return;
	}
	// return; //
	Notify::PeerUpdateValue(
		_peer,
		Notify::PeerUpdate::Flag::InviteLinkChanged
	) | rpl::start_with_next([=] {
		refreshCreateInviteLink();
		refreshEditInviteLink();
	}, _editInviteLinkWrap->lifetime());
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
	_editInviteLinkWrap = result.data();

	auto container = result->entity();
	container->add(object_ptr<Ui::FlatLabel>(
		container,
		Lang::Viewer(lng_profile_invite_link_section),
		st::editPeerSectionLabel));
	container->add(object_ptr<Ui::FixedHeightWidget>(
		container,
		st::editPeerInviteLinkSkip));

	inviteLink = container->add(object_ptr<Ui::FlatLabel>(
		container,
		st::editPeerInviteLink));
	inviteLink->setSelectable(true);
	inviteLink->setContextCopyText(QString());
	inviteLink->setBreakEverywhere(true);
	inviteLink->setClickHandlerFilter([=](auto&&...) {
		QApplication::clipboard()->setText(InviteLinkText());
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
	auto link = InviteLinkText();
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
	inviteLink->setMarkedText(text);

	// Hack to expand FlatLabel width to naturalWidth again.
	_editInviteLinkWrap->resizeToWidth(st::boxWideWidth);

	_editInviteLinkWrap->toggle(
		InviteLinkShown() && !link.isEmpty(),
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
	createInviteLinkWrap = result.data();

	observeInviteLink();

	return std::move(result);
}

void Controller::refreshCreateInviteLink() {
	createInviteLinkWrap->toggle(
		InviteLinkShown() && InviteLinkText().isEmpty(),
		anim::type::instant);
}

} // namespace

EditPeerTypeBox::EditPeerTypeBox(
		QWidget*,
		not_null<PeerData*> p,
		FnMut<void(Privacy, QString)> savedCallback,
		std::optional<Privacy> privacySaved,
		std::optional<QString> usernameSaved)
: _peer(p)
, _savedCallback(std::move(savedCallback)) {
	peer = p;
	privacySavedValue = privacySaved;
	usernameSavedValue = usernameSaved;
	allowSave = !usernameSaved->isEmpty() && usernameSaved.has_value();
}

void EditPeerTypeBox::prepare() {
	_peer->updateFull();

	setTitle(langFactory((peer->isChat() || peer->isMegagroup())
		? lng_manage_peer_group_type
		: lng_manage_peer_channel_type));

	addButton(langFactory(lng_settings_save), [=] {
		const auto v = privacyButtons->value();
		if (!allowSave && (v == Privacy::Public)) {
			SetFocusUsername();
			return;
		}

		auto local = std::move(_savedCallback);
		local(v,
			(v == Privacy::Public)
				? GetUsernameInput()
				: QString()); // We dont need username with private type.
		closeBox();
	});
	addButton(langFactory(lng_cancel), [=] { closeBox(); });

	setupContent();
}

void EditPeerTypeBox::setupContent() {
	isGroup = (_peer->isChat() || _peer->isMegagroup());

	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	FillContent(content, _peer, privacySavedValue);

	auto controller = Ui::CreateChild<Controller>(this, content, _peer);
	_focusRequests.events(
	) | rpl::start_with_next(
		[=] { SetFocusUsername(); },
		lifetime());
	controller->createContent();
	// setDimensionsToContent(st::boxWidth, content);
	setDimensionsToContent(st::boxWideWidth, content);
}
