/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "window/window_session_controller.h"

#include "boxes/peers/edit_peer_info_box.h"
#include "window/window_controller.h"
#include "window/main_window.h"
#include "window/window_filters_menu.h"
#include "info/info_memento.h"
#include "info/info_controller.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/view/history_view_element.h"
//#include "history/feed/history_feed_section.h" // #feed
#include "data/data_media_types.h"
#include "data/data_session.h"
#include "data/data_folder.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_chat_filters.h"
#include "data/data_photo.h" // requestAttachedStickerSets.
#include "passport/passport_form_controller.h"
#include "chat_helpers/tabbed_selector.h"
#include "core/shortcuts.h"
#include "base/unixtime.h"
#include "boxes/calendar_box.h"
#include "boxes/sticker_set_box.h" // requestAttachedStickerSets.
#include "boxes/confirm_box.h" // requestAttachedStickerSets.
#include "boxes/stickers_box.h" // requestAttachedStickerSets.
#include "lang/lang_keys.h" // requestAttachedStickerSets.
#include "mainwidget.h"
#include "mainwindow.h"
#include "main/main_session.h"
#include "main/main_session_settings.h"
#include "apiwrap.h"
#include "support/support_helper.h"
#include "facades.h"
#include "styles/style_window.h"
#include "styles/style_dialogs.h"

