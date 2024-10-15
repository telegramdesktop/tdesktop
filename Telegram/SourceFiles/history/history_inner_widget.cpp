/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/history_inner_widget.h"

#include "chat_helpers/stickers_emoji_pack.h"
#include "core/file_utilities.h"
#include "core/click_handler_types.h"
#include "history/history_item_helpers.h"
#include "history/view/controls/history_view_forward_panel.h"
#include "api/api_report.h"
#include "history/view/controls/history_view_draft_options.h"
#include "boxes/moderate_messages_box.h"
#include "history/view/media/history_view_sticker.h"
#include "history/view/media/history_view_web_page.h"
#include "history/view/reactions/history_view_reactions_button.h"
#include "history/view/reactions/history_view_reactions_selector.h"
#include "history/view/history_view_about_view.h"
#include "history/view/history_view_message.h"
#include "history/view/history_view_service_message.h"
#include "history/view/history_view_cursor_state.h"
#include "history/view/history_view_context_menu.h"
#include "history/view/history_view_quick_action.h"
#include "history/view/history_view_emoji_interactions.h"
#include "history/history_item_components.h"
#include "history/history_item_text.h"
#include "history/history_view_swipe.h"
#include "payments/payments_reaction_process.h"
#include "ui/widgets/menu/menu_add_action_callback_factory.h"
#include "ui/widgets/menu/menu_multiline_action.h"
#include "ui/widgets/popup_menu.h"
#include "ui/effects/path_shift_gradient.h"
#include "ui/effects/message_sending_animation_controller.h"
#include "ui/effects/reaction_fly_animation.h"
#include "ui/text/text_options.h"
#include "ui/text/text_isolated_emoji.h"
#include "ui/boxes/confirm_box.h"
#include "ui/boxes/edit_factcheck_box.h"
#include "ui/boxes/report_box_graphics.h"
#include "ui/layers/generic_box.h"
#include "ui/controls/delete_message_context_action.h"
#include "ui/inactive_press.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "ui/ui_utility.h"
#include "window/window_session_controller.h"
#include "window/window_controller.h"
#include "window/window_peer_menu.h"
#include "window/notifications_manager.h"
#include "info/info_memento.h"
#include "info/statistics/info_statistics_widget.h"
#include "boxes/about_sponsored_box.h"
#include "boxes/delete_messages_box.h"
#include "boxes/report_messages_box.h"
#include "boxes/sticker_set_box.h"
#include "boxes/premium_preview_box.h"
#include "boxes/translate_box.h"
#include "chat_helpers/message_field.h"
#include "chat_helpers/emoji_interactions.h"
#include "history/history_widget.h"
#include "history/view/history_view_translate_tracker.h"
#include "base/platform/base_platform_info.h"
#include "base/qt/qt_common_adapters.h"
#include "base/qt/qt_key_modifiers.h"
#include "base/unixtime.h"
#include "base/call_delayed.h"
#include "main/main_session.h"
#include "main/main_session_settings.h"
#include "mainwidget.h"
#include "menu/menu_item_download_files.h"
#include "core/application.h"
#include "apiwrap.h"
#include "api/api_attached_stickers.h"
#include "api/api_toggling_media.h"
#include "api/api_who_reacted.h"
#include "api/api_views.h"
#include "lang/lang_keys.h"
#include "data/components/factchecks.h"
#include "data/components/sponsored_messages.h"
#include "data/data_session.h"
#include "data/data_document.h"
#include "data/data_channel.h"
#include "data/data_forum_topic.h"
#include "data/data_photo_media.h"
#include "data/data_peer_values.h"
#include "data/data_chat.h"
#include "data/data_user.h"
#include "data/data_file_click_handler.h"
#include "data/data_histories.h"
#include "data/data_changes.h"
#include "dialogs/ui/dialogs_video_userpic.h"
#include "styles/style_chat.h"
#include "styles/style_menu_icons.h"

#include <QtGui/QClipboard>
#include <QtWidgets/QApplication>
#include <QtCore/QMimeData>

namespace {

constexpr auto kScrollDateHideTimeout = 1000;
constexpr auto kUnloadHeavyPartsPages = 2;
constexpr auto kClearUserpicsAfter = 50;

// Helper binary search for an item in a list that is not completely
// above the given top of the visible area or below the given bottom of the visible area
// is applied once for blocks list in a history and once for items list in the found block.
template <bool TopToBottom, typename T>
int BinarySearchBlocksOrItems(const T &list, int edge) {
	// static_cast to work around GCC bug #78693
	auto start = 0, end = static_cast<int>(list.size());
	while (end - start > 1) {
		auto middle = (start + end) / 2;
		auto top = list[middle]->y();
		auto chooseLeft = (TopToBottom ? (top <= edge) : (top < edge));
		if (chooseLeft) {
			start = middle;
		} else {
			end = middle;
		}
	}
	return start;
}

[[nodiscard]] bool CanSendReply(not_null<const HistoryItem*> item) {
	const auto peer = item->history()->peer;
	const auto topic = item->topic();
	return topic
		? Data::CanSendAnything(topic)
		: (Data::CanSendAnything(peer)
			&& (!peer->isChannel() || peer->asChannel()->amIn()));
}

void FillSponsoredMessagesMenu(
		not_null<Window::SessionController*> controller,
		FullMsgId itemId,
		not_null<Ui::PopupMenu*> menu) {
	const auto &data = controller->session().sponsoredMessages();
	const auto info = data.lookupDetails(itemId).info;
	const auto show = controller->uiShow();
	if (!info.empty()) {
		auto fillSubmenu = [&](not_null<Ui::PopupMenu*> menu) {
			const auto allText = ranges::accumulate(
				info,
				TextWithEntities(),
				[](TextWithEntities a, TextWithEntities b) {
					return a.text.isEmpty() ? b : a.append('\n').append(b);
				}).text;
			const auto callback = [=] {
				QGuiApplication::clipboard()->setText(allText);
				show->showToast(tr::lng_text_copied(tr::now));
			};
			for (const auto &i : info) {
				auto item = base::make_unique_q<Ui::Menu::MultilineAction>(
					menu,
					st::defaultMenu,
					st::historySponsorInfoItem,
					st::historyHasCustomEmojiPosition,
					base::duplicate(i));
				item->clicks(
				) | rpl::start_with_next(callback, menu->lifetime());
				menu->addAction(std::move(item));
				if (i != info.back()) {
					menu->addSeparator();
				}
			}
		};
		using namespace Ui::Menu;
		CreateAddActionCallback(menu)(MenuCallback::Args{
			.text = tr::lng_sponsored_info_menu(tr::now),
			.handler = nullptr,
			.icon = &st::menuIconChannel,
			.fillSubmenu = std::move(fillSubmenu),
		});
		menu->addSeparator(&st::expandedMenuSeparator);
	}
	menu->addAction(tr::lng_sponsored_hide_ads(tr::now), [=] {
		if (controller->session().premium()) {
			using Result = Data::SponsoredReportResult;
			controller->session().sponsoredMessages().createReportCallback(
				itemId)(Result::Id("-1"), [](const auto &) {});
		} else {
			ShowPremiumPreviewBox(controller, PremiumFeature::NoAds);
		}
	}, &st::menuIconCancel);
}

} // namespace

// flick scroll taken from http://qt-project.org/doc/qt-4.8/demos-embedded-anomaly-src-flickcharm-cpp.html

HistoryMainElementDelegateMixin::HistoryMainElementDelegateMixin() = default;

HistoryMainElementDelegateMixin::~HistoryMainElementDelegateMixin()
	= default;

class HistoryMainElementDelegate final
	: public HistoryView::ElementDelegate
	, public HistoryMainElementDelegateMixin {
public:
	using Element = HistoryView::Element;

	HistoryView::Context elementContext() override {
		return HistoryView::Context::History;
	}
	bool elementUnderCursor(
			not_null<const Element*> view) override {
		return (Element::Moused() == view);
	}
	HistoryView::SelectionModeResult elementInSelectionMode() override {
		return _widget
			? _widget->inSelectionMode()
			: HistoryView::SelectionModeResult();
	}
	bool elementIntersectsRange(
			not_null<const Element*> view,
			int from,
			int till) override {
		return _widget
			? _widget->elementIntersectsRange(view, from, till)
			: false;
	}
	void elementStartStickerLoop(
			not_null<const Element*> view) override {
		if (_widget) {
			_widget->elementStartStickerLoop(view);
		}
	}
	void elementShowPollResults(
			not_null<PollData*> poll,
			FullMsgId context) override {
		if (_widget) {
			_widget->elementShowPollResults(poll, context);
		}
	}
	void elementOpenPhoto(
			not_null<PhotoData*> photo,
			FullMsgId context) override {
		if (_widget) {
			_widget->elementOpenPhoto(photo, context);
		}
	}
	void elementOpenDocument(
			not_null<DocumentData*> document,
			FullMsgId context,
			bool showInMediaView = false) override {
		if (_widget) {
			_widget->elementOpenDocument(
				document,
				context,
				showInMediaView);
		}
	}
	void elementCancelUpload(const FullMsgId &context) override {
		if (_widget) {
			_widget->elementCancelUpload(context);
		}
	}
	void elementShowTooltip(
			const TextWithEntities &text,
			Fn<void()> hiddenCallback) override {
		if (_widget) {
			_widget->elementShowTooltip(text, hiddenCallback);
		}
	}
	bool elementAnimationsPaused() override {
		return _widget ? _widget->elementAnimationsPaused() : false;
	}
	bool elementHideReply(not_null<const Element*> view) override {
		if (!view->isTopicRootReply()) {
			return false;
		}
		const auto reply = view->data()->Get<HistoryMessageReply>();
		return reply && !reply->fields().manualQuote;
	}
	bool elementShownUnread(not_null<const Element*> view) override {
		return view->data()->unread(view->data()->history());
	}
	void elementSendBotCommand(
			const QString &command,
			const FullMsgId &context) override {
		if (_widget) {
			_widget->elementSendBotCommand(command, context);
		}
	}
	void elementSearchInList(
			const QString &query,
			const FullMsgId &context) override {
		if (_widget) {
			_widget->elementSearchInList(query, context);
		}
	}
	void elementHandleViaClick(not_null<UserData*> bot) override {
		if (_widget) {
			_widget->elementHandleViaClick(bot);
		}
	}
	bool elementIsChatWide() override {
		return _widget ? _widget->elementIsChatWide() : false;
	}
	not_null<Ui::PathShiftGradient*> elementPathShiftGradient() override {
		Expects(_widget != nullptr);

		return _widget->elementPathShiftGradient();
	}
	void elementReplyTo(const FullReplyTo &to) override {
		if (_widget) {
			_widget->elementReplyTo(to);
		}
	}
	void elementStartInteraction(not_null<const Element*> view) override {
		if (_widget) {
			_widget->elementStartInteraction(view);
		}
	}
	void elementStartPremium(
			not_null<const Element*> view,
			Element *replacing) override {
		if (_widget) {
			_widget->elementStartPremium(view, replacing);
		}
	}
	void elementCancelPremium(not_null<const Element*> view) override {
		if (_widget) {
			_widget->elementCancelPremium(view);
		}
	}
	void elementStartEffect(
			not_null<const Element*> view,
			Element *replacing) override {
		if (_widget) {
			_widget->elementStartEffect(view, replacing);
		}
	}

	QString elementAuthorRank(not_null<const Element*> view) override {
		return {};
	}

	bool elementHideTopicButton(not_null<const Element*> view) override {
		return false;
	}

	not_null<HistoryView::ElementDelegate*> delegate() override {
		return this;
	}

};

HistoryInner::HistoryInner(
	not_null<HistoryWidget*> historyWidget,
	not_null<Ui::ScrollArea*> scroll,
	not_null<Window::SessionController*> controller,
	not_null<History*> history)
: RpWidget(nullptr)
, _widget(historyWidget)
, _scroll(scroll)
, _controller(controller)
, _peer(history->peer)
, _history(history)
, _elementDelegate(_history->delegateMixin()->delegate())
, _emojiInteractions(std::make_unique<HistoryView::EmojiInteractions>(
	this,
	controller->content(),
	&controller->session(),
	[=](not_null<const Element*> view) { return itemTop(view); }))
, _migrated(history->migrateFrom())
, _translateTracker(std::make_unique<HistoryView::TranslateTracker>(history))
, _pathGradient(
	HistoryView::MakePathShiftGradient(
		controller->chatStyle(),
		[=] { update(); }))
, _reactionsManager(
	std::make_unique<HistoryView::Reactions::Manager>(
		this,
		[=](QRect updated) { update(updated); }))
, _touchSelectTimer([=] { onTouchSelect(); })
, _touchScrollTimer([=] { onTouchScrollTimer(); })
, _scrollDateCheck([this] { scrollDateCheck(); })
, _scrollDateHideTimer([this] { scrollDateHideByTimer(); }) {
	_history->delegateMixin()->setCurrent(this);
	if (_migrated) {
		_migrated->delegateMixin()->setCurrent(this);
		_migrated->translateTo(_history->translatedTo());
	}

	Window::ChatThemeValueFromPeer(
		controller,
		_peer
	) | rpl::start_with_next([=](std::shared_ptr<Ui::ChatTheme> &&theme) {
		_theme = std::move(theme);
		controller->setChatStyleTheme(_theme);
	}, lifetime());
	Assert(_theme != nullptr);

	setAttribute(Qt::WA_AcceptTouchEvents);

	refreshAboutView();

	setMouseTracking(true);
	_controller->gifPauseLevelChanged(
	) | rpl::start_with_next([=] {
		if (!elementAnimationsPaused()) {
			update();
		}
	}, lifetime());

	using PlayRequest = ChatHelpers::EmojiInteractionPlayRequest;
	_controller->emojiInteractions().playRequests(
	) | rpl::filter([=](const PlayRequest &request) {
		return (request.item->history() == _history)
			&& _controller->widget()->isActive();
	}) | rpl::start_with_next([=](PlayRequest &&request) {
		if (const auto view = viewByItem(request.item)) {
			_emojiInteractions->play(std::move(request), view);
		}
	}, lifetime());
	_emojiInteractions->playStarted(
	) | rpl::start_with_next([=](QString &&emoji) {
		_controller->emojiInteractions().playStarted(_peer, std::move(emoji));
	}, lifetime());

	_reactionsManager->chosen(
	) | rpl::start_with_next([=](ChosenReaction reaction) {
		_reactionsManager->updateButton({});
		reactionChosen(reaction);
	}, lifetime());

	session().data().peerDecorationsUpdated(
	) | rpl::start_with_next([=] {
		update();
	}, lifetime());
	session().data().itemRemoved(
	) | rpl::start_with_next(
		[this](auto item) { itemRemoved(item); },
		lifetime());
	session().data().viewRemoved(
	) | rpl::start_with_next(
		[this](auto view) { viewRemoved(view); },
		lifetime());
	rpl::merge(
		session().data().historyUnloaded(),
		session().data().historyCleared()
	) | rpl::filter([this](not_null<const History*> history) {
		return (_history == history);
	}) | rpl::start_with_next([this] {
		mouseActionCancel();
	}, lifetime());
	session().data().viewRepaintRequest(
	) | rpl::start_with_next([this](not_null<const Element*> view) {
		repaintItem(view);
	}, lifetime());
	session().data().viewLayoutChanged(
	) | rpl::filter([=](not_null<const Element*> view) {
		return (view == viewByItem(view->data())) && view->isUnderCursor();
	}) | rpl::start_with_next([=](not_null<const Element*> view) {
		mouseActionUpdate();
	}, lifetime());

	session().data().itemDataChanges(
	) | rpl::start_with_next([=](not_null<HistoryItem*> item) {
		if (const auto view = viewByItem(item)) {
			view->itemDataChanged();
		}
	}, lifetime());

	session().changes().historyUpdates(
		_history,
		(Data::HistoryUpdate::Flag::OutboxRead
			| Data::HistoryUpdate::Flag::TranslatedTo)
	) | rpl::start_with_next([=] {
		update();
	}, lifetime());

	HistoryView::Reactions::SetupManagerList(
		_reactionsManager.get(),
		_reactionsItem.value());

	Core::App().settings().cornerReactionValue(
	) | rpl::start_with_next([=](bool value) {
		_useCornerReaction = value;
		if (!value) {
			_reactionsManager->updateButton({});
		}
	}, lifetime());

	controller->adaptive().chatWideValue(
	) | rpl::start_with_next([=](bool wide) {
		_isChatWide = wide;
	}, lifetime());

	_selectScroll.scrolls(
	) | rpl::start_with_next([=](int d) {
		_scroll->scrollToY(_scroll->scrollTop() + d);
	}, _scroll->lifetime());

	setupSharingDisallowed();
	setupSwipeReply();
}

void HistoryInner::reactionChosen(const ChosenReaction &reaction) {
	const auto item = session().data().message(reaction.context);
	if (!item) {
		return;
	} else if (reaction.id.paid()) {
		Payments::ShowPaidReactionDetails(
			_controller,
			item,
			viewByItem(item),
			HistoryReactionSource::Selector);
		return;
	} else if (Window::ShowReactPremiumError(
			_controller,
			item,
			reaction.id)) {
		if (_menu) {
			_menu->hideMenu();
		}
		return;
	}
	item->toggleReaction(reaction.id, HistoryReactionSource::Selector);
	if (!ranges::contains(item->chosenReactions(), reaction.id)) {
		return;
	} else if (const auto view = viewByItem(item)) {
		if (const auto top = itemTop(view); top >= 0) {
			const auto geometry = reaction.localGeometry.isEmpty()
				? mapFromGlobal(reaction.globalGeometry)
				: reaction.localGeometry;
			view->animateReaction({
				.id = reaction.id,
				.flyIcon = reaction.icon,
				.flyFrom = geometry.translated(0, -top),
			});
		}
	}
}

Main::Session &HistoryInner::session() const {
	return _controller->session();
}

void HistoryInner::setupSharingDisallowed() {
	Expects(_peer != nullptr);

	if (_peer->isUser()) {
		_sharingDisallowed = false;
		return;
	}
	const auto chat = _peer->asChat();
	const auto channel = _peer->asChannel();
	_sharingDisallowed = chat
		? Data::PeerFlagValue(chat, ChatDataFlag::NoForwards)
		: Data::PeerFlagValue(
			channel,
			ChannelDataFlag::NoForwards
		) | rpl::type_erased();

	auto rights = chat
		? chat->adminRightsValue()
		: channel->adminRightsValue();
	auto canDelete = std::move(
		rights
	) | rpl::map([=] {
		return chat
			? chat->canDeleteMessages()
			: channel->canDeleteMessages();
	});
	rpl::combine(
		_sharingDisallowed.value(),
		std::move(canDelete)
	) | rpl::filter([=](bool disallowed, bool canDelete) {
		return hasSelectRestriction() && !getSelectedItems().empty();
	}) | rpl::start_with_next([=] {
		_widget->clearSelected();
		if (_mouseAction == MouseAction::PrepareSelect) {
			mouseActionCancel();
		}
	}, lifetime());
}

void HistoryInner::setupSwipeReply() {
	if (_peer && _peer->isChannel() && !_peer->isMegagroup()) {
		return;
	}
	HistoryView::SetupSwipeHandler(this, _scroll, [=, history = _history](
			HistoryView::ChatPaintGestureHorizontalData data) {
		const auto changed = (_gestureHorizontal.msgBareId != data.msgBareId)
			|| (_gestureHorizontal.translation != data.translation)
			|| (_gestureHorizontal.reachRatio != data.reachRatio);
		if (changed) {
			_gestureHorizontal = data;
			const auto item = history->peer->owner().message(
				history->peer->id,
				MsgId{ data.msgBareId });
			if (item) {
				repaintItem(item);
			}
		}
	}, [=, show = _controller->uiShow()](int cursorTop) {
		auto result = HistoryView::SwipeHandlerFinishData();
		if (inSelectionMode().inSelectionMode) {
			return result;
		}
		enumerateItems<EnumItemsDirection::BottomToTop>([&](
				not_null<Element*> view,
				int itemtop,
				int itembottom) {
			if ((cursorTop < itemtop)
				|| (cursorTop > itembottom)
				|| !view->data()->isRegular()
				|| view->data()->isService()) {
				return true;
			}
			const auto item = view->data();
			const auto canSendReply = CanSendReply(item);
			const auto canReply = (canSendReply || item->allowsForward());
			if (!canReply) {
				return true;
			}
			result.msgBareId = item->fullId().msg.bare;
			result.callback = [=, itemId = item->fullId()] {
				const auto still = show->session().data().message(itemId);
				const auto selected = selectedQuote(still);
				const auto replyToItemId = (selected.item
					? selected.item
					: still)->fullId();
				if (canSendReply) {
					_widget->replyToMessage({
						.messageId = replyToItemId,
						.quote = selected.text,
						.quoteOffset = selected.offset,
					});
					if (!selected.text.empty()) {
						_widget->clearSelected();
					}
				} else {
					HistoryView::Controls::ShowReplyToChatBox(show, {
						.messageId = replyToItemId,
						.quote = selected.text,
						.quoteOffset = selected.offset,
					});
				}
			};
			return false;
		});
		return result;
	}, _touchMaybeSelecting.value());
}

