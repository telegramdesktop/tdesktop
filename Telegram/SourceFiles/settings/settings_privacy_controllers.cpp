/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/settings_privacy_controllers.h"

#include "settings/settings_common.h"
#include "lang/lang_keys.h"
#include "apiwrap.h"
#include "observer_peer.h"
#include "mainwidget.h"
#include "main/main_session.h"
#include "storage/localstorage.h"
#include "data/data_user.h"
#include "data/data_session.h"
#include "history/admin_log/history_admin_log_item.h"
#include "history/view/history_view_element.h"
#include "history/view/history_view_message.h"
#include "history/history_item_components.h"
#include "history/history_message.h"
#include "history/history.h"
#include "calls/calls_instance.h"
#include "base/unixtime.h"
#include "ui/widgets/checkbox.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/image/image_prepare.h"
#include "window/section_widget.h"
#include "window/window_session_controller.h"
#include "boxes/peer_list_controllers.h"
#include "boxes/confirm_box.h"
#include "settings/settings_privacy_security.h"
#include "styles/style_history.h"
#include "styles/style_boxes.h"
#include "styles/style_settings.h"

namespace Settings {
namespace {

constexpr auto kBlockedPerPage = 40;

class BlockUserBoxController
	: public ChatsListBoxController
	, private base::Subscriber {
public:
	explicit BlockUserBoxController(
		not_null<Window::SessionNavigation*> navigation);

	Main::Session &session() const override;
	void rowClicked(not_null<PeerListRow*> row) override;

	void setBlockUserCallback(Fn<void(not_null<UserData*> user)> callback) {
		_blockUserCallback = std::move(callback);
	}

protected:
	void prepareViewHook() override;
	std::unique_ptr<Row> createRow(not_null<History*> history) override;
	void updateRowHook(not_null<Row*> row) override {
		updateIsBlocked(row, row->peer()->asUser());
		delegate()->peerListUpdateRow(row);
	}

private:
	void updateIsBlocked(not_null<PeerListRow*> row, UserData *user) const;

	const not_null<Window::SessionNavigation*> _navigation;
	Fn<void(not_null<UserData*> user)> _blockUserCallback;

};

BlockUserBoxController::BlockUserBoxController(
	not_null<Window::SessionNavigation*> navigation)
: ChatsListBoxController(navigation)
, _navigation(navigation) {
}

Main::Session &BlockUserBoxController::session() const {
	return _navigation->session();
}

void BlockUserBoxController::prepareViewHook() {
	delegate()->peerListSetTitle(tr::lng_blocked_list_add_title());
	subscribe(Notify::PeerUpdated(), Notify::PeerUpdatedHandler(Notify::PeerUpdate::Flag::UserIsBlocked, [this](const Notify::PeerUpdate &update) {
		if (auto user = update.peer->asUser()) {
			if (auto row = delegate()->peerListFindRow(user->id)) {
				updateIsBlocked(row, user);
				delegate()->peerListUpdateRow(row);
			}
		}
	}));
}

void BlockUserBoxController::updateIsBlocked(not_null<PeerListRow*> row, UserData *user) const {
	auto blocked = user->isBlocked();
	row->setDisabledState(blocked ? PeerListRow::State::DisabledChecked : PeerListRow::State::Active);
	if (blocked) {
		row->setCustomStatus(tr::lng_blocked_list_already_blocked(tr::now));
	} else {
		row->clearCustomStatus();
	}
}

void BlockUserBoxController::rowClicked(not_null<PeerListRow*> row) {
	_blockUserCallback(row->peer()->asUser());
}

std::unique_ptr<BlockUserBoxController::Row> BlockUserBoxController::createRow(not_null<History*> history) {
	if (history->peer->isSelf()) {
		return nullptr;
	}
	if (auto user = history->peer->asUser()) {
		auto row = std::make_unique<Row>(history);
		updateIsBlocked(row.get(), user);
		return row;
	}
	return nullptr;
}

AdminLog::OwnedItem GenerateForwardedItem(
		not_null<HistoryView::ElementDelegate*> delegate,
		not_null<History*> history,
		const QString &text) {
	Expects(history->peer->isUser());

	using Flag = MTPDmessage::Flag;
	using FwdFlag = MTPDmessageFwdHeader::Flag;
	// #TODO common global incrementable id for fake items, like clientMsgId.
	static auto id = ServerMaxMsgId + (ServerMaxMsgId / 6);
	const auto flags = Flag::f_from_id | Flag::f_fwd_from;
	const auto replyTo = 0;
	const auto viaBotId = 0;
	const auto item = MTP_message(
		MTP_flags(flags),
		MTP_int(++id),
		MTP_int(peerToUser(history->peer->id)),
		peerToMTP(history->session().userPeerId()),
		MTP_messageFwdHeader(
			MTP_flags(FwdFlag::f_from_id),
			MTP_int(history->session().userId()),
			MTPstring(), // from_name
			MTP_int(base::unixtime::now()),
			MTPint(), // channel_id
			MTPint(), // channel_post
			MTPstring(), // post_author
			MTPPeer(), // saved_from_peer
			MTPint()), // saved_from_msg_id
		MTPint(), // via_bot_id
		MTPint(), // reply_to_msg_id,
		MTP_int(base::unixtime::now()), // date
		MTP_string(text),
		MTPMessageMedia(),
		MTPReplyMarkup(),
		MTPVector<MTPMessageEntity>(),
		MTPint(), // views
		MTPint(), // edit_date
		MTPstring(), // post_author
		MTPlong(), // grouped_id
		//MTPMessageReactions(),
		MTPVector<MTPRestrictionReason>()
	).match([&](const MTPDmessage &data) {
		return history->owner().makeMessage(
			history,
			data,
			MTPDmessage_ClientFlag::f_fake_history_item);
	}, [](auto &&) -> not_null<HistoryMessage*> {
		Unexpected("Type in GenerateForwardedItem.");
	});

	return AdminLog::OwnedItem(delegate, item);
}

} // namespace

BlockedBoxController::BlockedBoxController(
	not_null<Window::SessionController*> window)
: _window(window) {
}

Main::Session &BlockedBoxController::session() const {
	return _window->session();
}

void BlockedBoxController::prepare() {
	delegate()->peerListSetTitle(tr::lng_blocked_list_title());
	setDescriptionText(tr::lng_contacts_loading(tr::now));
	delegate()->peerListRefreshRows();

	subscribe(Notify::PeerUpdated(), Notify::PeerUpdatedHandler(Notify::PeerUpdate::Flag::UserIsBlocked, [this](const Notify::PeerUpdate &update) {
		if (const auto user = update.peer->asUser()) {
			handleBlockedEvent(user);
		}
	}));

	_loadRequestId = -1;
	_window->session().api().blockedUsersSlice(
	) | rpl::take(
		1
	) | rpl::start_with_next([=](const ApiWrap::BlockedUsersSlice &result) {
		setDescriptionText(tr::lng_blocked_list_about(tr::now));
		_loadRequestId = 0;
		_offset = result.list.size();
		_allLoaded = (_offset >= result.total);
		for (const auto item : result.list) {
			appendRow(item.user);
		};
		delegate()->peerListRefreshRows();
		loadMoreRows();
	}, lifetime());
}

void BlockedBoxController::loadMoreRows() {
	if (_loadRequestId || _allLoaded) {
		return;
	}

	_loadRequestId = request(MTPcontacts_GetBlocked(
		MTP_int(_offset),
		MTP_int(kBlockedPerPage)
	)).done([=](const MTPcontacts_Blocked &result) {
		_loadRequestId = 0;

		auto handleContactsBlocked = [&](auto &list) {
			_window->session().data().processUsers(list.vusers());
			return list.vblocked().v;
		};
		switch (result.type()) {
		case mtpc_contacts_blockedSlice: {
			receivedUsers(handleContactsBlocked(result.c_contacts_blockedSlice()));
		} break;
		case mtpc_contacts_blocked: {
			_allLoaded = true;
			receivedUsers(handleContactsBlocked(result.c_contacts_blocked()));
		} break;
		default: Unexpected("Bad type() in MTPcontacts_GetBlocked() result.");
		}
	}).fail([this](const RPCError &error) {
		_loadRequestId = 0;
	}).send();
}

void BlockedBoxController::rowClicked(not_null<PeerListRow*> row) {
	InvokeQueued(App::main(), [peerId = row->peer()->id] {
		Ui::showPeerHistory(peerId, ShowAtUnreadMsgId);
	});
}

void BlockedBoxController::rowActionClicked(not_null<PeerListRow*> row) {
	auto user = row->peer()->asUser();
	Expects(user != nullptr);

	_window->session().api().unblockUser(user);
}

void BlockedBoxController::receivedUsers(const QVector<MTPContactBlocked> &result) {
	if (result.empty()) {
		_allLoaded = true;
	}

	_offset += result.size();
	for (const auto &item : result) {
		item.match([&](const MTPDcontactBlocked &data) {
			if (const auto user = _window->session().data().userLoaded(data.vuser_id().v)) {
				appendRow(user);
				user->setIsBlocked(true);
			}
		});
	}
	delegate()->peerListRefreshRows();
}

void BlockedBoxController::handleBlockedEvent(not_null<UserData*> user) {
	if (user->isBlocked()) {
		if (prependRow(user)) {
			delegate()->peerListRefreshRows();
			delegate()->peerListScrollToTop();
		}
	} else if (auto row = delegate()->peerListFindRow(user->id)) {
		delegate()->peerListRemoveRow(row);
		delegate()->peerListRefreshRows();
	}
}

void BlockedBoxController::BlockNewUser(
		not_null<Window::SessionController*> window) {
	auto controller = std::make_unique<BlockUserBoxController>(window);
	auto initBox = [=, controller = controller.get()](
			not_null<PeerListBox*> box) {
		controller->setBlockUserCallback([=](not_null<UserData*> user) {
			window->session().api().blockUser(user);
			box->closeBox();
		});
		box->addButton(tr::lng_cancel(), [box] { box->closeBox(); });
	};
	Ui::show(
		Box<PeerListBox>(std::move(controller), std::move(initBox)),
		LayerOption::KeepOther);
}

bool BlockedBoxController::appendRow(not_null<UserData*> user) {
	if (delegate()->peerListFindRow(user->id)) {
		return false;
	}
	delegate()->peerListAppendRow(createRow(user));
	return true;
}

bool BlockedBoxController::prependRow(not_null<UserData*> user) {
	if (delegate()->peerListFindRow(user->id)) {
		return false;
	}
	delegate()->peerListPrependRow(createRow(user));
	return true;
}

std::unique_ptr<PeerListRow> BlockedBoxController::createRow(
		not_null<UserData*> user) const {
	auto row = std::make_unique<PeerListRowWithLink>(user);
	row->setActionLink(tr::lng_blocked_list_unblock(tr::now));
	const auto status = [&] {
		if (!user->phone().isEmpty()) {
			return App::formatPhone(user->phone());
		} else if (!user->username.isEmpty()) {
			return '@' + user->username;
		} else if (user->isBot()) {
			return tr::lng_status_bot(tr::now);
		}
		return tr::lng_blocked_list_unknown_phone(tr::now);
	}();
	row->setCustomStatus(status);
	return std::move(row);
}

ApiWrap::Privacy::Key PhoneNumberPrivacyController::key() {
	return Key::PhoneNumber;
}

MTPInputPrivacyKey PhoneNumberPrivacyController::apiKey() {
	return MTP_inputPrivacyKeyPhoneNumber();
}

rpl::producer<QString> PhoneNumberPrivacyController::title() {
	return tr::lng_edit_privacy_phone_number_title();
}

rpl::producer<QString> PhoneNumberPrivacyController::optionsTitleKey() {
	return tr::lng_edit_privacy_phone_number_header();
}

rpl::producer<QString> PhoneNumberPrivacyController::warning() {
	using namespace rpl::mappers;
	return rpl::combine(
		_phoneNumberOption.value(),
		_addedByPhone.value(),
		(_1 == Option::Nobody) && (_2 != Option::Everyone)
	) | rpl::map([](bool onlyContactsSee) {
		return onlyContactsSee
			? tr::lng_edit_privacy_phone_number_contacts()
			: tr::lng_edit_privacy_phone_number_warning();
	}) | rpl::flatten_latest();
}

rpl::producer<QString> PhoneNumberPrivacyController::exceptionButtonTextKey(
		Exception exception) {
	switch (exception) {
	case Exception::Always:
		return tr::lng_edit_privacy_phone_number_always_empty();
	case Exception::Never:
		return tr::lng_edit_privacy_phone_number_never_empty();
	}
	Unexpected("Invalid exception value.");
}

rpl::producer<QString> PhoneNumberPrivacyController::exceptionBoxTitle(
		Exception exception) {
	switch (exception) {
	case Exception::Always: return tr::lng_edit_privacy_phone_number_always_title();
	case Exception::Never: return tr::lng_edit_privacy_phone_number_never_title();
	}
	Unexpected("Invalid exception value.");
}

rpl::producer<QString> PhoneNumberPrivacyController::exceptionsDescription() {
	return tr::lng_edit_privacy_phone_number_exceptions();
}

object_ptr<Ui::RpWidget> PhoneNumberPrivacyController::setupMiddleWidget(
		not_null<Window::SessionController*> controller,
		not_null<QWidget*> parent,
		rpl::producer<Option> optionValue) {
	const auto key = ApiWrap::Privacy::Key::AddedByPhone;
	controller->session().api().reloadPrivacy(key);

	_phoneNumberOption = std::move(optionValue);

	auto widget = object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
		parent,
		object_ptr<Ui::VerticalLayout>(parent));

