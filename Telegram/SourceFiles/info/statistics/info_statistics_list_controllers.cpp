/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/statistics/info_statistics_list_controllers.h"

#include "api/api_credits.h"
#include "api/api_statistics.h"
#include "boxes/peer_list_controllers.h"
#include "boxes/peer_list_widgets.h"
#include "chat_helpers/stickers_gift_box_pack.h"
#include "core/ui_integration.h" // Core::MarkedTextContext.
#include "data/data_channel.h"
#include "data/data_credits.h"
#include "data/data_session.h"
#include "data/data_stories.h"
#include "data/data_user.h"
#include "data/stickers/data_custom_emoji.h"
#include "history/history_item.h"
#include "info/channel_statistics/boosts/giveaway/boost_badge.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "main/session/session_show.h"
#include "settings/settings_credits_graphics.h" // PaintSubscriptionRightLabelCallback
#include "ui/dynamic_image.h"
#include "ui/dynamic_thumbnails.h"
#include "ui/effects/credits_graphics.h"
#include "ui/effects/outline_segments.h" // Ui::UnreadStoryOutlineGradient.
#include "ui/effects/toggle_arrow.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/popup_menu.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "styles/style_boxes.h"
#include "styles/style_color_indices.h"
#include "styles/style_credits.h"
#include "styles/style_dialogs.h" // dialogsStoriesFull.
#include "styles/style_layers.h" // boxRowPadding.
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "styles/style_statistics.h"
#include "styles/style_window.h"

namespace Info::Statistics {
namespace {

using BoostCallback = Fn<void(const Data::Boost &)>;

[[nodiscard]] PeerListRowId UniqueRowIdFromEntry(
		const Data::CreditsHistoryEntry &entry) {
	return UniqueRowIdFromString(entry.id
		+ (entry.refunded ? '1' : '0')
		+ (entry.pending ? '1' : '0')
		+ (entry.failed ? '1' : '0')
		+ (entry.in ? '1' : '0'));
}

void AddSubtitle(
		not_null<Ui::VerticalLayout*> container,
		rpl::producer<QString> title) {
	const auto &subtitlePadding = st::settingsButton.padding;
	Ui::AddSubsectionTitle(
		container,
		std::move(title),
		{ 0, -subtitlePadding.top(), 0, -subtitlePadding.bottom() });
}

[[nodiscard]] object_ptr<Ui::SettingsButton> CreateShowMoreButton(
		not_null<Ui::RpWidget*> parent,
		rpl::producer<QString> title) {
	auto owned = object_ptr<Ui::SettingsButton>(
		parent,
		std::move(title),
		st::statisticsShowMoreButton);
	Ui::AddToggleUpDownArrowToMoreButton(owned.data());
	return owned;
}

[[nodiscard]] QString FormatText(
		int value1, tr::phrase<lngtag_count> phrase1,
		int value2, tr::phrase<lngtag_count> phrase2,
		int value3, tr::phrase<lngtag_count> phrase3) {
	const auto separator = u", "_q;
	auto resultText = QString();
	if (value1 > 0) {
		resultText += phrase1(tr::now, lt_count, value1);
	}
	if (value2 > 0) {
		if (!resultText.isEmpty()) {
			resultText += separator;
		}
		resultText += phrase2(tr::now, lt_count, value2);
	}
	if (value3 > 0) {
		if (!resultText.isEmpty()) {
			resultText += separator;
		}
		resultText += phrase3(tr::now, lt_count, value3);
	}
	return resultText;
}

struct PublicForwardsDescriptor final {
	Data::PublicForwardsSlice firstSlice;
	Fn<void(Data::RecentPostId)> requestShow;
	not_null<PeerData*> peer;
	Data::RecentPostId contextId;
};

struct MembersDescriptor final {
	not_null<Main::Session*> session;
	Fn<void(not_null<PeerData*>)> showPeerInfo;
	Data::SupergroupStatistics data;
};

struct BoostsDescriptor final {
	Data::BoostsListSlice firstSlice;
	BoostCallback boostClickedCallback;
	not_null<PeerData*> peer;
};

struct CreditsDescriptor final {
	Data::CreditsStatusSlice firstSlice;
	Clicked entryClickedCallback;
	not_null<PeerData*> peer;
	bool in = false;
	bool out = false;
	bool subscription = false;
};

class PeerListRowWithFullId : public PeerListRow {
public:
	PeerListRowWithFullId(
		not_null<PeerData*> peer,
		Data::RecentPostId contextId);

	[[nodiscard]] PaintRoundImageCallback generatePaintUserpicCallback(
		bool) override;

	[[nodiscard]] Data::RecentPostId contextId() const;

private:
	const Data::RecentPostId _contextId;

};

PeerListRowWithFullId::PeerListRowWithFullId(
	not_null<PeerData*> peer,
	Data::RecentPostId contextId)
: PeerListRow(peer)
, _contextId(contextId) {
}

PaintRoundImageCallback PeerListRowWithFullId::generatePaintUserpicCallback(
		bool forceRound) {
	if (!_contextId.storyId) {
		return PeerListRow::generatePaintUserpicCallback(forceRound);
	}
	const auto peer = PeerListRow::peer();
	auto userpic = PeerListRow::ensureUserpicView();

	const auto line = st::dialogsStoriesFull.lineTwice;
	const auto penWidth = line / 2.;
	const auto offset = 1.5 * penWidth * 2;
	return [=](Painter &p, int x, int y, int outerWidth, int size) mutable {
		const auto rect = QRect(QPoint(x, y), Size(size));
		peer->paintUserpicLeft(
			p,
			userpic,
			x + offset,
			y + offset,
			outerWidth,
			size - offset * 2);
		auto hq = PainterHighQualityEnabler(p);
		auto gradient = Ui::UnreadStoryOutlineGradient();
		gradient.setStart(rect.topRight());
		gradient.setFinalStop(rect.bottomLeft());

		p.setPen(QPen(gradient, penWidth));
		p.setBrush(Qt::NoBrush);
		p.drawEllipse(rect - Margins(penWidth));
	};
}

Data::RecentPostId PeerListRowWithFullId::contextId() const {
	return _contextId;
}

class MembersController final : public PeerListController {
public:
	MembersController(MembersDescriptor d);

