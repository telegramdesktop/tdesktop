/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "window/section_widget.h"
#include "window/section_memento.h"
#include "history/view/history_view_list_widget.h"
#include "data/data_messages.h"
#include "base/weak_ptr.h"
#include "base/timer.h"

class History;

namespace Ui {
class ScrollArea;
class PlainShadow;
class FlatButton;
class HistoryDownButton;
} // namespace Ui

namespace Profile {
class BackButton;
} // namespace Profile

namespace HistoryView {

class Element;
class TopBarWidget;
class PinnedMemento;

class PinnedWidget final
	: public Window::SectionWidget
	, private ListDelegate {
public:
	PinnedWidget(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		not_null<History*> history);
	~PinnedWidget();

	[[nodiscard]] not_null<History*> history() const;
	Dialogs::RowDescriptor activeChat() const override;

	bool hasTopBarShadow() const override {
		return true;
	}

	QPixmap grabForShowAnimation(
		const Window::SectionSlideParams &params) override;

	bool showInternal(
		not_null<Window::SectionMemento*> memento,
		const Window::SectionShow &params) override;
	std::shared_ptr<Window::SectionMemento> createMemento() override;
	bool showMessage(
		PeerId peerId,
		const Window::SectionShow &params,
		MsgId messageId) override;

	void setInternalState(
		const QRect &geometry,
		not_null<PinnedMemento*> memento);

	// Float player interface.
	bool floatPlayerHandleWheelEvent(QEvent *e) override;
	QRect floatPlayerAvailableRect() override;

	// ListDelegate interface.
	Context listContext() override;
	void listScrollTo(int top) override;
	void listCancelRequest() override;
	void listDeleteRequest() override;
	rpl::producer<Data::MessagesSlice> listSource(
		Data::MessagePosition aroundId,
		int limitBefore,
		int limitAfter) override;
	bool listAllowsMultiSelect() override;
	bool listIsItemGoodForSelection(not_null<HistoryItem*> item) override;
	bool listIsLessInOrder(
		not_null<HistoryItem*> first,
		not_null<HistoryItem*> second) override;
	void listSelectionChanged(SelectedItems &&items) override;
	void listVisibleItemsChanged(HistoryItemsList &&items) override;
	MessagesBarData listMessagesBar(
		const std::vector<not_null<Element*>> &elements) override;
	void listContentRefreshed() override;
	ClickHandlerPtr listDateLink(not_null<Element*> view) override;
	bool listElementHideReply(not_null<const Element*> view) override;
	bool listElementShownUnread(not_null<const Element*> view) override;
	bool listIsGoodForAroundPosition(not_null<const Element*> view) override;
	void listSendBotCommand(
		const QString &command,
		const FullMsgId &context) override;
	void listHandleViaClick(not_null<UserData*> bot) override;

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
	void saveState(not_null<PinnedMemento*> memento);
	void restoreState(not_null<PinnedMemento*> memento);
	void showAtStart();
	void showAtEnd();
	void showAtPosition(
		Data::MessagePosition position,
		HistoryItem *originItem = nullptr);
	bool showAtPositionNow(
		Data::MessagePosition position,
		HistoryItem *originItem,
		anim::type animated = anim::type::normal);

	void setupClearButton();
	void setupScrollDownButton();
	void scrollDownClicked();
	void scrollDownAnimationFinish();
	void updateScrollDownVisibility();
	void updateScrollDownPosition();

	void confirmDeleteSelected();
	void confirmForwardSelected();
	void clearSelected();
	void recountChatWidth();

	void setMessagesCount(int count);
	void refreshClearButtonText();

	const not_null<History*> _history;
	PeerData *_migratedPeer = nullptr;
	QPointer<ListWidget> _inner;
	object_ptr<TopBarWidget> _topBar;
	object_ptr<Ui::PlainShadow> _topBarShadow;

	bool _skipScrollEvent = false;
	std::unique_ptr<Ui::ScrollArea> _scroll;
	std::unique_ptr<Ui::FlatButton> _clearButton;

	Ui::Animations::Simple _scrollDownShown;
	bool _scrollDownIsShown = false;
	object_ptr<Ui::HistoryDownButton> _scrollDown;

	Data::MessagesSlice _lastSlice;
	int _messagesCount = -1;

};

class PinnedMemento : public Window::SectionMemento {
public:
	using UniversalMsgId = int32;

	explicit PinnedMemento(
		not_null<History*> history,
		UniversalMsgId highlightId = 0);

	object_ptr<Window::SectionWidget> createWidget(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		Window::Column column,
		const QRect &geometry) override;

	[[nodiscard]] not_null<History*> getHistory() const {
		return _history;
	}

	[[nodiscard]] not_null<ListMemento*> list() {
		return &_list;
	}
	[[nodiscard]] UniversalMsgId getHighlightId() const {
		return _highlightId;
	}

private:
	const not_null<History*> _history;
	const UniversalMsgId _highlightId = 0;
	ListMemento _list;

};

} // namespace HistoryView
