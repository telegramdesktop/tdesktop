/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/polls/info_polls_list_widget.h"

#include "data/data_poll_messages.h"
#include "data/data_shared_media.h"
#include "data/data_chat_participant_status.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_forum_topic.h"
#include "data/data_peer_values.h"
#include "data/data_saved_sublist.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/view/history_view_list_widget.h"
#include "history/view/history_view_corner_buttons.h"
#include "history/view/history_view_element.h"
#include "history/view/history_view_service_message.h"
#include "history/view/reactions/history_view_reactions_button.h"
#include "lottie/lottie_icon.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "info/info_controller.h"
#include "info/info_memento.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/chat/chat_style.h"
#include "ui/chat/chat_theme.h"
#include "ui/widgets/elastic_scroll.h"
#include "ui/widgets/scroll_area.h"
#include "ui/ui_utility.h"
#include "window/section_widget.h"
#include "window/themes/window_theme.h"
#include "window/main_window.h"
#include "window/window_session_controller.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/menu/menu_add_action_callback.h"
#include "info/info_wrap_widget.h"
#include "window/window_peer_menu.h"
#include "styles/style_chat.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_dialogs.h"
#include "styles/style_polls.h"
#include "styles/style_widgets.h"

namespace Info::Polls {

class ListWidget::Inner final
	: private HistoryView::ListDelegate
	, private HistoryView::CornerButtonsDelegate {
public:
	Inner(
		not_null<QWidget*> parent,
		not_null<AbstractController*> controller,
		Fn<void(int top)> inlineScrollTo = nullptr);
	~Inner();

	[[nodiscard]] not_null<Ui::ElasticScroll*> scroll() const {
		return _scroll.get();
	}
	[[nodiscard]] HistoryView::ListWidget *list() const {
		return _list;
	}

	void updateGeometry(QRect rect);
	void setInlineVisibleRegion(int top, int bottom);

	[[nodiscard]] int scrollTop() const;
	void setScrollTop(int top);
	void setSearchQuery(const QString &query);
	[[nodiscard]] bool canCreatePoll() const;
	void createPoll();

	[[nodiscard]] rpl::producer<SelectedItems> selectedItems() const;
	void selectionAction(SelectionAction action);

	void paintBackground(QPainter &p, QRect clip);

private:
	[[nodiscard]] SparseIdsMergedSlice::UniversalMsgId computeUniversalId(
		Data::MessagePosition aroundId) const;
	[[nodiscard]] Data::MessagesSlice sliceFromSparseIds(
		SparseIdsMergedSlice &&slice,
		SparseIdsMergedSlice::UniversalMsgId aroundId) const;
	void setupHistory();
	void updateInnerVisibleArea();
	void updateNewPollButtonPosition();
	void updateNewPollButtonVisibility();

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

	void cornerButtonsShowAtPosition(
		Data::MessagePosition position) override;
	Data::Thread *cornerButtonsThread() override;
	FullMsgId cornerButtonsCurrentId() override;
	bool cornerButtonsIgnoreVisibility() override;
	std::optional<bool> cornerButtonsDownShown() override;
	bool cornerButtonsUnreadMayBeShown() override;
	bool cornerButtonsHas(HistoryView::CornerButtonType type) override;

	const not_null<AbstractController*> _controller;
	const not_null<QWidget*> _parent;
	const Fn<void(int top)> _inlineScrollTo;
	const not_null<Main::Session*> _session;
	const not_null<History*> _history;
	const MsgId _topicRootId = 0;
	const PeerId _monoforumPeerId = 0;
	rpl::lifetime _themeLifetime;
	const std::shared_ptr<Ui::ChatTheme> _theme;
	const std::unique_ptr<Ui::ChatStyle> _chatStyle;
	const std::unique_ptr<Ui::ElasticScroll> _scroll;

	QPointer<HistoryView::ListWidget> _list;
	std::unique_ptr<HistoryView::CornerButtons> _cornerButtons;
	bool _viewerRefreshed = false;
	int _inlineVisibleTop = 0;
	int _inlineViewportHeight = 0;
	QString _searchQuery;

	object_ptr<Ui::RoundButton> _newPollButton = { nullptr };
	Ui::Animations::Simple _newPollButtonAnimation;
	bool _newPollButtonShown = true;

	std::unique_ptr<Lottie::Icon> _emptyIcon;
	Ui::Text::String _emptyText;
	bool _emptyAnimated = false;

	QImage _bg;

	rpl::event_stream<SelectedItems> _selectedItems;

};

ListWidget::Inner::Inner(
	not_null<QWidget*> parent,
	not_null<AbstractController*> controller,
	Fn<void(int top)> inlineScrollTo)
: _controller(controller)
, _parent(parent)
, _inlineScrollTo(std::move(inlineScrollTo))
, _session(&controller->session())
, _history(_session->data().history(controller->key().peer()))
, _topicRootId(controller->topic()
	? controller->topic()->rootId()
	: MsgId())
, _monoforumPeerId(controller->sublist()
	? controller->sublist()->sublistPeer()->id
	: PeerId())
, _theme(Window::Theme::DefaultChatThemeOn(_themeLifetime))
, _chatStyle(
	std::make_unique<Ui::ChatStyle>(_session->colorIndicesValue()))
, _scroll(_inlineScrollTo
	? nullptr
	: std::make_unique<Ui::ElasticScroll>(parent)) {
	_chatStyle->apply(_theme.get());
	setupHistory();
}

ListWidget::Inner::~Inner() {
	if (!_scroll && _list) {
		delete _list.data();
	}
}

void ListWidget::Inner::setupHistory() {
	if (!_scroll) {
		_list = Ui::CreateChild<HistoryView::ListWidget>(
			_parent.get(),
			_session,
			static_cast<HistoryView::ListDelegate*>(this));
		_list->show();
		return;
	}
	_list = _scroll->setOwnedWidget(
		object_ptr<HistoryView::ListWidget>(
			_scroll.get(),
			_session,
			static_cast<HistoryView::ListDelegate*>(this)));
	_cornerButtons = std::make_unique<HistoryView::CornerButtons>(
		_scroll.get(),
		_chatStyle.get(),
		static_cast<HistoryView::CornerButtonsDelegate*>(this));

	_scroll->scrolls(
	) | rpl::on_next([=] {
		updateInnerVisibleArea();
	}, _scroll->lifetime());
	_scroll->setOverscrollBg(QColor(0, 0, 0, 0));
	using Type = Ui::ElasticScroll::OverscrollType;
	_scroll->setOverscrollTypes(Type::Real, Type::Real);

	_list->scrollKeyEvents(
	) | rpl::on_next([=](not_null<QKeyEvent*> e) {
		_scroll->keyPressEvent(e);
	}, _scroll->lifetime());

	const auto topic = _controller->topic();
	const auto canCreate = topic
		? Data::CanSend(topic, ChatRestriction::SendPolls)
		: _history->peer->canCreatePolls();
	if (!canCreate) {
		return;
	}
	_newPollButton.create(
		_scroll.get(),
		tr::lng_polls_create_title(),
		st::defaultActiveButton);
	_newPollButton->setFullRadius(true);
	_newPollButton->setClickedCallback([=] {
		Window::PeerMenuCreatePoll(
			_controller->parentController(),
			_history->peer);
	});
	_newPollButton->show();
}

void ListWidget::Inner::updateNewPollButtonPosition() {
	if (!_newPollButton) {
		return;
	}
	const auto progress = _newPollButtonAnimation.value(
		_newPollButtonShown ? 1. : 0.);
	const auto buttonWidth = _newPollButton->width();
	const auto buttonHeight = _newPollButton->height();
	const auto x = (_scroll->width() - buttonWidth) / 2;
	const auto bottom = st::infoPollsNewButtonBottom;
	const auto top = anim::interpolate(
		_scroll->height(),
		_scroll->height() - buttonHeight - bottom,
		progress);
	_newPollButton->moveToLeft(x, top);
	const auto shouldBeHidden = !_newPollButtonShown
		&& !_newPollButtonAnimation.animating();
	if (shouldBeHidden != _newPollButton->isHidden()) {
		_newPollButton->setVisible(!shouldBeHidden);
	}
}

void ListWidget::Inner::updateNewPollButtonVisibility() {
	const auto scrollTop = _scroll->scrollTop();
	const auto scrollTopMax = _scroll->scrollTopMax();
	const auto nearBottom = (scrollTop + st::historyToDownShownAfter / 2)
		>= scrollTopMax;
	const auto shown = !nearBottom;
	if (_newPollButtonShown != shown) {
		_newPollButtonShown = shown;
		_newPollButtonAnimation.start(
			[=] { updateNewPollButtonPosition(); },
			shown ? 0. : 1.,
			shown ? 1. : 0.,
			st::historyToDownDuration);
	}
}

void ListWidget::Inner::updateGeometry(QRect rect) {
	if (_scroll) {
		_scroll->setGeometry(rect);
		_cornerButtons->updatePositions();
		updateNewPollButtonPosition();
	}

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

void ListWidget::Inner::paintBackground(QPainter &p, QRect clip) {
	if (_bg.isNull()) {
		return;
	}
	const auto position = _scroll
		? _scroll->geometry().topLeft()
		: QPoint(0, std::max(_inlineVisibleTop, 0));
	p.drawImage(position, _bg);
}

void ListWidget::Inner::setInlineVisibleRegion(int top, int bottom) {
	_inlineVisibleTop = top;
	_list->setVisibleTopBottom(top, bottom);
}

void ListWidget::Inner::updateInnerVisibleArea() {
	const auto scrollTop = _scroll->scrollTop();
	_list->setVisibleTopBottom(scrollTop, scrollTop + _scroll->height());
	_cornerButtons->updateJumpDownVisibility();
	updateNewPollButtonVisibility();
}

int ListWidget::Inner::scrollTop() const {
	return _scroll ? _scroll->scrollTop() : 0;
}

void ListWidget::Inner::setScrollTop(int top) {
	if (_scroll) {
		_scroll->scrollToY(top);
	}
}

void ListWidget::Inner::setSearchQuery(const QString &query) {
	if (_searchQuery == query) {
		return;
	}
	_emptyAnimated = false;
	if (_emptyIcon) {
		_emptyIcon->jumpTo(0, nullptr);
	}
	_searchQuery = query;
	if (_viewerRefreshed) {
		_list->refreshViewer();
	}
}

bool ListWidget::Inner::canCreatePoll() const {
	const auto topic = _controller->topic();
	return topic
		? Data::CanSend(topic, ChatRestriction::SendPolls)
		: _history->peer->canCreatePolls();
}

void ListWidget::Inner::createPoll() {
	Window::PeerMenuCreatePoll(
		_controller->parentController(),
		_history->peer);
}

rpl::producer<SelectedItems> ListWidget::Inner::selectedItems() const {
	return _selectedItems.events();
}

void ListWidget::Inner::selectionAction(SelectionAction action) {
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

SparseIdsMergedSlice::UniversalMsgId
ListWidget::Inner::computeUniversalId(
		Data::MessagePosition aroundId) const {
	const auto peerId = _history->peer->id;
	return ((aroundId.fullId.msg == ShowAtTheEndMsgId)
			|| (aroundId == Data::MaxMessagePosition))
		? (ServerMaxMsgId - 1)
		: (aroundId.fullId.peer == peerId)
		? aroundId.fullId.msg
		: (aroundId.fullId.msg
			? (aroundId.fullId.msg - ServerMaxMsgId)
			: (ServerMaxMsgId - 1));
}

Data::MessagesSlice ListWidget::Inner::sliceFromSparseIds(
		SparseIdsMergedSlice &&slice,
		SparseIdsMergedSlice::UniversalMsgId aroundId) const {
	auto result = Data::MessagesSlice();
	result.fullCount = slice.fullCount();
	result.skippedAfter = slice.skippedAfter();
	result.skippedBefore = slice.skippedBefore();
	const auto count = slice.size();
	result.ids.reserve(count);
	if (const auto msgId = slice.nearest(aroundId)) {
		result.nearestToAround = *msgId;
	}
	for (auto i = 0; i != count; ++i) {
		result.ids.push_back(slice[i]);
	}
	return result;
}

HistoryView::Context ListWidget::Inner::listContext() {
	return HistoryView::Context::ChatPreview;
}

bool ListWidget::Inner::listScrollTo(int top, bool syntetic) {
	if (!_scroll) {
		if (syntetic) {
			return true;
		}
		const auto max = _list
			? std::max(_list->height() - _inlineViewportHeight, 0)
			: 0;
		if (_inlineScrollTo) {
			_inlineScrollTo(std::clamp(top, 0, max));
		}
		return true;
	}
	top = std::clamp(top, 0, _scroll->scrollTopMax());
	if (_scroll->scrollTop() == top) {
		updateInnerVisibleArea();
		return false;
	}
	_scroll->scrollToY(top);
	return true;
}

void ListWidget::Inner::listCancelRequest() {
	_list->cancelSelection();
}

void ListWidget::Inner::listDeleteRequest() {
	HistoryView::ConfirmDeleteSelectedItems(_list);
}

void ListWidget::Inner::listTryProcessKeyInput(not_null<QKeyEvent*> e) {
}

rpl::producer<Data::MessagesSlice> ListWidget::Inner::listSource(
		Data::MessagePosition aroundId,
		int limitBefore,
		int limitAfter) {
	if (!_searchQuery.isEmpty()) {
		const auto universalId = computeUniversalId(aroundId);
		return _controller->mediaSource(
			universalId,
			limitBefore,
			limitAfter
		) | rpl::map([=](SparseIdsMergedSlice &&slice) {
			return sliceFromSparseIds(
				std::move(slice),
				universalId);
		});
	}
	return Data::PollMessagesViewer(
		_session,
		_history,
		_topicRootId,
		_monoforumPeerId,
		aroundId,
		limitBefore,
		limitAfter);
}

bool ListWidget::Inner::listAllowsMultiSelect() {
	return true;
}

bool ListWidget::Inner::listIsItemGoodForSelection(
		not_null<HistoryItem*> item) {
	return item->isRegular() && !item->isService();
}

bool ListWidget::Inner::listIsLessInOrder(
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

void ListWidget::Inner::listSelectionChanged(
		HistoryView::SelectedItems &&items) {
	auto result = SelectedItems(Storage::SharedMediaType::Poll);
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

void ListWidget::Inner::listMarkReadTill(not_null<HistoryItem*> item) {
}

void ListWidget::Inner::listMarkContentsRead(
		const base::flat_set<not_null<HistoryItem*>> &items) {
}

HistoryView::MessagesBarData ListWidget::Inner::listMessagesBar(
		const std::vector<not_null<HistoryView::Element*>> &elements,
		bool markLastAsRead) {
	return {};
}

void ListWidget::Inner::listContentRefreshed() {
}

void ListWidget::Inner::listUpdateDateLink(
		ClickHandlerPtr &link,
		not_null<HistoryView::Element*> view) {
}

bool ListWidget::Inner::listElementHideReply(
		not_null<const HistoryView::Element*> view) {
	return false;
}

bool ListWidget::Inner::listElementShownUnread(
		not_null<const HistoryView::Element*> view) {
	return false;
}

bool ListWidget::Inner::listIsGoodForAroundPosition(
		not_null<const HistoryView::Element*> view) {
	return view->data()->isRegular();
}

void ListWidget::Inner::listSendBotCommand(
		const QString &command,
		const FullMsgId &context) {
}

void ListWidget::Inner::listSearch(
		const QString &query,
		const FullMsgId &context) {
}

void ListWidget::Inner::listHandleViaClick(not_null<UserData*> bot) {
}

not_null<Ui::ChatTheme*> ListWidget::Inner::listChatTheme() {
	return _theme.get();
}

HistoryView::CopyRestrictionType
ListWidget::Inner::listCopyRestrictionType(HistoryItem *item) {
	return HistoryView::CopyRestrictionType::None;
}

HistoryView::CopyRestrictionType
ListWidget::Inner::listCopyMediaRestrictionType(
		not_null<HistoryItem*> item) {
	return HistoryView::CopyRestrictionType::None;
}

HistoryView::CopyRestrictionType
ListWidget::Inner::listSelectRestrictionType() {
	return HistoryView::CopyRestrictionType::None;
}

auto ListWidget::Inner::listAllowedReactionsValue()
-> rpl::producer<Data::AllowedReactions> {
	return Data::PeerAllowedReactionsValue(_history->peer);
}

void ListWidget::Inner::listShowPremiumToast(
		not_null<DocumentData*> document) {
}

void ListWidget::Inner::listOpenPhoto(
		not_null<PhotoData*> photo,
		FullMsgId context) {
}

void ListWidget::Inner::listOpenDocument(
		not_null<DocumentData*> document,
		FullMsgId context,
		bool showInMediaView) {
}

void ListWidget::Inner::listPaintEmpty(
		Painter &p,
		const Ui::ChatPaintContext &context) {
	if (!_emptyIcon) {
		const auto size = st::recentPeersEmptySize;
		_emptyIcon = Lottie::MakeIcon({
			.name = u"noresults"_q,
			.sizeOverride = { size, size },
		});
		_emptyText.setText(
			st::serviceTextStyle,
			tr::lng_polls_search_none(tr::now));
	}
	if (!_emptyAnimated) {
		_emptyAnimated = true;
		_emptyIcon->animate(
			[=] { _scroll ? _scroll->update() : _list->update(); },
			0,
			_emptyIcon->framesCount() - 1);
	}
	const auto iconSize = _emptyIcon->size();
	const auto width = st::repliesEmptyWidth;
	const auto padding = st::repliesEmptyPadding;
	const auto textWidth = width - padding.left() - padding.right();
	const auto textHeight = _emptyText.countHeight(textWidth);
	const auto height = padding.top()
		+ iconSize.height()
		+ st::repliesEmptySkip
		+ textHeight
		+ padding.bottom();
	const auto viewportWidth = _scroll ? _scroll->width() : _list->width();
	const auto viewportHeight = _scroll
		? _scroll->height()
		: _inlineViewportHeight;
	const auto r = QRect(
		(viewportWidth - width) / 2,
		std::max(_scroll ? 0 : _inlineVisibleTop, 0)
			+ (viewportHeight - height) / 3,
		width,
		height);
	HistoryView::ServiceMessagePainter::PaintBubble(
		p,
		context.st,
		r);

	_emptyIcon->paintInCenter(
		p,
		QRect(r.x(), r.y() + padding.top(), r.width(), iconSize.height()),
		st::msgServiceFg->c);
	p.setPen(st::msgServiceFg);
	_emptyText.draw(
		p,
		r.x() + (r.width() - textWidth) / 2,
		r.y() + padding.top() + iconSize.height() + st::repliesEmptySkip,
		textWidth,
		style::al_top);
}

QString ListWidget::Inner::listElementAuthorRank(
		not_null<const HistoryView::Element*> view) {
	return {};
}

bool ListWidget::Inner::listElementHideTopicButton(
		not_null<const HistoryView::Element*> view) {
	return true;
}

History *ListWidget::Inner::listTranslateHistory() {
	return nullptr;
}

void ListWidget::Inner::listAddTranslatedItems(
		not_null<HistoryView::TranslateTracker*> tracker) {
}

not_null<Window::SessionController*> ListWidget::Inner::listWindow() {
	return _controller->parentController();
}

not_null<QWidget*> ListWidget::Inner::listEmojiInteractionsParent() {
	return _scroll ? not_null<QWidget*>(_scroll.get()) : _parent;
}

not_null<const Ui::ChatStyle*> ListWidget::Inner::listChatStyle() {
	return _chatStyle.get();
}

rpl::producer<bool> ListWidget::Inner::listChatWideValue() {
	return rpl::single(false);
}

std::unique_ptr<HistoryView::Reactions::Manager>
ListWidget::Inner::listMakeReactionsManager(
		QWidget *wheelEventsTarget,
		Fn<void(QRect)> update) {
	return std::make_unique<HistoryView::Reactions::Manager>(
		wheelEventsTarget,
		std::move(update));
}

void ListWidget::Inner::listVisibleAreaUpdated() {
}

std::shared_ptr<Ui::Show> ListWidget::Inner::listUiShow() {
	return _controller->parentController()->uiShow();
}

void ListWidget::Inner::listShowPollResults(
		not_null<PollData*> poll,
		FullMsgId context) {
	_controller->parentController()->showSection(
		std::make_shared<Info::Memento>(poll, context));
}

void ListWidget::Inner::listCancelUploadLayer(
		not_null<HistoryItem*> item) {
}

bool ListWidget::Inner::listAnimationsPaused() {
	return false;
}

auto ListWidget::Inner::listSendingAnimation()
-> Ui::MessageSendingAnimationController* {
	return nullptr;
}

Ui::ChatPaintContext ListWidget::Inner::listPreparePaintContext(
		Ui::ChatPaintContextArgs &&args) {
	const auto area = QRect(
		0,
		args.visibleAreaTop,
		args.visibleAreaWidth,
		args.visibleAreaHeight);
	const auto viewport = [&] {
		if (!_scroll) {
			return QRect(
				0,
				_inlineVisibleTop,
				args.visibleAreaWidth,
				std::max(_inlineViewportHeight, 1));
		}
		const auto visibleAreaTopLocal = _scroll->mapFromGlobal(
			args.visibleAreaPositionGlobal).y();
		return QRect(
			0,
			args.visibleAreaTop - visibleAreaTopLocal,
			args.visibleAreaWidth,
			_scroll->height());
	}();
	return args.theme->preparePaintContext(
		_chatStyle.get(),
		viewport,
		area,
		args.clip,
		false);
}

bool ListWidget::Inner::listMarkingContentRead() {
	return false;
}

bool ListWidget::Inner::listIgnorePaintEvent(QWidget *w, QPaintEvent *e) {
	return false;
}

bool ListWidget::Inner::listShowReactPremiumError(
		not_null<HistoryItem*> item,
		const Data::ReactionId &id) {
	return Window::ShowReactPremiumError(
		_controller->parentController(),
		item,
		id);
}

base::unique_qptr<Ui::PopupMenu>
ListWidget::Inner::listFillSenderUserpicMenu(PeerId userpicPeerId) {
	return nullptr;
}

void ListWidget::Inner::listWindowSetInnerFocus() {
}

bool ListWidget::Inner::listAllowsDragForward() {
	return _controller->parentController()->adaptive().isOneColumn();
}

void ListWidget::Inner::listLaunchDrag(
		std::unique_ptr<QMimeData> data,
		Fn<void()> finished) {
}

void ListWidget::Inner::cornerButtonsShowAtPosition(
		Data::MessagePosition position) {
	if (position == Data::UnreadMessagePosition) {
		position = Data::MaxMessagePosition;
	}
	_list->showAtPosition(
		position,
		{},
		_cornerButtons->doneJumpFrom(position.fullId, {}, true));
}

Data::Thread *ListWidget::Inner::cornerButtonsThread() {
	return _history;
}

FullMsgId ListWidget::Inner::cornerButtonsCurrentId() {
	return {};
}

bool ListWidget::Inner::cornerButtonsIgnoreVisibility() {
	return false;
}

std::optional<bool> ListWidget::Inner::cornerButtonsDownShown() {
	const auto top = _scroll->scrollTop() + st::historyToDownShownAfter;
	if (top < _scroll->scrollTopMax()) {
		return true;
	} else if (_list->loadedAtBottomKnown()) {
		return !_list->loadedAtBottom();
	}
	return std::nullopt;
}

bool ListWidget::Inner::cornerButtonsUnreadMayBeShown() {
	return false;
}

bool ListWidget::Inner::cornerButtonsHas(
		HistoryView::CornerButtonType type) {
	return (type == HistoryView::CornerButtonType::Down)
		|| (type == HistoryView::CornerButtonType::PollVotes);
}

InlinePolls ListWidget::MakeInline(
		not_null<Ui::RpWidget*> parent,
		not_null<AbstractController*> controller,
		Fn<void(int top)> scrollToRequest) {
	auto inner = std::make_shared<Inner>(
		parent,
		controller,
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
		.selectionAction = [raw](SelectionAction action) {
			raw->selectionAction(action);
		},
		.setSearchQuery = [raw](const QString &query) {
			raw->setSearchQuery(query);
		},
		.canCreatePoll = raw->canCreatePoll(),
		.createPoll = [raw] {
			raw->createPoll();
		},
		.guard = std::move(inner),
	};
}

// --- ListMemento ---

ListMemento::ListMemento(
	not_null<PeerData*> peer,
	PeerId migratedPeerId)
: ContentMemento(peer, nullptr, nullptr, migratedPeerId) {
}

ListMemento::ListMemento(not_null<Data::ForumTopic*> topic)
: ContentMemento(topic->peer(), topic, nullptr, PeerId()) {
}

ListMemento::ListMemento(not_null<Data::SavedSublist*> sublist)
: ContentMemento(
	sublist->owningHistory()->peer,
	nullptr,
	sublist,
	PeerId()) {
}

Section ListMemento::section() const {
	return Section(Storage::SharedMediaType::Poll);
}

object_ptr<ContentWidget> ListMemento::createWidget(
		QWidget *parent,
		not_null<Controller*> controller,
		const QRect &geometry) {
	auto result = object_ptr<ListWidget>(parent, controller);
	result->setInternalState(geometry, this);
	return result;
}

// --- ListWidget ---

ListWidget::ListWidget(
	QWidget *parent,
	not_null<Controller*> controller)
: ContentWidget(parent, controller)
, _inner(std::make_unique<Inner>(this, controller)) {
	setInnerWidget(object_ptr<Ui::RpWidget>(this));
	scroll()->hide();

	scroll()->geometryValue(
	) | rpl::on_next([=](QRect rect) {
		_inner->updateGeometry(rect);
	}, lifetime());

	setupSearch();
}

ListWidget::~ListWidget() = default;

void ListWidget::fillTopBarMenu(
		const Ui::Menu::MenuCallback &addAction) {
}

void ListWidget::setupSearch() {
	auto search = controller()->searchFieldController();
	if (!search) {
		return;
	}
	controller()->setSearchEnabledByContent(true);

	controller()->mediaSourceQueryValue(
	) | rpl::on_next([=](const QString &query) {
		_inner->setSearchQuery(query);
	}, lifetime());
}

rpl::producer<SelectedItems> ListWidget::selectedListValue() const {
	return _inner->selectedItems();
}

void ListWidget::selectionAction(SelectionAction action) {
	_inner->selectionAction(action);
}

rpl::producer<QString> ListWidget::title() {
	return tr::lng_media_type_polls();
}

rpl::producer<int> ListWidget::desiredHeightValue() const {
	return sizeValue(
	) | rpl::map([=](QSize) {
		return maxVisibleHeight();
	}) | rpl::distinct_until_changed();
}

bool ListWidget::showInternal(not_null<ContentMemento*> memento) {
	if (!controller()->validateMementoPeer(memento)) {
		return false;
	}
	if (auto my = dynamic_cast<ListMemento*>(memento.get())) {
		restoreState(my);
		return true;
	}
	return false;
}

void ListWidget::setInternalState(
		const QRect &geometry,
		not_null<ListMemento*> memento) {
	setGeometry(geometry);
	Ui::SendPendingMoveResizeEvents(this);
	restoreState(memento);
}

std::shared_ptr<ContentMemento> ListWidget::doCreateMemento() {
	auto result = std::make_shared<ListMemento>(
		controller()->key().peer(),
		controller()->migratedPeerId());
	saveState(result.get());
	return result;
}

void ListWidget::saveState(not_null<ListMemento*> memento) {
	memento->setScrollTop(_inner->scrollTop());
}

void ListWidget::restoreState(not_null<ListMemento*> memento) {
	_inner->setScrollTop(memento->scrollTop());
}

void ListWidget::resizeEvent(QResizeEvent *e) {
	ContentWidget::resizeEvent(e);
	_inner->updateGeometry(scroll()->geometry());
}

void ListWidget::paintEvent(QPaintEvent *e) {
	ContentWidget::paintEvent(e);
	auto p = QPainter(this);
	_inner->paintBackground(p, e->rect());
}

} // namespace Info::Polls