	Main::Session &session() const override;
	void prepare() override;
	void rowClicked(not_null<PeerListRow*> row) override;
	void loadMoreRows() override;

	void setLimit(int limit);

private:
	void addRows(int from, int to);

	const not_null<Main::Session*> _session;
	Fn<void(not_null<PeerData*>)> _showPeerInfo;
	Data::SupergroupStatistics _data;
	int _limit = 0;

};

MembersController::MembersController(MembersDescriptor d)
: _session(std::move(d.session))
, _showPeerInfo(std::move(d.showPeerInfo))
, _data(std::move(d.data)) {
}

Main::Session &MembersController::session() const {
	return *_session;
}

void MembersController::setLimit(int limit) {
	addRows(_limit, limit);
	_limit = limit;
}

void MembersController::addRows(int from, int to) {
	const auto addRow = [&](UserId userId, QString text) {
		const auto user = _session->data().user(userId);
		auto row = std::make_unique<PeerListRow>(user);
		row->setCustomStatus(std::move(text));
		delegate()->peerListAppendRow(std::move(row));
	};
	if (!_data.topSenders.empty()) {
		for (auto i = from; i < to; i++) {
			const auto &member = _data.topSenders[i];
			addRow(
				member.userId,
				FormatText(
					member.sentMessageCount,
					tr::lng_stats_member_messages,
					member.averageCharacterCount,
					tr::lng_stats_member_characters,
					0,
					{}));
		}
	} else if (!_data.topAdministrators.empty()) {
		for (auto i = from; i < to; i++) {
			const auto &admin = _data.topAdministrators[i];
			addRow(
				admin.userId,
				FormatText(
					admin.deletedMessageCount,
					tr::lng_stats_member_deletions,
					admin.bannedUserCount,
					tr::lng_stats_member_bans,
					admin.restrictedUserCount,
					tr::lng_stats_member_restrictions));
		}
	} else if (!_data.topInviters.empty()) {
		for (auto i = from; i < to; i++) {
			const auto &inviter = _data.topInviters[i];
			addRow(
				inviter.userId,
				FormatText(
					inviter.addedMemberCount,
					tr::lng_stats_member_invitations,
					0,
					{},
					0,
					{}));
		}
	}
}

void MembersController::prepare() {
}

void MembersController::loadMoreRows() {
}

void MembersController::rowClicked(not_null<PeerListRow*> row) {
	crl::on_main([=, peer = row->peer()] {
		_showPeerInfo(peer);
	});
}

class PublicForwardsController final : public PeerListController {
public:
	explicit PublicForwardsController(PublicForwardsDescriptor d);

	Main::Session &session() const override;
	void prepare() override;
	void rowClicked(not_null<PeerListRow*> row) override;
	void loadMoreRows() override;
	base::unique_qptr<Ui::PopupMenu> rowContextMenu(
		QWidget *parent,
		not_null<PeerListRow*> row) override;

private:
	void appendRow(not_null<PeerData*> peer, Data::RecentPostId contextId);
	void applySlice(const Data::PublicForwardsSlice &slice);

	const not_null<Main::Session*> _session;
	Fn<void(Data::RecentPostId)> _requestShow;

	Api::PublicForwards _api;
	Data::PublicForwardsSlice _firstSlice;
	Data::PublicForwardsSlice::OffsetToken _apiToken;

