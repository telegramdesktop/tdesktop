/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/feed/history_feed_section.h"

#include "history/view/history_view_top_bar_widget.h"
#include "history/view/history_view_list_widget.h"
#include "history/view/history_view_element.h"
#include "history/view/history_view_message.h"
#include "history/view/history_view_service_message.h"
#include "history/history_item.h"
#include "history/history_service.h"
#include "history/history_inner_widget.h"
#include "core/event_filter.h"
#include "core/shortcuts.h"
#include "lang/lang_keys.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/shadow.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/popup_menu.h"
#include "ui/special_buttons.h"
#include "boxes/confirm_box.h"
#include "window/window_controller.h"
#include "window/window_peer_menu.h"
#include "data/data_feed_messages.h"
#include "data/data_photo.h"
#include "data/data_document.h"
#include "data/data_session.h"
#include "storage/storage_feed_messages.h"
#include "mainwidget.h"
#include "apiwrap.h"
#include "auth_session.h"
#include "styles/style_widgets.h"
#include "styles/style_history.h"

namespace HistoryFeed {

Memento::Memento(
	not_null<Data::Feed*> feed,
	Data::MessagePosition position)
: _feed(feed)
, _position(position)
, _list(std::make_unique<HistoryView::ListMemento>(position)) {
}

Memento::~Memento() = default;

object_ptr<Window::SectionWidget> Memento::createWidget(
		QWidget *parent,
		not_null<Window::Controller*> controller,
		Window::Column column,
		const QRect &geometry) {
	if (column == Window::Column::Third) {
		return nullptr;
	}
	auto result = object_ptr<Widget>(parent, controller, _feed);
	result->setInternalState(geometry, this);
	return std::move(result);
}

Widget::Widget(
	QWidget *parent,
	not_null<Window::Controller*> controller,
	not_null<Data::Feed*> feed)
: Window::SectionWidget(parent, controller)
, _feed(feed)
, _scroll(this, st::historyScroll, false)
, _topBar(this, controller)
, _topBarShadow(this)
, _showNext(nullptr)
//, _showNext(
//	this,
//	lang(lng_feed_show_next).toUpper(),
//	st::historyComposeButton)
, _scrollDown(_scroll, st::historyToDown) {
	_topBar->setActiveChat(_feed);

	_topBar->move(0, 0);
	_topBar->resizeToWidth(width());
	_topBar->show();

	_topBar->forwardSelectionRequest(
	) | rpl::start_with_next([=] {
		forwardSelected();
	}, _topBar->lifetime());
	_topBar->deleteSelectionRequest(
	) | rpl::start_with_next([=] {
		confirmDeleteSelected();
	}, _topBar->lifetime());
	_topBar->clearSelectionRequest(
	) | rpl::start_with_next([=] {
		clearSelected();
	}, _topBar->lifetime());

	_topBarShadow->raise();
	updateAdaptiveLayout();
	subscribe(Adaptive::Changed(), [this] { updateAdaptiveLayout(); });

	_inner = _scroll->setOwnedWidget(
		object_ptr<HistoryView::ListWidget>(this, controller, this));
	_scroll->move(0, _topBar->height());
	_scroll->show();

	connect(
		_scroll,
		&Ui::ScrollArea::scrolled,
		this,
		[this] { onScroll(); });

	//_showNext->setClickedCallback([this] {
	//	// TODO feeds show next
	//});

	_feed->unreadPositionChanges(
	) | rpl::filter([=](const Data::MessagePosition &position) {
		return _undefinedAroundPosition && position;
	}) | rpl::start_with_next([=](const Data::MessagePosition &position) {
		auto memento = HistoryView::ListMemento(position);
		_inner->restoreState(&memento);
	}, lifetime());

	rpl::single(
		Data::FeedUpdate{ _feed, Data::FeedUpdateFlag::Channels }
	) | rpl::then(
		Auth().data().feedUpdated(
		) | rpl::filter([=](const Data::FeedUpdate &update) {
			return (update.feed == _feed)
				&& (update.flag == Data::FeedUpdateFlag::Channels);
		})
	) | rpl::start_with_next([=] {
		crl::on_main(this, [=] { checkForSingleChannelFeed(); });
	}, lifetime());

	setupScrollDownButton();
	setupShortcuts();
}

void Widget::setupScrollDownButton() {
	_scrollDown->setClickedCallback([=] {
		scrollDownClicked();
	});
	Core::InstallEventFilter(_scrollDown, [=](not_null<QEvent*> event) {
		if (event->type() == QEvent::Wheel) {
			return _scroll->viewportEvent(event);
		}
		return false;
	});
	updateScrollDownVisibility();
	_feed->unreadCountValue(
	) | rpl::start_with_next([=](int count) {
		_scrollDown->setUnreadCount(count);
	}, _scrollDown->lifetime());
}

void Widget::scrollDownClicked() {
	_currentMessageId = Data::MaxMessagePosition.fullId;
	showAtPosition(Data::MaxMessagePosition);
}

void Widget::showAtPosition(Data::MessagePosition position) {
	if (showAtPositionNow(position)) {
		if (const auto highlight = base::take(_highlightMessageId)) {
			_inner->highlightMessage(highlight);
		}
	} else {
		_nextAnimatedScrollPosition = position;
		_nextAnimatedScrollDelta = _inner->isBelowPosition(position)
			? -_scroll->height()
			: _inner->isAbovePosition(position)
			? _scroll->height()
			: 0;
		auto memento = HistoryView::ListMemento(position);
		_inner->restoreState(&memento);
	}
}

bool Widget::showAtPositionNow(Data::MessagePosition position) {
	if (const auto scrollTop = _inner->scrollTopForPosition(position)) {
		const auto currentScrollTop = _scroll->scrollTop();
		const auto wanted = snap(*scrollTop, 0, _scroll->scrollTopMax());
		const auto fullDelta = (wanted - currentScrollTop);
		const auto limit = _scroll->height();
		const auto scrollDelta = snap(fullDelta, -limit, limit);
		_inner->animatedScrollTo(
			wanted,
			position,
			scrollDelta,
			(std::abs(fullDelta) > limit
				? HistoryView::ListWidget::AnimatedScroll::Part
				: HistoryView::ListWidget::AnimatedScroll::Full));
		return true;
	}
	return false;
}

void Widget::updateScrollDownVisibility() {
	if (animating()) {
		return;
	}

	const auto scrollDownIsVisible = [&]() -> std::optional<bool> {
		const auto top = _scroll->scrollTop() + st::historyToDownShownAfter;
		if (top < _scroll->scrollTopMax()) {
			return true;
		}
		if (_inner->loadedAtBottomKnown()) {
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

void Widget::updateScrollDownPosition() {
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

void Widget::scrollDownAnimationFinish() {
	_scrollDownShown.stop();
	updateScrollDownPosition();
}

void Widget::checkForSingleChannelFeed() {
	const auto &channels = _feed->channels();
	if (channels.size() > 1) {
		return;
	} else if (!channels.empty()) {
		controller()->showPeerHistory(channels[0]);
	} else {
		controller()->clearSectionStack();
	}
}

Dialogs::RowDescriptor Widget::activeChat() const {
	return Dialogs::RowDescriptor(_feed, _currentMessageId);
}

void Widget::updateAdaptiveLayout() {
	_topBarShadow->moveToLeft(
		Adaptive::OneColumn() ? 0 : st::lineWidth,
		_topBar->height());
}

QPixmap Widget::grabForShowAnimation(const Window::SectionSlideParams &params) {
	if (params.withTopBarShadow) _topBarShadow->hide();
	auto result = Ui::GrabWidget(this);
	if (params.withTopBarShadow) _topBarShadow->show();
	return result;
}

void Widget::doSetInnerFocus() {
	_inner->setFocus();
}

bool Widget::showInternal(
		not_null<Window::SectionMemento*> memento,
		const Window::SectionShow &params) {
	if (const auto feedMemento = dynamic_cast<Memento*>(memento.get())) {
		if (feedMemento->feed() == _feed) {
			restoreState(feedMemento);
			return true;
		}
	}
	return false;
}

void Widget::setInternalState(
		const QRect &geometry,
		not_null<Memento*> memento) {
	setGeometry(geometry);
	Ui::SendPendingMoveResizeEvents(this);
	restoreState(memento);
}

void Widget::setupShortcuts() {
	Shortcuts::Requests(
	) | rpl::filter([=] {
		return isActiveWindow() && !Ui::isLayerShown() && inFocusChain();
	}) | rpl::start_with_next([=](not_null<Shortcuts::Request*> request) {
		using Command = Shortcuts::Command;
		request->check(Command::Search, 2) && request->handle([=] {
			App::main()->searchInChat(_feed);
			return true;
		});
	}, lifetime());
}

HistoryView::Context Widget::listContext() {
	return HistoryView::Context::Feed;
}

void Widget::listScrollTo(int top) {
	if (_scroll->scrollTop() != top) {
		_scroll->scrollToY(top);
	} else {
		updateInnerVisibleArea();
	}
}

void Widget::listCancelRequest() {
	controller()->showBackFromStack();
}

void Widget::listDeleteRequest() {
	confirmDeleteSelected();
}

rpl::producer<Data::MessagesSlice> Widget::listSource(
		Data::MessagePosition aroundId,
		int limitBefore,
		int limitAfter) {
	return Data::FeedMessagesViewer(
		Storage::FeedMessagesKey(_feed->id(), aroundId),
		limitBefore,
		limitAfter);
}

bool Widget::listAllowsMultiSelect() {
	return true;
}

bool Widget::listIsLessInOrder(
		not_null<HistoryItem*> first,
		not_null<HistoryItem*> second) {
	return first->position() < second->position();
}

void Widget::listSelectionChanged(HistoryView::SelectedItems &&items) {
	HistoryView::TopBarWidget::SelectedState state;
	state.count = items.size();
	for (const auto item : items) {
		if (item.canForward) {
			++state.canForwardCount;
		}
		if (item.canDelete) {
			++state.canDeleteCount;
		}
	}
	_topBar->showSelected(state);
}

void Widget::listVisibleItemsChanged(HistoryItemsList &&items) {
	const auto reversed = ranges::view::reverse(items);
	const auto good = ranges::find_if(reversed, [](auto item) {
		return IsServerMsgId(item->id);
	});
	if (good != end(reversed)) {
		Auth().api().readFeed(_feed, (*good)->position());
	}
}

std::optional<int> Widget::listUnreadBarView(
		const std::vector<not_null<Element*>> &elements) {
	const auto position = _feed->unreadPosition();
	if (!position || elements.empty() || !_feed->unreadCount()) {
		return std::nullopt;
	}
	const auto minimal = ranges::upper_bound(
		elements,
		position,
		std::less<>(),
		[](auto view) { return view->data()->position(); });
	if (minimal == end(elements)) {
		return std::nullopt;
	}
	const auto view = *minimal;
	const auto unreadMessagesHeight = elements.back()->y()
		+ elements.back()->height()
		- view->y();
	if (unreadMessagesHeight < _scroll->height()) {
		return std::nullopt;
	}
	return base::make_optional(int(minimal - begin(elements)));
}

void Widget::validateEmptyTextItem() {
	if (!_inner->isEmpty()) {
		_emptyTextView = nullptr;
		_emptyTextItem = nullptr;
		update();
		return;
	} else if (_emptyTextItem) {
		return;
	}
	const auto channels = _feed->channels();
	if (channels.empty()) {
		return;
	}
	const auto history = channels[0];
	_emptyTextItem.reset(new HistoryService(
		history,
		clientMsgId(),
		unixtime(),
		{ lang(lng_feed_no_messages) }));
	_emptyTextView = _emptyTextItem->createView(
		HistoryInner::ElementDelegate());
	updateControlsGeometry();
	update();
}

void Widget::listContentRefreshed() {
	validateEmptyTextItem();

	if (!_nextAnimatedScrollPosition) {
		return;
	}
	const auto position = *base::take(_nextAnimatedScrollPosition);
	if (const auto scrollTop = _inner->scrollTopForPosition(position)) {
		const auto wanted = snap(*scrollTop, 0, _scroll->scrollTopMax());
		_inner->animatedScrollTo(
			wanted,
			position,
			_nextAnimatedScrollDelta,
			HistoryView::ListWidget::AnimatedScroll::Part);
		if (const auto highlight = base::take(_highlightMessageId)) {
			_inner->highlightMessage(highlight);
		}
	}
}

ClickHandlerPtr Widget::listDateLink(not_null<Element*> view) {
	if (!_dateLink) {
		_dateLink = std::make_shared<Window::DateClickHandler>(_feed, view->dateTime().date());
	} else {
		_dateLink->setDate(view->dateTime().date());
	}
	return _dateLink;
}

std::unique_ptr<Window::SectionMemento> Widget::createMemento() {
	auto result = std::make_unique<Memento>(_feed);
	saveState(result.get());
	return std::move(result);
}

void Widget::saveState(not_null<Memento*> memento) {
	_inner->saveState(memento->list());
}

void Widget::restoreState(not_null<Memento*> memento) {
	const auto list = memento->list();
	if (!list->aroundPosition()) {
		if (const auto position = _feed->unreadPosition()) {
			list->setAroundPosition(position);
		}
	}
	_undefinedAroundPosition = !list->aroundPosition();
	_inner->restoreState(memento->list());
	if (const auto position = memento->position()) {
		_currentMessageId = _highlightMessageId = position.fullId;
		showAtPosition(position);
	}
}

void Widget::resizeEvent(QResizeEvent *e) {
	if (!width() || !height()) {
		return;
	}
	updateControlsGeometry();
}

void Widget::updateControlsGeometry() {
	const auto contentWidth = width();

	const auto newScrollTop = _scroll->isHidden()
		? std::nullopt
		: base::make_optional(_scroll->scrollTop() + topDelta());
	_topBar->resizeToWidth(contentWidth);
	_topBarShadow->resize(contentWidth, st::lineWidth);

	const auto bottom = height();
	const auto scrollHeight = bottom
		- _topBar->height();
//		- _showNext->height();
	const auto scrollSize = QSize(contentWidth, scrollHeight);
	if (_scroll->size() != scrollSize) {
		_skipScrollEvent = true;
		_scroll->resize(scrollSize);
		_inner->resizeToWidth(scrollSize.width(), _scroll->height());
		_skipScrollEvent = false;
	}
	if (!_scroll->isHidden()) {
		if (newScrollTop) {
			_scroll->scrollToY(*newScrollTop);
		}
		updateInnerVisibleArea();
	}

	updateScrollDownPosition();
	//const auto fullWidthButtonRect = myrtlrect(
	//	0,
	//	bottom - _showNext->height(),
	//	contentWidth,
	//	_showNext->height());
	//_showNext->setGeometry(fullWidthButtonRect);

	if (_emptyTextView) {
		_emptyTextView->resizeGetHeight(width());
	}
}

void Widget::paintEvent(QPaintEvent *e) {
	if (animating()) {
		SectionWidget::paintEvent(e);
		return;
	}
	if (Ui::skipPaintEvent(this, e)) {
		return;
	}
	//if (hasPendingResizedItems()) {
	//	updateListSize();
	//}

	SectionWidget::PaintBackground(this, e->rect());

	if (_emptyTextView) {
		Painter p(this);

		const auto clip = e->rect();
		const auto left = 0;
		const auto top = (height()
//			- _showNext->height()
			- _emptyTextView->height()) / 2;
		p.translate(left, top);
		_emptyTextView->draw(
			p,
			clip.translated(-left, -top),
			TextSelection(),
			crl::now());
	}
}

void Widget::onScroll() {
	if (_skipScrollEvent) {
		return;
	}
	updateInnerVisibleArea();
}

void Widget::updateInnerVisibleArea() {
	const auto scrollTop = _scroll->scrollTop();
	_inner->setVisibleTopBottom(scrollTop, scrollTop + _scroll->height());
	updateScrollDownVisibility();
}

void Widget::showAnimatedHook(
		const Window::SectionSlideParams &params) {
	_topBar->setAnimatingMode(true);
	if (params.withTopBarShadow) _topBarShadow->show();
}

void Widget::showFinishedHook() {
	_topBar->setAnimatingMode(false);
}

bool Widget::wheelEventFromFloatPlayer(QEvent *e) {
	return _scroll->viewportEvent(e);
}

QRect Widget::rectForFloatPlayer() const {
	return mapToGlobal(_scroll->geometry());
}

void Widget::forwardSelected() {
	auto items = _inner->getSelectedItems();
	if (items.empty()) {
		return;
	}
	const auto weak = make_weak(this);
	Window::ShowForwardMessagesBox(std::move(items), [=] {
		if (const auto strong = weak.data()) {
			strong->clearSelected();
		}
	});
}

void Widget::confirmDeleteSelected() {
	auto items = _inner->getSelectedItems();
	if (items.empty()) {
		return;
	}
	const auto weak = make_weak(this);
	const auto box = Ui::show(Box<DeleteMessagesBox>(std::move(items)));
	box->setDeleteConfirmedCallback([=] {
		if (const auto strong = weak.data()) {
			strong->clearSelected();
		}
	});
}

void Widget::clearSelected() {
	_inner->cancelSelection();
}

} // namespace HistoryFeed
