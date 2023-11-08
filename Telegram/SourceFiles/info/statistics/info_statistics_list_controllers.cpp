/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/statistics/info_statistics_list_controllers.h"

#include "api/api_statistics.h"
#include "boxes/peer_list_controllers.h"
#include "data/data_boosts.h"
#include "data/data_channel.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/history_item.h"
#include "info/boosts/giveaway/boost_badge.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "settings/settings_common.h"
#include "ui/effects/toggle_arrow.h"
#include "ui/empty_userpic.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "ui/widgets/buttons.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "styles/style_settings.h"
#include "styles/style_statistics.h"
#include "styles/style_window.h"

namespace Info::Statistics {
namespace {

using BoostCallback = Fn<void(const Data::Boost &)>;
constexpr auto kColorIndexUnclaimed = int(3);
constexpr auto kColorIndexPending = int(4);

void AddArrow(not_null<Ui::RpWidget*> parent) {
	const auto arrow = Ui::CreateChild<Ui::RpWidget>(parent.get());
	arrow->paintRequest(
	) | rpl::start_with_next([=](const QRect &r) {
		auto p = QPainter(arrow);

		const auto path = Ui::ToggleUpDownArrowPath(
			st::statisticsShowMoreButtonArrowSize,
			st::statisticsShowMoreButtonArrowSize,
			st::statisticsShowMoreButtonArrowSize,
			st::mainMenuToggleFourStrokes,
			0.);

		auto hq = PainterHighQualityEnabler(p);
		p.fillPath(path, st::lightButtonFg);
	}, arrow->lifetime());
	arrow->resize(Size(st::statisticsShowMoreButtonArrowSize * 2));
	arrow->move(st::statisticsShowMoreButtonArrowPosition);
	arrow->show();
}

void AddSubsectionTitle(
		not_null<Ui::VerticalLayout*> container,
		rpl::producer<QString> title) {
	const auto &subtitlePadding = st::settingsButton.padding;
	::Settings::AddSubsectionTitle(
		container,
		std::move(title),
		{ 0, -subtitlePadding.top(), 0, -subtitlePadding.bottom() });
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

struct Descriptor final {
	Data::PublicForwardsSlice firstSlice;
	Fn<void(FullMsgId)> showPeerHistory;
	not_null<PeerData*> peer;
	FullMsgId contextId;
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

class PeerListRowWithMsgId : public PeerListRow {
public:
	using PeerListRow::PeerListRow;

	void setMsgId(MsgId msgId);
	[[nodiscard]] MsgId msgId() const;

private:
	MsgId _msgId;

};

void PeerListRowWithMsgId::setMsgId(MsgId msgId) {
	_msgId = msgId;
}

MsgId PeerListRowWithMsgId::msgId() const {
	return _msgId;
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
	explicit PublicForwardsController(Descriptor d);

	Main::Session &session() const override;
	void prepare() override;
	void rowClicked(not_null<PeerListRow*> row) override;
	void loadMoreRows() override;

private:
	void appendRow(not_null<PeerData*> peer, MsgId msgId);
	void applySlice(const Data::PublicForwardsSlice &slice);

	const not_null<Main::Session*> _session;
	Fn<void(FullMsgId)> _showPeerHistory;

	Api::PublicForwards _api;
	Data::PublicForwardsSlice _firstSlice;
	Data::PublicForwardsSlice::OffsetToken _apiToken;

	bool _allLoaded = false;

};

PublicForwardsController::PublicForwardsController(Descriptor d)
: _session(&d.peer->session())
, _showPeerHistory(std::move(d.showPeerHistory))
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
		if (const auto peer = session().data().peerLoaded(item.peer)) {
			appendRow(peer, item.msg);
		}
	}
	delegate()->peerListRefreshRows();
}

void PublicForwardsController::rowClicked(not_null<PeerListRow*> row) {
	const auto rowWithMsgId = static_cast<PeerListRowWithMsgId*>(row.get());
	crl::on_main([=, msgId = rowWithMsgId->msgId(), peer = row->peer()] {
		_showPeerHistory({ peer->id, msgId });
	});
}

void PublicForwardsController::appendRow(
		not_null<PeerData*> peer,
		MsgId msgId) {
	if (delegate()->peerListFindRow(peer->id.value)) {
		return;
	}

	auto row = std::make_unique<PeerListRowWithMsgId>(peer);
	row->setMsgId(msgId);

	const auto members = peer->asChannel()->membersCount();
	const auto message = peer->owner().message({ peer->id, msgId });
	const auto views = message ? message->viewsCount() : 0;

	const auto membersText = !members
		? QString()
		: peer->isMegagroup()
		? tr::lng_chat_status_members(tr::now, lt_count_decimal, members)
		: tr::lng_chat_status_subscribers(tr::now, lt_count_decimal, members);
	const auto viewsText = views
		? tr::lng_stats_recent_messages_views({}, lt_count_decimal, views)
		: QString();
	const auto resultText = (membersText.isEmpty() || viewsText.isEmpty())
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
	Ui::EmptyUserpic::UserpicColor(boost.isUnclaimed
		? kColorIndexUnclaimed
		: kColorIndexPending),
	QString()) {
	init();
}

void BoostRow::init() {
	invalidateBadges();
	auto status = !PeerListRow::special()
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
		: _boost.isUnclaimed
		? tr::lng_boosts_list_unclaimed(tr::now)
		: tr::lng_boosts_list_pending(tr::now);
}

PaintRoundImageCallback BoostRow::generatePaintUserpicCallback(bool force) {
	if (!PeerListRow::special()) {
		return PeerListRow::generatePaintUserpicCallback(force);
	}
	return [=](Painter &p, int x, int y, int outerWidth, int size) mutable {
		_userpic.paintCircle(p, x, y, outerWidth, size);
		(_boost.isUnclaimed
			? st::boostsListUnclaimedIcon
			: st::boostsListUnknownIcon).paintInCenter(
				p,
				{ x, y, size, size });
	};
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
	_rightBadge = (_boost.isGift || _boost.isGiveaway)
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

} // namespace

void AddPublicForwards(
		const Data::PublicForwardsSlice &firstSlice,
		not_null<Ui::VerticalLayout*> container,
		Fn<void(FullMsgId)> showPeerHistory,
		not_null<PeerData*> peer,
		FullMsgId contextId) {
	if (!peer->isChannel()) {
		return;
	}

	struct State final {
		State(Descriptor d) : controller(std::move(d)) {
		}
		PeerListContentDelegateSimple delegate;
		PublicForwardsController controller;
	};
	const auto state = container->lifetime().make_state<State>(Descriptor{
		firstSlice,
		std::move(showPeerHistory),
		peer,
		contextId,
	});

	if (const auto total = firstSlice.total; total > 0) {
		AddSubsectionTitle(
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

	AddSubsectionTitle(container, std::move(title));

	state->delegate.setContent(container->add(
		object_ptr<PeerListContent>(container, &state->controller)));
	state->controller.setDelegate(&state->delegate);

	const auto wrap = container->add(
		object_ptr<Ui::SlideWrap<Ui::SettingsButton>>(
			container,
			object_ptr<Ui::SettingsButton>(
				container,
				tr::lng_stories_show_more())),
		{ 0, -st::settingsButton.padding.top(), 0, 0 });

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

	const auto wrap = container->add(
		object_ptr<Ui::SlideWrap<Ui::SettingsButton>>(
			container,
			object_ptr<Ui::SettingsButton>(
				container,
				(firstSlice.token.gifts
					? tr::lng_boosts_show_more_gifts
					: tr::lng_boosts_show_more_boosts)(
						lt_count,
						state->controller.totalBoostsValue(
						) | rpl::map(
							max - rpl::mappers::_1
						) | tr::to_count()),
				st::statisticsShowMoreButton)),
		{ 0, -st::settingsButton.padding.top(), 0, 0 });
	const auto button = wrap->entity();
	AddArrow(button);

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
	button->setClickedCallback(showMore);
}

} // namespace Info::Statistics
