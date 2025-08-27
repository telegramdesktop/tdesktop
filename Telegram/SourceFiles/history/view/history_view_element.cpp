/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_element.h"

#include "history/view/history_view_service_message.h"
#include "history/view/history_view_message.h"
#include "history/view/media/history_view_media_generic.h"
#include "history/view/media/history_view_media_grouped.h"
#include "history/view/media/history_view_similar_channels.h"
#include "history/view/media/history_view_sticker.h"
#include "history/view/media/history_view_large_emoji.h"
#include "history/view/media/history_view_custom_emoji.h"
#include "history/view/media/history_view_suggest_decision.h"
#include "history/view/reactions/history_view_reactions_button.h"
#include "history/view/reactions/history_view_reactions.h"
#include "history/view/history_view_cursor_state.h"
#include "history/view/history_view_reply.h"
#include "history/view/history_view_text_helper.h"
#include "history/history.h"
#include "history/history_item_components.h"
#include "history/history_item_helpers.h"
#include "base/unixtime.h"
#include "boxes/premium_preview_box.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "core/click_handler_types.h"
#include "core/ui_integration.h"
#include "main/main_app_config.h"
#include "main/main_session.h"
#include "chat_helpers/stickers_emoji_pack.h"
#include "payments/payments_reaction_process.h" // TryAddingPaidReaction.
#include "window/window_session_controller.h"
#include "ui/effects/path_shift_gradient.h"
#include "ui/effects/reaction_fly_animation.h"
#include "ui/toast/toast.h"
#include "ui/text/text_utilities.h"
#include "ui/item_text_options.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "data/components/sponsored_messages.h"
#include "data/data_channel.h"
#include "data/data_saved_sublist.h"
#include "data/data_session.h"
#include "data/data_todo_list.h"
#include "data/data_forum.h"
#include "data/data_forum_topic.h"
#include "data/data_message_reactions.h"
#include "data/data_user.h"
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
	const auto sender = previous->displayFrom();
	if ((prevForwarded != nullptr) != (forwarded != nullptr)) {
		return false;
	} else if (sender != item->displayFrom()) {
		return false;
	} else if (!prevForwarded || sender) {
		return true;
	}
	const auto previousInfo = prevForwarded->savedFromHiddenSenderInfo
		? prevForwarded->savedFromHiddenSenderInfo.get()
		: prevForwarded->originalHiddenSenderInfo.get();
	const auto itemInfo = forwarded->savedFromHiddenSenderInfo
		? forwarded->savedFromHiddenSenderInfo.get()
		: forwarded->originalHiddenSenderInfo.get();
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

