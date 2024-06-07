/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/settings_privacy_controllers.h"

#include "api/api_global_privacy.h"
#include "api/api_peer_photo.h"
#include "apiwrap.h"
#include "base/call_delayed.h"
#include "base/event_filter.h"
#include "base/unixtime.h"
#include "boxes/abstract_box.h"
#include "boxes/peer_list_controllers.h"
#include "boxes/peers/peer_short_info_box.h"
#include "boxes/peers/prepare_short_info_box.h"
#include "calls/calls_instance.h"
#include "core/application.h"
#include "data/data_changes.h"
#include "data/data_file_origin.h"
#include "data/data_peer_values.h" // Data::AmPremiumValue.
#include "data/data_photo_media.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "data/data_user_photos.h" // UserPhotosViewer.
#include "editor/photo_editor_common.h"
#include "editor/photo_editor_layer_widget.h"
#include "history/admin_log/history_admin_log_item.h"
#include "history/history.h"
#include "history/history_item_components.h"
#include "history/view/history_view_message.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "settings/settings_premium.h"
#include "settings/settings_privacy_security.h"
#include "ui/boxes/confirm_box.h"
#include "ui/chat/chat_style.h"
#include "ui/chat/chat_theme.h"
#include "ui/painter.h"
#include "ui/vertical_list.h"
#include "ui/text/format_values.h" // Ui::FormatPhone
#include "ui/text/text_utilities.h"
#include "ui/toast/toast.h"
#include "ui/widgets/checkbox.h"
#include "ui/wrap/slide_wrap.h"
#include "window/section_widget.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "styles/style_chat.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_settings.h"
#include "styles/style_info.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"

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
		MTPint(), // from_boosts_applied
		peerToMTP(history->peer->id),
		MTPPeer(), // saved_peer_id
		MTP_messageFwdHeader(
			MTP_flags(MTPDmessageFwdHeader::Flag::f_from_id),
			peerToMTP(history->session().userPeerId()),
			MTPstring(), // from_name
			MTP_int(base::unixtime::now()),
			MTPint(), // channel_post
			MTPstring(), // post_author
			MTPPeer(), // saved_from_peer
			MTPint(), // saved_from_msg_id
			MTPPeer(), // saved_from_id
			MTPstring(), // saved_from_name
			MTPint(), // saved_date
			MTPstring()), // psa_type
		MTPlong(), // via_bot_id
		MTPlong(), // via_business_bot_id
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
		MTPint(), // ttl_period
		MTPint(), // quick_reply_shortcut_id
		MTPlong(), // effect
		MTPFactCheck()
	).match([&](const MTPDmessage &data) {
		return history->makeMessage(
			history->nextNonHistoryEntryId(),
			data,
			MessageFlag::FakeHistoryItem);
	}, [](auto &&) -> not_null<HistoryItem*> {
		Unexpected("Type in GenerateForwardedItem.");
	});

	return AdminLog::OwnedItem(delegate, item);
}

struct ForwardedTooltip {
	QRect geometry;
	Fn<void(QPainter&)> paint;
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
		case Option::CloseFriends:
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

