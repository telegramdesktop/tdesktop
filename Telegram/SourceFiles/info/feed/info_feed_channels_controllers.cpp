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
#include "ui/toast/toast.h"
#include "auth_session.h"
#include "mainwidget.h"
#include "apiwrap.h"
#include "styles/style_widgets.h"
#include "styles/style_info.h"
#include "styles/style_boxes.h"

namespace Info {
namespace FeedProfile {
namespace {

constexpr auto kChannelsInFeedMin = 4;

} // namespace

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
//		Auth().api().requestFeedChannels(_feed); // #feed
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
	setSearchNoResultsText(lang(lng_feed_channels_not_found));
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

	// Must not capture `this` here, because it dies before my->lifetime.
	Auth().data().feedUpdated(
	) | rpl::filter([feed = _feed](const Data::FeedUpdate &update) {
		return (update.feed == feed) && (update.flag == Flag::Channels);
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
	//result->addAction( // #feed
	//	lang(lng_feed_ungroup),
	//	[=] { Window::ToggleChannelGrouping(channel, false); });

	result->addAction(
		lang(lng_profile_leave_channel),
		Window::DeleteAndLeaveHandler(channel));

	return result;
}

void NotificationsController::Start(not_null<Data::Feed*> feed) {
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
		std::make_unique<NotificationsController>(feed),
		initBox));
}

NotificationsController::NotificationsController(
	not_null<Data::Feed*> feed)
: _feed(feed) {
}

void NotificationsController::prepare() {
	setSearchNoResultsText(lang(lng_feed_channels_not_found));
	delegate()->peerListSetSearchMode(PeerListSearchMode::Enabled);
	delegate()->peerListSetTitle(langFactory(lng_feed_notifications));

	loadMoreRows();
}

void NotificationsController::loadMoreRows() {
	if (_preloadRequestId || _allLoaded) {
		return;
	}
	//_preloadRequestId = request(MTPmessages_GetDialogs( // #feed
	//	MTP_flags(MTPmessages_GetDialogs::Flag::f_feed_id),
	//	MTP_int(_feed->id()),
	//	MTP_int(_preloadOffsetDate),
	//	MTP_int(_preloadOffsetId),
	//	_preloadPeer ? _preloadPeer->input : MTP_inputPeerEmpty(),
	//	MTP_int(Data::Feed::kChannelsLimit)
	//)).done([=](const MTPmessages_Dialogs &result) {
	//	applyFeedDialogs(result);
	//	_preloadRequestId = 0;
	//}).fail([=](const RPCError &error) {
	//	_preloadRequestId = 0;
	//}).send();
}

void NotificationsController::applyFeedDialogs(
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
		Unexpected("Type in NotificationsController::applyFeedDialogs");
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
		//case mtpc_dialogFeed: { // #feed
		//	LOG(("API Error: Unexpected dialogFeed in feed dialogs list."));
		//} break;
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

void NotificationsController::rowClicked(not_null<PeerListRow*> row) {
	delegate()->peerListSetRowChecked(row, !row->checked());
}

std::unique_ptr<PeerListRow> NotificationsController::createRow(
		not_null<ChannelData*> channel) {
	return std::make_unique<PeerListRow>(channel);
}

void EditController::Start(
		not_null<Data::Feed*> feed,
		ChannelData *channel) {
	const auto initBox = [=](not_null<PeerListBox*> box) {
		box->addButton(langFactory(lng_settings_save), [=] {
			auto channels = std::vector<not_null<ChannelData*>>();
			const auto main = App::main();
			const auto count = box->peerListFullRowsCount();
			for (auto i = 0; i != count; ++i) {
				const auto row = box->peerListRowAt(i);
				if (row->checked()) {
					channels.push_back(row->peer()->asChannel());
				}
			}
			if (channels.size() < kChannelsInFeedMin) {
				Ui::Toast::Show(lng_feed_select_more_channels(
					lt_count,
					kChannelsInFeedMin));
				return;
			}
			box->closeBox();
			//Auth().api().setFeedChannels(feed, channels); // #feed
		});
		box->addButton(langFactory(lng_cancel), [box] { box->closeBox(); });
	};
	Ui::show(Box<PeerListBox>(
		std::make_unique<EditController>(feed, channel),
		initBox));
}

EditController::EditController(
	not_null<Data::Feed*> feed,
	ChannelData *channel)
: _feed(feed) {
//, _startWithChannel(channel) { // #feed
}

void EditController::prepare() {
	setSearchNoResultsText(lang(lng_feed_channels_not_found));
	delegate()->peerListSetSearchMode(PeerListSearchMode::Enabled);
	delegate()->peerListSetTitle(langFactory(
		(_feed->channels().size() < kChannelsInFeedMin
			? lng_feed_create_new
			: lng_feed_edit_title)));

	loadMoreRows();
}

void EditController::loadMoreRows() {
	if (_preloadRequestId || _allLoaded) {
		return;
	}
	//const auto hash = 0; // #feed
	//_preloadRequestId = request(MTPchannels_GetFeedSources(
	//	MTP_flags(0),
	//	MTP_int(0),
	//	MTP_int(hash)
	//)).done([=](const MTPchannels_FeedSources &result) {
	//	applyFeedSources(result);
	//	_preloadRequestId = 0;
	//}).fail([=](const RPCError &error) {
	//	_preloadRequestId = 0;
	//}).send();
}
// #feed
//void EditController::applyFeedSources(
//		const MTPchannels_FeedSources &result) {
//	auto channels = std::vector<not_null<ChannelData*>>();
//
//	switch (result.type()) {
//	case mtpc_channels_feedSourcesNotModified:
//		LOG(("API Error: Unexpected channels.feedSourcesNotModified."));
//		break;
//
//	case mtpc_channels_feedSources: {
//		const auto &data = result.c_channels_feedSources();
//		Auth().api().applyFeedSources(data);
//
//		for (const auto &chat : data.vchats.v) {
//			if (chat.type() == mtpc_channel) {
//				channels.push_back(App::channel(chat.c_channel().vid.v));
//			}
//		}
//	} break;
//
//	default: Unexpected("Type in channels.getFeedSources response.");
//	}
//
//	_allLoaded = true;
//	if (channels.size() < kChannelsInFeedMin) {
//		setDescriptionText(lng_feed_too_few_channels(
//			lt_count,
//			kChannelsInFeedMin));
//		delegate()->peerListSetSearchMode(PeerListSearchMode::Disabled);
//	} else {
//		auto alreadyInFeed = ranges::view::all(
//			channels
//		) | ranges::view::filter([&](not_null<ChannelData*> channel) {
//			return (channel->feed() == _feed)
//				|| (channel == _startWithChannel);
//		});
//		delegate()->peerListAddSelectedRows(alreadyInFeed);
//		for (const auto channel : channels) {
//			delegate()->peerListAppendRow(createRow(channel));
//		}
//	}
//	delegate()->peerListRefreshRows();
//}

void EditController::rowClicked(not_null<PeerListRow*> row) {
	delegate()->peerListSetRowChecked(row, !row->checked());
}

std::unique_ptr<PeerListRow> EditController::createRow(
		not_null<ChannelData*> channel) {
	return std::make_unique<PeerListRow>(channel);
}

} // namespace FeedProfile
} // namespace Info
