/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_pinned_section.h"

#include "history/view/history_view_top_bar_widget.h"
#include "history/view/history_view_translate_bar.h"
#include "history/view/history_view_list_widget.h"
#include "history/history.h"
#include "history/history_item_components.h"
#include "history/history_item.h"
#include "ui/boxes/confirm_box.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/shadow.h"
#include "ui/widgets/buttons.h"
#include "ui/layers/generic_box.h"
#include "ui/item_text_options.h"
#include "ui/chat/chat_style.h"
#include "ui/toast/toast.h"
#include "ui/text/format_values.h"
#include "ui/text/text_utilities.h"
#include "ui/ui_utility.h"
#include "base/timer_rpl.h"
#include "apiwrap.h"
#include "window/window_adaptive.h"
#include "window/window_session_controller.h"
#include "window/window_peer_menu.h"
#include "base/event_filter.h"
#include "base/call_delayed.h"
#include "base/qt/qt_key_modifiers.h"
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
#include "styles/style_chat.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_window.h"
#include "styles/style_info.h"
#include "styles/style_boxes.h"

#include <QtCore/QMimeData>

namespace HistoryView {
namespace {

} // namespace

PinnedMemento::PinnedMemento(
	not_null<Data::Thread*> thread,
	UniversalMsgId highlightId)
: _thread(thread)
, _highlightId(highlightId) {
	_list.setAroundPosition({
		.fullId = FullMsgId(_thread->peer()->id, highlightId),
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
		_thread);
	result->setInternalState(geometry, this);
	return result;
}

Data::ForumTopic *PinnedMemento::topicForRemoveRequests() const {
	return _thread->asTopic();
}

PinnedWidget::PinnedWidget(
	QWidget *parent,
	not_null<Window::SessionController*> controller,
	not_null<Data::Thread*> thread)
: Window::SectionWidget(parent, controller, thread->peer())
, _thread(thread->migrateToOrMe())
, _history(thread->owningHistory())
, _migratedPeer(thread->asHistory()
	? thread->asHistory()->peer->migrateFrom()
	: nullptr)
, _topBar(this, controller)
, _topBarShadow(this)
, _translateBar(std::make_unique<TranslateBar>(this, controller, _history))
, _scroll(std::make_unique<Ui::ScrollArea>(
	this,
	controller->chatStyle()->value(lifetime(), st::historyScroll),
	false))
, _clearButton(std::make_unique<Ui::FlatButton>(
	this,
	QString(),
	st::historyComposeButton))
, _cornerButtons(
		_scroll.get(),
		controller->chatStyle(),
		static_cast<HistoryView::CornerButtonsDelegate*>(this)) {
	controller->chatStyle()->paletteChanged(
	) | rpl::start_with_next([=] {
		_scroll->updateBars();
	}, _scroll->lifetime());

	Window::ChatThemeValueFromPeer(
		controller,
		thread->peer()
	) | rpl::start_with_next([=](std::shared_ptr<Ui::ChatTheme> &&theme) {
		_theme = std::move(theme);
		controller->setChatStyleTheme(_theme);
	}, lifetime());

	_topBar->setActiveChat(
		TopBarWidget::ActiveChat{
			.key = _thread,
			.section = Dialogs::EntryState::Section::Pinned,
		},
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

	_translateBar->raise();
	_topBarShadow->raise();
	controller->adaptive().value(
	) | rpl::start_with_next([=] {
		updateAdaptiveLayout();
	}, lifetime());

	_inner = _scroll->setOwnedWidget(object_ptr<ListWidget>(
		this,
		controller,
		static_cast<ListDelegate*>(this)));
	_scroll->move(0, _topBar->height());
	_scroll->show();
	_scroll->scrolls(
	) | rpl::start_with_next([=] {
		onScroll();
	}, lifetime());

	setupClearButton();
	setupTranslateBar();
}

PinnedWidget::~PinnedWidget() = default;

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
				_thread->topicRootId(),
				crl::guard(this, callback));
		} else {
			Window::UnpinAllMessages(controller(), _thread);
		}
	});
}

void PinnedWidget::setupTranslateBar() {
	controller()->adaptive().oneColumnValue(
	) | rpl::start_with_next([=, raw = _translateBar.get()](bool one) {
		raw->setShadowGeometryPostprocess([=](QRect geometry) {
			if (!one) {
				geometry.setLeft(geometry.left() + st::lineWidth);
			}
			return geometry;
		});
	}, _translateBar->lifetime());

	_translateBarHeight = 0;
	_translateBar->heightValue(
	) | rpl::start_with_next([=](int height) {
		if (const auto delta = height - _translateBarHeight) {
			_translateBarHeight = height;
			setGeometryWithTopMoved(geometry(), delta);
		}
	}, _translateBar->lifetime());

	_translateBar->finishAnimating();
}