	const auto container = widget->entity();
	AddDivider(container);
	AddSkip(container);
	AddSubsectionTitle(container, tr::lng_edit_privacy_phone_number_find());
	const auto group = std::make_shared<Ui::RadioenumGroup<Option>>();
	group->setChangedCallback([=](Option value) {
		_addedByPhone = value;
	});
	controller->session().api().privacyValue(
		key
	) | rpl::take(
		1
	) | rpl::start_with_next([=](const ApiWrap::Privacy &value) {
		group->setValue(value.option);
	}, widget->lifetime());

	const auto addOption = [&](Option option) {
		return EditPrivacyBox::AddOption(container, this, group, option);
	};
	addOption(Option::Everyone);
	addOption(Option::Contacts);
	AddSkip(container);

	using namespace rpl::mappers;
	widget->toggleOn(_phoneNumberOption.value(
	) | rpl::map(
		_1 == Option::Nobody
	));

	_saveAdditional = [=] {
		const auto value = [&] {
			switch (group->value()) {
			case Option::Everyone: return MTP_inputPrivacyValueAllowAll();
			default: return MTP_inputPrivacyValueAllowContacts();
			}
		}();
		controller->session().api().savePrivacy(
			MTP_inputPrivacyKeyAddedByPhone(),
			QVector<MTPInputPrivacyRule>(1, value));
	};

