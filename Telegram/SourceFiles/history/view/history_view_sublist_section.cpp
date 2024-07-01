/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_sublist_section.h"

#include "main/main_session.h"
#include "core/application.h"
#include "core/shortcuts.h"
#include "data/data_message_reaction_id.h"
#include "data/data_saved_messages.h"
#include "data/data_saved_sublist.h"
#include "data/data_session.h"
#include "data/data_peer_values.h"
#include "data/data_user.h"
#include "history/view/controls/history_view_compose_search.h"
#include "history/view/history_view_top_bar_widget.h"
#include "history/view/history_view_translate_bar.h"
#include "history/view/history_view_list_widget.h"
#include "history/history.h"
#include "history/history_item.h"
#include "lang/lang_keys.h"
#include "ui/chat/chat_style.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/shadow.h"
#include "window/window_session_controller.h"
#include "styles/style_chat.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_window.h"

namespace HistoryView {
namespace {

} // namespace

SublistMemento::SublistMemento(not_null<Data::SavedSublist*> sublist)
: _sublist(sublist) {
	const auto selfId = sublist->session().userPeerId();
	_list.setAroundPosition({
		.fullId = FullMsgId(selfId, ShowAtUnreadMsgId),
		.date = TimeId(0),
	});
}

object_ptr<Window::SectionWidget> SublistMemento::createWidget(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		Window::Column column,
		const QRect &geometry) {
	if (column == Window::Column::Third) {
		return nullptr;
	}
	auto result = object_ptr<SublistWidget>(
		parent,
		controller,
		_sublist);
	result->setInternalState(geometry, this);
	return result;
}

SublistWidget::SublistWidget(
	QWidget *parent,
	not_null<Window::SessionController*> controller,
	not_null<Data::SavedSublist*> sublist)
: Window::SectionWidget(parent, controller, sublist->peer())
, WindowListDelegate(controller)
, _sublist(sublist)
, _history(sublist->owner().history(sublist->session().user()))
, _topBar(this, controller)
, _topBarShadow(this)
, _translateBar(std::make_unique<TranslateBar>(this, controller, _history))
, _scroll(std::make_unique<Ui::ScrollArea>(
	this,
	controller->chatStyle()->value(lifetime(), st::historyScroll),
	false))
, _cornerButtons(
		_scroll.get(),
		controller->chatStyle(),
		static_cast<HistoryView::CornerButtonsDelegate*>(this)) {
	controller->chatStyle()->paletteChanged(
	) | rpl::start_with_next([=] {
		_scroll->updateBars();
	}, _scroll->lifetime());

	setupOpenChatButton();
	setupAboutHiddenAuthor();

	Window::ChatThemeValueFromPeer(
		controller,
		sublist->peer()
	) | rpl::start_with_next([=](std::shared_ptr<Ui::ChatTheme> &&theme) {
		_theme = std::move(theme);
		controller->setChatStyleTheme(_theme);
	}, lifetime());

	_topBar->setActiveChat(
		TopBarWidget::ActiveChat{
			.key = sublist,
			.section = Dialogs::EntryState::Section::SavedSublist,
		},
		nullptr);

	_topBar->move(0, 0);
	_topBar->resizeToWidth(width());
	_topBar->show();

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
	_topBar->searchRequest(
	) | rpl::start_with_next([=] {
		searchInSublist();
	}, _topBar->lifetime());

	_translateBar->raise();
	_topBarShadow->raise();
	controller->adaptive().value(
	) | rpl::start_with_next([=] {
		updateAdaptiveLayout();
	}, lifetime());

	_inner = _scroll->setOwnedWidget(object_ptr<ListWidget>(
		this,
		&controller->session(),
		static_cast<ListDelegate*>(this)));
	_scroll->move(0, _topBar->height());
	_scroll->show();
	_scroll->scrolls(
	) | rpl::start_with_next([=] {
		onScroll();
	}, lifetime());

	setupShortcuts();
	setupTranslateBar();
}

SublistWidget::~SublistWidget() = default;

void SublistWidget::setupOpenChatButton() {
	if (_sublist->peer()->isSavedHiddenAuthor()) {
		return;
	}
	_openChatButton = std::make_unique<Ui::FlatButton>(
		this,
		(_sublist->peer()->isBroadcast()
			? tr::lng_saved_open_channel(tr::now)
			: _sublist->peer()->isUser()
			? tr::lng_saved_open_chat(tr::now)
			: tr::lng_saved_open_group(tr::now)),
		st::historyComposeButton);

	_openChatButton->setClickedCallback([=] {
		controller()->showPeerHistory(
			_sublist->peer(),
			Window::SectionShow::Way::Forward);
	});
}

void SublistWidget::setupAboutHiddenAuthor() {
	if (!_sublist->peer()->isSavedHiddenAuthor()) {
		return;
	}
	_aboutHiddenAuthor = std::make_unique<Ui::RpWidget>(this);
	_aboutHiddenAuthor->paintRequest() | rpl::start_with_next([=] {
		auto p = QPainter(_aboutHiddenAuthor.get());
		auto rect = _aboutHiddenAuthor->rect();

		p.fillRect(rect, st::historyReplyBg);

		p.setFont(st::normalFont);
		p.setPen(st::windowSubTextFg);
		p.drawText(
			rect.marginsRemoved(
				QMargins(st::historySendPadding, 0, st::historySendPadding, 0)),
			tr::lng_saved_about_hidden(tr::now),
			style::al_center);
	}, _aboutHiddenAuthor->lifetime());
}

void SublistWidget::setupTranslateBar() {
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

void SublistWidget::cornerButtonsShowAtPosition(
		Data::MessagePosition position) {
	showAtPosition(position);
}

Data::Thread *SublistWidget::cornerButtonsThread() {
	return nullptr;
}

FullMsgId SublistWidget::cornerButtonsCurrentId() {
	return _lastShownAt;
}

bool SublistWidget::cornerButtonsIgnoreVisibility() {
	return animatingShow();
}

std::optional<bool> SublistWidget::cornerButtonsDownShown() {
	const auto top = _scroll->scrollTop() + st::historyToDownShownAfter;
	if (top < _scroll->scrollTopMax() || _cornerButtons.replyReturn()) {
		return true;
	} else if (_inner->loadedAtBottomKnown()) {
		return !_inner->loadedAtBottom();
	}
	return std::nullopt;
}

bool SublistWidget::cornerButtonsUnreadMayBeShown() {
	return _inner->loadedAtBottomKnown();
}

bool SublistWidget::cornerButtonsHas(CornerButtonType type) {
	return (type == CornerButtonType::Down);
}

void SublistWidget::showAtPosition(
		Data::MessagePosition position,
		FullMsgId originId) {
	showAtPosition(position, originId, {});
}

void SublistWidget::showAtPosition(
		Data::MessagePosition position,
		FullMsgId originItemId,
		const Window::SectionShow &params) {
	_lastShownAt = position.fullId;
	controller()->setActiveChatEntry(activeChat());
	_inner->showAtPosition(
		position,
		params,
		_cornerButtons.doneJumpFrom(position.fullId, originItemId));
}
void SublistWidget::updateAdaptiveLayout() {
	_topBarShadow->moveToLeft(
		controller()->adaptive().isOneColumn() ? 0 : st::lineWidth,
		_topBar->height());
}

not_null<Data::SavedSublist*> SublistWidget::sublist() const {
	return _sublist;
}

Dialogs::RowDescriptor SublistWidget::activeChat() const {
	const auto messageId = _lastShownAt
		? _lastShownAt
		: FullMsgId(_history->peer->id, ShowAtUnreadMsgId);
	return { _sublist, messageId };
}

QPixmap SublistWidget::grabForShowAnimation(
		const Window::SectionSlideParams &params) {
	_topBar->updateControlsVisibility();
	if (params.withTopBarShadow) _topBarShadow->hide();
	auto result = Ui::GrabWidget(this);
	if (params.withTopBarShadow) _topBarShadow->show();
	_translateBar->hide();
	return result;
}

void SublistWidget::checkActivation() {
	_inner->checkActivation();
}

void SublistWidget::doSetInnerFocus() {
	if (_composeSearch) {
		_composeSearch->setInnerFocus();
	} else {
		_inner->setFocus();
	}
}

bool SublistWidget::showInternal(
		not_null<Window::SectionMemento*> memento,
		const Window::SectionShow &params) {
	if (auto logMemento = dynamic_cast<SublistMemento*>(memento.get())) {
		if (logMemento->getSublist() == sublist()) {
			restoreState(logMemento);
			return true;
		}
	}
	return false;
}

bool SublistWidget::sameTypeAs(not_null<Window::SectionMemento*> memento) {
	return dynamic_cast<SublistMemento*>(memento.get()) != nullptr;
}

void SublistWidget::setInternalState(
		const QRect &geometry,
		not_null<SublistMemento*> memento) {
	setGeometry(geometry);
	Ui::SendPendingMoveResizeEvents(this);
	restoreState(memento);
}

bool SublistWidget::searchInChatEmbedded(Dialogs::Key chat, QString query) {
	const auto sublist = chat.sublist();
	if (!sublist || sublist != _sublist) {
		return false;
	} else if (_composeSearch) {
		_composeSearch->setQuery(query);
		_composeSearch->setInnerFocus();
		return true;
	}
	_composeSearch = std::make_unique<HistoryView::ComposeSearch>(
		this,
		controller(),
		_history,
		sublist->peer(),
		query);

	updateControlsGeometry();
	setInnerFocus();

	_composeSearch->activations(
	) | rpl::start_with_next([=](not_null<HistoryItem*> item) {
		controller()->showPeerHistory(
			item->history()->peer->id,
			::Window::SectionShow::Way::ClearStack,
			item->fullId().msg);
	}, _composeSearch->lifetime());

	_composeSearch->destroyRequests(
	) | rpl::take(
		1
	) | rpl::start_with_next([=] {
		_composeSearch = nullptr;

		updateControlsGeometry();
		setInnerFocus();
	}, _composeSearch->lifetime());

	return true;
}

std::shared_ptr<Window::SectionMemento> SublistWidget::createMemento() {
	auto result = std::make_shared<SublistMemento>(sublist());
	saveState(result.get());
	return result;
}

bool SublistWidget::showMessage(
		PeerId peerId,
		const Window::SectionShow &params,
		MsgId messageId) {
	const auto id = FullMsgId(_history->peer->id, messageId);
	const auto message = _history->owner().message(id);
	if (!message || message->savedSublist() != _sublist) {
		return false;
	}
	const auto originMessage = [&]() -> HistoryItem* {
		using OriginMessage = Window::SectionShow::OriginMessage;
		if (const auto origin = std::get_if<OriginMessage>(&params.origin)) {
			if (const auto returnTo = session().data().message(origin->id)) {
				if (returnTo->savedSublist() == _sublist) {
					return returnTo;
				}
			}
		}
		return nullptr;
	}();
	const auto currentReplyReturn = _cornerButtons.replyReturn();
	const auto originItemId = !originMessage
		? FullMsgId()
		: (currentReplyReturn != originMessage)
		? originMessage->fullId()
		: FullMsgId();
	showAtPosition(message->position(), originItemId, params);
	return true;
}

void SublistWidget::saveState(not_null<SublistMemento*> memento) {
	_inner->saveState(memento->list());
}

void SublistWidget::restoreState(not_null<SublistMemento*> memento) {
	_inner->restoreState(memento->list());
}

void SublistWidget::resizeEvent(QResizeEvent *e) {
	if (!width() || !height()) {
		return;
	}
	recountChatWidth();
	updateControlsGeometry();
}

void SublistWidget::recountChatWidth() {
	auto layout = (width() < st::adaptiveChatWideWidth)
		? Window::Adaptive::ChatLayout::Normal
		: Window::Adaptive::ChatLayout::Wide;
	controller()->adaptive().setChatLayout(layout);
}

void SublistWidget::updateControlsGeometry() {
	const auto contentWidth = width();

	const auto newScrollTop = _scroll->isHidden()
		? std::nullopt
		: base::make_optional(_scroll->scrollTop() + topDelta());
	_topBar->resizeToWidth(contentWidth);
	_topBarShadow->resize(contentWidth, st::lineWidth);

	auto bottom = height();
	if (_openChatButton) {
		_openChatButton->resizeToWidth(width());
		bottom -= _openChatButton->height();
		_openChatButton->move(0, bottom);
	}
	if (_aboutHiddenAuthor) {
		_aboutHiddenAuthor->resize(width(), st::historyUnblock.height);
		bottom -= _aboutHiddenAuthor->height();
		_aboutHiddenAuthor->move(0, bottom);
	}
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

void SublistWidget::paintEvent(QPaintEvent *e) {
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

void SublistWidget::onScroll() {
	if (_skipScrollEvent) {
		return;
	}
	updateInnerVisibleArea();
}

void SublistWidget::updateInnerVisibleArea() {
	const auto scrollTop = _scroll->scrollTop();
	_inner->setVisibleTopBottom(scrollTop, scrollTop + _scroll->height());
	_cornerButtons.updateJumpDownVisibility();
	_cornerButtons.updateUnreadThingsVisibility();
}

void SublistWidget::showAnimatedHook(
		const Window::SectionSlideParams &params) {
	_topBar->setAnimatingMode(true);
	if (params.withTopBarShadow) {
		_topBarShadow->show();
	}
}

void SublistWidget::showFinishedHook() {
	_topBar->setAnimatingMode(false);
	_inner->showFinished();
	_translateBar->show();
}

bool SublistWidget::floatPlayerHandleWheelEvent(QEvent *e) {
	return _scroll->viewportEvent(e);
}

QRect SublistWidget::floatPlayerAvailableRect() {
	return mapToGlobal(_scroll->geometry());
}

Context SublistWidget::listContext() {
	return Context::SavedSublist;
}

bool SublistWidget::listScrollTo(int top, bool syntetic) {
	top = std::clamp(top, 0, _scroll->scrollTopMax());
	if (_scroll->scrollTop() == top) {
		updateInnerVisibleArea();
		return false;
	}
	_scroll->scrollToY(top);
	return true;
}

void SublistWidget::listCancelRequest() {
	if (_inner && !_inner->getSelectedIds().empty()) {
		clearSelected();
		return;
	}
	controller()->showBackFromStack();
}

void SublistWidget::listDeleteRequest() {
	confirmDeleteSelected();
}

void SublistWidget::listTryProcessKeyInput(not_null<QKeyEvent*> e) {
}

rpl::producer<Data::MessagesSlice> SublistWidget::listSource(
		Data::MessagePosition aroundId,
		int limitBefore,
		int limitAfter) {
	const auto messageId = aroundId.fullId.msg
		? aroundId.fullId.msg
		: (ServerMaxMsgId - 1);
	return [=](auto consumer) {
		const auto pushSlice = [=] {
			auto result = Data::MessagesSlice();
			result.fullCount = _sublist->fullCount();
			_topBar->setCustomTitle(result.fullCount
				? tr::lng_forum_messages(
					tr::now,
					lt_count_decimal,
					*result.fullCount)
				: tr::lng_contacts_loading(tr::now));
			const auto &messages = _sublist->messages();
			const auto i = ranges::lower_bound(
				messages,
				messageId,
				ranges::greater(),
				[](not_null<HistoryItem*> item) { return item->id; });
			const auto before = int(end(messages) - i);
			const auto useBefore = std::min(before, limitBefore);
			const auto after = int(i - begin(messages));
			const auto useAfter = std::min(after, limitAfter);
			const auto from = i - useAfter;
			const auto till = i + useBefore;
			auto nearestDistance = std::numeric_limits<int64>::max();
			result.ids.reserve(useAfter + useBefore);
			for (auto j = till; j != from;) {
				const auto item = *--j;
				result.ids.push_back(item->fullId());
				const auto distance = std::abs((messageId - item->id).bare);
				if (nearestDistance > distance) {
					nearestDistance = distance;
					result.nearestToAround = result.ids.back();
				}
			}
			result.skippedAfter = after - useAfter;
			result.skippedBefore = result.fullCount
				? (*result.fullCount - after - useBefore)
				: std::optional<int>();
			if (!result.fullCount || useBefore < limitBefore) {
				_sublist->owner().savedMessages().loadMore(_sublist);
			}
			consumer.put_next(std::move(result));
		};
		auto lifetime = rpl::lifetime();
		_sublist->changes() | rpl::start_with_next(pushSlice, lifetime);
		pushSlice();
		return lifetime;
	};
}

bool SublistWidget::listAllowsMultiSelect() {
	return true;
}

bool SublistWidget::listIsItemGoodForSelection(
		not_null<HistoryItem*> item) {
	return item->isRegular() && !item->isService();
}

bool SublistWidget::listIsLessInOrder(
		not_null<HistoryItem*> first,
		not_null<HistoryItem*> second) {
	return first->id < second->id;
}

void SublistWidget::listSelectionChanged(SelectedItems &&items) {
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
	if ((state.count > 0) && _composeSearch) {
		_composeSearch->hideAnimated();
	}
}

void SublistWidget::listMarkReadTill(not_null<HistoryItem*> item) {
}

void SublistWidget::listMarkContentsRead(
	const base::flat_set<not_null<HistoryItem*>> &items) {
}

MessagesBarData SublistWidget::listMessagesBar(
		const std::vector<not_null<Element*>> &elements) {
	return {};
}

void SublistWidget::listContentRefreshed() {
}

void SublistWidget::listUpdateDateLink(
	ClickHandlerPtr &link,
	not_null<Element*> view) {
}

bool SublistWidget::listElementHideReply(not_null<const Element*> view) {
	return false;
}

bool SublistWidget::listElementShownUnread(not_null<const Element*> view) {
	return view->data()->unread(view->data()->history());
}

bool SublistWidget::listIsGoodForAroundPosition(
		not_null<const Element*> view) {
	return view->data()->isRegular();
}

void SublistWidget::listSendBotCommand(
	const QString &command,
	const FullMsgId &context) {
}

void SublistWidget::listSearch(
		const QString &query,
		const FullMsgId &context) {
	const auto inChat = Data::SearchTagFromQuery(query)
		? Dialogs::Key(_sublist)
		: Dialogs::Key();
	controller()->searchMessages(query, inChat);
}

void SublistWidget::listHandleViaClick(not_null<UserData*> bot) {
}

not_null<Ui::ChatTheme*> SublistWidget::listChatTheme() {
	return _theme.get();
}

CopyRestrictionType SublistWidget::listCopyRestrictionType(
		HistoryItem *item) {
	return CopyRestrictionTypeFor(_history->peer, item);
}

CopyRestrictionType SublistWidget::listCopyMediaRestrictionType(
		not_null<HistoryItem*> item) {
	return CopyMediaRestrictionTypeFor(_history->peer, item);
}

CopyRestrictionType SublistWidget::listSelectRestrictionType() {
	return SelectRestrictionTypeFor(_history->peer);
}

auto SublistWidget::listAllowedReactionsValue()
-> rpl::producer<Data::AllowedReactions> {
	return Data::PeerAllowedReactionsValue(_history->peer);
}

void SublistWidget::listShowPremiumToast(not_null<DocumentData*> document) {
}

void SublistWidget::listOpenPhoto(
		not_null<PhotoData*> photo,
		FullMsgId context) {
	controller()->openPhoto(photo, { context });
}

void SublistWidget::listOpenDocument(
		not_null<DocumentData*> document,
		FullMsgId context,
		bool showInMediaView) {
	controller()->openDocument(document, showInMediaView, { context });
}

void SublistWidget::listPaintEmpty(
	Painter &p,
	const Ui::ChatPaintContext &context) {
}

QString SublistWidget::listElementAuthorRank(not_null<const Element*> view) {
	return {};
}

bool SublistWidget::listElementHideTopicButton(
		not_null<const Element*> view) {
	return true;
}

History *SublistWidget::listTranslateHistory() {
	return _history;
}

void SublistWidget::listAddTranslatedItems(
	not_null<TranslateTracker*> tracker) {
}

void SublistWidget::confirmDeleteSelected() {
	ConfirmDeleteSelectedItems(_inner);
}

void SublistWidget::confirmForwardSelected() {
	ConfirmForwardSelectedItems(_inner);
}

void SublistWidget::clearSelected() {
	_inner->cancelSelection();
}

void SublistWidget::setupShortcuts() {
	Shortcuts::Requests(
	) | rpl::filter([=] {
		return Ui::AppInFocus()
			&& Ui::InFocusChain(this)
			&& !controller()->isLayerShown()
			&& (Core::App().activeWindow() == &controller()->window());
	}) | rpl::start_with_next([=](not_null<Shortcuts::Request*> request) {
		using Command = Shortcuts::Command;
		request->check(Command::Search, 1) && request->handle([=] {
			searchInSublist();
			return true;
		});
	}, lifetime());
}

void SublistWidget::searchInSublist() {
	controller()->searchInChat(_sublist);
}

} // namespace HistoryView
