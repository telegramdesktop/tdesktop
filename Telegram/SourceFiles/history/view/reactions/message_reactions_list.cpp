/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/reactions/message_reactions_list.h"

#include "history/view/reactions/message_reactions_selector.h"
#include "boxes/peer_list_box.h"
#include "boxes/peers/prepare_short_info_box.h"
#include "window/window_session_controller.h"
#include "history/history_item.h"
#include "history/history.h"
#include "api/api_who_reacted.h"
#include "ui/controls/who_reacted_context_action.h"
#include "main/main_session.h"
#include "data/data_session.h"
#include "data/data_peer.h"
#include "lang/lang_keys.h"

namespace HistoryView {
namespace {

constexpr auto kPerPageFirst = 20;
constexpr auto kPerPage = 100;

class Row final : public PeerListRow {
public:
	Row(not_null<PeerData*> peer, const QString &reaction);

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
	EmojiPtr _emoji = nullptr;

};

class Controller final : public PeerListController {
public:
	Controller(
		not_null<Window::SessionController*> window,
		not_null<HistoryItem*> item,
		const QString &selected,
		rpl::producer<QString> switches,
		std::shared_ptr<Api::WhoReadList> whoReadIds);

	Main::Session &session() const override;
	void prepare() override;
	void rowClicked(not_null<PeerListRow*> row) override;
	void loadMoreRows() override;

private:
	using AllEntry = std::pair<not_null<PeerData*>, QString>;

	void fillWhoRead();
	void loadMore(const QString &reaction);
	bool appendRow(not_null<PeerData*> peer, QString reaction);
	std::unique_ptr<PeerListRow> createRow(
		not_null<PeerData*> peer,
		QString reaction) const;
	void showReaction(const QString &reaction);

	const not_null<Window::SessionController*> _window;
	const not_null<HistoryItem*> _item;
	MTP::Sender _api;

	QString _shownReaction;
	std::shared_ptr<Api::WhoReadList> _whoReadIds;
	std::vector<not_null<PeerData*>> _whoRead;

	std::vector<AllEntry> _all;
	QString _allOffset;

	std::vector<not_null<PeerData*>> _filtered;
	QString _filteredOffset;

	mtpRequestId _loadRequestId = 0;

};

Row::Row(not_null<PeerData*> peer, const QString &reaction)
: PeerListRow(peer)
, _emoji(Ui::Emoji::Find(reaction)) {
}

QSize Row::rightActionSize() const {
	const auto size = Ui::Emoji::GetSizeNormal() / style::DevicePixelRatio();
	return _emoji ? QSize(size, size) : QSize();
}

QMargins Row::rightActionMargins() const {
	if (!_emoji) {
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
	if (!_emoji) {
		return;
	}
	// #TODO reactions
	Ui::Emoji::Draw(p, _emoji, Ui::Emoji::GetSizeNormal(), x, y);
}

Controller::Controller(
	not_null<Window::SessionController*> window,
	not_null<HistoryItem*> item,
	const QString &selected,
	rpl::producer<QString> switches,
	std::shared_ptr<Api::WhoReadList> whoReadIds)
: _window(window)
, _item(item)
, _api(&window->session().mtp())
, _shownReaction(selected)
, _whoReadIds(whoReadIds) {
	std::move(
		switches
	) | rpl::filter([=](const QString &reaction) {
		return (_shownReaction != reaction);
	}) | rpl::start_with_next([=](const QString &reaction) {
		showReaction(reaction);
	}, lifetime());
}

Main::Session &Controller::session() const {
	return _window->session();
}

void Controller::prepare() {
	if (_shownReaction == u"read"_q) {
		fillWhoRead();
		setDescriptionText(QString());
	} else {
		setDescriptionText(tr::lng_contacts_loading(tr::now));
	}
	delegate()->peerListRefreshRows();
	loadMore(_shownReaction);
}

void Controller::showReaction(const QString &reaction) {
	if (_shownReaction == reaction) {
		return;
	}

	_api.request(base::take(_loadRequestId)).cancel();
	while (const auto count = delegate()->peerListFullRowsCount()) {
		delegate()->peerListRemoveRow(delegate()->peerListRowAt(count - 1));
	}

	_shownReaction = reaction;
	if (_shownReaction == u"read"_q) {
		fillWhoRead();
	} else if (_shownReaction.isEmpty()) {
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
		appendRow(peer, QString());
	}
}

void Controller::loadMoreRows() {
	const auto &offset = _shownReaction.isEmpty()
		? _allOffset
		: _filteredOffset;
	if (_loadRequestId || offset.isEmpty()) {
		return;
	}
	loadMore(_shownReaction);
}

void Controller::loadMore(const QString &reaction) {
	if (reaction == u"read"_q) {
		loadMore(QString());
		return;
	} else if (reaction.isEmpty() && _allOffset.isEmpty() && !_all.empty()) {
		return;
	}
	_api.request(_loadRequestId).cancel();

	const auto &offset = reaction.isEmpty()
		? _allOffset
		: _filteredOffset;

	using Flag = MTPmessages_GetMessageReactionsList::Flag;
	const auto flags = Flag(0)
		| (offset.isEmpty() ? Flag(0) : Flag::f_offset)
		| (reaction.isEmpty() ? Flag(0) : Flag::f_reaction);
	_loadRequestId = _api.request(MTPmessages_GetMessageReactionsList(
		MTP_flags(flags),
		_item->history()->peer->input,
		MTP_int(_item->id),
		MTP_string(reaction),
		MTP_string(offset),
		MTP_int(offset.isEmpty() ? kPerPageFirst : kPerPage)
	)).done([=](const MTPmessages_MessageReactionsList &result) {
		_loadRequestId = 0;
		const auto filtered = !reaction.isEmpty();
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
					const auto reaction = qs(data.vreaction());
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

bool Controller::appendRow(not_null<PeerData*> peer, QString reaction) {
	if (delegate()->peerListFindRow(peer->id.value)) {
		return false;
	}
	delegate()->peerListAppendRow(createRow(peer, reaction));
	return true;
}

std::unique_ptr<PeerListRow> Controller::createRow(
		not_null<PeerData*> peer,
		QString reaction) const {
	return std::make_unique<Row>(peer, reaction);
}

} // namespace

object_ptr<Ui::BoxContent> ReactionsListBox(
		not_null<Window::SessionController*> window,
		not_null<HistoryItem*> item,
		QString selected,
		std::shared_ptr<Api::WhoReadList> whoReadIds) {
	Expects(IsServerMsgId(item->id));

	if (!item->reactions().contains(selected)) {
		selected = QString();
	}
	if (selected.isEmpty() && whoReadIds && !whoReadIds->list.empty()) {
		selected = u"read"_q;
	}
	const auto tabRequests = std::make_shared<rpl::event_stream<QString>>();
	const auto initBox = [=](not_null<PeerListBox*> box) {
		box->setNoContentMargin(true);

		auto map = item->reactions();
		if (whoReadIds && !whoReadIds->list.empty()) {
			map.emplace(u"read"_q, int(whoReadIds->list.size()));
		}
		const auto selector = CreateReactionSelector(
			box,
			map,
			selected,
			whoReadIds ? whoReadIds->type : Ui::WhoReadType::Reacted);
		selector->changes(
		) | rpl::start_to_stream(*tabRequests, box->lifetime());

		box->widthValue(
		) | rpl::start_with_next([=](int width) {
			selector->resizeToWidth(width);
			selector->move(0, 0);
		}, box->lifetime());
		selector->heightValue(
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

} // namespace HistoryView
