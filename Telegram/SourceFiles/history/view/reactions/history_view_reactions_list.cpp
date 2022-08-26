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
		not_null<Window::SessionController*> window,
		not_null<HistoryItem*> item,
		const ReactionId &selected,
		rpl::producer<ReactionId> switches,
		std::shared_ptr<Api::WhoReadList> whoReadIds);

	Main::Session &session() const override;
	void prepare() override;
	void rowClicked(not_null<PeerListRow*> row) override;
	void loadMoreRows() override;

private:
	using AllEntry = std::pair<not_null<PeerData*>, Data::ReactionId>;

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

	const not_null<Window::SessionController*> _window;
	const not_null<HistoryItem*> _item;
	const Ui::Text::CustomEmojiFactory _factory;
	MTP::Sender _api;

	ReactionId _shownReaction;
	std::shared_ptr<Api::WhoReadList> _whoReadIds;
	std::vector<not_null<PeerData*>> _whoRead;

	mutable base::flat_map<std::pair<PeerId, ReactionId>, uint64> _idsMap;
	mutable uint64 _idsCounter = 0;

	std::vector<AllEntry> _all;
	QString _allOffset;

	std::vector<not_null<PeerData*>> _filtered;
	QString _filteredOffset;

	mtpRequestId _loadRequestId = 0;

};

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
		.preview = st::windowBgRipple->c,
		.now = crl::now(),
		.position = { x + skip, y + skip },
		.paused = _paused(),
	});
}


Controller::Controller(
	not_null<Window::SessionController*> window,
	not_null<HistoryItem*> item,
	const ReactionId &selected,
	rpl::producer<ReactionId> switches,
	std::shared_ptr<Api::WhoReadList> whoReadIds)
: _window(window)
, _item(item)
, _factory(Data::ReactedMenuFactory(&window->session()))
, _api(&window->session().mtp())
, _shownReaction(selected)
, _whoReadIds(whoReadIds) {
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
		_filtered = _all | ranges::view::filter([&](const AllEntry &entry) {
			return (entry.second == reaction);
		}) | ranges::view::transform(
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
	if (_whoReadIds && !_whoReadIds->list.empty() && _whoRead.empty()) {
		auto &owner = _window->session().data();
		for (const auto &peerId : _whoReadIds->list) {
			if (const auto peer = owner.peerLoaded(peerId)) {
				_whoRead.push_back(peer);
			}
		}
	}
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
		_item->history()->peer->input,
		MTP_int(_item->id),
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
		window->show(PrepareShortInfoBox(peer, window));
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
		[=] { return _window->isGifPausedAtLeastFor(
			Window::GifPauseReason::Layer); });
}

} // namespace

object_ptr<Ui::BoxContent> FullListBox(
		not_null<Window::SessionController*> window,
		not_null<HistoryItem*> item,
		Data::ReactionId selected,
		std::shared_ptr<Api::WhoReadList> whoReadIds) {
	Expects(IsServerMsgId(item->id));

	if (!ranges::contains(
			item->reactions(),
			selected,
			&Data::MessageReaction::id)) {
		selected = {};
	}
	if (selected.empty() && whoReadIds && !whoReadIds->list.empty()) {
		selected = Data::ReactionId{ u"read"_q };
	}
	const auto tabRequests = std::make_shared<
		rpl::event_stream<Data::ReactionId>>();
	const auto initBox = [=](not_null<PeerListBox*> box) {
		box->setNoContentMargin(true);

		auto map = item->reactions();
		if (whoReadIds && !whoReadIds->list.empty()) {
			map.push_back({
				.id = Data::ReactionId{ u"read"_q },
				.count = int(whoReadIds->list.size()),
			});
		}
		const auto tabs = CreateTabs(
			box,
			Data::ReactedMenuFactory(&item->history()->session()),
			[=] { return window->isGifPausedAtLeastFor(
				Window::GifPauseReason::Layer); },
			map,
			selected,
			whoReadIds ? whoReadIds->type : Ui::WhoReadType::Reacted);
		tabs->changes(
		) | rpl::start_to_stream(*tabRequests, box->lifetime());

		box->widthValue(
		) | rpl::start_with_next([=](int width) {
			tabs->resizeToWidth(width);
			tabs->move(0, 0);
		}, box->lifetime());
		tabs->heightValue(
		) | rpl::start_with_next([=](int height) {
			box->setAddedTopScrollSkip(height);
		}, box->lifetime());
		box->addButton(tr::lng_close(), [=] {
			box->closeBox();
		});
	};
	return Box<PeerListBox>(
		std::make_unique<Controller>(
			window,
			item,
			selected,
			tabRequests->events(),
			whoReadIds),
		initBox);
}

} // namespace HistoryView::Reactions