bool HistoryInner::hasSelectRestriction() const {
	if (!_sharingDisallowed.current()) {
		return false;
	} else if (const auto chat = _peer->asChat()) {
		return !chat->canDeleteMessages();
	} else if (const auto channel = _peer->asChannel()) {
		return !channel->canDeleteMessages();
	}
	return true;
}

void HistoryInner::messagesReceived(
		not_null<PeerData*> peer,
		const QVector<MTPMessage> &messages) {
	if (_history->peer == peer) {
		_history->addOlderSlice(messages);
		if (!messages.isEmpty()) {
			_translateTracker->addBunchFromBlocks();
		}
	} else if (_migrated && _migrated->peer == peer) {
		const auto newLoaded = _migrated
			&& _migrated->isEmpty()
			&& !_history->isEmpty();
		_migrated->addOlderSlice(messages);
		if (newLoaded) {
			_migrated->addNewerSlice(QVector<MTPMessage>());
		}
	}
}

void HistoryInner::messagesReceivedDown(
		not_null<PeerData*> peer,
		const QVector<MTPMessage> &messages) {
	if (_history->peer == peer) {
		const auto oldLoaded = _migrated
			&& _history->isEmpty()
			&& !_migrated->isEmpty();
		_history->addNewerSlice(messages);
		if (oldLoaded) {
			_history->addOlderSlice(QVector<MTPMessage>());
		}
	} else if (_migrated && _migrated->peer == peer) {
		_migrated->addNewerSlice(messages);
	}
}

void HistoryInner::repaintItem(const HistoryItem *item) {
	if (const auto view = viewByItem(item)) {
		repaintItem(view);
	}
}

void HistoryInner::repaintItem(const Element *view) {
	if (_widget->skipItemRepaint()) {
		return;
	}
	const auto top = itemTop(view);
	if (top >= 0) {
		const auto range = view->verticalRepaintRange();
		update(0, top + range.top, width(), range.height);
		const auto id = view->data()->fullId();
		if (const auto area = _reactionsManager->lookupEffectArea(id)) {
			update(*area);
		}
	}
}

template <bool TopToBottom, typename Method>
void HistoryInner::enumerateItemsInHistory(History *history, int historytop, Method method) {
	// No displayed messages in this history.
	if (historytop < 0 || history->isEmpty()) {
		return;
	}
	if (_visibleAreaBottom <= historytop || historytop + history->height() <= _visibleAreaTop) {
		return;
	}

	auto searchEdge = TopToBottom ? _visibleAreaTop : _visibleAreaBottom;

	// Binary search for blockIndex of the first block that is not completely below the visible area.
	auto blockIndex = BinarySearchBlocksOrItems<TopToBottom>(history->blocks, searchEdge - historytop);

	// Binary search for itemIndex of the first item that is not completely below the visible area.
	auto block = history->blocks[blockIndex].get();
	auto blocktop = historytop + block->y();
	auto blockbottom = blocktop + block->height();
	auto itemIndex = BinarySearchBlocksOrItems<TopToBottom>(block->messages, searchEdge - blocktop);

	while (true) {
		while (true) {
			auto view = block->messages[itemIndex].get();
			auto itemtop = blocktop + view->y();
			auto itembottom = itemtop + view->height();

			// Binary search should've skipped all the items that are above / below the visible area.
			if (TopToBottom) {
				Assert(itembottom > _visibleAreaTop);
			} else {
				Assert(itemtop < _visibleAreaBottom);
			}

			if (!method(view, itemtop, itembottom)) {
				return;
			}

			// Skip all the items that are below / above the visible area.
			if (TopToBottom) {
				if (itembottom >= _visibleAreaBottom) {
					return;
				}
			} else {
				if (itemtop <= _visibleAreaTop) {
					return;
				}
			}

			if (TopToBottom) {
				if (++itemIndex >= block->messages.size()) {
					break;
				}
			} else {
				if (--itemIndex < 0) {
					break;
				}
			}
		}

		// Skip all the rest blocks that are below / above the visible area.
		if (TopToBottom) {
			if (blockbottom >= _visibleAreaBottom) {
				return;
			}
		} else {
			if (blocktop <= _visibleAreaTop) {
				return;
			}
		}

		if (TopToBottom) {
			if (++blockIndex >= history->blocks.size()) {
				return;
			}
		} else {
			if (--blockIndex < 0) {
				return;
			}
		}
		block = history->blocks[blockIndex].get();
		blocktop = historytop + block->y();
		blockbottom = blocktop + block->height();
		if (TopToBottom) {
			itemIndex = 0;
		} else {
			itemIndex = block->messages.size() - 1;
		}
	}
}

bool HistoryInner::canHaveFromUserpics() const {
	if (_peer->isUser()
		&& !_peer->isSelf()
		&& !_peer->isRepliesChat()
		&& !_peer->isVerifyCodes()
		&& !_isChatWide) {
		return false;
	} else if (const auto channel = _peer->asBroadcast()) {
		return channel->signatureProfiles();
	}
	return true;
}

template <typename Method>
void HistoryInner::enumerateUserpics(Method method) {
	if (!canHaveFromUserpics()) {
		return;
	}

	// Find and remember the top of an attached messages pack
	// -1 means we didn't find an attached to next message yet.
	int lowestAttachedItemTop = -1;

	auto userpicCallback = [&](not_null<Element*> view, int itemtop, int itembottom) {
		// Skip all service messages.
		const auto item = view->data();
		if (view->isHidden() || item->isService()) {
			return true;
		}

		if (lowestAttachedItemTop < 0 && view->isAttachedToNext()) {
			lowestAttachedItemTop = itemtop + view->marginTop();
		}

		// Call method on a userpic for all messages that have it and for those who are not showing it
		// because of their attachment to the next message if they are bottom-most visible.
		if (view->displayFromPhoto() || (view->hasFromPhoto() && itembottom >= _visibleAreaBottom)) {
			if (lowestAttachedItemTop < 0) {
				lowestAttachedItemTop = itemtop + view->marginTop();
			}
			// Attach userpic to the bottom of the visible area with the same margin as the last message.
			auto userpicMinBottomSkip = st::historyPaddingBottom + st::msgMargin.bottom();
			auto userpicBottom = qMin(itembottom - view->marginBottom(), _visibleAreaBottom - userpicMinBottomSkip);

			// Do not let the userpic go above the attached messages pack top line.
			userpicBottom = qMax(userpicBottom, lowestAttachedItemTop + st::msgPhotoSize);

			// Call the template callback function that was passed
			// and return if it finished everything it needed.
			if (!method(view, userpicBottom - st::msgPhotoSize)) {
				return false;
			}
		}

		// Forget the found top of the pack, search for the next one from scratch.
		if (!view->isAttachedToNext()) {
			lowestAttachedItemTop = -1;
		}

		return true;
	};

	enumerateItems<EnumItemsDirection::TopToBottom>(userpicCallback);
}

template <typename Method>
void HistoryInner::enumerateDates(Method method) {
	auto drawtop = historyDrawTop();

	// Find and remember the bottom of an single-day messages pack
	// -1 means we didn't find a same-day with previous message yet.
	auto lowestInOneDayItemBottom = -1;

	auto dateCallback = [&](not_null<Element*> view, int itemtop, int itembottom) {
		const auto item = view->data();
		if (lowestInOneDayItemBottom < 0 && view->isInOneDayWithPrevious()) {
			lowestInOneDayItemBottom = itembottom - view->marginBottom();
		}

		// Call method on a date for all messages that have it and for those who are not showing it
		// because they are in a one day together with the previous message if they are top-most visible.
		if (view->displayDate() || (!item->isEmpty() && itemtop <= _visibleAreaTop)) {
			// skip the date of history migrate item if it will be in migrated
			if (itemtop < drawtop && item->history() == _history) {
				if (itemtop > _visibleAreaTop) {
					// Previous item (from the _migrated history) is drawing date now.
					return false;
				}
			}

			if (lowestInOneDayItemBottom < 0) {
				lowestInOneDayItemBottom = itembottom - view->marginBottom();
			}
			// Attach date to the top of the visible area with the same margin as it has in service message.
			int dateTop = qMax(itemtop, _visibleAreaTop) + st::msgServiceMargin.top();

			// Do not let the date go below the single-day messages pack bottom line.
			int dateHeight = st::msgServicePadding.bottom() + st::msgServiceFont->height + st::msgServicePadding.top();
			dateTop = qMin(dateTop, lowestInOneDayItemBottom - dateHeight);

			// Call the template callback function that was passed
			// and return if it finished everything it needed.
			if (!method(view, itemtop, dateTop)) {
				return false;
			}
		}

		// Forget the found bottom of the pack, search for the next one from scratch.
		if (!view->isInOneDayWithPrevious()) {
			lowestInOneDayItemBottom = -1;
		}

		return true;
	};

	enumerateItems<EnumItemsDirection::BottomToTop>(dateCallback);
}

TextSelection HistoryInner::computeRenderSelection(
		not_null<const SelectedItems*> selected,
		not_null<Element*> view) const {
	if (view->isHiddenByGroup()) {
		return TextSelection();
	}
	const auto item = view->data();
	const auto itemSelection = [&](not_null<HistoryItem*> item) {
		auto i = selected->find(item);
		if (i != selected->end()) {
			return i->second;
		}
		return TextSelection();
	};
	const auto result = itemSelection(item);
	if (result != TextSelection() && result != FullSelection) {
		return result;
	}
	if (const auto group = session().data().groups().find(item)) {
		auto parts = TextSelection();
		auto allFullSelected = true;
		const auto count = int(group->items.size());
		for (auto i = 0; i != count; ++i) {
			const auto part = group->items[i];
			const auto selection = itemSelection(part);
			if (part == item
				&& selection != FullSelection
				&& selection != TextSelection()) {
				return selection;
			} else if (selection == FullSelection) {
				parts = AddGroupItemSelection(parts, i);
			} else {
				allFullSelected = false;
			}
		}
		return allFullSelected ? FullSelection : parts;
	}
	return itemSelection(item);
}

TextSelection HistoryInner::itemRenderSelection(
		not_null<Element*> view,
		int selfromy,
		int seltoy) const {
	const auto item = view->data();
	const auto y = view->block()->y() + view->y();
	if (y >= selfromy && y < seltoy) {
		if (_dragSelecting && !item->isService() && item->isRegular()) {
			return FullSelection;
		}
	} else if (!_selected.empty()) {
		return computeRenderSelection(&_selected, view);
	}
	return TextSelection();
}

void HistoryInner::paintEmpty(
		Painter &p,
		not_null<const Ui::ChatStyle*> st,
		int width,
		int height) {
	if (!_emptyPainter) {
		_emptyPainter = std::make_unique<HistoryView::EmptyPainter>(
			_history);
	}
	_emptyPainter->paint(p, st, width, height);
}

Ui::ChatPaintContext HistoryInner::preparePaintContext(
		const QRect &clip) const {
	const auto visibleAreaPositionGlobal = mapToGlobal(
		QPoint(0, _visibleAreaTop));
	return _controller->preparePaintContext({
		.theme = _theme.get(),
		.clip = clip,
		.visibleAreaPositionGlobal = visibleAreaPositionGlobal,
		.visibleAreaTop = _visibleAreaTop,
		.visibleAreaWidth = width(),
	});
}

void HistoryInner::startEffectOnRead(not_null<HistoryItem*> item) {
	if (item->history() == _history) {
		if (const auto view = item->mainView()) {
			_emojiInteractions->playEffectOnRead(view);
		}
	}
}

void HistoryInner::paintEvent(QPaintEvent *e) {
	if (_controller->contentOverlapped(this, e)
		|| hasPendingResizedItems()) {
		return;
	} else if (_recountedAfterPendingResizedItems) {
		_recountedAfterPendingResizedItems = false;
		mouseActionUpdate();
	}

	Painter p(this);
	auto clip = e->rect();

	auto context = preparePaintContext(clip);
	context.gestureHorizontal = _gestureHorizontal;
	context.highlightPathCache = &_highlightPathCache;
	_pathGradient->startFrame(
		0,
		width(),
		std::min(st::msgMaxWidth / 2, width() / 2));

	const auto historyDisplayedEmpty = _history->isDisplayedEmpty()
		&& (!_migrated || _migrated->isDisplayedEmpty());
	if (const auto view = _aboutView ? _aboutView->view() : nullptr) {
		if (clip.y() < _aboutView->top + _aboutView->height
			&& clip.y() + clip.height() > _aboutView->top) {
			const auto top = _aboutView->top;
			context.translate(0, -top);
			context.selection = computeRenderSelection(&_selected, view);
			p.translate(0, top);
			view->draw(p, context);
			context.translate(0, top);
			p.translate(0, -top);
		}
	} else if (historyDisplayedEmpty) {
		paintEmpty(p, context.st, width(), height());
	} else {
		_emptyPainter = nullptr;
	}

	const auto mtop = migratedTop();
	const auto htop = historyTop();
	if (historyDisplayedEmpty || (mtop < 0 && htop < 0)) {
		return;
	}

	_translateTracker->startBunch();
	auto readTill = (HistoryItem*)nullptr;
	auto readContents = base::flat_set<not_null<HistoryItem*>>();
	auto startEffects = base::flat_set<not_null<const Element*>>();
	const auto markingAsViewed = _widget->markingContentsRead();
	const auto guard = gsl::finally([&] {
		if (_pinnedItem) {
			_translateTracker->add(_pinnedItem);
		}
		_translateTracker->finishBunch();
		if (!startEffects.empty()) {
			for (const auto &view : startEffects) {
				_emojiInteractions->playEffectOnRead(view);
			}
		}
		if (readTill && _widget->markingMessagesRead()) {
			session().data().histories().readInboxTill(readTill);
		}
		if (markingAsViewed && !readContents.empty()) {
			session().api().markContentsRead(readContents);
		}
		_userpicsCache.clear();
	});

	const auto processPainted = [&](
			not_null<Element*> view,
			int top,
			int height) {
		_translateTracker->add(view);
		const auto item = view->data();
		const auto isSponsored = item->isSponsored();
		const auto isUnread = !item->out()
			&& item->unread(_history)
			&& (item->history() == _history);
		const auto withReaction = item->hasUnreadReaction();
		const auto yShown = [&](int y) {
			return (_visibleAreaBottom >= y && _visibleAreaTop <= y);
		};
		const auto markShown = isSponsored
			? view->markSponsoredViewed(_visibleAreaBottom - top)
			: withReaction
			? yShown(top + context.reactionInfo->position.y())
			: isUnread
			? yShown(top + height)
			: yShown(top + height / 2);
		if (markShown) {
			if (isSponsored) {
				session().sponsoredMessages().view(item->fullId());
			} else if (isUnread) {
				readTill = item;
			}
			if (markingAsViewed && item->hasUnwatchedEffect()) {
				startEffects.emplace(view);
			}
			if (markingAsViewed && item->hasViews()) {
				session().api().views().scheduleIncrement(item);
			}
			if (withReaction) {
				readContents.insert(item);
			} else if (item->isUnreadMention()
				&& !item->isUnreadMedia()) {
				readContents.insert(item);
				_widget->enqueueMessageHighlight({ item });
			}
		}
		session().data().reactions().poll(item, context.now);
		if (item->hasUnpaidContent()) {
			session().api().views().pollExtendedMedia(item);
		}
		_reactionsManager->recordCurrentReactionEffect(
			item->fullId(),
			QPoint(0, top));
	};

	adjustCurrent(clip.top());

	const auto drawToY = clip.y() + clip.height();

	auto selfromy = itemTop(_dragSelFrom);
	auto seltoy = itemTop(_dragSelTo);
	if (selfromy < 0 || seltoy < 0) {
		selfromy = seltoy = -1;
	} else {
		seltoy += _dragSelTo->height();
	}

	const auto hdrawtop = historyDrawTop();
	if (mtop >= 0) {
		auto iBlock = (_curHistory == _migrated ? _curBlock : (_migrated->blocks.size() - 1));
		auto block = _migrated->blocks[iBlock].get();
		auto iItem = (_curHistory == _migrated ? _curItem : (block->messages.size() - 1));
		auto view = block->messages[iItem].get();
		auto top = mtop + block->y() + view->y();
		context.translate(0, -top);
		p.translate(0, top);
		if (context.clip.y() < view->height()) while (top < drawToY) {
			const auto height = view->height();
			context.reactionInfo
				= _reactionsManager->currentReactionPaintInfo();
			context.outbg = view->hasOutLayout();
			context.selection = itemRenderSelection(
				view,
				selfromy - mtop,
				seltoy - mtop);
			context.highlight = _widget->itemHighlight(view->data());
			view->draw(p, context);
			processPainted(view, top, height);

			top += height;
			context.translate(0, -height);
			p.translate(0, height);

			++iItem;
			if (iItem == block->messages.size()) {
				iItem = 0;
				++iBlock;
				if (iBlock == _migrated->blocks.size()) {
					break;
				}
				block = _migrated->blocks[iBlock].get();
			}
			view = block->messages[iItem].get();
		}
		context.translate(0, top);
		p.translate(0, -top);
	}
	if (htop >= 0) {
		auto iBlock = (_curHistory == _history ? _curBlock : 0);
		auto block = _history->blocks[iBlock].get();
		auto iItem = (_curHistory == _history ? _curItem : 0);
		auto view = block->messages[iItem].get();
		auto top = htop + block->y() + view->y();
		context.clip = clip.intersected(
			QRect(0, hdrawtop, width(), clip.top() + clip.height()));
		context.translate(0, -top);
		p.translate(0, top);
		const auto &sendingAnimation = _controller->sendingAnimation();
		while (top < drawToY) {
			const auto height = view->height();
			const auto item = view->data();
			if ((context.clip.y() < height)
				&& (hdrawtop < top + height)
				&& !sendingAnimation.hasAnimatedMessage(item)) {
				context.reactionInfo
					= _reactionsManager->currentReactionPaintInfo();
				context.outbg = view->hasOutLayout();
				context.selection = itemRenderSelection(
					view,
					selfromy - htop,
					seltoy - htop);
				context.highlight = _widget->itemHighlight(item);
				view->draw(p, context);
				processPainted(view, top, height);
			}
			top += height;
			context.translate(0, -height);
			p.translate(0, height);

			++iItem;
			if (iItem == block->messages.size()) {
				iItem = 0;
				++iBlock;
				if (iBlock == _history->blocks.size()) {
					break;
				}
				block = _history->blocks[iBlock].get();
			}
			view = block->messages[iItem].get();
		}
		context.translate(0, top);
		p.translate(0, -top);
	}

	enumerateUserpics([&](not_null<Element*> view, int userpicTop) {
		// stop the enumeration if the userpic is below the painted rect
		if (userpicTop >= clip.top() + clip.height()) {
			return false;
		}

		// paint the userpic if it intersects the painted rect
		if (userpicTop + st::msgPhotoSize > clip.top()) {
			const auto item = view->data();
			const auto hasTranslation = context.gestureHorizontal.translation
				&& (context.gestureHorizontal.msgBareId
					== item->fullId().msg.bare);
			if (hasTranslation) {
				p.translate(context.gestureHorizontal.translation, 0);
				update(
					QRect(
						st::historyPhotoLeft
							+ context.gestureHorizontal.translation,
						userpicTop,
						st::msgPhotoSize
							- context.gestureHorizontal.translation,
						st::msgPhotoSize));
			}
			if (const auto from = item->displayFrom()) {
				Dialogs::Ui::PaintUserpic(
					p,
					from,
					validateVideoUserpic(from),
					_userpics[from],
					st::historyPhotoLeft,
					userpicTop,
					width(),
					st::msgPhotoSize,
					context.paused);
			} else if (const auto info = item->displayHiddenSenderInfo()) {
				if (info->customUserpic.empty()) {
					info->emptyUserpic.paintCircle(
						p,
						st::historyPhotoLeft,
						userpicTop,
						width(),
						st::msgPhotoSize);
				} else {
					auto &userpic = _hiddenSenderUserpics[item->id];
					const auto valid = info->paintCustomUserpic(
						p,
						userpic,
						st::historyPhotoLeft,
						userpicTop,
						width(),
						st::msgPhotoSize);
					if (!valid) {
						info->customUserpic.load(&session(), item->fullId());
					}
				}
			} else {
				Unexpected("Corrupt forwarded information in message.");
			}
			if (hasTranslation) {
				p.translate(-_gestureHorizontal.translation, 0);
			}
		}
		return true;
	});

	const auto dateHeight = st::msgServicePadding.bottom()
		+ st::msgServiceFont->height
		+ st::msgServicePadding.top();
	//QDate lastDate;
	//if (!_history->isEmpty()) {
	//	lastDate = _history->blocks.back()->messages.back()->data()->date.date();
	//}

	//// if item top is before this value always show date as a floating date
	//int showFloatingBefore = height() - 2 * (_visibleAreaBottom - _visibleAreaTop) - dateHeight;

	auto scrollDateOpacity = _scrollDateOpacity.value(_scrollDateShown ? 1. : 0.);
	enumerateDates([&](not_null<Element*> view, int itemtop, int dateTop) {
		// stop the enumeration if the date is above the painted rect
		if (dateTop + dateHeight <= clip.top()) {
			return false;
		}

		const auto displayDate = view->displayDate();
		auto dateInPlace = displayDate;
		if (dateInPlace) {
			const auto correctDateTop = itemtop + st::msgServiceMargin.top();
			dateInPlace = (dateTop < correctDateTop + dateHeight);
		}
		//bool noFloatingDate = (item->date.date() == lastDate && displayDate);
		//if (noFloatingDate) {
		//	if (itemtop < showFloatingBefore) {
		//		noFloatingDate = false;
		//	}
		//}

		// paint the date if it intersects the painted rect
		if (dateTop < clip.top() + clip.height()) {
			auto opacity = (dateInPlace/* || noFloatingDate*/) ? 1. : scrollDateOpacity;
			if (opacity > 0.) {
				p.setOpacity(opacity);
				const auto dateY = false // noFloatingDate
					? itemtop
					: (dateTop - st::msgServiceMargin.top());
				if (const auto date = view->Get<HistoryView::DateBadge>()) {
					date->paint(p, context.st, dateY, _contentWidth, _isChatWide);
				} else {
					HistoryView::ServiceMessagePainter::PaintDate(
						p,
						context.st,
						view->dateTime(),
						dateY,
						_contentWidth,
						_isChatWide);
				}
			}
		}
		return true;
	});
	p.setOpacity(1.);

	_reactionsManager->paint(p, context);
}

