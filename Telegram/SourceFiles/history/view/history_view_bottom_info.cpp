/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_bottom_info.h"

#include "ui/chat/message_bubble.h"
#include "ui/chat/chat_style.h"
#include "ui/text/text_options.h"
#include "ui/text/text_utilities.h"
#include "lang/lang_keys.h"
#include "history/history_item_components.h"
#include "history/history_message.h"
#include "history/history.h"
#include "history/view/history_view_message.h"
#include "history/view/history_view_cursor_state.h"
#include "data/data_message_reactions.h"
#include "styles/style_chat.h"
#include "styles/style_dialogs.h"

namespace HistoryView {

BottomInfo::BottomInfo(
	not_null<::Data::Reactions*> reactionsOwner,
	Data &&data)
: _reactionsOwner(reactionsOwner)
, _data(std::move(data)) {
	layout();
}

void BottomInfo::update(Data &&data, int availableWidth) {
	_data = std::move(data);
	layout();
	if (width() > 0) {
		resizeGetHeight(std::min(maxWidth(), availableWidth));
	}
}

int BottomInfo::countReactionsMaxWidth() const {
	auto result = 0;
	for (const auto &reaction : _reactions) {
		result += st::reactionInfoSize;
		if (reaction.countTextWidth > 0) {
			result += st::reactionInfoSkip
				+ reaction.countTextWidth
				+ st::reactionInfoDigitSkip;
		} else {
			result += st::reactionInfoBetween;
		}
	}
	if (result) {
		result += (st::reactionInfoSkip - st::reactionInfoBetween);
	}
	return result;
}

int BottomInfo::countReactionsHeight(int newWidth) const {
	const auto left = 0;
	auto x = 0;
	auto y = 0;
	auto widthLeft = newWidth;
	for (const auto &reaction : _reactions) {
		const auto add = (reaction.countTextWidth > 0)
			? st::reactionInfoDigitSkip
			: st::reactionInfoBetween;
		const auto width = st::reactionInfoSize
			+ (reaction.countTextWidth > 0
				? (st::reactionInfoSkip + reaction.countTextWidth)
				: 0);
		if (x > left && widthLeft < width) {
			x = left;
			y += st::msgDateFont->height;
			widthLeft = newWidth;
		}
		x += width + add;
		widthLeft -= width + add;
	}
	if (x > left) {
		y += st::msgDateFont->height;
	}
	return y;
}

int BottomInfo::firstLineWidth() const {
	if (height() == minHeight()) {
		return width();
	}
	return maxWidth() - _reactionsMaxWidth;
}

bool BottomInfo::isWide() const {
	return (_data.flags & Data::Flag::Edited)
		|| !_data.author.isEmpty()
		|| !_views.isEmpty()
		|| !_replies.isEmpty()
		|| !_reactions.empty();
}

TextState BottomInfo::textState(
		not_null<const HistoryItem*> item,
		QPoint position) const {
	auto result = TextState(item);
	const auto inTime = QRect(
		width() - _dateWidth,
		0,
		_dateWidth,
		st::msgDateFont->height
	).contains(position);
	if (inTime) {
		result.cursor = CursorState::Date;
	}
	return result;
}

bool BottomInfo::isSignedAuthorElided() const {
	return _authorElided;
}

void BottomInfo::paint(
		Painter &p,
		QPoint position,
		int outerWidth,
		bool unread,
		bool inverted,
		const PaintContext &context) const {
	const auto st = context.st;
	const auto stm = context.messageStyle();

	auto right = position.x() + width();
	const auto firstLineBottom = position.y() + st::msgDateFont->height;
	if (_data.flags & Data::Flag::OutLayout) {
		const auto &icon = (_data.flags & Data::Flag::Sending)
			? (inverted
				? st->historySendingInvertedIcon()
				: st->historySendingIcon())
			: unread
			? (inverted
				? st->historySentInvertedIcon()
				: stm->historySentIcon)
			: (inverted
				? st->historyReceivedInvertedIcon()
				: stm->historyReceivedIcon);
		icon.paint(
			p,
			QPoint(right, firstLineBottom) + st::historySendStatePosition,
			outerWidth);
		right -= st::historySendStateSpace;
	}

	const auto authorEditedWidth = _authorEditedDate.maxWidth();
	right -= authorEditedWidth;
	_authorEditedDate.drawLeft(
		p,
		right,
		position.y(),
		authorEditedWidth,
		outerWidth);

	if (!_views.isEmpty()) {
		const auto viewsWidth = _views.maxWidth();
		right -= st::historyViewsSpace + viewsWidth;
		_views.drawLeft(p, right, position.y(), viewsWidth, outerWidth);

		const auto &icon = inverted
			? st->historyViewsInvertedIcon()
			: stm->historyViewsIcon;
		icon.paint(
			p,
			right - st::historyViewsWidth,
			firstLineBottom + st::historyViewsTop,
			outerWidth);
	}
	if (!_replies.isEmpty()) {
		const auto repliesWidth = _replies.maxWidth();
		right -= st::historyViewsSpace + repliesWidth;
		_replies.drawLeft(p, right, position.y(), repliesWidth, outerWidth);

		const auto &icon = inverted
			? st->historyRepliesInvertedIcon()
			: stm->historyRepliesIcon;
		icon.paint(
			p,
			right - st::historyViewsWidth,
			firstLineBottom + st::historyViewsTop,
			outerWidth);
	}
	if ((_data.flags & Data::Flag::Sending)
		&& !(_data.flags & Data::Flag::OutLayout)) {
		right -= st::historySendStateSpace;
		const auto &icon = inverted
			? st->historyViewsSendingInvertedIcon()
			: st->historyViewsSendingIcon();
		icon.paint(
			p,
			right,
			firstLineBottom + st::historyViewsTop,
			outerWidth);
	}
	if (!_reactions.empty()) {
		auto left = position.x();
		auto top = position.y();
		auto available = width();
		if (height() != minHeight()) {
			available = std::min(available, _reactionsMaxWidth);
			left += width() - available;
			top += st::msgDateFont->height;
		}
		paintReactions(p, left, top, available);
	}
}

void BottomInfo::paintReactions(
		Painter &p,
		int left,
		int top,
		int availableWidth) const {
	auto x = left;
	auto y = top;
	auto widthLeft = availableWidth;
	for (const auto &reaction : _reactions) {
		const auto add = (reaction.countTextWidth > 0)
			? st::reactionInfoDigitSkip
			: st::reactionInfoBetween;
		const auto width = st::reactionInfoSize
			+ (reaction.countTextWidth > 0
				? (st::reactionInfoSkip + reaction.countTextWidth)
				: 0);
		if (x > left && widthLeft < width) {
			x = left;
			y += st::msgDateFont->height;
			widthLeft = availableWidth;
		}
		if (reaction.image.isNull()) {
			reaction.image = _reactionsOwner->resolveImageFor(
				reaction.emoji,
				::Data::Reactions::ImageSize::BottomInfo);
		}
		if (!reaction.image.isNull()) {
			p.drawImage(
				x,
				y + (st::msgDateFont->height - st::reactionInfoSize) / 2,
				reaction.image);
		}
		if (reaction.countTextWidth > 0) {
			p.drawText(
				x + st::reactionInfoSize + st::reactionInfoSkip,
				y + st::msgDateFont->ascent,
				reaction.countText);
		}
		x += width + add;
		widthLeft -= width + add;
	}
}

QSize BottomInfo::countCurrentSize(int newWidth) {
	if (newWidth >= maxWidth()) {
		return optimalSize();
	}
	const auto noReactionsWidth = maxWidth() - _reactionsMaxWidth;
	accumulate_min(newWidth, std::max(noReactionsWidth, _reactionsMaxWidth));
	return QSize(
		newWidth,
		st::msgDateFont->height + countReactionsHeight(newWidth));
}

void BottomInfo::layout() {
	layoutDateText();
	layoutViewsText();
	layoutRepliesText();
	layoutReactionsText();
	initDimensions();
}

void BottomInfo::layoutDateText() {
	const auto edited = (_data.flags & Data::Flag::Edited)
		? (tr::lng_edited(tr::now) + ' ')
		: QString();
	const auto author = _data.author;
	const auto prefix = author.isEmpty() ? qsl(", ") : QString();
	const auto date = edited + _data.date.toString(cTimeFormat());
	_dateWidth = st::msgDateFont->width(date);
	const auto afterAuthor = prefix + date;
	const auto afterAuthorWidth = st::msgDateFont->width(afterAuthor);
	const auto authorWidth = st::msgDateFont->width(author);
	const auto maxWidth = st::maxSignatureSize;
	_authorElided = !author.isEmpty()
		&& (authorWidth + afterAuthorWidth > maxWidth);
	const auto name = _authorElided
		? st::msgDateFont->elided(author, maxWidth - afterAuthorWidth)
		: author;
	const auto full = (_data.flags & Data::Flag::Sponsored)
		? tr::lng_sponsored(tr::now)
		: name.isEmpty() ? date : (name + afterAuthor);
	_authorEditedDate.setText(
		st::msgDateTextStyle,
		full,
		Ui::NameTextOptions());
}

void BottomInfo::layoutViewsText() {
	if (!_data.views || (_data.flags & Data::Flag::Sending)) {
		_views.clear();
		return;
	}
	_views.setText(
		st::msgDateTextStyle,
		Lang::FormatCountToShort(std::max(*_data.views, 1)).string,
		Ui::NameTextOptions());
}

void BottomInfo::layoutRepliesText() {
	if (!_data.replies
		|| !*_data.replies
		|| (_data.flags & Data::Flag::RepliesContext)
		|| (_data.flags & Data::Flag::Sending)) {
		_replies.clear();
		return;
	}
	_replies.setText(
		st::msgDateTextStyle,
		Lang::FormatCountToShort(*_data.replies).string,
		Ui::NameTextOptions());
}

void BottomInfo::layoutReactionsText() {
	if (_data.reactions.empty()) {
		_reactions.clear();
		return;
	}
	auto sorted = ranges::view::all(
		_data.reactions
	) | ranges::view::transform([](const auto &pair) {
		return std::make_pair(pair.first, pair.second);
	}) | ranges::to_vector;
	ranges::sort(sorted, std::greater<>(), &std::pair<QString, int>::second);

	auto reactions = std::vector<Reaction>();
	reactions.reserve(sorted.size());
	for (const auto &[emoji, count] : sorted) {
		const auto i = ranges::find(_reactions, emoji, &Reaction::emoji);
		reactions.push_back((i != end(_reactions))
			? std::move(*i)
			: prepareReactionWithEmoji(emoji));
		setReactionCount(reactions.back(), count);
	}
	_reactions = std::move(reactions);
}

QSize BottomInfo::countOptimalSize() {
	auto width = 0;
	if (_data.flags & (Data::Flag::OutLayout | Data::Flag::Sending)) {
		width += st::historySendStateSpace;
	}
	width += _authorEditedDate.maxWidth();
	if (!_views.isEmpty()) {
		width += st::historyViewsSpace
			+ _views.maxWidth()
			+ st::historyViewsWidth;
	}
	if (!_replies.isEmpty()) {
		width += st::historyViewsSpace
			+ _replies.maxWidth()
			+ st::historyViewsWidth;
	}
	_reactionsMaxWidth = countReactionsMaxWidth();
	width += _reactionsMaxWidth;
	return QSize(width, st::msgDateFont->height);
}

BottomInfo::Reaction BottomInfo::prepareReactionWithEmoji(
		const QString &emoji) {
	auto result = Reaction{ .emoji = emoji };
	_reactionsOwner->preloadImageFor(emoji);
	return result;
}

void BottomInfo::setReactionCount(Reaction &reaction, int count) {
	if (reaction.count == count) {
		return;
	}
	reaction.count = count;
	reaction.countText = (count > 1)
		? Lang::FormatCountToShort(count).string
		: QString();
	reaction.countTextWidth = (count > 1)
		? st::msgDateFont->width(reaction.countText)
		: 0;
}

BottomInfo::Data BottomInfoDataFromMessage(not_null<Message*> message) {
	using Flag = BottomInfo::Data::Flag;
	const auto item = message->message();

	auto result = BottomInfo::Data();
	result.date = message->dateTime();
	if (message->embedReactionsInBottomInfo()) {
		result.reactions = item->reactions();
	}
	if (message->hasOutLayout()) {
		result.flags |= Flag::OutLayout;
	}
	if (message->context() == Context::Replies) {
		result.flags |= Flag::RepliesContext;
	}
	if (item->isSponsored()) {
		result.flags |= Flag::Sponsored;
	}
	if (const auto msgsigned = item->Get<HistoryMessageSigned>()) {
		 if (!msgsigned->isAnonymousRank) {
			result.author = msgsigned->author;
		 }
	}
	if (!item->hideEditedBadge()) {
		if (const auto edited = message->displayedEditBadge()) {
			result.flags |= Flag::Edited;
		}
	}
	if (const auto views = item->Get<HistoryMessageViews>()) {
		if (views->views.count >= 0) {
			result.views = views->views.count;
		}
		if (views->replies.count >= 0 && !views->commentsMegagroupId) {
			result.replies = views->replies.count;
		}
	}
	if (item->isSending() || item->hasFailed()) {
		result.flags |= Flag::Sending;
	}
	// We don't want to pass and update it in Date for now.
	//if (item->unread()) {
	//	result.flags |= Flag::Unread;
	//}
	return result;
}

} // namespace HistoryView