	return std::move(widget);
}

void PhoneNumberPrivacyController::saveAdditional() {
	if (_saveAdditional) {
		_saveAdditional();
	}
}

LastSeenPrivacyController::LastSeenPrivacyController(
	not_null<::Main::Session*> session)
: _session(session) {
}

ApiWrap::Privacy::Key LastSeenPrivacyController::key() {
	return Key::LastSeen;
}

MTPInputPrivacyKey LastSeenPrivacyController::apiKey() {
	return MTP_inputPrivacyKeyStatusTimestamp();
}

rpl::producer<QString> LastSeenPrivacyController::title() {
	return tr::lng_edit_privacy_lastseen_title();
}

rpl::producer<QString> LastSeenPrivacyController::optionsTitleKey() {
	return tr::lng_edit_privacy_lastseen_header();
}

rpl::producer<QString> LastSeenPrivacyController::warning() {
	return tr::lng_edit_privacy_lastseen_warning();
}

rpl::producer<QString> LastSeenPrivacyController::exceptionButtonTextKey(
		Exception exception) {
	switch (exception) {
	case Exception::Always:
		return tr::lng_edit_privacy_lastseen_always_empty();
	case Exception::Never:
		return tr::lng_edit_privacy_lastseen_never_empty();
	}
	Unexpected("Invalid exception value.");
}