	bool _allLoaded = false;

};

PublicForwardsController::PublicForwardsController(PublicForwardsDescriptor d)
: _session(&d.peer->session())
, _requestShow(std::move(d.requestShow))
, _api(d.peer->asChannel(), d.contextId)
, _firstSlice(std::move(d.firstSlice)) {
}

Main::Session &PublicForwardsController::session() const {
	return *_session;
}

void PublicForwardsController::prepare() {
	applySlice(base::take(_firstSlice));
	delegate()->peerListRefreshRows();
}

void PublicForwardsController::loadMoreRows() {
	if (_allLoaded) {
		return;
	}
	_api.request(_apiToken, [=](const Data::PublicForwardsSlice &slice) {
		applySlice(slice);
	});
}

void PublicForwardsController::applySlice(
		const Data::PublicForwardsSlice &slice) {
	_allLoaded = slice.allLoaded;
	_apiToken = slice.token;

	for (const auto &item : slice.list) {
		if (const auto &full = item.messageId) {
			if (const auto peer = session().data().peerLoaded(full.peer)) {
				appendRow(peer, item);
			}
		} else if (const auto &full = item.storyId) {
			if (const auto story = session().data().stories().lookup(full)) {
				appendRow((*story)->peer(), item);
			}
		}
	}
	delegate()->peerListRefreshRows();
}

void PublicForwardsController::rowClicked(not_null<PeerListRow*> row) {
	const auto rowWithId = static_cast<PeerListRowWithFullId*>(row.get());
	crl::on_main([=, id = rowWithId->contextId()] { _requestShow(id); });
}

base::unique_qptr<Ui::PopupMenu> PublicForwardsController::rowContextMenu(
		QWidget *parent,
		not_null<PeerListRow*> row) {
	auto menu = base::make_unique_q<Ui::PopupMenu>(
		parent,
		st::popupMenuWithIcons);
	const auto peer = row->peer();
	const auto text = (peer->isChat() || peer->isMegagroup())
		? tr::lng_context_view_group(tr::now)
		: peer->isUser()
		? tr::lng_context_view_profile(tr::now)
		: peer->isChannel()
		? tr::lng_context_view_channel(tr::now)
		: QString();
	if (text.isEmpty()) {
		return nullptr;
	}
	menu->addAction(text, crl::guard(parent, [=, peerId = peer->id] {
		_requestShow({ .messageId = { peerId, MsgId() } });
	}), peer->isUser() ? &st::menuIconProfile : &st::menuIconInfo);
	return menu;
}

void PublicForwardsController::appendRow(
		not_null<PeerData*> peer,
		Data::RecentPostId contextId) {
	if (delegate()->peerListFindRow(peer->id.value)) {
		return;
	}

	auto row = std::make_unique<PeerListRowWithFullId>(peer, contextId);

	const auto members = peer->isChannel()
		? peer->asChannel()->membersCount()
		: 0;
	const auto views = [&] {
		if (contextId.messageId) {
			const auto message = peer->owner().message(contextId.messageId);
			return message ? message->viewsCount() : 0;
		} else if (const auto &id = contextId.storyId) {
			const auto story = peer->owner().stories().lookup(id);
			return story ? (*story)->views() : 0;
		}
		return 0;
	}();

	const auto membersText = !members
		? QString()
		: peer->isMegagroup()
		? tr::lng_chat_status_members(tr::now, lt_count_decimal, members)
		: tr::lng_chat_status_subscribers(tr::now, lt_count_decimal, members);
	const auto viewsText = views
		? tr::lng_stats_recent_messages_views({}, lt_count_decimal, views)
		: QString();
	const auto resultText = (membersText.isEmpty() && viewsText.isEmpty())
		? tr::lng_stories_no_views(tr::now)
		: (membersText.isEmpty() || viewsText.isEmpty())
		? membersText + viewsText
		: QString("%1, %2").arg(membersText, viewsText);
	row->setCustomStatus(resultText);

	delegate()->peerListAppendRow(std::move(row));
	return;
}

class BoostRow final : public PeerListRow {
public:
	BoostRow(not_null<PeerData*> peer, const Data::Boost &boost);
	BoostRow(const Data::Boost &boost);

	[[nodiscard]] const Data::Boost &boost() const;
	[[nodiscard]] QString generateName() override;

	[[nodiscard]] PaintRoundImageCallback generatePaintUserpicCallback(
		bool forceRound) override;

	int paintNameIconGetWidth(
		Painter &p,
		Fn<void()> repaint,
		crl::time now,
		int nameLeft,
		int nameTop,
		int nameWidth,
		int availableWidth,
		int outerWidth,
		bool selected) override;

	QSize rightActionSize() const override;
	QMargins rightActionMargins() const override;
	bool rightActionDisabled() const override;
	void rightActionPaint(
		Painter &p,
		int x,
		int y,
		int outerWidth,
		bool selected,
		bool actionSelected) override;

private:
	void init();
	void invalidateBadges();