	const auto rounded = std::make_shared<Ui::RoundRect>(
		ImageRoundRadius::Large,
		st::toastBg);
	const auto paint = [=](QPainter &p) {
		p.translate(-origin);

		rounded->paint(p, geometry);

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
	const auto window = _window;
	crl::on_main(window, [=] {
		window->showPeerHistory(peer);
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
		Box<PeerListBox>(std::move(controller), std::move(initBox)));
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
		} else if (!user->username().isEmpty()) {
			return '@' + user->username();
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
	Ui::AddSkip(container);
	Ui::AddSubsectionTitle(
		container,
		tr::lng_edit_privacy_phone_number_find());
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
	Ui::AddSkip(
		container,
		st::defaultVerticalListSkip + st::settingsPrivacySkipTop);
	Ui::AddDivider(container);

	using namespace rpl::mappers;
	widget->toggleOn(_phoneNumberOption.value(
	) | rpl::map(
		_1 == Option::Nobody
	));

	_saveAdditional = [=] {
		controller->session().api().userPrivacy().save(
			Api::UserPrivacy::Key::AddedByPhone,
			Api::UserPrivacy::Rule{ .option = group->current() });
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

object_ptr<Ui::RpWidget> LastSeenPrivacyController::setupBelowWidget(
		not_null<Window::SessionController*> controller,
		not_null<QWidget*> parent,
		rpl::producer<Option> option) {
	using namespace rpl::mappers;

	auto result = object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
		parent,
		object_ptr<Ui::VerticalLayout>(parent));

	_option = std::move(option);

	const auto content = result->entity();

	Ui::AddSkip(content);

	const auto privacy = &controller->session().api().globalPrivacy();
	content->add(object_ptr<Ui::SettingsButton>(
		content,
		tr::lng_edit_lastseen_hide_read_time(),
		st::settingsButtonNoIcon
	))->toggleOn(privacy->hideReadTime())->toggledValue(
	) | rpl::start_with_next([=](bool value) {
		_hideReadTime = value;
	}, content->lifetime());

	Ui::AddSkip(content);
	Ui::AddDividerText(
		content,
		tr::lng_edit_lastseen_hide_read_time_about());
	if (!controller->session().premium()) {
		Ui::AddSkip(content);
		content->add(object_ptr<Ui::SettingsButton>(
			content,
			tr::lng_edit_lastseen_subscribe(),
			st::settingsButtonLightNoIcon
		))->setClickedCallback([=] {
			Settings::ShowPremium(controller, u"lastseen"_q);
		});
		Ui::AddSkip(content);
		Ui::AddDividerText(
			content,
			tr::lng_edit_lastseen_subscribe_about());
	}

	result->toggleOn(rpl::combine(
		_option.value(),
		_exceptionsNever.value(),
		(_1 != Option::Everyone) || (_2 > 0)));

	return result;
}

void LastSeenPrivacyController::handleExceptionsChange(
		Exception exception,
		rpl::producer<int> value) {
	if (exception == Exception::Never) {
		_exceptionsNever = std::move(value);
	}
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

void LastSeenPrivacyController::saveAdditional() {
	if (_option.current() == Option::Everyone
		&& !_exceptionsNever.current()) {
		return;
	}
	const auto privacy = &_session->api().globalPrivacy();
	if (privacy->hideReadTimeCurrent() != _hideReadTime) {
		privacy->updateHideReadTime(_hideReadTime);
	}
}

UserPrivacy::Key GroupsInvitePrivacyController::key() const {
	return Key::Invites;
}

rpl::producer<QString> GroupsInvitePrivacyController::title() const {
	return tr::lng_edit_privacy_groups_title();
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

bool GroupsInvitePrivacyController::allowPremiumsToggle(
		Exception exception) const {
	return (exception == Exception::Always);
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
		not_null<QWidget*> parent,
		rpl::producer<Option> option) {
	auto result = object_ptr<Ui::VerticalLayout>(parent);
	const auto content = result.data();

	Ui::AddSkip(content, st::settingsPeerToPeerSkip);
	Ui::AddSubsectionTitle(
		content,
		tr::lng_settings_calls_peer_to_peer_title());
	Settings::AddPrivacyButton(
		controller,
		content,
		tr::lng_settings_calls_peer_to_peer_button(),
		{ &st::menuIconNetwork },
		UserPrivacy::Key::CallsPeer2Peer,
		[] { return std::make_unique<CallsPeer2PeerPrivacyController>(); },
		&st::settingsButton);
	Ui::AddSkip(content);

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
	case Option::Everyone:
		return tr::lng_edit_privacy_calls_p2p_everyone(tr::now);
	case Option::Contacts:
		return tr::lng_edit_privacy_calls_p2p_contacts(tr::now);
	case Option::CloseFriends:
		return tr::lng_edit_privacy_close_friends(tr::now); // unused
	case Option::Nobody:
		return tr::lng_edit_privacy_calls_p2p_nobody(tr::now);
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
, _chatStyle(
	std::make_unique<Ui::ChatStyle>(
		controller->session().colorIndicesValue())) {
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
		not_null<Window::SessionController*> controller,
		not_null<QWidget*> parent,
		rpl::producer<Option> optionValue,
		not_null<QWidget*> outerContainer) {
	using namespace rpl::mappers;

	auto message = GenerateForwardedItem(
		delegate(),
		controller->session().data().history(
			PeerData::kServiceNotificationsId),
		tr::lng_edit_privacy_forwards_sample_message(tr::now));
	const auto view = message.get();

	auto result = object_ptr<Ui::PaddingWrap<Ui::RpWidget>>(
		parent,
		object_ptr<Ui::RpWidget>(parent),
		style::margins(
			0,
			st::defaultVerticalListSkip,
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
			auto p = QPainter(state->tooltip.get());
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
			controller,
			controller->defaultChatTheme().get(), // #TODO themes
			widget,
			rect);

		Painter p(widget);
		const auto theme = controller->defaultChatTheme().get();
		auto context = theme->preparePaintContext(
			_chatStyle.get(),
			widget->rect(),
			widget->rect(),
			controller->isGifPausedAtLeastFor(
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

rpl::producer<QString> ProfilePhotoPrivacyController::optionsTitleKey() const {
	return tr::lng_edit_privacy_profile_photo_header();
}

object_ptr<Ui::RpWidget> ProfilePhotoPrivacyController::setupAboveWidget(
		not_null<Window::SessionController*> controller,
		not_null<QWidget*> parent,
		rpl::producer<Option> optionValue,
		not_null<QWidget*> outerContainer) {
	_option = std::move(optionValue);
	return nullptr;
}

object_ptr<Ui::RpWidget> ProfilePhotoPrivacyController::setupMiddleWidget(
		not_null<Window::SessionController*> controller,
		not_null<QWidget*> parent,
		rpl::producer<Option> optionValue) {
	const auto self = controller->session().user();
	auto widget = object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
		parent,
		object_ptr<Ui::VerticalLayout>(parent));

	const auto container = widget->entity();
	struct State {
		void updatePhoto(QImage &&image, bool local) {
			auto result = image.scaled(
				userpicSize * style::DevicePixelRatio(),
				Qt::KeepAspectRatio,
				Qt::SmoothTransformation);
			result = Images::Round(
				std::move(result),
				ImageRoundRadius::Ellipse);
			result.setDevicePixelRatio(style::DevicePixelRatio());
			(local ? localPhoto : photo) = std::move(result);
			if (local) {
				localOriginal = std::move(image);
			}
			hasPhoto.fire(!localPhoto.isNull() || !photo.isNull());
		}

		rpl::event_stream<bool> hasPhoto;
		rpl::variable<bool> hiddenByUser = false;
		rpl::variable<QString> setUserpicButtonText;
		QSize userpicSize;
		QImage photo;
		QImage localPhoto;
		QImage localOriginal;
	};
	const auto state = container->lifetime().make_state<State>();
	state->userpicSize = QSize(
		st::inviteLinkUserpics.size,
		st::inviteLinkUserpics.size);

	Ui::AddSkip(container);
	const auto setUserpicButton = AddButtonWithIcon(
		container,
		state->setUserpicButtonText.value(),
		st::settingsButtonLight,
		{ &st::menuBlueIconPhotoSet });
	const auto &stRemoveButton = st::settingsAttentionButtonWithIcon;
	const auto removeButton = container->add(
		object_ptr<Ui::SlideWrap<Ui::SettingsButton>>(
			container,
			object_ptr<Ui::SettingsButton>(
				parent,
				tr::lng_edit_privacy_profile_photo_public_remove(),
				stRemoveButton)));
	Ui::AddSkip(container);
	Ui::AddDividerText(
		container,
		tr::lng_edit_privacy_profile_photo_public_about());

	const auto userpic = Ui::CreateChild<Ui::RpWidget>(
		removeButton->entity());
	userpic->resize(state->userpicSize);
	userpic->paintRequest(
	) | rpl::start_with_next([=](const QRect &r) {
		auto p = QPainter(userpic);
		p.fillRect(r, Qt::transparent);
		if (!state->localPhoto.isNull()) {
			p.drawImage(0, 0, state->localPhoto);
		} else if (!state->photo.isNull()) {
			p.drawImage(0, 0, state->photo);
		}
	}, userpic->lifetime());
	removeButton->entity()->heightValue(
	) | rpl::start_with_next([=,
			left = stRemoveButton.iconLeft,
			width = st::menuBlueIconPhotoSet.width()](int height) {
		userpic->moveToLeft(
			left + (width - userpic->width()) / 2,
			(height - userpic->height()) / 2);
	}, userpic->lifetime());
	removeButton->toggleOn(rpl::combine(
		state->hasPhoto.events_starting_with(false),
		state->hiddenByUser.value()
	) | rpl::map(rpl::mappers::_1 && !rpl::mappers::_2));

	(
		PrepareShortInfoFallbackUserpic(self, st::shortInfoCover).value
	) | rpl::start_with_next([=](PeerShortInfoUserpic info) {
		state->updatePhoto(base::take(info.photo), false);
		userpic->update();
	}, userpic->lifetime());
	setUserpicButton->setClickedCallback([=] {
		base::call_delayed(
			st::settingsButton.ripple.hideDuration,
			crl::guard(container, [=] {
				using namespace Editor;
				PrepareProfilePhotoFromFile(
					container,
					&controller->window(),
					{
						.confirm = tr::lng_profile_set_photo_button(tr::now),
						.cropType = EditorData::CropType::Ellipse,
						.keepAspectRatio = true,
					},
					[=](QImage &&image) {
						state->updatePhoto(std::move(image), true);
						state->hiddenByUser = false;
						userpic->update();
					});
			}));
	});
	removeButton->entity()->setClickedCallback([=] {
		state->hiddenByUser = true;
	});
	state->setUserpicButtonText = removeButton->toggledValue(
	) | rpl::map([](bool toggled) {
		return !toggled
			? tr::lng_edit_privacy_profile_photo_public_set()
			: tr::lng_edit_privacy_profile_photo_public_update();
	}) | rpl::flatten_latest();

	_saveAdditional = [=] {
		if (removeButton->isHidden()) {
			if (const auto photoId = SyncUserFallbackPhotoViewer(self)) {
				if (const auto photo = self->owner().photo(*photoId)) {
					controller->session().api().peerPhoto().clear(photo);
				}
			}
		} else if (!state->localOriginal.isNull()) {
			controller->session().api().peerPhoto().uploadFallback(
				self,
				{ base::take(state->localOriginal) });
		}
	};

	widget->toggleOn(rpl::combine(
		std::move(optionValue),
		_exceptionsNever.value()
	) | rpl::map(rpl::mappers::_1 != Option::Everyone || rpl::mappers::_2));

	return widget;
}

void ProfilePhotoPrivacyController::saveAdditional() {
	if (_saveAdditional) {
		_saveAdditional();
	}
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
	return _option.value(
	) | rpl::map([](Option option) {
		switch (option) {
		case Option::Everyone:
			return tr::lng_edit_privacy_forwards_exceptions_everyone();
		case Option::Contacts:
		case Option::CloseFriends:
			return tr::lng_edit_privacy_forwards_exceptions();
		case Option::Nobody:
			return tr::lng_edit_privacy_forwards_exceptions_nobody();
		}
		Unexpected("Option value in exceptionsDescription.");
	}) | rpl::flatten_latest();
}


void ProfilePhotoPrivacyController::handleExceptionsChange(
		Exception exception,
		rpl::producer<int> value) {
	if (exception == Exception::Never) {
		_exceptionsNever = std::move(value);
	}
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

object_ptr<Ui::RpWidget> VoicesPrivacyController::setupBelowWidget(
		not_null<Window::SessionController*> controller,
		not_null<QWidget*> parent,
		rpl::producer<Option> option) {
	using namespace rpl::mappers;

	auto result = object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
		parent,
		object_ptr<Ui::VerticalLayout>(parent));
	result->toggleOn(
		Data::AmPremiumValue(&controller->session()) | rpl::map(!_1),
		anim::type::instant);

	const auto content = result->entity();

	Ui::AddSkip(content);
	Settings::AddButtonWithIcon(
		content,
		tr::lng_messages_privacy_premium_button(),
		st::messagePrivacySubscribe,
		{ .icon = &st::menuBlueIconPremium }
	)->setClickedCallback([=] {
		Settings::ShowPremium(
			controller,
			u"voice_restrictions_require_premium"_q);
	});
	Ui::AddSkip(content);
	Ui::AddDividerText(content, tr::lng_messages_privacy_premium_about());

	return result;
}

Fn<void()> VoicesPrivacyController::premiumClickedCallback(
		Option option,
		not_null<Window::SessionController*> controller) {
	if (option == Option::Everyone) {
		return nullptr;
	}
	const auto showToast = [=] {
		auto link = Ui::Text::Link(
			Ui::Text::Semibold(
				tr::lng_settings_privacy_premium_link(tr::now)));
		_toastInstance = controller->showToast({
			.text = tr::lng_settings_privacy_premium(
				tr::now,
				lt_link,
				link,
				Ui::Text::WithEntities),
			.st = &st::defaultMultilineToast,
			.duration = Ui::Toast::kDefaultDuration * 2,
			.multiline = true,
			.filter = crl::guard(&controller->session(), [=](
					const ClickHandlerPtr &,
					Qt::MouseButton button) {
				if (button == Qt::LeftButton) {
					if (const auto strong = _toastInstance.get()) {
						strong->hideAnimated();
						_toastInstance = nullptr;
						Settings::ShowPremium(
							controller,
							u"voice_restrictions_require_premium"_q);
						return true;
					}
				}
				return false;
			}),
		});
	};

	return showToast;
}

UserPrivacy::Key AboutPrivacyController::key() const {
	return Key::About;
}

rpl::producer<QString> AboutPrivacyController::title() const {
	return tr::lng_edit_privacy_about_title();
}

rpl::producer<QString> AboutPrivacyController::optionsTitleKey() const {
	return tr::lng_edit_privacy_about_header();
}

rpl::producer<QString> AboutPrivacyController::exceptionButtonTextKey(
		Exception exception) const {
	switch (exception) {
	case Exception::Always: return tr::lng_edit_privacy_about_always_empty();
	case Exception::Never: return tr::lng_edit_privacy_about_never_empty();
	}
	Unexpected("Invalid exception value.");
}

rpl::producer<QString> AboutPrivacyController::exceptionBoxTitle(
		Exception exception) const {
	switch (exception) {
	case Exception::Always: return tr::lng_edit_privacy_about_always_title();
	case Exception::Never: return tr::lng_edit_privacy_about_never_title();
	}
	Unexpected("Invalid exception value.");
}

auto AboutPrivacyController::exceptionsDescription() const
-> rpl::producer<QString> {
	return tr::lng_edit_privacy_birthday_exceptions();
}

UserPrivacy::Key BirthdayPrivacyController::key() const {
	return Key::Birthday;
}

rpl::producer<QString> BirthdayPrivacyController::title() const {
	return tr::lng_edit_privacy_birthday_title();
}

rpl::producer<QString> BirthdayPrivacyController::optionsTitleKey() const {
	return tr::lng_edit_privacy_birthday_header();
}

rpl::producer<QString> BirthdayPrivacyController::exceptionButtonTextKey(
	Exception exception) const {
	switch (exception) {
	case Exception::Always: return tr::lng_edit_privacy_birthday_always_empty();
	case Exception::Never: return tr::lng_edit_privacy_birthday_never_empty();
	}
	Unexpected("Invalid exception value.");
}

rpl::producer<QString> BirthdayPrivacyController::exceptionBoxTitle(
	Exception exception) const {
	switch (exception) {
	case Exception::Always: return tr::lng_edit_privacy_birthday_always_title();
	case Exception::Never: return tr::lng_edit_privacy_birthday_never_title();
	}
	Unexpected("Invalid exception value.");
}

auto BirthdayPrivacyController::exceptionsDescription() const
-> rpl::producer<QString> {
	return tr::lng_edit_privacy_birthday_exceptions();
}

object_ptr<Ui::RpWidget> BirthdayPrivacyController::setupAboveWidget(
		not_null<Window::SessionController*> controller,
		not_null<QWidget*> parent,
		rpl::producer<Option> optionValue,
		not_null<QWidget*> outerContainer) {
	const auto session = &controller->session();
	const auto user = session->user();
	auto result = object_ptr<Ui::SlideWrap<Ui::DividerLabel>>(
		parent,
		object_ptr<Ui::DividerLabel>(
			parent,
			object_ptr<Ui::FlatLabel>(
				parent,
				tr::lng_edit_privacy_birthday_yet(
					lt_link,
					tr::lng_edit_privacy_birthday_yet_link(
					) | Ui::Text::ToLink("internal:edit_birthday"),
					Ui::Text::WithEntities),
				st::boxDividerLabel),
			st::defaultBoxDividerLabelPadding));
	result->toggleOn(session->changes().peerFlagsValue(
		user,
		Data::PeerUpdate::Flag::Birthday
	) | rpl::map([=] {
		return !user->birthday();
	}));
	result->finishAnimating();
	return result;
}

} // namespace Settings
