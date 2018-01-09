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

namespace Ui {
class ScrollArea;
class PlainShadow;
class FlatButton;
} // namespace Ui

namespace HistoryView {
class ListWidget;
class TopBarWidget;
} // namespace HistoryView

namespace HistoryFeed {

class Memento;

class Widget final
	: public Window::SectionWidget
	, public HistoryView::ListDelegate {
public:
	Widget(
		QWidget *parent,
		not_null<Window::Controller*> controller,
		not_null<Data::Feed*> feed);

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

	bool cmd_search() override;

	// HistoryView::ListDelegate interface.
	void listScrollTo(int top) override;
	void listCloseRequest() override;
	rpl::producer<Data::MessagesSlice> listSource(
		Data::MessagePosition aroundId,
		int limitBefore,
		int limitAfter) override;

protected:
	void resizeEvent(QResizeEvent *e) override;
	void paintEvent(QPaintEvent *e) override;

	void showAnimatedHook(
		const Window::SectionSlideParams &params) override;
	void showFinishedHook() override;
	void doSetInnerFocus() override;

private:
	void onScroll();
	void updateInnerVisibleArea();
	void updateControlsGeometry();
	void updateAdaptiveLayout();
	void saveState(not_null<Memento*> memento);
	void restoreState(not_null<Memento*> memento);

	not_null<Data::Feed*> _feed;
	object_ptr<Ui::ScrollArea> _scroll;
	QPointer<HistoryView::ListWidget> _inner;
	object_ptr<HistoryView::TopBarWidget> _topBar;
	object_ptr<Ui::PlainShadow> _topBarShadow;
	object_ptr<Ui::FlatButton> _showNext;
	bool _undefinedAroundPosition = false;

};

class Memento : public Window::SectionMemento {
public:
	explicit Memento(
		not_null<Data::Feed*> feed,
		Data::MessagePosition aroundPosition = Data::UnreadMessagePosition);
	~Memento();

	object_ptr<Window::SectionWidget> createWidget(
		QWidget *parent,
		not_null<Window::Controller*> controller,
		Window::Column column,
		const QRect &geometry) override;

	not_null<Data::Feed*> feed() const {
		return _feed;
	}
	not_null<HistoryView::ListMemento*> list() const {
		return _list.get();
	}

private:
	not_null<Data::Feed*> _feed;
	std::unique_ptr<HistoryView::ListMemento> _list;

};

} // namespace HistoryFeed