[[nodiscard]] TextSelection ApplyModificationsFrom(
		TextSelection result,
		const Ui::Text::String &text) {
	if (result.empty()) {
		return result;
	}
	for (const auto &modification : text.modifications()) {
		if (modification.position >= result.to) {
			break;
		}
		if (modification.added) {
			++result.to;
		}
		const auto shiftTo = std::min(
			int(modification.skipped),
			result.to - modification.position);
		result.to -= shiftTo;
		if (modification.position <= result.from) {
			if (modification.added) {
				++result.from;
			}
			const auto shiftFrom = std::min(
				int(modification.skipped),
				result.from - modification.position);
			result.from -= shiftFrom;
		}
	}
	return result;
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

SelectionModeResult DefaultElementDelegate::elementInSelectionMode(
		const Element *view) {
	return {};
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

void DefaultElementDelegate::elementSearchInList(
	const QString &query,
	const FullMsgId &context) {
}

void DefaultElementDelegate::elementHandleViaClick(
	not_null<UserData*> bot) {
}

ElementChatMode DefaultElementDelegate::elementChatMode() {
	return ElementChatMode::Default;
}

void DefaultElementDelegate::elementReplyTo(const FullReplyTo &to) {
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

void DefaultElementDelegate::elementStartEffect(
	not_null<const Element*> view,
	Element *replacing) {
}

QString DefaultElementDelegate::elementAuthorRank(
		not_null<const Element*> view) {
	return {};
}

bool DefaultElementDelegate::elementHideTopicButton(
		not_null<const Element*> view) {
	return true;
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
	if (item->awaitingVideoProcessing()) {
		dateText += '\n' + tr::lng_approximate_about(tr::now);
	}
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
			dateText += '\n' + tr::lng_signed_author(
				tr::now,
				lt_user,
				msgsigned->author);
		}
	}
	if (item->isScheduled() && item->isSilent()) {
		dateText += '\n' + QChar(0xD83D) + QChar(0xDD15);
	}
	if (const auto stars = item->out() ? item->starsPaid() : 0) {
		dateText += '\n' + tr::lng_you_paid_stars(tr::now, lt_count, stars);
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
		ElementChatMode mode) const {
	const auto previousTranslation = p.transform().dx();
	if (previousTranslation != 0) {
		p.translate(-previousTranslation, 0);
	}
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
	if (mode == ElementChatMode::Wide) {
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
	if (previousTranslation != 0) {
		p.translate(previousTranslation, 0);
	}
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

void ForumThreadBar::init(
		not_null<PeerData*> parentChat,
		not_null<Data::Thread*> thread) {
	this->thread = thread;
	const auto sublist = thread->asSublist();
	if (sublist) {
		text.setText(st::semiboldTextStyle, sublist->sublistPeer()->name());
	} else if (const auto topic = thread->asTopic()) {
		text.setMarkedText(
			st::semiboldTextStyle,
			topic->titleWithIconOrLogo(),
			kMarkupTextOptions,
			Core::TextContext({ .session = &topic->session() }));
	}
	const auto skip = st::monoforumBarUserpicSkip;
	const auto userpic = sublist
		? (st::msgServicePadding.top()
			+ st::msgServiceFont->height
			+ st::msgServicePadding.bottom()
			- 2 * skip)
		: (st::msgServicePadding.left() - 3 * skip);

	width = skip
		+ userpic
		+ skip * 2
		+ text.maxWidth()
		+ st::topicButtonArrowSkip
		+ st::msgServicePadding.right();
}

int ForumThreadBar::height() const {
	return st::msgServiceMargin.top()
		+ st::msgServicePadding.top()
		+ st::msgServiceFont->height
		+ st::msgServicePadding.bottom()
		+ st::msgServiceMargin.bottom();
}

void ForumThreadBar::paint(
		Painter &p,
		not_null<const Ui::ChatStyle*> st,
		int y,
		int w,
		bool chatWide,
		bool skipPatternLine) const {
	if (const auto strong = thread.get()) {
		Paint(
			p,
			st,
			strong,
			text,
			width,
			view,
			y,
			w,
			chatWide,
			skipPatternLine);
	}
}

int ForumThreadBar::PaintForGetWidth(
		Painter &p,
		not_null<const Ui::ChatStyle*> st,
		not_null<Element*> itemView,
		Ui::PeerUserpicView &userpicView,
		int y,
		int w,
		bool chatWide) {
	const auto item = itemView->data();
	const auto topic = item->topic();
	const auto sublist = item->savedSublist();
	const auto sender = topic
		? (Data::Thread*)topic
		: (sublist && sublist->parentChat())
		? (Data::Thread*)sublist
		: nullptr;
	auto text = Ui::Text::String();
	if (!sender
		|| !topic
		|| (sublist && sublist->sublistPeer()->isMonoforum())) {
		return 0;
	} else if (topic) {
		text.setMarkedText(
			st::semiboldTextStyle,
			topic->titleWithIconOrLogo(),
			kMarkupTextOptions,
			Core::TextContext({ .session = &topic->session() }));
	} else {
		text.setText(st::semiboldTextStyle, sublist->sublistPeer()->name());
	}
	const auto skip = st::monoforumBarUserpicSkip;
	const auto userpic = sublist
		? (st::msgServicePadding.top()
			+ st::msgServiceFont->height
			+ st::msgServicePadding.bottom()
			- 2 * skip)
		: (st::msgServicePadding.left() - 3 * skip);
	const auto width = skip
		+ userpic
		+ skip * 2
		+ text.maxWidth()
		+ st::topicButtonArrowSkip
		+ st::msgServicePadding.right();
	Paint(p, st, sender, text, width, userpicView, y, w, chatWide, true);
	return width;
}

void ForumThreadBar::Paint(
		Painter &p,
		not_null<const Ui::ChatStyle*> st,
		not_null<Data::Thread*> thread,
		const Ui::Text::String &text,
		int width,
		Ui::PeerUserpicView &view,
		int y,
		int w,
		bool chatWide,
		bool skipPatternLine) {
	int left = st::msgServiceMargin.left();
	const auto maxwidth = chatWide
		? std::min(w, WideChatWidth())
		: w;
	w = maxwidth - st::msgServiceMargin.left() - st::msgServiceMargin.left();

	const auto use = std::min(w, width);

	left += (w - use) / 2;
	int h = st::msgServicePadding.top() + st::msgServiceFont->height + st::msgServicePadding.bottom();
	ServiceMessagePainter::PaintBubble(
		p,
		st->msgServiceBg(),
		st->serviceBgCornersNormal(),
		QRect(left, y + st::msgServiceMargin.top(), use, h));

	const auto skip = st::monoforumBarUserpicSkip;
	if (!skipPatternLine) {
		auto pen = st->msgServiceBg()->p;
		pen.setWidthF(skip);
		pen.setCapStyle(Qt::RoundCap);
		pen.setDashPattern({ 2., 2. });
		p.setPen(pen);
		const auto top = y + st::msgServiceMargin.top() + (h / 2);
		p.drawLine(0, top, left, top);
		p.drawLine(left + use, top, 2 * w, top);
	}

	const auto sublist = thread->asSublist();
	const auto userpic = sublist
		? (st::msgServicePadding.top()
			+ st::msgServiceFont->height
			+ st::msgServicePadding.bottom()
			- 2 * skip)
		: (st::msgServicePadding.left() - 3 * skip);
	const auto available = use
		- (skip
			+ userpic
			+ skip * 2
			+ st::topicButtonArrowSkip
			+ st::msgServicePadding.right());

	if (sublist) {
		sublist->sublistPeer()->paintUserpic(
			p,
			view,
			left + skip,
			y + st::msgServiceMargin.top() + skip,
			userpic);
	}

	const auto textLeft = left + skip + userpic + skip * 2;
	const auto textTop = y
		+ st::msgServiceMargin.top()
		+ st::msgServicePadding.top();
	p.setFont(st::msgServiceFont);
	p.setPen(st->msgServiceFg());
	text.draw(p, {
		.position = { textLeft, textTop },
		.availableWidth = available,
		.elisionLines = 1,
	});

	st::topicButtonArrow.paint(
		p,
		textLeft + available + st::topicButtonArrowPosition.x(),
		textTop + st::topicButtonArrowPosition.y(),
		w,
		st->msgServiceFg()->c);
}

void ServicePreMessage::init(
		not_null<Element*> view,
		PreparedServiceText string,
		ClickHandlerPtr fullClickHandler,
		std::unique_ptr<Media> media) {
	text = Ui::Text::String(
		st::serviceTextStyle,
		string.text,
		kMarkupTextOptions,
		st::msgMinWidth,
		Core::TextContext({
			.session = &view->history()->session(),
			.repaint = [=] { view->customEmojiRepaint(); },
		}));
	handler = std::move(fullClickHandler);
	for (auto i = 0; i != int(string.links.size()); ++i) {
		text.setLink(i + 1, string.links[i]);
	}
	this->media = std::move(media);
}

int ServicePreMessage::resizeToWidth(int newWidth, ElementChatMode mode) {
	width = newWidth;
	if (mode == ElementChatMode::Wide) {
		accumulate_min(
			width,
			st::msgMaxWidth + 2 * st::msgPhotoSkip + 2 * st::msgMargin.left());
	}

	if (media) {
		media->initDimensions();
		media->resizeGetHeight(width);
	}

	if (media && media->hideServiceText()) {
		height = media->height() + st::msgServiceMargin.bottom();
	} else {
		auto contentWidth = width;
		contentWidth -= st::msgServiceMargin.left() + st::msgServiceMargin.right();
		if (contentWidth < st::msgServicePadding.left() + st::msgServicePadding.right() + 1) {
			contentWidth = st::msgServicePadding.left() + st::msgServicePadding.right() + 1;
		}

		auto maxWidth = text.maxWidth()
			+ st::msgServicePadding.left()
			+ st::msgServicePadding.right();
		auto minHeight = text.minHeight();

		auto nwidth = qMax(contentWidth
			- st::msgServicePadding.left()
			- st::msgServicePadding.right(), 0);
		height = (contentWidth >= maxWidth)
			? minHeight
			: text.countHeight(nwidth);
		height += st::msgServicePadding.top()
			+ st::msgServicePadding.bottom()
			+ st::msgServiceMargin.top()
			+ st::msgServiceMargin.bottom();
	}

	return height;
}

void ServicePreMessage::paint(
		Painter &p,
		const PaintContext &context,
		QRect g,
		ElementChatMode mode) const {
	if (media && media->hideServiceText()) {
		const auto left = (width - media->width()) / 2;
		const auto top = g.top() - height - st::msgMargin.bottom();
		const auto position = QPoint(left, top);
		p.translate(position);
		media->draw(p, context.selected()
			? context.translated(-position)
			: context.translated(-position).withSelection({}));
		p.translate(-position);
	} else {
		const auto top = g.top() - height - st::msgMargin.top();
		p.translate(0, top);

		const auto rect = QRect(0, 0, width, height)
			- st::msgServiceMargin;
		const auto trect = rect - st::msgServicePadding;

		ServiceMessagePainter::PaintComplexBubble(
			p,
			context.st,
			rect.left(),
			rect.width(),
			text,
			trect);

		p.setBrush(Qt::NoBrush);
		p.setPen(context.st->msgServiceFg());
		p.setFont(st::msgServiceFont);
		text.draw(p, {
			.position = trect.topLeft(),
			.availableWidth = trect.width(),
			.align = style::al_top,
			.palette = &context.st->serviceTextPalette(),
			.now = context.now,
			.fullWidthSelection = false,
			//.selection = context.selection,
		});

		p.translate(0, -top);
	}
}

ClickHandlerPtr ServicePreMessage::textState(
		QPoint point,
		const StateRequest &request,
		QRect g) const {
	if (media && media->hideServiceText()) {
		const auto left = (width - media->width()) / 2;
		const auto top = g.top() - height - st::msgMargin.bottom();
		const auto position = QPoint(left, top);
		return media->textState(point - position, request).link;
	}
	const auto top = g.top() - height - st::msgMargin.top();
	const auto rect = QRect(0, top, width, height)
		- st::msgServiceMargin;
	const auto trect = rect - st::msgServicePadding;
	if (trect.contains(point)) {
		auto textRequest = request.forText();
		textRequest.align = style::al_center;
		const auto link = text.getState(
			point - trect.topLeft(),
			trect.width(),
			textRequest).link;
		if (link) {
			return link;
		}
	}
	if (handler && rect.contains(point)) {
		return handler;
	}
	return {};
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
, _dateTime((IsItemScheduledUntilOnline(data) || data->shortcutId())
	? QDateTime()
	: ItemDateTime(data))
, _text(st::msgMinWidth)
, _flags(serviceFlag
	| Flag::NeedsResize
	| (IsItemScheduledUntilOnline(data)
		? Flag::ScheduledUntilOnline
		: Flag())
	| (countIsTopicRootReply() ? Flag::TopicRootReply : Flag()))
, _context(delegate->elementContext()) {
	history()->owner().registerItemView(this);
	refreshMedia(replacing);
	if (_context == Context::History) {
		history()->setHasPendingResizedItems();
	}
	if (data->isFakeAboutView()) {
		const auto user = data->history()->peer->asUser();
		if (user
			&& user->isBot()
			&& !user->isRepliesChat()
			&& !user->isVerifyCodes()) {
			AddComponents(FakeBotAboutTop::Bit());
		}
	}
}

bool Element::embedReactionsInBubble() const {
	return false;
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

uint8 Element::colorIndex() const {
	return data()->colorIndex();
}

uint8 Element::contentColorIndex() const {
	return data()->contentColorIndex();
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
	if (!(_flags & Flag::HeavyCustomEmoji)) {
		_flags |= Flag::HeavyCustomEmoji;
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
	if (!(_flags & Flag::HeavyCustomEmoji)) {
		_flags |= Flag::HeavyCustomEmoji;
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
	if (context.highlight.opacity == 0.) {
		return;
	}
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
	const auto o = p.opacity();
	p.setOpacity(o * context.highlight.opacity);
	p.fillRect(0, y, width(), height, context.st->msgSelectOverlay());
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

not_null<PurchasedTag*> Element::enforcePurchasedTag() {
	if (const auto purchased = Get<PurchasedTag>()) {
		return purchased;
	}
	AddComponents(PurchasedTag::Bit());
	return Get<PurchasedTag>();
}

int Element::AdditionalSpaceForSelectionCheckbox(
		not_null<const Element*> view,
		QRect countedGeometry) {
	if (!view->hasOutLayout()
		|| view->delegate()->elementChatMode() == ElementChatMode::Wide) {
		return 0;
	}
	if (countedGeometry.isEmpty()) {
		countedGeometry = view->innerGeometry();
	}
	const auto diff = view->width()
		- (countedGeometry.x() + countedGeometry.width())
		- st::msgPadding.right()
		- st::msgSelectionOffset
		- view->rightActionSize().value_or(QSize()).width();
	return (diff < 0)
		? -(std::min(st::msgSelectionOffset, -diff))
		: 0;
}

void Element::refreshMedia(Element *replacing) {
	if (_flags & Flag::MediaOverriden) {
		return;
	}
	_flags &= ~Flag::HiddenByGroup;

	const auto item = data();
	if (!item->computeUnavailableReason().isEmpty()) {
		_media = nullptr;
		return;
	}
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
	} else if (item->showSimilarChannels()) {
		_media = std::make_unique<SimilarChannels>(this);
	} else if (isOnlyCustomEmoji()
		&& Core::App().settings().largeEmoji()
		&& !item->isSponsored()) {
		_media = std::make_unique<UnwrappedMedia>(
			this,
			std::make_unique<CustomEmoji>(this, onlyCustomEmoji()));
	} else if (isIsolatedEmoji()
		&& Core::App().settings().largeEmoji()
		&& !item->isSponsored()) {
		const auto emoji = isolatedEmoji();
		const auto emojiStickers = &history()->session().emojiStickersPack();
		const auto skipPremiumEffect = false;
		if (const auto sticker = emojiStickers->stickerForEmoji(emoji)) {
			auto content = std::make_unique<Sticker>(
				this,
				sticker.document,
				skipPremiumEffect,
				replacing,
				sticker.replacements);
			content->setEmojiSticker();
			_media = std::make_unique<UnwrappedMedia>(
				this,
				std::move(content));
		} else {
			_media = std::make_unique<UnwrappedMedia>(
				this,
				std::make_unique<LargeEmoji>(this, emoji));
		}
	} else if (const auto decision = item->Get<HistoryServiceSuggestDecision>()) {
		_media = std::make_unique<MediaGeneric>(
			this,
			GenerateSuggestDecisionMedia(this, decision),
			MediaGenericDescriptor{
				.maxWidth = st::chatSuggestInfoWidth,
				.fullAreaLink = decision->lnk,
				.service = true,
				.hideServiceText = true,
			});
	} else {
		_media = nullptr;
	}
}

HistoryItem *Element::textItem() const {
	return _textItem;
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
	if (_context == Context::Replies) {
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
	const auto topicUrl = u"internal:url:https://t.me/c/%1/%2"_q
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
	const auto media = item->media();
	const auto storyMention = media && media->storyMention();
	if (media && media->storyExpired()) {
		_media = nullptr;
		_textItem = item;
		if (!storyMention) {
			if (_text.isEmpty()) {
				setTextWithLinks(Ui::Text::Italic(
					tr::lng_forwarded_story_expired(tr::now)));
			}
			return;
		}
	}

	// Albums may show text of a different item than the parent one.
	_textItem = _media ? _media->itemForText() : item.get();
	if (!_textItem) {
		if (!_text.isEmpty()) {
			setTextWithLinks({});
		}
		return;
	}
	const auto &text = _textItem->_text;
	if (_text.isEmpty() == text.empty()) {
	} else if (_flags & Flag::ServiceMessage) {
		const auto contextDependentText = contextDependentServiceText();
		const auto &markedText = contextDependentText.text.empty()
			? text
			: contextDependentText.text;
		const auto &customLinks = contextDependentText.text.empty()
			? _textItem->customTextLinks()
			: contextDependentText.links;
		setTextWithLinks(markedText, customLinks);

		if (const auto done = item->Get<HistoryServiceTodoCompletions>()) {
			if (!done->completed.empty() && !done->incompleted.empty()) {
				const auto todoItemId = (done->incompleted.size() == 1)
					? done->incompleted.front()
					: 0;
				setServicePreMessage(
					item->composeTodoIncompleted(done),
					JumpToMessageClickHandler(
						(done->peerId
							? history()->owner().peer(done->peerId)
							: history()->peer),
						done->msgId,
						item->fullId(),
						{ .todoItemId = todoItemId }));
			} else {
				setServicePreMessage({});
			}
		}
	} else {
		const auto unavailable = item->computeUnavailableReason();
		if (!unavailable.isEmpty()) {
			setTextWithLinks(Ui::Text::Italic(unavailable));
		} else {
			setTextWithLinks(_textItem->translatedTextWithLocalEntities());
		}
	}
}

void Element::setTextWithLinks(
		const TextWithEntities &text,
		const std::vector<ClickHandlerPtr> &links) {
	const auto context = Core::TextContext({
		.session = &history()->session(),
		.repaint = [=] { customEmojiRepaint(); },
	});
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
	InitElementTextPart(this, _text);
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
	recountThreadBarInBlocks();
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
			&& !item->isPostHidingAuthor()
			&& (!item->history()->peer->isMegagroup()
				|| !view->hasOutLayout()
				|| !item->from()->isChannel());
	};
	const auto item = data();
	if (!Has<DateBadge>()
		&& !Has<UnreadBar>()
		&& !Has<ServicePreMessage>()
		&& !Has<ForumThreadBar>()) {
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
			const auto peer = item->history()->peer;
			if (peer->isSelf()
				|| peer->isRepliesChat()
				|| peer->isVerifyCodes()
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
	if (const auto from = item->displayFrom()) {
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

bool Element::displayForumThreadBar() const {
	return Has<ForumThreadBar>();
}

bool Element::isInOneBunchWithPrevious() const {
	return !data()->isEmpty() && !displayForumThreadBar();
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

void Element::recountThreadBarInBlocks() {
	const auto item = data();
	const auto topic = item->topic();
	const auto sublist = item->savedSublist();
	const auto parentChat = (topic && topic->channel()->useSubsectionTabs())
		? topic->channel().get()
		: sublist
		? sublist->parentChat()
		: nullptr;
	const auto barThread = [&]() -> Data::Thread* {
		if (!parentChat
			|| isHidden()
			|| item->isEmpty()
			|| item->isSponsored()) {
			return nullptr;
		}
		if (const auto previous = previousDisplayedInBlocks()) {
			const auto prev = previous->data();
			if (const auto prevTopic = prev->topic()) {
				Assert(prevTopic->channel() == parentChat);
				const auto topicRootId = topic->rootId();
				if (prevTopic->rootId() == topicRootId) {
					return nullptr;
				}
			} else if (const auto prevSublist = prev->savedSublist()) {
				Assert(prevSublist->parentChat() == parentChat);
				const auto sublistPeer = sublist->sublistPeer();
				if (prevSublist->sublistPeer() == sublistPeer) {
					return nullptr;
				}
			}
		}
		return topic
			? (Data::Thread*)topic
			: (sublist && sublist->sublistPeer() != parentChat)
			? (Data::Thread*)sublist
			: nullptr;
	}();
	if (barThread && !Has<ForumThreadBar>()) {
		AddComponents(ForumThreadBar::Bit());
		Get<ForumThreadBar>()->init(parentChat, barThread);
	} else if (!barThread && Has<ForumThreadBar>()) {
		RemoveComponents(ForumThreadBar::Bit());
	}
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
			return prev->hideDisplayDate()
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
	if (item->hideDisplayDate()) {
		displayDate = false;
	}
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

void Element::setServicePreMessage(
		PreparedServiceText text,
		ClickHandlerPtr fullClickHandler,
		std::unique_ptr<Media> media) {
	if (!text.text.empty() || media) {
		AddComponents(ServicePreMessage::Bit());
		const auto service = Get<ServicePreMessage>();
		service->init(
			this,
			std::move(text),
			std::move(fullClickHandler),
			std::move(media));
		setPendingResize();
	} else if (Has<ServicePreMessage>()) {
		RemoveComponents(ServicePreMessage::Bit());
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

bool Element::displayReply() const {
	return Has<Reply>();
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

bool Element::hasRightLayout() const {
	return hasOutLayout()
		&& (_delegate->elementChatMode() != ElementChatMode::Wide);
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

bool Element::toggleSelectionByHandlerClick(
		const ClickHandlerPtr &handler) const {
	return false;
}

bool Element::allowTextSelectionByHandler(
		const ClickHandlerPtr &handler) const {
	return false;
}

bool Element::usesBubblePattern(const PaintContext &context) const {
	return (context.selection != FullSelection)
		&& hasOutLayout()
		&& context.bubblesPattern
		&& !context.viewport.isEmpty()
		&& !context.bubblesPattern->pixmap.size().isEmpty();
}

bool Element::hasVisibleText() const {
	return false;
}

int Element::textualMaxWidth() const {
	return st::msgPadding.left()
		+ (hasVisibleText() ? text().maxWidth() : 0)
		+ st::msgPadding.right();
}

auto Element::verticalRepaintRange() const -> VerticalRepaintRange {
	return {
		.top = 0,
		.height = height()
	};
}

bool Element::hasHeavyPart() const {
	return (_flags & Flag::HeavyCustomEmoji);
}

void Element::checkHeavyPart() {
	if (!hasHeavyPart() && (!_media || !_media->hasHeavyPart())) {
		history()->owner().unregisterHeavyViewPart(this);
	}
}

bool Element::isSignedAuthorElided() const {
	return false;
}

void Element::setupReactions(Element *replacing) {
	refreshReactions();
	auto animations = replacing
		? replacing->takeReactionAnimations()
		: base::flat_map<
		Data::ReactionId,
		std::unique_ptr<Ui::ReactionFlyAnimation>>();
	if (!animations.empty()) {
		const auto repainter = [=] { repaint(); };
		for (const auto &[id, animation] : animations) {
			animation->setRepaintCallback(repainter);
		}
		if (_reactions) {
			_reactions->continueAnimations(std::move(animations));
		}
	}
}

void Element::refreshReactions() {
	using namespace Reactions;
	auto reactionsData = InlineListDataFromMessage(this);
	if (reactionsData.reactions.empty()) {
		setReactions(nullptr);
		return;
	}
	if (!_reactions) {
		const auto handlerFactory = [=](ReactionId id) {
			const auto weak = base::make_weak(this);
			return std::make_shared<LambdaClickHandler>([=](
					ClickContext context) {
				const auto strong = weak.get();
				if (!strong) {
					return;
				}
				const auto item = strong->data();
				const auto controller = ExtractController(context);
				if (item->reactionsAreTags()) {
					if (item->history()->session().premium()) {
						const auto tag = Data::SearchTagToQuery(id);
						HashtagClickHandler(tag).onClick(context);
					} else if (controller) {
						ShowPremiumPreviewBox(
							controller,
							PremiumFeature::TagsForMessages);
					}
					return;
				}
				if (id.paid()) {
					Payments::TryAddingPaidReaction(
						item,
						weak.get(),
						1,
						std::nullopt,
						controller->uiShow());
					return;
				} else {
					const auto source = HistoryReactionSource::Existing;
					item->toggleReaction(id, source);
				}
				if (const auto now = weak.get()) {
					const auto chosen = now->data()->chosenReactions();
					if (id.paid() || ranges::contains(chosen, id)) {
						now->animateReaction({
							.id = id,
						});
					}
				}
			});
		};
		setReactions(std::make_unique<InlineList>(
			&history()->owner().reactions(),
			handlerFactory,
			[=] { customEmojiRepaint(); },
			std::move(reactionsData)));
	} else {
		auto was = _reactions->computeTagsList();
		_reactions->update(std::move(reactionsData), width());
		auto now = _reactions->computeTagsList();
		if (!was.empty() || !now.empty()) {
			auto &owner = history()->owner();
			owner.viewTagsChanged(this, std::move(was), std::move(now));
		}
	}
}

void Element::setReactions(std::unique_ptr<Reactions::InlineList> list) {
	auto was = _reactions
		? _reactions->computeTagsList()
		: std::vector<Data::ReactionId>();
	_reactions = std::move(list);
	auto now = _reactions
		? _reactions->computeTagsList()
		: std::vector<Data::ReactionId>();
	if (!was.empty() || !now.empty()) {
		auto &owner = history()->owner();
		owner.viewTagsChanged(this, std::move(was), std::move(now));
	}
}

bool Element::updateReactions() {
	const auto wasReactions = _reactions
		? _reactions->currentSize()
		: QSize();
	refreshReactions();
	const auto nowReactions = _reactions
		? _reactions->currentSize()
		: QSize();
	return (wasReactions != nowReactions);
}

void Element::itemDataChanged() {
	if (updateReactions()) {
		history()->owner().requestViewResize(this);
	} else {
		repaint();
	}
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

void Element::blockquoteExpandChanged() {
	_textWidth = -1;
	_textHeight = 0;
	history()->owner().requestViewResize(this);
}

void Element::unloadHeavyPart() {
	history()->owner().unregisterHeavyViewPart(this);
	if (_reactions) {
		_reactions->unloadCustomEmoji();
	}
	if (_media) {
		_media->unloadHeavyPart();
	}
	if (_flags & Flag::HeavyCustomEmoji) {
		_flags &= ~Flag::HeavyCustomEmoji;
		_text.unloadPersistentAnimation();
		if (const auto reply = Get<Reply>()) {
			reply->unloadPersistentAnimation();
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

SelectedQuote Element::FindSelectedQuote(
		const Ui::Text::String &text,
		TextSelection selection,
		not_null<HistoryItem*> item) {
	if (selection.to > text.length()) {
		return {};
	}
	auto modified = selection;
	for (const auto &modification : text.modifications()) {
		if (modification.position >= selection.to) {
			break;
		} else if (modification.position <= selection.from) {
			modified.from += modification.skipped;
			if (modification.added
				&& modification.position < selection.from) {
				--modified.from;
			}
		}
		modified.to += modification.skipped;
		if (modification.added && modified.to > modified.from) {
			--modified.to;
		}
	}
	auto result = item->originalText();
	if (modified.empty() || modified.to > result.text.size()) {
		return {};
	}
	const auto session = &item->history()->session();
	const auto limit = session->appConfig().quoteLengthMax();
	const auto overflown = (modified.from + limit < modified.to);
	if (overflown) {
		modified.to = modified.from + limit;
	}
	result.text = result.text.mid(
		modified.from,
		modified.to - modified.from);
	const auto allowed = std::array{
		EntityType::Bold,
		EntityType::Italic,
		EntityType::Underline,
		EntityType::StrikeOut,
		EntityType::Spoiler,
		EntityType::CustomEmoji,
	};
	for (auto i = result.entities.begin(); i != result.entities.end();) {
		const auto offset = i->offset();
		const auto till = offset + i->length();
		if ((till <= modified.from)
			|| (offset >= modified.to)
			|| !ranges::contains(allowed, i->type())) {
			i = result.entities.erase(i);
		} else {
			if (till > modified.to) {
				i->shrinkFromRight(till - modified.to);
			}
			i->shiftLeft(modified.from);
			++i;
		}
	}
	return { item, { result, modified.from }, overflown };
}

TextSelection Element::FindSelectionFromQuote(
		const Ui::Text::String &text,
		const SelectedQuote &quote) {
	Expects(quote.item != nullptr);

	const auto &rich = quote.highlight.quote;
	if (rich.empty()) {
		return {};
	}
	const auto &original = quote.item->originalText();
	if (quote.highlight.quoteOffset == kSearchQueryOffsetHint) {
		return ApplyModificationsFrom(
			FindSearchQueryHighlight(original.text, rich.text),
			text);
	}
	const auto length = int(original.text.size());
	const auto qlength = int(rich.text.size());
	const auto checkAt = [&](int offset) {
		return TextSelection{
			uint16(offset),
			uint16(offset + qlength),
		};
	};
	const auto findOneAfter = [&](int offset) {
		if (offset > length - qlength) {
			return TextSelection();
		}
		const auto i = original.text.indexOf(rich.text, offset);
		return (i >= 0) ? checkAt(i) : TextSelection();
	};
	const auto findOneBefore = [&](int offset) {
		if (!offset) {
			return TextSelection();
		}
		const auto end = std::min(offset + qlength - 1, length);
		const auto from = end - length - 1;
		const auto i = original.text.lastIndexOf(rich.text, from);
		return (i >= 0) ? checkAt(i) : TextSelection();
	};
	const auto findAfter = [&](int offset) {
		while (true) {
			const auto result = findOneAfter(offset);
			if (!result.empty() || result == TextSelection()) {
				return result;
			}
			offset = result.from;
		}
	};
	const auto findBefore = [&](int offset) {
		while (true) {
			const auto result = findOneBefore(offset);
			if (!result.empty() || result == TextSelection()) {
				return result;
			}
			offset = result.from - 2;
			if (offset < 0) {
				return result;
			}
		}
	};
	const auto findTwoWays = [&](int offset) {
		const auto after = findAfter(offset);
		if (after.empty()) {
			return findBefore(offset);
		} else if (after.from == offset) {
			return after;
		}
		const auto before = findBefore(offset);
		return before.empty()
			? after
			: (offset - before.from < after.from - offset)
			? before
			: after;
	};
	auto result = findTwoWays(quote.highlight.quoteOffset);
	if (result.empty()) {
		return {};
	}
	return ApplyModificationsFrom(result, text);
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
	if (_reactions) {
		return _reactions->takeAnimations();
	}
	return {};
}

void Element::animateEffect(Ui::ReactionFlyAnimationArgs &&args) {
}

void Element::animateUnreadEffect() {
}

auto Element::takeEffectAnimation()
-> std::unique_ptr<Ui::ReactionFlyAnimation> {
	return nullptr;
}

QRect Element::effectIconGeometry() const {
	return QRect();
}

Element::~Element() {
	setReactions(nullptr);

	// Delete media while owner still exists.
	clearSpecialOnlyEmoji();
	base::take(_media);
	if (_flags & Flag::HeavyCustomEmoji) {
		_flags &= ~Flag::HeavyCustomEmoji;
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

int FindViewY(not_null<Element*> view, uint16 symbol, int yfrom) {
	auto request = HistoryView::StateRequest();
	request.flags = Ui::Text::StateRequest::Flag::LookupSymbol;
	const auto single = st::messageTextStyle.font->height;
	const auto inner = view->innerGeometry();
	const auto origin = inner.topLeft();
	const auto top = 0;
	const auto bottom = view->height();
	if (origin.y() < top
		|| origin.y() + inner.height() > bottom
		|| inner.height() <= 0) {
		return yfrom;
	}
	const auto fory = [&](int y) {
		return view->textState(origin + QPoint(0, y), request).symbol;
	};
	yfrom = std::max(yfrom - origin.y(), 0);
	auto ytill = inner.height() - 1;
	auto symbolfrom = fory(yfrom);
	auto symboltill = fory(ytill);
	if ((yfrom >= ytill) || (symbolfrom >= symbol)) {
		return origin.y() + yfrom;
	} else if (symboltill <= symbol) {
		return origin.y() + ytill;
	}
	while (ytill - yfrom >= 2 * single) {
		const auto middle = (yfrom + ytill) / 2;
		const auto found = fory(middle);
		if (found == symbol
			|| symbolfrom > found
			|| symboltill < found) {
			return middle;
		} else if (found < symbol) {
			yfrom = middle;
			symbolfrom = found;
		} else {
			ytill = middle;
			symboltill = found;
		}
	}
	return origin.y() + (yfrom + ytill) / 2;
}

int FindViewTaskY(not_null<Element*> view, int taskId, int yfrom) {
	auto request = HistoryView::StateRequest();
	request.flags = Ui::Text::StateRequest::Flag::LookupLink;
	const auto single = st::messageTextStyle.font->height;
	const auto inner = view->innerGeometry();
	const auto origin = inner.topLeft();
	const auto top = 0;
	const auto bottom = view->height();
	if (origin.y() < top
		|| origin.y() + inner.height() > bottom
		|| inner.height() <= 0) {
		return yfrom;
	}
	const auto media = view->data()->media();
	const auto todolist = media ? media->todolist() : nullptr;
	if (!todolist) {
		return yfrom;
	}
	const auto &items = todolist->items;
	const auto indexOf = [&](int id) -> int {
		return ranges::find(items, id, &TodoListItem::id) - begin(items);
	};
	const auto index = indexOf(taskId);
	const auto count = int(items.size());
	if (index == count) {
		return yfrom;
	}
	yfrom = std::max(yfrom - origin.y(), 0);
	auto ytill = inner.height() - 1;
	const auto middle = (yfrom + ytill) / 2;
	const auto fory = [&](int y) {
		const auto state = view->textState(origin + QPoint(0, y), request);
		const auto &link = state.link;
		const auto id = link
			? link->property(kTodoListItemIdProperty).toInt()
			: -1;
		const auto index = (id >= 0) ? indexOf(id) : int(items.size());
		return (index < count) ? index : (y < middle) ? -1 : count;
	};
	auto indexfrom = fory(yfrom);
	auto indextill = fory(ytill);
	if ((yfrom >= ytill) || (indexfrom >= index)) {
		return origin.y() + yfrom;
	} else if (indextill <= index) {
		return origin.y() + ytill;
	}
	while (ytill - yfrom >= 2 * single) {
		const auto middle = (yfrom + ytill) / 2;
		const auto found = fory(middle);
		if (found == index
			|| indexfrom > found
			|| indextill < found) {
			return origin.y() + middle;
		} else if (found < index) {
			yfrom = middle;
			indexfrom = found;
		} else {
			ytill = middle;
			indextill = found;
		}
	}
	return origin.y() + (yfrom + ytill) / 2;
}

Window::SessionController *ExtractController(const ClickContext &context) {
	const auto my = context.other.value<ClickHandlerContext>();
	if (const auto controller = my.sessionWindow.get()) {
		return controller;
	}
	return nullptr;
}

TextSelection FindSearchQueryHighlight(
		const QString &text,
		const QString &query) {
	const auto lower = query.toLower();
	return FindSearchQueryHighlight(text, QStringView(lower));
}

TextSelection FindSearchQueryHighlight(
		const QString &text,
		QStringView lower) {
	const auto inside = text.toLower();
	const auto find = [&](QStringView part) {
		auto skip = 0;
		if (const auto from = inside.indexOf(part, skip); from >= 0) {
			if (!from || !inside[from - 1].isLetterOrNumber()) {
				return int(from);
			}
			skip = from + 1;
		}
		return -1;
	};
	if (const auto from = find(lower); from >= 0) {
		const auto till = from + lower.size();
		if (till >= inside.size()
			|| !(inside.begin() + till)->isLetterOrNumber()) {
			return { uint16(from), uint16(till) };
		}
	}
	const auto tillEndOfWord = [&](int from) {
		for (auto till = from + 1; till != inside.size(); ++till) {
			if (!inside[till].isLetterOrNumber()) {
				return TextSelection{ uint16(from), uint16(till) };
			}
		}
		return TextSelection{ uint16(from), uint16(inside.size()) };
	};
	const auto words = Ui::Text::Words(lower);
	for (const auto &word : words) {
		const auto length = int(word.size());
		const auto cut = length / 2;
		const auto part = word.mid(0, length - cut);
		const auto offset = find(part);
		if (offset < 0) {
			continue;
		}
		for (auto i = 0; i != cut; ++i) {
			const auto part = word.mid(0, length - i);
			if (const auto from = find(part); from >= 0) {
				return tillEndOfWord(from);
			}
		}
		return tillEndOfWord(offset);
	}
	return {};
}

} // namespace HistoryView
