/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "history/view/history_view_list_widget.h"
#include "window/section_widget.h"
#include "window/section_memento.h"
#include "data/data_feed.h"
#include "history/history_item.h"
#include "history/admin_log/history_admin_log_item.h"

namespace Ui {
class ScrollArea;
class PlainShadow;
class FlatButton;
class HistoryDownButton;
} // namespace Ui

namespace HistoryView {
class ListWidget;
class TopBarWidget;
class Element;
} // namespace HistoryView

namespace Window {
class DateClickHandler;
} // namespace Window

namespace HistoryFeed {

class Memento;

class Widget final
	: public Window::SectionWidget
	, public HistoryView::ListDelegate {
public:
	using Element = HistoryView::Element;

	Widget(
		QWidget *parent,
		not_null<Window::Controller*> controller,
		not_null<Data::Feed*> feed);

	Dialogs::RowDescriptor activeChat() const override;

	bool hasTopBarShadow() const override {
		return true;
	}

	QPixmap grabForShowAnimation(
		const Window::SectionSlideParams &params) override;

	bool showInternal(
		not_null<Window::SectionMemento*> memento,
		const Window::SectionShow &params) override;
	std::unique_ptr<Window::SectionMemento> createMemento() override;

	void setInternalState(
		const QRect &geometry,
		not_null<Memento*> memento);

	// Float player interface.
	bool wheelEventFromFloatPlayer(QEvent *e) override;
	QRect rectForFloatPlayer() const override;

	// HistoryView::ListDelegate interface.
	HistoryView::Context listContext() override;
	void listScrollTo(int top) override;
	void listCancelRequest() override;
	void listDeleteRequest() override;
	rpl::producer<Data::MessagesSlice> listSource(
		Data::MessagePosition aroundId,
		int limitBefore,
		int limitAfter) override;
	bool listAllowsMultiSelect() override;
	bool listIsLessInOrder(
		not_null<HistoryItem*> first,
		not_null<HistoryItem*> second) override;
	void listSelectionChanged(
		HistoryView::SelectedItems &&items) override;
	void listVisibleItemsChanged(HistoryItemsList &&items) override;
	std::optional<int> listUnreadBarView(
		const std::vector<not_null<Element*>> &elements) override;
	void listContentRefreshed() override;
	ClickHandlerPtr listDateLink(not_null<Element*> view) override;

protected:
	void resizeEvent(QResizeEvent *e) override;
	void paintEvent(QPaintEvent *e) override;

	void showAnimatedHook(
		const Window::SectionSlideParams &params) override;
	void showFinishedHook() override;
	void doSetInnerFocus() override;

private:
	void checkForSingleChannelFeed();
	void onScroll();
	void updateInnerVisibleArea();
	void updateControlsGeometry();
	void updateAdaptiveLayout();
	void saveState(not_null<Memento*> memento);
	void restoreState(not_null<Memento*> memento);
	void showAtPosition(Data::MessagePosition position);
	bool showAtPositionNow(Data::MessagePosition position);
	void validateEmptyTextItem();

	void setupScrollDownButton();
	void scrollDownClicked();
	void scrollDownAnimationFinish();
	void updateScrollDownVisibility();
	void updateScrollDownPosition();

	void forwardSelected();
	void confirmDeleteSelected();
	void clearSelected();

	void setupShortcuts();

	not_null<Data::Feed*> _feed;
	object_ptr<Ui::ScrollArea> _scroll;
	QPointer<HistoryView::ListWidget> _inner;
	object_ptr<HistoryView::TopBarWidget> _topBar;
	object_ptr<Ui::PlainShadow> _topBarShadow;
	object_ptr<Ui::FlatButton> _showNext;
	bool _skipScrollEvent = false;
	bool _undefinedAroundPosition = false;
	std::unique_ptr<HistoryItem, HistoryItem::Destroyer> _emptyTextItem;
	std::unique_ptr<HistoryView::Element> _emptyTextView;

	FullMsgId _currentMessageId;
	FullMsgId _highlightMessageId;
	std::optional<Data::MessagePosition> _nextAnimatedScrollPosition;
	int _nextAnimatedScrollDelta = 0;

	Animation _scrollDownShown;
	bool _scrollDownIsShown = false;
	object_ptr<Ui::HistoryDownButton> _scrollDown;
	std::shared_ptr<Window::DateClickHandler> _dateLink;

};

class Memento : public Window::SectionMemento {
public:
	explicit Memento(
		not_null<Data::Feed*> feed,
		Data::MessagePosition position = Data::UnreadMessagePosition);
	~Memento();

	object_ptr<Window::SectionWidget> createWidget(
		QWidget *parent,
		not_null<Window::Controller*> controller,
		Window::Column column,
		const QRect &geometry) override;

	not_null<Data::Feed*> feed() const {
		return _feed;
	}
	Data::MessagePosition position() const {
		return _position;
	}
	not_null<HistoryView::ListMemento*> list() const {
		return _list.get();
	}

private:
	not_null<Data::Feed*> _feed;
	Data::MessagePosition _position;
	std::unique_ptr<HistoryView::ListMemento> _list;

};

} // namespace HistoryFeed
