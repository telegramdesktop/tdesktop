/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_element.h"

#include "api/api_chat_invite.h"
#include "history/view/history_view_service_message.h"
#include "history/view/history_view_message.h"
#include "history/view/media/history_view_media.h"
#include "history/view/media/history_view_media_grouped.h"
#include "history/view/media/history_view_sticker.h"
#include "history/view/media/history_view_large_emoji.h"
#include "history/view/media/history_view_custom_emoji.h"
#include "history/view/reactions/history_view_reactions_button.h"
#include "history/view/reactions/history_view_reactions.h"
#include "history/view/history_view_cursor_state.h"
#include "history/view/history_view_spoiler_click_handler.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "history/history_item_helpers.h"
#include "base/unixtime.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "core/click_handler_types.h"
#include "core/file_utilities.h"
#include "core/ui_integration.h"
#include "main/main_session.h"
#include "main/main_domain.h"
#include "chat_helpers/stickers_emoji_pack.h"
#include "window/window_session_controller.h"
#include "ui/effects/path_shift_gradient.h"
#include "ui/effects/reaction_fly_animation.h"
#include "ui/chat/chat_style.h"
#include "ui/toast/toast.h"
#include "ui/text/text_options.h"
#include "ui/text/text_utilities.h"
#include "ui/item_text_options.h"
#include "ui/painter.h"
#include "data/data_session.h"
#include "data/data_groups.h"
#include "data/data_forum.h"
#include "data/data_forum_topic.h"
#include "data/data_media_types.h"
#include "data/data_sponsored_messages.h"
#include "data/data_message_reactions.h"
#include "lang/lang_keys.h"
#include "styles/style_chat.h"

