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
#include "main/main_session.h"
#include "data/data_user.h"
#include "data/data_session.h"
#include "data/data_changes.h"
#include "data/data_peer_values.h" // Data::AmPremiumValue.
#include "core/application.h"
#include "core/core_settings.h"
#include "history/admin_log/history_admin_log_item.h"
#include "history/view/history_view_element.h"
#include "history/view/history_view_message.h"
#include "history/history_item_components.h"
#include "history/history_message.h"
#include "history/history.h"
#include "calls/calls_instance.h"
#include "base/unixtime.h"
#include "base/event_filter.h"
#include "ui/chat/chat_theme.h"
#include "ui/chat/chat_style.h"
#include "ui/widgets/checkbox.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/image/image_prepare.h"
#include "ui/cached_round_corners.h"
#include "ui/text/format_values.h" // Ui::FormatPhone
#include "ui/text/text_utilities.h"
#include "window/section_widget.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "boxes/peer_list_controllers.h"
#include "ui/boxes/confirm_box.h"
#include "settings/settings_privacy_security.h"
#include "facades.h"
#include "styles/style_chat.h"
#include "styles/style_boxes.h"
#include "styles/style_settings.h"

#include <QtGui/QGuiApplication>
#include <QtGui/QClipboard>

namespace Settings {
namespace {

using UserPrivacy = Api::UserPrivacy;
using PrivacyRule = Api::UserPrivacy::Rule;
using Option = EditPrivacyBox::Option;

[[nodiscard]] QString PublicLinkByPhone(not_null<UserData*> user) {
	return user->session().createInternalLinkFull('+' + user->phone());
}

class BlockPeerBoxController final : public ChatsListBoxController {
public:
	explicit BlockPeerBoxController(not_null<Main::Session*> session);

	Main::Session &session() const override;
	void rowClicked(not_null<PeerListRow*> row) override;

	void setBlockPeerCallback(Fn<void(not_null<PeerData*> peer)> callback) {
		_blockPeerCallback = std::move(callback);
	}

protected:
	void prepareViewHook() override;
	std::unique_ptr<Row> createRow(not_null<History*> history) override;
	void updateRowHook(not_null<Row*> row) override {
		updateIsBlocked(row, row->peer());
		delegate()->peerListUpdateRow(row);
	}

private:
	void updateIsBlocked(not_null<PeerListRow*> row, PeerData *peer) const;