void PinnedWidget::cornerButtonsShowAtPosition(
		Data::MessagePosition position) {
	showAtPosition(position);
}

Data::Thread *PinnedWidget::cornerButtonsThread() {
	return _thread;
}

FullMsgId PinnedWidget::cornerButtonsCurrentId() {
	return {};
}

bool PinnedWidget::cornerButtonsIgnoreVisibility() {
	return animatingShow();
}

std::optional<bool> PinnedWidget::cornerButtonsDownShown() {
	const auto top = _scroll->scrollTop() + st::historyToDownShownAfter;
	if (top < _scroll->scrollTopMax() || _cornerButtons.replyReturn()) {
		return true;
	} else if (_inner->loadedAtBottomKnown()) {
		return !_inner->loadedAtBottom();
	}
	return std::nullopt;
}

bool PinnedWidget::cornerButtonsUnreadMayBeShown() {
	return _inner->loadedAtBottomKnown();
}

bool PinnedWidget::cornerButtonsHas(CornerButtonType type) {
	return (type == CornerButtonType::Down);
}

void PinnedWidget::showAtPosition(
		Data::MessagePosition position,
		FullMsgId originId) {
	_inner->showAtPosition(
		position,
		{},
		_cornerButtons.doneJumpFrom(position.fullId, originId));
}

void PinnedWidget::updateAdaptiveLayout() {
	_topBarShadow->moveToLeft(
		controller()->adaptive().isOneColumn() ? 0 : st::lineWidth,
		_topBar->height());
}

not_null<Data::Thread*> PinnedWidget::thread() const {
	return _thread;
}

Dialogs::RowDescriptor PinnedWidget::activeChat() const {
	return {
		_thread,
		FullMsgId(_history->peer->id, ShowAtUnreadMsgId)
	};
}

QPixmap PinnedWidget::grabForShowAnimation(const Window::SectionSlideParams &params) {
	_topBar->updateControlsVisibility();
	if (params.withTopBarShadow) _topBarShadow->hide();
	auto result = Ui::GrabWidget(this);
	if (params.withTopBarShadow) _topBarShadow->show();
	_translateBar->hide();
	return result;
}

void PinnedWidget::checkActivation() {
	_inner->checkActivation();
}

void PinnedWidget::doSetInnerFocus() {
	_inner->setFocus();
}