namespace HistoryView {
namespace {

// A new message from the same sender is attached to previous within 15 minutes.
constexpr int kAttachMessageToPreviousSecondsDelta = 900;

Element *HoveredElement/* = nullptr*/;
Element *PressedElement/* = nullptr*/;
Element *HoveredLinkElement/* = nullptr*/;
Element *PressedLinkElement/* = nullptr*/;
Element *MousedElement/* = nullptr*/;

[[nodiscard]] bool IsAttachedToPreviousInSavedMessages(
		not_null<HistoryItem*> previous,
		HistoryMessageForwarded *prevForwarded,
		not_null<HistoryItem*> item,
		HistoryMessageForwarded *forwarded) {
	const auto sender = previous->senderOriginal();
	if ((prevForwarded != nullptr) != (forwarded != nullptr)) {
		return false;
	} else if (sender != item->senderOriginal()) {
		return false;
	} else if (!prevForwarded || sender) {
		return true;
	}
	const auto previousInfo = prevForwarded->hiddenSenderInfo.get();
	const auto itemInfo = forwarded->hiddenSenderInfo.get();
	Assert(previousInfo != nullptr);
	Assert(itemInfo != nullptr);
	return (*previousInfo == *itemInfo);
}

[[nodiscard]] Window::SessionController *ContextOrSessionWindow(
		const ClickHandlerContext &context,
		not_null<Main::Session*> session) {
	if (const auto controller = context.sessionWindow.get()) {
		if (&controller->session() == session) {
			return controller;
		}
	}
	return session->tryResolveWindow();
}

} // namespace

std::unique_ptr<Ui::PathShiftGradient> MakePathShiftGradient(
		not_null<const Ui::ChatStyle*> st,
		Fn<void()> update) {
	return std::make_unique<Ui::PathShiftGradient>(
		st->msgServiceBg(),
		st->msgServiceBgSelected(),
		std::move(update),
		st->paletteChanged());
}

bool DefaultElementDelegate::elementUnderCursor(
		not_null<const Element*> view) {
	return false;
}

float64 DefaultElementDelegate::elementHighlightOpacity(
		not_null<const HistoryItem*> item) const {
	return 0.;
}

bool DefaultElementDelegate::elementInSelectionMode() {
	return false;
}

bool DefaultElementDelegate::elementIntersectsRange(
		not_null<const Element*> view,
		int from,
		int till) {
	return true;
}

void DefaultElementDelegate::elementStartStickerLoop(
	not_null<const Element*> view) {
}

void DefaultElementDelegate::elementShowPollResults(
	not_null<PollData*> poll,
	FullMsgId context) {
}

void DefaultElementDelegate::elementOpenPhoto(
	not_null<PhotoData*> photo,
	FullMsgId context) {
}

void DefaultElementDelegate::elementOpenDocument(
	not_null<DocumentData*> document,
	FullMsgId context,
	bool showInMediaView) {
}

void DefaultElementDelegate::elementCancelUpload(const FullMsgId &context) {
}

void DefaultElementDelegate::elementShowTooltip(
	const TextWithEntities &text,
	Fn<void()> hiddenCallback) {
}

bool DefaultElementDelegate::elementHideReply(
		not_null<const Element*> view) {
	return false;
}

bool DefaultElementDelegate::elementShownUnread(
		not_null<const Element*> view) {
	return view->data()->unread(view->data()->history());
}

void DefaultElementDelegate::elementSendBotCommand(
	const QString &command,
	const FullMsgId &context) {
}

void DefaultElementDelegate::elementHandleViaClick(
	not_null<UserData*> bot) {
}

bool DefaultElementDelegate::elementIsChatWide() {
	return false;
}

void DefaultElementDelegate::elementReplyTo(const FullMsgId &to) {
}

void DefaultElementDelegate::elementStartInteraction(
	not_null<const Element*> view) {
}

void DefaultElementDelegate::elementStartPremium(
	not_null<const Element*> view,
	Element *replacing) {
}

void DefaultElementDelegate::elementCancelPremium(
	not_null<const Element*> view) {
}

QString DefaultElementDelegate::elementAuthorRank(
	not_null<const Element*> view) {
	return {};
}

SimpleElementDelegate::SimpleElementDelegate(
	not_null<Window::SessionController*> controller,
	Fn<void()> update)
: _controller(controller)
, _pathGradient(
	MakePathShiftGradient(
		controller->chatStyle(),
		std::move(update))) {
}

SimpleElementDelegate::~SimpleElementDelegate() = default;

bool SimpleElementDelegate::elementAnimationsPaused() {
	return _controller->isGifPausedAtLeastFor(Window::GifPauseReason::Any);
}

auto SimpleElementDelegate::elementPathShiftGradient()
-> not_null<Ui::PathShiftGradient*> {
	return _pathGradient.get();
}

TextSelection UnshiftItemSelection(
		TextSelection selection,
		uint16 byLength) {
	return (selection == FullSelection)
		? selection
		: ::unshiftSelection(selection, byLength);
}

TextSelection ShiftItemSelection(
		TextSelection selection,
		uint16 byLength) {
	return (selection == FullSelection)
		? selection
		: ::shiftSelection(selection, byLength);
}

TextSelection UnshiftItemSelection(
		TextSelection selection,
		const Ui::Text::String &byText) {
	return UnshiftItemSelection(selection, byText.length());
}

TextSelection ShiftItemSelection(
		TextSelection selection,
		const Ui::Text::String &byText) {
	return ShiftItemSelection(selection, byText.length());
}

QString DateTooltipText(not_null<Element*> view) {
	const auto locale = QLocale();
	const auto format = QLocale::LongFormat;
	const auto item = view->data();
	auto dateText = locale.toString(view->dateTime(), format);
	if (const auto editedDate = view->displayedEditDate()) {
		dateText += '\n' + tr::lng_edited_date(
			tr::now,
			lt_date,
			locale.toString(base::unixtime::parse(editedDate), format));
	}
	if (const auto forwarded = item->Get<HistoryMessageForwarded>()) {
		if (!forwarded->story && forwarded->psaType.isEmpty()) {
			dateText += '\n' + tr::lng_forwarded_date(
				tr::now,
				lt_date,
				locale.toString(
					base::unixtime::parse(forwarded->originalDate),
					format));
			if (forwarded->imported) {
				dateText = tr::lng_forwarded_imported(tr::now)
					+ "\n\n" + dateText;
			}
		}
	}
	if (view->isSignedAuthorElided()) {
		if (const auto msgsigned = item->Get<HistoryMessageSigned>()) {
			dateText += '\n'
				+ tr::lng_signed_author(tr::now, lt_user, msgsigned->author);
		}
	}
	return dateText;
}

void UnreadBar::init(const QString &string) {
	text = string;
	width = st::semiboldFont->width(text);
}

int UnreadBar::height() {
	return st::historyUnreadBarHeight + st::historyUnreadBarMargin;
}

int UnreadBar::marginTop() {
	return st::lineWidth + st::historyUnreadBarMargin;
}

void UnreadBar::paint(
		Painter &p,
		const PaintContext &context,
		int y,
		int w,
		bool chatWide) const {
	const auto st = context.st;
	const auto bottom = y + height();
	y += marginTop();
	p.fillRect(
		0,
		y,
		w,
		height() - marginTop() - st::lineWidth,
		st->historyUnreadBarBg());
	p.fillRect(
		0,
		bottom - st::lineWidth,
		w,
		st::lineWidth,
		st->historyUnreadBarBorder());
	p.setFont(st::historyUnreadBarFont);
	p.setPen(st->historyUnreadBarFg());

	int maxwidth = w;
	if (chatWide) {
		maxwidth = qMin(
			maxwidth,
			st::msgMaxWidth
				+ 2 * st::msgPhotoSkip
				+ 2 * st::msgMargin.left());
	}
	w = maxwidth;

	const auto skip = st::historyUnreadBarHeight
		- 2 * st::lineWidth
		- st::historyUnreadBarFont->height;
	p.drawText(
		(w - width) / 2,
		y + (skip / 2) + st::historyUnreadBarFont->ascent,
		text);
}


void DateBadge::init(const QString &date) {
	text = date;
	width = st::msgServiceFont->width(text);
}

int DateBadge::height() const {
	return st::msgServiceMargin.top()
		+ st::msgServicePadding.top()
		+ st::msgServiceFont->height
		+ st::msgServicePadding.bottom()
		+ st::msgServiceMargin.bottom();
}

void DateBadge::paint(
		Painter &p,
		not_null<const Ui::ChatStyle*> st,
		int y,
		int w,
		bool chatWide) const {
	ServiceMessagePainter::PaintDate(p, st, text, width, y, w, chatWide);
}

void FakeBotAboutTop::init() {
	if (!text.isEmpty()) {
		return;
	}
	text.setText(
		st::msgNameStyle,
		tr::lng_bot_description(tr::now),
		Ui::NameTextOptions());
	maxWidth = st::msgPadding.left()
		+ text.maxWidth()
		+ st::msgPadding.right();
	height = st::msgNameStyle.font->height + st::botDescSkip;
}

Element::Element(
	not_null<ElementDelegate*> delegate,
	not_null<HistoryItem*> data,
	Element *replacing,
	Flag serviceFlag)
: _delegate(delegate)
, _data(data)
, _dateTime(IsItemScheduledUntilOnline(data)
	? QDateTime()
	: ItemDateTime(data))
, _text(st::msgMinWidth)
, _flags(serviceFlag
	| Flag::NeedsResize
	| (IsItemScheduledUntilOnline(data)
		? Flag::ScheduledUntilOnline
		: Flag()))
, _context(delegate->elementContext()) {
	history()->owner().registerItemView(this);
	refreshMedia(replacing);
	if (_context == Context::History) {
		history()->setHasPendingResizedItems();
	}
	if (data->isFakeBotAbout() && !data->history()->peer->isRepliesChat()) {
		AddComponents(FakeBotAboutTop::Bit());
	}
}

not_null<ElementDelegate*> Element::delegate() const {
	return _delegate;
}

not_null<HistoryItem*> Element::data() const {
	return _data;
}

not_null<History*> Element::history() const {
	return _data->history();
}

QDateTime Element::dateTime() const {
	return _dateTime;
}

Media *Element::media() const {
	return _media.get();
}

Context Element::context() const {
	return _context;
}

int Element::y() const {
	return _y;
}

void Element::setY(int y) {
	_y = y;
}

void Element::refreshDataIdHook() {
}

void Element::clearSpecialOnlyEmoji() {
	if (!(_flags & Flag::SpecialOnlyEmoji)) {
		return;
	}
	history()->session().emojiStickersPack().remove(this);
	_flags &= ~Flag::SpecialOnlyEmoji;
}

void Element::checkSpecialOnlyEmoji() {
	if (history()->session().emojiStickersPack().add(this)) {
		_flags |= Flag::SpecialOnlyEmoji;
	}
}

void Element::hideSpoilers() {
	if (_text.hasSpoilers()) {
		_text.setSpoilerRevealed(false, anim::type::instant);
	}
	if (_media) {
		_media->hideSpoilers();
	}
}

void Element::customEmojiRepaint() {
	if (!(_flags & Flag::CustomEmojiRepainting)) {
		_flags |= Flag::CustomEmojiRepainting;
		history()->owner().requestViewRepaint(this);
	}
}

void Element::clearCustomEmojiRepaint() const {
	_flags &= ~Flag::CustomEmojiRepainting;
	data()->_flags &= ~MessageFlag::CustomEmojiRepainting;
}

void Element::prepareCustomEmojiPaint(
		Painter &p,
		const PaintContext &context,
		const Ui::Text::String &text) const {
	if (!text.hasPersistentAnimation()) {
		return;
	}
	clearCustomEmojiRepaint();
	p.setInactive(context.paused);
	if (!_heavyCustomEmoji) {
		_heavyCustomEmoji = true;
		history()->owner().registerHeavyViewPart(const_cast<Element*>(this));
	}
}

void Element::prepareCustomEmojiPaint(
		Painter &p,
		const PaintContext &context,
		const Reactions::InlineList &reactions) const {
	if (!reactions.hasCustomEmoji()) {
		return;
	}
	clearCustomEmojiRepaint();
	p.setInactive(context.paused);
	if (!_heavyCustomEmoji) {
		_heavyCustomEmoji = true;
		history()->owner().registerHeavyViewPart(const_cast<Element*>(this));
	}
}

void Element::repaint() const {
	history()->owner().requestViewRepaint(this);
}

void Element::paintHighlight(
		Painter &p,
		const PaintContext &context,
		int geometryHeight) const {
	const auto top = marginTop();
	const auto bottom = marginBottom();
	const auto fill = qMin(top, bottom);
	const auto skiptop = top - fill;
	const auto fillheight = fill + geometryHeight + fill;

	paintCustomHighlight(p, context, skiptop, fillheight, data());
}

void Element::paintCustomHighlight(
		Painter &p,
		const PaintContext &context,
		int y,
		int height,
		not_null<const HistoryItem*> item) const {
	const auto opacity = delegate()->elementHighlightOpacity(item);
	if (opacity == 0.) {
		return;
	}
	const auto o = p.opacity();
	p.setOpacity(o * opacity);
	p.fillRect(
		0,
		y,
		width(),
		height,
		context.st->msgSelectOverlay());
	p.setOpacity(o);
}

bool Element::isUnderCursor() const {
	return _delegate->elementUnderCursor(this);
}

bool Element::isLastAndSelfMessage() const {
	if (!hasOutLayout() || data()->_history->peer->isSelf()) {
		return false;
	}
	if (const auto last = data()->_history->lastMessage()) {
		return last == data();
	}
	return false;
}

void Element::setPendingResize() {
	_flags |= Flag::NeedsResize;
	if (_context == Context::History) {
		data()->_history->setHasPendingResizedItems();
	}
}

bool Element::pendingResize() const {
	return _flags & Flag::NeedsResize;
}

bool Element::isAttachedToPrevious() const {
	return _flags & Flag::AttachedToPrevious;
}

bool Element::isAttachedToNext() const {
	return _flags & Flag::AttachedToNext;
}

bool Element::isBubbleAttachedToPrevious() const {
	return _flags & Flag::BubbleAttachedToPrevious;
}

bool Element::isBubbleAttachedToNext() const {
	return _flags & Flag::BubbleAttachedToNext;
}

bool Element::isTopicRootReply() const {
	return _flags & Flag::TopicRootReply;
}

int Element::skipBlockWidth() const {
	return st::msgDateSpace + infoWidth() - st::msgDateDelta.x();
}

int Element::skipBlockHeight() const {
	return st::msgDateFont->height - st::msgDateDelta.y();
}

int Element::infoWidth() const {
	return 0;
}

int Element::bottomInfoFirstLineWidth() const {
	return 0;
}

bool Element::bottomInfoIsWide() const {
	return false;
}

bool Element::isHiddenByGroup() const {
	return _flags & Flag::HiddenByGroup;
}

bool Element::isHidden() const {
	return isHiddenByGroup();
}

void Element::overrideMedia(std::unique_ptr<Media> media) {
	Expects(!history()->owner().groups().find(data()));

	_text = Ui::Text::String(st::msgMinWidth);
	_textWidth = -1;
	_textHeight = 0;

	_media = std::move(media);
	if (!pendingResize()) {
		history()->owner().requestViewResize(this);
	}
	_flags |= Flag::MediaOverriden;
}

void Element::refreshMedia(Element *replacing) {
	if (_flags & Flag::MediaOverriden) {
		return;
	}
	_flags &= ~Flag::HiddenByGroup;

	const auto item = data();
	if (const auto media = item->media()) {
		if (media->canBeGrouped()) {
			if (const auto group = history()->owner().groups().find(item)) {
				if (group->items.front() != item) {
					_media = nullptr;
					_flags |= Flag::HiddenByGroup;
				} else {
					_media = std::make_unique<GroupedMedia>(
						this,
						group->items);
					if (!pendingResize()) {
						history()->owner().requestViewResize(this);
					}
				}
				return;
			}
		}
		_media = media->createView(this, replacing);
	} else if (isOnlyCustomEmoji()
		&& Core::App().settings().largeEmoji()) {
		_media = std::make_unique<UnwrappedMedia>(
			this,
			std::make_unique<CustomEmoji>(this, onlyCustomEmoji()));
	} else if (isIsolatedEmoji()
		&& Core::App().settings().largeEmoji()) {
		const auto emoji = isolatedEmoji();
		const auto emojiStickers = &history()->session().emojiStickersPack();
		const auto skipPremiumEffect = false;
		if (const auto sticker = emojiStickers->stickerForEmoji(emoji)) {
			_media = std::make_unique<UnwrappedMedia>(
				this,
				std::make_unique<Sticker>(
					this,
					sticker.document,
					skipPremiumEffect,
					replacing,
					sticker.replacements));
		} else {
			_media = std::make_unique<UnwrappedMedia>(
				this,
				std::make_unique<LargeEmoji>(this, emoji));
		}
	} else {
		_media = nullptr;
	}
}

Ui::Text::IsolatedEmoji Element::isolatedEmoji() const {
	return _text.toIsolatedEmoji();
}

Ui::Text::OnlyCustomEmoji Element::onlyCustomEmoji() const {
	return _text.toOnlyCustomEmoji();
}

const Ui::Text::String &Element::text() const {
	return _text;
}

OnlyEmojiAndSpaces Element::isOnlyEmojiAndSpaces() const {
	if (data()->Has<HistoryMessageTranslation>()) {
		return OnlyEmojiAndSpaces::No;
	} else if (!_text.isEmpty()) {
		return _text.hasNotEmojiAndSpaces()
			? OnlyEmojiAndSpaces::No
			: OnlyEmojiAndSpaces::Yes;
	} else if (data()->originalText().empty()) {
		return OnlyEmojiAndSpaces::Yes;
	} else {
		return OnlyEmojiAndSpaces::Unknown;
	}
}

int Element::textHeightFor(int textWidth) {
	validateText();
	if (_textWidth != textWidth) {
		_textWidth = textWidth;
		_textHeight = _text.countHeight(textWidth);
	}
	return _textHeight;
}

auto Element::contextDependentServiceText() -> TextWithLinks {
	const auto item = data();
	const auto info = item->Get<HistoryServiceTopicInfo>();
	if (!info) {
		return {};
	}
	if (_delegate->elementContext() == Context::Replies) {
		if (info->created()) {
			return { { tr::lng_action_topic_created_inside(tr::now) } };
		}
		return {};
	} else if (info->created()) {
		return{};
	}
	const auto peerId = item->history()->peer->id;
	const auto topicRootId = item->topicRootId();
	if (!peerIsChannel(peerId)) {
		return {};
	}
	const auto from = item->from();
	const auto topicUrl =  u"internal:url:https://t.me/c/%1/%2"_q
		.arg(peerToChannel(peerId).bare)
		.arg(topicRootId.bare);
	const auto fromLink = [&](int index) {
		return Ui::Text::Link(from->name(), index);
	};
	const auto placeholderLink = [&] {
		return Ui::Text::Link(
			tr::lng_action_topic_placeholder(tr::now),
			topicUrl);
	};
	const auto wrapTopic = [&](
			const QString &title,
			std::optional<DocumentId> iconId) {
		return Ui::Text::Link(
			Data::ForumTopicIconWithTitle(
				topicRootId,
				iconId.value_or(0),
				title),
			topicUrl);
	};
	const auto wrapParentTopic = [&] {
		const auto forum = history()->asForum();
		if (!forum || forum->topicDeleted(topicRootId)) {
			return wrapTopic(
				tr::lng_deleted_message(tr::now),
				std::nullopt);
		} else if (const auto topic = forum->topicFor(topicRootId)) {
			return wrapTopic(topic->title(), topic->iconId());
		} else {
			forum->requestTopic(topicRootId, crl::guard(this, [=] {
				itemTextUpdated();
				history()->owner().requestViewResize(this);
			}));
			return wrapTopic(
				tr::lng_profile_loading(tr::now),
				std::nullopt);
		}
	};

	if (info->closed) {
		return {
			tr::lng_action_topic_closed(
				tr::now,
				lt_topic,
				wrapParentTopic(),
				Ui::Text::WithEntities),
		};
	} else if (info->reopened) {
		return {
			tr::lng_action_topic_reopened(
				tr::now,
				lt_topic,
				wrapParentTopic(),
				Ui::Text::WithEntities),
		};
	} else if (info->hidden) {
		return {
			tr::lng_action_topic_hidden(
				tr::now,
				lt_topic,
				wrapParentTopic(),
				Ui::Text::WithEntities),
		};
	} else if (info->unhidden) {
		return {
			tr::lng_action_topic_unhidden(
				tr::now,
				lt_topic,
				wrapParentTopic(),
				Ui::Text::WithEntities),
		};
	} else if (info->renamed) {
		return {
			tr::lng_action_topic_renamed(
				tr::now,
				lt_from,
				fromLink(1),
				lt_link,
				placeholderLink(),
				lt_title,
				wrapTopic(
					info->title,
					(info->reiconed
						? info->iconId
						: std::optional<DocumentId>())),
				Ui::Text::WithEntities),
			{ from->createOpenLink() },
		};
	} else if (info->reiconed) {
		if (const auto iconId = info->iconId) {
			return {
				tr::lng_action_topic_icon_changed(
					tr::now,
					lt_from,
					fromLink(1),
					lt_link,
					placeholderLink(),
					lt_emoji,
					Data::SingleCustomEmoji(iconId),
					Ui::Text::WithEntities),
				{ from->createOpenLink() },
			};
		} else {
			return {
				tr::lng_action_topic_icon_removed(
					tr::now,
					lt_from,
					fromLink(1),
					lt_link,
					placeholderLink(),
					Ui::Text::WithEntities),
				{ from->createOpenLink() },
			};
		}
	} else {
		return {};
	}
}

void Element::validateText() {
	const auto item = data();
	const auto &text = item->_text;
	const auto media = item->media();
	const auto storyMention = media && media->storyMention();
	if (media && media->storyExpired()) {
		_media = nullptr;
		if (!storyMention) {
			if (_text.isEmpty()) {
				setTextWithLinks(Ui::Text::Italic(
					tr::lng_forwarded_story_expired(tr::now)));
			}
			return;
		}
	}
	if (_text.isEmpty() == text.empty()) {
	} else if (_flags & Flag::ServiceMessage) {
		const auto contextDependentText = contextDependentServiceText();
		const auto &markedText = contextDependentText.text.empty()
			? text
			: contextDependentText.text;
		const auto &customLinks = contextDependentText.text.empty()
			? item->customTextLinks()
			: contextDependentText.links;
		setTextWithLinks(markedText, customLinks);
	} else {
		setTextWithLinks(item->translatedTextWithLocalEntities());
	}
}

void Element::setTextWithLinks(
		const TextWithEntities &text,
		const std::vector<ClickHandlerPtr> &links) {
	const auto context = Core::MarkedTextContext{
		.session = &history()->session(),
		.customEmojiRepaint = [=] { customEmojiRepaint(); },
	};
	if (_flags & Flag::ServiceMessage) {
		const auto &options = Ui::ItemTextServiceOptions();
		_text.setMarkedText(st::serviceTextStyle, text, options, context);
		auto linkIndex = 0;
		for (const auto &link : links) {
			// Link indices start with 1.
			_text.setLink(++linkIndex, link);
		}
	} else {
		const auto item = data();
		const auto &options = Ui::ItemTextOptions(item);
		clearSpecialOnlyEmoji();
		_text.setMarkedText(st::messageTextStyle, text, options, context);
		if (!item->_text.empty() && _text.isEmpty()){
			// If server has allowed some text that we've trim-ed entirely,
			// just replace it with something so that UI won't look buggy.
			_text.setMarkedText(
				st::messageTextStyle,
				{ u":-("_q },
				Ui::ItemTextOptions(item));
		}
		if (!item->media()) {
			checkSpecialOnlyEmoji();
			refreshMedia(nullptr);
		}
	}
	FillTextWithAnimatedSpoilers(this, _text);
	_textWidth = -1;
	_textHeight = 0;
}

void Element::validateTextSkipBlock(bool has, int width, int height) {
	validateText();
	if (!has) {
		if (_text.removeSkipBlock()) {
			_textWidth = -1;
			_textHeight = 0;
		}
	} else if (_text.updateSkipBlock(width, height)) {
		_textWidth = -1;
		_textHeight = 0;
	}
}

void Element::previousInBlocksChanged() {
	recountDisplayDateInBlocks();
	recountAttachToPreviousInBlocks();
}

void Element::nextInBlocksRemoved() {
	setAttachToNext(false);
}

bool Element::markSponsoredViewed(int shownFromTop) const {
	const auto sponsoredTextTop = height()
		- st::msgPadding.bottom()
		- st::historyViewButtonHeight;
	return shownFromTop >= sponsoredTextTop;
}

void Element::refreshDataId() {
	if (const auto media = this->media()) {
		media->refreshParentId(data());
	}
	refreshDataIdHook();
}

bool Element::computeIsAttachToPrevious(not_null<Element*> previous) {
	const auto mayBeAttached = [](not_null<Element*> view) {
		const auto item = view->data();
		return !item->isService()
			&& !item->isEmpty()
			&& !item->isPost()
			&& (!item->history()->peer->isMegagroup()
				|| !view->hasOutLayout()
				|| !item->from()->isChannel());
	};
	const auto item = data();
	if (!Has<DateBadge>() && !Has<UnreadBar>()) {
		const auto prev = previous->data();
		const auto previousMarkup = prev->inlineReplyMarkup();
		const auto possible = (std::abs(prev->date() - item->date())
				< kAttachMessageToPreviousSecondsDelta)
			&& mayBeAttached(this)
			&& mayBeAttached(previous)
			&& (!previousMarkup || previousMarkup->hiddenBy(prev->media()))
			&& (item->topicRootId() == prev->topicRootId());
		if (possible) {
			const auto forwarded = item->Get<HistoryMessageForwarded>();
			const auto prevForwarded = prev->Get<HistoryMessageForwarded>();
			if (item->history()->peer->isSelf()
				|| item->history()->peer->isRepliesChat()
				|| (forwarded && forwarded->imported)
				|| (prevForwarded && prevForwarded->imported)) {
				return IsAttachedToPreviousInSavedMessages(
					prev,
					prevForwarded,
					item,
					forwarded);
			} else {
				return prev->from() == item->from();
			}
		}
	}
	return false;
}

ClickHandlerPtr Element::fromLink() const {
	if (_fromLink) {
		return _fromLink;
	}
	const auto item = data();
	if (item->isSponsored()) {
		const auto session = &item->history()->session();
		_fromLink = std::make_shared<LambdaClickHandler>([=](
				ClickContext context) {
			if (context.button != Qt::LeftButton) {
				return;
			}
			const auto my = context.other.value<ClickHandlerContext>();
			if (const auto window = ContextOrSessionWindow(my, session)) {
				auto &sponsored = session->data().sponsoredMessages();
				const auto itemId = my.itemId ? my.itemId : item->fullId();
				const auto details = sponsored.lookupDetails(itemId);
				if (!details.externalLink.isEmpty()) {
					File::OpenUrl(details.externalLink);
				} else if (const auto &hash = details.hash) {
					Api::CheckChatInvite(window, *hash);
				} else if (const auto peer = details.peer) {
					window->showPeerInfo(peer);
				}
			}
		});
		return _fromLink;
	} else if (const auto from = item->displayFrom()) {
		_fromLink = std::make_shared<LambdaClickHandler>([=](
				ClickContext context) {
			if (context.button != Qt::LeftButton) {
				return;
			}
			const auto my = context.other.value<ClickHandlerContext>();
			const auto session = &from->session();
			if (const auto window = ContextOrSessionWindow(my, session)) {
				window->showPeerInfo(from);
			}
		});
		_fromLink->setProperty(kPeerLinkPeerIdProperty, from->id.value);
		return _fromLink;
	}
	if (const auto forwarded = item->Get<HistoryMessageForwarded>()) {
		if (forwarded->imported) {
			static const auto imported = std::make_shared<LambdaClickHandler>([](
					ClickContext context) {
				const auto my = context.other.value<ClickHandlerContext>();
				const auto weak = my.sessionWindow;
				if (const auto strong = weak.get()) {
					strong->showToast(tr::lng_forwarded_imported(tr::now));
				}
			});
			return imported;
		}
	}
	_fromLink = HiddenSenderInfo::ForwardClickHandler();
	return _fromLink;
}

void Element::createUnreadBar(rpl::producer<QString> text) {
	if (!AddComponents(UnreadBar::Bit())) {
		return;
	}
	const auto bar = Get<UnreadBar>();
	std::move(
		text
	) | rpl::start_with_next([=](const QString &text) {
		if (const auto bar = Get<UnreadBar>()) {
			bar->init(text);
		}
	}, bar->lifetime);
	if (data()->mainView() == this) {
		recountAttachToPreviousInBlocks();
	}
	history()->owner().requestViewResize(this);
}

void Element::destroyUnreadBar() {
	if (!Has<UnreadBar>()) {
		return;
	}
	RemoveComponents(UnreadBar::Bit());
	if (data()->mainView() == this) {
		recountAttachToPreviousInBlocks();
	}
	history()->owner().requestViewResize(this);
}

int Element::displayedDateHeight() const {
	if (auto date = Get<DateBadge>()) {
		return date->height();
	}
	return 0;
}

bool Element::displayDate() const {
	return Has<DateBadge>();
}

bool Element::isInOneDayWithPrevious() const {
	return !data()->isEmpty() && !displayDate();
}

void Element::recountAttachToPreviousInBlocks() {
	if (isHidden() || data()->isEmpty()) {
		if (const auto next = nextDisplayedInBlocks()) {
			next->recountAttachToPreviousInBlocks();
		} else if (const auto previous = previousDisplayedInBlocks()) {
			previous->setAttachToNext(false);
		}
		return;
	}
	auto attachToPrevious = false;
	const auto previous = previousDisplayedInBlocks();
	if (previous) {
		attachToPrevious = computeIsAttachToPrevious(previous);
		previous->setAttachToNext(attachToPrevious, this);
	}
	setAttachToPrevious(attachToPrevious, previous);
}

void Element::recountDisplayDateInBlocks() {
	setDisplayDate([&] {
		const auto item = data();
		if (isHidden() || item->isEmpty()) {
			return false;
		}
		if (item->isSponsored()) {
			return false;
		}

		if (const auto previous = previousDisplayedInBlocks()) {
			const auto prev = previous->data();
			return prev->isEmpty()
				|| (previous->dateTime().date() != dateTime().date());
		}
		return true;
	}());
}

QSize Element::countOptimalSize() {
	_flags &= ~Flag::NeedsResize;
	return performCountOptimalSize();
}

QSize Element::countCurrentSize(int newWidth) {
	if (_flags & Flag::NeedsResize) {
		initDimensions();
	}
	return performCountCurrentSize(newWidth);
}

void Element::refreshIsTopicRootReply() {
	const auto topicRootReply = countIsTopicRootReply();
	if (topicRootReply) {
		_flags |= Flag::TopicRootReply;
	} else {
		_flags &= ~Flag::TopicRootReply;
	}
}

bool Element::countIsTopicRootReply() const {
	const auto item = data();
	if (!item->history()->isForum()) {
		return false;
	}
	const auto replyTo = item->replyToId();
	return !replyTo || (item->topicRootId() == replyTo);
}

void Element::setDisplayDate(bool displayDate) {
	const auto item = data();
	if (displayDate && !Has<DateBadge>()) {
		AddComponents(DateBadge::Bit());
		Get<DateBadge>()->init(
			ItemDateText(item, (_flags & Flag::ScheduledUntilOnline)));
		setPendingResize();
	} else if (!displayDate && Has<DateBadge>()) {
		RemoveComponents(DateBadge::Bit());
		setPendingResize();
	}
}

void Element::setAttachToNext(bool attachToNext, Element *next) {
	Expects(next || !attachToNext);

	auto pending = false;
	if (attachToNext && !(_flags & Flag::AttachedToNext)) {
		_flags |= Flag::AttachedToNext;
		pending = true;
	} else if (!attachToNext && (_flags & Flag::AttachedToNext)) {
		_flags &= ~Flag::AttachedToNext;
		pending = true;
	}
	const auto bubble = attachToNext && !next->unwrapped();
	if (bubble && !(_flags & Flag::BubbleAttachedToNext)) {
		_flags |= Flag::BubbleAttachedToNext;
		pending = true;
	} else if (!bubble && (_flags & Flag::BubbleAttachedToNext)) {
		_flags &= ~Flag::BubbleAttachedToNext;
		pending = true;
	}
	if (pending) {
		setPendingResize();
	}
}

void Element::setAttachToPrevious(bool attachToPrevious, Element *previous) {
	Expects(previous || !attachToPrevious);

	auto pending = false;
	if (attachToPrevious && !(_flags & Flag::AttachedToPrevious)) {
		_flags |= Flag::AttachedToPrevious;
		pending = true;
	} else if (!attachToPrevious && (_flags & Flag::AttachedToPrevious)) {
		_flags &= ~Flag::AttachedToPrevious;
		pending = true;
	}
	const auto bubble = attachToPrevious && !previous->unwrapped();
	if (bubble && !(_flags & Flag::BubbleAttachedToPrevious)) {
		_flags |= Flag::BubbleAttachedToPrevious;
		pending = true;
	} else if (!bubble && (_flags & Flag::BubbleAttachedToPrevious)) {
		_flags &= ~Flag::BubbleAttachedToPrevious;
		pending = true;
	}
	if (pending) {
		setPendingResize();
	}
}

bool Element::displayFromPhoto() const {
	return false;
}

bool Element::hasFromPhoto() const {
	return false;
}

bool Element::hasFromName() const {
	return false;
}

bool Element::displayFromName() const {
	return false;
}

TopicButton *Element::displayedTopicButton() const {
	return nullptr;
}

bool Element::displayForwardedFrom() const {
	return false;
}

bool Element::hasOutLayout() const {
	return false;
}

bool Element::drawBubble() const {
	return false;
}

bool Element::hasBubble() const {
	return false;
}

bool Element::unwrapped() const {
	return true;
}

bool Element::hasFastReply() const {
	return false;
}

bool Element::displayFastReply() const {
	return false;
}

std::optional<QSize> Element::rightActionSize() const {
	return std::nullopt;
}

void Element::drawRightAction(
	Painter &p,
	const PaintContext &context,
	int left,
	int top,
	int outerWidth) const {
}

ClickHandlerPtr Element::rightActionLink(
		std::optional<QPoint> pressPoint) const {
	return ClickHandlerPtr();
}

TimeId Element::displayedEditDate() const {
	return TimeId(0);
}

HistoryMessageReply *Element::displayedReply() const {
	return nullptr;
}

bool Element::toggleSelectionByHandlerClick(
	const ClickHandlerPtr &handler) const {
	return false;
}

bool Element::hasVisibleText() const {
	return false;
}

auto Element::verticalRepaintRange() const -> VerticalRepaintRange {
	return {
		.top = 0,
		.height = height()
	};
}

bool Element::hasHeavyPart() const {
	return _heavyCustomEmoji;
}

void Element::checkHeavyPart() {
	if (!hasHeavyPart() && (!_media || !_media->hasHeavyPart())) {
		history()->owner().unregisterHeavyViewPart(this);
	}
}

bool Element::isSignedAuthorElided() const {
	return false;
}

void Element::itemDataChanged() {
}

void Element::itemTextUpdated() {
	if (const auto media = _media.get()) {
		media->parentTextUpdated();
	}
	clearSpecialOnlyEmoji();
	_text = Ui::Text::String(st::msgMinWidth);
	_textWidth = -1;
	_textHeight = 0;
	if (_media && !data()->media()) {
		refreshMedia(nullptr);
	}
}

void Element::unloadHeavyPart() {
	history()->owner().unregisterHeavyViewPart(this);
	if (_media) {
		_media->unloadHeavyPart();
	}
	if (_heavyCustomEmoji) {
		_heavyCustomEmoji = false;
		_text.unloadPersistentAnimation();
		if (const auto reply = data()->Get<HistoryMessageReply>()) {
			reply->replyToText.unloadPersistentAnimation();
		}
	}
}

HistoryBlock *Element::block() {
	return _block;
}

const HistoryBlock *Element::block() const {
	return _block;
}

void Element::attachToBlock(not_null<HistoryBlock*> block, int index) {
	Expects(_data->isHistoryEntry());
	Expects(_block == nullptr);
	Expects(_indexInBlock < 0);
	Expects(index >= 0);

	_block = block;
	_indexInBlock = index;
	_data->setMainView(this);
	previousInBlocksChanged();
}

void Element::removeFromBlock() {
	Expects(_block != nullptr);

	_block->remove(this);
}

void Element::refreshInBlock() {
	Expects(_block != nullptr);

	_block->refreshView(this);
}

void Element::setIndexInBlock(int index) {
	Expects(_block != nullptr);
	Expects(index >= 0);

	_indexInBlock = index;
}

int Element::indexInBlock() const {
	Expects((_indexInBlock >= 0) == (_block != nullptr));
	Expects((_block == nullptr) || (_block->messages[_indexInBlock].get() == this));

	return _indexInBlock;
}

Element *Element::previousInBlocks() const {
	if (_block && _indexInBlock >= 0) {
		if (_indexInBlock > 0) {
			return _block->messages[_indexInBlock - 1].get();
		}
		if (auto previous = _block->previousBlock()) {
			Assert(!previous->messages.empty());
			return previous->messages.back().get();
		}
	}
	return nullptr;
}

Element *Element::previousDisplayedInBlocks() const {
	auto result = previousInBlocks();
	while (result && (result->data()->isEmpty() || result->isHidden())) {
		result = result->previousInBlocks();
	}
	return result;
}

Element *Element::nextInBlocks() const {
	if (_block && _indexInBlock >= 0) {
		if (_indexInBlock + 1 < _block->messages.size()) {
			return _block->messages[_indexInBlock + 1].get();
		}
		if (auto next = _block->nextBlock()) {
			Assert(!next->messages.empty());
			return next->messages.front().get();
		}
	}
	return nullptr;
}

Element *Element::nextDisplayedInBlocks() const {
	auto result = nextInBlocks();
	while (result && (result->data()->isEmpty() || result->isHidden())) {
		result = result->nextInBlocks();
	}
	return result;
}

void Element::drawInfo(
	Painter &p,
	const PaintContext &context,
	int right,
	int bottom,
	int width,
	InfoDisplayType type) const {
}

TextState Element::bottomInfoTextState(
		int right,
		int bottom,
		QPoint point,
		InfoDisplayType type) const {
	return TextState();
}

TextSelection Element::adjustSelection(
		TextSelection selection,
		TextSelectType type) const {
	return selection;
}

Reactions::ButtonParameters Element::reactionButtonParameters(
		QPoint position,
		const TextState &reactionState) const {
	return {};
}

int Element::reactionsOptimalWidth() const {
	return 0;
}

void Element::clickHandlerActiveChanged(
		const ClickHandlerPtr &handler,
		bool active) {
	if (const auto markup = _data->Get<HistoryMessageReplyMarkup>()) {
		if (const auto keyboard = markup->inlineKeyboard.get()) {
			keyboard->clickHandlerActiveChanged(handler, active);
		}
	}
	HoveredLink(active ? this : nullptr);
	repaint();
	if (const auto media = this->media()) {
		media->clickHandlerActiveChanged(handler, active);
	}
}

void Element::clickHandlerPressedChanged(
		const ClickHandlerPtr &handler,
		bool pressed) {
	PressedLink(pressed ? this : nullptr);
	repaint();
	if (const auto media = this->media()) {
		media->clickHandlerPressedChanged(handler, pressed);
	}
}

void Element::animateReaction(Ui::ReactionFlyAnimationArgs &&args) {
}

void Element::animateUnreadReactions() {
	const auto &recent = data()->recentReactions();
	for (const auto &[id, list] : recent) {
		if (ranges::contains(list, true, &Data::RecentReaction::unread)) {
			animateReaction({ .id = id });
		}
	}
}

auto Element::takeReactionAnimations()
-> base::flat_map<
		Data::ReactionId,
		std::unique_ptr<Ui::ReactionFlyAnimation>> {
	return {};
}

Element::~Element() {
	// Delete media while owner still exists.
	clearSpecialOnlyEmoji();
	base::take(_media);
	if (_heavyCustomEmoji) {
		_heavyCustomEmoji = false;
		_text.unloadPersistentAnimation();
		checkHeavyPart();
	}
	if (_data->mainView() == this) {
		_data->clearMainView();
	}
	if (_context == Context::History) {
		history()->owner().notifyViewRemoved(this);
	}
	history()->owner().unregisterItemView(this);
}

void Element::Hovered(Element *view) {
	HoveredElement = view;
}

Element *Element::Hovered() {
	return HoveredElement;
}

void Element::Pressed(Element *view) {
	PressedElement = view;
}

Element *Element::Pressed() {
	return PressedElement;
}

void Element::HoveredLink(Element *view) {
	HoveredLinkElement = view;
}

Element *Element::HoveredLink() {
	return HoveredLinkElement;
}

void Element::PressedLink(Element *view) {
	PressedLinkElement = view;
}

Element *Element::PressedLink() {
	return PressedLinkElement;
}

void Element::Moused(Element *view) {
	MousedElement = view;
}

Element *Element::Moused() {
	return MousedElement;
}

void Element::ClearGlobal() {
	HoveredElement = nullptr;
	PressedElement = nullptr;
	HoveredLinkElement = nullptr;
	PressedLinkElement = nullptr;
	MousedElement = nullptr;
}

} // namespace HistoryView