bool HistoryInner::eventHook(QEvent *e) {
	if (e->type() == QEvent::TouchBegin
		|| e->type() == QEvent::TouchUpdate
		|| e->type() == QEvent::TouchEnd
		|| e->type() == QEvent::TouchCancel) {
		QTouchEvent *ev = static_cast<QTouchEvent*>(e);
		if (ev->device()->type() == base::TouchDevice::TouchScreen) {
			touchEvent(ev);
			return true;
		}
	}
	return RpWidget::eventHook(e);
}

int HistoryInner::SelectionViewOffset(
		not_null<const HistoryInner*> inner,
		not_null<const Element*> view) {
	if (inner->_lastInSelectionMode) {
		const auto translation
			= Element::AdditionalSpaceForSelectionCheckbox(view);
		const auto progress = inner->_inSelectionModeAnimation.value(1.);
		return translation * progress;
	}
	return 0;
}

HistoryInner::VideoUserpic *HistoryInner::validateVideoUserpic(
		not_null<PeerData*> peer) {
	if (!peer->isPremium()
		|| peer->userpicPhotoUnknown()
		|| !peer->userpicHasVideo()) {
		_videoUserpics.remove(peer);
		return nullptr;
	}
	const auto i = _videoUserpics.find(peer);
	if (i != end(_videoUserpics)) {
		return i->second.get();
	}
	const auto repaint = [=] {
		if (hasPendingResizedItems()) {
			return;
		}
		enumerateUserpics([&](not_null<Element*> view, int userpicTop) {
			// stop the enumeration if the userpic is below the painted rect
			if (userpicTop >= _visibleAreaBottom) {
				return false;
			}

			// repaint the userpic if it intersects the painted rect
			if (userpicTop + st::msgPhotoSize > _visibleAreaTop) {
				if (const auto from = view->data()->displayFrom()) {
					if (from == peer) {
						rtlupdate(
							st::historyPhotoLeft,
							userpicTop,
							st::msgPhotoSize,
							st::msgPhotoSize);
					}
				}
			}
			return true;
		});
	};
	return _videoUserpics.emplace(peer, std::make_unique<VideoUserpic>(
		peer,
		repaint
	)).first->second.get();
}

void HistoryInner::onTouchScrollTimer() {
	auto nowTime = crl::now();
	if (_touchScrollState == Ui::TouchScrollState::Acceleration && _touchWaitingAcceleration && (nowTime - _touchAccelerationTime) > 40) {
		_touchScrollState = Ui::TouchScrollState::Manual;
		touchResetSpeed();
	} else if (_touchScrollState == Ui::TouchScrollState::Auto || _touchScrollState == Ui::TouchScrollState::Acceleration) {
		int32 elapsed = int32(nowTime - _touchTime);
		QPoint delta = _touchSpeed * elapsed / 1000;
		const auto consumedHorizontal = consumeScrollAction(delta);
		if (consumedHorizontal) {
			_horizontalScrollLocked = true;
		}
		const auto hasScrolled = consumedHorizontal
			|| (!_horizontalScrollLocked && _widget->touchScroll(delta));

		if (_touchSpeed.isNull() || !hasScrolled) {
			_touchScrollState = Ui::TouchScrollState::Manual;
			_touchScroll = false;
			_horizontalScrollLocked = false;
			_touchScrollTimer.cancel();
		} else {
			_touchTime = nowTime;
		}
		touchDeaccelerate(elapsed);
	}
}

void HistoryInner::touchUpdateSpeed() {
	const auto nowTime = crl::now();
	if (_touchPrevPosValid) {
		const int elapsed = nowTime - _touchSpeedTime;
		if (elapsed) {
			const QPoint newPixelDiff = (_touchPos - _touchPrevPos);
			const QPoint pixelsPerSecond = newPixelDiff * (1000 / elapsed);

			// fingers are inacurates, we ignore small changes to avoid stopping the autoscroll because
			// of a small horizontal offset when scrolling vertically
			const int newSpeedY = (qAbs(pixelsPerSecond.y()) > Ui::kFingerAccuracyThreshold) ? pixelsPerSecond.y() : 0;
			const int newSpeedX = (qAbs(pixelsPerSecond.x()) > Ui::kFingerAccuracyThreshold) ? pixelsPerSecond.x() : 0;
			if (_touchScrollState == Ui::TouchScrollState::Auto) {
				const int oldSpeedY = _touchSpeed.y();
				const int oldSpeedX = _touchSpeed.x();
				if ((oldSpeedY <= 0 && newSpeedY <= 0) || ((oldSpeedY >= 0 && newSpeedY >= 0)
					&& (oldSpeedX <= 0 && newSpeedX <= 0)) || (oldSpeedX >= 0 && newSpeedX >= 0)) {
					_touchSpeed.setY(std::clamp(
						(oldSpeedY + (newSpeedY / 4)),
						-Ui::kMaxScrollAccelerated,
						+Ui::kMaxScrollAccelerated));
					_touchSpeed.setX(std::clamp(
						(oldSpeedX + (newSpeedX / 4)),
						-Ui::kMaxScrollAccelerated,
						+Ui::kMaxScrollAccelerated));
				} else {
					_touchSpeed = QPoint();
				}
			} else {
				// we average the speed to avoid strange effects with the last delta
				if (!_touchSpeed.isNull()) {
					_touchSpeed.setX(std::clamp(
						(_touchSpeed.x() / 4) + (newSpeedX * 3 / 4),
						-Ui::kMaxScrollFlick,
						+Ui::kMaxScrollFlick));
					_touchSpeed.setY(std::clamp(
						(_touchSpeed.y() / 4) + (newSpeedY * 3 / 4),
						-Ui::kMaxScrollFlick,
						+Ui::kMaxScrollFlick));
				} else {
					_touchSpeed = QPoint(newSpeedX, newSpeedY);
				}
			}
		}
	} else {
		_touchPrevPosValid = true;
	}
	_touchSpeedTime = nowTime;
	_touchPrevPos = _touchPos;
}

void HistoryInner::touchResetSpeed() {
	_touchSpeed = QPoint();
	_touchPrevPosValid = false;
}

void HistoryInner::touchDeaccelerate(int32 elapsed) {
	int32 x = _touchSpeed.x();
	int32 y = _touchSpeed.y();
	_touchSpeed.setX((x == 0) ? x : (x > 0) ? qMax(0, x - elapsed) : qMin(0, x + elapsed));
	_touchSpeed.setY((y == 0) ? y : (y > 0) ? qMax(0, y - elapsed) : qMin(0, y + elapsed));
}

void HistoryInner::touchEvent(QTouchEvent *e) {
	if (e->type() == QEvent::TouchCancel) { // cancel
		if (!_touchInProgress) {
			return;
		}
		_touchInProgress = false;
		_touchSelectTimer.cancel();
		_touchScroll = _touchSelect = false;
		_horizontalScrollLocked = false;
		_touchScrollState = Ui::TouchScrollState::Manual;
		_touchMaybeSelecting = false;
		mouseActionCancel();
		return;
	}

	if (!e->touchPoints().isEmpty()) {
		_touchPrevPos = _touchPos;
		_touchPos = e->touchPoints().cbegin()->screenPos().toPoint();
	}

	switch (e->type()) {
	case QEvent::TouchBegin: {
		if (_menu) {
			e->accept();
			return; // ignore mouse press, that was hiding context menu
		}
		if (_touchInProgress || e->touchPoints().isEmpty()) {
			return;
		}

		_touchInProgress = true;
		_horizontalScrollLocked = false;
		if (_touchScrollState == Ui::TouchScrollState::Auto) {
			_touchMaybeSelecting = false;
			_touchScrollState = Ui::TouchScrollState::Acceleration;
			_touchWaitingAcceleration = true;
			_touchAccelerationTime = crl::now();
			touchUpdateSpeed();
			_touchStart = _touchPos;
		} else {
			_touchScroll = false;
			_touchMaybeSelecting = true;
			_touchSelectTimer.callOnce(QApplication::startDragTime());
		}
		_touchSelect = false;
		_touchStart = _touchPrevPos = _touchPos;
	} break;

	case QEvent::TouchUpdate: {
		if (!_touchInProgress) {
			return;
		} else if (_touchSelect) {
			mouseActionUpdate(_touchPos);
		} else if (!_touchScroll && (_touchPos - _touchStart).manhattanLength() >= QApplication::startDragDistance()) {
			_touchSelectTimer.cancel();
			_touchMaybeSelecting = false;
			_touchScroll = true;
			touchUpdateSpeed();
		}
		if (_touchScroll) {
			if (_touchScrollState == Ui::TouchScrollState::Manual) {
				touchScrollUpdated(_touchPos);
			} else if (_touchScrollState == Ui::TouchScrollState::Acceleration) {
				touchUpdateSpeed();
				_touchAccelerationTime = crl::now();
				if (_touchSpeed.isNull()) {
					_touchScrollState = Ui::TouchScrollState::Manual;
				}
			}
		}
	} break;

	case QEvent::TouchEnd: {
		if (!_touchInProgress) {
			return;
		}
		_touchInProgress = false;
		const auto notMoved = (_touchPos - _touchStart).manhattanLength()
			< QApplication::startDragDistance();
		auto weak = Ui::MakeWeak(this);
		if (_touchSelect) {
			if (notMoved || _touchMaybeSelecting.current()) {
				mouseActionFinish(_touchPos, Qt::RightButton);
				auto contextMenu = QContextMenuEvent(
					QContextMenuEvent::Mouse,
					mapFromGlobal(_touchPos),
					_touchPos);
				showContextMenu(&contextMenu, true);
			}
			_touchScroll = false;
		} else if (_touchScroll) {
			if (_touchScrollState == Ui::TouchScrollState::Manual) {
				_touchScrollState = Ui::TouchScrollState::Auto;
				_touchPrevPosValid = false;
				_touchScrollTimer.callEach(15);
				_touchTime = crl::now();
			} else if (_touchScrollState == Ui::TouchScrollState::Auto) {
				_touchScrollState = Ui::TouchScrollState::Manual;
				_horizontalScrollLocked = false;
				_touchScroll = false;
				touchResetSpeed();
			} else if (_touchScrollState == Ui::TouchScrollState::Acceleration) {
				_touchScrollState = Ui::TouchScrollState::Auto;
				_touchWaitingAcceleration = false;
				_touchPrevPosValid = false;
			}
		} else if (notMoved) { // One short tap is like left mouse click.
			mouseActionStart(_touchPos, Qt::LeftButton);
			mouseActionFinish(_touchPos, Qt::LeftButton);
		}
		if (weak) {
			_touchSelectTimer.cancel();
			_touchMaybeSelecting = false;
			_touchSelect = false;
		}
	} break;
	}
}

void HistoryInner::mouseMoveEvent(QMouseEvent *e) {
	static auto lastGlobalPosition = e->globalPos();
	auto reallyMoved = (lastGlobalPosition != e->globalPos());
	auto buttonsPressed = (e->buttons() & (Qt::LeftButton | Qt::MiddleButton));
	if (!buttonsPressed && _mouseAction != MouseAction::None) {
		mouseReleaseEvent(e);
	}
	if (reallyMoved) {
		lastGlobalPosition = e->globalPos();
		if (!buttonsPressed || (_scrollDateLink && ClickHandler::getPressed() == _scrollDateLink)) {
			keepScrollDateForNow();
		}
	}
	mouseActionUpdate(e->globalPos());
}

void HistoryInner::mouseActionUpdate(const QPoint &screenPos) {
	_mousePosition = screenPos;
	mouseActionUpdate();
}

void HistoryInner::touchScrollUpdated(const QPoint &screenPos) {
	_touchPos = screenPos;
	if (consumeScrollAction(_touchPos - _touchPrevPos)) {
		_horizontalScrollLocked = true;
	} else if (!_horizontalScrollLocked) {
		_widget->touchScroll(_touchPos - _touchPrevPos);
	}
	touchUpdateSpeed();
}

QPoint HistoryInner::mapPointToItem(QPoint p, const Element *view) const {
	if (view) {
		const auto top = itemTop(view);
		p.setY(p.y() - top);
		return p;
	}
	return QPoint();
}

QPoint HistoryInner::mapPointToItem(
		QPoint p,
		const HistoryItem *item) const {
	if (const auto view = viewByItem(item)) {
		return mapPointToItem(p, view);
	}
	return QPoint();
}

void HistoryInner::mousePressEvent(QMouseEvent *e) {
	if (_menu) {
		e->accept();
		return; // ignore mouse press, that was hiding context menu
	}
	mouseActionStart(e->globalPos(), e->button());
}

void HistoryInner::mouseActionStart(const QPoint &screenPos, Qt::MouseButton button) {
	mouseActionUpdate(screenPos);
	if (button != Qt::LeftButton) return;

	ClickHandler::pressed();
	if (Element::Pressed() != Element::Hovered()) {
		repaintItem(Element::Pressed());
		Element::Pressed(Element::Hovered());
		repaintItem(Element::Pressed());
	}

	const auto mouseActionView = Element::Moused();
	_mouseAction = MouseAction::None;
	_mouseActionItem = mouseActionView
		? mouseActionView->data().get()
		: nullptr;
	_dragStartPosition = mapPointToItem(mapFromGlobal(screenPos), mouseActionView);
	_pressWasInactive = Ui::WasInactivePress(_controller->widget());
	if (_pressWasInactive) {
		Ui::MarkInactivePress(_controller->widget(), false);
	}

	const auto pressed = ClickHandler::getPressed();
	if (pressed
		&& (!Element::Hovered()
			|| !Element::Hovered()->allowTextSelectionByHandler(pressed))) {
		_mouseAction = MouseAction::PrepareDrag;
	} else if (inSelectionMode().inSelectionMode) {
		if (_dragStateItem
			&& _selected.find(_dragStateItem) != _selected.cend()
			&& Element::Hovered()) {
			_mouseAction = MouseAction::PrepareDrag; // start items drag
		} else if (!_pressWasInactive) {
			_mouseAction = MouseAction::PrepareSelect; // start items select
		}
	}
	if (_mouseAction == MouseAction::None && mouseActionView) {
		TextState dragState;
		if (_trippleClickTimer.isActive() && (screenPos - _trippleClickPoint).manhattanLength() < QApplication::startDragDistance()) {
			StateRequest request;
			request.flags = Ui::Text::StateRequest::Flag::LookupSymbol;
			dragState = mouseActionView->textState(_dragStartPosition, request);
			if (dragState.cursor == CursorState::Text) {
				TextSelection selStatus = { dragState.symbol, dragState.symbol };
				if (selStatus != FullSelection && (_selected.empty() || _selected.cbegin()->second != FullSelection)) {
					if (!_selected.empty()) {
						repaintItem(_selected.cbegin()->first);
						_selected.clear();
					}
					_selected.emplace(_mouseActionItem, selStatus);
					_mouseTextSymbol = dragState.symbol;
					_mouseAction = MouseAction::Selecting;
					_mouseSelectType = TextSelectType::Paragraphs;
					mouseActionUpdate(_mousePosition);
					_trippleClickTimer.callOnce(
						QApplication::doubleClickInterval());
				}
			}
		} else if (Element::Pressed()) {
			StateRequest request;
			request.flags = Ui::Text::StateRequest::Flag::LookupSymbol;
			dragState = mouseActionView->textState(_dragStartPosition, request);
		}
		if (_mouseSelectType != TextSelectType::Paragraphs) {
			if (Element::Pressed()) {
				_mouseTextSymbol = dragState.symbol;
				bool uponSelected = (dragState.cursor == CursorState::Text);
				if (uponSelected) {
					if (_selected.empty()
						|| _selected.cbegin()->second == FullSelection
						|| _selected.cbegin()->first != _mouseActionItem) {
						uponSelected = false;
					} else {
						uint16 selFrom = _selected.cbegin()->second.from, selTo = _selected.cbegin()->second.to;
						if (_mouseTextSymbol < selFrom || _mouseTextSymbol >= selTo) {
							uponSelected = false;
						}
					}
				}
				if (uponSelected) {
					_mouseAction = MouseAction::PrepareDrag; // start text drag
				} else if (!_pressWasInactive) {
					if (_mouseCursorState == CursorState::Date) {
						_mouseAction = MouseAction::PrepareDrag; // start sticker drag or by-date drag
					} else {
						if (dragState.afterSymbol) ++_mouseTextSymbol;
						TextSelection selStatus = { _mouseTextSymbol, _mouseTextSymbol };
						if (selStatus != FullSelection && (_selected.empty() || _selected.cbegin()->second != FullSelection)) {
							if (!_selected.empty()) {
								repaintItem(_selected.cbegin()->first);
								_selected.clear();
							}
							_selected.emplace(_mouseActionItem, selStatus);
							_mouseAction = MouseAction::Selecting;
							repaintItem(_mouseActionItem);
						} else if (!hasSelectRestriction()) {
							_mouseAction = MouseAction::PrepareSelect;
						}
					}
				}
			} else if (!_pressWasInactive && !hasSelectRestriction()) {
				_mouseAction = MouseAction::PrepareSelect; // start items select
			}
		}
	}

	if (!_mouseActionItem) {
		_mouseAction = MouseAction::None;
	} else if (_mouseAction == MouseAction::None) {
		_mouseActionItem = nullptr;
	}
}