rpl::producer<QString> LastSeenPrivacyController::exceptionBoxTitle(
		Exception exception) {
	switch (exception) {
	case Exception::Always: return tr::lng_edit_privacy_lastseen_always_title();
	case Exception::Never: return tr::lng_edit_privacy_lastseen_never_title();
	}
	Unexpected("Invalid exception value.");
}

rpl::producer<QString> LastSeenPrivacyController::exceptionsDescription() {
	return tr::lng_edit_privacy_lastseen_exceptions();
}

void LastSeenPrivacyController::confirmSave(
		bool someAreDisallowed,
		FnMut<void()> saveCallback) {
	if (someAreDisallowed && !_session->settings().lastSeenWarningSeen()) {
		const auto session = _session;
		auto weakBox = std::make_shared<QPointer<ConfirmBox>>();
		auto callback = [=, saveCallback = std::move(saveCallback)]() mutable {
			if (auto box = *weakBox) {
				box->closeBox();
			}
			saveCallback();
			session->settings().setLastSeenWarningSeen(true);
			Local::writeUserSettings();
		};
		auto box = Box<ConfirmBox>(
			tr::lng_edit_privacy_lastseen_warning(tr::now),
			tr::lng_continue(tr::now),
			tr::lng_cancel(tr::now),
			std::move(callback));
		*weakBox = Ui::show(std::move(box), LayerOption::KeepOther);
	} else {
		saveCallback();
	}
}

