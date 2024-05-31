/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_chat_preview.h"

#include "base/unixtime.h"
#include "data/data_changes.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_forum_topic.h"
#include "data/data_history_messages.h"
#include "data/data_peer.h"
#include "data/data_peer_values.h"
#include "data/data_replies_list.h"
#include "data/data_session.h"
#include "data/data_thread.h"
#include "history/view/reactions/history_view_reactions_button.h"
#include "history/view/history_view_list_widget.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "info/profile/info_profile_cover.h"
#include "info/profile/info_profile_values.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/chat/chat_style.h"
#include "ui/chat/chat_theme.h"
#include "ui/controls/userpic_button.h"
#include "ui/widgets/menu/menu_item_base.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/elastic_scroll.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/shadow.h"
#include "window/themes/window_theme.h"
#include "window/section_widget.h"
#include "window/window_session_controller.h"
#include "styles/style_chat.h"
#include "styles/style_chat_helpers.h"

namespace HistoryView {
namespace {

class Item final
	: public Ui::Menu::ItemBase
	, private HistoryView::ListDelegate {
public:
	Item(not_null<Ui::RpWidget*> parent, not_null<Data::Thread*> thread);

	[[nodiscard]] not_null<QAction*> action() const override;
	[[nodiscard]] bool isEnabled() const override;

	[[nodiscard]] rpl::producer<ChatPreviewAction> actions() {
		return _actions.events();
	}

private:
	int contentHeight() const override;
	void paintEvent(QPaintEvent *e) override;

	void setupTop();
	void setupMarkRead();
	void setupBackground();
	void setupHistory();
	void updateInnerVisibleArea();

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
	bool listIsGoodForAroundPosition(
		not_null<const Element*> view) override;
	void listSendBotCommand(
		const QString &command,
		const FullMsgId &context) override;
	void listSearch(
		const QString &query,
		const FullMsgId &context) override;
	void listHandleViaClick(not_null<UserData*> bot) override;
	not_null<Ui::ChatTheme*> listChatTheme() override;
	CopyRestrictionType listCopyRestrictionType(
		HistoryItem *item) override;
	CopyRestrictionType listCopyRestrictionType() {
		return listCopyRestrictionType(nullptr);
	}
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
	not_null<Window::SessionController*> listWindow() override;
	not_null<QWidget*> listEmojiInteractionsParent() override;
	not_null<const Ui::ChatStyle*> listChatStyle() override;
	rpl::producer<bool> listChatWideValue() override;
	std::unique_ptr<Reactions::Manager> listMakeReactionsManager(
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
	void listWindowSetInnerFocus() override;
	bool listAllowsDragForward() override;
	void listLaunchDrag(
		std::unique_ptr<QMimeData> data,
		Fn<void()> finished) override;

	const not_null<QAction*> _dummyAction;
	const not_null<Main::Session*> _session;
	const not_null<Data::Thread*> _thread;
	const std::shared_ptr<Data::RepliesList> _replies;
	const not_null<History*> _history;
	const not_null<PeerData*> _peer;
	const std::shared_ptr<Ui::ChatTheme> _theme;
	const std::unique_ptr<Ui::ChatStyle> _chatStyle;
	const std::unique_ptr<Ui::AbstractButton> _top;
	const std::unique_ptr<Ui::ElasticScroll> _scroll;
	const std::unique_ptr<Ui::FlatButton> _markRead;

	QPointer<HistoryView::ListWidget> _inner;
	rpl::event_stream<ChatPreviewAction> _actions;

	QImage _bg;

};

struct StatusFields {
	QString text;
	bool active = false;
};

[[nodiscard]] rpl::producer<StatusFields> StatusValue(
		not_null<PeerData*> peer) {
	peer->updateFull();

	using UpdateFlag = Data::PeerUpdate::Flag;
	return peer->session().changes().peerFlagsValue(
		peer,
		UpdateFlag::OnlineStatus | UpdateFlag::Members
	) | rpl::map([=](const Data::PeerUpdate &update)
	-> StatusFields {
		const auto wrap = [](QString text) {
			return StatusFields{ .text = text };
		};
		if (const auto user = peer->asUser()) {
			const auto now = base::unixtime::now();
			return {
				.text = Data::OnlineText(user, now),
				.active = Data::OnlineTextActive(user, now),
			};
		} else if (const auto chat = peer->asChat()) {
			return wrap(!chat->amIn()
				? tr::lng_chat_status_unaccessible(tr::now)
				: (chat->count <= 0)
				? tr::lng_group_status(tr::now)
				: tr::lng_chat_status_members(
					tr::now,
					lt_count_decimal,
					chat->count));
		} else if (const auto channel = peer->asChannel()) {
			return wrap((channel->membersCount() > 0)
				? ((channel->isMegagroup()
					? tr::lng_chat_status_members
					: tr::lng_chat_status_subscribers)(
						tr::now,
						lt_count_decimal,
						channel->membersCount()))
				: (channel->isMegagroup()
					? tr::lng_group_status(tr::now)
					: tr::lng_channel_status(tr::now)));
		}
		Unexpected("Peer type in ChatPreview Item.");
	});

}

Item::Item(not_null<Ui::RpWidget*> parent, not_null<Data::Thread*> thread)
: Ui::Menu::ItemBase(parent, st::previewMenu.menu)
, _dummyAction(new QAction(parent))
, _session(&thread->session())
, _thread(thread)
, _replies(thread->asTopic() ? thread->asTopic()->replies() : nullptr)
, _history(thread->owningHistory())
, _peer(thread->peer())
, _theme(Window::Theme::DefaultChatThemeOn(lifetime()))
, _chatStyle(std::make_unique<Ui::ChatStyle>(_session->colorIndicesValue()))
, _top(std::make_unique<Ui::AbstractButton>(this))
, _scroll(std::make_unique<Ui::ElasticScroll>(this))
, _markRead(
	std::make_unique<Ui::FlatButton>(
		this,
		tr::lng_context_mark_read(tr::now),
		st::previewMarkRead)) {
	_chatStyle->apply(_theme.get());
	setPointerCursor(false);
	setMinWidth(st::previewMenu.menu.widthMin);
	resize(minWidth(), contentHeight());
	setupTop();
	setupMarkRead();
	setupBackground();
	setupHistory();
}

not_null<QAction*> Item::action() const {
	return _dummyAction;
}

bool Item::isEnabled() const {
	return false;
}

int Item::contentHeight() const {
	return st::previewMenu.maxHeight;
}

void Item::setupTop() {
	_top->setGeometry(0, 0, width(), st::previewTop.height);
	_top->setClickedCallback([=] {
		_actions.fire({ .openInfo = true });
	});
	_top->paintRequest() | rpl::start_with_next([=](QRect clip) {
		auto p = QPainter(_top.get());
		p.fillRect(clip, st::topBarBg);
	}, _top->lifetime());

	const auto topic = _thread->asTopic();
	auto nameValue = (topic
		? Info::Profile::TitleValue(topic)
		: Info::Profile::NameValue(_thread->peer())
	) | rpl::start_spawning(_top->lifetime());
	const auto name = Ui::CreateChild<Ui::FlatLabel>(
		_top.get(),
		rpl::duplicate(nameValue),
		st::previewName);
	name->setAttribute(Qt::WA_TransparentForMouseEvents);
	auto statusFields = StatusValue(
		_thread->peer()
	) | rpl::start_spawning(lifetime());
	auto statusText = rpl::duplicate(
		statusFields
	) | rpl::map([](StatusFields &&fields) {
		return fields.text;
	});
	const auto status = Ui::CreateChild<Ui::FlatLabel>(
		_top.get(),
		(topic
			? Info::Profile::NameValue(topic->channel())
			: std::move(statusText)),
		st::previewStatus);
	std::move(statusFields) | rpl::start_with_next([=](const StatusFields &fields) {
		status->setTextColorOverride(fields.active
			? st::windowActiveTextFg->c
			: std::optional<QColor>());
	}, status->lifetime());
	status->setAttribute(Qt::WA_TransparentForMouseEvents);
	const auto userpic = topic
		? nullptr
		: Ui::CreateChild<Ui::UserpicButton>(
			_top.get(),
			_thread->peer(),
			st::previewUserpic);
	if (userpic) {
		userpic->setAttribute(Qt::WA_TransparentForMouseEvents);
	}
	const auto icon = topic
		? Ui::CreateChild<Info::Profile::TopicIconButton>(
			this,
			topic,
			[=] { return false; })
		: nullptr;
	if (icon) {
		icon->setAttribute(Qt::WA_TransparentForMouseEvents);
	}

	const auto shadow = Ui::CreateChild<Ui::PlainShadow>(this);
	rpl::combine(
		_top->widthValue(),
		std::move(nameValue)
	) | rpl::start_with_next([=](int width, const auto &) {
		const auto &st = st::previewTop;
		name->resizeToNaturalWidth(width
			- st.namePosition.x()
			- st.photoPosition.x());
		name->move(st::previewTop.namePosition);
	}, name->lifetime());

	_top->geometryValue() | rpl::start_with_next([=](QRect geometry) {
		const auto &st = st::previewTop;
		status->resizeToWidth(geometry.width()
			- st.statusPosition.x()
			- st.photoPosition.x());
		status->move(st.statusPosition);
		shadow->setGeometry(
			geometry.x(),
			geometry.y() + geometry.height(),
			geometry.width(),
			st::lineWidth);
		if (userpic) {
			userpic->move(st.photoPosition);
		} else {
			icon->move(
				st.photoPosition.x() + (st.photoSize - icon->width()) / 2,
				st.photoPosition.y() + (st.photoSize - icon->height()) / 2);
		}
	}, shadow->lifetime());
}

void Item::setupMarkRead() {
	_markRead->resizeToWidth(width());
	_markRead->move(0, height() - _markRead->height());

	rpl::single(
		rpl::empty
	) | rpl::then(
		_thread->owner().chatsListFor(_thread)->unreadStateChanges(
		) | rpl::to_empty
	) | rpl::start_with_next([=] {
		const auto state = _thread->chatListBadgesState();
		const auto unread = (state.unreadCounter || state.unread);
		const auto hidden = _thread->asTopic()
			? (!unread)
			: _thread->peer()->isForum();
		if (hidden) {
			_markRead->hide();
			return;
		}
		_markRead->setText(unread
			? tr::lng_context_mark_read(tr::now)
			: tr::lng_context_mark_unread(tr::now));
		_markRead->setClickedCallback([=] {
			_actions.fire({ .markRead = unread, .markUnread = !unread });
		});
		_markRead->show();
	}, _markRead->lifetime());

	const auto shadow = Ui::CreateChild<Ui::PlainShadow>(this);
	_markRead->geometryValue() | rpl::start_with_next([=](QRect geometry) {
		shadow->setGeometry(
			geometry.x(),
			geometry.y() - st::lineWidth,
			geometry.width(),
			st::lineWidth);
	}, shadow->lifetime());
	shadow->showOn(_markRead->shownValue());
}

void Item::setupBackground() {
	const auto ratio = style::DevicePixelRatio();
	_bg = QImage(
		size() * ratio,
		QImage::Format_ARGB32_Premultiplied);

	const auto paint = [=] {
		auto p = QPainter(&_bg);
		Window::SectionWidget::PaintBackground(
			p,
			_theme.get(),
			QSize(width(), height() * 2),
			QRect(QPoint(), size()));
	};
	paint();
	_theme->repaintBackgroundRequests() | rpl::start_with_next([=] {
		paint();
		update();
	}, lifetime());
}

void Item::setupHistory() {
	_inner = _scroll->setOwnedWidget(object_ptr<ListWidget>(
		this,
		_session,
		static_cast<ListDelegate*>(this)));

	_markRead->shownValue() | rpl::start_with_next([=](bool shown) {
		const auto top = _top->height();
		const auto bottom = shown ? _markRead->height() : 0;
		_scroll->setGeometry(rect().marginsRemoved({ 0, top, 0, bottom }));
	}, _markRead->lifetime());

	_scroll->scrolls(
	) | rpl::start_with_next([=] {
		updateInnerVisibleArea();
	}, lifetime());
	_scroll->setOverscrollBg(QColor(0, 0, 0, 0));
	using Type = Ui::ElasticScroll::OverscrollType;
	_scroll->setOverscrollTypes(Type::Real, Type::Real);

	_scroll->events() | rpl::start_with_next([=](not_null<QEvent*> e) {
		if (e->type() == QEvent::MouseButtonDblClick) {
			const auto button = static_cast<QMouseEvent*>(e.get())->button();
			if (button == Qt::LeftButton) {
				const auto relative = Ui::MapFrom(
					_inner.data(),
					_scroll.get(),
					static_cast<QMouseEvent*>(e.get())->pos());
				if (const auto view = _inner->lookupItemByY(relative.y())) {
					_actions.fire(ChatPreviewAction{
						.openItemId = view->data()->fullId(),
					});
				} else {
					_actions.fire(ChatPreviewAction{});
				}
			}
		}
	}, lifetime());

	_inner->resizeToWidth(_scroll->width(), _scroll->height());

	_inner->refreshViewer();

	_inner->setAttribute(Qt::WA_TransparentForMouseEvents);
}

void Item::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	p.drawImage(0, 0, _bg);
}

void Item::updateInnerVisibleArea() {
	const auto scrollTop = _scroll->scrollTop();
	_inner->setVisibleTopBottom(scrollTop, scrollTop + _scroll->height());
}

Context Item::listContext() {
	return Context::ChatPreview;
}

bool Item::listScrollTo(int top, bool syntetic) {
	top = std::clamp(top, 0, _scroll->scrollTopMax());
	if (_scroll->scrollTop() == top) {
		updateInnerVisibleArea();
		return false;
	}
	_scroll->scrollToY(top);
	return true;
}

void Item::listCancelRequest() {
}

void Item::listDeleteRequest() {
}

void Item::listTryProcessKeyInput(not_null<QKeyEvent*> e) {
}

rpl::producer<Data::MessagesSlice> Item::listSource(
		Data::MessagePosition aroundId,
		int limitBefore,
		int limitAfter) {
	return _replies
		? _replies->source(aroundId, limitBefore, limitAfter)
		: Data::HistoryMessagesViewer(
			_thread->asHistory(),
			aroundId,
			limitBefore,
			limitAfter);
}

bool Item::listAllowsMultiSelect() {
	return false;
}

bool Item::listIsItemGoodForSelection(not_null<HistoryItem*> item) {
	return false;
}

bool Item::listIsLessInOrder(
		not_null<HistoryItem*> first,
		not_null<HistoryItem*> second) {
	if (first->isRegular() && second->isRegular()) {
		const auto firstPeer = first->history()->peer;
		const auto secondPeer = second->history()->peer;
		if (firstPeer == secondPeer) {
			return first->id < second->id;
		} else if (firstPeer->isChat()) {
			return true;
		}
		return false;
	} else if (first->isRegular()) {
		return true;
	} else if (second->isRegular()) {
		return false;
	}
	return first->id < second->id;
}

void Item::listSelectionChanged(SelectedItems &&items) {
}

void Item::listMarkReadTill(not_null<HistoryItem*> item) {
}

void Item::listMarkContentsRead(
	const base::flat_set<not_null<HistoryItem*>> &items) {
}

MessagesBarData Item::listMessagesBar(
		const std::vector<not_null<Element*>> &elements) {
	if (elements.empty()) {
		return {};
	} else if (!_replies && !_history->unreadCount()) {
		return {};
	}
	const auto repliesTill = _replies
		? _replies->computeInboxReadTillFull()
		: MsgId();
	const auto migrated = _replies ? nullptr : _history->migrateFrom();
	const auto migratedTill = migrated ? migrated->inboxReadTillId() : 0;
	const auto historyTill = _replies ? 0 : _history->inboxReadTillId();
	if (!_replies && !migratedTill && !historyTill) {
		return {};
	}

	const auto hidden = _replies && (repliesTill < 2);
	for (auto i = 0, count = int(elements.size()); i != count; ++i) {
		const auto item = elements[i]->data();
		if (!item->isRegular()
			|| item->out()
			|| (_replies && !item->replyToId())) {
			continue;
		}
		const auto inHistory = (item->history() == _history);
		if ((_replies && item->id > repliesTill)
			|| (migratedTill && (inHistory || item->id > migratedTill))
			|| (historyTill && inHistory && item->id > historyTill)) {
			return {
				.bar = {
					.element = elements[i],
					.hidden = hidden,
					.focus = true,
				},
				.text = tr::lng_unread_bar_some(),
			};
		}
	}
	return {};
}

void Item::listContentRefreshed() {
}

void Item::listUpdateDateLink(
	ClickHandlerPtr &link,
	not_null<Element*> view) {
}

bool Item::listElementHideReply(not_null<const Element*> view) {
	if (!view->isTopicRootReply()) {
		return false;
	}
	const auto reply = view->data()->Get<HistoryMessageReply>();
	return reply && !reply->fields().manualQuote;
}

bool Item::listElementShownUnread(not_null<const Element*> view) {
	return view->data()->unread(view->data()->history());
}

bool Item::listIsGoodForAroundPosition(not_null<const Element*> view) {
	return view->data()->isRegular();
}

void Item::listSendBotCommand(
	const QString &command,
	const FullMsgId &context) {
}

void Item::listSearch(
	const QString &query,
	const FullMsgId &context) {
}

void Item::listHandleViaClick(not_null<UserData*> bot) {
}

not_null<Ui::ChatTheme*> Item::listChatTheme() {
	return _theme.get();
}

CopyRestrictionType Item::listCopyRestrictionType(HistoryItem *item) {
	return CopyRestrictionType::None;
}

CopyRestrictionType Item::listCopyMediaRestrictionType(
		not_null<HistoryItem*> item) {
	return CopyRestrictionType::None;
}

CopyRestrictionType Item::listSelectRestrictionType() {
	return CopyRestrictionType::None;
}

auto Item::listAllowedReactionsValue()
-> rpl::producer<Data::AllowedReactions> {
	return rpl::single(Data::AllowedReactions());
}

void Item::listShowPremiumToast(not_null<DocumentData*> document) {
}

void Item::listOpenPhoto(
	not_null<PhotoData*> photo,
	FullMsgId context) {
}

void Item::listOpenDocument(
	not_null<DocumentData*> document,
	FullMsgId context,
	bool showInMediaView) {
}

void Item::listPaintEmpty(
	Painter &p,
	const Ui::ChatPaintContext &context) {
	// #TODO
}

QString Item::listElementAuthorRank(not_null<const Element*> view) {
	return {};
}

History *Item::listTranslateHistory() {
	return nullptr;
}

void Item::listAddTranslatedItems(
	not_null<TranslateTracker*> tracker) {
}

not_null<Window::SessionController*> Item::listWindow() {
	Unexpected("Item::listWindow.");
}

not_null<QWidget*> Item::listEmojiInteractionsParent() {
	return this;
}

not_null<const Ui::ChatStyle*> Item::listChatStyle() {
	return _chatStyle.get();
}

rpl::producer<bool> Item::listChatWideValue() {
	return rpl::single(false);
}

std::unique_ptr<Reactions::Manager> Item::listMakeReactionsManager(
		QWidget *wheelEventsTarget,
		Fn<void(QRect)> update) {
	return nullptr;
}

void Item::listVisibleAreaUpdated() {
}

std::shared_ptr<Ui::Show> Item::listUiShow() {
	Unexpected("Item::listUiShow.");
}

void Item::listShowPollResults(
	not_null<PollData*> poll,
	FullMsgId context) {
}

void Item::listCancelUploadLayer(not_null<HistoryItem*> item) {
}

bool Item::listAnimationsPaused() {
	return false;
}

auto Item::listSendingAnimation()
-> Ui::MessageSendingAnimationController* {
	return nullptr;
}

Ui::ChatPaintContext Item::listPreparePaintContext(
		Ui::ChatPaintContextArgs &&args) {
	const auto visibleAreaTopLocal = mapFromGlobal(
		args.visibleAreaPositionGlobal).y();
	const auto viewport = QRect(
		0,
		args.visibleAreaTop - visibleAreaTopLocal,
		args.visibleAreaWidth,
		height());
	return args.theme->preparePaintContext(
		_chatStyle.get(),
		viewport,
		args.clip,
		false);
}

bool Item::listMarkingContentRead() {
	return false;
}

bool Item::listIgnorePaintEvent(QWidget *w, QPaintEvent *e) {
	return false;
}

bool Item::listShowReactPremiumError(
		not_null<HistoryItem*> item,
		const Data::ReactionId &id) {
	return false;
}

void Item::listWindowSetInnerFocus() {
}

bool Item::listAllowsDragForward() {
	return false;
}

void Item::listLaunchDrag(
	std::unique_ptr<QMimeData> data,
	Fn<void()> finished) {
}

} // namespace

ChatPreview MakeChatPreview(
		QWidget *parent,
		not_null<Dialogs::Entry*> entry) {
	const auto thread = entry->asThread();
	if (!thread) {
		return {};
	}

	auto result = ChatPreview{
		.menu = base::make_unique_q<Ui::PopupMenu>(
			parent,
			st::previewMenu),
	};
	const auto menu = result.menu.get();

	auto action = base::make_unique_q<Item>(menu, thread);
	result.actions = action->actions();
	menu->addAction(std::move(action));
	if (const auto topic = thread->asTopic()) {
		const auto weak = Ui::MakeWeak(menu);
		topic->destroyed() | rpl::start_with_next([weak] {
			if (const auto strong = weak.data()) {
				LOG(("Preview hidden for a destroyed topic."));
				strong->hideMenu(true);
			}
		}, menu->lifetime());
	}

	return result;
}

} // namespace HistoryView