void HistoryInner::mouseActionCancel() {
	_mouseActionItem = nullptr;
	_dragStateItem = nullptr;
	_mouseAction = MouseAction::None;
	_dragStartPosition = QPoint(0, 0);
	_dragSelFrom = _dragSelTo = nullptr;
	_wasSelectedText = false;
	_selectScroll.cancel();
}

std::unique_ptr<QMimeData> HistoryInner::prepareDrag() {
	if (_mouseAction != MouseAction::Dragging) {
		return nullptr;
	}

	const auto pressedHandler = ClickHandler::getPressed();
	if (dynamic_cast<VoiceSeekClickHandler*>(pressedHandler.get())
		|| hasCopyRestriction()) {
		return nullptr;
	}

	const auto pressedView = viewByItem(_mouseActionItem);
	bool uponSelected = false;
	if (pressedView) {
		if (!_selected.empty() && _selected.cbegin()->second == FullSelection) {
			uponSelected = _mouseActionItem
				&& (_selected.find(_mouseActionItem) != _selected.cend());
		} else {
			StateRequest request;
			request.flags |= Ui::Text::StateRequest::Flag::LookupSymbol;
			auto dragState = pressedView->textState(_dragStartPosition, request);
			uponSelected = (dragState.cursor == CursorState::Text);
			if (uponSelected) {
				if (_selected.empty()
					|| _selected.cbegin()->second == FullSelection
					|| _selected.cbegin()->first != _mouseActionItem) {
					uponSelected = false;
				} else {
					uint16 selFrom = _selected.cbegin()->second.from, selTo = _selected.cbegin()->second.to;
					if (dragState.symbol < selFrom || dragState.symbol >= selTo) {
						uponSelected = false;
					}
				}
			}
		}
	}
	auto urls = QList<QUrl>();
	const auto selectedText = [&] {
		if (uponSelected) {
			return getSelectedText();
		} else if (pressedHandler) {
			//if (!sel.isEmpty() && sel.at(0) != '/' && sel.at(0) != '@' && sel.at(0) != '#') {
			//	urls.push_back(QUrl::fromEncoded(sel.toUtf8())); // Google Chrome crashes in Mac OS X O_o
			//}
			return TextForMimeData::Simple(pressedHandler->dragText());
		}
		return TextForMimeData();
	}();
	if (auto mimeData = TextUtilities::MimeDataFromText(selectedText)) {
		updateDragSelection(nullptr, nullptr, false);
		_selectScroll.cancel();

		if (!urls.isEmpty()) {
			mimeData->setUrls(urls);
		}
		if (uponSelected && !_controller->adaptive().isOneColumn()) {
			auto selectedState = getSelectionState();
			if (selectedState.count > 0 && selectedState.count == selectedState.canForwardCount) {
				session().data().setMimeForwardIds(getSelectedItems());
				mimeData->setData(u"application/x-td-forward"_q, "1");
			}
		}
		return mimeData;
	} else if (pressedView) {
		auto forwardIds = MessageIdsList();
		const auto tryForwardSelection = uponSelected
			&& !_controller->adaptive().isOneColumn();
		const auto forwardSelectionState = tryForwardSelection
			? getSelectionState()
			: HistoryView::TopBarWidget::SelectedState();
		if (forwardSelectionState.count > 0
			&& (forwardSelectionState.count
				== forwardSelectionState.canForwardCount)) {
			forwardIds = getSelectedItems();
		} else if (_mouseCursorState == CursorState::Date) {
			const auto item = _mouseActionItem;
			if (item && item->allowsForward()) {
				forwardIds = session().data().itemOrItsGroup(item);
			}
		} else if ((pressedView->isHiddenByGroup() && pressedHandler)
			|| (pressedView->media()
				&& pressedView->media()->dragItemByHandler(pressedHandler))) {
			const auto item = _dragStateItem
				? _dragStateItem
				: _mouseActionItem;
			if (item && item->allowsForward()) {
				forwardIds = MessageIdsList(1, item->fullId());
			}
		}

		if (pressedHandler) {
			const auto lnkDocument = reinterpret_cast<DocumentData*>(
				pressedHandler->property(
					kDocumentLinkMediaProperty).toULongLong());
			if (lnkDocument) {
				const auto filepath = lnkDocument->filepath(true);
				if (!filepath.isEmpty()) {
					urls.push_back(QUrl::fromLocalFile(filepath));
				}
			}
		}

		if (forwardIds.empty() && urls.isEmpty()) {
			return nullptr;
		}

		auto result = std::make_unique<QMimeData>();
		if (!forwardIds.empty()) {
			session().data().setMimeForwardIds(std::move(forwardIds));
			result->setData(u"application/x-td-forward"_q, "1");
		}
		if (!urls.isEmpty()) {
			result->setUrls(urls);
		}
		return result;
	}
	return nullptr;
}

void HistoryInner::performDrag() {
	if (auto mimeData = prepareDrag()) {
		// This call enters event loop and can destroy any QObject.
		_reactionsManager->updateButton({});
		_controller->widget()->launchDrag(
			std::move(mimeData),
			crl::guard(this, [=] { mouseActionUpdate(QCursor::pos()); }));
	}
}

void HistoryInner::itemRemoved(not_null<const HistoryItem*> item) {
	if (_pinnedItem == item) {
		_pinnedItem = nullptr;
	}
	if (_history != item->history() && _migrated != item->history()) {
		return;
	}
	if (_reactionsItem.current() == item) {
		_reactionsItem = nullptr;
	}
	_animatedStickersPlayed.remove(item);
	_reactionsManager->remove(item->fullId());

	auto i = _selected.find(item);
	if (i != _selected.cend()) {
		_selected.erase(i);
		_widget->updateTopBarSelection();
	}

	if (_mouseActionItem == item) {
		mouseActionCancel();
	}
	if (_dragStateItem == item) {
		_dragStateItem = nullptr;
	}

	if ((_dragSelFrom && _dragSelFrom->data() == item)
		|| (_dragSelTo && _dragSelTo->data() == item)) {
		_dragSelFrom = nullptr;
		_dragSelTo = nullptr;
		update();
	}
	if (_scrollDateLastItem && _scrollDateLastItem->data() == item) {
		_scrollDateLastItem = nullptr;
	}
	mouseActionUpdate();
}

void HistoryInner::viewRemoved(not_null<const Element*> view) {
	const auto refresh = [&](auto &saved) {
		if (saved == view) {
			const auto now = viewByItem(view->data());
			saved = (now && now != view) ? now : nullptr;
		}
	};
	refresh(_dragSelFrom);
	refresh(_dragSelTo);
	refresh(_scrollDateLastItem);
}

void HistoryInner::mouseActionFinish(
		const QPoint &screenPos,
		Qt::MouseButton button) {
	mouseActionUpdate(screenPos);

	auto activated = ClickHandler::unpressed();
	if (_mouseAction == MouseAction::Dragging) {
		activated = nullptr;
	} else if (_mouseActionItem) {
		// if we are in selecting items mode perhaps we want to
		// toggle selection instead of activating the pressed link
		if (_mouseAction == MouseAction::PrepareDrag
			&& !_pressWasInactive
			&& inSelectionMode().inSelectionMode
			&& button != Qt::RightButton) {
			if (const auto view = viewByItem(_mouseActionItem)) {
				if (view->toggleSelectionByHandlerClick(activated)) {
					activated = nullptr;
				}
			}
		}
	}
	const auto pressedItemView = Element::Pressed();
	if (pressedItemView) {
		repaintItem(pressedItemView);
		Element::Pressed(nullptr);
	}

	_wasSelectedText = false;

	if (activated) {
		const auto pressedItemId = pressedItemView
			? pressedItemView->data()->fullId()
			: _mouseActionItem
			? _mouseActionItem->fullId()
			: FullMsgId();
		const auto weak = base::make_weak(_controller);
		mouseActionCancel();
		ActivateClickHandler(
			window(),
			activated,
			prepareClickContext(button, pressedItemId));
		return;
	}
	if ((_mouseAction == MouseAction::PrepareSelect)
		&& !_pressWasInactive
		&& inSelectionMode().inSelectionMode) {
		changeSelectionAsGroup(
			&_selected,
			_mouseActionItem,
			SelectAction::Invert);
		repaintItem(_mouseActionItem);
	} else if ((_mouseAction == MouseAction::PrepareDrag)
		&& !_pressWasInactive
		&& _dragStateItem
		&& (button != Qt::RightButton)) {
		auto i = _selected.find(_dragStateItem);
		if (i != _selected.cend() && i->second == FullSelection) {
			_selected.erase(i);
			repaintItem(_mouseActionItem);
		} else if ((i == _selected.cend())
			&& !_dragStateItem->isService()
			&& _dragStateItem->isRegular()
			&& inSelectionMode().inSelectionMode) {
			if (_selected.size() < MaxSelectedItems) {
				_selected.emplace(_dragStateItem, FullSelection);
				repaintItem(_mouseActionItem);
			}
		} else {
			_selected.clear();
			update();
		}
	} else if (_mouseAction == MouseAction::Selecting) {
		if (_dragSelFrom && _dragSelTo) {
			applyDragSelection();
			_dragSelFrom = _dragSelTo = nullptr;
		} else if (!_selected.empty() && !_pressWasInactive) {
			auto sel = _selected.cbegin()->second;
			if (sel != FullSelection && sel.from == sel.to) {
				_selected.clear();
				_controller->widget()->setInnerFocus();
			}
		}
	}
	_mouseAction = MouseAction::None;
	_mouseActionItem = nullptr;
	_mouseSelectType = TextSelectType::Letters;
	_selectScroll.cancel();
	_widget->updateTopBarSelection();

	if (QGuiApplication::clipboard()->supportsSelection()
		&& !_selected.empty()
		&& _selected.cbegin()->second != FullSelection
		&& !hasCopyRestriction(_selected.cbegin()->first)) {
		const auto &[item, selection] = *_selected.cbegin();
		if (const auto view = viewByItem(item)) {
			TextUtilities::SetClipboardText(
				view->selectedText(selection),
				QClipboard::Selection);
		}
	}
}

void HistoryInner::mouseReleaseEvent(QMouseEvent *e) {
	mouseActionFinish(e->globalPos(), e->button());
	if (!rect().contains(e->pos())) {
		leaveEvent(e);
	}
}

void HistoryInner::mouseDoubleClickEvent(QMouseEvent *e) {
	mouseActionStart(e->globalPos(), e->button());

	const auto mouseActionView = viewByItem(_mouseActionItem);
	if (_mouseSelectType == TextSelectType::Letters
		&& mouseActionView
		&& ((_mouseAction == MouseAction::Selecting
			&& !_selected.empty()
			&& _selected.cbegin()->second != FullSelection)
			|| (_mouseAction == MouseAction::None
				&& (_selected.empty()
					|| _selected.cbegin()->second != FullSelection)))) {
		StateRequest request;
		request.flags |= Ui::Text::StateRequest::Flag::LookupSymbol;
		auto dragState = mouseActionView->textState(_dragStartPosition, request);
		if (dragState.cursor == CursorState::Text) {
			_mouseTextSymbol = dragState.symbol;
			_mouseSelectType = TextSelectType::Words;
			if (_mouseAction == MouseAction::None) {
				_mouseAction = MouseAction::Selecting;
				TextSelection selStatus = { dragState.symbol, dragState.symbol };
				if (!_selected.empty()) {
					repaintItem(_selected.cbegin()->first);
					_selected.clear();
				}
				_selected.emplace(_mouseActionItem, selStatus);
			}
			mouseMoveEvent(e);

			_trippleClickPoint = e->globalPos();
			_trippleClickTimer.callOnce(QApplication::doubleClickInterval());
		}
	}
	if (!ClickHandler::getActive()
		&& !ClickHandler::getPressed()
		&& (_mouseCursorState == CursorState::None
			|| _mouseCursorState == CursorState::Date)
		&& !inSelectionMode().inSelectionMode
		&& !_emptyPainter
		&& e->button() == Qt::LeftButton) {
		if (const auto view = Element::Moused()) {
			mouseActionCancel();
			switch (HistoryView::CurrentQuickAction()) {
			case HistoryView::DoubleClickQuickAction::Reply: {
				_widget->replyToMessage(view->data());
			} break;
			case HistoryView::DoubleClickQuickAction::React: {
				toggleFavoriteReaction(view);
			} break;
			default: break;
			}
		}
	}
}

void HistoryInner::toggleFavoriteReaction(not_null<Element*> view) const {
	const auto item = view->data();
	const auto favorite = session().data().reactions().favoriteId();
	if (!ranges::contains(
			Data::LookupPossibleReactions(item).recent,
			favorite,
			&Data::Reaction::id)
		|| Window::ShowReactPremiumError(_controller, item, favorite)) {
		return;
	} else if (!ranges::contains(item->chosenReactions(), favorite)) {
		if (const auto top = itemTop(view); top >= 0) {
			view->animateReaction({ .id = favorite });
		}
	}
	item->toggleReaction(favorite, HistoryReactionSource::Quick);
}

HistoryView::SelectedQuote HistoryInner::selectedQuote(
		not_null<HistoryItem*> item) const {
	if (_selected.size() != 1
		|| _selected.begin()->first != item
		|| _selected.begin()->second == FullSelection) {
		return {};
	}
	const auto view = item->mainView();
	if (!view) {
		return {};
	}
	return view->selectedQuote(_selected.begin()->second);
}

void HistoryInner::contextMenuEvent(QContextMenuEvent *e) {
	showContextMenu(e);
}

