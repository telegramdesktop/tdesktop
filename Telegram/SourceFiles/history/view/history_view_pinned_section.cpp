/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_pinned_section.h"

#include "history/view/history_view_top_bar_widget.h"
#include "history/view/history_view_list_widget.h"
#include "history/history.h"
#include "history/history_item_components.h"
#include "history/history_item.h"
#include "boxes/confirm_box.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/shadow.h"
#include "ui/layers/generic_box.h"
#include "ui/item_text_options.h"
#include "ui/toast/toast.h"
#include "ui/text/format_values.h"
#include "ui/text/text_utilities.h"
#include "ui/special_buttons.h"
#include "ui/ui_utility.h"
#include "ui/toasts/common_toasts.h"
#include "base/timer_rpl.h"
#include "apiwrap.h"
#include "window/window_session_controller.h"
#include "window/window_peer_menu.h"
#include "base/event_filter.h"
#include "base/call_delayed.h"
#include "core/file_utilities.h"
#include "main/main_session.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "data/data_chat.h"
#include "data/data_channel.h"
#include "data/data_changes.h"
#include "data/data_sparse_ids.h"
#include "data/data_shared_media.h"
#include "data/data_peer_values.h"
#include "storage/storage_account.h"
#include "platform/platform_specific.h"
#include "lang/lang_keys.h"
#include "facades.h"
#include "app.h"
#include "styles/style_chat.h"
#include "styles/style_window.h"
#include "styles/style_info.h"
#include "styles/style_boxes.h"

#include <QtCore/QMimeData>
#include <QtGui/QGuiApplication>