	const Data::Boost _boost;
	Ui::EmptyUserpic _userpic;
	QImage _badge;
	QImage _rightBadge;
	PaintRoundImageCallback _paintUserpicCallback;

};

BoostRow::BoostRow(not_null<PeerData*> peer, const Data::Boost &boost)
: PeerListRow(peer, UniqueRowIdFromString(boost.id))
, _boost(boost)
, _userpic(Ui::EmptyUserpic::UserpicColor(0), QString()) {
	init();
}

BoostRow::BoostRow(const Data::Boost &boost)
: PeerListRow(UniqueRowIdFromString(boost.id))
, _boost(boost)
, _userpic(
	Ui::EmptyUserpic::UserpicColor(boost.credits
		? st::colorIndexOrange
		: boost.isUnclaimed
		? st::colorIndexSea
		: st::colorIndexBlue),
	QString()) {
	init();
}

void BoostRow::init() {
	if (!PeerListRow::special()) {
		_paintUserpicCallback = PeerListRow::generatePaintUserpicCallback(
			false);
	} else if (_boost.credits) {
		const auto creditsIcon = std::make_shared<QImage>();
		_paintUserpicCallback = [=](
				Painter &p,
				int x,
				int y,
				int outerWidth,
				int size) mutable {
			_userpic.paintCircle(p, x, y, outerWidth, size);
			if (creditsIcon->isNull()) {
				*creditsIcon = Ui::CreditsWhiteDoubledIcon(size, 1.);
			}
			p.drawImage(x, y, *creditsIcon);
		};
	} else {
		_paintUserpicCallback = [=](
				Painter &p,
				int x,
				int y,
				int outerWidth,
				int size) mutable {
			_userpic.paintCircle(p, x, y, outerWidth, size);
			(_boost.isUnclaimed
				? st::boostsListUnclaimedIcon
				: st::boostsListUnknownIcon).paintInCenter(
					p,
					Rect(x, y, Size(size)));
		};
	}

	invalidateBadges();
	auto status = (!PeerListRow::special() || _boost.credits)
		? tr::lng_boosts_list_status(
			tr::now,
			lt_date,
			langDayOfMonth(_boost.expiresAt.date()))
		: tr::lng_months_tiny(tr::now, lt_count, _boost.expiresAfterMonths)
			+ ' '
			+ QChar(0x2022)
			+ ' '
			+ langDayOfMonth(_boost.date.date());
	PeerListRow::setCustomStatus(std::move(status));
}

const Data::Boost &BoostRow::boost() const {
	return _boost;
}

QString BoostRow::generateName() {
	return !PeerListRow::special()
		? PeerListRow::generateName()
		: _boost.credits
		? tr::lng_giveaway_prizes_additional_credits_amount(
			tr::now,
			lt_count_decimal,
			_boost.credits)
		: _boost.isUnclaimed
		? tr::lng_boosts_list_unclaimed(tr::now)
		: tr::lng_boosts_list_pending(tr::now);
}

PaintRoundImageCallback BoostRow::generatePaintUserpicCallback(bool force) {
	return _paintUserpicCallback;
}

void BoostRow::invalidateBadges() {
	_badge = _boost.multiplier
		? CreateBadge(
			st::statisticsDetailsBottomCaptionStyle,
			QString::number(_boost.multiplier),
			st::boostsListBadgeHeight,
			st::boostsListBadgeTextPadding,
			st::premiumButtonBg2,
			st::premiumButtonFg,
			1.,
			st::boostsListMiniIconPadding,
			st::boostsListMiniIcon)
		: QImage();

	constexpr auto kBadgeBgOpacity = 0.2;
	const auto &rightColor = _boost.isGiveaway
		? st::historyPeer4UserpicBg2
		: st::historyPeer8UserpicBg2;
	const auto &rightIcon = _boost.isGiveaway
		? st::boostsListGiveawayMiniIcon
		: st::boostsListGiftMiniIcon;
	_rightBadge = ((_boost.isGift || _boost.isGiveaway) && !_boost.credits)
		? CreateBadge(
			st::boostsListRightBadgeTextStyle,
			_boost.isGiveaway
				? tr::lng_gift_link_reason_giveaway(tr::now)
				: tr::lng_gift_link_label_gift(tr::now),
			st::boostsListRightBadgeHeight,
			st::boostsListRightBadgeTextPadding,
			rightColor,
			rightColor,
			kBadgeBgOpacity,
			st::boostsListGiftMiniIconPadding,
			rightIcon)
		: QImage();
}


QSize BoostRow::rightActionSize() const {
	return _rightBadge.size() / style::DevicePixelRatio();
}

QMargins BoostRow::rightActionMargins() const {
	return st::boostsListRightBadgePadding;
}

bool BoostRow::rightActionDisabled() const {
	return true;
}

void BoostRow::rightActionPaint(
		Painter &p,
		int x,
		int y,
		int outerWidth,
		bool selected,
		bool actionSelected) {
	if (!_rightBadge.isNull()) {
		p.drawImage(x, y, _rightBadge);
	}
}

int BoostRow::paintNameIconGetWidth(
		Painter &p,
		Fn<void()> repaint,
		crl::time now,
		int nameLeft,
		int nameTop,
		int nameWidth,
		int availableWidth,
		int outerWidth,
		bool selected) {
	if (_badge.isNull()) {
		return 0;
	}
	const auto badgew = _badge.width() / style::DevicePixelRatio();
	const auto nameTooLarge = (nameWidth > availableWidth);
	const auto &padding = st::boostsListBadgePadding;
	const auto left = nameTooLarge
		? ((nameLeft + availableWidth) - badgew - padding.left())
		: (nameLeft + nameWidth + padding.right());
	p.drawImage(left, nameTop + padding.top(), _badge);
	return badgew + (nameTooLarge ? padding.left() : 0);
}

class BoostsController final : public PeerListController {
public:
	explicit BoostsController(BoostsDescriptor d);

	Main::Session &session() const override;
	void prepare() override;
	void rowClicked(not_null<PeerListRow*> row) override;
	void loadMoreRows() override;

	[[nodiscard]] bool skipRequest() const;
	void requestNext();

	[[nodiscard]] rpl::producer<int> totalBoostsValue() const;

private:
	void applySlice(const Data::BoostsListSlice &slice);

	const not_null<Main::Session*> _session;
	BoostCallback _boostClickedCallback;

	Api::Boosts _api;
	Data::BoostsListSlice _firstSlice;
	Data::BoostsListSlice::OffsetToken _apiToken;

	bool _allLoaded = false;
	bool _requesting = false;

	rpl::variable<int> _totalBoosts;

};

BoostsController::BoostsController(BoostsDescriptor d)
: _session(&d.peer->session())
, _boostClickedCallback(std::move(d.boostClickedCallback))
, _api(d.peer)
, _firstSlice(std::move(d.firstSlice)) {
	PeerListController::setStyleOverrides(&st::boostsListBox);
}

Main::Session &BoostsController::session() const {
	return *_session;
}

bool BoostsController::skipRequest() const {
	return _requesting || _allLoaded;
}

void BoostsController::requestNext() {
	_requesting = true;
	_api.requestBoosts(_apiToken, [=](const Data::BoostsListSlice &slice) {
		_requesting = false;
		applySlice(slice);
	});
}

void BoostsController::prepare() {
	applySlice(base::take(_firstSlice));
	delegate()->peerListRefreshRows();
}

void BoostsController::loadMoreRows() {
}

void BoostsController::applySlice(const Data::BoostsListSlice &slice) {
	_allLoaded = slice.allLoaded;
	_apiToken = slice.token;

	auto sumFromSlice = 0;
	for (const auto &item : slice.list) {
		sumFromSlice += item.multiplier ? item.multiplier : 1;
		auto row = [&] {
			if (item.userId && !item.isUnclaimed) {
				const auto user = session().data().user(item.userId);
				return std::make_unique<BoostRow>(user, item);
			} else {
				return std::make_unique<BoostRow>(item);
			}
		}();
		delegate()->peerListAppendRow(std::move(row));
	}
	delegate()->peerListRefreshRows();
	_totalBoosts = _totalBoosts.current() + sumFromSlice;
}

void BoostsController::rowClicked(not_null<PeerListRow*> row) {
	if (_boostClickedCallback) {
		_boostClickedCallback(
			static_cast<const BoostRow*>(row.get())->boost());
	}
}

rpl::producer<int> BoostsController::totalBoostsValue() const {
	return _totalBoosts.value();
}

class CreditsRow final : public PeerListRow {
public:
	struct Descriptor final {
		Data::CreditsHistoryEntry entry;
		Data::SubscriptionEntry subscription;
		Core::MarkedTextContext context;
		int rowHeight = 0;
		Fn<void(not_null<PeerListRow*>)> updateCallback;
	};