namespace Window {
namespace {

constexpr auto kMaxChatEntryHistorySize = 50;

} // namespace

DateClickHandler::DateClickHandler(Dialogs::Key chat, QDate date)
: _chat(chat)
, _date(date) {
}

void DateClickHandler::setDate(QDate date) {
	_date = date;
}

void DateClickHandler::onClick(ClickContext context) const {
	App::wnd()->sessionController()->showJumpToDate(_chat, _date);
}

SessionNavigation::SessionNavigation(not_null<Main::Session*> session)
: _session(session) {
}

Main::Session &SessionNavigation::session() const {
	return *_session;
}

void SessionNavigation::showPeerInfo(
		PeerId peerId,
		const SectionShow &params) {
	showPeerInfo(_session->data().peer(peerId), params);
}

void SessionNavigation::showPeerInfo(
		not_null<PeerData*> peer,
		const SectionShow &params) {
	//if (Adaptive::ThreeColumn()
	//	&& !_session->settings().thirdSectionInfoEnabled()) {
	//	_session->settings().setThirdSectionInfoEnabled(true);
	//	_session->saveSettingsDelayed();
	//}
	showSection(Info::Memento(peer), params);
}

void SessionNavigation::showPeerInfo(
		not_null<History*> history,
		const SectionShow &params) {
	showPeerInfo(history->peer->id, params);
}

void SessionNavigation::showSettings(
		Settings::Type type,
		const SectionShow &params) {
	showSection(
		Info::Memento(
			Info::Settings::Tag{ _session->user() },
			Info::Section(type)),
		params);
}

void SessionNavigation::showSettings(const SectionShow &params) {
	showSettings(Settings::Type::Main, params);
}

void SessionNavigation::showPollResults(
		not_null<PollData*> poll,
		FullMsgId contextId,
		const SectionShow &params) {
	showSection(Info::Memento(poll, contextId), params);
}

SessionController::SessionController(
	not_null<Main::Session*> session,
	not_null<Controller*> window)
: SessionNavigation(session)
, _window(window)
, _tabbedSelector(
	std::make_unique<ChatHelpers::TabbedSelector>(
		_window->widget(),
		this)) {
	init();

	subscribe(session->api().fullPeerUpdated(), [=](PeerData *peer) {
		if (peer == _showEditPeer) {
			_showEditPeer = nullptr;
			Ui::show(Box<EditPeerInfoBox>(this, peer));
		}
	});

	session->data().chatsListChanges(
	) | rpl::filter([=](Data::Folder *folder) {
		return (folder != nullptr)
			&& (folder == _openedFolder.current())
			&& folder->chatsList()->indexed()->empty();
	}) | rpl::start_with_next([=](Data::Folder *folder) {
		folder->updateChatListSortPosition();
		closeFolder();
	}, lifetime());

	session->data().chatsFilters().changed(
	) | rpl::start_with_next([=] {
		checkOpenedFilter();
		crl::on_main(this, [=] {
			refreshFiltersMenu();
		});
	}, session->lifetime());

	session->addWindow(this);
}

not_null<::MainWindow*> SessionController::widget() const {
	return _window->widget();
}

auto SessionController::tabbedSelector() const
-> not_null<ChatHelpers::TabbedSelector*> {
	return _tabbedSelector.get();
}

void SessionController::takeTabbedSelectorOwnershipFrom(
		not_null<QWidget*> parent) {
	if (_tabbedSelector->parent() == parent) {
		if (const auto chats = widget()->sessionContent()) {
			chats->returnTabbedSelector();
		}
		if (_tabbedSelector->parent() == parent) {
			_tabbedSelector->hide();
			_tabbedSelector->setParent(widget());
		}
	}
}

bool SessionController::hasTabbedSelectorOwnership() const {
	return (_tabbedSelector->parent() == widget());
}

void SessionController::showEditPeerBox(PeerData *peer) {
	_showEditPeer = peer;
	session().api().requestFullPeer(peer);
}

void SessionController::init() {
	if (session().supportMode()) {
		initSupportMode();
	}
}

void SessionController::initSupportMode() {
	session().supportHelper().registerWindow(this);

	Shortcuts::Requests(
	) | rpl::start_with_next([=](not_null<Shortcuts::Request*> request) {
		using C = Shortcuts::Command;

		request->check(C::SupportHistoryBack) && request->handle([=] {
			return chatEntryHistoryMove(-1);
		});
		request->check(C::SupportHistoryForward) && request->handle([=] {
			return chatEntryHistoryMove(1);
		});
	}, lifetime());
}

void SessionController::toggleFiltersMenu(bool enabled) {
	if (!enabled == !_filters) {
		return;
	} else if (enabled) {
		_filters = std::make_unique<FiltersMenu>(
			widget()->bodyWidget(),
			this);
	} else {
		_filters = nullptr;
	}
	_filtersMenuChanged.fire({});
}

void SessionController::refreshFiltersMenu() {
	toggleFiltersMenu(!session().data().chatsFilters().list().empty());
}

rpl::producer<> SessionController::filtersMenuChanged() const {
	return _filtersMenuChanged.events();
}

void SessionController::requestAttachedStickerSets(
		not_null<PhotoData*> photo) {
	session().api().request(_attachedStickerSetsRequestId).cancel();
	_attachedStickerSetsRequestId = session().api().request(
		MTPmessages_GetAttachedStickers(
			MTP_inputStickeredMediaPhoto(photo->mtpInput())
	)).done([=](const MTPVector<MTPStickerSetCovered> &result) {
		if (result.v.isEmpty()) {
			Ui::show(Box<InformBox>(tr::lng_stickers_not_found(tr::now)));
			return;
		} else if (result.v.size() > 1) {
			Ui::show(Box<StickersBox>(this, result));
			return;
		}
		// Single attached sticker pack.
		const auto setData = result.v.front().match([&](const auto &data) {
			return data.vset().match([&](const MTPDstickerSet &data) {
				return &data;
			});
		});

		const auto setId = (setData->vid().v && setData->vaccess_hash().v)
			? MTP_inputStickerSetID(setData->vid(), setData->vaccess_hash())
			: MTP_inputStickerSetShortName(setData->vshort_name());
		Ui::show(Box<StickerSetBox>(this, setId), Ui::LayerOption::KeepOther);
	}).fail([=](const RPCError &error) {
		Ui::show(Box<InformBox>(tr::lng_stickers_not_found(tr::now)));
	}).send();
}

void SessionController::checkOpenedFilter() {
	if (const auto filterId = activeChatsFilterCurrent()) {
		const auto &list = session().data().chatsFilters().list();
		const auto i = ranges::find(list, filterId, &Data::ChatFilter::id);
		if (i == end(list)) {
			setActiveChatsFilter(0);
		}
	}
}

bool SessionController::uniqueChatsInSearchResults() const {
	return session().supportMode()
		&& !session().settings().supportAllSearchResults()
		&& !searchInChat.current();
}

void SessionController::openFolder(not_null<Data::Folder*> folder) {
	if (_openedFolder.current() != folder) {
		resetFakeUnreadWhileOpened();
	}
	setActiveChatsFilter(0);
	_openedFolder = folder.get();
}

void SessionController::closeFolder() {
	_openedFolder = nullptr;
}

const rpl::variable<Data::Folder*> &SessionController::openedFolder() const {
	return _openedFolder;
}

void SessionController::setActiveChatEntry(Dialogs::RowDescriptor row) {
	const auto was = _activeChatEntry.current().key.history();
	const auto now = row.key.history();
	if (was && was != now) {
		was->setFakeUnreadWhileOpened(false);
	}
	_activeChatEntry = row;
	if (now) {
		now->setFakeUnreadWhileOpened(true);
	}
	if (session().supportMode()) {
		pushToChatEntryHistory(row);
	}
}

void SessionController::resetFakeUnreadWhileOpened() {
	if (const auto history = _activeChatEntry.current().key.history()) {
		history->setFakeUnreadWhileOpened(false);
	}
}

bool SessionController::chatEntryHistoryMove(int steps) {
	if (_chatEntryHistory.empty()) {
		return false;
	}
	const auto position = _chatEntryHistoryPosition + steps;
	if (!base::in_range(position, 0, int(_chatEntryHistory.size()))) {
		return false;
	}
	_chatEntryHistoryPosition = position;
	return jumpToChatListEntry(_chatEntryHistory[position]);
}

bool SessionController::jumpToChatListEntry(Dialogs::RowDescriptor row) {
	if (const auto history = row.key.history()) {
		Ui::showPeerHistory(history, row.fullId.msg);
		return true;
	//} else if (const auto feed = row.key.feed()) { // #feed
	//	if (const auto item = session().data().message(row.fullId)) {
	//		showSection(HistoryFeed::Memento(feed, item->position()));
	//	} else {
	//		showSection(HistoryFeed::Memento(feed));
	//	}
	}
	return false;
}

void SessionController::pushToChatEntryHistory(Dialogs::RowDescriptor row) {
	if (!_chatEntryHistory.empty()
		&& _chatEntryHistory[_chatEntryHistoryPosition] == row) {
		return;
	}
	_chatEntryHistory.resize(++_chatEntryHistoryPosition);
	_chatEntryHistory.push_back(row);
	if (_chatEntryHistory.size() > kMaxChatEntryHistorySize) {
		_chatEntryHistory.pop_front();
		--_chatEntryHistoryPosition;
	}
}

void SessionController::setActiveChatEntry(Dialogs::Key key) {
	setActiveChatEntry({ key, FullMsgId() });
}

Dialogs::RowDescriptor SessionController::activeChatEntryCurrent() const {
	return _activeChatEntry.current();
}

Dialogs::Key SessionController::activeChatCurrent() const {
	return activeChatEntryCurrent().key;
}

auto SessionController::activeChatEntryChanges() const
-> rpl::producer<Dialogs::RowDescriptor> {
	return _activeChatEntry.changes();
}

rpl::producer<Dialogs::Key> SessionController::activeChatChanges() const {
	return activeChatEntryChanges(
	) | rpl::map([](const Dialogs::RowDescriptor &value) {
		return value.key;
	}) | rpl::distinct_until_changed();
}

auto SessionController::activeChatEntryValue() const
-> rpl::producer<Dialogs::RowDescriptor> {
	return _activeChatEntry.value();
}

rpl::producer<Dialogs::Key> SessionController::activeChatValue() const {
	return activeChatEntryValue(
	) | rpl::map([](const Dialogs::RowDescriptor &value) {
		return value.key;
	}) | rpl::distinct_until_changed();
}

void SessionController::enableGifPauseReason(GifPauseReason reason) {
	if (!(_gifPauseReasons & reason)) {
		auto notify = (static_cast<int>(_gifPauseReasons) < static_cast<int>(reason));
		_gifPauseReasons |= reason;
		if (notify) {
			_gifPauseLevelChanged.notify();
		}
	}
}

void SessionController::disableGifPauseReason(GifPauseReason reason) {
	if (_gifPauseReasons & reason) {
		_gifPauseReasons &= ~reason;
		if (_gifPauseReasons < reason) {
			_gifPauseLevelChanged.notify();
		}
	}
}

bool SessionController::isGifPausedAtLeastFor(GifPauseReason reason) const {
	if (reason == GifPauseReason::Any) {
		return (_gifPauseReasons != 0) || !widget()->isActive();
	}
	return (static_cast<int>(_gifPauseReasons) >= 2 * static_cast<int>(reason)) || !widget()->isActive();
}

int SessionController::dialogsSmallColumnWidth() const {
	return st::dialogsPadding.x() + st::dialogsPhotoSize + st::dialogsPadding.x();
}

int SessionController::minimalThreeColumnWidth() const {
	return st::columnMinimalWidthLeft
		+ st::columnMinimalWidthMain
		+ st::columnMinimalWidthThird;
}

bool SessionController::forceWideDialogs() const {
	if (dialogsListDisplayForced().value()) {
		return true;
	} else if (dialogsListFocused().value()) {
		return true;
	}
	return !content()->isMainSectionShown();
}

auto SessionController::computeColumnLayout() const -> ColumnLayout {
	auto layout = Adaptive::WindowLayout::OneColumn;

	auto bodyWidth = widget()->bodyWidget()->width() - filtersWidth();
	auto dialogsWidth = 0, chatWidth = 0, thirdWidth = 0;

	auto useOneColumnLayout = [&] {
		auto minimalNormal = st::columnMinimalWidthLeft
			+ st::columnMinimalWidthMain;
		if (bodyWidth < minimalNormal) {
			return true;
		}
		return false;
	};

	auto useNormalLayout = [&] {
		// Used if useSmallColumnLayout() == false.
		if (bodyWidth < minimalThreeColumnWidth()) {
			return true;
		}
		if (!session().settings().tabbedSelectorSectionEnabled()
			&& !session().settings().thirdSectionInfoEnabled()) {
			return true;
		}
		return false;
	};

	if (useOneColumnLayout()) {
		dialogsWidth = chatWidth = bodyWidth;
	} else if (useNormalLayout()) {
		layout = Adaptive::WindowLayout::Normal;
		dialogsWidth = countDialogsWidthFromRatio(bodyWidth);
		accumulate_min(dialogsWidth, bodyWidth - st::columnMinimalWidthMain);
		chatWidth = bodyWidth - dialogsWidth;
	} else {
		layout = Adaptive::WindowLayout::ThreeColumn;
		dialogsWidth = countDialogsWidthFromRatio(bodyWidth);
		thirdWidth = countThirdColumnWidthFromRatio(bodyWidth);
		auto shrink = shrinkDialogsAndThirdColumns(
			dialogsWidth,
			thirdWidth,
			bodyWidth);
		dialogsWidth = shrink.dialogsWidth;
		thirdWidth = shrink.thirdWidth;

		chatWidth = bodyWidth - dialogsWidth - thirdWidth;
	}
	return { bodyWidth, dialogsWidth, chatWidth, thirdWidth, layout };
}

int SessionController::countDialogsWidthFromRatio(int bodyWidth) const {
	auto result = qRound(bodyWidth * session().settings().dialogsWidthRatio());
	accumulate_max(result, st::columnMinimalWidthLeft);
//	accumulate_min(result, st::columnMaximalWidthLeft);
	return result;
}

int SessionController::countThirdColumnWidthFromRatio(int bodyWidth) const {
	auto result = session().settings().thirdColumnWidth();
	accumulate_max(result, st::columnMinimalWidthThird);
	accumulate_min(result, st::columnMaximalWidthThird);
	return result;
}

SessionController::ShrinkResult SessionController::shrinkDialogsAndThirdColumns(
		int dialogsWidth,
		int thirdWidth,
		int bodyWidth) const {
	auto chatWidth = st::columnMinimalWidthMain;
	if (dialogsWidth + thirdWidth + chatWidth <= bodyWidth) {
		return { dialogsWidth, thirdWidth };
	}
	auto thirdWidthNew = ((bodyWidth - chatWidth) * thirdWidth)
		/ (dialogsWidth + thirdWidth);
	auto dialogsWidthNew = ((bodyWidth - chatWidth) * dialogsWidth)
		/ (dialogsWidth + thirdWidth);
	if (thirdWidthNew < st::columnMinimalWidthThird) {
		thirdWidthNew = st::columnMinimalWidthThird;
		dialogsWidthNew = bodyWidth - thirdWidthNew - chatWidth;
		Assert(dialogsWidthNew >= st::columnMinimalWidthLeft);
	} else if (dialogsWidthNew < st::columnMinimalWidthLeft) {
		dialogsWidthNew = st::columnMinimalWidthLeft;
		thirdWidthNew = bodyWidth - dialogsWidthNew - chatWidth;
		Assert(thirdWidthNew >= st::columnMinimalWidthThird);
	}
	return { dialogsWidthNew, thirdWidthNew };
}

bool SessionController::canShowThirdSection() const {
	auto currentLayout = computeColumnLayout();
	auto minimalExtendBy = minimalThreeColumnWidth()
		- currentLayout.bodyWidth;
	return (minimalExtendBy <= widget()->maximalExtendBy());
}

bool SessionController::canShowThirdSectionWithoutResize() const {
	auto currentWidth = computeColumnLayout().bodyWidth;
	return currentWidth >= minimalThreeColumnWidth();
}

bool SessionController::takeThirdSectionFromLayer() {
	return widget()->takeThirdSectionFromLayer();
}

void SessionController::resizeForThirdSection() {
	if (Adaptive::ThreeColumn()) {
		return;
	}

	auto layout = computeColumnLayout();
	auto tabbedSelectorSectionEnabled =
		session().settings().tabbedSelectorSectionEnabled();
	auto thirdSectionInfoEnabled =
		session().settings().thirdSectionInfoEnabled();
	session().settings().setTabbedSelectorSectionEnabled(false);
	session().settings().setThirdSectionInfoEnabled(false);

	auto wanted = countThirdColumnWidthFromRatio(layout.bodyWidth);
	auto minimal = st::columnMinimalWidthThird;
	auto extendBy = wanted;
	auto extendedBy = [&] {
		// Best - extend by third column without moving the window.
		// Next - extend by minimal third column without moving.
		// Next - show third column inside the window without moving.
		// Last - extend with moving.
		if (widget()->canExtendNoMove(wanted)) {
			return widget()->tryToExtendWidthBy(wanted);
		} else if (widget()->canExtendNoMove(minimal)) {
			extendBy = minimal;
			return widget()->tryToExtendWidthBy(minimal);
		} else if (layout.bodyWidth >= minimalThreeColumnWidth()) {
			return 0;
		}
		return widget()->tryToExtendWidthBy(minimal);
	}();
	if (extendedBy) {
		if (extendBy != session().settings().thirdColumnWidth()) {
			session().settings().setThirdColumnWidth(extendBy);
		}
		auto newBodyWidth = layout.bodyWidth + extendedBy;
		auto currentRatio = session().settings().dialogsWidthRatio();
		session().settings().setDialogsWidthRatio(
			(currentRatio * layout.bodyWidth) / newBodyWidth);
	}
	auto savedValue = (extendedBy == extendBy) ? -1 : extendedBy;
	session().settings().setThirdSectionExtendedBy(savedValue);

	session().settings().setTabbedSelectorSectionEnabled(
		tabbedSelectorSectionEnabled);
	session().settings().setThirdSectionInfoEnabled(
		thirdSectionInfoEnabled);
}

void SessionController::closeThirdSection() {
	auto newWindowSize = widget()->size();
	auto layout = computeColumnLayout();
	if (layout.windowLayout == Adaptive::WindowLayout::ThreeColumn) {
		auto noResize = widget()->isFullScreen()
			|| widget()->isMaximized();
		auto savedValue = session().settings().thirdSectionExtendedBy();
		auto extendedBy = (savedValue == -1)
			? layout.thirdWidth
			: savedValue;
		auto newBodyWidth = noResize
			? layout.bodyWidth
			: (layout.bodyWidth - extendedBy);
		auto currentRatio = session().settings().dialogsWidthRatio();
		session().settings().setDialogsWidthRatio(
			(currentRatio * layout.bodyWidth) / newBodyWidth);
		newWindowSize = QSize(
			widget()->width() + (newBodyWidth - layout.bodyWidth),
			widget()->height());
	}
	session().settings().setTabbedSelectorSectionEnabled(false);
	session().settings().setThirdSectionInfoEnabled(false);
	session().saveSettingsDelayed();
	if (widget()->size() != newWindowSize) {
		widget()->resize(newWindowSize);
	} else {
		updateColumnLayout();
	}
}

void SessionController::showJumpToDate(Dialogs::Key chat, QDate requestedDate) {
	const auto currentPeerDate = [&] {
		if (const auto history = chat.history()) {
			if (history->scrollTopItem) {
				return history->scrollTopItem->dateTime().date();
			} else if (history->loadedAtTop()
				&& !history->isEmpty()
				&& history->peer->migrateFrom()) {
				if (const auto migrated = history->owner().historyLoaded(history->peer->migrateFrom())) {
					if (migrated->scrollTopItem) {
						// We're up in the migrated history.
						// So current date is the date of first message here.
						return history->blocks.front()->messages.front()->dateTime().date();
					}
				}
			} else if (history->chatListTimeId() != 0) {
				return base::unixtime::parse(history->chatListTimeId()).date();
			}
		//} else if (const auto feed = chat.feed()) { // #feed
		//	if (chatScrollPosition(feed)) { // #TODO feeds save position

		//	} else if (feed->chatListTimeId() != 0) {
		//		return base::unixtime::parse(feed->chatListTimeId()).date();
		//	}
		}
		return QDate();
	}();
	const auto maxPeerDate = [](Dialogs::Key chat) {
		if (auto history = chat.history()) {
			if (const auto channel = history->peer->migrateTo()) {
				history = channel->owner().historyLoaded(channel);
			}
			if (history && history->chatListTimeId() != 0) {
				return base::unixtime::parse(history->chatListTimeId()).date();
			}
		//} else if (const auto feed = chat.feed()) { // #feed
		//	if (feed->chatListTimeId() != 0) {
		//		return base::unixtime::parse(feed->chatListTimeId()).date();
		//	}
		}
		return QDate::currentDate();
	};
	const auto minPeerDate = [](Dialogs::Key chat) {
		const auto startDate = [] {
			// Telegram was launched in August 2013 :)
			return QDate(2013, 8, 1);
		};
		if (const auto history = chat.history()) {
			if (const auto chat = history->peer->migrateFrom()) {
				if (const auto history = chat->owner().historyLoaded(chat)) {
					if (history->loadedAtTop()) {
						if (!history->isEmpty()) {
							return history->blocks.front()->messages.front()->dateTime().date();
						}
					} else {
						return startDate();
					}
				}
			}
			if (history->loadedAtTop()) {
				if (!history->isEmpty()) {
					return history->blocks.front()->messages.front()->dateTime().date();
				}
				return QDate::currentDate();
			}
		//} else if (const auto feed = chat.feed()) { // #feed
		//	return startDate();
		}
		return startDate();
	};
	const auto highlighted = !requestedDate.isNull()
		? requestedDate
		: !currentPeerDate.isNull()
		? currentPeerDate
		: QDate::currentDate();
	const auto month = highlighted;
	auto callback = [=](const QDate &date) {
		session().api().jumpToDate(chat, date);
	};
	auto box = Box<CalendarBox>(
		month,
		highlighted,
		std::move(callback));
	box->setMinDate(minPeerDate(chat));
	box->setMaxDate(maxPeerDate(chat));
	box->setBeginningButton(true);
	Ui::show(std::move(box));
}

void SessionController::showPassportForm(const Passport::FormRequest &request) {
	_passportForm = std::make_unique<Passport::FormController>(
		this,
		request);
	_passportForm->show();
}

void SessionController::clearPassportForm() {
	_passportForm = nullptr;
}

void SessionController::updateColumnLayout() {
	content()->updateColumnLayout();
}

void SessionController::showPeerHistory(
		PeerId peerId,
		const SectionShow &params,
		MsgId msgId) {
	content()->ui_showPeerHistory(
		peerId,
		params,
		msgId);
}

void SessionController::showPeerHistory(
		not_null<PeerData*> peer,
		const SectionShow &params,
		MsgId msgId) {
	showPeerHistory(
		peer->id,
		params,
		msgId);
}

void SessionController::showPeerHistory(
		not_null<History*> history,
		const SectionShow &params,
		MsgId msgId) {
	showPeerHistory(
		history->peer->id,
		params,
		msgId);
}

void SessionController::showSection(
		SectionMemento &&memento,
		const SectionShow &params) {
	if (!params.thirdColumn && widget()->showSectionInExistingLayer(
			&memento,
			params)) {
		return;
	}
	content()->showSection(std::move(memento), params);
}

void SessionController::showBackFromStack(const SectionShow &params) {
	content()->showBackFromStack(params);
}

void SessionController::showSpecialLayer(
		object_ptr<Ui::LayerWidget> &&layer,
		anim::type animated) {
	widget()->showSpecialLayer(std::move(layer), animated);
}

void SessionController::removeLayerBlackout() {
	widget()->ui_removeLayerBlackout();
}

not_null<MainWidget*> SessionController::content() const {
	return widget()->sessionContent();
}

void SessionController::setDefaultFloatPlayerDelegate(
		not_null<Media::Player::FloatDelegate*> delegate) {
	Expects(_defaultFloatPlayerDelegate == nullptr);

	_defaultFloatPlayerDelegate = delegate;
	_floatPlayers = std::make_unique<Media::Player::FloatController>(
		delegate);
}

void SessionController::replaceFloatPlayerDelegate(
		not_null<Media::Player::FloatDelegate*> replacement) {
	Expects(_floatPlayers != nullptr);

	_replacementFloatPlayerDelegate = replacement;
	_floatPlayers->replaceDelegate(replacement);
}

void SessionController::restoreFloatPlayerDelegate(
		not_null<Media::Player::FloatDelegate*> replacement) {
	Expects(_floatPlayers != nullptr);

	if (_replacementFloatPlayerDelegate == replacement) {
		_replacementFloatPlayerDelegate = nullptr;
		_floatPlayers->replaceDelegate(_defaultFloatPlayerDelegate);
	}
}

rpl::producer<FullMsgId> SessionController::floatPlayerClosed() const {
	Expects(_floatPlayers != nullptr);

	return _floatPlayers->closeEvents();
}

int SessionController::filtersWidth() const {
	return _filters ? st::windowFiltersWidth : 0;
}

rpl::producer<FilterId> SessionController::activeChatsFilter() const {
	return _activeChatsFilter.value();
}

FilterId SessionController::activeChatsFilterCurrent() const {
	return _activeChatsFilter.current();
}

void SessionController::setActiveChatsFilter(FilterId id) {
	if (activeChatsFilterCurrent() != id) {
		resetFakeUnreadWhileOpened();
	}
	_activeChatsFilter.force_assign(id);
	if (id) {
		closeFolder();
	}
	if (Adaptive::OneColumn()) {
		Ui::showChatsList(&session());
	}
}

SessionController::~SessionController() {
	session().api().request(_attachedStickerSetsRequestId).cancel();
}

} // namespace Window