	const not_null<Main::Session*> _session;
	Fn<void(not_null<PeerData*> peer)> _blockPeerCallback;

};

BlockPeerBoxController::BlockPeerBoxController(
	not_null<Main::Session*> session)
: ChatsListBoxController(session)
, _session(session) {
}

Main::Session &BlockPeerBoxController::session() const {
	return *_session;
}

void BlockPeerBoxController::prepareViewHook() {
	delegate()->peerListSetTitle(tr::lng_blocked_list_add_title());
	session().changes().peerUpdates(
		Data::PeerUpdate::Flag::IsBlocked
	) | rpl::start_with_next([=](const Data::PeerUpdate &update) {
		if (auto row = delegate()->peerListFindRow(update.peer->id.value)) {
			updateIsBlocked(row, update.peer);
			delegate()->peerListUpdateRow(row);
		}
	}, lifetime());
}

void BlockPeerBoxController::updateIsBlocked(
		not_null<PeerListRow*> row,
		PeerData *peer) const {
	if (!peer) {
		return;
	}
	const auto blocked = peer->isBlocked();
	row->setDisabledState(blocked
		? PeerListRow::State::DisabledChecked
		: PeerListRow::State::Active);
	if (blocked) {
		row->setCustomStatus(tr::lng_blocked_list_already_blocked(tr::now));
	} else {
		row->clearCustomStatus();
	}
}

void BlockPeerBoxController::rowClicked(not_null<PeerListRow*> row) {
	_blockPeerCallback(row->peer());
}

auto BlockPeerBoxController::createRow(not_null<History*> history)
-> std::unique_ptr<BlockPeerBoxController::Row> {
	if (!history->peer->isUser()
		|| history->peer->isServiceUser()
		|| history->peer->isSelf()
		|| history->peer->isRepliesChat()) {
		return nullptr;
	}
	auto row = std::make_unique<Row>(history);
	updateIsBlocked(row.get(), history->peer);
	return row;
}

AdminLog::OwnedItem GenerateForwardedItem(
		not_null<HistoryView::ElementDelegate*> delegate,
		not_null<History*> history,
		const QString &text) {
	Expects(history->peer->isUser());

	using Flag = MTPDmessage::Flag;
	const auto flags = Flag::f_from_id | Flag::f_fwd_from;
	const auto item = MTP_message(
		MTP_flags(flags),
		MTP_int(0), // Not used (would've been trimmed to 32 bits).
		peerToMTP(history->peer->id),
		peerToMTP(history->peer->id),
		MTP_messageFwdHeader(
			MTP_flags(MTPDmessageFwdHeader::Flag::f_from_id),
			peerToMTP(history->session().userPeerId()),
			MTPstring(), // from_name
			MTP_int(base::unixtime::now()),
			MTPint(), // channel_post
			MTPstring(), // post_author
			MTPPeer(), // saved_from_peer
			MTPint(), // saved_from_msg_id
			MTPstring()), // psa_type
		MTPlong(), // via_bot_id
		MTPMessageReplyHeader(),
		MTP_int(base::unixtime::now()), // date
		MTP_string(text),
		MTPMessageMedia(),
		MTPReplyMarkup(),
		MTPVector<MTPMessageEntity>(),
		MTPint(), // views
		MTPint(), // forwards
		MTPMessageReplies(),
		MTPint(), // edit_date
		MTPstring(), // post_author
		MTPlong(), // grouped_id
		MTPMessageReactions(),
		MTPVector<MTPRestrictionReason>(),
		MTPint() // ttl_period
	).match([&](const MTPDmessage &data) {
		return history->makeMessage(
			history->nextNonHistoryEntryId(),
			data,
			MessageFlag::FakeHistoryItem);
	}, [](auto &&) -> not_null<HistoryMessage*> {
		Unexpected("Type in GenerateForwardedItem.");
	});

	return AdminLog::OwnedItem(delegate, item);
}

struct ForwardedTooltip {
	QRect geometry;
	Fn<void(Painter&)> paint;
};
[[nodiscard]] ForwardedTooltip PrepareForwardedTooltip(
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
		view->history()->session().user()->name());
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
	const auto nameLeft = skip
		+ (small ? st::msgServiceFont->width(before) : 0);
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
	const auto &font = st::defaultToast.style.font;
	const auto textWidth = font->width(text);
	const auto arrowSkip = st::settingsForwardPrivacyArrowSkip;
	const auto arrowSize = st::settingsForwardPrivacyArrowSize;
	const auto padding = st::settingsForwardPrivacyTooltipPadding;
	const auto rect = QRect(0, 0, textWidth, font->height).marginsAdded(
		padding
	).translated(padding.left(), padding.top());

	const auto top = st::settingsForwardPrivacyPadding
		+ view->marginTop()
		+ st::msgPadding.top()
		- arrowSize
		- rect.height();
	const auto left1 = std::min(nameLeft, right - rect.width());
	const auto left2 = std::max(left1, skip);
	const auto left = left2;
	const auto arrowLeft1 = nameLeft + arrowSkip;
	const auto arrowLeft2 = std::min(
		arrowLeft1,
		std::max((left + right) / 2, right - arrowSkip));
	const auto arrowLeft = arrowLeft2;
	const auto geometry = rect.translated(left, top);

	const auto line = st::lineWidth;
	const auto full = geometry.marginsAdded(
		{ line, line, line, line + arrowSize });
	const auto origin = full.topLeft();

	const auto paint = [=](Painter &p) {
		p.translate(-origin);

		Ui::FillRoundRect(
			p,
			geometry,
			st::toastBg,
			ImageRoundRadius::Large);

		p.setFont(font);
		p.setPen(st::toastFg);
		p.drawText(
			geometry.x() + padding.left(),
			geometry.y() + padding.top() + font->ascent,
			text);

		const auto bottom = full.y() + full.height() - line;

		QPainterPath path;
		path.moveTo(arrowLeft - arrowSize, bottom - arrowSize);
		path.lineTo(arrowLeft, bottom);
		path.lineTo(arrowLeft + arrowSize, bottom - arrowSize);
		path.lineTo(arrowLeft - arrowSize, bottom - arrowSize);
		{
			PainterHighQualityEnabler hq(p);
			p.setPen(Qt::NoPen);
			p.fillPath(path, st::toastBg);
		}
	};
	return { .geometry = full, .paint = paint };
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