	CreditsRow(not_null<PeerData*> peer, const Descriptor &descriptor);
	CreditsRow(const Descriptor &descriptor);

	[[nodiscard]] const Data::CreditsHistoryEntry &entry() const;
	[[nodiscard]] const Data::SubscriptionEntry &subscription() const;
	[[nodiscard]] QString generateName() override;

	[[nodiscard]] PaintRoundImageCallback generatePaintUserpicCallback(
		bool forceRound) override;

	QSize rightActionSize() const override;
	QMargins rightActionMargins() const override;
	bool rightActionDisabled() const override;
	void rightActionPaint(
		Painter &p,
		int x,
		int y,
		int outerWidth,
		bool selected,
		bool actionSelected) override;

	void paintStatusText(
		Painter &p,
		const style::PeerListItem &st,
		int x,
		int y,
		int available,
		int outer,
		bool selected) override;

	const style::PeerListItem &computeSt(
		const style::PeerListItem &st) const override;

private:
	void init();

	const Data::CreditsHistoryEntry _entry;
	const Data::SubscriptionEntry _subscription;
	const Core::MarkedTextContext _context;
	const int _rowHeight;

	PaintRoundImageCallback _paintUserpicCallback;
	std::optional<Settings::SubscriptionRightLabel> _rightLabel;
	QString _title;
	QString _name;

	Ui::Text::String _description;
	Ui::Text::String _rightText;

	std::shared_ptr<Ui::DynamicImage> _descriptionThumbnail;
	QImage _descriptionThumbnailCache;

