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
#include "history/view/history_view_corner_buttons.h"
#include "data/data_messages.h"
#include "base/weak_ptr.h"
#include "base/timer.h"

class History;

namespace Ui {
class ScrollArea;
class PlainShadow;
class FlatButton;
} // namespace Ui

namespace Profile {
class BackButton;
} // namespace Profile

namespace HistoryView {

class Element;
class TopBarWidget;
class SublistMemento;
class TranslateBar;
class ComposeSearch;

class SublistWidget final
	: public Window::SectionWidget
	, private WindowListDelegate
	, private CornerButtonsDelegate {
public:
	SublistWidget(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		not_null<Data::SavedSublist*> sublist);
	~SublistWidget();

	[[nodiscard]] not_null<Data::SavedSublist*> sublist() const;
	Dialogs::RowDescriptor activeChat() const override;

	bool hasTopBarShadow() const override {
		return true;
	}

	QPixmap grabForShowAnimation(
		const Window::SectionSlideParams &params) override;

	bool showInternal(
		not_null<Window::SectionMemento*> memento,
		const Window::SectionShow &params) override;
	bool sameTypeAs(not_null<Window::SectionMemento*> memento) override;

	std::shared_ptr<Window::SectionMemento> createMemento() override;
	bool showMessage(
		PeerId peerId,
		const Window::SectionShow &params,
		MsgId messageId) override;

	void setInternalState(
		const QRect &geometry,
		not_null<SublistMemento*> memento);

	Window::SectionActionResult sendBotCommand(
			Bot::SendCommandRequest request) override {
		return Window::SectionActionResult::Fallback;
	}

	bool searchInChatEmbedded(Dialogs::Key chat, QString query) override;

	// Float player interface.
	bool floatPlayerHandleWheelEvent(QEvent *e) override;
	QRect floatPlayerAvailableRect() override;

	// ListDelegate interface.
	Context listContext() override;
	bool listScrollTo(int top, bool syntetic = true) override;
	void listCancelRequest() override;
	void listDeleteRequest() override;
	void listTryProcessKeyInput(not_null<QKeyEvent*> e) override;
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
	void listMarkReadTill(not_null<HistoryItem*> item) override;
	void listMarkContentsRead(
		const base::flat_set<not_null<HistoryItem*>> &items) override;
	MessagesBarData listMessagesBar(
		const std::vector<not_null<Element*>> &elements) override;
	void listContentRefreshed() override;
	void listUpdateDateLink(
		ClickHandlerPtr &link,
		not_null<Element*> view) override;
	bool listElementHideReply(not_null<const Element*> view) override;
	bool listElementShownUnread(not_null<const Element*> view) override;
	bool listIsGoodForAroundPosition(not_null<const Element*> view) override;
	void listSendBotCommand(
		const QString &command,
		const FullMsgId &context) override;
	void listSearch(
		const QString &query,
		const FullMsgId &context) override;
	void listHandleViaClick(not_null<UserData*> bot) override;
	not_null<Ui::ChatTheme*> listChatTheme() override;
	CopyRestrictionType listCopyRestrictionType(HistoryItem *item) override;
	CopyRestrictionType listCopyMediaRestrictionType(
		not_null<HistoryItem*> item) override;
	CopyRestrictionType listSelectRestrictionType() override;
	auto listAllowedReactionsValue()
		-> rpl::producer<Data::AllowedReactions> override;
	void listShowPremiumToast(not_null<DocumentData*> document) override;
	void listOpenPhoto(
		not_null<PhotoData*> photo,
		FullMsgId context) override;
	void listOpenDocument(
		not_null<DocumentData*> document,
		FullMsgId context,
		bool showInMediaView) override;
	void listPaintEmpty(
		Painter &p,
		const Ui::ChatPaintContext &context) override;
	QString listElementAuthorRank(not_null<const Element*> view) override;
	History *listTranslateHistory() override;
	void listAddTranslatedItems(
		not_null<TranslateTracker*> tracker) override;

	// CornerButtonsDelegate delegate.
	void cornerButtonsShowAtPosition(
		Data::MessagePosition position) override;
	Data::Thread *cornerButtonsThread() override;
	FullMsgId cornerButtonsCurrentId() override;
	bool cornerButtonsIgnoreVisibility() override;
	std::optional<bool> cornerButtonsDownShown() override;
	bool cornerButtonsUnreadMayBeShown() override;
	bool cornerButtonsHas(CornerButtonType type) override;

private:
	void resizeEvent(QResizeEvent *e) override;
	void paintEvent(QPaintEvent *e) override;

	void showAnimatedHook(
		const Window::SectionSlideParams &params) override;
	void showFinishedHook() override;
	void doSetInnerFocus() override;
	void checkActivation() override;

	void onScroll();
	void updateInnerVisibleArea();
	void updateControlsGeometry();
	void updateAdaptiveLayout();
	void saveState(not_null<SublistMemento*> memento);
	void restoreState(not_null<SublistMemento*> memento);
	void showAtPosition(
		Data::MessagePosition position,
		FullMsgId originId = {});
	void showAtPosition(
		Data::MessagePosition position,
		FullMsgId originItemId,
		const Window::SectionShow &params);

	void setupOpenChatButton();
	void setupAboutHiddenAuthor();
	void setupTranslateBar();
	void setupShortcuts();

	void confirmDeleteSelected();
	void confirmForwardSelected();
	void clearSelected();
	void recountChatWidth();
	void searchInSublist();

	const not_null<Data::SavedSublist*> _sublist;
	const not_null<History*> _history;
	std::shared_ptr<Ui::ChatTheme> _theme;
	QPointer<ListWidget> _inner;
	object_ptr<TopBarWidget> _topBar;
	object_ptr<Ui::PlainShadow> _topBarShadow;

	std::unique_ptr<TranslateBar> _translateBar;
	int _translateBarHeight = 0;

	bool _skipScrollEvent = false;
	std::unique_ptr<Ui::ScrollArea> _scroll;
	std::unique_ptr<Ui::FlatButton> _openChatButton;
	std::unique_ptr<Ui::RpWidget> _aboutHiddenAuthor;
	std::unique_ptr<ComposeSearch> _composeSearch;

	FullMsgId _lastShownAt;
	CornerButtons _cornerButtons;

};

class SublistMemento : public Window::SectionMemento {
public:
	explicit SublistMemento(not_null<Data::SavedSublist*> sublist);

	object_ptr<Window::SectionWidget> createWidget(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		Window::Column column,
		const QRect &geometry) override;

	[[nodiscard]] not_null<Data::SavedSublist*> getSublist() const {
		return _sublist;
	}

	[[nodiscard]] not_null<ListMemento*> list() {
		return &_list;
	}

private:
	const not_null<Data::SavedSublist*> _sublist;
	ListMemento _list;

};

} // namespace HistoryView