void HistoryInner::showContextMenu(QContextMenuEvent *e, bool showFromTouch) {
	if (e->reason() == QContextMenuEvent::Mouse) {
		mouseActionUpdate(e->globalPos());
	}

	const auto link = ClickHandler::getActive();
	if (link
		&& !link->property(
			kSendReactionEmojiProperty).value<Data::ReactionId>().empty()
		&& _reactionsManager->showContextMenu(
			this,
			e,
			session().data().reactions().favoriteId())) {
		return;
	}
	auto selectedState = getSelectionState();

	// -2 - has full selected items, but not over, -1 - has selection, but no over, 0 - no selection, 1 - over text, 2 - over full selected items
	auto isUponSelected = 0;
	auto hasSelected = 0;
	if (!_selected.empty()) {
		isUponSelected = -1;
		if (_selected.cbegin()->second == FullSelection) {
			hasSelected = 2;
			if (_dragStateItem && _selected.find(_dragStateItem) != _selected.cend()) {
				isUponSelected = 2;
			} else {
				isUponSelected = -2;
			}
		} else if (Element::Moused()
			&& Element::Moused() == Element::Hovered()
			&& _selected.cbegin()->first == Element::Moused()->data()) {
			uint16 selFrom = _selected.cbegin()->second.from, selTo = _selected.cbegin()->second.to;
			hasSelected = (selTo > selFrom) ? 1 : 0;
			auto mousePos = mapPointToItem(mapFromGlobal(_mousePosition), Element::Moused());
			StateRequest request;
			request.flags |= Ui::Text::StateRequest::Flag::LookupSymbol;
			auto dragState = Element::Moused()->textState(mousePos, request);
			if (dragState.cursor == CursorState::Text
				&& dragState.symbol >= selFrom
				&& dragState.symbol < selTo) {
				isUponSelected = 1;
			}
		}
	}
	if (showFromTouch && hasSelected && isUponSelected < hasSelected) {
		isUponSelected = hasSelected;
	}

	const auto groupLeaderOrSelf = [](HistoryItem *item) -> HistoryItem* {
		if (!item) {
			return nullptr;
		} else if (const auto group = item->history()->owner().groups().find(item)) {
			return group->items.front();
		}
		return item;
	};
	const auto whoReactedItem = groupLeaderOrSelf(_dragStateItem);
	const auto hasWhoReactedItem = whoReactedItem
		&& Api::WhoReactedExists(whoReactedItem, Api::WhoReactedList::All);
	const auto clickedReaction = link
		? link->property(
			kReactionsCountEmojiProperty).value<Data::ReactionId>()
		: Data::ReactionId();
	_whoReactedMenuLifetime.destroy();
	if (!clickedReaction.empty()
		&& whoReactedItem
		&& Api::WhoReactedExists(whoReactedItem, Api::WhoReactedList::One)) {
		HistoryView::ShowWhoReactedMenu(
			&_menu,
			e->globalPos(),
			this,
			whoReactedItem,
			clickedReaction,
			_controller,
			_whoReactedMenuLifetime);
		e->accept();
		return;
	}
	_menu = base::make_unique_q<Ui::PopupMenu>(this, st::popupMenuWithIcons);
	const auto session = &this->session();
	const auto controller = _controller;
	const auto addItemActions = [&](
			HistoryItem *item,
			HistoryItem *albumPartItem) {
		if (!item
			|| !item->isRegular()
			|| isUponSelected == 2
			|| isUponSelected == -2) {
			return;
		}
		const auto itemId = item->fullId();
		const auto repliesCount = item->repliesCount();
		const auto withReplies = (repliesCount > 0);
		const auto topicRootId = item->history()->isForum()
			? item->topicRootId()
			: 0;
		if (topicRootId
			|| (withReplies && item->history()->peer->isMegagroup())) {
			const auto highlightId = topicRootId ? item->id : 0;
			const auto rootId = topicRootId
				? topicRootId
				: repliesCount
				? item->id
				: item->replyToTop();
			const auto phrase = topicRootId
				? tr::lng_replies_view_topic(tr::now)
				: (repliesCount > 0)
				? tr::lng_replies_view(
					tr::now,
					lt_count,
					repliesCount)
				: tr::lng_replies_view_thread(tr::now);
			_menu->addAction(phrase, [=] {
				controller->showRepliesForMessage(
					_history,
					rootId,
					highlightId);
			}, &st::menuIconViewReplies);
		}
		const auto t = base::unixtime::now();
		const auto editItem = (albumPartItem && albumPartItem->allowsEdit(t))
			? albumPartItem
			: item->allowsEdit(t)
			? item
			: nullptr;
		if (editItem) {
			const auto editItemId = editItem->fullId();
			_menu->addAction(tr::lng_context_edit_msg(tr::now), [=] {
				if (const auto item = session->data().message(editItemId)) {
					auto it = _selected.find(item);
					const auto selection = ((it != _selected.end())
							&& (it->second != FullSelection))
						? it->second
						: TextSelection();
					if (!selection.empty()) {
						clearSelected(true);
					}
					_widget->editMessage(item, selection);
				}
			}, &st::menuIconEdit);
		}
		if (session->factchecks().canEdit(item)) {
			const auto text = item->factcheckText();
			const auto phrase = text.empty()
				? tr::lng_context_add_factcheck(tr::now)
				: tr::lng_context_edit_factcheck(tr::now);
			_menu->addAction(phrase, [=] {
				const auto limit = session->factchecks().lengthLimit();
				controller->show(Box(EditFactcheckBox, text, limit, [=](
						TextWithEntities result) {
					const auto show = controller->uiShow();
					session->factchecks().save(itemId, text, result, show);
				}, FactcheckFieldIniter(controller->uiShow())));
			}, &st::menuIconFactcheck);
		}
		const auto pinItem = (item->canPin() && item->isPinned())
			? item
			: groupLeaderOrSelf(item);
		if (pinItem->canPin()) {
			const auto isPinned = pinItem->isPinned();
			const auto pinItemId = pinItem->fullId();
			_menu->addAction(isPinned ? tr::lng_context_unpin_msg(tr::now) : tr::lng_context_pin_msg(tr::now), crl::guard(controller, [=] {
				Window::ToggleMessagePinned(controller, pinItemId, !isPinned);
			}), isPinned ? &st::menuIconUnpin : &st::menuIconPin);
		}
		if (!item->isService()
			&& peerIsChannel(itemId.peer)
			&& !_peer->isMegagroup()) {
			constexpr auto kMinViewsCount = 10;
			if (const auto channel = _peer->asChannel()) {
				if ((channel->flags() & ChannelDataFlag::CanGetStatistics)
					|| (channel->canPostMessages()
						&& item->viewsCount() >= kMinViewsCount)) {
					auto callback = crl::guard(controller, [=] {
						controller->showSection(
							Info::Statistics::Make(channel, itemId, {}));
					});
					_menu->addAction(
						tr::lng_stats_title(tr::now),
						std::move(callback),
						&st::menuIconStats);
				}
			}
		}
	};
	const auto addPhotoActions = [&](not_null<PhotoData*> photo, HistoryItem *item) {
		const auto media = photo->activeMediaView();
		const auto itemId = item ? item->fullId() : FullMsgId();
		if (!photo->isNull() && media && media->loaded() && !hasCopyMediaRestriction(item)) {
			_menu->addAction(tr::lng_context_save_image(tr::now), base::fn_delayed(st::defaultDropdownMenu.menu.ripple.hideDuration, this, [=] {
				savePhotoToFile(photo);
			}), &st::menuIconSaveImage);
			_menu->addAction(tr::lng_context_copy_image(tr::now), [=] {
				copyContextImage(photo, itemId);
			}, &st::menuIconCopy);
		}
		if (photo->hasAttachedStickers()) {
			_menu->addAction(tr::lng_context_attached_stickers(tr::now), [=] {
				session->api().attachedStickers().requestAttachedStickerSets(
					controller,
					photo);
			}, &st::menuIconStickers);
		}
	};
	const auto addDocumentActions = [&](not_null<DocumentData*> document, HistoryItem *item) {
		if (document->loading()) {
			_menu->addAction(tr::lng_context_cancel_download(tr::now), [=] {
				cancelContextDownload(document);
			}, &st::menuIconCancel);
			return;
		}
		const auto itemId = item ? item->fullId() : FullMsgId();
		const auto lnkIsVideo = document->isVideoFile();
		const auto lnkIsVoice = document->isVoiceMessage();
		const auto lnkIsAudio = document->isAudioFile();
		if (document->isGifv()) {
			const auto notAutoplayedGif = [&] {
				return item
					&& !Data::AutoDownload::ShouldAutoPlay(
						session->settings().autoDownload(),
						item->history()->peer,
						document);
			}();
			if (notAutoplayedGif) {
				_menu->addAction(tr::lng_context_open_gif(tr::now), [=] {
					openContextGif(itemId);
				}, &st::menuIconShowInChat);
			}
			if (!hasCopyMediaRestriction(item)) {
				_menu->addAction(tr::lng_context_save_gif(tr::now), [=] {
					saveContextGif(itemId);
				}, &st::menuIconGif);
			}
		}
		if (!document->filepath(true).isEmpty()) {
			_menu->addAction(Platform::IsMac() ? tr::lng_context_show_in_finder(tr::now) : tr::lng_context_show_in_folder(tr::now), [=] {
				showContextInFolder(document);
			}, &st::menuIconShowInFolder);
		}
		if (item
			&& !hasCopyMediaRestriction(item)
			&& !HistoryView::ItemHasTtl(item)) {
			HistoryView::AddSaveSoundForNotifications(
				_menu,
				item,
				document,
				controller);
			_menu->addAction(lnkIsVideo ? tr::lng_context_save_video(tr::now) : (lnkIsVoice ? tr::lng_context_save_audio(tr::now) : (lnkIsAudio ? tr::lng_context_save_audio_file(tr::now) : tr::lng_context_save_file(tr::now))), base::fn_delayed(st::defaultDropdownMenu.menu.ripple.hideDuration, this, [=] {
				saveDocumentToFile(itemId, document);
			}), &st::menuIconDownload);

			HistoryView::AddCopyFilename(
				_menu,
				document,
				[=] { return showCopyRestrictionForSelected(); });
		}
		if (document->hasAttachedStickers()) {
			_menu->addAction(tr::lng_context_attached_stickers(tr::now), [=] {
				session->api().attachedStickers().requestAttachedStickerSets(
					controller,
					document);
			}, &st::menuIconStickers);
		}
	};

#ifdef _DEBUG // Sometimes we need to save emoji to files.
	if (const auto item = _dragStateItem) {
		const auto emojiStickers = &session->emojiStickersPack();
		if (const auto view = item->media() ? nullptr : item->mainView()) {
			if (const auto isolated = view->isolatedEmoji()) {
				if (const auto sticker = emojiStickers->stickerForEmoji(
						isolated)) {
					addDocumentActions(sticker.document, item);
				}
			}
		}
	}
#endif

	const auto asGroup = !Element::Moused()
		|| (Element::Moused() != Element::Hovered())
		|| (Element::Moused()->pointState(
			mapPointToItem(
				mapFromGlobal(_mousePosition),
				Element::Moused())
		) != HistoryView::PointState::GroupPart);
	const auto addSelectMessageAction = [&](not_null<HistoryItem*> item) {
		if (item->isRegular()
			&& !item->isService()
			&& !hasSelectRestriction()) {
			const auto itemId = item->fullId();
			_menu->addAction(tr::lng_context_select_msg(tr::now), [=] {
				if (const auto item = session->data().message(itemId)) {
					if (const auto view = viewByItem(item)) {
						if (asGroup) {
							changeSelectionAsGroup(&_selected, item, SelectAction::Select);
						} else {
							changeSelection(&_selected, item, SelectAction::Select);
						}
						repaintItem(item);
						_widget->updateTopBarSelection();
					}
				}
			}, &st::menuIconSelect);
			const auto collectBetween = [=](
					not_null<HistoryItem*> from,
					not_null<HistoryItem*> to,
					int max) -> HistoryItemsList {
				auto current = from;
				auto collected = HistoryItemsList();
				collected.reserve(max);
				collected.push_back(from);
				collected.push_back(to);
				const auto toId = to->fullId();
				while (true) {
					if (collected.size() > max) {
						return {};
					}
					const auto view = viewByItem(current);
					const auto nextView = nextItem(view);
					if (!nextView) {
						return {};
					}
					const auto nextItem = nextView->data();
					if (nextItem->fullId() == toId) {
						return collected;
					}
					if (nextItem->isRegular() && !nextItem->isService()) {
						collected.push_back(nextItem);
					}
					current = nextItem;
				}
			};

			[&] { // Select up to this message.
				if (selectedState.count <= 0) {
					return;
				}
				const auto toItem = groupLeaderOrSelf(item);
				auto topToBottom = false;
				auto nearestItem = (HistoryItem*)(nullptr);
				{
					auto minDiff = std::numeric_limits<int>::max();
					for (const auto &[item, _] : _selected) {
						const auto diff = item->fullId().msg.bare
							- toItem->fullId().msg.bare;
						if (std::abs(diff) < minDiff) {
							nearestItem = item;
							minDiff = std::abs(diff);
							topToBottom = (diff < 0);
						}
					}
				}
				if (!nearestItem) {
					return;
				}
				const auto start = (topToBottom ? nearestItem : toItem);
				const auto end = (topToBottom ? toItem : nearestItem);
				const auto left = MaxSelectedItems
					- selectedState.count
					+ (topToBottom ? 0 : 1);
				if (collectBetween(start, end, left).empty()) {
					return;
				}
				const auto startId = start->fullId();
				const auto endId = end->fullId();
				const auto callback = [=] {
					const auto from = session->data().message(startId);
					const auto to = session->data().message(endId);
					if (from && to) {
						for (const auto &i : collectBetween(from, to, left)) {
							changeSelectionAsGroup(
								&_selected,
								i,
								SelectAction::Select);
						}
						update();
						_widget->updateTopBarSelection();
					}
				};
				_menu->addAction(
					tr::lng_context_select_msg_bulk(tr::now),
					callback,
					&st::menuIconSelect);
			}();
		}
	};

	const auto addReplyAction = [&](HistoryItem *item) {
		if (!item || !item->isRegular()) {
			return;
		}
		const auto canSendReply = CanSendReply(item);
		const auto canReply = canSendReply || item->allowsForward();
		if (canReply) {
			const auto selected = selectedQuote(item);
			auto text = selected
				? tr::lng_context_quote_and_reply(tr::now)
				: tr::lng_context_reply_msg(tr::now);
			const auto replyToItem = selected.item ? selected.item : item;
			const auto itemId = replyToItem->fullId();
			const auto quote = selected.text;
			const auto quoteOffset = selected.offset;
			text.replace('&', u"&&"_q);
			_menu->addAction(text, [=] {
				const auto still = session->data().message(itemId);
				const auto forceAnotherChat = base::IsCtrlPressed()
					&& still
					&& still->allowsForward();
				if (canSendReply && !forceAnotherChat) {
					_widget->replyToMessage({
						.messageId = itemId,
						.quote = quote,
						.quoteOffset = quoteOffset,
					});
					if (!quote.empty()) {
						_widget->clearSelected();
					}
				} else {
					const auto show = controller->uiShow();
					HistoryView::Controls::ShowReplyToChatBox(show, {
						.messageId = itemId,
						.quote = quote,
						.quoteOffset = quoteOffset,
					});
				}
			}, &st::menuIconReply);
		}
	};

	const auto lnkPhoto = link
		? reinterpret_cast<PhotoData*>(
			link->property(kPhotoLinkMediaProperty).toULongLong())
		: nullptr;
	const auto lnkDocument = link
		? reinterpret_cast<DocumentData*>(
			link->property(kDocumentLinkMediaProperty).toULongLong())
		: nullptr;
	if (lnkPhoto || lnkDocument) {
		const auto item = _dragStateItem;
		const auto itemId = item ? item->fullId() : FullMsgId();
		addReplyAction(item);

		if (isUponSelected > 0) {
			const auto selectedText = getSelectedText();
			if (!hasCopyRestrictionForSelected()
				&& !selectedText.empty()) {
				_menu->addAction(
					(isUponSelected > 1
						? tr::lng_context_copy_selected_items(tr::now)
						: tr::lng_context_copy_selected(tr::now)),
					[=] { copySelectedText(); },
					&st::menuIconCopy);
			}
			if (item && !Ui::SkipTranslate(selectedText.rich)) {
				const auto peer = item->history()->peer;
				_menu->addAction(tr::lng_context_translate_selected({}), [=] {
					_controller->show(Box(
						Ui::TranslateBox,
						peer,
						MsgId(),
						getSelectedText().rich,
						hasCopyRestrictionForSelected()));
				}, &st::menuIconTranslate);
			}
		}
		addItemActions(item, item);
		if (!selectedState.count) {
			if (lnkPhoto) {
				addPhotoActions(lnkPhoto, item);
			} else {
				addDocumentActions(lnkDocument, item);
			}
		}
		if (item && item->hasDirectLink() && isUponSelected != 2 && isUponSelected != -2) {
			_menu->addAction(item->history()->peer->isMegagroup() ? tr::lng_context_copy_message_link(tr::now) : tr::lng_context_copy_post_link(tr::now), [=] {
				HistoryView::CopyPostLink(controller, itemId, HistoryView::Context::History);
			}, &st::menuIconLink);
		}
		if (isUponSelected > 1) {
			if (selectedState.count > 0 && selectedState.canForwardCount == selectedState.count) {
				_menu->addAction(tr::lng_context_forward_selected(tr::now), [=] {
					_widget->forwardSelected();
				}, &st::menuIconForward);
			}
			if (selectedState.count > 0 && selectedState.canDeleteCount == selectedState.count) {
				_menu->addAction(tr::lng_context_delete_selected(tr::now), [=] {
					_widget->confirmDeleteSelected();
				}, &st::menuIconDelete);
			}
			if (selectedState.count > 0 && !hasCopyRestrictionForSelected()) {
				Menu::AddDownloadFilesAction(_menu, controller, _selected, this);
			}
			_menu->addAction(tr::lng_context_clear_selection(tr::now), [=] {
				_widget->clearSelected();
			}, &st::menuIconSelect);
		} else if (item) {
			const auto itemId = item->fullId();
			const auto blockSender = item->history()->peer->isRepliesChat();
			if (isUponSelected != -2) {
				if (item->allowsForward()) {
					_menu->addAction(tr::lng_context_forward_msg(tr::now), [=] {
						forwardItem(itemId);
					}, &st::menuIconForward);
				}
				if (item->canDelete()) {
					const auto callback = [=] { deleteItem(itemId); };
					if (item->isUploading()) {
						_menu->addAction(tr::lng_context_cancel_upload(tr::now), callback, &st::menuIconCancel);
					} else {
						_menu->addAction(Ui::DeleteMessageContextAction(
							_menu->menu(),
							callback,
							item->ttlDestroyAt(),
							[=] { _menu = nullptr; }));
					}
				}
				if (!blockSender && item->suggestReport()) {
					_menu->addAction(tr::lng_context_report_msg(tr::now), [=] {
						reportItem(itemId);
					}, &st::menuIconReport);
				}
			}
			addSelectMessageAction(item);
			if (isUponSelected != -2 && blockSender) {
				_menu->addAction(tr::lng_profile_block_user(tr::now), [=] {
					blockSenderItem(itemId);
				}, &st::menuIconBlock);
			}
		}
	} else { // maybe cursor on some text history item?
		const auto albumPartItem = _dragStateItem;
		const auto item = [&] {
			const auto result = Element::Hovered()
				? Element::Hovered()->data().get()
				: Element::HoveredLink()
				? Element::HoveredLink()->data().get()
				: nullptr;
			return result ? groupLeaderOrSelf(result) : nullptr;
		}();
		const auto partItemOrLeader = (asGroup || !albumPartItem)
			? item
			: albumPartItem;
		const auto itemId = item ? item->fullId() : FullMsgId();
		const auto canDelete = item
			&& item->canDelete()
			&& (item->isRegular() || !item->isService());
		const auto canForward = item && item->allowsForward();
		const auto canReport = item && item->suggestReport();
		const auto canBlockSender = item && item->history()->peer->isRepliesChat();
		const auto view = viewByItem(item);
		const auto actionText = link
			? link->copyToClipboardContextItemText()
			: QString();

		if (item && item->isSponsored()) {
			FillSponsoredMessagesMenu(controller, item->fullId(), _menu);
		}
		if (isUponSelected > 0) {
			addReplyAction(item);
			const auto selectedText = getSelectedText();
			if (!hasCopyRestrictionForSelected() && !selectedText.empty()) {
				_menu->addAction(
					((isUponSelected > 1)
						? tr::lng_context_copy_selected_items(tr::now)
						: tr::lng_context_copy_selected(tr::now)),
					[=] { copySelectedText(); },
					&st::menuIconCopy);
			}
			if (item && !Ui::SkipTranslate(selectedText.rich)) {
				const auto peer = item->history()->peer;
				_menu->addAction(tr::lng_context_translate_selected({}), [=] {
					_controller->show(Box(
						Ui::TranslateBox,
						peer,
						MsgId(),
						selectedText.rich,
						hasCopyRestrictionForSelected()));
				}, &st::menuIconTranslate);
			}
			addItemActions(item, item);
		} else {
			addReplyAction(partItemOrLeader);
			addItemActions(item, albumPartItem);
			if (item && !isUponSelected) {
				const auto media = (view ? view->media() : nullptr);
				const auto mediaHasTextForCopy = media && media->hasTextForCopy();
				if (const auto document = media ? media->getDocument() : nullptr) {
					if (!view->isIsolatedEmoji() && document->sticker()) {
						if (document->sticker()->set) {
							_menu->addAction(document->isStickerSetInstalled() ? tr::lng_context_pack_info(tr::now) : tr::lng_context_pack_add(tr::now), [=] {
								showStickerPackInfo(document);
							}, &st::menuIconStickers);
							const auto isFaved = session->data().stickers().isFaved(document);
							_menu->addAction(isFaved ? tr::lng_faved_stickers_remove(tr::now) : tr::lng_faved_stickers_add(tr::now), [=] {
								Api::ToggleFavedSticker(controller->uiShow(), document, itemId);
							}, isFaved ? &st::menuIconUnfave : &st::menuIconFave);
						}
						if (!hasCopyMediaRestriction(item)) {
							_menu->addAction(tr::lng_context_save_image(tr::now), base::fn_delayed(st::defaultDropdownMenu.menu.ripple.hideDuration, this, [=] {
								saveDocumentToFile(itemId, document);
							}), &st::menuIconDownload);
						}
					}
				}
				if (const auto media = item->media()) {
					if (const auto poll = media->poll()) {
						HistoryView::AddPollActions(
							_menu,
							poll,
							item,
							HistoryView::Context::History,
							_controller);
					} else if (const auto contact = media->sharedContact()) {
						const auto phone = contact->phoneNumber;
						_menu->addAction(tr::lng_profile_copy_phone(tr::now), [=] {
							QGuiApplication::clipboard()->setText(phone);
						}, &st::menuIconCopy);
					}
				}
				if (!item->isService() && view && actionText.isEmpty()) {
					if (!hasCopyRestriction(item)
						&& (view->hasVisibleText() || mediaHasTextForCopy)) {
						_menu->addAction(
							tr::lng_context_copy_text(tr::now),
							[=] { copyContextText(itemId); },
							&st::menuIconCopy);
					}
					if ((!item->translation() || !_history->translatedTo())
						&& (view->hasVisibleText() || mediaHasTextForCopy)) {
						const auto translate = mediaHasTextForCopy
							? (HistoryView::TransribedText(item)
								.append('\n')
								.append(item->originalText()))
							: item->originalText();
						if (!translate.text.isEmpty()
							&& !Ui::SkipTranslate(translate)) {
							_menu->addAction(tr::lng_context_translate(tr::now), [=] {
								_controller->show(Box(
									Ui::TranslateBox,
									item->history()->peer,
									mediaHasTextForCopy
										? MsgId()
										: item->fullId().msg,
									translate,
									hasCopyRestriction(item)));
							}, &st::menuIconTranslate);
						}
					}
				}
			}
		}
		if (!actionText.isEmpty()) {
			_menu->addAction(
				actionText,
				[text = link->copyToClipboardText()] {
					QGuiApplication::clipboard()->setText(text);
				},
				&st::menuIconCopy);
		} else if (item && item->hasDirectLink() && isUponSelected != 2 && isUponSelected != -2) {
			_menu->addAction(item->history()->peer->isMegagroup() ? tr::lng_context_copy_message_link(tr::now) : tr::lng_context_copy_post_link(tr::now), [=] {
				HistoryView::CopyPostLink(controller, itemId, HistoryView::Context::History);
			}, &st::menuIconLink);
		}
		if (item && item->isSponsored()) {
			if (!_menu->empty()) {
				_menu->addSeparator(&st::expandedMenuSeparator);
			}
			auto item = base::make_unique_q<Ui::Menu::MultilineAction>(
				_menu,
				st::menuWithIcons,
				st::historyHasCustomEmoji,
				st::historySponsoredAboutMenuLabelPosition,
				TextWithEntities{ tr::lng_sponsored_title(tr::now) },
				&st::menuIconInfo);
			item->clicks(
			) | rpl::start_with_next([=] {
				controller->show(Box(Ui::AboutSponsoredBox));
			}, item->lifetime());
			_menu->addAction(std::move(item));
		}
		if (isUponSelected > 1) {
			if (selectedState.count > 0 && selectedState.count == selectedState.canForwardCount) {
				_menu->addAction(tr::lng_context_forward_selected(tr::now), [=] {
					_widget->forwardSelected();
				}, &st::menuIconForward);
			}
			if (selectedState.count > 0 && selectedState.count == selectedState.canDeleteCount) {
				_menu->addAction(tr::lng_context_delete_selected(tr::now), [=] {
					_widget->confirmDeleteSelected();
				}, &st::menuIconDelete);
			}
			if (selectedState.count > 0 && !hasCopyRestrictionForSelected()) {
				Menu::AddDownloadFilesAction(_menu, controller, _selected, this);
			}
			_menu->addAction(tr::lng_context_clear_selection(tr::now), [=] {
				_widget->clearSelected();
			}, &st::menuIconSelect);
		} else if (item && ((isUponSelected != -2 && (canForward || canDelete)) || item->isRegular())) {
			if (isUponSelected != -2) {
				if (canForward) {
					_menu->addAction(tr::lng_context_forward_msg(tr::now), [=] {
						forwardAsGroup(itemId);
					}, &st::menuIconForward);
				}
				if (canDelete) {
					const auto callback = [=] {
						deleteAsGroup(itemId);
					};
					if (item->isUploading()) {
						_menu->addAction(tr::lng_context_cancel_upload(tr::now), callback, &st::menuIconCancel);
					} else {
						_menu->addAction(Ui::DeleteMessageContextAction(
							_menu->menu(),
							callback,
							item->ttlDestroyAt(),
							[=] { _menu = nullptr; }));
					}
				}
				if (!canBlockSender && canReport) {
					_menu->addAction(tr::lng_context_report_msg(tr::now), [=] {
						reportAsGroup(itemId);
					}, &st::menuIconReport);
				}
			}
			addSelectMessageAction(partItemOrLeader);
			if (isUponSelected != -2 && canBlockSender) {
				_menu->addAction(tr::lng_profile_block_user(tr::now), [=] {
					blockSenderAsGroup(itemId);
				}, &st::menuIconBlock);
			}
		} else if (Element::Moused()) {
			addSelectMessageAction(Element::Moused()->data());
		}
	}

	if (_dragStateItem) {
		const auto view = viewByItem(_dragStateItem);
		const auto textItem = view ? view->textItem() : _dragStateItem;
		const auto wasAmount = _menu->actions().size();
		HistoryView::AddEmojiPacksAction(
			_menu,
			textItem ? textItem : _dragStateItem,
			HistoryView::EmojiPacksSource::Message,
			_controller);
		const auto added = (_menu->actions().size() > wasAmount);
		if (!added) {
			_menu->addSeparator();
		}
		HistoryView::AddSelectRestrictionAction(
			_menu,
			textItem ? textItem : _dragStateItem,
			!added);
	}
	if (hasWhoReactedItem) {
		HistoryView::AddWhoReactedAction(
			_menu,
			this,
			whoReactedItem,
			_controller);
	}

	if (_menu->empty()) {
		_menu = nullptr;
		return;
	}
	using namespace HistoryView::Reactions;
	const auto desiredPosition = e->globalPos();
	const auto reactItem = Element::Hovered()
		? Element::Hovered()->data().get()
		: nullptr;
	const auto attached = reactItem
		? AttachSelectorToMenu(
			_menu.get(),
			controller,
			desiredPosition,
			reactItem,
			[=](ChosenReaction reaction) { reactionChosen(reaction); },
			ItemReactionsAbout(reactItem))
		: AttachSelectorResult::Skipped;
	if (attached == AttachSelectorResult::Failed) {
		_menu = nullptr;
		return;
	} else if (attached == AttachSelectorResult::Attached) {
		_menu->popupPrepared();
	} else {
		_menu->popup(desiredPosition);
	}
	e->accept();
}

