/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/reactions/history_view_reactions_list.h"

#include "history/view/reactions/history_view_reactions_tabs.h"
#include "boxes/peer_list_box.h"
#include "boxes/peers/prepare_short_info_box.h"
#include "window/window_session_controller.h"
#include "history/history_item.h"
#include "history/history.h"
#include "api/api_who_reacted.h"
#include "ui/controls/who_reacted_context_action.h"
#include "ui/text/text_custom_emoji.h"
#include "ui/painter.h"
#include "data/stickers/data_custom_emoji.h"
#include "data/data_message_reaction_id.h"
#include "main/main_session.h"
#include "data/data_session.h"
#include "data/data_peer.h"
#include "lang/lang_keys.h"

namespace HistoryView::Reactions {
namespace {

constexpr auto kPerPageFirst = 20;
constexpr auto kPerPage = 100;

using ::Data::ReactionId;

class Row final : public PeerListRow {
public:
	Row(
		uint64 id,
		not_null<PeerData*> peer,
		const Ui::Text::CustomEmojiFactory &factory,
		QStringView reactionEntityData,
		Fn<void(Row*)> repaint,
		Fn<bool()> paused);

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
	std::unique_ptr<Ui::Text::CustomEmoji> _custom;
	Fn<bool()> _paused;

};

class Controller final : public PeerListController {
public:
	Controller(
		not_null<Window::SessionNavigation*> window,
		FullMsgId itemId,
		const ReactionId &selected,
		rpl::producer<ReactionId> switches,
		std::shared_ptr<Api::WhoReadList> whoReadIds);

	Main::Session &session() const override;
	void prepare() override;
	void rowClicked(not_null<PeerListRow*> row) override;
	void loadMoreRows() override;

	std::unique_ptr<PeerListRow> createRestoredRow(
		not_null<PeerData*> peer) override;

	std::unique_ptr<PeerListState> saveState() const override;
	void restoreState(std::unique_ptr<PeerListState> state) override;

private:
	using AllEntry = std::pair<not_null<PeerData*>, Data::ReactionId>;

	struct SavedState : SavedStateBase {
		ReactionId shownReaction;
		base::flat_map<std::pair<PeerId, ReactionId>, uint64> idsMap;
		uint64 idsCounter = 0;
		std::vector<AllEntry> all;
		QString allOffset;
		std::vector<not_null<PeerData*>> filtered;
		QString filteredOffset;
		bool wasLoading = false;
	};

	void fillWhoRead();
	void loadMore(const ReactionId &reaction);
	bool appendRow(not_null<PeerData*> peer, ReactionId reaction);
	std::unique_ptr<PeerListRow> createRow(
		not_null<PeerData*> peer,
		ReactionId reaction) const;
	void showReaction(const ReactionId &reaction);

	[[nodiscard]] uint64 id(
		not_null<PeerData*> peer,
		const ReactionId &reaction) const;

	const not_null<Window::SessionNavigation*> _window;
	const not_null<PeerData*> _peer;
	const FullMsgId _itemId;
	const Ui::Text::CustomEmojiFactory _factory;
	const std::shared_ptr<Api::WhoReadList> _whoReadIds;
	const std::vector<not_null<PeerData*>> _whoRead;
	MTP::Sender _api;

	ReactionId _shownReaction;

	mutable base::flat_map<std::pair<PeerId, ReactionId>, uint64> _idsMap;
	mutable uint64 _idsCounter = 0;

	std::vector<AllEntry> _all;
	QString _allOffset;

	std::vector<not_null<PeerData*>> _filtered;
	QString _filteredOffset;