	base::has_weak_ptr _guard;
};

CreditsRow::CreditsRow(
	not_null<PeerData*> peer,
	const Descriptor &descriptor)
: PeerListRow(peer, UniqueRowIdFromEntry(descriptor.entry))
, _entry(descriptor.entry)
, _subscription(descriptor.subscription)
, _context(descriptor.context)
, _rowHeight(descriptor.rowHeight) {
	const auto callback = Ui::PaintPreviewCallback(
		&peer->session(),
		_entry);
	if (callback) {
		_paintUserpicCallback = callback(crl::guard(
			&_guard,
			[this, update = descriptor.updateCallback] { update(this); }));
	}
	if (!_subscription.cancelled
		&& !_subscription.expired
		&& _subscription.subscription) {
		_rightLabel = Settings::PaintSubscriptionRightLabelCallback(
			&peer->session(),
			st::boostsListBox.item,
			_subscription.subscription.credits);
	}
	init();
}

CreditsRow::CreditsRow(const Descriptor &descriptor)
: PeerListRow(UniqueRowIdFromEntry(descriptor.entry))
, _entry(descriptor.entry)
, _subscription(descriptor.subscription)
, _context(descriptor.context)
, _rowHeight(descriptor.rowHeight) {
	init();
}

void CreditsRow::init() {
	const auto isSpecial = PeerListRow::special();
	const auto name = !isSpecial
		? PeerListRow::generateName()
		: Ui::GenerateEntryName(_entry).text;
	_name = _entry.title.isEmpty()
		? name
		: (!_entry.subscriptionUntil.isNull() && !isSpecial)
		? name
		: _entry.title;
	setSkipPeerBadge(true);
	const auto description = _entry.floodSkip
		? tr::lng_credits_box_history_entry_floodskip_about(
			tr::now,
			lt_count_decimal,
			_entry.floodSkip)
		: (!_entry.subscriptionUntil.isNull() && !_entry.title.isEmpty())
		? _entry.title
		: _entry.refunded
		? tr::lng_channel_earn_history_return(tr::now)
		: _entry.pending
		? tr::lng_channel_earn_history_pending(tr::now)
		: _entry.failed
		? tr::lng_channel_earn_history_failed(tr::now)
		: !_entry.subscriptionUntil.isNull()
		? tr::lng_credits_box_history_entry_subscription(tr::now)
		: (_entry.peerType
			== Data::CreditsHistoryEntry::PeerType::PremiumBot)
		? tr::lng_credits_box_history_entry_via_premium_bot(tr::now)
		: (_entry.gift && isSpecial)
		? tr::lng_credits_box_history_entry_anonymous(tr::now)
		: (_name == name)
		? Ui::GenerateEntryName(_entry).text
		: name;
	_description.setText(st::defaultTextStyle, description);
	PeerListRow::setCustomStatus(langDateTime(_entry.date));
	if (_subscription) {
		PeerListRow::setCustomStatus((_subscription.expired
			? tr::lng_credits_subscription_status_none
			: _subscription.cancelled
			? tr::lng_credits_subscription_status_off
			: tr::lng_credits_subscription_status_on)(
				tr::now,
				lt_date,
				langDayOfMonthFull(_subscription.until.date())));
		_description.setText(st::defaultTextStyle, _subscription.title);
	}
	const auto descriptionPhotoId = (!_entry.subscriptionUntil.isNull())
		? _entry.photoId
		: _subscription.photoId;
	if (descriptionPhotoId) {
		_descriptionThumbnail = Ui::MakePhotoThumbnail(
			_context.session->data().photo(descriptionPhotoId),
			{});
		_descriptionThumbnail->subscribeToUpdates([this] {
			const auto thumbnailSide = st::defaultTextStyle.font->height;
			_descriptionThumbnailCache = Images::Round(
				_descriptionThumbnail->image(thumbnailSide),
				ImageRoundRadius::Large);
			if (_context.customEmojiRepaint) {
				_context.customEmojiRepaint();
			}
		});
	}
	auto &manager = _context.session->data().customEmojiManager();
	if (_entry) {
		constexpr auto kMinus = QChar(0x2212);
		_rightText.setMarkedText(
			st::semiboldTextStyle,
			TextWithEntities()
				.append(_entry.in ? QChar('+') : kMinus)
				.append(Lang::FormatStarsAmountDecimal(_entry.credits.abs()))
				.append(QChar(' '))
				.append(manager.creditsEmoji()),
			kMarkupTextOptions,
			_context);
	}
	if (!_paintUserpicCallback) {
		_paintUserpicCallback = _entry.stargift
			? Ui::GenerateGiftStickerUserpicCallback(
				_context.session,
				_entry.bareGiftStickerId,
				_context.customEmojiRepaint)
			: !isSpecial
			? PeerListRow::generatePaintUserpicCallback(false)
			: Ui::GenerateCreditsPaintUserpicCallback(_entry);
	}
}

const Data::CreditsHistoryEntry &CreditsRow::entry() const {
	return _entry;
}

const Data::SubscriptionEntry &CreditsRow::subscription() const {
	return _subscription;
}

QString CreditsRow::generateName() {
	return (!_entry.title.isEmpty() && !_entry.subscriptionUntil.isNull())
		? _name
		: _entry.title.isEmpty()
		? _name
		: _entry.title;
}

PaintRoundImageCallback CreditsRow::generatePaintUserpicCallback(bool force) {
	return _paintUserpicCallback;
}

[[nodiscard]] QString RightActionText(const Data::SubscriptionEntry &s) {
	return s.cancelledByBot
		? tr::lng_credits_subscription_status_off_by_bot_right(tr::now)
		: s.cancelled
		? tr::lng_credits_subscription_status_off_right(tr::now)
		: s.expired
		? tr::lng_credits_subscription_status_none_right(tr::now)
		: QString();
}

QSize CreditsRow::rightActionSize() const {
	if (_rightLabel) {
		return _rightLabel->size;
	} else if (const auto t = RightActionText(_subscription); !t.isEmpty()) {
		const auto lines = t.split('\n');
		auto maxWidth = 0;
		for (const auto &line : lines) {
			const auto width = st::contactsStatusFont->width(line);
			if (width > maxWidth) {
				maxWidth = width;
			}
		}
		return QSize(maxWidth + st::boxRowPadding.right(), _rowHeight);
	} else if (_subscription || _entry) {
		return QSize(
			_rightText.maxWidth() + st::boxRowPadding.right() / 2,
			_rowHeight);
	} else if (!_entry && !_subscription) {
		return QSize();
	}
	return QSize();
}

QMargins CreditsRow::rightActionMargins() const {
	return QMargins(0, 0, st::boxRowPadding.right(), 0);
}

bool CreditsRow::rightActionDisabled() const {
	return true;
}

void CreditsRow::rightActionPaint(
		Painter &p,
		int x,
		int y,
		int outerWidth,
		bool selected,
		bool actionSelected) {
	const auto &font = _rightText.style()->font;
	const auto rightSkip = st::boxRowPadding.right();
	if (_rightLabel) {
		return _rightLabel->draw(p, x, y, _rowHeight);
	} else if (const auto t = RightActionText(_subscription); !t.isEmpty()) {
		const auto &statusFont = st::contactsStatusFont;
		y += _rowHeight / 2;
		p.setFont(statusFont);
		p.setPen(st::attentionButtonFg);

		const auto lines = t.split('\n');
		if (lines.size() > 1) {
			const auto rect = QRect(x, 0, outerWidth - x, _rowHeight);
			const auto lineHeight = statusFont->height;
			const auto totalHeight = lines.size() * lineHeight;
			auto startY = rect.top()
				+ (rect.height() - totalHeight) / 2
				+ statusFont->ascent;

			for (const auto &line : lines) {
				const auto lineWidth = statusFont->width(line);
				const auto startX = rect.left()
					+ (rect.width() - lineWidth) / 2;
				p.drawText(startX, startY, line);
				startY += lineHeight;
			}
			return;
		}
		p.drawTextRight(rightSkip, y - statusFont->height / 2, outerWidth, t);
		return;
	}
	y += _rowHeight / 2;
	p.setPen(_entry.pending
		? st::creditsStroke
		: _entry.in
		? st::boxTextFgGood
		: st::menuIconAttentionColor);
	_rightText.draw(p, Ui::Text::PaintContext{
		.position = QPoint(
			outerWidth - _rightText.maxWidth() - rightSkip,
			y - font->height / 2),
		.outerWidth = outerWidth,
		.availableWidth = outerWidth,
	});
}

void CreditsRow::paintStatusText(
		Painter &p,
		const style::PeerListItem &st,
		int x,
		int y,
		int available,
		int outer,
		bool selected) {
	PeerListRow::paintStatusText(p, st, x, y, available, outer, selected);
	p.setPen(st.nameFg);
	if (!_descriptionThumbnailCache.isNull()) {
		const auto thumbnailSide = _descriptionThumbnailCache.width()
			/ style::DevicePixelRatio();
		const auto thumbnailSpace = st::lineWidth * 4 + thumbnailSide;
		p.drawImage(
			x,
			y - thumbnailSide,
			_descriptionThumbnailCache);
		x += thumbnailSpace;
		outer -= thumbnailSpace;
		available -= thumbnailSpace;
	}
	_description.draw(p, {
		.position = QPoint(x, y - _description.minHeight()),
		.outerWidth = outer,
		.availableWidth = available,
		.elisionLines = 1,
	});
}

const style::PeerListItem &CreditsRow::computeSt(
		const style::PeerListItem &st) const {
	return (!_subscription || !_subscription.title.isEmpty())
		? st
		: st::boostsListBox.item;
}

class CreditsController final : public PeerListController {
public:
	explicit CreditsController(CreditsDescriptor d);

