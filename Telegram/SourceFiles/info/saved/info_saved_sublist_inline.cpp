/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/saved/info_saved_sublist_inline.h"

#include "data/data_chat_participant_status.h"
#include "data/data_peer.h"
#include "data/data_peer_values.h"
#include "data/data_saved_sublist.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/view/history_view_element.h"
#include "history/view/history_view_list_widget.h"
#include "history/view/reactions/history_view_reactions_button.h"
#include "info/info_controller.h"
#include "info/info_memento.h"
#include "lang/lang_keys.h"
#include "lang/lang_numbers_animation.h"
#include "main/main_session.h"
#include "ui/chat/chat_style.h"
#include "ui/chat/chat_theme.h"
#include "ui/effects/numbers_animation.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "ui/ui_utility.h"
#include "window/main_window.h"
#include "window/section_widget.h"
#include "window/themes/window_theme.h"
#include "window/window_peer_menu.h"
#include "window/window_session_controller.h"

namespace Info::Saved {
namespace {

class InlineSublistInner final : private HistoryView::ListDelegate {
public:
	InlineSublistInner(
		not_null<QWidget*> parent,
		not_null<AbstractController*> controller,
		not_null<Data::SavedSublist*> sublist,
		Fn<void(int top)> inlineScrollTo);
	~InlineSublistInner();

	[[nodiscard]] HistoryView::ListWidget *list() const {
		return _list;
	}

	void updateGeometry(QRect rect);
	void setInlineVisibleRegion(int top, int bottom);

	[[nodiscard]] rpl::producer<SelectedItems> selectedItems() const;
	[[nodiscard]] rpl::producer<> firstSliceLoaded() const;
	void selectionAction(SelectionAction action);

	void paintBackground(QPainter &p, QRect clip);

private:
	HistoryView::Context listContext() override;
	bool listScrollTo(int top, bool syntetic = true) override;
	void listCancelRequest() override;
	void listDeleteRequest() override;
	void listTryProcessKeyInput(not_null<QKeyEvent*> e) override;
	rpl::producer<Data::MessagesSlice> listSource(
		Data::MessagePosition aroundId,
		int limitBefore,
		int limitAfter) override;
	bool listAllowsMultiSelect() override;
	bool listIsItemGoodForSelection(
		not_null<HistoryItem*> item) override;
	bool listIsLessInOrder(
		not_null<HistoryItem*> first,
		not_null<HistoryItem*> second) override;
	void listSelectionChanged(
		HistoryView::SelectedItems &&items) override;
	void listMarkReadTill(not_null<HistoryItem*> item) override;
	void listMarkContentsRead(
		const base::flat_set<not_null<HistoryItem*>> &items) override;
	HistoryView::MessagesBarData listMessagesBar(
		const std::vector<not_null<HistoryView::Element*>> &elements,
		bool markLastAsRead) override;
	void listContentRefreshed() override;
	void listUpdateDateLink(
		ClickHandlerPtr &link,
		not_null<HistoryView::Element*> view) override;
	bool listElementHideReply(
		not_null<const HistoryView::Element*> view) override;
	bool listElementShownUnread(
		not_null<const HistoryView::Element*> view) override;
	bool listIsGoodForAroundPosition(
		not_null<const HistoryView::Element*> view) override;
	void listSendBotCommand(
		const QString &command,
		const FullMsgId &context) override;
	void listSearch(
		const QString &query,
		const FullMsgId &context) override;
	void listHandleViaClick(not_null<UserData*> bot) override;
	not_null<Ui::ChatTheme*> listChatTheme() override;
	HistoryView::CopyRestrictionType listCopyRestrictionType(
		HistoryItem *item) override;
	HistoryView::CopyRestrictionType listCopyMediaRestrictionType(
		not_null<HistoryItem*> item) override;
	HistoryView::CopyRestrictionType listSelectRestrictionType() override;
	auto listAllowedReactionsValue()
		-> rpl::producer<Data::AllowedReactions> override;
	void listShowPremiumToast(
		not_null<DocumentData*> document) override;
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
	QString listElementAuthorRank(
		not_null<const HistoryView::Element*> view) override;
	bool listElementHideTopicButton(
		not_null<const HistoryView::Element*> view) override;
	History *listTranslateHistory() override;
	void listAddTranslatedItems(
		not_null<HistoryView::TranslateTracker*> tracker) override;
	not_null<Window::SessionController*> listWindow() override;
	not_null<QWidget*> listEmojiInteractionsParent() override;
	not_null<const Ui::ChatStyle*> listChatStyle() override;
	rpl::producer<bool> listChatWideValue() override;
	std::unique_ptr<HistoryView::Reactions::Manager>
		listMakeReactionsManager(
			QWidget *wheelEventsTarget,
			Fn<void(QRect)> update) override;
	void listVisibleAreaUpdated() override;
	std::shared_ptr<Ui::Show> listUiShow() override;
	void listShowPollResults(
		not_null<PollData*> poll,
		FullMsgId context) override;
	void listCancelUploadLayer(not_null<HistoryItem*> item) override;
	bool listAnimationsPaused() override;
	auto listSendingAnimation()
		-> Ui::MessageSendingAnimationController* override;
	Ui::ChatPaintContext listPreparePaintContext(
		Ui::ChatPaintContextArgs &&args) override;
	bool listMarkingContentRead() override;
	bool listIgnorePaintEvent(QWidget *w, QPaintEvent *e) override;
	bool listShowReactPremiumError(
		not_null<HistoryItem*> item,
		const Data::ReactionId &id) override;
	base::unique_qptr<Ui::PopupMenu> listFillSenderUserpicMenu(
		PeerId userpicPeerId) override;
	void listWindowSetInnerFocus() override;
	bool listAllowsDragForward() override;
	void listLaunchDrag(
		std::unique_ptr<QMimeData> data,
		Fn<void()> finished) override;