namespace HistoryView {
namespace {

} // namespace

PinnedMemento::PinnedMemento(
	not_null<History*> history,
	MsgId highlightId)
: _history(history)
, _highlightId(highlightId) {
	_list.setAroundPosition({
		.fullId = FullMsgId(
			history->channelId(),
			highlightId),
		.date = TimeId(0),
	});
}

object_ptr<Window::SectionWidget> PinnedMemento::createWidget(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		Window::Column column,
		const QRect &geometry) {
	if (column == Window::Column::Third) {
		return nullptr;
	}
	auto result = object_ptr<PinnedWidget>(
		parent,
		controller,
		_history);
	result->setInternalState(geometry, this);
	return result;
}

PinnedWidget::PinnedWidget(
	QWidget *parent,
	not_null<Window::SessionController*> controller,
	not_null<History*> history)
: Window::SectionWidget(parent, controller)
, _history(history->migrateToOrMe())
, _migratedPeer(_history->peer->migrateFrom())
, _topBar(this, controller)
, _topBarShadow(this)
, _scroll(std::make_unique<Ui::ScrollArea>(this, st::historyScroll, false))
, _clearButton(std::make_unique<Ui::FlatButton>(
	this,
	QString(),
	st::historyComposeButton))
, _scrollDown(_scroll.get(), st::historyToDown) {
	_topBar->setActiveChat(
		_history,
		TopBarWidget::Section::Pinned,
		nullptr);

	_topBar->move(0, 0);
	_topBar->resizeToWidth(width());
	_topBar->show();
	_topBar->setCustomTitle(tr::lng_contacts_loading(tr::now));

	_topBar->deleteSelectionRequest(
	) | rpl::start_with_next([=] {
		confirmDeleteSelected();
	}, _topBar->lifetime());
	_topBar->forwardSelectionRequest(
	) | rpl::start_with_next([=] {
		confirmForwardSelected();
	}, _topBar->lifetime());
	_topBar->clearSelectionRequest(
	) | rpl::start_with_next([=] {
		clearSelected();
	}, _topBar->lifetime());

	_topBarShadow->raise();
	updateAdaptiveLayout();
	subscribe(Adaptive::Changed(), [=] { updateAdaptiveLayout(); });

	_inner = _scroll->setOwnedWidget(object_ptr<ListWidget>(
		this,
		controller,
		static_cast<ListDelegate*>(this)));
	_scroll->move(0, _topBar->height());
	_scroll->show();
	connect(_scroll.get(), &Ui::ScrollArea::scrolled, [=] { onScroll(); });

	setupClearButton();
	setupScrollDownButton();
}

PinnedWidget::~PinnedWidget() = default;

void PinnedWidget::setupScrollDownButton() {
	_scrollDown->setClickedCallback([=] {
		scrollDownClicked();
	});
	base::install_event_filter(_scrollDown, [=](not_null<QEvent*> event) {
		if (event->type() != QEvent::Wheel) {
			return base::EventFilterResult::Continue;
		}
		return _scroll->viewportEvent(event)
			? base::EventFilterResult::Cancel
			: base::EventFilterResult::Continue;
	});
	updateScrollDownVisibility();
}

void PinnedWidget::setupClearButton() {
	Data::CanPinMessagesValue(
		_history->peer
	) | rpl::start_with_next([=] {
		refreshClearButtonText();
	}, _clearButton->lifetime());

	_clearButton->setClickedCallback([=] {
		if (!_history->peer->canPinMessages()) {
			const auto callback = [=] {
				controller()->showBackFromStack();
			};
			Window::HidePinnedBar(
				controller(),
				_history->peer,
				crl::guard(this, callback));
		} else {
			Window::UnpinAllMessages(controller(), _history);
		}
	});
}

void PinnedWidget::scrollDownClicked() {
	if (QGuiApplication::keyboardModifiers() == Qt::ControlModifier) {
		showAtEnd();
	//} else if (_replyReturn) {
	//	showAtPosition(_replyReturn->position());
	} else {
		showAtEnd();
	}
}

void PinnedWidget::showAtStart() {
	showAtPosition(Data::MinMessagePosition);
}

void PinnedWidget::showAtEnd() {
	showAtPosition(Data::MaxMessagePosition);
}

void PinnedWidget::showAtPosition(
		Data::MessagePosition position,
		HistoryItem *originItem) {
	if (!showAtPositionNow(position, originItem)) {
		_inner->showAroundPosition(position, [=] {
			return showAtPositionNow(position, originItem);
		});
	}
}

bool PinnedWidget::showAtPositionNow(
		Data::MessagePosition position,
		HistoryItem *originItem,
		anim::type animated) {
	using AnimatedScroll = HistoryView::ListWidget::AnimatedScroll;

	const auto item = position.fullId
		? _history->owner().message(position.fullId)
		: nullptr;
	const auto use = item ? item->position() : position;
	if (const auto scrollTop = _inner->scrollTopForPosition(use)) {
		const auto currentScrollTop = _scroll->scrollTop();
		const auto wanted = snap(*scrollTop, 0, _scroll->scrollTopMax());
		const auto fullDelta = (wanted - currentScrollTop);
		const auto limit = _scroll->height();
		const auto scrollDelta = snap(fullDelta, -limit, limit);
		const auto type = (animated == anim::type::instant)
			? AnimatedScroll::None
			: (std::abs(fullDelta) > limit)
			? AnimatedScroll::Part
			: AnimatedScroll::Full;
		_inner->scrollTo(
			wanted,
			use,
			scrollDelta,
			type);
		if (use != Data::MaxMessagePosition
			&& use != Data::UnreadMessagePosition) {
			_inner->highlightMessage(use.fullId);
		}
		return true;
	}
	return false;
}

void PinnedWidget::updateScrollDownVisibility() {
	if (animating()) {
		return;
	}

	const auto scrollDownIsVisible = [&]() -> std::optional<bool> {
		const auto top = _scroll->scrollTop() + st::historyToDownShownAfter;
		if (top < _scroll->scrollTopMax()) {
			return true;
		} else if (_inner->loadedAtBottomKnown()) {
			return !_inner->loadedAtBottom();
		}
		return std::nullopt;
	};
	const auto scrollDownIsShown = scrollDownIsVisible();
	if (!scrollDownIsShown) {
		return;
	}
	if (_scrollDownIsShown != *scrollDownIsShown) {
		_scrollDownIsShown = *scrollDownIsShown;
		_scrollDownShown.start(
			[=] { updateScrollDownPosition(); },
			_scrollDownIsShown ? 0. : 1.,
			_scrollDownIsShown ? 1. : 0.,
			st::historyToDownDuration);
	}
}

void PinnedWidget::updateScrollDownPosition() {
	// _scrollDown is a child widget of _scroll, not me.
	auto top = anim::interpolate(
		0,
		_scrollDown->height() + st::historyToDownPosition.y(),
		_scrollDownShown.value(_scrollDownIsShown ? 1. : 0.));
	_scrollDown->moveToRight(
		st::historyToDownPosition.x(),
		_scroll->height() - top);
	auto shouldBeHidden = !_scrollDownIsShown && !_scrollDownShown.animating();
	if (shouldBeHidden != _scrollDown->isHidden()) {
		_scrollDown->setVisible(!shouldBeHidden);
	}
}

void PinnedWidget::scrollDownAnimationFinish() {
	_scrollDownShown.stop();
	updateScrollDownPosition();
}

void PinnedWidget::updateAdaptiveLayout() {
	_topBarShadow->moveToLeft(
		Adaptive::OneColumn() ? 0 : st::lineWidth,
		_topBar->height());
}

not_null<History*> PinnedWidget::history() const {
	return _history;
}

Dialogs::RowDescriptor PinnedWidget::activeChat() const {
	return {
		_history,
		FullMsgId(_history->channelId(), ShowAtUnreadMsgId)
	};
}

QPixmap PinnedWidget::grabForShowAnimation(const Window::SectionSlideParams &params) {
	_topBar->updateControlsVisibility();
	if (params.withTopBarShadow) _topBarShadow->hide();
	auto result = Ui::GrabWidget(this);
	if (params.withTopBarShadow) _topBarShadow->show();
	return result;
}

void PinnedWidget::doSetInnerFocus() {
	_inner->setFocus();
}

bool PinnedWidget::showInternal(
		not_null<Window::SectionMemento*> memento,
		const Window::SectionShow &params) {
	if (auto logMemento = dynamic_cast<PinnedMemento*>(memento.get())) {
		if (logMemento->getHistory() == history()
			|| logMemento->getHistory()->migrateToOrMe() == history()) {
			restoreState(logMemento);
			return true;
		}
	}
	return false;
}

void PinnedWidget::setInternalState(
		const QRect &geometry,
		not_null<PinnedMemento*> memento) {
	setGeometry(geometry);
	Ui::SendPendingMoveResizeEvents(this);
	restoreState(memento);
}

std::unique_ptr<Window::SectionMemento> PinnedWidget::createMemento() {
	auto result = std::make_unique<PinnedMemento>(history());
	saveState(result.get());
	return result;
}

bool PinnedWidget::showMessage(
		PeerId peerId,
		const Window::SectionShow &params,
		MsgId messageId) {
	return false; // We want 'Go to original' to work.
}

void PinnedWidget::saveState(not_null<PinnedMemento*> memento) {
	_inner->saveState(memento->list());
}

void PinnedWidget::restoreState(not_null<PinnedMemento*> memento) {
	_inner->restoreState(memento->list());
	if (const auto highlight = memento->getHighlightId()) {
		const auto position = Data::MessagePosition{
			.fullId = ((highlight > 0 || !_migratedPeer)
				? FullMsgId(_history->channelId(), highlight)
				: FullMsgId(0, -highlight)),
			.date = TimeId(0),
		};
		_inner->showAroundPosition(position, [=] {
			return showAtPositionNow(position, nullptr, anim::type::instant);
		});
	}
}

void PinnedWidget::resizeEvent(QResizeEvent *e) {
	if (!width() || !height()) {
		return;
	}
	recountChatWidth();
	updateControlsGeometry();
}

void PinnedWidget::recountChatWidth() {
	auto layout = (width() < st::adaptiveChatWideWidth)
		? Adaptive::ChatLayout::Normal
		: Adaptive::ChatLayout::Wide;
	if (layout != Global::AdaptiveChatLayout()) {
		Global::SetAdaptiveChatLayout(layout);
		Adaptive::Changed().notify(true);
	}
}

void PinnedWidget::setMessagesCount(int count) {
	if (_messagesCount == count) {
		return;
	}
	_messagesCount = count;
	_topBar->setCustomTitle(
		tr::lng_pinned_messages_title(tr::now, lt_count, count));
	refreshClearButtonText();
}

void PinnedWidget::refreshClearButtonText() {
	const auto can = _history->peer->canPinMessages();
	_clearButton->setText(can
		? tr::lng_pinned_unpin_all(
			tr::now,
			lt_count,
			std::max(_messagesCount, 1)).toUpper()
		: tr::lng_pinned_hide_all(tr::now).toUpper());
}

void PinnedWidget::updateControlsGeometry() {
	const auto contentWidth = width();

	const auto newScrollTop = _scroll->isHidden()
		? std::nullopt
		: base::make_optional(_scroll->scrollTop() + topDelta());
	_topBar->resizeToWidth(contentWidth);
	_topBarShadow->resize(contentWidth, st::lineWidth);

	const auto bottom = height() - _clearButton->height();
	_clearButton->resizeToWidth(width());
	_clearButton->move(0, bottom);
	const auto controlsHeight = 0;
	const auto scrollY = _topBar->height();
	const auto scrollHeight = bottom - scrollY - controlsHeight;
	const auto scrollSize = QSize(contentWidth, scrollHeight);
	if (_scroll->size() != scrollSize) {
		_skipScrollEvent = true;
		_scroll->resize(scrollSize);
		_inner->resizeToWidth(scrollSize.width(), _scroll->height());
		_skipScrollEvent = false;
	}
	_scroll->move(0, scrollY);
	if (!_scroll->isHidden()) {
		if (newScrollTop) {
			_scroll->scrollToY(*newScrollTop);
		}
		updateInnerVisibleArea();
	}
	updateScrollDownPosition();
}

void PinnedWidget::paintEvent(QPaintEvent *e) {
	if (animating()) {
		SectionWidget::paintEvent(e);
		return;
	} else if (Ui::skipPaintEvent(this, e)) {
		return;
	}

	const auto aboveHeight = _topBar->height();
	const auto bg = e->rect().intersected(
		QRect(0, aboveHeight, width(), height() - aboveHeight));
	SectionWidget::PaintBackground(controller(), this, bg);
}

void PinnedWidget::onScroll() {
	if (_skipScrollEvent) {
		return;
	}
	updateInnerVisibleArea();
}

void PinnedWidget::updateInnerVisibleArea() {
	const auto scrollTop = _scroll->scrollTop();
	_inner->setVisibleTopBottom(scrollTop, scrollTop + _scroll->height());
	updateScrollDownVisibility();
}

void PinnedWidget::showAnimatedHook(
		const Window::SectionSlideParams &params) {
	_topBar->setAnimatingMode(true);
	if (params.withTopBarShadow) {
		_topBarShadow->show();
	}
}

void PinnedWidget::showFinishedHook() {
	_topBar->setAnimatingMode(false);
}

bool PinnedWidget::floatPlayerHandleWheelEvent(QEvent *e) {
	return _scroll->viewportEvent(e);
}

QRect PinnedWidget::floatPlayerAvailableRect() {
	return mapToGlobal(_scroll->geometry());
}

Context PinnedWidget::listContext() {
	return Context::Pinned;
}

void PinnedWidget::listScrollTo(int top) {
	if (_scroll->scrollTop() != top) {
		_scroll->scrollToY(top);
	} else {
		updateInnerVisibleArea();
	}
}

void PinnedWidget::listCancelRequest() {
	if (_inner && !_inner->getSelectedIds().empty()) {
		clearSelected();
		return;
	}
	controller()->showBackFromStack();
}

void PinnedWidget::listDeleteRequest() {
	confirmDeleteSelected();
}

rpl::producer<int> SharedMediaCountValue(
		not_null<PeerData*> peer,
		PeerData *migrated,
		Storage::SharedMediaType type) {
	auto aroundId = 0;
	auto limit = 0;
	auto updated = SharedMediaMergedViewer(
		&peer->session(),
		SharedMediaMergedKey(
			SparseIdsMergedSlice::Key(
				peer->id,
				migrated ? migrated->id : 0,
				aroundId),
			type),
		limit,
		limit
	) | rpl::map([](const SparseIdsMergedSlice &slice) {
		return slice.fullCount();
	}) | rpl::filter_optional();
	return rpl::single(0) | rpl::then(std::move(updated));
}

rpl::producer<Data::MessagesSlice> PinnedWidget::listSource(
		Data::MessagePosition aroundId,
		int limitBefore,
		int limitAfter) {
	const auto channelId = peerToChannel(_history->peer->id);
	const auto messageId = aroundId.fullId.msg
		? aroundId.fullId.msg
		: (ServerMaxMsgId - 1);

	return SharedMediaMergedViewer(
		&_history->session(),
		SharedMediaMergedKey(
			SparseIdsMergedSlice::Key(
				_history->peer->id,
				_migratedPeer ? _migratedPeer->id : 0,
				messageId),
			Storage::SharedMediaType::Pinned),
		limitBefore,
		limitAfter
	) | rpl::filter([=](const SparseIdsMergedSlice &slice) {
		const auto count = slice.fullCount();
		if (!count.has_value()) {
			return true;
		} else if (*count != 0) {
			setMessagesCount(*count);
			return true;
		} else {
			controller()->showBackFromStack();
			return false;
		}
	}) | rpl::map([=](SparseIdsMergedSlice &&slice) {
		auto result = Data::MessagesSlice();
		result.fullCount = slice.fullCount();
		result.skippedAfter = slice.skippedAfter();
		result.skippedBefore = slice.skippedBefore();
		const auto count = slice.size();
		result.ids.reserve(count);
		if (const auto msgId = slice.nearest(messageId)) {
			result.nearestToAround = *msgId;
		}
		for (auto i = 0; i != count; ++i) {
			result.ids.push_back(slice[i]);
		}
		return result;
	});
}

bool PinnedWidget::listAllowsMultiSelect() {
	return true;
}

bool PinnedWidget::listIsItemGoodForSelection(
		not_null<HistoryItem*> item) {
	return IsServerMsgId(item->id);
}

bool PinnedWidget::listIsLessInOrder(
		not_null<HistoryItem*> first,
		not_null<HistoryItem*> second) {
	return first->position() < second->position();
}

void PinnedWidget::listSelectionChanged(SelectedItems &&items) {
	HistoryView::TopBarWidget::SelectedState state;
	state.count = items.size();
	for (const auto item : items) {
		if (item.canDelete) {
			++state.canDeleteCount;
		}
		if (item.canForward) {
			++state.canForwardCount;
		}
	}
	_topBar->showSelected(state);
}

void PinnedWidget::listVisibleItemsChanged(HistoryItemsList &&items) {
}

MessagesBarData PinnedWidget::listMessagesBar(
		const std::vector<not_null<Element*>> &elements) {
	return {};
}

void PinnedWidget::listContentRefreshed() {
}

ClickHandlerPtr PinnedWidget::listDateLink(not_null<Element*> view) {
	return nullptr;
}

bool PinnedWidget::listElementHideReply(not_null<const Element*> view) {
	return false;
}

bool PinnedWidget::listElementShownUnread(not_null<const Element*> view) {
	return view->data()->unread();
}

bool PinnedWidget::listIsGoodForAroundPosition(
		not_null<const Element*> view) {
	return IsServerMsgId(view->data()->id);
}

void PinnedWidget::confirmDeleteSelected() {
	ConfirmDeleteSelectedItems(_inner);
}

void PinnedWidget::confirmForwardSelected() {
	ConfirmForwardSelectedItems(_inner);
}

void PinnedWidget::clearSelected() {
	_inner->cancelSelection();
}

} // namespace HistoryView