ApiWrap::Privacy::Key GroupsInvitePrivacyController::key() {
	return Key::Invites;
}

MTPInputPrivacyKey GroupsInvitePrivacyController::apiKey() {
	return MTP_inputPrivacyKeyChatInvite();
}

rpl::producer<QString> GroupsInvitePrivacyController::title() {
	return tr::lng_edit_privacy_groups_title();
}

bool GroupsInvitePrivacyController::hasOption(Option option) {
	return (option != Option::Nobody);
}

rpl::producer<QString> GroupsInvitePrivacyController::optionsTitleKey() {
	return tr::lng_edit_privacy_groups_header();
}

rpl::producer<QString> GroupsInvitePrivacyController::exceptionButtonTextKey(
		Exception exception) {
	switch (exception) {
	case Exception::Always: return tr::lng_edit_privacy_groups_always_empty();
	case Exception::Never: return tr::lng_edit_privacy_groups_never_empty();
	}
	Unexpected("Invalid exception value.");
}

rpl::producer<QString> GroupsInvitePrivacyController::exceptionBoxTitle(
		Exception exception) {
	switch (exception) {
	case Exception::Always: return tr::lng_edit_privacy_groups_always_title();
	case Exception::Never: return tr::lng_edit_privacy_groups_never_title();
	}
	Unexpected("Invalid exception value.");
}

auto GroupsInvitePrivacyController::exceptionsDescription()
-> rpl::producer<QString> {
	return tr::lng_edit_privacy_groups_exceptions();
}

ApiWrap::Privacy::Key CallsPrivacyController::key() {
	return Key::Calls;
}

MTPInputPrivacyKey CallsPrivacyController::apiKey() {
	return MTP_inputPrivacyKeyPhoneCall();
}

rpl::producer<QString> CallsPrivacyController::title() {
	return tr::lng_edit_privacy_calls_title();
}

rpl::producer<QString> CallsPrivacyController::optionsTitleKey() {
	return tr::lng_edit_privacy_calls_header();
}

rpl::producer<QString> CallsPrivacyController::exceptionButtonTextKey(
		Exception exception) {
	switch (exception) {
	case Exception::Always: return tr::lng_edit_privacy_calls_always_empty();
	case Exception::Never: return tr::lng_edit_privacy_calls_never_empty();
	}
	Unexpected("Invalid exception value.");
}

