/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/feed/history_feed_section.h"

#include "history/view/history_view_top_bar_widget.h"
#include "history/view/history_view_list_widget.h"
#include "history/view/history_view_message.h"
#include "lang/lang_keys.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/shadow.h"
#include "ui/widgets/scroll_area.h"
#include "boxes/confirm_box.h"
#include "window/window_controller.h"
#include "data/data_feed_messages.h"
#include "storage/storage_feed_messages.h"
#include "styles/style_widgets.h"
#include "styles/style_history.h"

namespace HistoryFeed {

Memento::Memento(
	not_null<Data::Feed*> feed,
	Data::MessagePosition aroundPosition)
: _feed(feed)
, _list(std::make_unique<HistoryView::ListMemento>(aroundPosition)) {
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
, _showNext(
		this,
		lang(lng_feed_show_next).toUpper(),
		st::historyComposeButton) {
	_topBar->move(0, 0);
	_topBar->resizeToWidth(width());
	_topBar->show();

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

	_showNext->setClickedCallback([this] {
		// #TODO feeds show next
		Ui::show(Box<InformBox>(lang(lng_admin_log_about_text)));
	});

	_feed->unreadPositionChanges(
	) | rpl::filter([=](const Data::MessagePosition &position) {
		return _undefinedAroundPosition && position;
	}) | rpl::start_with_next([=](const Data::MessagePosition &position) {
		auto memento = HistoryView::ListMemento(position);
		_inner->restoreState(&memento);
	}, lifetime());
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

bool Widget::cmd_search() {
	if (!inFocusChain()) {
		return false;
	}
	// #TODO feeds search
	return true;
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

void Widget::listCloseRequest() {
	controller()->showBackFromStack();
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
}

void Widget::resizeEvent(QResizeEvent *e) {
	if (!width() || !height()) {
		return;
	}
	updateControlsGeometry();
}

void Widget::updateControlsGeometry() {
	const auto contentWidth = width();

	const auto newScrollTop = _scroll->scrollTop() + topDelta();
	_topBar->resizeToWidth(contentWidth);
	_topBarShadow->resize(contentWidth, st::lineWidth);

	const auto bottom = height();
	const auto scrollHeight = bottom
		- _topBar->height()
		- _showNext->height();
	const auto scrollSize = QSize(contentWidth, scrollHeight);
	if (_scroll->size() != scrollSize) {
		_scroll->resize(scrollSize);
		_inner->resizeToWidth(scrollSize.width(), _scroll->height());
		//_inner->restoreScrollPosition();
	}

	if (!_scroll->isHidden()) {
		if (topDelta()) {
			_scroll->scrollToY(newScrollTop);
		}
		updateInnerVisibleArea();
	}
	const auto fullWidthButtonRect = myrtlrect(
		0,
		bottom - _showNext->height(),
		contentWidth,
		_showNext->height());
	_showNext->setGeometry(fullWidthButtonRect);
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

	//auto ms = getms();
	//_historyDownShown.step(ms);

	SectionWidget::PaintBackground(this, e);
}

void Widget::onScroll() {
	updateInnerVisibleArea();
}

void Widget::updateInnerVisibleArea() {
	const auto scrollTop = _scroll->scrollTop();
	_inner->setVisibleTopBottom(scrollTop, scrollTop + _scroll->height());
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

} // namespace HistoryFeed
