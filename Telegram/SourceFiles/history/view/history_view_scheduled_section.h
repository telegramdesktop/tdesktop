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

class History;
enum class CompressConfirm;
enum class SendMediaType;
struct SendingAlbum;
enum class SendMenuType;

namespace Api {
struct SendOptions;
} // namespace Api

namespace Notify {
struct PeerUpdate;
} // namespace Notify

namespace Storage {
struct PreparedList;
} // namespace Storage

namespace Ui {
class ScrollArea;
class PlainShadow;
class FlatButton;
class HistoryDownButton;
} // namespace Ui

namespace Profile {
class BackButton;
} // namespace Profile

namespace InlineBots {
class Result;
} // namespace InlineBots

namespace HistoryView {

class Element;
class TopBarWidget;
class ScheduledMemento;
class ComposeControls;

class ScheduledWidget final
	: public Window::SectionWidget
	, private ListDelegate {
public:
	ScheduledWidget(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		not_null<History*> history);
	~ScheduledWidget();

	not_null<History*> history() const;
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
		not_null<ScheduledMemento*> memento);

	// Tabbed selector management.
	void pushTabbedSelectorToThirdSection(
		const Window::SectionShow &params) override;
	bool returnTabbedSelector() override;

	// Float player interface.
	bool wheelEventFromFloatPlayer(QEvent *e) override;
	QRect rectForFloatPlayer() const override;

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
	void onScroll();
	void updateInnerVisibleArea();
	void updateControlsGeometry();
	void updateAdaptiveLayout();
	void saveState(not_null<ScheduledMemento*> memento);
	void restoreState(not_null<ScheduledMemento*> memento);
	void showAtPosition(Data::MessagePosition position);
	bool showAtPositionNow(Data::MessagePosition position);

	void setupComposeControls();

	void setupScrollDownButton();
	void scrollDownClicked();
	void scrollDownAnimationFinish();
	void updateScrollDownVisibility();
	void updateScrollDownPosition();

	void confirmSendNowSelected();
	void confirmDeleteSelected();
	void clearSelected();

	void send();
	void send(Api::SendOptions options);
	void highlightSingleNewMessage(const Data::MessagesSlice &slice);
	void chooseAttach();
	[[nodiscard]] SendMenuType sendMenuType() const;

	void uploadFile(const QByteArray &fileContent, SendMediaType type);
	bool confirmSendingFiles(
		QImage &&image,
		QByteArray &&content,
		CompressConfirm compressed,
		const QString &insertTextOnCancel = QString());
	bool confirmSendingFiles(
		Storage::PreparedList &&list,
		CompressConfirm compressed,
		const QString &insertTextOnCancel = QString());
	bool showSendingFilesError(const Storage::PreparedList &list) const;
	void uploadFilesAfterConfirmation(
		Storage::PreparedList &&list,
		SendMediaType type,
		TextWithTags &&caption,
		MsgId replyTo,
		Api::SendOptions options,
		std::shared_ptr<SendingAlbum> album);

	void sendExistingDocument(not_null<DocumentData*> document);
	bool sendExistingDocument(
		not_null<DocumentData*> document,
		Api::SendOptions options);
	void sendExistingPhoto(not_null<PhotoData*> photo);
	bool sendExistingPhoto(
		not_null<PhotoData*> photo,
		Api::SendOptions options);
	void sendInlineResult(
		not_null<InlineBots::Result*> result,
		not_null<UserData*> bot);
	void sendInlineResult(
		not_null<InlineBots::Result*> result,
		not_null<UserData*> bot,
		Api::SendOptions options);

	const not_null<History*> _history;
	object_ptr<Ui::ScrollArea> _scroll;
	QPointer<ListWidget> _inner;
	object_ptr<TopBarWidget> _topBar;
	object_ptr<Ui::PlainShadow> _topBarShadow;
	std::unique_ptr<ComposeControls> _composeControls;
	bool _skipScrollEvent = false;

	FullMsgId _highlightMessageId;
	std::optional<Data::MessagePosition> _nextAnimatedScrollPosition;
	int _nextAnimatedScrollDelta = 0;

	Ui::Animations::Simple _scrollDownShown;
	bool _scrollDownIsShown = false;
	object_ptr<Ui::HistoryDownButton> _scrollDown;

	Data::MessagesSlice _lastSlice;
	bool _choosingAttach = false;

};

class ScheduledMemento : public Window::SectionMemento {
public:
	ScheduledMemento(not_null<History*> history)
	: _history(history) {
	}

	object_ptr<Window::SectionWidget> createWidget(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		Window::Column column,
		const QRect &geometry) override;

	not_null<History*> getHistory() const {
		return _history;
	}

	not_null<ListMemento*> list() {
		return &_list;
	}

private:
	const not_null<History*> _history;
	ListMemento _list;

};

} // namespace HistoryScheduled