bool HistoryInner::hasCopyRestriction(HistoryItem *item) const {
	return !_peer->allowsForwarding() || (item && item->forbidsForward());
}

bool HistoryInner::hasCopyMediaRestriction(
		not_null<HistoryItem*> item) const {
	return hasCopyRestriction(item) || item->forbidsSaving();
}

bool HistoryInner::showCopyRestriction(HistoryItem *item) {
	if (!hasCopyRestriction(item)) {
		return false;
	}
	_controller->showToast(_peer->isBroadcast()
		? tr::lng_error_nocopy_channel(tr::now)
		: tr::lng_error_nocopy_group(tr::now));
	return true;
}

bool HistoryInner::showCopyMediaRestriction(not_null<HistoryItem*> item) {
	if (!hasCopyMediaRestriction(item)) {
		return false;
	}
	_controller->showToast(_peer->isBroadcast()
		? tr::lng_error_nocopy_channel(tr::now)
		: tr::lng_error_nocopy_group(tr::now));
	return true;
}

bool HistoryInner::hasCopyRestrictionForSelected() const {
	if (hasCopyRestriction()) {
		return true;
	}
	for (const auto &[item, selection] : _selected) {
		if (item && item->forbidsForward()) {
			return true;
		}
	}
	return false;
}

bool HistoryInner::showCopyRestrictionForSelected() {
	for (const auto &[item, selection] : _selected) {
		if (showCopyRestriction(item)) {
			return true;
		}
	}
	return false;
}

void HistoryInner::copySelectedText() {
	if (!showCopyRestrictionForSelected()) {
		TextUtilities::SetClipboardText(getSelectedText());
	}
}

void HistoryInner::savePhotoToFile(not_null<PhotoData*> photo) {
	const auto media = photo->activeMediaView();
	if (photo->isNull() || !media || !media->loaded()) {
		return;
	}

	auto filter = u"JPEG Image (*.jpg);;"_q + FileDialog::AllFilesFilter();
	FileDialog::GetWritePath(
		this,
		tr::lng_save_photo(tr::now),
		filter,
		filedialogDefaultName(u"photo"_q, u".jpg"_q),
		crl::guard(this, [=](const QString &result) {
			if (!result.isEmpty()) {
				media->saveToFile(result);
			}
		}));
}

void HistoryInner::copyContextImage(
		not_null<PhotoData*> photo,
		FullMsgId itemId) {
	const auto item = session().data().message(itemId);
	const auto media = photo->activeMediaView();
	const auto restricted = item
		? showCopyMediaRestriction(item)
		: IsServerMsgId(itemId.msg);
	if (photo->isNull() || !media || !media->loaded()) {
		return;
	} else if (!restricted) {
		media->setToClipboard();
	}
}

void HistoryInner::showStickerPackInfo(not_null<DocumentData*> document) {
	StickerSetBox::Show(_controller->uiShow(), document);
}

void HistoryInner::cancelContextDownload(not_null<DocumentData*> document) {
	document->cancel();
}

void HistoryInner::showContextInFolder(not_null<DocumentData*> document) {
	const auto filepath = document->filepath(true);
	if (!filepath.isEmpty()) {
		File::ShowInFolder(filepath);
	}
}

void HistoryInner::saveDocumentToFile(
		FullMsgId contextId,
		not_null<DocumentData*> document) {
	DocumentSaveClickHandler::SaveAndTrack(
		contextId,
		document,
		DocumentSaveClickHandler::Mode::ToNewFile);
}

void HistoryInner::openContextGif(FullMsgId itemId) {
	if (const auto item = session().data().message(itemId)) {
		if (const auto media = item->media()) {
			if (const auto document = media->document()) {
				_controller->openDocument(document, true, { itemId });
			}
		}
	}
}

void HistoryInner::saveContextGif(FullMsgId itemId) {
	if (const auto item = session().data().message(itemId)) {
		if (!hasCopyMediaRestriction(item)) {
			if (const auto media = item->media()) {
				if (const auto document = media->document()) {
					Api::ToggleSavedGif(
						_controller->uiShow(),
						document,
						item->fullId(),
						true);
				}
			}
		}
	}
}

void HistoryInner::copyContextText(FullMsgId itemId) {
	if (const auto item = session().data().message(itemId)) {
		if (!showCopyRestriction(item)) {
			if (const auto group = session().data().groups().find(item)) {
				TextUtilities::SetClipboardText(HistoryGroupText(group));
			} else {
				TextUtilities::SetClipboardText(HistoryItemText(item));
			}
		}
	}
}

void HistoryInner::resizeEvent(QResizeEvent *e) {
	mouseActionUpdate();
}

TextForMimeData HistoryInner::getSelectedText() const {
	auto selected = _selected;

	if (_mouseAction == MouseAction::Selecting && _dragSelFrom && _dragSelTo) {
		applyDragSelection(&selected);
	}

	if (selected.empty()) {
		return TextForMimeData();
	}
	if (selected.cbegin()->second != FullSelection) {
		const auto &[item, selection] = *selected.cbegin();
		if (const auto view = viewByItem(item)) {
			return view->selectedText(selection);
		}
		return TextForMimeData();
	}

	struct Part {
		QString name;
		QString time;
		TextForMimeData unwrapped;
	};

	auto groups = base::flat_set<not_null<const Data::Group*>>();
	auto fullSize = 0;
	auto texts = base::flat_map<Data::MessagePosition, Part>();

	const auto wrapItem = [&](
			not_null<HistoryItem*> item,
			TextForMimeData &&unwrapped) {
		const auto i = texts.emplace(item->position(), Part{
			.name = item->author()->name(),
			.time = QString(", [%1]\n").arg(
				QLocale().toString(ItemDateTime(item), QLocale::ShortFormat)),
			.unwrapped = std::move(unwrapped),
		}).first;
		fullSize += i->second.name.size()
			+ i->second.time.size()
			+ i->second.unwrapped.expanded.size();
	};
	const auto addItem = [&](not_null<HistoryItem*> item) {
		wrapItem(item, HistoryItemText(item));
	};
	const auto addGroup = [&](not_null<const Data::Group*> group) {
		Expects(!group->items.empty());

		wrapItem(group->items.back(), HistoryGroupText(group));
	};

	for (const auto &[item, selection] : selected) {
		if (const auto group = session().data().groups().find(item)) {
			if (groups.contains(group)) {
				continue;
			}
			if (isSelectedGroup(&selected, group)) {
				groups.emplace(group);
				addGroup(group);
			} else {
				addItem(item);
			}
		} else {
			addItem(item);
		}
	}
	if (texts.size() == 1) {
		return texts.front().second.unwrapped;
	}
	auto result = TextForMimeData();
	const auto sep = u"\n\n"_q;
	result.reserve(fullSize + (texts.size() - 1) * sep.size());
	for (auto i = texts.begin(), e = texts.end(); i != e;) {
		result.append(i->second.name).append(i->second.time);
		result.append(std::move(i->second.unwrapped));
		if (++i != e) {
			result.append(sep);
		}
	}
	return result;
}

void HistoryInner::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Escape) {
		_widget->escape();
	} else if (e == QKeySequence::Copy && !_selected.empty()) {
		copySelectedText();
#ifdef Q_OS_MAC
	} else if (e->key() == Qt::Key_E
		&& e->modifiers().testFlag(Qt::ControlModifier)
		&& !showCopyRestrictionForSelected()) {
		TextUtilities::SetClipboardText(getSelectedText(), QClipboard::FindBuffer);
#endif // Q_OS_MAC
	} else if (e == QKeySequence::Delete || e->key() == Qt::Key_Backspace) {
		auto selectedState = getSelectionState();
		if (selectedState.count > 0
			&& selectedState.canDeleteCount == selectedState.count) {
			_widget->confirmDeleteSelected();
		}
	} else if (!(e->modifiers() & ~Qt::ShiftModifier)
		&& e->key() != Qt::Key_Shift) {
		_widget->tryProcessKeyInput(e);
	} else {
		e->ignore();
	}
}

void HistoryInner::checkActivation() {
	if (!_widget->markingMessagesRead()) {
		return;
	}
	adjustCurrent(_visibleAreaBottom);
	if (_history->loadedAtBottom() && _visibleAreaBottom >= height()) {
		// Clear possible message notifications.
		// Side-effect: Also clears all notifications from forum topics.
		Core::App().notifications().clearFromHistory(_history);
	}
	if (_curHistory != _history || _history->isEmpty()) {
		return;
	}
	auto block = _history->blocks[_curBlock].get();
	auto view = block->messages[_curItem].get();
	while (_curBlock > 0 || _curItem > 0) {
		const auto bottom = itemTop(view) + view->height();
		if (_visibleAreaBottom >= bottom) {
			break;
		}
		if (_curItem > 0) {
			view = block->messages[--_curItem].get();
		} else {
			while (_curBlock > 0) {
				block = _history->blocks[--_curBlock].get();
				_curItem = block->messages.size();
				if (_curItem > 0) {
					view = block->messages[--_curItem].get();
					break;
				}
			}
		}
	}
	session().data().histories().readInboxTill(view->data());
}

void HistoryInner::recountHistoryGeometry() {
	_contentWidth = _scroll->width();

	if (_history->hasPendingResizedItems()
		|| (_migrated && _migrated->hasPendingResizedItems())) {
		_recountedAfterPendingResizedItems = true;
	}

	const auto visibleHeight = _scroll->height();
	auto oldHistoryPaddingTop = qMax(
		visibleHeight - historyHeight() - st::historyPaddingBottom,
		0);
	if (_aboutView) {
		accumulate_max(oldHistoryPaddingTop, _aboutView->height);
	}

	updateBotInfo(false);

	_history->resizeToWidth(_contentWidth);
	if (_migrated) {
		_migrated->resizeToWidth(_contentWidth);
	}

	// With migrated history we perhaps do not need to display
	// the first _history message date (just skip it by height).
	_historySkipHeight = 0;
	if (_migrated
		&& _migrated->loadedAtBottom()
		&& _history->loadedAtTop()) {
		if (const auto first = _history->findFirstNonEmpty()) {
			if (const auto last = _migrated->findLastNonEmpty()) {
				if (first->dateTime().date() == last->dateTime().date()) {
					const auto dateHeight = first->displayedDateHeight();
					if (_migrated->height() > dateHeight) {
						_historySkipHeight += dateHeight;
					}
				}
			}
		}
	}

	if (const auto view = _aboutView ? _aboutView->view() : nullptr) {
		_aboutView->height = view->resizeGetHeight(_contentWidth);
		_aboutView->top = qMin(
			_historyPaddingTop - _aboutView->height,
			qMax(0, (_scroll->height() - _aboutView->height) / 2));
	} else if (_aboutView) {
		_aboutView->top = _aboutView->height = 0;
	}

	auto newHistoryPaddingTop = qMax(
		visibleHeight - historyHeight() - st::historyPaddingBottom,
		0);
	if (_aboutView) {
		accumulate_max(newHistoryPaddingTop, _aboutView->height);
	}

	auto historyPaddingTopDelta = (newHistoryPaddingTop - oldHistoryPaddingTop);
	if (historyPaddingTopDelta != 0) {
		if (_history->scrollTopItem) {
			_history->scrollTopOffset += historyPaddingTopDelta;
		} else if (_migrated && _migrated->scrollTopItem) {
			_migrated->scrollTopOffset += historyPaddingTopDelta;
		}
	}
}

void HistoryInner::updateBotInfo(bool recount) {
	if (!_aboutView) {
		return;
	} else if (_aboutView->refresh() && recount && _contentWidth > 0) {
		const auto view = _aboutView->view();
		const auto now = view ? view->resizeGetHeight(_contentWidth) : 0;
		if (_aboutView->height != now) {
			_aboutView->height = now;
			updateSize();
		}
	}
}

bool HistoryInner::wasSelectedText() const {
	return _wasSelectedText;
}

void HistoryInner::visibleAreaUpdated(int top, int bottom) {
	auto scrolledUp = (top < _visibleAreaTop);
	_visibleAreaTop = top;
	_visibleAreaBottom = bottom;
	const auto visibleAreaHeight = bottom - top;

	// if history has pending resize events we should not update scrollTopItem
	if (hasPendingResizedItems()) {
		return;
	}

	if (bottom >= _historyPaddingTop + historyHeight() + st::historyPaddingBottom) {
		_history->forgetScrollState();
		if (_migrated) {
			_migrated->forgetScrollState();
		}
	} else {
		int htop = historyTop(), mtop = migratedTop();
		if ((htop >= 0 && top >= htop) || mtop < 0) {
			_history->countScrollState(top - htop);
			if (_migrated) {
				_migrated->forgetScrollState();
			}
		} else if (mtop >= 0 && top >= mtop) {
			_history->forgetScrollState();
			_migrated->countScrollState(top - mtop);
		} else {
			_history->countScrollState(top - htop);
			if (_migrated) {
				_migrated->forgetScrollState();
			}
		}
	}
	if (scrolledUp) {
		_scrollDateCheck.call();
	} else {
		scrollDateHideByTimer();
	}

	// Unload userpics.
	if (_userpics.size() > kClearUserpicsAfter) {
		_userpicsCache = std::move(_userpics);
	}

	// Unload lottie animations.
	const auto pages = kUnloadHeavyPartsPages;
	const auto from = _visibleAreaTop - pages * visibleAreaHeight;
	const auto till = _visibleAreaBottom + pages * visibleAreaHeight;
	session().data().unloadHeavyViewParts(_elementDelegate, from, till);
	if (_migratedElementDelegate) {
		session().data().unloadHeavyViewParts(
			_migratedElementDelegate,
			from,
			till);
	}
	checkActivation();

	_emojiInteractions->visibleAreaUpdated(
		_visibleAreaTop,
		_visibleAreaBottom);
}

bool HistoryInner::displayScrollDate() const {
	return (_visibleAreaTop <= height() - 2 * (_visibleAreaBottom - _visibleAreaTop));
}

void HistoryInner::scrollDateCheck() {
	auto newScrollDateItem = _history->scrollTopItem ? _history->scrollTopItem : (_migrated ? _migrated->scrollTopItem : nullptr);
	auto newScrollDateItemTop = _history->scrollTopItem ? _history->scrollTopOffset : (_migrated ? _migrated->scrollTopOffset : 0);
	//if (newScrollDateItem && !displayScrollDate()) {
	//	if (!_history->isEmpty() && newScrollDateItem->date.date() == _history->blocks.back()->messages.back()->data()->date.date()) {
	//		newScrollDateItem = nullptr;
	//	}
	//}
	if (!newScrollDateItem) {
		_scrollDateLastItem = nullptr;
		_scrollDateLastItemTop = 0;
		scrollDateHide();
	} else if (newScrollDateItem != _scrollDateLastItem || newScrollDateItemTop != _scrollDateLastItemTop) {
		// Show scroll date only if it is not the initial onScroll() event (with empty _scrollDateLastItem).
		if (_scrollDateLastItem && !_scrollDateShown) {
			toggleScrollDateShown();
		}
		_scrollDateLastItem = newScrollDateItem;
		_scrollDateLastItemTop = newScrollDateItemTop;
		_scrollDateHideTimer.callOnce(kScrollDateHideTimeout);
	}
}

void HistoryInner::scrollDateHideByTimer() {
	_scrollDateHideTimer.cancel();
	if (!_scrollDateLink || ClickHandler::getPressed() != _scrollDateLink) {
		scrollDateHide();
	}
}

void HistoryInner::scrollDateHide() {
	if (_scrollDateShown) {
		toggleScrollDateShown();
	}
}

void HistoryInner::keepScrollDateForNow() {
	if (!_scrollDateShown && _scrollDateLastItem && _scrollDateOpacity.animating()) {
		toggleScrollDateShown();
	}
	_scrollDateHideTimer.callOnce(kScrollDateHideTimeout);
}

void HistoryInner::toggleScrollDateShown() {
	_scrollDateShown = !_scrollDateShown;
	auto from = _scrollDateShown ? 0. : 1.;
	auto to = _scrollDateShown ? 1. : 0.;
	_scrollDateOpacity.start([this] { repaintScrollDateCallback(); }, from, to, st::historyDateFadeDuration);
}

void HistoryInner::repaintScrollDateCallback() {
	int updateTop = _visibleAreaTop;
	int updateHeight = st::msgServiceMargin.top() + st::msgServicePadding.top() + st::msgServiceFont->height + st::msgServicePadding.bottom();
	update(0, updateTop, width(), updateHeight);
}

void HistoryInner::setItemsRevealHeight(int revealHeight) {
	_revealHeight = revealHeight;
}

void HistoryInner::changeItemsRevealHeight(int revealHeight) {
	if (_revealHeight == revealHeight) {
		return;
	}
	_revealHeight = revealHeight;
	updateSize();
}

void HistoryInner::updateSize() {
	const auto visibleHeight = _scroll->height();
	const auto itemsHeight = historyHeight() - _revealHeight;
	auto newHistoryPaddingTop = qMax(visibleHeight - itemsHeight - st::historyPaddingBottom, 0);
	if (_aboutView) {
		accumulate_max(newHistoryPaddingTop, _aboutView->height);
	}

	if (_aboutView && _aboutView->height > 0) {
		_aboutView->top = qMin(
			newHistoryPaddingTop - _aboutView->height,
			qMax(0, (_scroll->height() - _aboutView->height) / 2));
	}

	if (_historyPaddingTop != newHistoryPaddingTop) {
		_historyPaddingTop = newHistoryPaddingTop;
	}

	int newHeight = _historyPaddingTop + itemsHeight + st::historyPaddingBottom;
	if (width() != _scroll->width() || height() != newHeight) {
		resize(_scroll->width(), newHeight);

		if (!_revealHeight) {
			mouseActionUpdate(QCursor::pos());
		}
	} else {
		update();
	}
}

void HistoryInner::setShownPinned(HistoryItem *item) {
	_pinnedItem = item;
}

void HistoryInner::enterEventHook(QEnterEvent *e) {
	mouseActionUpdate(QCursor::pos());
	return TWidget::enterEventHook(e);
}