bool PinnedWidget::showInternal(
		not_null<Window::SectionMemento*> memento,
		const Window::SectionShow &params) {
	if (auto logMemento = dynamic_cast<PinnedMemento*>(memento.get())) {
		if (logMemento->getThread() == thread()
			|| logMemento->getThread()->migrateToOrMe() == thread()) {
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

std::shared_ptr<Window::SectionMemento> PinnedWidget::createMemento() {
	auto result = std::make_shared<PinnedMemento>(thread());
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
		_inner->showAtPosition(Data::MessagePosition{
			.fullId = ((highlight > 0 || !_migratedPeer)
				? FullMsgId(_history->peer->id, highlight)
				: FullMsgId(_migratedPeer->id, -highlight)),
			.date = TimeId(0),
		}, { Window::SectionShow::Way::Forward, anim::type::instant });
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
		? Window::Adaptive::ChatLayout::Normal
		: Window::Adaptive::ChatLayout::Wide;
	controller()->adaptive().setChatLayout(layout);
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
	auto top = _topBar->height();
	_translateBar->move(0, top);
	_translateBar->resizeToWidth(contentWidth);
	top += _translateBarHeight;
	const auto scrollHeight = bottom - top - controlsHeight;
	const auto scrollSize = QSize(contentWidth, scrollHeight);
	if (_scroll->size() != scrollSize) {
		_skipScrollEvent = true;
		_scroll->resize(scrollSize);
		_inner->resizeToWidth(scrollSize.width(), _scroll->height());
		_skipScrollEvent = false;
	}
	_scroll->move(0, top);
	if (!_scroll->isHidden()) {
		if (newScrollTop) {
			_scroll->scrollToY(*newScrollTop);
		}
		updateInnerVisibleArea();
	}

	_cornerButtons.updatePositions();
}

void PinnedWidget::paintEvent(QPaintEvent *e) {
	if (animatingShow()) {
		SectionWidget::paintEvent(e);
		return;
	} else if (controller()->contentOverlapped(this, e)) {
		return;
	}

	const auto aboveHeight = _topBar->height();
	const auto bg = e->rect().intersected(
		QRect(0, aboveHeight, width(), height() - aboveHeight));
	SectionWidget::PaintBackground(controller(), _theme.get(), this, bg);
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
	_cornerButtons.updateJumpDownVisibility();
	_cornerButtons.updateUnreadThingsVisibility();
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
	_inner->showFinished();
	_translateBar->show();
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

bool PinnedWidget::listScrollTo(int top, bool syntetic) {
	top = std::clamp(top, 0, _scroll->scrollTopMax());
	if (_scroll->scrollTop() == top) {
		updateInnerVisibleArea();
		return false;
	}
	_scroll->scrollToY(top);
	return true;
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

void PinnedWidget::listTryProcessKeyInput(not_null<QKeyEvent*> e) {
}

rpl::producer<Data::MessagesSlice> PinnedWidget::listSource(
		Data::MessagePosition aroundId,
		int limitBefore,
		int limitAfter) {
	const auto messageId = aroundId.fullId.msg
		? aroundId.fullId.msg
		: (ServerMaxMsgId - 1);

	return SharedMediaMergedViewer(
		&_thread->session(),
		SharedMediaMergedKey(
			SparseIdsMergedSlice::Key(
				_history->peer->id,
				_thread->topicRootId(),
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
	return item->isRegular() && !item->isService();
}

bool PinnedWidget::listIsLessInOrder(
		not_null<HistoryItem*> first,
		not_null<HistoryItem*> second) {
	return first->position() < second->position();
}

void PinnedWidget::listSelectionChanged(SelectedItems &&items) {
	HistoryView::TopBarWidget::SelectedState state;
	state.count = items.size();
	for (const auto &item : items) {
		if (item.canDelete) {
			++state.canDeleteCount;
		}
		if (item.canForward) {
			++state.canForwardCount;
		}
	}
	_topBar->showSelected(state);
}

void PinnedWidget::listMarkReadTill(not_null<HistoryItem*> item) {
}

void PinnedWidget::listMarkContentsRead(
	const base::flat_set<not_null<HistoryItem*>> &items) {
}

MessagesBarData PinnedWidget::listMessagesBar(
		const std::vector<not_null<Element*>> &elements) {
	return {};
}

void PinnedWidget::listContentRefreshed() {
}

void PinnedWidget::listUpdateDateLink(
	ClickHandlerPtr &link,
	not_null<Element*> view) {
}

bool PinnedWidget::listElementHideReply(not_null<const Element*> view) {
	if (const auto reply = view->data()->Get<HistoryMessageReply>()) {
		return !reply->fields().manualQuote
			&& (reply->messageId() == _thread->topicRootId());
	}
	return false;
}

bool PinnedWidget::listElementShownUnread(not_null<const Element*> view) {
	return view->data()->unread(view->data()->history());
}

bool PinnedWidget::listIsGoodForAroundPosition(
		not_null<const Element*> view) {
	return view->data()->isRegular();
}

void PinnedWidget::listSendBotCommand(
	const QString &command,
	const FullMsgId &context) {
}

void PinnedWidget::listSearch(
		const QString &query,
		const FullMsgId &context) {
	const auto inChat = _history->peer->isUser()
		? Dialogs::Key()
		: Dialogs::Key(_history);
	controller()->searchMessages(query, inChat);
}

void PinnedWidget::listHandleViaClick(not_null<UserData*> bot) {
}

not_null<Ui::ChatTheme*> PinnedWidget::listChatTheme() {
	return _theme.get();
}

CopyRestrictionType PinnedWidget::listCopyRestrictionType(
		HistoryItem *item) {
	return CopyRestrictionTypeFor(_history->peer, item);
}

CopyRestrictionType PinnedWidget::listCopyMediaRestrictionType(
		not_null<HistoryItem*> item) {
	return CopyMediaRestrictionTypeFor(_history->peer, item);
}

CopyRestrictionType PinnedWidget::listSelectRestrictionType() {
	return SelectRestrictionTypeFor(_history->peer);
}

auto PinnedWidget::listAllowedReactionsValue()
-> rpl::producer<Data::AllowedReactions> {
	return Data::PeerAllowedReactionsValue(_history->peer);
}

void PinnedWidget::listShowPremiumToast(not_null<DocumentData*> document) {
}

void PinnedWidget::listOpenPhoto(
		not_null<PhotoData*> photo,
		FullMsgId context) {
	controller()->openPhoto(photo, { context });
}

void PinnedWidget::listOpenDocument(
		not_null<DocumentData*> document,
		FullMsgId context,
		bool showInMediaView) {
	controller()->openDocument(document, showInMediaView, { context });
}

void PinnedWidget::listPaintEmpty(
	Painter &p,
	const Ui::ChatPaintContext &context) {
}

QString PinnedWidget::listElementAuthorRank(not_null<const Element*> view) {
	return {};
}

History *PinnedWidget::listTranslateHistory() {
	return _history;
}

void PinnedWidget::listAddTranslatedItems(
	not_null<TranslateTracker*> tracker) {
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