	session().changes().peerUpdates(
		Data::PeerUpdate::Flag::IsBlocked
	) | rpl::start_with_next([=](const Data::PeerUpdate &update) {
		handleBlockedEvent(update.peer);
	}, lifetime());

	session().api().blockedPeers().slice(
	) | rpl::take(
		1
	) | rpl::start_with_next([=](const Api::BlockedPeers::Slice &result) {
		setDescriptionText(tr::lng_blocked_list_about(tr::now));
		applySlice(result);
		loadMoreRows();
	}, lifetime());
}

void BlockedBoxController::loadMoreRows() {
	if (_allLoaded) {
		return;
	}

	session().api().blockedPeers().request(
		_offset,
		crl::guard(&_guard, [=](const Api::BlockedPeers::Slice &slice) {
			applySlice(slice);
		}));
}

void BlockedBoxController::rowClicked(not_null<PeerListRow*> row) {
	const auto peer = row->peer();
	crl::on_main(&peer->session(), [=] {
		Ui::showPeerHistory(peer, ShowAtUnreadMsgId);
	});
}

void BlockedBoxController::rowRightActionClicked(not_null<PeerListRow*> row) {
	session().api().blockedPeers().unblock(row->peer());
}

void BlockedBoxController::applySlice(const Api::BlockedPeers::Slice &slice) {
	if (slice.list.empty()) {
		_allLoaded = true;
	}

	_offset += slice.list.size();
	for (const auto &item : slice.list) {
		if (const auto peer = session().data().peerLoaded(item.id)) {
			appendRow(peer);
			peer->setIsBlocked(true);
		}
	}
	if (_offset >= slice.total) {
		_allLoaded = true;
	}
	delegate()->peerListRefreshRows();
}

void BlockedBoxController::handleBlockedEvent(not_null<PeerData*> user) {
	if (user->isBlocked()) {
		if (prependRow(user)) {
			delegate()->peerListRefreshRows();
			delegate()->peerListScrollToTop();
		}
	} else if (auto row = delegate()->peerListFindRow(user->id.value)) {
		delegate()->peerListRemoveRow(row);
		delegate()->peerListRefreshRows();
		_rowsCountChanges.fire(delegate()->peerListFullRowsCount());
	}
}

void BlockedBoxController::BlockNewPeer(
		not_null<Window::SessionController*> window) {
	auto controller = std::make_unique<BlockPeerBoxController>(
		&window->session());
	auto initBox = [=, controller = controller.get()](
			not_null<PeerListBox*> box) {
		controller->setBlockPeerCallback([=](not_null<PeerData*> peer) {
			window->session().api().blockedPeers().block(peer);
			box->closeBox();
		});
		box->addButton(tr::lng_cancel(), [box] { box->closeBox(); });
	};
	window->show(
		Box<PeerListBox>(std::move(controller), std::move(initBox)),
		Ui::LayerOption::KeepOther);
}

bool BlockedBoxController::appendRow(not_null<PeerData*> peer) {
	if (delegate()->peerListFindRow(peer->id.value)) {
		return false;
	}
	delegate()->peerListAppendRow(createRow(peer));
	_rowsCountChanges.fire(delegate()->peerListFullRowsCount());
	return true;
}

bool BlockedBoxController::prependRow(not_null<PeerData*> peer) {
	if (delegate()->peerListFindRow(peer->id.value)) {
		return false;
	}
	delegate()->peerListPrependRow(createRow(peer));
	_rowsCountChanges.fire(delegate()->peerListFullRowsCount());
	return true;
}

std::unique_ptr<PeerListRow> BlockedBoxController::createRow(
		not_null<PeerData*> peer) const {
	auto row = std::make_unique<PeerListRowWithLink>(peer);
	row->setActionLink(tr::lng_blocked_list_unblock(tr::now));
	const auto status = [&] {
		const auto user = peer->asUser();
		if (!user) {
			return tr::lng_group_status(tr::now);
		} else if (!user->phone().isEmpty()) {
			return Ui::FormatPhone(user->phone());
		} else if (!user->username.isEmpty()) {
			return '@' + user->username;
		} else if (user->isBot()) {
			return tr::lng_status_bot(tr::now);
		}
		return tr::lng_blocked_list_unknown_phone(tr::now);
	}();
	row->setCustomStatus(status);
	return row;
}

rpl::producer<int> BlockedBoxController::rowsCountChanges() const {
	return _rowsCountChanges.events();
}

PhoneNumberPrivacyController::PhoneNumberPrivacyController(
	not_null<Window::SessionController*> controller)
: _controller(controller) {
}

UserPrivacy::Key PhoneNumberPrivacyController::key() const {
	return Key::PhoneNumber;
}

rpl::producer<QString> PhoneNumberPrivacyController::title() const {
	return tr::lng_edit_privacy_phone_number_title();
}

rpl::producer<QString> PhoneNumberPrivacyController::optionsTitleKey() const {
	return tr::lng_edit_privacy_phone_number_header();
}

auto PhoneNumberPrivacyController::warning() const
-> rpl::producer<TextWithEntities> {
	using namespace rpl::mappers;
	const auto self = _controller->session().user();
	return rpl::combine(
		_phoneNumberOption.value(),
		_addedByPhone.value(),
		(_1 == Option::Nobody) && (_2 != Option::Everyone)
	) | rpl::map([=](bool onlyContactsSee) {
		return onlyContactsSee
			? tr::lng_edit_privacy_phone_number_contacts(
				Ui::Text::WithEntities)
			: rpl::combine(
				tr::lng_edit_privacy_phone_number_warning(),
				tr::lng_username_link()
			) | rpl::map([=](const QString &warning, const QString &added) {
				auto base = TextWithEntities{
					warning + "\n\n" + added + "\n",
				};
				const auto link = PublicLinkByPhone(self);
				return base.append(Ui::Text::Link(link, link));
			});
	}) | rpl::flatten_latest();
}

void PhoneNumberPrivacyController::prepareWarningLabel(
		not_null<Ui::FlatLabel*> warning) const {
	warning->overrideLinkClickHandler([=] {
		QGuiApplication::clipboard()->setText(PublicLinkByPhone(
			_controller->session().user()));
		_controller->window().showToast(
			tr::lng_username_copied(tr::now));
	});
}

rpl::producer<QString> PhoneNumberPrivacyController::exceptionButtonTextKey(
		Exception exception) const {
	switch (exception) {
	case Exception::Always:
		return tr::lng_edit_privacy_phone_number_always_empty();
	case Exception::Never:
		return tr::lng_edit_privacy_phone_number_never_empty();
	}
	Unexpected("Invalid exception value.");
}

rpl::producer<QString> PhoneNumberPrivacyController::exceptionBoxTitle(
		Exception exception) const {
	switch (exception) {
	case Exception::Always: {
		return tr::lng_edit_privacy_phone_number_always_title();
	};
	case Exception::Never: {
		return tr::lng_edit_privacy_phone_number_never_title();
	};
	}
	Unexpected("Invalid exception value.");
}

auto PhoneNumberPrivacyController::exceptionsDescription() const
-> rpl::producer<QString> {
	return tr::lng_edit_privacy_phone_number_exceptions();
}

object_ptr<Ui::RpWidget> PhoneNumberPrivacyController::setupMiddleWidget(
		not_null<Window::SessionController*> controller,
		not_null<QWidget*> parent,
		rpl::producer<Option> optionValue) {
	const auto key = UserPrivacy::Key::AddedByPhone;
	controller->session().api().userPrivacy().reload(key);

	_phoneNumberOption = std::move(optionValue);

	auto widget = object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
		parent,
		object_ptr<Ui::VerticalLayout>(parent));

