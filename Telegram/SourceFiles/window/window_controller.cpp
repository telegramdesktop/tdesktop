/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "window/window_controller.h"

#include "window/main_window.h"
#include "info/info_memento.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/view/history_view_element.h"
#include "media/player/media_player_round_controller.h"
#include "data/data_session.h"
#include "data/data_feed.h"
#include "passport/passport_form_controller.h"
#include "boxes/calendar_box.h"
#include "mainwidget.h"
#include "mainwindow.h"
#include "auth_session.h"
#include "apiwrap.h"
#include "styles/style_window.h"
#include "styles/style_dialogs.h"

namespace Window {

DateClickHandler::DateClickHandler(Dialogs::Key chat, QDate date)
: _chat(chat)
, _date(date) {
}

void DateClickHandler::setDate(QDate date) {
	_date = date;
}

void DateClickHandler::onClick(ClickContext context) const {
	App::wnd()->controller()->showJumpToDate(_chat, _date);
}

Controller::Controller(not_null<MainWindow*> window)
: _window(window) {
	Auth().data().animationPlayInlineRequest(
	) | rpl::start_with_next([this](auto item) {
		if (const auto video = roundVideo(item)) {
			video->pauseResume();
		} else {
			startRoundVideo(item);
		}
	}, lifetime());
}

void Controller::setActiveChatEntry(Dialogs::RowDescriptor row) {
	_activeChatEntry = row;
}

void Controller::setActiveChatEntry(Dialogs::Key key) {
	setActiveChatEntry({ key, FullMsgId() });
}

Dialogs::RowDescriptor Controller::activeChatEntryCurrent() const {
	return _activeChatEntry.current();
}

Dialogs::Key Controller::activeChatCurrent() const {
	return activeChatEntryCurrent().key;
}

auto Controller::activeChatEntryChanges() const
-> rpl::producer<Dialogs::RowDescriptor> {
	return _activeChatEntry.changes();
}

rpl::producer<Dialogs::Key> Controller::activeChatChanges() const {
	return activeChatEntryChanges(
	) | rpl::map([](const Dialogs::RowDescriptor &value) {
		return value.key;
	}) | rpl::distinct_until_changed();
}

auto Controller::activeChatEntryValue() const
-> rpl::producer<Dialogs::RowDescriptor> {
	return _activeChatEntry.value();
}

rpl::producer<Dialogs::Key> Controller::activeChatValue() const {
	return activeChatEntryValue(
	) | rpl::map([](const Dialogs::RowDescriptor &value) {
		return value.key;
	}) | rpl::distinct_until_changed();
}

void Controller::enableGifPauseReason(GifPauseReason reason) {
	if (!(_gifPauseReasons & reason)) {
		auto notify = (static_cast<int>(_gifPauseReasons) < static_cast<int>(reason));
		_gifPauseReasons |= reason;
		if (notify) {
			_gifPauseLevelChanged.notify();
		}
	}
}

void Controller::disableGifPauseReason(GifPauseReason reason) {
	if (_gifPauseReasons & reason) {
		_gifPauseReasons &= ~reason;
		if (_gifPauseReasons < reason) {
			_gifPauseLevelChanged.notify();
		}
	}
}

bool Controller::isGifPausedAtLeastFor(GifPauseReason reason) const {
	if (reason == GifPauseReason::Any) {
		return (_gifPauseReasons != 0) || !window()->isActive();
	}
	return (static_cast<int>(_gifPauseReasons) >= 2 * static_cast<int>(reason)) || !window()->isActive();
}

int Controller::dialogsSmallColumnWidth() const {
	return st::dialogsPadding.x() + st::dialogsPhotoSize + st::dialogsPadding.x();
}

int Controller::minimalThreeColumnWidth() const {
	return st::columnMinimalWidthLeft
		+ st::columnMinimalWidthMain
		+ st::columnMinimalWidthThird;
}

bool Controller::forceWideDialogs() const {
	if (dialogsListDisplayForced().value()) {
		return true;
	} else if (dialogsListFocused().value()) {
		return true;
	}
	return !App::main()->isMainSectionShown();
}

Controller::ColumnLayout Controller::computeColumnLayout() const {
	auto layout = Adaptive::WindowLayout::OneColumn;

	auto bodyWidth = window()->bodyWidget()->width();
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
		if (!Auth().settings().tabbedSelectorSectionEnabled()
			&& !Auth().settings().thirdSectionInfoEnabled()) {
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

int Controller::countDialogsWidthFromRatio(int bodyWidth) const {
	auto result = qRound(bodyWidth * Auth().settings().dialogsWidthRatio());
	accumulate_max(result, st::columnMinimalWidthLeft);
//	accumulate_min(result, st::columnMaximalWidthLeft);
	return result;
}

int Controller::countThirdColumnWidthFromRatio(int bodyWidth) const {
	auto result = Auth().settings().thirdColumnWidth();
	accumulate_max(result, st::columnMinimalWidthThird);
	accumulate_min(result, st::columnMaximalWidthThird);
	return result;
}

Controller::ShrinkResult Controller::shrinkDialogsAndThirdColumns(
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

bool Controller::canShowThirdSection() const {
	auto currentLayout = computeColumnLayout();
	auto minimalExtendBy = minimalThreeColumnWidth()
		- currentLayout.bodyWidth;
	return (minimalExtendBy <= window()->maximalExtendBy());
}

bool Controller::canShowThirdSectionWithoutResize() const {
	auto currentWidth = computeColumnLayout().bodyWidth;
	return currentWidth >= minimalThreeColumnWidth();
}

bool Controller::takeThirdSectionFromLayer() {
	return App::wnd()->takeThirdSectionFromLayer();
}

void Controller::resizeForThirdSection() {
	if (Adaptive::ThreeColumn()) {
		return;
	}

	auto layout = computeColumnLayout();
	auto tabbedSelectorSectionEnabled =
		Auth().settings().tabbedSelectorSectionEnabled();
	auto thirdSectionInfoEnabled =
		Auth().settings().thirdSectionInfoEnabled();
	Auth().settings().setTabbedSelectorSectionEnabled(false);
	Auth().settings().setThirdSectionInfoEnabled(false);

	auto wanted = countThirdColumnWidthFromRatio(layout.bodyWidth);
	auto minimal = st::columnMinimalWidthThird;
	auto extendBy = wanted;
	auto extendedBy = [&] {
		// Best - extend by third column without moving the window.
		// Next - extend by minimal third column without moving.
		// Next - show third column inside the window without moving.
		// Last - extend with moving.
		if (window()->canExtendNoMove(wanted)) {
			return window()->tryToExtendWidthBy(wanted);
		} else if (window()->canExtendNoMove(minimal)) {
			extendBy = minimal;
			return window()->tryToExtendWidthBy(minimal);
		} else if (layout.bodyWidth >= minimalThreeColumnWidth()) {
			return 0;
		}
		return window()->tryToExtendWidthBy(minimal);
	}();
	if (extendedBy) {
		if (extendBy != Auth().settings().thirdColumnWidth()) {
			Auth().settings().setThirdColumnWidth(extendBy);
		}
		auto newBodyWidth = layout.bodyWidth + extendedBy;
		auto currentRatio = Auth().settings().dialogsWidthRatio();
		Auth().settings().setDialogsWidthRatio(
			(currentRatio * layout.bodyWidth) / newBodyWidth);
	}
	auto savedValue = (extendedBy == extendBy) ? -1 : extendedBy;
	Auth().settings().setThirdSectionExtendedBy(savedValue);

	Auth().settings().setTabbedSelectorSectionEnabled(
		tabbedSelectorSectionEnabled);
	Auth().settings().setThirdSectionInfoEnabled(
		thirdSectionInfoEnabled);
}

void Controller::closeThirdSection() {
	auto newWindowSize = window()->size();
	auto layout = computeColumnLayout();
	if (layout.windowLayout == Adaptive::WindowLayout::ThreeColumn) {
		auto noResize = window()->isFullScreen()
			|| window()->isMaximized();
		auto savedValue = Auth().settings().thirdSectionExtendedBy();
		auto extendedBy = (savedValue == -1)
			? layout.thirdWidth
			: savedValue;
		auto newBodyWidth = noResize
			? layout.bodyWidth
			: (layout.bodyWidth - extendedBy);
		auto currentRatio = Auth().settings().dialogsWidthRatio();
		Auth().settings().setDialogsWidthRatio(
			(currentRatio * layout.bodyWidth) / newBodyWidth);
		newWindowSize = QSize(
			window()->width() + (newBodyWidth - layout.bodyWidth),
			window()->height());
	}
	Auth().settings().setTabbedSelectorSectionEnabled(false);
	Auth().settings().setThirdSectionInfoEnabled(false);
	Auth().saveSettingsDelayed();
	if (window()->size() != newWindowSize) {
		window()->resize(newWindowSize);
	} else {
		updateColumnLayout();
	}
}

void Controller::showJumpToDate(Dialogs::Key chat, QDate requestedDate) {
	const auto currentPeerDate = [&] {
		if (const auto history = chat.history()) {
			if (history->scrollTopItem) {
				return history->scrollTopItem->dateTime().date();
			} else if (history->loadedAtTop()
				&& !history->isEmpty()
				&& history->peer->migrateFrom()) {
				if (const auto migrated = App::historyLoaded(history->peer->migrateFrom())) {
					if (migrated->scrollTopItem) {
						// We're up in the migrated history.
						// So current date is the date of first message here.
						return history->blocks.front()->messages.front()->dateTime().date();
					}
				}
			} else if (history->chatsListTimeId() != 0) {
				return ParseDateTime(history->chatsListTimeId()).date();
			}
		} else if (const auto feed = chat.feed()) {
			/*if (chatScrollPosition(feed)) { // #TODO feeds save position

			} else */if (feed->chatsListTimeId() != 0) {
				return ParseDateTime(feed->chatsListTimeId()).date();
			}
		}
		return QDate::currentDate();
	};
	const auto maxPeerDate = [](Dialogs::Key chat) {
		if (auto history = chat.history()) {
			if (const auto channel = history->peer->migrateTo()) {
				history = App::historyLoaded(channel);
			}
			if (history && history->chatsListTimeId() != 0) {
				return ParseDateTime(history->chatsListTimeId()).date();
			}
		} else if (const auto feed = chat.feed()) {
			if (feed->chatsListTimeId() != 0) {
				return ParseDateTime(feed->chatsListTimeId()).date();
			}
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
				if (const auto history = App::historyLoaded(chat)) {
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
		} else if (const auto feed = chat.feed()) {
			return startDate();
		}
		return startDate();
	};
	const auto highlighted = requestedDate.isNull()
		? currentPeerDate()
		: requestedDate;
	const auto month = highlighted;
	auto callback = [=](const QDate &date) {
		Auth().api().jumpToDate(chat, date);
	};
	auto box = Box<CalendarBox>(
		month,
		highlighted,
		std::move(callback));
	box->setMinDate(minPeerDate(chat));
	box->setMaxDate(maxPeerDate(chat));
	Ui::show(std::move(box));
}

void Controller::showPassportForm(const Passport::FormRequest &request) {
	_passportForm = std::make_unique<Passport::FormController>(
		this,
		request);
	_passportForm->show();
}

void Controller::clearPassportForm() {
	_passportForm = nullptr;
}

void Controller::updateColumnLayout() {
	App::main()->updateColumnLayout();
}

void Controller::showPeerHistory(
		PeerId peerId,
		const SectionShow &params,
		MsgId msgId) {
	App::main()->ui_showPeerHistory(
		peerId,
		params,
		msgId);
}

void Controller::showPeerHistory(
		not_null<PeerData*> peer,
		const SectionShow &params,
		MsgId msgId) {
	showPeerHistory(
		peer->id,
		params,
		msgId);
}

void Controller::showPeerHistory(
		not_null<History*> history,
		const SectionShow &params,
		MsgId msgId) {
	showPeerHistory(
		history->peer->id,
		params,
		msgId);
}

void Navigation::showPeerInfo(
		PeerId peerId,
		const SectionShow &params) {
	//if (Adaptive::ThreeColumn()
	//	&& !Auth().settings().thirdSectionInfoEnabled()) {
	//	Auth().settings().setThirdSectionInfoEnabled(true);
	//	Auth().saveSettingsDelayed();
	//}
	showSection(Info::Memento(peerId), params);
}

void Navigation::showPeerInfo(
		not_null<PeerData*> peer,
		const SectionShow &params) {
	showPeerInfo(peer->id, params);
}

void Navigation::showPeerInfo(
		not_null<History*> history,
		const SectionShow &params) {
	showPeerInfo(history->peer->id, params);
}

void Controller::showSection(
		SectionMemento &&memento,
		const SectionShow &params) {
	if (!params.thirdColumn && App::wnd()->showSectionInExistingLayer(
			&memento,
			params)) {
		return;
	}
	App::main()->showSection(std::move(memento), params);
}

void Controller::showBackFromStack(const SectionShow &params) {
	chats()->showBackFromStack(params);
}

void Controller::showSpecialLayer(
		object_ptr<LayerWidget> &&layer,
		anim::type animated) {
	App::wnd()->showSpecialLayer(std::move(layer), animated);
}

not_null<MainWidget*> Controller::chats() const {
	return App::wnd()->chatsWidget();
}

bool Controller::startRoundVideo(not_null<HistoryItem*> context) {
	if (auto video = RoundController::TryStart(this, context)) {
		enableGifPauseReason(Window::GifPauseReason::RoundPlaying);
		_roundVideo = std::move(video);
		return true;
	}
	return false;
}

auto Controller::currentRoundVideo() const -> RoundController* {
	return _roundVideo.get();
}

auto Controller::roundVideo(not_null<const HistoryItem*> context) const
-> RoundController* {
	return roundVideo(context->fullId());
}

auto Controller::roundVideo(FullMsgId contextId) const -> RoundController* {
	if (const auto result = currentRoundVideo()) {
		if (result->contextId() == contextId) {
			return result;
		}
	}
	return nullptr;
}

void Controller::roundVideoFinished(not_null<RoundController*> video) {
	if (video == _roundVideo.get()) {
		_roundVideo = nullptr;
		disableGifPauseReason(Window::GifPauseReason::RoundPlaying);
	}
}

Controller::~Controller() = default;

} // namespace Window