	const not_null<AbstractController*> _controller;
	const not_null<QWidget*> _parent;
	const Fn<void(int top)> _inlineScrollTo;
	const not_null<Main::Session*> _session;
	const not_null<Data::SavedSublist*> _sublist;
	const not_null<History*> _history;
	rpl::lifetime _themeLifetime;
	const std::shared_ptr<Ui::ChatTheme> _theme;
	const std::unique_ptr<Ui::ChatStyle> _chatStyle;

	QPointer<HistoryView::ListWidget> _list;
	bool _viewerRefreshed = false;
	bool _sliceLoaded = false;
	int _inlineVisibleTop = 0;
	int _inlineViewportHeight = 0;

	QImage _bg;

	rpl::event_stream<SelectedItems> _selectedItems;
	rpl::event_stream<> _firstSliceLoaded;

};

InlineSublistInner::InlineSublistInner(
	not_null<QWidget*> parent,
	not_null<AbstractController*> controller,
	not_null<Data::SavedSublist*> sublist,
	Fn<void(int top)> inlineScrollTo)
: _controller(controller)
, _parent(parent)
, _inlineScrollTo(std::move(inlineScrollTo))
, _session(&controller->session())
, _sublist(sublist)
, _history(sublist->owningHistory())
, _theme(Window::Theme::DefaultChatThemeOn(_themeLifetime))
, _chatStyle(
	std::make_unique<Ui::ChatStyle>(_session->colorIndicesValue())) {
	_chatStyle->apply(_theme.get());
	_list = Ui::CreateChild<HistoryView::ListWidget>(
		_parent.get(),
		_session,
		static_cast<HistoryView::ListDelegate*>(this));
	_list->show();
}

InlineSublistInner::~InlineSublistInner() {
	if (_list) {
		delete _list.data();
	}
}

void InlineSublistInner::updateGeometry(QRect rect) {
	if (rect.isEmpty()) {
		return;
	}
	_inlineViewportHeight = rect.height();
	_list->resizeToWidth(rect.width(), rect.height());
	if (!_viewerRefreshed) {
		_viewerRefreshed = true;
		_list->refreshViewer();
	}
	const auto ratio = style::DevicePixelRatio();
	_bg = QImage(
		rect.size() * ratio,
		QImage::Format_ARGB32_Premultiplied);
	_bg.setDevicePixelRatio(ratio);
	auto p = QPainter(&_bg);
	Window::SectionWidget::PaintBackground(
		p,
		_theme.get(),
		QSize(rect.width(), rect.height() * 2),
		QRect(QPoint(), rect.size()));
}

void InlineSublistInner::paintBackground(QPainter &p, QRect clip) {
	if (_bg.isNull()) {
		return;
	}
	p.drawImage(QPoint(0, std::max(_inlineVisibleTop, 0)), _bg);
}

void InlineSublistInner::setInlineVisibleRegion(int top, int bottom) {
	_inlineVisibleTop = top;
	_list->setVisibleTopBottom(top, bottom);
}

rpl::producer<SelectedItems> InlineSublistInner::selectedItems() const {
	return _selectedItems.events();
}

rpl::producer<> InlineSublistInner::firstSliceLoaded() const {
	return _firstSliceLoaded.events();
}

void InlineSublistInner::selectionAction(SelectionAction action) {
	switch (action) {
	case SelectionAction::Clear: _list->cancelSelection(); return;
	case SelectionAction::Forward:
		HistoryView::ConfirmForwardSelectedItems(_list);
		return;
	case SelectionAction::Delete:
		HistoryView::ConfirmDeleteSelectedItems(_list);
		return;
	default: return;
	}
}

HistoryView::Context InlineSublistInner::listContext() {
	return HistoryView::Context::SavedSublist;
}

bool InlineSublistInner::listScrollTo(int top, bool syntetic) {
	if (syntetic) {
		return true;
	}
	const auto max = _list
		? std::max(_list->height() - _inlineViewportHeight, 0)
		: 0;
	top = std::clamp(top, 0, max);
	if (_inlineScrollTo) {
		_inlineScrollTo(top);
	}
	return true;
}

void InlineSublistInner::listCancelRequest() {
	_list->cancelSelection();
}

void InlineSublistInner::listDeleteRequest() {
	HistoryView::ConfirmDeleteSelectedItems(_list);
}

void InlineSublistInner::listTryProcessKeyInput(not_null<QKeyEvent*> e) {
}

rpl::producer<Data::MessagesSlice> InlineSublistInner::listSource(
		Data::MessagePosition aroundId,
		int limitBefore,
		int limitAfter) {
	return _sublist->source(
		aroundId,
		limitBefore,
		limitAfter
	) | rpl::map([this](Data::MessagesSlice &&slice) {
		if (!_sliceLoaded) {
			_sliceLoaded = true;
			_firstSliceLoaded.fire({});
		}
		return std::move(slice);
	});
}

bool InlineSublistInner::listAllowsMultiSelect() {
	return true;
}

bool InlineSublistInner::listIsItemGoodForSelection(
		not_null<HistoryItem*> item) {
	return item->canBeSelected();
}

bool InlineSublistInner::listIsLessInOrder(
		not_null<HistoryItem*> first,
		not_null<HistoryItem*> second) {
	if (first->isRegular() && second->isRegular()) {
		return first->id < second->id;
	} else if (first->isRegular()) {
		return true;
	} else if (second->isRegular()) {
		return false;
	}
	return first->id < second->id;
}

void InlineSublistInner::listSelectionChanged(
		HistoryView::SelectedItems &&items) {
	auto result = SelectedItems();
	result.title = [](int count) {
		return tr::lng_media_selected_message(
			tr::now,
			lt_count,
			count,
			Ui::StringWithNumbers::FromString);
	};
	result.list.reserve(items.size());
	const auto sessionId = _session->uniqueId();
	for (const auto &item : items) {
		auto entry = SelectedItem(GlobalMsgId{ item.msgId, sessionId });
		entry.canDelete = item.canDelete;
		entry.canForward = item.canForward;
		result.list.push_back(std::move(entry));
	}
	_selectedItems.fire(std::move(result));
}

void InlineSublistInner::listMarkReadTill(not_null<HistoryItem*> item) {
}

void InlineSublistInner::listMarkContentsRead(
		const base::flat_set<not_null<HistoryItem*>> &items) {
}

HistoryView::MessagesBarData InlineSublistInner::listMessagesBar(
		const std::vector<not_null<HistoryView::Element*>> &elements,
		bool markLastAsRead) {
	return {};
}

void InlineSublistInner::listContentRefreshed() {
}

void InlineSublistInner::listUpdateDateLink(
		ClickHandlerPtr &link,
		not_null<HistoryView::Element*> view) {
}

bool InlineSublistInner::listElementHideReply(
		not_null<const HistoryView::Element*> view) {
	return false;
}

bool InlineSublistInner::listElementShownUnread(
		not_null<const HistoryView::Element*> view) {
	return false;
}

bool InlineSublistInner::listIsGoodForAroundPosition(
		not_null<const HistoryView::Element*> view) {
	return view->data()->isRegular();
}

void InlineSublistInner::listSendBotCommand(
		const QString &command,
		const FullMsgId &context) {
}

void InlineSublistInner::listSearch(
		const QString &query,
		const FullMsgId &context) {
}

void InlineSublistInner::listHandleViaClick(not_null<UserData*> bot) {
}

not_null<Ui::ChatTheme*> InlineSublistInner::listChatTheme() {
	return _theme.get();
}

auto InlineSublistInner::listCopyRestrictionType(HistoryItem *item)
-> HistoryView::CopyRestrictionType {
	return HistoryView::CopyRestrictionType::None;
}

auto InlineSublistInner::listCopyMediaRestrictionType(
		not_null<HistoryItem*> item)
-> HistoryView::CopyRestrictionType {
	return HistoryView::CopyRestrictionType::None;
}

auto InlineSublistInner::listSelectRestrictionType()
-> HistoryView::CopyRestrictionType {
	return HistoryView::CopyRestrictionType::None;
}

auto InlineSublistInner::listAllowedReactionsValue()
-> rpl::producer<Data::AllowedReactions> {
	return Data::PeerAllowedReactionsValue(_history->peer);
}

void InlineSublistInner::listShowPremiumToast(
		not_null<DocumentData*> document) {
}

void InlineSublistInner::listOpenPhoto(
		not_null<PhotoData*> photo,
		FullMsgId context) {
	const auto showDrawButton = Data::CanSendAnyOf(
		_history->peer,
		Data::FilesSendRestrictions());
	_controller->parentController()->openPhoto(
		photo,
		{ context, MsgId(), PeerId(), showDrawButton });
}

void InlineSublistInner::listOpenDocument(
		not_null<DocumentData*> document,
		FullMsgId context,
		bool showInMediaView) {
	const auto showDrawButton = Data::CanSendAnyOf(
		_history->peer,
		Data::FilesSendRestrictions());
	_controller->parentController()->openDocument(
		document,
		showInMediaView,
		{ context, MsgId(), PeerId(), showDrawButton });
}

void InlineSublistInner::listPaintEmpty(
		Painter &p,
		const Ui::ChatPaintContext &context) {
}

QString InlineSublistInner::listElementAuthorRank(
		not_null<const HistoryView::Element*> view) {
	return {};
}

bool InlineSublistInner::listElementHideTopicButton(
		not_null<const HistoryView::Element*> view) {
	return true;
}

History *InlineSublistInner::listTranslateHistory() {
	return nullptr;
}

void InlineSublistInner::listAddTranslatedItems(
		not_null<HistoryView::TranslateTracker*> tracker) {
}

not_null<Window::SessionController*> InlineSublistInner::listWindow() {
	return _controller->parentController();
}

not_null<QWidget*> InlineSublistInner::listEmojiInteractionsParent() {
	return _parent;
}

not_null<const Ui::ChatStyle*> InlineSublistInner::listChatStyle() {
	return _chatStyle.get();
}

rpl::producer<bool> InlineSublistInner::listChatWideValue() {
	return rpl::single(false);
}

auto InlineSublistInner::listMakeReactionsManager(
		QWidget *wheelEventsTarget,
		Fn<void(QRect)> update)
-> std::unique_ptr<HistoryView::Reactions::Manager> {
	return std::make_unique<HistoryView::Reactions::Manager>(
		wheelEventsTarget,
		std::move(update));
}

void InlineSublistInner::listVisibleAreaUpdated() {
}

std::shared_ptr<Ui::Show> InlineSublistInner::listUiShow() {
	return _controller->parentController()->uiShow();
}

void InlineSublistInner::listShowPollResults(
		not_null<PollData*> poll,
		FullMsgId context) {
	_controller->parentController()->showSection(
		std::make_shared<Info::Memento>(poll, context));
}

void InlineSublistInner::listCancelUploadLayer(
		not_null<HistoryItem*> item) {
}

bool InlineSublistInner::listAnimationsPaused() {
	return false;
}

auto InlineSublistInner::listSendingAnimation()
-> Ui::MessageSendingAnimationController* {
	return nullptr;
}

Ui::ChatPaintContext InlineSublistInner::listPreparePaintContext(
		Ui::ChatPaintContextArgs &&args) {
	const auto area = QRect(
		0,
		args.visibleAreaTop,
		args.visibleAreaWidth,
		args.visibleAreaHeight);
	const auto viewport = QRect(
		0,
		_inlineVisibleTop,
		args.visibleAreaWidth,
		std::max(_inlineViewportHeight, 1));
	return args.theme->preparePaintContext(
		_chatStyle.get(),
		viewport,
		area,
		args.clip,
		false);
}

bool InlineSublistInner::listMarkingContentRead() {
	return false;
}

bool InlineSublistInner::listIgnorePaintEvent(QWidget *w, QPaintEvent *e) {
	return false;
}

bool InlineSublistInner::listShowReactPremiumError(
		not_null<HistoryItem*> item,
		const Data::ReactionId &id) {
	return Window::ShowReactPremiumError(
		_controller->parentController(),
		item,
		id);
}

auto InlineSublistInner::listFillSenderUserpicMenu(PeerId userpicPeerId)
-> base::unique_qptr<Ui::PopupMenu> {
	return nullptr;
}

void InlineSublistInner::listWindowSetInnerFocus() {
}

bool InlineSublistInner::listAllowsDragForward() {
	return _controller->parentController()->adaptive().isOneColumn();
}

void InlineSublistInner::listLaunchDrag(
		std::unique_ptr<QMimeData> data,
		Fn<void()> finished) {
}

} // namespace

InlineSublist MakeInlineSublist(
		not_null<Ui::RpWidget*> parent,
		not_null<AbstractController*> controller,
		not_null<Data::SavedSublist*> sublist,
		Fn<void(int top)> scrollToRequest) {
	auto inner = std::make_shared<InlineSublistInner>(
		parent,
		controller,
		sublist,
		std::move(scrollToRequest));
	const auto raw = inner.get();
	return {
		.list = raw->list(),
		.updateGeometry = [raw](int width, int viewportHeight) {
			raw->updateGeometry(Rect(QSize(width, viewportHeight)));
		},
		.setVisibleRegion = [raw](int top, int bottom) {
			raw->setInlineVisibleRegion(top, bottom);
		},
		.paintBackground = [raw](QPainter &p, QRect clip) {
			raw->paintBackground(p, clip);
		},
		.selectedItems = raw->selectedItems(),
		.firstSliceLoaded = raw->firstSliceLoaded(),
		.selectionAction = [raw](SelectionAction action) {
			raw->selectionAction(action);
		},
		.guard = std::move(inner),
	};
}

} // namespace Info::Saved