	const auto container = widget->entity();
	AddSkip(container);
	AddSubsectionTitle(container, tr::lng_edit_privacy_phone_number_find());
	const auto group = std::make_shared<Ui::RadioenumGroup<Option>>();
	group->setChangedCallback([=](Option value) {
		_addedByPhone = value;
	});
	controller->session().api().userPrivacy().value(
		key
	) | rpl::take(
		1
	) | rpl::start_with_next([=](const PrivacyRule &value) {
		group->setValue(value.option);
	}, widget->lifetime());

	const auto addOption = [&](Option option) {
		return EditPrivacyBox::AddOption(container, this, group, option);
	};
	addOption(Option::Everyone);
	addOption(Option::Contacts);
	AddSkip(container, st::settingsSectionSkip + st::settingsPrivacySkipTop);
	AddDivider(container);

	using namespace rpl::mappers;
	widget->toggleOn(_phoneNumberOption.value(
	) | rpl::map(
		_1 == Option::Nobody
	));

	_saveAdditional = [=] {
		controller->session().api().userPrivacy().save(
			Api::UserPrivacy::Key::AddedByPhone,
			Api::UserPrivacy::Rule{ .option = group->value() });
	};

	return widget;
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

UserPrivacy::Key LastSeenPrivacyController::key() const {
	return Key::LastSeen;
}

rpl::producer<QString> LastSeenPrivacyController::title() const {
	return tr::lng_edit_privacy_lastseen_title();
}

rpl::producer<QString> LastSeenPrivacyController::optionsTitleKey() const {
	return tr::lng_edit_privacy_lastseen_header();
}

auto LastSeenPrivacyController::warning() const
-> rpl::producer<TextWithEntities> {
	return tr::lng_edit_privacy_lastseen_warning(Ui::Text::WithEntities);
}

rpl::producer<QString> LastSeenPrivacyController::exceptionButtonTextKey(
		Exception exception) const {
	switch (exception) {
	case Exception::Always:
		return tr::lng_edit_privacy_lastseen_always_empty();
	case Exception::Never:
		return tr::lng_edit_privacy_lastseen_never_empty();
	}
	Unexpected("Invalid exception value.");
}

rpl::producer<QString> LastSeenPrivacyController::exceptionBoxTitle(
		Exception exception) const {
	switch (exception) {
	case Exception::Always: {
		return tr::lng_edit_privacy_lastseen_always_title();
	};
	case Exception::Never: {
		return tr::lng_edit_privacy_lastseen_never_title();
	};
	}
	Unexpected("Invalid exception value.");
}

auto LastSeenPrivacyController::exceptionsDescription() const
-> rpl::producer<QString>{
	return tr::lng_edit_privacy_lastseen_exceptions();
}

void LastSeenPrivacyController::confirmSave(
		bool someAreDisallowed,
		Fn<void()> saveCallback) {
	if (someAreDisallowed && !Core::App().settings().lastSeenWarningSeen()) {
		auto callback = [
			=,
			saveCallback = std::move(saveCallback)
		](Fn<void()> &&close) {
			close();
			saveCallback();
			Core::App().settings().setLastSeenWarningSeen(true);
			Core::App().saveSettingsDelayed();
		};
		auto box = Ui::MakeConfirmBox({
			.text = tr::lng_edit_privacy_lastseen_warning(),
			.confirmed = std::move(callback),
			.confirmText = tr::lng_continue(),
		});
		Ui::show(std::move(box), Ui::LayerOption::KeepOther);
	} else {
		saveCallback();
	}
}

UserPrivacy::Key GroupsInvitePrivacyController::key() const {
	return Key::Invites;
}

rpl::producer<QString> GroupsInvitePrivacyController::title() const {
	return tr::lng_edit_privacy_groups_title();
}

bool GroupsInvitePrivacyController::hasOption(Option option) const {
	return (option != Option::Nobody);
}

rpl::producer<QString> GroupsInvitePrivacyController::optionsTitleKey(
		) const {
	return tr::lng_edit_privacy_groups_header();
}

rpl::producer<QString> GroupsInvitePrivacyController::exceptionButtonTextKey(
		Exception exception) const {
	switch (exception) {
	case Exception::Always: return tr::lng_edit_privacy_groups_always_empty();
	case Exception::Never: return tr::lng_edit_privacy_groups_never_empty();
	}
	Unexpected("Invalid exception value.");
}

rpl::producer<QString> GroupsInvitePrivacyController::exceptionBoxTitle(
		Exception exception) const {
	switch (exception) {
	case Exception::Always: return tr::lng_edit_privacy_groups_always_title();
	case Exception::Never: return tr::lng_edit_privacy_groups_never_title();
	}
	Unexpected("Invalid exception value.");
}

auto GroupsInvitePrivacyController::exceptionsDescription() const
-> rpl::producer<QString> {
	return tr::lng_edit_privacy_groups_exceptions();
}

UserPrivacy::Key CallsPrivacyController::key() const {
	return Key::Calls;
}

rpl::producer<QString> CallsPrivacyController::title() const {
	return tr::lng_edit_privacy_calls_title();
}

rpl::producer<QString> CallsPrivacyController::optionsTitleKey() const {
	return tr::lng_edit_privacy_calls_header();
}

rpl::producer<QString> CallsPrivacyController::exceptionButtonTextKey(
		Exception exception) const {
	switch (exception) {
	case Exception::Always: return tr::lng_edit_privacy_calls_always_empty();
	case Exception::Never: return tr::lng_edit_privacy_calls_never_empty();
	}
	Unexpected("Invalid exception value.");
}

rpl::producer<QString> CallsPrivacyController::exceptionBoxTitle(
		Exception exception) const {
	switch (exception) {
	case Exception::Always: return tr::lng_edit_privacy_calls_always_title();
	case Exception::Never: return tr::lng_edit_privacy_calls_never_title();
	}
	Unexpected("Invalid exception value.");
}

auto CallsPrivacyController::exceptionsDescription() const
-> rpl::producer<QString>{
	return tr::lng_edit_privacy_calls_exceptions();
}

object_ptr<Ui::RpWidget> CallsPrivacyController::setupBelowWidget(
		not_null<Window::SessionController*> controller,
		not_null<QWidget*> parent) const {
	auto result = object_ptr<Ui::VerticalLayout>(parent);
	const auto content = result.data();

	AddSkip(content, st::settingsPeerToPeerSkip);
	AddSubsectionTitle(content, tr::lng_settings_calls_peer_to_peer_title());
	Settings::AddPrivacyButton(
		controller,
		content,
		tr::lng_settings_calls_peer_to_peer_button(),
		{ &st::settingsIconArrows, kIconLightBlue },
		UserPrivacy::Key::CallsPeer2Peer,
		[] { return std::make_unique<CallsPeer2PeerPrivacyController>(); });
	AddSkip(content);

	return result;
}

UserPrivacy::Key CallsPeer2PeerPrivacyController::key() const {
	return Key::CallsPeer2Peer;
}

rpl::producer<QString> CallsPeer2PeerPrivacyController::title() const {
	return tr::lng_edit_privacy_calls_p2p_title();
}

rpl::producer<QString> CallsPeer2PeerPrivacyController::optionsTitleKey() const {
	return tr::lng_edit_privacy_calls_p2p_header();
}

QString CallsPeer2PeerPrivacyController::optionLabel(
		EditPrivacyBox::Option option) const {
	switch (option) {
	case Option::Everyone: {
		return tr::lng_edit_privacy_calls_p2p_everyone(tr::now);
	};
	case Option::Contacts: {
		return tr::lng_edit_privacy_calls_p2p_contacts(tr::now);
	};
	case Option::Nobody: {
		return tr::lng_edit_privacy_calls_p2p_nobody(tr::now);
	};
	}
	Unexpected("Option value in optionsLabelKey.");
}

auto CallsPeer2PeerPrivacyController::warning() const
-> rpl::producer<TextWithEntities> {
	return tr::lng_settings_peer_to_peer_about(Ui::Text::WithEntities);
}

rpl::producer<QString> CallsPeer2PeerPrivacyController::exceptionButtonTextKey(
		Exception exception) const {
	switch (exception) {
	case Exception::Always: {
		return tr::lng_edit_privacy_calls_p2p_always_empty();
	};
	case Exception::Never: {
		return tr::lng_edit_privacy_calls_p2p_never_empty();
	};
	}
	Unexpected("Invalid exception value.");
}

rpl::producer<QString> CallsPeer2PeerPrivacyController::exceptionBoxTitle(
		Exception exception) const {
	switch (exception) {
	case Exception::Always: {
		return tr::lng_edit_privacy_calls_p2p_always_title();
	};
	case Exception::Never: {
		return tr::lng_edit_privacy_calls_p2p_never_title();
	};
	}
	Unexpected("Invalid exception value.");
}

auto CallsPeer2PeerPrivacyController::exceptionsDescription() const
-> rpl::producer<QString>{
	return tr::lng_edit_privacy_calls_p2p_exceptions();
}

ForwardsPrivacyController::ForwardsPrivacyController(
	not_null<Window::SessionController*> controller)
: SimpleElementDelegate(controller, [] {})
, _controller(controller)
, _chatStyle(std::make_unique<Ui::ChatStyle>()) {
	_chatStyle->apply(controller->defaultChatTheme().get());
}

UserPrivacy::Key ForwardsPrivacyController::key() const {
	return Key::Forwards;
}

rpl::producer<QString> ForwardsPrivacyController::title() const {
	return tr::lng_edit_privacy_forwards_title();
}

rpl::producer<QString> ForwardsPrivacyController::optionsTitleKey() const {
	return tr::lng_edit_privacy_forwards_header();
}

auto ForwardsPrivacyController::warning() const
-> rpl::producer<TextWithEntities> {
	return tr::lng_edit_privacy_forwards_warning(Ui::Text::WithEntities);
}

rpl::producer<QString> ForwardsPrivacyController::exceptionButtonTextKey(
		Exception exception) const {
	switch (exception) {
	case Exception::Always: {
		return tr::lng_edit_privacy_forwards_always_empty();
	};
	case Exception::Never: {
		return tr::lng_edit_privacy_forwards_never_empty();
	};
	}
	Unexpected("Invalid exception value.");
}

rpl::producer<QString> ForwardsPrivacyController::exceptionBoxTitle(
		Exception exception) const {
	switch (exception) {
	case Exception::Always: {
		return tr::lng_edit_privacy_forwards_always_title();
	};
	case Exception::Never: {
		return tr::lng_edit_privacy_forwards_never_title();
	};
	}
	Unexpected("Invalid exception value.");
}

auto ForwardsPrivacyController::exceptionsDescription() const
-> rpl::producer<QString> {
	return tr::lng_edit_privacy_forwards_exceptions();
}

object_ptr<Ui::RpWidget> ForwardsPrivacyController::setupAboveWidget(
		not_null<QWidget*> parent,
		rpl::producer<Option> optionValue,
		not_null<QWidget*> outerContainer) {
	using namespace rpl::mappers;

	auto message = GenerateForwardedItem(
		delegate(),
		_controller->session().data().history(
			PeerData::kServiceNotificationsId),
		tr::lng_edit_privacy_forwards_sample_message(tr::now));
	const auto view = message.get();

	auto result = object_ptr<Ui::PaddingWrap<Ui::RpWidget>>(
		parent,
		object_ptr<Ui::RpWidget>(parent),
		style::margins(
			0,
			st::settingsSectionSkip,
			0,
			st::settingsPrivacySkipTop));
	const auto widget = result->entity();

	struct State {
		AdminLog::OwnedItem item;
		Option option = {};
		base::unique_qptr<Ui::RpWidget> tooltip;
		ForwardedTooltip info;
		Fn<void()> refreshGeometry;
	};
	const auto state = widget->lifetime().make_state<State>();
	state->item = std::move(message);
	state->tooltip = base::make_unique_q<Ui::RpWidget>(outerContainer);
	state->tooltip->paintRequest(
	) | rpl::start_with_next([=] {
		if (state->info.paint) {
			auto p = Painter(state->tooltip.get());
			state->info.paint(p);
		}
	}, state->tooltip->lifetime());
	state->refreshGeometry = [=] {
		state->tooltip->show();
		state->tooltip->raise();
		auto position = state->info.geometry.topLeft();
		auto parent = (QWidget*)widget;
		while (parent && parent != outerContainer) {
			position += parent->pos();
			parent = parent->parentWidget();
		}
		state->tooltip->move(position);
	};
	const auto watch = [&](QWidget *widget, const auto &self) -> void {
		if (!widget) {
			return;
		}
		base::install_event_filter(state->tooltip, widget, [=](
				not_null<QEvent*> e) {
			if (e->type() == QEvent::Move
				|| e->type() == QEvent::Show
				|| e->type() == QEvent::ShowToParent
				|| e->type() == QEvent::ZOrderChange) {
				state->refreshGeometry();
			}
			return base::EventFilterResult::Continue;
		});
		if (widget == outerContainer) {
			return;
		}
		self(widget->parentWidget(), self);
	};
	watch(widget, watch);

	const auto padding = st::settingsForwardPrivacyPadding;
	widget->widthValue(
	) | rpl::filter(
		_1 >= (st::historyMinimalWidth / 2)
	) | rpl::start_with_next([=](int width) {
		const auto height = view->resizeGetHeight(width);
		const auto top = view->marginTop();
		const auto bottom = view->marginBottom();
		const auto full = padding + top + height + bottom + padding;
		widget->resize(width, full);
	}, widget->lifetime());

	rpl::combine(
		widget->widthValue(),
		std::move(optionValue)
	) | rpl::start_with_next([=](int width, Option value) {
		state->info = PrepareForwardedTooltip(view, value);
		state->tooltip->resize(state->info.geometry.size());
		state->refreshGeometry();
		state->tooltip->update();
	}, state->tooltip->lifetime());

	widget->paintRequest(
	) | rpl::start_with_next([=](QRect rect) {
		// #TODO themes
		Window::SectionWidget::PaintBackground(
			_controller,
			_controller->defaultChatTheme().get(), // #TODO themes
			widget,
			rect);

		Painter p(widget);
		const auto theme = _controller->defaultChatTheme().get();
		auto context = theme->preparePaintContext(
			_chatStyle.get(),
			widget->rect(),
			widget->rect(),
			_controller->isGifPausedAtLeastFor(
				Window::GifPauseReason::Layer));
		p.translate(padding / 2, padding + view->marginBottom());
		context.outbg = view->hasOutLayout();
		view->draw(p, context);
	}, widget->lifetime());

	return result;
}

auto ForwardsPrivacyController::delegate()
-> not_null<HistoryView::ElementDelegate*> {
	return static_cast<HistoryView::ElementDelegate*>(this);
}

HistoryView::Context ForwardsPrivacyController::elementContext() {
	return HistoryView::Context::ContactPreview;
}

UserPrivacy::Key ProfilePhotoPrivacyController::key() const {
	return Key::ProfilePhoto;
}

rpl::producer<QString> ProfilePhotoPrivacyController::title() const {
	return tr::lng_edit_privacy_profile_photo_title();
}

bool ProfilePhotoPrivacyController::hasOption(Option option) const {
	return (option != Option::Nobody);
}

rpl::producer<QString> ProfilePhotoPrivacyController::optionsTitleKey() const {
	return tr::lng_edit_privacy_profile_photo_header();
}

rpl::producer<QString> ProfilePhotoPrivacyController::exceptionButtonTextKey(
		Exception exception) const {
	switch (exception) {
	case Exception::Always: {
		return tr::lng_edit_privacy_profile_photo_always_empty();
	};
	case Exception::Never: {
		return tr::lng_edit_privacy_profile_photo_never_empty();
	};
	}
	Unexpected("Invalid exception value.");
}

rpl::producer<QString> ProfilePhotoPrivacyController::exceptionBoxTitle(
		Exception exception) const {
	switch (exception) {
	case Exception::Always: {
		return tr::lng_edit_privacy_profile_photo_always_title();
	};
	case Exception::Never: {
		return tr::lng_edit_privacy_profile_photo_never_title();
	};
	}
	Unexpected("Invalid exception value.");
}

auto ProfilePhotoPrivacyController::exceptionsDescription() const
-> rpl::producer<QString> {
	return tr::lng_edit_privacy_profile_photo_exceptions();
}

VoicesPrivacyController::VoicesPrivacyController(
		not_null<::Main::Session*> session) {
	Data::AmPremiumValue(
		session
	) | rpl::start_with_next([=](bool premium) {
		if (!premium) {
			if (const auto box = view()) {
				box->closeBox();
			}
		}
	}, _lifetime);
}

UserPrivacy::Key VoicesPrivacyController::key() const {
	return Key::Voices;
}

rpl::producer<QString> VoicesPrivacyController::title() const {
	return tr::lng_edit_privacy_voices_title();
}

rpl::producer<QString> VoicesPrivacyController::optionsTitleKey() const {
	return tr::lng_edit_privacy_voices_header();
}

rpl::producer<QString> VoicesPrivacyController::exceptionButtonTextKey(
		Exception exception) const {
	switch (exception) {
	case Exception::Always: return tr::lng_edit_privacy_voices_always_empty();
	case Exception::Never: return tr::lng_edit_privacy_voices_never_empty();
	}
	Unexpected("Invalid exception value.");
}

rpl::producer<QString> VoicesPrivacyController::exceptionBoxTitle(
		Exception exception) const {
	switch (exception) {
	case Exception::Always: return tr::lng_edit_privacy_voices_always_title();
	case Exception::Never: return tr::lng_edit_privacy_voices_never_title();
	}
	Unexpected("Invalid exception value.");
}

auto VoicesPrivacyController::exceptionsDescription() const
-> rpl::producer<QString> {
	return tr::lng_edit_privacy_voices_exceptions();
}

} // namespace Settings