	Main::Session &session() const override;
	void prepare() override;
	void rowClicked(not_null<PeerListRow*> row) override;
	void loadMoreRows() override;

	[[nodiscard]] bool skipRequest() const;
	void requestNext();

	[[nodiscard]] rpl::producer<bool> allLoadedValue() const;

private:
	void applySlice(const Data::CreditsStatusSlice &slice);

	const not_null<Main::Session*> _session;
	const bool _subscription;
	Clicked _entryClickedCallback;

	Api::CreditsHistory _api;
	Data::CreditsStatusSlice _firstSlice;
	Data::CreditsStatusSlice::OffsetToken _apiToken;
	Core::MarkedTextContext _context;

	rpl::variable<bool> _allLoaded = false;
	bool _requesting = false;

};

CreditsController::CreditsController(CreditsDescriptor d)
: _session(&d.peer->session())
, _subscription(d.subscription)
, _entryClickedCallback(std::move(d.entryClickedCallback))
, _api(d.peer, d.in, d.out)
, _firstSlice(std::move(d.firstSlice))
, _context(Core::MarkedTextContext{
	.session = _session,
	.customEmojiRepaint = [] {},
}) {
	PeerListController::setStyleOverrides(&st::creditsHistoryEntriesList);
}

Main::Session &CreditsController::session() const {
	return *_session;
}

bool CreditsController::skipRequest() const {
	return _requesting || _allLoaded.current();
}

void CreditsController::requestNext() {
	_requesting = true;
	const auto done = [=](const Data::CreditsStatusSlice &s) {
		_requesting = false;
		applySlice(s);
	};
	if (_subscription) {
		return _api.requestSubscriptions(_apiToken, done);
	}
	_api.request(_apiToken, done);
}

void CreditsController::prepare() {
	applySlice(base::take(_firstSlice));
	delegate()->peerListRefreshRows();
}

void CreditsController::loadMoreRows() {
}

void CreditsController::applySlice(const Data::CreditsStatusSlice &slice) {
	_allLoaded = slice.allLoaded;
	_apiToken = _subscription ? slice.tokenSubscriptions : slice.token;

	auto create = [&](
			const Data::CreditsHistoryEntry &i,
			const Data::SubscriptionEntry &s) {
		const auto descriptor = CreditsRow::Descriptor{
			.entry = i,
			.subscription = s,
			.context = _context,
			.rowHeight = ((!s || !s.title.isEmpty())
				? computeListSt().item
				: st::boostsListBox.item).height,
			.updateCallback = [=](not_null<PeerListRow*> row) {
				delegate()->peerListUpdateRow(row);
			},
		};
		if (i.bareActorId) {
			const auto peer = session().data().peer(PeerId(i.bareActorId));
			return std::make_unique<CreditsRow>(peer, descriptor);
		} else if (const auto peerId = PeerId(i.barePeerId + s.barePeerId)) {
			const auto peer = session().data().peer(peerId);
			return std::make_unique<CreditsRow>(peer, descriptor);
		} else {
			return std::make_unique<CreditsRow>(descriptor);
		}
	};

	auto giftPacksRequested = false;
	for (const auto &item : slice.list) {
		if (item.bareGiveawayMsgId && !giftPacksRequested) {
			giftPacksRequested = true;
			session().giftBoxStickersPacks().load();
		}
		delegate()->peerListAppendRow(create(item, {}));
	}
	for (const auto &item : slice.subscriptions) {
		delegate()->peerListAppendRow(create({}, item));
	}
	delegate()->peerListRefreshRows();
}

void CreditsController::rowClicked(not_null<PeerListRow*> row) {
	if (_entryClickedCallback) {
		const auto r = static_cast<const CreditsRow*>(row.get());
		_entryClickedCallback(r->entry(), r->subscription());
	}
}

rpl::producer<bool> CreditsController::allLoadedValue() const {
	return _allLoaded.value();
}

} // namespace

void AddPublicForwards(
		const Data::PublicForwardsSlice &firstSlice,
		not_null<Ui::VerticalLayout*> container,
		Fn<void(Data::RecentPostId)> requestShow,
		not_null<PeerData*> peer,
		Data::RecentPostId contextId) {
	if (!peer->isChannel()) {
		return;
	}

	struct State final {
		State(PublicForwardsDescriptor d) : controller(std::move(d)) {
		}
		PeerListContentDelegateSimple delegate;
		PublicForwardsController controller;
	};
	auto d = PublicForwardsDescriptor{
		firstSlice,
		std::move(requestShow),
		peer,
		contextId,
	};
	const auto state = container->lifetime().make_state<State>(std::move(d));

	if (const auto total = firstSlice.total; total > 0) {
		AddSubtitle(
			container,
			tr::lng_stats_overview_message_public_share(
				lt_count_decimal,
				rpl::single<float64>(total)));
	}

	state->delegate.setContent(container->add(
		object_ptr<PeerListContent>(container, &state->controller)));
	state->controller.setDelegate(&state->delegate);
}

void AddMembersList(
		Data::SupergroupStatistics data,
		not_null<Ui::VerticalLayout*> container,
		Fn<void(not_null<PeerData*>)> showPeerInfo,
		not_null<PeerData*> peer,
		rpl::producer<QString> title) {
	if (!peer->isMegagroup()) {
		return;
	}
	const auto max = !data.topSenders.empty()
		? data.topSenders.size()
		: !data.topAdministrators.empty()
		? data.topAdministrators.size()
		: !data.topInviters.empty()
		? data.topInviters.size()
		: 0;
	if (!max) {
		return;
	}

	constexpr auto kPerPage = 40;
	struct State final {
		State(MembersDescriptor d) : controller(std::move(d)) {
		}
		PeerListContentDelegateSimple delegate;
		MembersController controller;
		int limit = 0;
	};
	auto d = MembersDescriptor{
		&peer->session(),
		std::move(showPeerInfo),
		std::move(data),
	};
	const auto state = container->lifetime().make_state<State>(std::move(d));

	AddSubtitle(container, std::move(title));

	state->delegate.setContent(container->add(
		object_ptr<PeerListContent>(container, &state->controller)));
	state->controller.setDelegate(&state->delegate);

	const auto wrap = AddShowMoreButton(
		container,
		tr::lng_stories_show_more());

	const auto showMore = [=] {
		state->limit = std::min(int(max), state->limit + kPerPage);
		state->controller.setLimit(state->limit);
		if (state->limit == max) {
			wrap->toggle(false, anim::type::instant);
		}
		container->resizeToWidth(container->width());
	};
	wrap->entity()->setClickedCallback(showMore);
	showMore();
}

void AddBoostsList(
		const Data::BoostsListSlice &firstSlice,
		not_null<Ui::VerticalLayout*> container,
		BoostCallback boostClickedCallback,
		not_null<PeerData*> peer,
		rpl::producer<QString> title) {
	const auto max = firstSlice.multipliedTotal;
	struct State final {
		State(BoostsDescriptor d) : controller(std::move(d)) {
		}
		PeerListContentDelegateSimple delegate;
		BoostsController controller;
	};
	auto d = BoostsDescriptor{ firstSlice, boostClickedCallback, peer };
	const auto state = container->lifetime().make_state<State>(std::move(d));

	state->delegate.setContent(container->add(
		object_ptr<PeerListContent>(container, &state->controller)));
	state->controller.setDelegate(&state->delegate);

	const auto wrap = AddShowMoreButton(
		container,
		(firstSlice.token.gifts
			? tr::lng_boosts_show_more_gifts
			: tr::lng_boosts_show_more_boosts)(
				lt_count,
				state->controller.totalBoostsValue(
				) | rpl::map(max - rpl::mappers::_1) | tr::to_count()));

	const auto showMore = [=] {
		if (!state->controller.skipRequest()) {
			state->controller.requestNext();
			container->resizeToWidth(container->width());
		}
	};
	wrap->toggleOn(
		state->controller.totalBoostsValue(
		) | rpl::map(rpl::mappers::_1 > 0 && rpl::mappers::_1 < max),
		anim::type::instant);
	wrap->entity()->setClickedCallback(showMore);
}

void AddCreditsHistoryList(
		std::shared_ptr<Main::SessionShow> show,
		const Data::CreditsStatusSlice &firstSlice,
		not_null<Ui::VerticalLayout*> container,
		Clicked callback,
		not_null<PeerData*> bot,
		bool in,
		bool out,
		bool subs) {
	struct State final {
		State(CreditsDescriptor d) : controller(std::move(d)) {
		}
		std::optional<PeerListContentDelegateShow> creditsDelegate;
		std::optional<PeerListWidgetsDelegate> subscriptionDelegate;
		CreditsController controller;
	};
	const auto state = container->lifetime().make_state<State>(
		CreditsDescriptor{ firstSlice, callback, bot, in, out, subs });
	if (subs) {
		state->subscriptionDelegate.emplace();
		state->subscriptionDelegate->setUiShow(show);
		state->subscriptionDelegate->setContent(container->add(
			object_ptr<PeerListWidgets>(container, &state->controller)));
		state->controller.setDelegate(&(*state->subscriptionDelegate));
	} else {
		state->creditsDelegate.emplace(show);
		state->creditsDelegate->setContent(container->add(
			object_ptr<PeerListContent>(container, &state->controller)));
		state->controller.setDelegate(&(*state->creditsDelegate));
	}

	const auto wrap = container->add(
		object_ptr<Ui::SlideWrap<Ui::SettingsButton>>(
			container,
			CreateShowMoreButton(container, tr::lng_stories_show_more())),
		subs
			? QMargins()
			: QMargins(0, -st::settingsButton.padding.top(), 0, 0));

	const auto showMore = [=] {
		if (!state->controller.skipRequest()) {
			state->controller.requestNext();
			container->resizeToWidth(container->width());
		}
	};
	wrap->toggleOn(
		state->controller.allLoadedValue() | rpl::map(!rpl::mappers::_1),
		anim::type::instant);
	wrap->entity()->setClickedCallback(showMore);
}

not_null<Ui::SlideWrap<Ui::SettingsButton>*> AddShowMoreButton(
		not_null<Ui::VerticalLayout*> container,
		rpl::producer<QString> title) {
	return container->add(
		object_ptr<Ui::SlideWrap<Ui::SettingsButton>>(
			container,
			CreateShowMoreButton(container, std::move(title))),
		{ 0, -st::settingsButton.padding.top(), 0, 0 });
}

} // namespace Info::Statistics
