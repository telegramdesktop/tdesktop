/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/feed/info_feed_channels_controllers.h"

#include "data/data_feed.h"
#include "data/data_session.h"
#include "info/info_controller.h"
#include "lang/lang_keys.h"
#include "history/history.h"
#include "window/window_peer_menu.h"
#include "ui/widgets/popup_menu.h"
#include "auth_session.h"
#include "mainwidget.h"
#include "apiwrap.h"
#include "styles/style_widgets.h"
#include "styles/style_info.h"
#include "styles/style_boxes.h"

namespace Info {
namespace FeedProfile {

class ChannelsController::Row final : public PeerListRow {
public:
	Row(not_null<History*> history);

	QSize actionSize() const override;
	QMargins actionMargins() const override;
	void paintAction(
		Painter &p,
		TimeMs ms,
		int x,
		int y,
		int outerWidth,
		bool selected,
		bool actionSelected) override;

	not_null<History*> history() const {
		return _history;
	}

private:
	not_null<History*> _history;

};

ChannelsController::Row::Row(not_null<History*> history)
: PeerListRow(history->peer)
, _history(history) {
}

QSize ChannelsController::Row::actionSize() const {
	return QRect(
			QPoint(),
			st::smallCloseIcon.size()).marginsAdded(
				st::infoFeedLeaveIconMargins).size();
}

QMargins ChannelsController::Row::actionMargins() const {
	return QMargins(
		0,
		(st::infoCommonGroupsList.item.height - actionSize().height()) / 2,
		0,
		0);
}

void ChannelsController::Row::paintAction(
		Painter &p,
		TimeMs ms,
		int x,
		int y,
		int outerWidth,
		bool selected,
		bool actionSelected) {
	if (selected) {
		x += st::infoFeedLeaveIconMargins.left();
		y += st::infoFeedLeaveIconMargins.top();
		(actionSelected
			? st::smallCloseIconOver
			: st::smallCloseIcon).paint(p, x, y, outerWidth);
	}
}

ChannelsController::ChannelsController(not_null<Controller*> controller)
: PeerListController()
, _controller(controller)
, _feed(_controller->key().feed()) {
	if (!_feed->channelsLoaded()) {
		Auth().api().requestFeedChannels(_feed);
	}
	_controller->setSearchEnabledByContent(false);
}

auto ChannelsController::createRow(not_null<History*> history)
-> std::unique_ptr<Row> {
	auto result = std::make_unique<Row>(history);
	result->setCustomStatus(QString());
	return result;
}

std::unique_ptr<PeerListRow> ChannelsController::createRestoredRow(
		not_null<PeerData*> peer) {
	return createRow(App::history(peer));
}

void ChannelsController::prepare() {
	setSearchNoResultsText(lang(lng_bot_groups_not_found));
	delegate()->peerListSetSearchMode(PeerListSearchMode::Enabled);
	delegate()->peerListSetTitle(langFactory(lng_info_feed_channels));

	rebuildRows();
	using Flag = Data::FeedUpdateFlag;
	Auth().data().feedUpdated(
	) | rpl::filter([=](const Data::FeedUpdate &update) {
		return (update.feed == _feed) && (update.flag == Flag::Channels);
	}) | rpl::filter([=] {
		return _feed->channelsLoaded();
	}) | rpl::start_with_next([=] {
		rebuildRows();
	}, lifetime());
}

void ChannelsController::rebuildRows() {
	if (!_feed->channelsLoaded()) {
		return;
	}
	const auto &channels = _feed->channels();
	auto count = delegate()->peerListFullRowsCount();
	for (auto i = 0; i != count;) {
		const auto row = delegate()->peerListRowAt(i);
		const auto peer = row->peer();
		if (ranges::find_if(channels, [=](not_null<History*> history) {
			return (history->peer == peer);
		}) != end(channels)) {
			++i;
		} else {
			delegate()->peerListRemoveRow(row);
			--count;
		}
	}
	for (const auto history : channels) {
		if (auto row = createRow(history)) {
			delegate()->peerListAppendRow(std::move(row));
		}
	}
	delegate()->peerListRefreshRows();
}

std::unique_ptr<PeerListState> ChannelsController::saveState() const {
	auto result = PeerListController::saveState();
	auto my = std::make_unique<SavedState>();
	using Flag = Data::FeedUpdateFlag;
	Auth().data().feedUpdated(
	) | rpl::filter([=](const Data::FeedUpdate &update) {
		return (update.feed == _feed) && (update.flag == Flag::Channels);
	}) | rpl::start_with_next([state = result.get()] {
		state->controllerState = nullptr;
	}, my->lifetime);
	result->controllerState = std::move(my);
	return result;
}

void ChannelsController::restoreState(
		std::unique_ptr<PeerListState> state) {
	PeerListController::restoreState(std::move(state));
}

void ChannelsController::rowClicked(not_null<PeerListRow*> row) {
	_controller->parentController()->showPeerHistory(
		row->peer(),
		Window::SectionShow::Way::Forward);
}

void ChannelsController::rowActionClicked(not_null<PeerListRow*> row) {
	Window::DeleteAndLeaveHandler(row->peer())();
}

base::unique_qptr<Ui::PopupMenu> ChannelsController::rowContextMenu(
		not_null<PeerListRow*> row) {
	auto my = static_cast<Row*>(row.get());
	auto channel = my->history()->peer->asChannel();

	auto result = base::make_unique_q<Ui::PopupMenu>(nullptr);
	Window::PeerMenuAddMuteAction(channel, [&](
			const QString &text,
			base::lambda<void()> handler) {
		return result->addAction(text, handler);
	});
	result->addAction(
		lang(lng_feed_ungroup),
		[=] { Window::ToggleChannelGrouping(channel, false); });

	result->addAction(
		lang(lng_profile_leave_channel),
		Window::DeleteAndLeaveHandler(channel));

	return result;
}

void FeedNotificationsController::Start(not_null<Data::Feed*> feed) {
	const auto initBox = [=](not_null<PeerListBox*> box) {
		box->addButton(langFactory(lng_settings_save), [=] {
			const auto main = App::main();
			const auto count = box->peerListFullRowsCount();
			for (auto i = 0; i != count; ++i) {
				const auto row = box->peerListRowAt(i);
				const auto peer = row->peer();
				const auto muted = !row->checked();
				if (muted != peer->isMuted()) {
					main->updateNotifySettings(
						peer,
						(muted
							? Data::NotifySettings::MuteChange::Mute
							: Data::NotifySettings::MuteChange::Unmute));
				}
			}
			box->closeBox();
		});
		box->addButton(langFactory(lng_cancel), [box] { box->closeBox(); });
	};
	Ui::show(Box<PeerListBox>(
		std::make_unique<FeedNotificationsController>(feed),
		initBox));
}

FeedNotificationsController::FeedNotificationsController(
	not_null<Data::Feed*> feed)
: _feed(feed) {
}

void FeedNotificationsController::prepare() {
	setSearchNoResultsText(lang(lng_blocked_list_not_found));
	delegate()->peerListSetSearchMode(PeerListSearchMode::Enabled);
	delegate()->peerListSetTitle(langFactory(lng_feed_notifications));

	loadMoreRows();
}

void FeedNotificationsController::loadMoreRows() {
	if (_preloadRequestId || _allLoaded) {
		return;
	}
	_preloadRequestId = request(MTPmessages_GetDialogs(
		MTP_flags(MTPmessages_GetDialogs::Flag::f_feed_id),
		MTP_int(_feed->id()),
		MTP_int(_preloadOffsetDate),
		MTP_int(_preloadOffsetId),
		_preloadPeer ? _preloadPeer->input : MTP_inputPeerEmpty(),
		MTP_int(Data::Feed::kChannelsLimit)
	)).done([=](const MTPmessages_Dialogs &result) {
		applyFeedDialogs(result);
		_preloadRequestId = 0;
	}).fail([=](const RPCError &error) {
		_preloadRequestId = 0;
	}).send();
}

void FeedNotificationsController::applyFeedDialogs(
		const MTPmessages_Dialogs &result) {
	const auto [dialogsList, messagesList] = [&] {
		const auto process = [&](const auto &data) {
			App::feedUsers(data.vusers);
			App::feedChats(data.vchats);
			return std::make_tuple(&data.vdialogs.v, &data.vmessages.v);
		};
		switch (result.type()) {
		case mtpc_messages_dialogs:
			_allLoaded = true;
			return process(result.c_messages_dialogs());

		case mtpc_messages_dialogsSlice:
			LOG(("API Error: "
				"Unexpected dialogsSlice in feed dialogs list."));
			return process(result.c_messages_dialogsSlice());
		}
		Unexpected("Type in FeedNotificationsController::applyFeedDialogs");
	}();

	App::feedMsgs(*messagesList, NewMessageLast);

	if (dialogsList->empty()) {
		_allLoaded = true;
	}
	auto channels = std::vector<not_null<ChannelData*>>();
	channels.reserve(dialogsList->size());
	for (const auto &dialog : *dialogsList) {
		switch (dialog.type()) {
		case mtpc_dialog: {
			if (const auto peerId = peerFromMTP(dialog.c_dialog().vpeer)) {
				if (peerIsChannel(peerId)) {
					const auto history = App::history(peerId);
					const auto channel = history->peer->asChannel();
					history->applyDialog(dialog.c_dialog());
					channels.push_back(channel);
				} else {
					LOG(("API Error: "
						"Unexpected non-channel in feed dialogs list."));
				}
			}
		} break;
		case mtpc_dialogFeed: {
			LOG(("API Error: Unexpected dialogFeed in feed dialogs list."));
		} break;
		default: Unexpected("Type in DialogsInner::dialogsReceived");
		}
	}
	if (!channels.empty()) {
		auto notMutedChannels = ranges::view::all(
			channels
		) | ranges::view::filter([](not_null<ChannelData*> channel) {
			return !channel->isMuted();
		});
		delegate()->peerListAddSelectedRows(notMutedChannels);
		for (const auto channel : channels) {
			delegate()->peerListAppendRow(createRow(channel));
		}
	}
	delegate()->peerListRefreshRows();
}

void FeedNotificationsController::rowClicked(not_null<PeerListRow*> row) {
	delegate()->peerListSetRowChecked(row, !row->checked());
}

std::unique_ptr<PeerListRow> FeedNotificationsController::createRow(
		not_null<ChannelData*> channel) {
	return std::make_unique<PeerListRow>(channel);
}

} // namespace FeedProfile
} // namespace Info