	mtpRequestId _loadRequestId = 0;

};

[[nodiscard]] std::vector<not_null<PeerData*>> ResolveWhoRead(
		not_null<Window::SessionNavigation*> window,
		const std::shared_ptr<Api::WhoReadList> &whoReadIds) {
	if (!whoReadIds || whoReadIds->list.empty()) {
		return {};
	}
	auto result = std::vector<not_null<PeerData*>>();
	auto &owner = window->session().data();
	for (const auto &peerWithDate : whoReadIds->list) {
		if (const auto peer = owner.peerLoaded(peerWithDate.peer)) {
			result.push_back(peer);
		}
	}
	return result;
}

Row::Row(
	uint64 id,
	not_null<PeerData*> peer,
	const Ui::Text::CustomEmojiFactory &factory,
	QStringView reactionEntityData,
	Fn<void(Row*)> repaint,
	Fn<bool()> paused)
: PeerListRow(peer, id)
, _custom(reactionEntityData.isEmpty()
	? nullptr
	: factory(reactionEntityData, [=] { repaint(this); }))
, _paused(std::move(paused)) {
}

QSize Row::rightActionSize() const {
	const auto size = Ui::Emoji::GetSizeNormal() / style::DevicePixelRatio();
	return _custom ? QSize(size, size) : QSize();
}

QMargins Row::rightActionMargins() const {
	if (!_custom) {
		return QMargins();
	}
	const auto size = Ui::Emoji::GetSizeNormal() / style::DevicePixelRatio();
	return QMargins(
		size / 2,
		(st::defaultPeerList.item.height - size) / 2,
		(size * 3) / 2,
		0);
}

bool Row::rightActionDisabled() const {
	return true;
}

void Row::rightActionPaint(
		Painter &p,
		int x,
		int y,
		int outerWidth,
		bool selected,
		bool actionSelected) {
	if (!_custom) {
		return;
	}
	const auto size = Ui::Emoji::GetSizeNormal() / style::DevicePixelRatio();
	const auto skip = (size - Ui::Text::AdjustCustomEmojiSize(size)) / 2;
	_custom->paint(p, {
		.textColor = st::windowFg->c,
		.now = crl::now(),
		.position = { x + skip, y + skip },
		.paused = _paused(),
	});
}

Controller::Controller(
	not_null<Window::SessionNavigation*> window,
	FullMsgId itemId,
	const ReactionId &selected,
	rpl::producer<ReactionId> switches,
	std::shared_ptr<Api::WhoReadList> whoReadIds)
: _window(window)
, _peer(window->session().data().peer(itemId.peer))
, _itemId(itemId)
, _factory(Data::ReactedMenuFactory(&window->session()))
, _whoReadIds(whoReadIds)
, _whoRead(ResolveWhoRead(window, _whoReadIds))
, _api(&window->session().mtp())
, _shownReaction(selected) {
	std::move(
		switches
	) | rpl::filter([=](const ReactionId &reaction) {
		return (_shownReaction != reaction);
	}) | rpl::start_with_next([=](const ReactionId &reaction) {
		showReaction(reaction);
	}, lifetime());
}

Main::Session &Controller::session() const {
	return _window->session();
}

void Controller::prepare() {
	if (_shownReaction.emoji() == u"read"_q) {
		fillWhoRead();
		setDescriptionText(QString());
	} else {
		setDescriptionText(tr::lng_contacts_loading(tr::now));
	}
	delegate()->peerListRefreshRows();
	loadMore(_shownReaction);
}

void Controller::showReaction(const ReactionId &reaction) {
	if (_shownReaction == reaction) {
		return;
	}

	_api.request(base::take(_loadRequestId)).cancel();
	while (const auto count = delegate()->peerListFullRowsCount()) {
		delegate()->peerListRemoveRow(delegate()->peerListRowAt(count - 1));
	}

	_shownReaction = reaction;
	if (_shownReaction.emoji() == u"read"_q) {
		fillWhoRead();
	} else if (_shownReaction.empty()) {
		_filtered.clear();
		for (const auto &[peer, reaction] : _all) {
			appendRow(peer, reaction);
		}
	} else {
		_filtered = _all | ranges::views::filter([&](const AllEntry &entry) {
			return (entry.second == reaction);
		}) | ranges::views::transform(
			&AllEntry::first
		) | ranges::to_vector;
		for (const auto peer : _filtered) {
			appendRow(peer, _shownReaction);
		}
		_filteredOffset = QString();
	}
	loadMore(_shownReaction);
	setDescriptionText(delegate()->peerListFullRowsCount()
		? QString()
		: tr::lng_contacts_loading(tr::now));
	delegate()->peerListRefreshRows();
}

uint64 Controller::id(
		not_null<PeerData*> peer,
		const ReactionId &reaction) const {
	const auto key = std::pair{ peer->id, reaction };
	const auto i = _idsMap.find(key);
	return (i != end(_idsMap)
		? i
		: _idsMap.emplace(key, ++_idsCounter).first)->second;
}

void Controller::fillWhoRead() {
	for (const auto &peer : _whoRead) {
		appendRow(peer, ReactionId());
	}
}

void Controller::loadMoreRows() {
	const auto &offset = _shownReaction.empty()
		? _allOffset
		: _filteredOffset;
	if (_loadRequestId || offset.isEmpty()) {
		return;
	}
	loadMore(_shownReaction);
}

std::unique_ptr<PeerListRow> Controller::createRestoredRow(
		not_null<PeerData*> peer) {
	if (_shownReaction.emoji() == u"read"_q) {
		return createRow(peer, Data::ReactionId());
	} else if (_shownReaction.empty()) {
		const auto i = ranges::find(_all, peer, &AllEntry::first);
		const auto reaction = (i != end(_all)) ? i->second : _shownReaction;
		return createRow(peer, reaction);
	}
	return createRow(peer, _shownReaction);
}

std::unique_ptr<PeerListState> Controller::saveState() const {
	auto result = PeerListController::saveState();

	auto my = std::make_unique<SavedState>();
	my->shownReaction = _shownReaction;
	my->idsMap = _idsMap;
	my->idsCounter = _idsCounter;
	my->all = _all;
	my->allOffset = _allOffset;
	my->filtered = _filtered;
	my->filteredOffset = _filteredOffset;
	my->wasLoading = (_loadRequestId != 0);
	result->controllerState = std::move(my);
	return result;
}

void Controller::restoreState(std::unique_ptr<PeerListState> state) {
	auto typeErasedState = state
		? state->controllerState.get()
		: nullptr;
	if (const auto my = dynamic_cast<SavedState*>(typeErasedState)) {
		if (const auto requestId = base::take(_loadRequestId)) {
			_api.request(requestId).cancel();
		}
		_shownReaction = my->shownReaction;
		_idsMap = std::move(my->idsMap);
		_idsCounter = my->idsCounter;
		_all = std::move(my->all);
		_allOffset = std::move(my->allOffset);
		_filtered = std::move(my->filtered);
		_filteredOffset = std::move(my->filteredOffset);
		if (my->wasLoading) {
			loadMoreRows();
		}
		PeerListController::restoreState(std::move(state));
		if (delegate()->peerListFullRowsCount()) {
			setDescriptionText(QString());
			delegate()->peerListRefreshRows();
		}
	}
}

void Controller::loadMore(const ReactionId &reaction) {
	if (reaction.emoji() == u"read"_q) {
		loadMore(ReactionId());
		return;
	} else if (reaction.empty() && _allOffset.isEmpty() && !_all.empty()) {
		return;
	}
	_api.request(_loadRequestId).cancel();

	const auto &offset = reaction.empty()
		? _allOffset
		: _filteredOffset;

	using Flag = MTPmessages_GetMessageReactionsList::Flag;
	const auto flags = Flag(0)
		| (offset.isEmpty() ? Flag(0) : Flag::f_offset)
		| (reaction.empty() ? Flag(0) : Flag::f_reaction);
	_loadRequestId = _api.request(MTPmessages_GetMessageReactionsList(
		MTP_flags(flags),
		_peer->input,
		MTP_int(_itemId.msg),
		Data::ReactionToMTP(reaction),
		MTP_string(offset),
		MTP_int(offset.isEmpty() ? kPerPageFirst : kPerPage)
	)).done([=](const MTPmessages_MessageReactionsList &result) {
		_loadRequestId = 0;
		const auto filtered = !reaction.empty();
		const auto shown = (reaction == _shownReaction);
		result.match([&](const MTPDmessages_messageReactionsList &data) {
			const auto sessionData = &session().data();
			sessionData->processUsers(data.vusers());
			sessionData->processChats(data.vchats());
			(filtered ? _filteredOffset : _allOffset)
				= data.vnext_offset().value_or_empty();
			for (const auto &reaction : data.vreactions().v) {
				reaction.match([&](const MTPDmessagePeerReaction &data) {
					const auto peer = sessionData->peerLoaded(
						peerFromMTP(data.vpeer_id()));
					const auto reaction = Data::ReactionFromMTP(
						data.vreaction());
					if (peer && (!shown || appendRow(peer, reaction))) {
						if (filtered) {
							_filtered.emplace_back(peer);
						} else {
							_all.emplace_back(peer, reaction);
						}
					}
				});
			}
		});
		if (shown) {
			setDescriptionText(QString());
			delegate()->peerListRefreshRows();
		}
	}).send();
}

void Controller::rowClicked(not_null<PeerListRow*> row) {
	const auto window = _window;
	const auto peer = row->peer();
	crl::on_main(window, [=] {
		window->showPeerInfo(peer);
	});
}

bool Controller::appendRow(not_null<PeerData*> peer, ReactionId reaction) {
	if (delegate()->peerListFindRow(id(peer, reaction))) {
		return false;
	}
	delegate()->peerListAppendRow(createRow(peer, reaction));
	return true;
}

std::unique_ptr<PeerListRow> Controller::createRow(
		not_null<PeerData*> peer,
		ReactionId reaction) const {
	return std::make_unique<Row>(
		id(peer, reaction),
		peer,
		_factory,
		Data::ReactionEntityData(reaction),
		[=](Row *row) { delegate()->peerListUpdateRow(row); },
		[=] { return _window->parentController()->isGifPausedAtLeastFor(
			Window::GifPauseReason::Layer); });
}

} // namespace

Data::ReactionId DefaultSelectedTab(
		not_null<HistoryItem*> item,
		std::shared_ptr<Api::WhoReadList> whoReadIds) {
	return DefaultSelectedTab(item, {}, std::move(whoReadIds));
}

Data::ReactionId DefaultSelectedTab(
		not_null<HistoryItem*> item,
		Data::ReactionId selected,
		std::shared_ptr<Api::WhoReadList> whoReadIds) {
	const auto proj = &Data::MessageReaction::id;
	if (!ranges::contains(item->reactions(), selected, proj)) {
		selected = {};
	}
	return (selected.empty() && whoReadIds && !whoReadIds->list.empty())
		? Data::ReactionId{ u"read"_q }
		: selected;
}

not_null<Tabs*> CreateReactionsTabs(
		not_null<QWidget*> parent,
		not_null<Window::SessionNavigation*> window,
		FullMsgId itemId,
		Data::ReactionId selected,
		std::shared_ptr<Api::WhoReadList> whoReadIds) {
	const auto item = window->session().data().message(itemId);
	auto map = item
		? item->reactions()
		: std::vector<Data::MessageReaction>();
	if (whoReadIds && !whoReadIds->list.empty()) {
		map.push_back({
			.id = Data::ReactionId{ u"read"_q },
			.count = int(whoReadIds->list.size()),
		});
	}
	return CreateTabs(
		parent,
		Data::ReactedMenuFactory(&window->session()),
		[=] { return window->parentController()->isGifPausedAtLeastFor(
			Window::GifPauseReason::Layer); },
		map,
		selected,
		whoReadIds ? whoReadIds->type : Ui::WhoReadType::Reacted);
}

PreparedFullList FullListController(
		not_null<Window::SessionNavigation*> window,
		FullMsgId itemId,
		Data::ReactionId selected,
		std::shared_ptr<Api::WhoReadList> whoReadIds) {
	Expects(IsServerMsgId(itemId.msg));

	const auto tab = std::make_shared<
		rpl::event_stream<Data::ReactionId>>();
	return {
		.controller = std::make_unique<Controller>(
			window,
			itemId,
			selected,
			tab->events(),
			whoReadIds),
		.switchTab = [=](Data::ReactionId id) { tab->fire_copy(id); },
	};
}

} // namespace HistoryView::Reactions
