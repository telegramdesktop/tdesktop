/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_element.h"

#include "history/view/history_view_service_message.h"
#include "history/view/history_view_message.h"
#include "history/history_item_components.h"
#include "history/history_item.h"
#include "history/view/media/history_view_media.h"
#include "history/view/media/history_view_media_grouped.h"
#include "history/view/media/history_view_sticker.h"
#include "history/view/media/history_view_large_emoji.h"
#include "history/history.h"
#include "base/unixtime.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "main/main_session.h"
#include "chat_helpers/stickers_emoji_pack.h"
#include "window/window_session_controller.h"
#include "ui/toast/toast.h"
#include "ui/toasts/common_toasts.h"
#include "data/data_session.h"
#include "data/data_groups.h"
#include "data/data_media_types.h"
#include "lang/lang_keys.h"
#include "layout.h"
#include "app.h"
#include "styles/style_chat.h"

namespace HistoryView {
namespace {

// A new message from the same sender is attached to previous within 15 minutes.
constexpr int kAttachMessageToPreviousSecondsDelta = 900;

bool IsAttachedToPreviousInSavedMessages(
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

} // namespace

SimpleElementDelegate::SimpleElementDelegate(
	not_null<Window::SessionController*> controller)
: _controller(controller) {
}

std::unique_ptr<HistoryView::Element> SimpleElementDelegate::elementCreate(
		not_null<HistoryMessage*> message,
		Element *replacing) {
	return std::make_unique<HistoryView::Message>(this, message, replacing);
}

std::unique_ptr<HistoryView::Element> SimpleElementDelegate::elementCreate(
		not_null<HistoryService*> message,
		Element *replacing) {
	return std::make_unique<HistoryView::Service>(this, message, replacing);
}

bool SimpleElementDelegate::elementUnderCursor(
		not_null<const Element*> view) {
	return false;
}

crl::time SimpleElementDelegate::elementHighlightTime(
		not_null<const HistoryItem*> item) {
	return crl::time(0);
}

bool SimpleElementDelegate::elementInSelectionMode() {
	return false;
}

bool SimpleElementDelegate::elementIntersectsRange(
		not_null<const Element*> view,
		int from,
		int till) {
	return true;
}

void SimpleElementDelegate::elementStartStickerLoop(
	not_null<const Element*> view) {
}

void SimpleElementDelegate::elementShowPollResults(
	not_null<PollData*> poll,
	FullMsgId context) {
}

void SimpleElementDelegate::elementShowTooltip(
	const TextWithEntities &text,
	Fn<void()> hiddenCallback) {
}

bool SimpleElementDelegate::elementIsGifPaused() {
	return _controller->isGifPausedAtLeastFor(Window::GifPauseReason::Any);
}

bool SimpleElementDelegate::elementHideReply(not_null<const Element*> view) {
	return false;
}

bool SimpleElementDelegate::elementShownUnread(
		not_null<const Element*> view) {
	return view->data()->unread();
}

void SimpleElementDelegate::elementSendBotCommand(
	const QString &command,
	const FullMsgId &context) {
}

void SimpleElementDelegate::elementHandleViaClick(not_null<UserData*> bot) {
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
	const auto format = QLocale::system().dateTimeFormat(QLocale::LongFormat);
	auto dateText = view->dateTime().toString(format);
	if (const auto editedDate = view->displayedEditDate()) {
		dateText += '\n' + tr::lng_edited_date(
			tr::now,
			lt_date,
			base::unixtime::parse(editedDate).toString(format));
	}
	if (const auto forwarded = view->data()->Get<HistoryMessageForwarded>()) {
		dateText += '\n' + tr::lng_forwarded_date(
			tr::now,
			lt_date,
			base::unixtime::parse(forwarded->originalDate).toString(format));
		if (const auto media = view->media()) {
			if (media->hidesForwardedInfo()) {
				const auto from = forwarded->originalSender
					? forwarded->originalSender->shortName()
					: forwarded->hiddenSenderInfo->firstName;
				if (forwarded->imported) {
					dateText += '\n' + tr::lng_signed_author(
						tr::now,
						lt_user,
						from);
				} else {
					dateText += '\n' + tr::lng_forwarded(
						tr::now,
						lt_user,
						from);
				}
			}
		}
		if (forwarded->imported) {
			dateText = tr::lng_forwarded_imported(tr::now)
				+ "\n\n" + dateText;
		}
	}
	if (const auto msgsigned = view->data()->Get<HistoryMessageSigned>()) {
		if (msgsigned->isElided && !msgsigned->isAnonymousRank) {
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

void UnreadBar::paint(Painter &p, int y, int w) const {
	const auto bottom = y + height();
	y += marginTop();
	p.fillRect(
		0,
		y,
		w,
		height() - marginTop() - st::lineWidth,
		st::historyUnreadBarBg);
	p.fillRect(
		0,
		bottom - st::lineWidth,
		w,
		st::lineWidth,
		st::historyUnreadBarBorder);
	p.setFont(st::historyUnreadBarFont);
	p.setPen(st::historyUnreadBarFg);

	int left = st::msgServiceMargin.left();
	int maxwidth = w;
	if (Core::App().settings().chatWide()) {
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

void DateBadge::paint(Painter &p, int y, int w) const {
	ServiceMessagePainter::paintDate(p, text, width, y, w);
}

Element::Element(
	not_null<ElementDelegate*> delegate,
	not_null<HistoryItem*> data,
	Element *replacing)
: _delegate(delegate)
, _data(data)
, _isScheduledUntilOnline(IsItemScheduledUntilOnline(data))
, _dateTime(_isScheduledUntilOnline ? QDateTime() : ItemDateTime(data))
, _context(delegate->elementContext()) {
	history()->owner().registerItemView(this);
	refreshMedia(replacing);
	if (_context == Context::History) {
		history()->setHasPendingResizedItems();
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

void Element::paintHighlight(
		Painter &p,
		int geometryHeight) const {
	const auto top = marginTop();
	const auto bottom = marginBottom();
	const auto fill = qMin(top, bottom);
	const auto skiptop = top - fill;
	const auto fillheight = fill + geometryHeight + fill;

	paintCustomHighlight(p, skiptop, fillheight, data());
}

float64 Element::highlightOpacity(not_null<const HistoryItem*> item) const {
	const auto animms = delegate()->elementHighlightTime(item);
	if (!animms
		|| animms >= st::activeFadeInDuration + st::activeFadeOutDuration) {
		return 0.;
	}

	return (animms > st::activeFadeInDuration)
		? (1. - (animms - st::activeFadeInDuration)
			/ float64(st::activeFadeOutDuration))
		: (animms / float64(st::activeFadeInDuration));
}

void Element::paintCustomHighlight(
		Painter &p,
		int y,
		int height,
		not_null<const HistoryItem*> item) const {
	const auto opacity = highlightOpacity(item);
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
		st::defaultTextPalette.selectOverlay);
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

int Element::skipBlockWidth() const {
	return st::msgDateSpace + infoWidth() - st::msgDateDelta.x();
}

int Element::skipBlockHeight() const {
	return st::msgDateFont->height - st::msgDateDelta.y();
}

QString Element::skipBlock() const {
	return textcmdSkipBlock(skipBlockWidth(), skipBlockHeight());
}

int Element::infoWidth() const {
	return 0;
}

bool Element::isHiddenByGroup() const {
	return _flags & Flag::HiddenByGroup;
}

bool Element::isHidden() const {
	return isHiddenByGroup();
}

void Element::refreshMedia(Element *replacing) {
	_flags &= ~Flag::HiddenByGroup;

	const auto item = data();
	const auto media = item->media();
	if (media && media->canBeGrouped()) {
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
	const auto session = &history()->session();
	if (const auto media = _data->media()) {
		_media = media->createView(this, replacing);
	} else if (_data->isIsolatedEmoji()
		&& Core::App().settings().largeEmoji()) {
		const auto emoji = _data->isolatedEmoji();
		const auto emojiStickers = &session->emojiStickersPack();
		if (const auto sticker = emojiStickers->stickerForEmoji(emoji)) {
			_media = std::make_unique<UnwrappedMedia>(
				this,
				std::make_unique<Sticker>(
					this,
					sticker.document,
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

void Element::previousInBlocksChanged() {
	recountDisplayDateInBlocks();
	recountAttachToPreviousInBlocks();
}

void Element::nextInBlocksRemoved() {
	setAttachToNext(false);
}

void Element::refreshDataId() {
	if (const auto media = this->media()) {
		media->refreshParentId(data());
	}
	refreshDataIdHook();
}

bool Element::computeIsAttachToPrevious(not_null<Element*> previous) {
	const auto mayBeAttached = [](not_null<HistoryItem*> item) {
		return !item->serviceMsg()
			&& !item->isEmpty()
			&& !item->isPost()
			&& (item->from() != item->history()->peer
				|| !item->from()->isChannel());
	};
	const auto item = data();
	if (!Has<DateBadge>() && !Has<UnreadBar>()) {
		const auto prev = previous->data();
		const auto possible = (std::abs(prev->date() - item->date())
				< kAttachMessageToPreviousSecondsDelta)
			&& mayBeAttached(item)
			&& mayBeAttached(prev);
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
	const auto item = data();
	const auto from = item->displayFrom();
	if (from) {
		return from->openLink();
	}
	if (const auto forwarded = item->Get<HistoryMessageForwarded>()) {
		if (forwarded->imported) {
			static const auto imported = std::make_shared<LambdaClickHandler>([] {
				Ui::ShowMultilineToast({
					.text = { tr::lng_forwarded_imported(tr::now) },
				});
			});
			return imported;
		}
	}
	static const auto hidden = std::make_shared<LambdaClickHandler>([] {
		Ui::Toast::Show(tr::lng_forwarded_hidden(tr::now));
	});
	return hidden;
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
	history()->owner().requestViewResize(this);
	if (data()->mainView() == this) {
		recountAttachToPreviousInBlocks();
	}
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
	if (const auto previous = previousDisplayedInBlocks()) {
		attachToPrevious = computeIsAttachToPrevious(previous);
		previous->setAttachToNext(attachToPrevious);
	}
	setAttachToPrevious(attachToPrevious);
}

void Element::recountDisplayDateInBlocks() {
	setDisplayDate([&] {
		const auto item = data();
		if (isHidden() || item->isEmpty()) {
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
	return performCountOptimalSize();
}

QSize Element::countCurrentSize(int newWidth) {
	if (_flags & Flag::NeedsResize) {
		_flags &= ~Flag::NeedsResize;
		initDimensions();
	}
	return performCountCurrentSize(newWidth);
}

void Element::setDisplayDate(bool displayDate) {
	const auto item = data();
	if (displayDate && !Has<DateBadge>()) {
		AddComponents(DateBadge::Bit());
		Get<DateBadge>()->init(ItemDateText(item, _isScheduledUntilOnline));
		setPendingResize();
	} else if (!displayDate && Has<DateBadge>()) {
		RemoveComponents(DateBadge::Bit());
		setPendingResize();
	}
}

void Element::setAttachToNext(bool attachToNext) {
	if (attachToNext && !(_flags & Flag::AttachedToNext)) {
		_flags |= Flag::AttachedToNext;
		setPendingResize();
	} else if (!attachToNext && (_flags & Flag::AttachedToNext)) {
		_flags &= ~Flag::AttachedToNext;
		setPendingResize();
	}
}

void Element::setAttachToPrevious(bool attachToPrevious) {
	if (attachToPrevious && !(_flags & Flag::AttachedToPrevious)) {
		_flags |= Flag::AttachedToPrevious;
		setPendingResize();
	} else if (!attachToPrevious && (_flags & Flag::AttachedToPrevious)) {
		_flags &= ~Flag::AttachedToPrevious;
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
	int left,
	int top,
	int outerWidth) const {
}

ClickHandlerPtr Element::rightActionLink() const {
	return ClickHandlerPtr();
}

bool Element::displayEditedBadge() const {
	return false;
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
	return false;
}

void Element::checkHeavyPart() {
	if (!hasHeavyPart() && (!_media || !_media->hasHeavyPart())) {
		history()->owner().unregisterHeavyViewPart(this);
	}
}

void Element::unloadHeavyPart() {
	history()->owner().unregisterHeavyViewPart(this);
	if (_media) {
		_media->unloadHeavyPart();
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
	int right,
	int bottom,
	int width,
	bool selected,
	InfoDisplayType type) const {
}

bool Element::pointInTime(
		int right,
		int bottom,
		QPoint point,
		InfoDisplayType type) const {
	return false;
}

TextSelection Element::adjustSelection(
		TextSelection selection,
		TextSelectType type) const {
	return selection;
}

void Element::clickHandlerActiveChanged(
		const ClickHandlerPtr &handler,
		bool active) {
	if (const auto markup = _data->Get<HistoryMessageReplyMarkup>()) {
		if (const auto keyboard = markup->inlineKeyboard.get()) {
			keyboard->clickHandlerActiveChanged(handler, active);
		}
	}
	App::hoveredLinkItem(active ? this : nullptr);
	history()->owner().requestViewRepaint(this);
	if (const auto media = this->media()) {
		media->clickHandlerActiveChanged(handler, active);
	}
}

void Element::clickHandlerPressedChanged(
		const ClickHandlerPtr &handler,
		bool pressed) {
	if (const auto markup = _data->Get<HistoryMessageReplyMarkup>()) {
		if (const auto keyboard = markup->inlineKeyboard.get()) {
			keyboard->clickHandlerPressedChanged(handler, pressed);
		}
	}
	App::pressedLinkItem(pressed ? this : nullptr);
	history()->owner().requestViewRepaint(this);
	if (const auto media = this->media()) {
		media->clickHandlerPressedChanged(handler, pressed);
	}
}

Element::~Element() {
	// Delete media while owner still exists.
	base::take(_media);
	if (_data->mainView() == this) {
		_data->clearMainView();
	}
	if (_context == Context::History) {
		history()->owner().notifyViewRemoved(this);
	}
	history()->owner().unregisterItemView(this);
}

} // namespace HistoryView