rpl::producer<QString> CallsPrivacyController::exceptionBoxTitle(
		Exception exception) {
	switch (exception) {
	case Exception::Always: return tr::lng_edit_privacy_calls_always_title();
	case Exception::Never: return tr::lng_edit_privacy_calls_never_title();
	}
	Unexpected("Invalid exception value.");
}

rpl::producer<QString> CallsPrivacyController::exceptionsDescription() {
	return tr::lng_edit_privacy_calls_exceptions();
}

object_ptr<Ui::RpWidget> CallsPrivacyController::setupBelowWidget(
		not_null<Window::SessionController*> controller,
		not_null<QWidget*> parent) {
	auto result = object_ptr<Ui::VerticalLayout>(parent);
	const auto content = result.data();

	AddDivider(content);
	AddSkip(content);
	AddSubsectionTitle(content, tr::lng_settings_calls_peer_to_peer_title());
	Settings::AddPrivacyButton(
		controller,
		content,
		tr::lng_settings_calls_peer_to_peer_button(),
		ApiWrap::Privacy::Key::CallsPeer2Peer,
		[] { return std::make_unique<CallsPeer2PeerPrivacyController>(); });
	AddSkip(content);

	return result;
}

ApiWrap::Privacy::Key CallsPeer2PeerPrivacyController::key() {
	return Key::CallsPeer2Peer;
}

MTPInputPrivacyKey CallsPeer2PeerPrivacyController::apiKey() {
	return MTP_inputPrivacyKeyPhoneP2P();
}

rpl::producer<QString> CallsPeer2PeerPrivacyController::title() {
	return tr::lng_edit_privacy_calls_p2p_title();
}

rpl::producer<QString> CallsPeer2PeerPrivacyController::optionsTitleKey() {
	return tr::lng_edit_privacy_calls_p2p_header();
}

QString CallsPeer2PeerPrivacyController::optionLabel(
		EditPrivacyBox::Option option) {
	switch (option) {
	case Option::Everyone: return tr::lng_edit_privacy_calls_p2p_everyone(tr::now);
	case Option::Contacts: return tr::lng_edit_privacy_calls_p2p_contacts(tr::now);
	case Option::Nobody: return tr::lng_edit_privacy_calls_p2p_nobody(tr::now);
	}
	Unexpected("Option value in optionsLabelKey.");
}

rpl::producer<QString> CallsPeer2PeerPrivacyController::warning() {
	return tr::lng_settings_peer_to_peer_about();
}

rpl::producer<QString> CallsPeer2PeerPrivacyController::exceptionButtonTextKey(
		Exception exception) {
	switch (exception) {
	case Exception::Always: return tr::lng_edit_privacy_calls_p2p_always_empty();
	case Exception::Never: return tr::lng_edit_privacy_calls_p2p_never_empty();
	}
	Unexpected("Invalid exception value.");
}

rpl::producer<QString> CallsPeer2PeerPrivacyController::exceptionBoxTitle(
		Exception exception) {
	switch (exception) {
	case Exception::Always: return tr::lng_edit_privacy_calls_p2p_always_title();
	case Exception::Never: return tr::lng_edit_privacy_calls_p2p_never_title();
	}
	Unexpected("Invalid exception value.");
}

rpl::producer<QString> CallsPeer2PeerPrivacyController::exceptionsDescription() {
	return tr::lng_edit_privacy_calls_p2p_exceptions();
}

ForwardsPrivacyController::ForwardsPrivacyController(
	not_null<::Main::Session*> session)
: _session(session) {
}

ApiWrap::Privacy::Key ForwardsPrivacyController::key() {
	return Key::Forwards;
}

MTPInputPrivacyKey ForwardsPrivacyController::apiKey() {
	return MTP_inputPrivacyKeyForwards();
}

rpl::producer<QString> ForwardsPrivacyController::title() {
	return tr::lng_edit_privacy_forwards_title();
}

rpl::producer<QString> ForwardsPrivacyController::optionsTitleKey() {
	return tr::lng_edit_privacy_forwards_header();
}

rpl::producer<QString> ForwardsPrivacyController::warning() {
	return tr::lng_edit_privacy_forwards_warning();
}

