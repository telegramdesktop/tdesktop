/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/statistics/info_statistics_public_forwards.h"

#include "api/api_statistics.h"
#include "boxes/peer_list_controllers.h"
#include "data/data_channel.h"
#include "data/data_session.h"
#include "history/history_item.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "settings/settings_common.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "styles/style_settings.h"

namespace Info::Statistics {
namespace {

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

class PublicForwardsController final : public PeerListController {
public:
	explicit PublicForwardsController(
		Fn<void(FullMsgId)> showPeerHistory,
		not_null<PeerData*> peer,
		FullMsgId contextId);

	Main::Session &session() const override;
	void prepare() override;
	void rowClicked(not_null<PeerListRow*> row) override;
	void loadMoreRows() override;

	[[nodiscard]] rpl::producer<int> totalCountChanges() const;

private:
	bool appendRow(not_null<PeerData*> peer, MsgId msgId);

	const not_null<Main::Session*> _session;
	Fn<void(FullMsgId)> _showPeerHistory;

	Api::PublicForwards _api;
	Api::PublicForwards::OffsetToken _apiToken;

	bool _allLoaded = false;

	rpl::event_stream<int> _totalCountChanges;

};

PublicForwardsController::PublicForwardsController(
	Fn<void(FullMsgId)> showPeerHistory,
	not_null<PeerData*> peer,
	FullMsgId contextId)
: _session(&peer->session())
, _showPeerHistory(std::move(showPeerHistory))
, _api(peer->asChannel(), contextId) {
}

Main::Session &PublicForwardsController::session() const {
	return *_session;
}

void PublicForwardsController::prepare() {
	loadMoreRows();
	delegate()->peerListRefreshRows();
}

void PublicForwardsController::loadMoreRows() {
	if (_allLoaded) {
		return;
	}
	_api.request(_apiToken, [=](const Api::PublicForwards::Slice &slice) {
		_allLoaded = slice.allLoaded;
		_apiToken = slice.token;
		_totalCountChanges.fire_copy(slice.total);

		for (const auto &item : slice.list) {
			if (const auto peer = session().data().peerLoaded(item.peer)) {
				appendRow(peer, item.msg);
			}
		}
		delegate()->peerListRefreshRows();
	});
}

void PublicForwardsController::rowClicked(not_null<PeerListRow*> row) {
	const auto rowWithMsgId = static_cast<PeerListRowWithMsgId*>(row.get());
	crl::on_main([=, msgId = rowWithMsgId->msgId(), peer = row->peer()] {
		_showPeerHistory({ peer->id, msgId });
	});
}

bool PublicForwardsController::appendRow(
		not_null<PeerData*> peer,
		MsgId msgId) {
	if (delegate()->peerListFindRow(peer->id.value)) {
		return false;
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
	return true;
}

rpl::producer<int> PublicForwardsController::totalCountChanges() const {
	return _totalCountChanges.events();
}

} // namespace

void AddPublicForwards(
		not_null<Ui::VerticalLayout*> container,
		Fn<void(FullMsgId)> showPeerHistory,
		not_null<PeerData*> peer,
		FullMsgId contextId) {
	if (!peer->isChannel()) {
		return;
	}

	struct State final {
		State(
			Fn<void(FullMsgId)> c,
			not_null<PeerData*> p,
			FullMsgId i) : controller(std::move(c), p, i) {
		}
		PeerListContentDelegateSimple delegate;
		PublicForwardsController controller;
	};
	const auto state = container->lifetime().make_state<State>(
		std::move(showPeerHistory),
		peer,
		contextId);

	const auto wrap = container->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			container,
			object_ptr<Ui::VerticalLayout>(container)));
	wrap->toggle(false, anim::type::instant);

	auto title = state->controller.totalCountChanges(
	) | rpl::distinct_until_changed(
	) | rpl::map([=](int total) {
		if (total && !wrap->toggled()) {
			wrap->toggle(true, anim::type::normal);
		}
		return total
			? tr::lng_stats_overview_message_public_share(
				tr::now,
				lt_count_decimal,
				total)
			: QString();
	});

	{
		const auto &subtitlePadding = st::settingsButton.padding;
		::Settings::AddSubsectionTitle(
			wrap->entity(),
			std::move(title),
			{ 0, -subtitlePadding.top(), 0, -subtitlePadding.bottom() });
	}
	state->delegate.setContent(wrap->entity()->add(
		object_ptr<PeerListContent>(wrap->entity(), &state->controller)));
	state->controller.setDelegate(&state->delegate);
}

} // namespace Info::Statistics
