/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_scheduled_section.h"

#include "history/view/history_view_compose_controls.h"
#include "history/view/history_view_top_bar_widget.h"
#include "history/view/history_view_list_widget.h"
#include "history/history.h"
#include "history/history_item.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/shadow.h"
#include "ui/special_buttons.h"
#include "boxes/confirm_box.h"
#include "window/window_session_controller.h"
#include "window/window_peer_menu.h"
#include "core/event_filter.h"
#include "main/main_session.h"
#include "data/data_session.h"
#include "data/data_scheduled_messages.h"
#include "lang/lang_keys.h"
#include "styles/style_history.h"
#include "styles/style_window.h"
#include "styles/style_info.h"

namespace HistoryView {

object_ptr<Window::SectionWidget> ScheduledMemento::createWidget(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		Window::Column column,
		const QRect &geometry) {
	if (column == Window::Column::Third) {
		return nullptr;
	}
	auto result = object_ptr<ScheduledWidget>(parent, controller, _history);
	result->setInternalState(geometry, this);
	return std::move(result);
}

ScheduledWidget::ScheduledWidget(
	QWidget *parent,
	not_null<Window::SessionController*> controller,
	not_null<History*> history)
: Window::SectionWidget(parent, controller)
, _history(history)
, _scroll(this, st::historyScroll, false)
, _topBar(this, controller)
, _topBarShadow(this)
, _composeControls(std::make_unique<ComposeControls>(
	this,
	controller,
	ComposeControls::Mode::Scheduled))
, _scrollDown(_scroll, st::historyToDown) {
	_topBar->setActiveChat(_history, TopBarWidget::Section::Scheduled);

	_topBar->move(0, 0);
	_topBar->resizeToWidth(width());
	_topBar->show();

	_topBar->sendNowSelectionRequest(
	) | rpl::start_with_next([=] {
		confirmSendNowSelected();
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
	subscribe(Adaptive::Changed(), [=] { updateAdaptiveLayout(); });

	_inner = _scroll->setOwnedWidget(object_ptr<ListWidget>(
		this,
		controller,
		static_cast<ListDelegate*>(this)));
	_scroll->move(0, _topBar->height());
	_scroll->show();
	connect(_scroll, &Ui::ScrollArea::scrolled, [=] { onScroll(); });

	setupScrollDownButton();
	setupComposeControls();
}

ScheduledWidget::~ScheduledWidget() = default;

void ScheduledWidget::setupComposeControls() {
	_composeControls->height(
	) | rpl::start_with_next([=] {
		updateControlsGeometry();
	}, lifetime());

	_composeControls->cancelRequests(
	) | rpl::start_with_next([=] {
		controller()->showBackFromStack();
	}, lifetime());
}

void ScheduledWidget::setupScrollDownButton() {
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
}

void ScheduledWidget::scrollDownClicked() {
	showAtPosition(Data::MaxMessagePosition);
}

void ScheduledWidget::showAtPosition(Data::MessagePosition position) {
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

bool ScheduledWidget::showAtPositionNow(Data::MessagePosition position) {
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

void ScheduledWidget::updateScrollDownVisibility() {
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

void ScheduledWidget::updateScrollDownPosition() {
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

void ScheduledWidget::scrollDownAnimationFinish() {
	_scrollDownShown.stop();
	updateScrollDownPosition();
}

void ScheduledWidget::updateAdaptiveLayout() {
	_topBarShadow->moveToLeft(
		Adaptive::OneColumn() ? 0 : st::lineWidth,
		_topBar->height());
}

not_null<History*> ScheduledWidget::history() const {
	return _history;
}

Dialogs::RowDescriptor ScheduledWidget::activeChat() const {
	return {
		_history,
		FullMsgId(_history->channelId(), ShowAtUnreadMsgId)
	};
}

QPixmap ScheduledWidget::grabForShowAnimation(const Window::SectionSlideParams &params) {
	_topBar->updateControlsVisibility();
	if (params.withTopBarShadow) _topBarShadow->hide();
	_composeControls->showForGrab();
	auto result = Ui::GrabWidget(this);
	if (params.withTopBarShadow) _topBarShadow->show();
	return result;
}

void ScheduledWidget::doSetInnerFocus() {
	_composeControls->focus();
}

bool ScheduledWidget::showInternal(
		not_null<Window::SectionMemento*> memento,
		const Window::SectionShow &params) {
	if (auto logMemento = dynamic_cast<ScheduledMemento*>(memento.get())) {
		if (logMemento->getHistory() == history()) {
			restoreState(logMemento);
			return true;
		}
	}
	return false;
}

void ScheduledWidget::setInternalState(
		const QRect &geometry,
		not_null<ScheduledMemento*> memento) {
	setGeometry(geometry);
	Ui::SendPendingMoveResizeEvents(this);
	restoreState(memento);
}

void ScheduledWidget::pushTabbedSelectorToThirdSection(
		const Window::SectionShow &params) {
	_composeControls->pushTabbedSelectorToThirdSection(params);
}

bool ScheduledWidget::returnTabbedSelector() {
	return _composeControls->returnTabbedSelector();
}

std::unique_ptr<Window::SectionMemento> ScheduledWidget::createMemento() {
	auto result = std::make_unique<ScheduledMemento>(history());
	saveState(result.get());
	return std::move(result);
}

void ScheduledWidget::saveState(not_null<ScheduledMemento*> memento) {
	_inner->saveState(memento->list());
}

void ScheduledWidget::restoreState(not_null<ScheduledMemento*> memento) {
	_inner->restoreState(memento->list());
}

void ScheduledWidget::resizeEvent(QResizeEvent *e) {
	if (!width() || !height()) {
		return;
	}
	_composeControls->resizeToWidth(width());
	updateControlsGeometry();
}

void ScheduledWidget::updateControlsGeometry() {
	const auto contentWidth = width();

	const auto newScrollTop = _scroll->isHidden()
		? std::nullopt
		: base::make_optional(_scroll->scrollTop() + topDelta());
	_topBar->resizeToWidth(contentWidth);
	_topBarShadow->resize(contentWidth, st::lineWidth);

	const auto bottom = height();
	const auto controlsHeight = _composeControls->heightCurrent();
	const auto scrollHeight = bottom - _topBar->height() - controlsHeight;
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
	_composeControls->move(0, bottom - controlsHeight);

	updateScrollDownPosition();
}

void ScheduledWidget::paintEvent(QPaintEvent *e) {
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

	//auto ms = crl::now();
	//_historyDownShown.step(ms);

	SectionWidget::PaintBackground(this, e->rect());
}

void ScheduledWidget::onScroll() {
	if (_skipScrollEvent) {
		return;
	}
	updateInnerVisibleArea();
}

void ScheduledWidget::updateInnerVisibleArea() {
	const auto scrollTop = _scroll->scrollTop();
	_inner->setVisibleTopBottom(scrollTop, scrollTop + _scroll->height());
	updateScrollDownVisibility();
}

void ScheduledWidget::showAnimatedHook(
		const Window::SectionSlideParams &params) {
	_topBar->setAnimatingMode(true);
	if (params.withTopBarShadow) {
		_topBarShadow->show();
	}
	_composeControls->showStarted();
}

void ScheduledWidget::showFinishedHook() {
	_topBar->setAnimatingMode(false);
	_composeControls->showFinished();
}

bool ScheduledWidget::wheelEventFromFloatPlayer(QEvent *e) {
	return _scroll->viewportEvent(e);
}

QRect ScheduledWidget::rectForFloatPlayer() const {
	return mapToGlobal(_scroll->geometry());
}

Context ScheduledWidget::listContext() {
	return Context::History;
}

void ScheduledWidget::listScrollTo(int top) {
	if (_scroll->scrollTop() != top) {
		_scroll->scrollToY(top);
	} else {
		updateInnerVisibleArea();
	}
}

void ScheduledWidget::listCancelRequest() {
	controller()->showBackFromStack();
}

void ScheduledWidget::listDeleteRequest() {
	confirmDeleteSelected();
}

rpl::producer<Data::MessagesSlice> ScheduledWidget::listSource(
		Data::MessagePosition aroundId,
		int limitBefore,
		int limitAfter) {
	const auto data = &controller()->session().data();
	return rpl::single(
		rpl::empty_value()
	) | rpl::then(
		data->scheduledMessages().updates(_history)
	) | rpl::map([=] {
		return data->scheduledMessages().list(_history);
	});
}

bool ScheduledWidget::listAllowsMultiSelect() {
	return true;
}

bool ScheduledWidget::listIsItemGoodForSelection(
		not_null<HistoryItem*> item) {
	return !item->isSending() && !item->hasFailed();
}

bool ScheduledWidget::listIsLessInOrder(
		not_null<HistoryItem*> first,
		not_null<HistoryItem*> second) {
	return first->position() < second->position();
}

void ScheduledWidget::listSelectionChanged(SelectedItems &&items) {
	HistoryView::TopBarWidget::SelectedState state;
	state.count = items.size();
	for (const auto item : items) {
		if (item.canDelete) {
			++state.canDeleteCount;
		}
		if (item.canSendNow) {
			++state.canSendNowCount;
		}
	}
	_topBar->showSelected(state);
}

void ScheduledWidget::listVisibleItemsChanged(HistoryItemsList &&items) {
}

std::optional<int> ScheduledWidget::listUnreadBarView(
		const std::vector<not_null<Element*>> &elements) {
	return std::nullopt;
}

void ScheduledWidget::listContentRefreshed() {
}

ClickHandlerPtr ScheduledWidget::listDateLink(not_null<Element*> view) {
	return nullptr;
}

void ScheduledWidget::confirmSendNowSelected() {
	auto items = _inner->getSelectedItems();
	if (items.empty()) {
		return;
	}
	const auto navigation = controller();
	Window::ShowSendNowMessagesBox(
		navigation,
		_history,
		std::move(items),
		[=] { navigation->showBackFromStack(); });
}

void ScheduledWidget::confirmDeleteSelected() {
	auto items = _inner->getSelectedItems();
	if (items.empty()) {
		return;
	}
	const auto weak = make_weak(this);
	const auto box = Ui::show(Box<DeleteMessagesBox>(
		&_history->session(),
		std::move(items)));
	box->setDeleteConfirmedCallback([=] {
		if (const auto strong = weak.data()) {
			strong->clearSelected();
		}
	});
}

void ScheduledWidget::clearSelected() {
	_inner->cancelSelection();
}

} // namespace HistoryView