rpl::producer<QString> ForwardsPrivacyController::exceptionButtonTextKey(
		Exception exception) {
	switch (exception) {
	case Exception::Always: return tr::lng_edit_privacy_forwards_always_empty();
	case Exception::Never: return tr::lng_edit_privacy_forwards_never_empty();
	}
	Unexpected("Invalid exception value.");
}

rpl::producer<QString> ForwardsPrivacyController::exceptionBoxTitle(
		Exception exception) {
	switch (exception) {
	case Exception::Always: return tr::lng_edit_privacy_forwards_always_title();
	case Exception::Never: return tr::lng_edit_privacy_forwards_never_title();
	}
	Unexpected("Invalid exception value.");
}

auto ForwardsPrivacyController::exceptionsDescription()
-> rpl::producer<QString> {
	return tr::lng_edit_privacy_forwards_exceptions();
}

object_ptr<Ui::RpWidget> ForwardsPrivacyController::setupAboveWidget(
		not_null<QWidget*> parent,
		rpl::producer<Option> optionValue) {
	using namespace rpl::mappers;

	auto message = GenerateForwardedItem(
		delegate(),
		_session->data().history(
			peerFromUser(PeerData::kServiceNotificationsId)),
		tr::lng_edit_privacy_forwards_sample_message(tr::now));
	const auto view = message.get();

	auto result = object_ptr<Ui::RpWidget>(parent);
	const auto widget = result.data();
	Ui::AttachAsChild(widget, std::move(message));

	const auto option = widget->lifetime().make_state<Option>();

	const auto padding = st::settingsForwardPrivacyPadding;
	widget->widthValue(
	) | rpl::filter(
		_1 >= (st::historyMinimalWidth / 2)
	) | rpl::start_with_next([=](int width) {
		const auto height = view->resizeGetHeight(width);
		const auto top = view->marginTop();
		const auto bottom = view->marginBottom();
		const auto full = padding + bottom + height + top + padding;
		widget->resize(width, full);
	}, widget->lifetime());

	widget->paintRequest(
	) | rpl::start_with_next([=](QRect rect) {
		Window::SectionWidget::PaintBackground(widget, rect);

		Painter p(widget);
		p.translate(0, padding + view->marginBottom());
		view->draw(p, widget->rect(), TextSelection(), crl::now());

		PaintForwardedTooltip(p, view, *option);
	}, widget->lifetime());

	std::move(
		optionValue
	) | rpl::start_with_next([=](Option value) {
		*option = value;
		widget->update();
	}, widget->lifetime());

	return result;
}