void HistoryInner::leaveEventHook(QEvent *e) {
	_reactionsManager->updateButton({ .cursorLeft = true });
	if (auto item = Element::Hovered()) {
		repaintItem(item);
		Element::Hovered(nullptr);
	}
	ClickHandler::clearActive();
	Ui::Tooltip::Hide();
	if (!ClickHandler::getPressed() && _cursor != style::cur_default) {
		_cursor = style::cur_default;
		setCursor(_cursor);
	}
	return TWidget::leaveEventHook(e);
}

HistoryInner::~HistoryInner() {
	_aboutView = nullptr;
	for (const auto &item : _animatedStickersPlayed) {
		if (const auto view = item->mainView()) {
			if (const auto media = view->media()) {
				media->stickerClearLoopPlayed();
			}
		}
	}
	_history->delegateMixin()->setCurrent(nullptr);
	if (_migrated) {
		_migrated->delegateMixin()->setCurrent(nullptr);
	}
	delete _menu;
	_mouseAction = MouseAction::None;
}

bool HistoryInner::focusNextPrevChild(bool next) {
	if (_selected.empty()) {
		return TWidget::focusNextPrevChild(next);
	} else {
		clearSelected();
		return true;
	}
}

void HistoryInner::adjustCurrent(int32 y) const {
	int32 htop = historyTop(), hdrawtop = historyDrawTop(), mtop = migratedTop();
	_curHistory = nullptr;
	if (mtop >= 0) {
		adjustCurrent(y - mtop, _migrated);
	}
	if (htop >= 0 && hdrawtop >= 0 && (mtop < 0 || y >= hdrawtop)) {
		adjustCurrent(y - htop, _history);
	}
}

void HistoryInner::adjustCurrent(int32 y, History *history) const {
	Expects(!history->isEmpty());

	_curHistory = history;
	if (_curBlock >= history->blocks.size()) {
		_curBlock = history->blocks.size() - 1;
		_curItem = 0;
	}
	while (history->blocks[_curBlock]->y() > y && _curBlock > 0) {
		--_curBlock;
		_curItem = 0;
	}
	while (history->blocks[_curBlock]->y() + history->blocks[_curBlock]->height() <= y && _curBlock + 1 < history->blocks.size()) {
		++_curBlock;
		_curItem = 0;
	}
	auto block = history->blocks[_curBlock].get();
	if (_curItem >= block->messages.size()) {
		_curItem = block->messages.size() - 1;
	}
	auto by = block->y();
	while (block->messages[_curItem]->y() + by > y && _curItem > 0) {
		--_curItem;
	}
	while (block->messages[_curItem]->y() + block->messages[_curItem]->height() + by <= y && _curItem + 1 < block->messages.size()) {
		++_curItem;
	}
}

auto HistoryInner::prevItem(Element *view) -> Element* {
	if (!view) {
		return nullptr;
	} else if (const auto result = view->previousDisplayedInBlocks()) {
		return result;
	} else if (view->history() == _history
		&& _migrated
		&& _history->loadedAtTop()
		&& !_migrated->isEmpty()
		&& _migrated->loadedAtBottom()) {
		return _migrated->findLastDisplayed();
	}
	return nullptr;
}

auto HistoryInner::nextItem(Element *view) -> Element* {
	if (!view) {
		return nullptr;
	} else if (const auto result = view->nextDisplayedInBlocks()) {
		return result;
	} else if (view->history() == _migrated
		&& _migrated->loadedAtBottom()
		&& _history->loadedAtTop()
		&& !_history->isEmpty()) {
		return _history->findFirstDisplayed();
	}
	return nullptr;
}

bool HistoryInner::canCopySelected() const {
	return !_selected.empty();
}

bool HistoryInner::canDeleteSelected() const {
	const auto selectedState = getSelectionState();
	return (selectedState.count > 0)
		&& (selectedState.count == selectedState.canDeleteCount);
}

HistoryView::SelectionModeResult HistoryInner::inSelectionMode() const {
	const auto inSelectionMode = [&] {
		if (hasSelectedItems()) {
			return true;
		}
		const auto isSelecting = _mouseAction == MouseAction::Selecting;
		if (isSelecting && _dragSelFrom && _dragSelTo) {
			return true;
		} else if (_chooseForReportReason.has_value()) {
			return true;
		} else if (_lastInSelectionMode && isSelecting) {
			return true;
		}
		return false;
	}();
	const auto now = inSelectionMode;
	if (_lastInSelectionMode != now) {
		_lastInSelectionMode = now;
		if (_inSelectionModeAnimation.animating()) {
			const auto progress = !now
				? _inSelectionModeAnimation.value(0.)
				: 1. - _inSelectionModeAnimation.value(0.);
			_inSelectionModeAnimation.change(
				now ? 1. : 0.,
				st::universalDuration * (1. - progress));
		} else {
			_inSelectionModeAnimation.stop();
			_inSelectionModeAnimation.start(
				[this] {
					const_cast<HistoryInner*>(this)->update(
						QRect(
							0,
							_visibleAreaTop,
							width(),
							_visibleAreaBottom - _visibleAreaTop));
				},
				now ? 0. : 1.,
				now ? 1. : 0.,
				st::universalDuration);
		}
	}
	return { now, _inSelectionModeAnimation.value(now ? 1. : 0.) };
}

bool HistoryInner::elementIntersectsRange(
		not_null<const Element*> view,
		int from,
		int till) const {
	const auto top = itemTop(view);
	if (top < 0) {
		return false;
	}
	const auto bottom = top + view->height();
	return (top < till && bottom > from);
}

void HistoryInner::elementStartStickerLoop(
		not_null<const Element*> view) {
	_animatedStickersPlayed.emplace(view->data());
}

void HistoryInner::elementShowPollResults(
		not_null<PollData*> poll,
		FullMsgId context) {
	_controller->showPollResults(poll, context);
}

void HistoryInner::elementOpenPhoto(
		not_null<PhotoData*> photo,
		FullMsgId context) {
	_controller->openPhoto(photo, { context });
}

void HistoryInner::elementOpenDocument(
		not_null<DocumentData*> document,
		FullMsgId context,
		bool showInMediaView) {
	_controller->openDocument(document, showInMediaView, { context });
}

void HistoryInner::elementCancelUpload(const FullMsgId &context) {
	if (const auto item = session().data().message(context)) {
		_controller->cancelUploadLayer(item);
	}
}

void HistoryInner::elementShowTooltip(
		const TextWithEntities &text,
		Fn<void()> hiddenCallback) {
	_widget->showInfoTooltip(text, std::move(hiddenCallback));
}

bool HistoryInner::elementAnimationsPaused() {
	return _controller->isGifPausedAtLeastFor(Window::GifPauseReason::Any);
}

void HistoryInner::elementSendBotCommand(
		const QString &command,
		const FullMsgId &context) {
	_widget->sendBotCommand({ _history->peer, command, context });
}

void HistoryInner::elementSearchInList(
		const QString &query,
		const FullMsgId &context) {
	const auto inChat = _history->peer->isUser()
		? Dialogs::Key()
		: Dialogs::Key(_history);
	_controller->searchMessages(query, inChat);
}

void HistoryInner::elementHandleViaClick(not_null<UserData*> bot) {
	_widget->insertBotCommand('@' + bot->username());
}

bool HistoryInner::elementIsChatWide() {
	return _isChatWide;
}

not_null<Ui::PathShiftGradient*> HistoryInner::elementPathShiftGradient() {
	return _pathGradient.get();
}

void HistoryInner::elementReplyTo(const FullReplyTo &to) {
	return _widget->replyToMessage(to);
}

void HistoryInner::elementStartInteraction(not_null<const Element*> view) {
	_controller->emojiInteractions().startOutgoing(view);
}

void HistoryInner::elementStartPremium(
		not_null<const Element*> view,
		Element *replacing) {
	const auto already = !_emojiInteractions->playPremiumEffect(
		view,
		replacing);
	_animatedStickersPlayed.emplace(view->data());
	if (already) {
		_widget->showPremiumStickerTooltip(view);
	}
}

void HistoryInner::elementCancelPremium(not_null<const Element*> view) {
	_emojiInteractions->cancelPremiumEffect(view);
}

void HistoryInner::elementStartEffect(
		not_null<const Element*> view,
		Element *replacing) {
	_emojiInteractions->playEffect(view);
}

auto HistoryInner::getSelectionState() const
-> HistoryView::TopBarWidget::SelectedState {
	auto result = HistoryView::TopBarWidget::SelectedState {};
	for (auto &selected : _selected) {
		if (selected.second == FullSelection) {
			++result.count;
			if (selected.first->canDelete()) {
				++result.canDeleteCount;
			}
			if (selected.first->allowsForward()) {
				++result.canForwardCount;
			}
		} else if (selected.second.from != selected.second.to) {
			result.textSelected = true;
		}
	}
	return result;
}

void HistoryInner::clearSelected(bool onlyTextSelection) {
	if (!_selected.empty() && (!onlyTextSelection || _selected.cbegin()->second != FullSelection)) {
		_selected.clear();
		_widget->updateTopBarSelection();
		_widget->update();
	}
}

bool HistoryInner::hasSelectedItems() const {
	return !_selected.empty() && _selected.cbegin()->second == FullSelection;
}

MessageIdsList HistoryInner::getSelectedItems() const {
	using namespace ranges;

	if (!hasSelectedItems()) {
		return {};
	}

	auto result = ranges::make_subrange(
		_selected.begin(),
		_selected.end()
	) | views::filter([](const auto &selected) {
		const auto item = selected.first;
		return item && !item->isService() && item->isRegular();
	}) | views::transform([](const auto &selected) {
		return selected.first->fullId();
	}) | to_vector;

	result |= actions::sort(less{}, [](const FullMsgId &msgId) {
		return peerIsChannel(msgId.peer)
			? msgId.msg
			: (msgId.msg - ServerMaxMsgId);
	});
	return result;
}

void HistoryInner::onTouchSelect() {
	_touchSelect = true;
	_touchMaybeSelecting = true;
	mouseActionStart(_touchPos, Qt::LeftButton);
}

auto HistoryInner::reactionButtonParameters(
	not_null<const Element*> view,
	QPoint position,
	const HistoryView::TextState &reactionState) const
-> HistoryView::Reactions::ButtonParameters {
	if (!_useCornerReaction) {
		return {};
	}
	const auto top = itemTop(view);
	if (top < 0
		|| !view->data()->canReact()
		|| _mouseAction == MouseAction::Dragging
		|| _mouseAction == MouseAction::Selecting
		|| inSelectionMode().inSelectionMode) {
		return {};
	}
	auto result = view->reactionButtonParameters(
		position,
		reactionState
	).translated({ 0, itemTop(view) });
	result.visibleTop = _visibleAreaTop;
	result.visibleBottom = _visibleAreaBottom;
	result.globalPointer = _mousePosition;
	return result;
}

void HistoryInner::mouseActionUpdate() {
	if (hasPendingResizedItems()) {
		return;
	}

	auto mousePos = mapFromGlobal(_mousePosition);
	auto point = _widget->clampMousePosition(mousePos);

	auto m = QPoint();

	adjustCurrent(point.y());
	const auto reactionState = _reactionsManager->buttonTextState(point);
	const auto reactionItem = session().data().message(reactionState.itemId);
	const auto reactionView = viewByItem(reactionItem);
	const auto view = reactionView
		? reactionView
		: (_aboutView
			&& _aboutView->view()
			&& point.y() >= _aboutView->top
			&& point.y() < _aboutView->top + _aboutView->view()->height())
		? _aboutView->view()
		: (_curHistory && !_curHistory->isEmpty())
		? _curHistory->blocks[_curBlock]->messages[_curItem].get()
		: nullptr;
	const auto item = view ? view->data().get() : nullptr;
	const auto selectionViewOffset = view
		? QPoint(SelectionViewOffset(this, view), 0)
		: QPoint(0, 0);
	point -= selectionViewOffset;
	if (view) {
		const auto changed = (Element::Moused() != view);
		if (changed) {
			repaintItem(Element::Moused());
			Element::Moused(view);
			repaintItem(Element::Moused());
		}
		m = mapPointToItem(point, view);
		_reactionsManager->updateButton(reactionButtonParameters(
			view,
			m,
			reactionState));
		if (changed) {
			_reactionsItem = item;
		}
		if (view->pointState(m) != PointState::Outside) {
			if (Element::Hovered() != view) {
				repaintItem(Element::Hovered());
				Element::Hovered(view);
				repaintItem(Element::Hovered());
			}
		} else if (Element::Hovered()) {
			repaintItem(Element::Hovered());
			Element::Hovered(nullptr);
		}
	} else {
		if (Element::Moused()) {
			repaintItem(Element::Moused());
			Element::Moused(nullptr);
		}
		_reactionsManager->updateButton({});
	}
	if (_mouseActionItem && !viewByItem(_mouseActionItem)) {
		mouseActionCancel();
	}

	TextState dragState;
	ClickHandlerHost *lnkhost = nullptr;
	auto selectingText = (item == _mouseActionItem)
		&& (view == Element::Hovered())
		&& !_selected.empty()
		&& (_selected.cbegin()->second != FullSelection);
	const auto overReaction = reactionView && reactionState.link;
	if (overReaction) {
		dragState = reactionState;
		lnkhost = reactionView;
	} else if (item) {
		if (item != _mouseActionItem || ((m + selectionViewOffset) - _dragStartPosition).manhattanLength() >= QApplication::startDragDistance()) {
			if (_mouseAction == MouseAction::PrepareDrag) {
				_mouseAction = MouseAction::Dragging;
				InvokeQueued(this, [=] { performDrag(); });
			} else if (_mouseAction == MouseAction::PrepareSelect) {
				_mouseAction = MouseAction::Selecting;
			}
		}

		auto dateHeight = st::msgServicePadding.bottom() + st::msgServiceFont->height + st::msgServicePadding.top();
		auto scrollDateOpacity = _scrollDateOpacity.value(_scrollDateShown ? 1. : 0.);
		enumerateDates([&](not_null<Element*> view, int itemtop, int dateTop) {
			// stop enumeration if the date is above our point
			if (dateTop + dateHeight <= point.y()) {
				return false;
			}

			const auto displayDate = view->displayDate();
			auto dateInPlace = displayDate;
			if (dateInPlace) {
				const auto correctDateTop = itemtop + st::msgServiceMargin.top();
				dateInPlace = (dateTop < correctDateTop + dateHeight);
			}

			// stop enumeration if we've found a date under the cursor
			if (dateTop <= point.y()) {
				auto opacity = (dateInPlace/* || noFloatingDate*/) ? 1. : scrollDateOpacity;
				if (opacity > 0.) {
					const auto item = view->data();
					auto dateWidth = 0;
					if (const auto date = view->Get<HistoryView::DateBadge>()) {
						dateWidth = date->width;
					} else {
						dateWidth = st::msgServiceFont->width(langDayOfMonthFull(view->dateTime().date()));
					}
					dateWidth += st::msgServicePadding.left() + st::msgServicePadding.right();
					auto dateLeft = st::msgServiceMargin.left();
					auto maxwidth = _contentWidth;
					if (_isChatWide) {
						maxwidth = qMin(maxwidth, int32(st::msgMaxWidth + 2 * st::msgPhotoSkip + 2 * st::msgMargin.left()));
					}
					auto widthForDate = maxwidth - st::msgServiceMargin.left() - st::msgServiceMargin.left();

					dateLeft += (widthForDate - dateWidth) / 2;

					if (point.x() >= dateLeft && point.x() < dateLeft + dateWidth) {
						if (!_scrollDateLink) {
							_scrollDateLink = std::make_shared<Window::DateClickHandler>(item->history(), view->dateTime().date());
						} else {
							static_cast<Window::DateClickHandler*>(_scrollDateLink.get())->setDate(view->dateTime().date());
						}
						dragState = TextState(
							nullptr,
							_scrollDateLink);
						_dragStateItem = session().data().message(dragState.itemId);
						lnkhost = view;
					}
				}
				return false;
			}
			return true;
		});
		if (!dragState.link) {
			StateRequest request;
			if (_mouseAction == MouseAction::Selecting) {
				request.flags |= Ui::Text::StateRequest::Flag::LookupSymbol;
			} else {
				selectingText = false;
			}
			if (base::IsAltPressed()) {
				request.flags &= ~Ui::Text::StateRequest::Flag::LookupLink;
			}
			dragState = view->textState(m, request);
			_dragStateItem = session().data().message(dragState.itemId);
			lnkhost = view;
			if (!dragState.link && m.x() >= st::historyPhotoLeft && m.x() < st::historyPhotoLeft + st::msgPhotoSize) {
				if (!item->isService() && view->hasFromPhoto()) {
					enumerateUserpics([&](not_null<Element*> view, int userpicTop) -> bool {
						// stop enumeration if the userpic is below our point
						if (userpicTop > point.y()) {
							return false;
						}

						// stop enumeration if we've found a userpic under the cursor
						if (point.y() >= userpicTop && point.y() < userpicTop + st::msgPhotoSize) {
							dragState = TextState(nullptr, view->fromPhotoLink());
							_dragStateItem = nullptr;
							lnkhost = view;
							return false;
						}
						return true;
					});
				}
			}
		}
	}
	auto lnkChanged = ClickHandler::setActive(dragState.link, lnkhost);
	if (lnkChanged || dragState.cursor != _mouseCursorState) {
		Ui::Tooltip::Hide();
	}
	if (dragState.link
		|| dragState.cursor == CursorState::Date
		|| dragState.cursor == CursorState::Forwarded
		|| dragState.customTooltip) {
		Ui::Tooltip::Show(1000, this);
	}

	Qt::CursorShape cur = style::cur_default;
	_acceptsHorizontalScroll = dragState.horizontalScroll;
	if (_mouseAction == MouseAction::None) {
		_mouseCursorState = dragState.cursor;
		if (dragState.link) {
			cur = style::cur_pointer;
		} else if (_mouseCursorState == CursorState::Text && (_selected.empty() || _selected.cbegin()->second != FullSelection)) {
			cur = style::cur_text;
		} else if (_mouseCursorState == CursorState::Date) {
			//cur = style::cur_cross;
		}
	} else if (item) {
		if (_mouseAction == MouseAction::Selecting) {
			if (selectingText) {
				uint16 second = dragState.symbol;
				if (dragState.afterSymbol && _mouseSelectType == TextSelectType::Letters) {
					++second;
				}
				auto selState = TextSelection { qMin(second, _mouseTextSymbol), qMax(second, _mouseTextSymbol) };
				if (_mouseSelectType != TextSelectType::Letters) {
					if (const auto view = viewByItem(_mouseActionItem)) {
						selState = view->adjustSelection(selState, _mouseSelectType);
					}
				}
				if (!selState.empty()) {
					// We started selecting text in web page preview.
					ClickHandler::unpressed();
				}
				if (_selected[_mouseActionItem] != selState) {
					_selected[_mouseActionItem] = selState;
					repaintItem(_mouseActionItem);
				}
				if (!_wasSelectedText && (selState == FullSelection || selState.from != selState.to)) {
					_wasSelectedText = true;
					setFocus();
				}
				updateDragSelection(nullptr, nullptr, false);
			} else {
				auto selectingDown = (itemTop(_mouseActionItem) < itemTop(item)) || (_mouseActionItem == item && _dragStartPosition.y() < m.y());
				auto dragSelFrom = viewByItem(_mouseActionItem);
				auto dragSelTo = view;
				// Maybe exclude dragSelFrom.
				if (dragSelFrom->pointState(_dragStartPosition) == PointState::Outside) {
					if (selectingDown) {
						if (_dragStartPosition.y() >= dragSelFrom->height() - dragSelFrom->marginBottom() || ((view == dragSelFrom) && (m.y() < _dragStartPosition.y() + QApplication::startDragDistance() || m.y() < dragSelFrom->marginTop()))) {
							dragSelFrom = (dragSelFrom != dragSelTo)
								? nextItem(dragSelFrom)
								: nullptr;
						}
					} else {
						if (_dragStartPosition.y() < dragSelFrom->marginTop() || ((view == dragSelFrom) && (m.y() >= _dragStartPosition.y() - QApplication::startDragDistance() || m.y() >= dragSelFrom->height() - dragSelFrom->marginBottom()))) {
							dragSelFrom = (dragSelFrom != dragSelTo)
								? prevItem(dragSelFrom)
								: nullptr;
						}
					}
				}
				if (_mouseActionItem != item) { // maybe exclude dragSelTo
					if (selectingDown) {
						if (m.y() < dragSelTo->marginTop()) {
							dragSelTo = (dragSelFrom != dragSelTo)
								? prevItem(dragSelTo)
								: nullptr;
						}
					} else {
						if (m.y() >= dragSelTo->height() - dragSelTo->marginBottom()) {
							dragSelTo = (dragSelFrom != dragSelTo)
								? nextItem(dragSelTo)
								: nullptr;
						}
					}
				}
				auto dragSelecting = false;
				auto dragFirstAffected = dragSelFrom;
				while (dragFirstAffected
					&& (!dragFirstAffected->data()->isRegular()
						|| dragFirstAffected->data()->isService())) {
					dragFirstAffected = (dragFirstAffected != dragSelTo)
						? (selectingDown
							? nextItem(dragFirstAffected)
							: prevItem(dragFirstAffected))
						: nullptr;
				}
				if (dragFirstAffected) {
					auto i = _selected.find(dragFirstAffected->data());
					dragSelecting = (i == _selected.cend() || i->second != FullSelection);
				}
				updateDragSelection(dragSelFrom, dragSelTo, dragSelecting);
			}
		} else if (_mouseAction == MouseAction::Dragging) {
		}

		if (ClickHandler::getPressed()) {
			cur = style::cur_pointer;
		} else if ((_mouseAction == MouseAction::Selecting)
			&& !_selected.empty()
			&& (_selected.cbegin()->second != FullSelection)) {
			if (!_dragSelFrom || !_dragSelTo) {
				cur = style::cur_text;
			}
		}
	}

	// Voice message seek support.
	if (const auto pressedItem = _dragStateItem) {
		if (const auto pressedView = viewByItem(pressedItem)) {
			if (pressedItem->history() == _history || pressedItem->history() == _migrated) {
				auto adjustedPoint = mapPointToItem(point, pressedView);
				pressedView->updatePressed(adjustedPoint);
			}
		}
	}

	if (_mouseAction == MouseAction::Selecting) {
		_selectScroll.checkDeltaScroll(
			mousePos,
			_scroll->scrollTop(),
			_scroll->scrollTop() + _scroll->height());
	} else {
		updateDragSelection(nullptr, nullptr, false);
		_selectScroll.cancel();
	}

	if (_mouseAction == MouseAction::None && (lnkChanged || cur != _cursor)) {
		setCursor(_cursor = cur);
	}
}

