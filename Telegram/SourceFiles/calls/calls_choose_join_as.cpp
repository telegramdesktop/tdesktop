/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "calls/calls_choose_join_as.h"

#include "data/data_peer.h"
#include "data/data_user.h"
#include "data/data_channel.h"
#include "data/data_session.h"
#include "main/main_session.h"
#include "main/main_account.h"
#include "lang/lang_keys.h"
#include "apiwrap.h"
#include "ui/layers/generic_box.h"
#include "boxes/peer_list_box.h"
#include "styles/style_boxes.h"

namespace Calls {
namespace {

using Context = ChooseJoinAsProcess::Context;

class ListController : public PeerListController {
public:
	ListController(
		std::vector<not_null<PeerData*>> list,
		not_null<PeerData*> selected);

	Main::Session &session() const override;
	void prepare() override;
	void rowClicked(not_null<PeerListRow*> row) override;

	[[nodiscard]] not_null<PeerData*> selected() const;

private:
	std::unique_ptr<PeerListRow> createRow(not_null<PeerData*> peer);

	std::vector<not_null<PeerData*>> _list;
	not_null<PeerData*> _selected;

};

ListController::ListController(
	std::vector<not_null<PeerData*>> list,
	not_null<PeerData*> selected)
: PeerListController()
, _list(std::move(list))
, _selected(selected) {
}

Main::Session &ListController::session() const {
	return _selected->session();
}

std::unique_ptr<PeerListRow> ListController::createRow(
		not_null<PeerData*> peer) {
	auto result = std::make_unique<PeerListRow>(peer);
	if (peer->isSelf()) {
		result->setCustomStatus(
			tr::lng_group_call_join_as_personal(tr::now));
	} else if (const auto channel = peer->asChannel()) {
		result->setCustomStatus(
			(channel->isMegagroup()
				? tr::lng_chat_status_members
				: tr::lng_chat_status_subscribers)(
					tr::now,
					lt_count,
					channel->membersCount()));
	}
	return result;
}

void ListController::prepare() {
	delegate()->peerListSetSearchMode(PeerListSearchMode::Disabled);
	for (const auto &peer : _list) {
		auto row = createRow(peer);
		const auto raw = row.get();
		delegate()->peerListAppendRow(std::move(row));
		if (peer == _selected) {
			delegate()->peerListSetRowChecked(raw, true);
			raw->finishCheckedAnimation();
		}
	}
	delegate()->peerListRefreshRows();
}

void ListController::rowClicked(not_null<PeerListRow*> row) {
	const auto peer = row->peer();
	if (peer == _selected) {
		return;
	}
	const auto previous = delegate()->peerListFindRow(_selected->id);
	Assert(previous != nullptr);
	delegate()->peerListSetRowChecked(previous, false);
	delegate()->peerListSetRowChecked(row, true);
	_selected = peer;
}

not_null<PeerData*> ListController::selected() const {
	return _selected;
}

void ChooseJoinAsBox(
		not_null<Ui::GenericBox*> box,
		Context context,
		std::vector<not_null<PeerData*>> list,
		not_null<PeerData*> selected,
		Fn<void(not_null<PeerData*>)> done) {
	box->setTitle([&] {
		switch (context) {
		case Context::Create: return tr::lng_group_call_start_as_header();
		case Context::Join: return tr::lng_group_call_join_as_header();
		case Context::Switch: return tr::lng_group_call_display_as_header();
		}
		Unexpected("Context in ChooseJoinAsBox.");
	}());
	box->addRow(object_ptr<Ui::FlatLabel>(
		box,
		tr::lng_group_call_join_as_about(),
		st::confirmPhoneAboutLabel));

	auto &lifetime = box->lifetime();
	const auto delegate = lifetime.make_state<
		PeerListContentDelegateSimple
	>();
	const auto controller = lifetime.make_state<ListController>(
		std::move(list),
		selected);
	//controller->setStyleOverrides();
	const auto content = box->addRow(
		object_ptr<PeerListContent>(box, controller),
		style::margins());
	delegate->setContent(content);
	controller->setDelegate(delegate);
	box->addButton(tr::lng_continue(), [=] {
		done(controller->selected());
	});
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
}

} // namespace

ChooseJoinAsProcess::~ChooseJoinAsProcess() {
	if (_request) {
		_request->peer->session().api().request(_request->id).cancel();
	}
}

void ChooseJoinAsProcess::start(
		not_null<PeerData*> peer,
		Context context,
		Fn<void(not_null<PeerData*>, not_null<PeerData*>)> done) {
	Expects(done != nullptr);

	const auto session = &peer->session();
	if (_request) {
		const auto already = _request->peer;
		_request->context = context;
		_request->done = std::move(done);
		if (already == peer) {
			return;
		} else if (&already->session() == session) {
			_request->peer = peer;
			return;
		}
		session->api().request(_request->id).cancel();
		_request = nullptr;
	}
	_request = std::make_unique<ChannelsListRequest>(
		ChannelsListRequest{
			.peer = peer,
			.done = std::move(done),
			.context = context });
	session->account().sessionChanges(
	) | rpl::start_with_next([=] {
		_request = nullptr;
	}, _request->lifetime);

	const auto finish = [=](not_null<PeerData*> joinAs) {
		const auto peer = _request->peer;
		const auto done = std::move(_request->done);
		const auto box = _request->box;
		_request = nullptr;
		done(peer, joinAs);
		if (const auto strong = box.data()) {
			strong->closeBox();
		}
	};
	using Flag = MTPchannels_GetAdminedPublicChannels::Flag;
	_request->id = session->api().request(
		MTPchannels_GetAdminedPublicChannels(
			MTP_flags(Flag::f_for_groupcall))
	).done([=](const MTPmessages_Chats &result) {
		const auto &chats = result.match([](const auto &data) {
			return data.vchats().v;
		});
		const auto peer = _request->peer;
		const auto self = peer->session().user();
		if (chats.size() == 1) {
			finish(self);
			return;
		}
		auto list = std::vector<not_null<PeerData*>>();
		list.reserve(chats.size() + 1);
		list.push_back(self);
		for (const auto &chat : chats) {
			list.push_back(session->data().processChat(chat));
		}
		const auto selectedId = peer->groupCallDefaultJoinAs();
		const auto selected = [&]() -> not_null<PeerData*> {
			if (!selectedId) {
				return self;
			}
			const auto loaded = session->data().peerLoaded(selectedId);
			return (loaded && ranges::contains(list, not_null{ loaded }))
				? not_null(loaded)
				: self;
		}();
		_request->box = Ui::show(
			Box(
				ChooseJoinAsBox,
				_request->context,
				std::move(list),
				selected,
				crl::guard(&_request->guard, finish)),
			Ui::LayerOption::KeepOther);

		_request->box->boxClosing(
		) | rpl::start_with_next([=] {
			_request = nullptr;
		}, _request->lifetime);
	}).fail([=](const RPCError &error) {
		finish(session->user());
	}).send();
}

} // namespace Calls
