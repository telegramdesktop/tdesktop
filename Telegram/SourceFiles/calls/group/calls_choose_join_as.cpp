/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "calls/group/calls_choose_join_as.h"

#include "calls/group/calls_group_common.h"
#include "calls/group/calls_group_menu.h"
#include "data/data_peer.h"
#include "data/data_user.h"
#include "data/data_channel.h"
#include "data/data_session.h"
#include "data/data_group_call.h"
#include "main/main_session.h"
#include "main/main_account.h"
#include "lang/lang_keys.h"
#include "apiwrap.h"
#include "ui/layers/generic_box.h"
#include "ui/boxes/choose_date_time.h"
#include "ui/text/text_utilities.h"
#include "ui/toast/toast.h"
#include "boxes/peer_list_box.h"
#include "base/unixtime.h"
#include "base/timer_rpl.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_calls.h"

namespace Calls::Group {
namespace {

constexpr auto kLabelRefreshInterval = 10 * crl::time(1000);

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
	const auto previous = delegate()->peerListFindRow(_selected->id.value);
	Assert(previous != nullptr);
	delegate()->peerListSetRowChecked(previous, false);
	delegate()->peerListSetRowChecked(row, true);
	_selected = peer;
}

not_null<PeerData*> ListController::selected() const {
	return _selected;
}

void ScheduleGroupCallBox(
		not_null<Ui::GenericBox*> box,
		const JoinInfo &info,
		Fn<void(JoinInfo)> done) {
	const auto send = [=](TimeId date) {
		box->closeBox();

		auto copy = info;
		copy.scheduleDate = date;
		done(std::move(copy));
	};
	const auto livestream = info.peer->isBroadcast();
	const auto duration = box->lifetime().make_state<
		rpl::variable<QString>>();
	auto description = (info.peer->isBroadcast()
		? tr::lng_group_call_schedule_notified_channel
		: tr::lng_group_call_schedule_notified_group)(
			lt_duration,
			duration->value());

	const auto now = QDateTime::currentDateTime();
	const auto min = [] {
		return base::unixtime::serialize(
			QDateTime::currentDateTime().addSecs(12));
	};
	const auto max = [] {
		return base::unixtime::serialize(
			QDateTime(QDate::currentDate().addDays(8), QTime(0, 0))) - 1;
	};

	// At least half an hour later, at zero minutes/seconds.
	const auto schedule = QDateTime(
		now.date(),
		QTime(now.time().hour(), 0)
	).addSecs(60 * 60 * (now.time().minute() < 30 ? 1 : 2));

	auto descriptor = Ui::ChooseDateTimeBox(box, {
		.title = (livestream
			? tr::lng_group_call_schedule_title_channel()
			: tr::lng_group_call_schedule_title()),
		.submit = tr::lng_schedule_button(),
		.done = send,
		.min = min,
		.time = base::unixtime::serialize(schedule),
		.max = max,
		.description = std::move(description),
	});

	using namespace rpl::mappers;
	*duration = rpl::combine(
		rpl::single(rpl::empty) | rpl::then(
			base::timer_each(kLabelRefreshInterval)
		),
		std::move(descriptor.values) | rpl::filter(_1 != 0),
		_2
	) | rpl::map([](TimeId date) {
		const auto now = base::unixtime::now();
		const auto duration = (date - now);
		if (duration >= 24 * 60 * 60) {
			return tr::lng_days(tr::now, lt_count, duration / (24 * 60 * 60));
		} else if (duration >= 60 * 60) {
			return tr::lng_hours(tr::now, lt_count, duration / (60 * 60));
		}
		return tr::lng_minutes(tr::now, lt_count, std::max(duration / 60, 1));
	});
}

void ChooseJoinAsBox(
		not_null<Ui::GenericBox*> box,
		Context context,
		JoinInfo info,
		Fn<void(JoinInfo)> done) {
	box->setWidth(st::groupCallJoinAsWidth);
	const auto livestream = info.peer->isBroadcast();
	box->setTitle([&] {
		switch (context) {
		case Context::Create: return livestream
			? tr::lng_group_call_start_as_header_channel()
			: tr::lng_group_call_start_as_header();
		case Context::Join:
		case Context::JoinWithConfirm: return livestream
			? tr::lng_group_call_join_as_header_channel()
			: tr::lng_group_call_join_as_header();
		case Context::Switch: return tr::lng_group_call_display_as_header();
		}
		Unexpected("Context in ChooseJoinAsBox.");
	}());
	const auto &labelSt = (context == Context::Switch)
		? st::groupCallJoinAsLabel
		: st::confirmPhoneAboutLabel;
	box->addRow(object_ptr<Ui::FlatLabel>(
		box,
		tr::lng_group_call_join_as_about(),
		labelSt));

	auto &lifetime = box->lifetime();
	const auto delegate = lifetime.make_state<
		PeerListContentDelegateSimple
	>();
	const auto controller = lifetime.make_state<ListController>(
		info.possibleJoinAs,
		info.joinAs);
	if (context == Context::Switch) {
		controller->setStyleOverrides(
			&st::groupCallJoinAsList,
			&st::groupCallMultiSelect);
	} else {
		controller->setStyleOverrides(
			&st::peerListJoinAsList,
			nullptr);
	}
	const auto content = box->addRow(
		object_ptr<PeerListContent>(box, controller),
		style::margins());
	delegate->setContent(content);
	controller->setDelegate(delegate);
	const auto &peer = info.peer;
	if ((context == Context::Create)
		&& (peer->isChannel() && peer->asChannel()->hasAdminRights())) {
		const auto makeLink = [](const QString &text) {
			return Ui::Text::Link(text);
		};
		const auto label = box->addRow(object_ptr<Ui::FlatLabel>(
			box,
			tr::lng_group_call_or_schedule(
				lt_link,
				(livestream
					? tr::lng_group_call_schedule_channel
					: tr::lng_group_call_schedule)(makeLink),
				Ui::Text::WithEntities),
			labelSt));
		label->setClickHandlerFilter([=](const auto&...) {
			auto withJoinAs = info;
			withJoinAs.joinAs = controller->selected();
			box->getDelegate()->show(
				Box(ScheduleGroupCallBox, withJoinAs, done));
			return false;
		});
	}
	auto next = (context == Context::Switch)
		? tr::lng_settings_save()
		: tr::lng_continue();
	box->addButton(std::move(next), [=] {
		auto copy = info;
		copy.joinAs = controller->selected();
		done(std::move(copy));
	});
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
}

[[nodiscard]] TextWithEntities CreateOrJoinConfirmation(
		not_null<PeerData*> peer,
		ChooseJoinAsProcess::Context context,
		bool joinAsAlreadyUsed) {
	const auto existing = peer->groupCall();
	if (!existing) {
		return { peer->isBroadcast()
			? tr::lng_group_call_create_sure_channel(tr::now)
			: tr::lng_group_call_create_sure(tr::now) };
	}
	const auto channel = peer->asChannel();
	const auto anonymouseAdmin = channel
		&& ((channel->isMegagroup() && channel->amAnonymous())
			|| (channel->isBroadcast()
				&& (channel->amCreator() || channel->hasAdminRights())));
	if (anonymouseAdmin && !joinAsAlreadyUsed) {
		return { tr::lng_group_call_join_sure_personal(tr::now) };
	} else if (context != ChooseJoinAsProcess::Context::JoinWithConfirm) {
		return {};
	}
	const auto name = !existing->title().isEmpty()
		? existing->title()
		: peer->name();
	return (peer->isBroadcast()
		? tr::lng_group_call_join_confirm_channel
		: tr::lng_group_call_join_confirm)(
		tr::now,
		lt_chat,
		Ui::Text::Bold(name),
		Ui::Text::WithEntities);
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
		std::shared_ptr<Ui::Show> show,
		Fn<void(JoinInfo)> done,
		PeerData *changingJoinAsFrom) {
	Expects(done != nullptr);

	const auto isScheduled = (context == Context::CreateScheduled);

	const auto session = &peer->session();
	if (_request) {
		if (_request->peer == peer && !isScheduled) {
			_request->context = context;
			_request->show = std::move(show);
			_request->done = std::move(done);
			_request->changingJoinAsFrom = changingJoinAsFrom;
			return;
		}
		session->api().request(_request->id).cancel();
		_request = nullptr;
	}

	const auto createRequest = [=, done = std::move(done)] {
		_request = std::make_unique<ChannelsListRequest>(ChannelsListRequest{
			.peer = peer,
			.show = show,
			.done = std::move(done),
			.context = context,
			.changingJoinAsFrom = changingJoinAsFrom });
	};

	if (isScheduled) {
		auto box = Box(
			ScheduleGroupCallBox,
			JoinInfo{ .peer = peer, .joinAs = peer },
			[=, createRequest = std::move(createRequest)](JoinInfo info) {
				createRequest();
				finish(info);
			});
		show->showBox(std::move(box));
		return;
	}

	createRequest();
	session->account().sessionChanges(
	) | rpl::start_with_next([=] {
		_request = nullptr;
	}, _request->lifetime);

	requestList();
}

void ChooseJoinAsProcess::requestList() {
	const auto session = &_request->peer->session();
	_request->id = session->api().request(MTPphone_GetGroupCallJoinAs(
		_request->peer->input
	)).done([=](const MTPphone_JoinAsPeers &result) {
		auto list = result.match([&](const MTPDphone_joinAsPeers &data) {
			session->data().processUsers(data.vusers());
			session->data().processChats(data.vchats());
			const auto &peers = data.vpeers().v;
			auto list = std::vector<not_null<PeerData*>>();
			list.reserve(peers.size());
			for (const auto &peer : peers) {
				const auto peerId = peerFromMTP(peer);
				if (const auto peer = session->data().peerLoaded(peerId)) {
					if (!ranges::contains(list, not_null{ peer })) {
						list.push_back(peer);
					}
				}
			}
			return list;
		});
		processList(std::move(list));
	}).fail([=] {
		finish({
			.peer = _request->peer,
			.joinAs = _request->peer->session().user(),
		});
	}).send();
}

void ChooseJoinAsProcess::finish(JoinInfo info) {
	const auto done = std::move(_request->done);
	const auto box = _request->box;
	_request = nullptr;
	done(std::move(info));
	if (const auto strong = box.data()) {
		strong->closeBox();
	}
}

void ChooseJoinAsProcess::processList(
		std::vector<not_null<PeerData*>> &&list) {
	const auto session = &_request->peer->session();
	const auto peer = _request->peer;
	const auto self = peer->session().user();
	auto info = JoinInfo{ .peer = peer, .joinAs = self };
	const auto selectedId = peer->groupCallDefaultJoinAs();
	if (list.empty()) {
		Ui::Toast::Show(
			_request->show->toastParent(),
			Lang::Hard::ServerError());
		return;
	}
	info.joinAs = [&]() -> not_null<PeerData*> {
		const auto loaded = selectedId
			? session->data().peerLoaded(selectedId)
			: nullptr;
		const auto changingJoinAsFrom = _request->changingJoinAsFrom;
		return (changingJoinAsFrom
			&& ranges::contains(list, not_null{ changingJoinAsFrom }))
			? not_null(changingJoinAsFrom)
			: (loaded && ranges::contains(list, not_null{ loaded }))
			? not_null(loaded)
			: ranges::contains(list, self)
			? self
			: list.front();
	}();
	info.possibleJoinAs = std::move(list);

	const auto onlyByMe = (info.possibleJoinAs.size() == 1)
		&& (info.possibleJoinAs.front() == self);

	// We already joined this voice chat, just rejoin with the same.
	const auto byAlreadyUsed = selectedId
		&& (info.joinAs->id == selectedId)
		&& (peer->groupCall() != nullptr);

	if (!_request->changingJoinAsFrom && (onlyByMe || byAlreadyUsed)) {
		auto confirmation = CreateOrJoinConfirmation(
			peer,
			_request->context,
			byAlreadyUsed);
		if (confirmation.text.isEmpty()) {
			finish(info);
			return;
		}
		const auto livestream = peer->isBroadcast();
		const auto creating = !peer->groupCall();
		if (creating) {
			confirmation
				.append("\n\n")
				.append(tr::lng_group_call_or_schedule(
				tr::now,
				lt_link,
				Ui::Text::Link((livestream
					? tr::lng_group_call_schedule_channel
					: tr::lng_group_call_schedule)(tr::now)),
				Ui::Text::WithEntities));
		}
		const auto guard = base::make_weak(&_request->guard);
		const auto safeFinish = crl::guard(guard, [=] { finish(info); });
		const auto filter = [=](const auto &...) {
			if (guard) {
				_request->show->showBox(Box(
					ScheduleGroupCallBox,
					info,
					crl::guard(guard, [=](auto info) { finish(info); })));
			}
			return false;
		};
		auto box = Ui::MakeConfirmBox({
			.text = confirmation,
			.confirmed = crl::guard(guard, [=] { finish(info); }),
			.confirmText = (creating
				? tr::lng_create_group_create()
				: tr::lng_group_call_join()),
			.labelFilter = filter,
		});
		box->boxClosing(
		) | rpl::start_with_next([=] {
			_request = nullptr;
		}, _request->lifetime);

		_request->box = box.data();
		_request->show->showBox(std::move(box));
		return;
	}
	auto box = Box(
		ChooseJoinAsBox,
		_request->context,
		std::move(info),
		crl::guard(&_request->guard, [=](auto info) { finish(info); }));
	box->boxClosing(
	) | rpl::start_with_next([=] {
		_request = nullptr;
	}, _request->lifetime);

	_request->box = box.data();
	_request->show->showBox(std::move(box));
}

} // namespace Calls::Group