void HistoryInner::updateDragSelection(Element *dragSelFrom, Element *dragSelTo, bool dragSelecting) {
	if (_dragSelFrom == dragSelFrom && _dragSelTo == dragSelTo && _dragSelecting == dragSelecting) {
		return;
	} else if (dragSelFrom && hasSelectRestriction()) {
		updateDragSelection(nullptr, nullptr, false);
		return;
	}
	_dragSelFrom = dragSelFrom;
	_dragSelTo = dragSelTo;
	int32 fromy = itemTop(_dragSelFrom), toy = itemTop(_dragSelTo);
	if (fromy >= 0 && toy >= 0 && fromy > toy) {
		std::swap(_dragSelFrom, _dragSelTo);
	}
	_dragSelecting = dragSelecting;
	if (!_wasSelectedText && _dragSelFrom && _dragSelTo && _dragSelecting) {
		_wasSelectedText = true;
		setFocus();
	}
	update();
}

int HistoryInner::historyHeight() const {
	int result = 0;
	if (_history->isEmpty()) {
		result += _migrated ? _migrated->height() : 0;
	} else {
		result += _history->height() - _historySkipHeight + (_migrated ? _migrated->height() : 0);
	}
	return result;
}

int HistoryInner::historyScrollTop() const {
	auto htop = historyTop();
	auto mtop = migratedTop();
	if (htop >= 0 && _history->scrollTopItem) {
		return htop + _history->scrollTopItem->block()->y() + _history->scrollTopItem->y() + _history->scrollTopOffset;
	}
	if (mtop >= 0 && _migrated->scrollTopItem) {
		return mtop + _migrated->scrollTopItem->block()->y() + _migrated->scrollTopItem->y() + _migrated->scrollTopOffset;
	}
	return ScrollMax;
}

int HistoryInner::migratedTop() const {
	return (_migrated && !_migrated->isEmpty()) ? _historyPaddingTop : -1;
}

int HistoryInner::historyTop() const {
	int mig = migratedTop();
	return !_history->isEmpty()
		? (mig >= 0
			? (mig + _migrated->height() - _historySkipHeight)
			: _historyPaddingTop)
		: -1;
}

int HistoryInner::historyDrawTop() const {
	auto top = historyTop();
	return (top >= 0) ? (top + _historySkipHeight) : -1;
}

void HistoryInner::setChooseReportReason(Data::ReportInput reportInput) {
	_chooseForReportReason = reportInput;
}

void HistoryInner::clearChooseReportReason() {
	_chooseForReportReason = std::nullopt;
}

auto HistoryInner::viewByItem(const HistoryItem *item) const -> Element* {
	return !item
		? nullptr
		: (_aboutView && _aboutView->item() == item)
		? _aboutView->view()
		: item->mainView();
}

// -1 if should not be visible, -2 if bad history()
int HistoryInner::itemTop(const HistoryItem *item) const {
	return item ? itemTop(viewByItem(item)) : -2;
}

int HistoryInner::itemTop(const Element *view) const {
	if (!view) {
		return -1;
	} else if (_aboutView && view == _aboutView->view()) {
		return _aboutView->top;
	} else if (view->data()->mainView() != view) {
		return -1;
	}

	const auto top = (view->history() == _history)
		? historyTop()
		: (view->history() == _migrated
			? migratedTop()
			: -2);
	return (top < 0) ? top : (top + view->y() + view->block()->y());
}

auto HistoryInner::findViewForPinnedTracking(int top) const
-> std::pair<Element*, int> {
	const auto normalTop = historyTop();
	const auto oldTop = migratedTop();
	const auto fromHistory = [&](not_null<History*> history, int historyTop)
	-> std::pair<Element*, int> {
		auto [view, offset] = history->findItemAndOffset(top - historyTop);
		while (view && !view->data()->isRegular()) {
			offset -= view->height();
			view = view->nextInBlocks();
		}
		return { view, offset };
	};
	if (normalTop >= 0 && (oldTop < 0 || top >= normalTop)) {
		return fromHistory(_history, normalTop);
	} else if (oldTop >= 0) {
		auto [view, offset] = fromHistory(_migrated, oldTop);
		if (!view && normalTop >= 0) {
			return fromHistory(_history, normalTop);
		}
		return { view, offset };
	}
	return { nullptr, 0 };
}

void HistoryInner::refreshAboutView(bool force) {
	const auto refresh = [&] {
		if (force) {
			_aboutView = nullptr;
		}
		if (!_aboutView) {
			_aboutView = std::make_unique<HistoryView::AboutView>(
				_history,
				_history->delegateMixin()->delegate());
		}
	};
	if (const auto user = _peer->asUser()) {
		if (const auto info = user->botInfo.get()) {
			refresh();
			if (!info->inited) {
				session().api().requestFullPeer(user);
			}
		} else if (user->meRequiresPremiumToWrite()
			&& !user->session().premium()
			&& !historyHeight()) {
			refresh();
		} else if (!historyHeight()) {
			if (!user->isFullLoaded()) {
				session().api().requestFullPeer(user);
			} else {
				refresh();
			}
		}
	}
}

void HistoryInner::notifyMigrateUpdated() {
	const auto migrated = _history->migrateFrom();
	if (_migrated != migrated) {
		if (_migrated) {
			_migrated->delegateMixin()->setCurrent(nullptr);
		}
		_migrated = migrated;
		if (_migrated) {
			_migrated->delegateMixin()->setCurrent(this);
			_migrated->translateTo(_history->translatedTo());
		}
	}
}

void HistoryInner::applyDragSelection() {
	if (!hasSelectRestriction()) {
		applyDragSelection(&_selected);
	}
}

bool HistoryInner::isSelected(
		not_null<SelectedItems*> toItems,
		not_null<HistoryItem*> item) const {
	const auto i = toItems->find(item);
	return (i != toItems->cend()) && (i->second == FullSelection);
}

bool HistoryInner::isSelectedGroup(
		not_null<SelectedItems*> toItems,
		not_null<const Data::Group*> group) const {
	for (const auto &other : group->items) {
		if (!isSelected(toItems, other)) {
			return false;
		}
	}
	return true;
}

bool HistoryInner::isSelectedAsGroup(
		not_null<SelectedItems*> toItems,
		not_null<HistoryItem*> item) const {
	if (const auto group = session().data().groups().find(item)) {
		return isSelectedGroup(toItems, group);
	}
	return isSelected(toItems, item);
}

bool HistoryInner::goodForSelection(
		not_null<SelectedItems*> toItems,
		not_null<HistoryItem*> item,
		int &totalCount) const {
	if (!item->isRegular() || item->isService()) {
		return false;
	} else if (toItems->find(item) == toItems->end()) {
		++totalCount;
	}
	return true;
}

void HistoryInner::addToSelection(
		not_null<SelectedItems*> toItems,
		not_null<HistoryItem*> item) const {
	const auto i = toItems->find(item);
	if (i == toItems->cend()) {
		if (toItems->size() == 1
			&& toItems->begin()->second != FullSelection) {
			toItems->clear();
		}
		toItems->emplace(item, FullSelection);
	} else if (i->second != FullSelection) {
		i->second = FullSelection;
	}
}

void HistoryInner::removeFromSelection(
		not_null<SelectedItems*> toItems,
		not_null<HistoryItem*> item) const {
	const auto i = toItems->find(item);
	if (i != toItems->cend()) {
		toItems->erase(i);
	}
}

void HistoryInner::changeSelection(
		not_null<SelectedItems*> toItems,
		not_null<HistoryItem*> item,
		SelectAction action) const {
	if (action == SelectAction::Invert) {
		action = isSelected(toItems, item)
			? SelectAction::Deselect
			: SelectAction::Select;
	}
	auto total = int(toItems->size());
	const auto add = (action == SelectAction::Select);
	if (add
		&& goodForSelection(toItems, item, total)
		&& total <= MaxSelectedItems) {
		addToSelection(toItems, item);
	} else {
		removeFromSelection(toItems, item);
	}
}

void HistoryInner::changeSelectionAsGroup(
		not_null<SelectedItems*> toItems,
		not_null<HistoryItem*> item,
		SelectAction action) const {
	const auto group = session().data().groups().find(item);
	if (!group) {
		return changeSelection(toItems, item, action);
	}
	if (action == SelectAction::Invert) {
		action = isSelectedAsGroup(toItems, item)
			? SelectAction::Deselect
			: SelectAction::Select;
	}
	auto total = int(toItems->size());
	const auto canSelect = [&] {
		for (const auto &other : group->items) {
			if (!goodForSelection(toItems, other, total)) {
				return false;
			}
		}
		return (total <= MaxSelectedItems);
	}();
	if (action == SelectAction::Select && canSelect) {
		for (const auto &other : group->items) {
			addToSelection(toItems, other);
		}
	} else {
		for (const auto &other : group->items) {
			removeFromSelection(toItems, other);
		}
	}
}

void HistoryInner::forwardItem(FullMsgId itemId) {
	Window::ShowForwardMessagesBox(_controller, { 1, itemId });
}

void HistoryInner::forwardAsGroup(FullMsgId itemId) {
	if (const auto item = session().data().message(itemId)) {
		Window::ShowForwardMessagesBox(
			_controller,
			session().data().itemOrItsGroup(item));
	}
}

void HistoryInner::deleteItem(FullMsgId itemId) {
	if (const auto item = session().data().message(itemId)) {
		deleteItem(item);
	}
}

void HistoryInner::deleteItem(not_null<HistoryItem*> item) {
	if (item->isUploading()) {
		_controller->cancelUploadLayer(item);
		return;
	}
	const auto list = HistoryItemsList{ item };
	if (CanCreateModerateMessagesBox(list)) {
		_controller->show(Box(CreateModerateMessagesBox, list, nullptr));
	} else {
		const auto suggestModerate = false;
		_controller->show(Box<DeleteMessagesBox>(item, suggestModerate));
	}
}

bool HistoryInner::hasPendingResizedItems() const {
	return _history->hasPendingResizedItems()
		|| (_migrated && _migrated->hasPendingResizedItems());
}

void HistoryInner::deleteAsGroup(FullMsgId itemId) {
	if (const auto item = session().data().message(itemId)) {
		const auto group = session().data().groups().find(item);
		if (!group) {
			return deleteItem(item);
		} else if (CanCreateModerateMessagesBox(group->items)) {
			_controller->show(Box(
				CreateModerateMessagesBox,
				group->items,
				nullptr));
		} else {
			_controller->show(Box<DeleteMessagesBox>(
				&session(),
				session().data().itemsToIds(group->items)));
		}
	}
}

void HistoryInner::reportItem(FullMsgId itemId) {
	ShowReportMessageBox(_controller->uiShow(), _peer, { itemId.msg }, {});
}

void HistoryInner::reportAsGroup(FullMsgId itemId) {
	if (const auto item = session().data().message(itemId)) {
		const auto group = session().data().groups().find(item);
		const auto ids = group
			? (ranges::views::all(
				group->items
			) | ranges::views::transform([](const auto &i) {
				return i->fullId().msg;
			}) | ranges::to_vector)
			: std::vector<MsgId>{ 1, itemId.msg };
		ShowReportMessageBox(_controller->uiShow(), _peer, ids, {});
	}
}

void HistoryInner::blockSenderItem(FullMsgId itemId) {
	if (const auto item = session().data().message(itemId)) {
		_controller->show(Box(
			Window::BlockSenderFromRepliesBox,
			_controller,
			itemId));
	}
}

void HistoryInner::blockSenderAsGroup(FullMsgId itemId) {
	blockSenderItem(itemId);
}

void HistoryInner::addSelectionRange(
		not_null<SelectedItems*> toItems,
		not_null<History*> history,
		int fromblock,
		int fromitem,
		int toblock,
		int toitem) const {
	if (fromblock >= 0 && fromitem >= 0 && toblock >= 0 && toitem >= 0) {
		for (; fromblock <= toblock; ++fromblock) {
			auto block = history->blocks[fromblock].get();
			for (int cnt = (fromblock < toblock) ? block->messages.size() : (toitem + 1); fromitem < cnt; ++fromitem) {
				auto item = block->messages[fromitem]->data();
				changeSelectionAsGroup(toItems, item, SelectAction::Select);
			}
			if (toItems->size() >= MaxSelectedItems) break;
			fromitem = 0;
		}
	}
}

void HistoryInner::applyDragSelection(
		not_null<SelectedItems*> toItems) const {
	const auto selfromy = itemTop(_dragSelFrom);
	const auto seltoy = [&] {
		auto result = itemTop(_dragSelTo);
		return (result < 0) ? result : (result + _dragSelTo->height());
	}();
	if (selfromy < 0 || seltoy < 0) {
		return;
	}

	if (!toItems->empty() && toItems->cbegin()->second != FullSelection) {
		toItems->clear();
	}
	const auto botAboutView = _aboutView ? _aboutView->view() : nullptr;
	if (_dragSelecting) {
		auto fromblock = (_dragSelFrom != botAboutView)
			? _dragSelFrom->block()->indexInHistory()
			: _history->blocks.empty()
			? -1
			: 0;
		auto fromitem = (_dragSelFrom != botAboutView)
			? _dragSelFrom->indexInBlock()
			: (_history->blocks.empty()
				|| _history->blocks[0]->messages.empty())
			? -1
			: 0;
		auto toblock = (_dragSelTo != botAboutView)
			? _dragSelTo->block()->indexInHistory()
			: _history->blocks.empty()
			? -1
			: 0;
		auto toitem = (_dragSelTo != botAboutView)
			? _dragSelTo->indexInBlock()
			: (_history->blocks.empty()
				|| _history->blocks[0]->messages.empty())
			? -1
			: 0;
		if (_migrated) {
			if (_dragSelFrom->history() == _migrated) {
				if (_dragSelTo->history() == _migrated) {
					addSelectionRange(toItems, _migrated, fromblock, fromitem, toblock, toitem);
					toblock = -1;
					toitem = -1;
				} else {
					addSelectionRange(toItems, _migrated, fromblock, fromitem, _migrated->blocks.size() - 1, _migrated->blocks.back()->messages.size() - 1);
				}
				fromblock = 0;
				fromitem = 0;
			} else if (_dragSelTo->history() == _migrated) { // wtf
				toblock = -1;
				toitem = -1;
			}
		}
		addSelectionRange(toItems, _history, fromblock, fromitem, toblock, toitem);
	} else {
		auto toRemove = std::vector<not_null<HistoryItem*>>();
		for (const auto &item : *toItems) {
			auto iy = itemTop(item.first);
			if (iy < -1) {
				toRemove.emplace_back(item.first);
			} else if (iy >= 0 && iy >= selfromy && iy < seltoy) {
				toRemove.emplace_back(item.first);
			}
		}
		for (const auto item : toRemove) {
			changeSelectionAsGroup(toItems, item, SelectAction::Deselect);
		}
	}
}

QString HistoryInner::tooltipText() const {
	if (_mouseCursorState == CursorState::Date
		&& _mouseAction == MouseAction::None) {
		if (const auto view = Element::Hovered()) {
			return HistoryView::DateTooltipText(view);
		}
	} else if (_mouseCursorState == CursorState::Forwarded
		&& _mouseAction == MouseAction::None) {
		if (const auto view = Element::Moused()) {
			if (const auto forwarded = view->data()->Get<HistoryMessageForwarded>()) {
				return forwarded->text.toString();
			}
		}
	} else if (const auto lnk = ClickHandler::getActive()) {
		return lnk->tooltip();
	} else if (const auto view = Element::Moused()) {
		StateRequest request;
		const auto local = mapFromGlobal(_mousePosition);
		const auto point = _widget->clampMousePosition(local);
		request.flags |= Ui::Text::StateRequest::Flag::LookupCustomTooltip;
		const auto state = view->textState(
			mapPointToItem(point, view),
			request);
		return state.customTooltipText;
	}
	return QString();
}

QPoint HistoryInner::tooltipPos() const {
	return _mousePosition;
}

bool HistoryInner::tooltipWindowActive() const {
	return Ui::AppInFocus() && Ui::InFocusChain(window());
}

void HistoryInner::onParentGeometryChanged() {
	auto mousePos = QCursor::pos();
	auto mouseOver = _widget->rect().contains(_widget->mapFromGlobal(mousePos));
	auto needToUpdate = (_mouseAction != MouseAction::None || _touchScroll || mouseOver);
	if (needToUpdate) {
		mouseActionUpdate(mousePos);
	}
}

bool HistoryInner::consumeScrollAction(QPoint delta) {
	const auto horizontal = (std::abs(delta.x()) > std::abs(delta.y()));
	if (!horizontal || !_acceptsHorizontalScroll || !Element::Moused()) {
		return false;
	}
	const auto position = mapPointToItem(
		mapFromGlobal(_mousePosition),
		Element::Moused());
	return Element::Moused()->consumeHorizontalScroll(position, delta.x());
}

Fn<HistoryView::ElementDelegate*()> HistoryInner::elementDelegateFactory(
		FullMsgId itemId) const {
	const auto weak = base::make_weak(_controller);
	return [=]() -> HistoryView::ElementDelegate* {
		if (const auto strong = weak.get()) {
			auto &data = strong->session().data();
			if (const auto item = data.message(itemId)) {
				const auto history = item->history();
				return history->delegateMixin()->delegate();
			}
		}
		return nullptr;
	};
}

ClickHandlerContext HistoryInner::prepareClickHandlerContext(
		FullMsgId itemId) const {
	return ClickHandlerContext{
		.itemId = itemId,
		.elementDelegate = elementDelegateFactory(itemId),
		.sessionWindow = base::make_weak(_controller),
	};
}

ClickContext HistoryInner::prepareClickContext(
		Qt::MouseButton button,
		FullMsgId itemId) const {
	return {
		button,
		QVariant::fromValue(prepareClickHandlerContext(itemId)),
	};
}

auto HistoryInner::DelegateMixin()
-> std::unique_ptr<HistoryMainElementDelegateMixin> {
	return std::make_unique<HistoryMainElementDelegate>();
}