void ForwardsPrivacyController::PaintForwardedTooltip(
		Painter &p,
		not_null<HistoryView::Element*> view,
		Option value) {
	// This breaks HistoryView::Element encapsulation :(
	const auto forwarded = view->data()->Get<HistoryMessageForwarded>();
	const auto availableWidth = view->width()
		- st::msgMargin.left()
		- st::msgMargin.right();
	const auto bubbleWidth = ranges::min({
		availableWidth,
		view->maxWidth(),
		st::msgMaxWidth
	});
	const auto innerWidth = bubbleWidth
		- st::msgPadding.left()
		- st::msgPadding.right();
	const auto phrase = tr::lng_forwarded(
		tr::now,
		lt_user,
		App::peerName(view->data()->history()->session().user()));
	const auto kReplacementPosition = QChar(0x0001);
	const auto possiblePosition = tr::lng_forwarded(
		tr::now,
		lt_user,
		QString(1, kReplacementPosition)
	).indexOf(kReplacementPosition);
	const auto position = (possiblePosition >= 0
		&& possiblePosition < phrase.size())
		? possiblePosition
		: 0;
	const auto before = phrase.mid(0, position);
	const auto skip = st::msgMargin.left() + st::msgPadding.left();
	const auto small = forwarded->text.countHeight(innerWidth)
		< 2 * st::msgServiceFont->height;
	const auto nameLeft = skip + (small ? st::msgServiceFont->width(before) : 0);
	const auto right = skip + innerWidth;
	const auto text = [&] {
		switch (value) {
		case Option::Everyone:
			return tr::lng_edit_privacy_forwards_sample_everyone(tr::now);
		case Option::Contacts:
			return tr::lng_edit_privacy_forwards_sample_contacts(tr::now);
		case Option::Nobody:
			return tr::lng_edit_privacy_forwards_sample_nobody(tr::now);
		}
		Unexpected("Option value in ForwardsPrivacyController.");
	}();
	const auto &font = st::toastTextStyle.font;
	const auto textWidth = font->width(text);
	const auto arrowSkip = st::settingsForwardPrivacyArrowSkip;
	const auto arrowSize = st::settingsForwardPrivacyArrowSize;
	const auto fullWidth = std::max(textWidth, 2 * arrowSkip);
	const auto padding = st::settingsForwardPrivacyTooltipPadding;
	const auto rect = QRect(0, 0, textWidth, font->height).marginsAdded(
		padding
	).translated(padding.left(), padding.top());

	const auto top = view->marginTop()
		+ st::msgPadding.top()
		+ (small ? 1 : 2) * st::msgServiceFont->height
		+ arrowSize;
	const auto left1 = std::min(nameLeft, right - rect.width());
	const auto left2 = std::max(left1, skip);
	const auto left = left2;
	const auto arrowLeft1 = nameLeft + arrowSkip;
	const auto arrowLeft2 = std::min(
		arrowLeft1,
		std::max((left + right) / 2, right - arrowSkip));
	const auto arrowLeft = arrowLeft2;
	const auto geometry = rect.translated(left, top);

	App::roundRect(p, geometry, st::toastBg, ImageRoundRadius::Small);

	p.setFont(font);
	p.setPen(st::toastFg);
	p.drawText(
		geometry.x() + padding.left(),
		geometry.y() + padding.top() + font->ascent,
		text);

	QPainterPath path;
	path.moveTo(arrowLeft - arrowSize, top);
	path.lineTo(arrowLeft, top - arrowSize);
	path.lineTo(arrowLeft + arrowSize, top);
	path.lineTo(arrowLeft - arrowSize, top);
	{
		PainterHighQualityEnabler hq(p);
		p.setPen(Qt::NoPen);
		p.fillPath(path, st::toastBg);
	}
}

auto ForwardsPrivacyController::delegate()
-> not_null<HistoryView::ElementDelegate*> {
	return static_cast<HistoryView::ElementDelegate*>(this);
}

HistoryView::Context ForwardsPrivacyController::elementContext() {
	return HistoryView::Context::ContactPreview;
}

ApiWrap::Privacy::Key ProfilePhotoPrivacyController::key() {
	return Key::ProfilePhoto;
}

MTPInputPrivacyKey ProfilePhotoPrivacyController::apiKey() {
	return MTP_inputPrivacyKeyProfilePhoto();
}

rpl::producer<QString> ProfilePhotoPrivacyController::title() {
	return tr::lng_edit_privacy_profile_photo_title();
}

bool ProfilePhotoPrivacyController::hasOption(Option option) {
	return (option != Option::Nobody);
}

rpl::producer<QString> ProfilePhotoPrivacyController::optionsTitleKey() {
	return tr::lng_edit_privacy_profile_photo_header();
}

rpl::producer<QString> ProfilePhotoPrivacyController::exceptionButtonTextKey(
		Exception exception) {
	switch (exception) {
	case Exception::Always: return tr::lng_edit_privacy_profile_photo_always_empty();
	case Exception::Never: return tr::lng_edit_privacy_profile_photo_never_empty();
	}
	Unexpected("Invalid exception value.");
}

rpl::producer<QString> ProfilePhotoPrivacyController::exceptionBoxTitle(
		Exception exception) {
	switch (exception) {
	case Exception::Always: return tr::lng_edit_privacy_profile_photo_always_title();
	case Exception::Never: return tr::lng_edit_privacy_profile_photo_never_title();
	}
	Unexpected("Invalid exception value.");
}

auto ProfilePhotoPrivacyController::exceptionsDescription()
-> rpl::producer<QString> {
	return tr::lng_edit_privacy_profile_photo_exceptions();
}

} // namespace Settings
