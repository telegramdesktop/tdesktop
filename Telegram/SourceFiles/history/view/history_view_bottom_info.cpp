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
#include "history/view/history_view_message.h"
#include "history/view/history_view_cursor_state.h"
#include "data/data_message_reactions.h"
#include "styles/style_chat.h"
#include "styles/style_dialogs.h"

namespace HistoryView {

BottomInfo::BottomInfo(Data &&data, Context &&context)
: _data(std::move(data))
, _context(std::move(context))
, _reactions(st::msgMinWidth / 2) {
	layout();
}

void BottomInfo::update(Data &&data, Context &&context, int availableWidth) {
	_data = std::move(data);
	_context = std::move(context);
	layout();
	if (!_size.isEmpty()) {
		resizeToWidth(std::min(optimalSize().width(), availableWidth));
	}
}

QSize BottomInfo::optimalSize() const {
	return _optimalSize;
}

QSize BottomInfo::size() const {
	return _size;
}

int BottomInfo::firstLineWidth() const {
	if (_size.height() == _optimalSize.height()) {
		return _size.width();
	}
	const auto reactionsWidth = _reactions.maxWidth();
	const auto noReactionsWidth = _optimalSize.width()
		- st::historyReactionsSkip
		- reactionsWidth;
	return noReactionsWidth;
}

TextState BottomInfo::textState(
		not_null<const HistoryItem*> item,
		QPoint position) const {
	auto result = TextState(item);
	if (!_reactions.isEmpty()) {
		const auto reactionsPosition = [&] {
			if (_size.height() == _optimalSize.height()) {
				return QPoint(0, 0);
			}
			const auto available = _size.width();
			const auto use = std::min(available, _reactions.maxWidth());
			return QPoint(_size.width() - use, st::msgDateFont->height);
		}();
		const auto state = _reactions.getStateLeft(
			position - reactionsPosition,
			std::min(_size.width(), _reactions.maxWidth()),
			_size.width());
		if (state.uponSymbol) {
			result.link = _context.reactions;
			return result;
		}
	}
	const auto inTime = QRect(
		_size.width() - _dateWidth,
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
	const auto sti = context.imageStyle();
	const auto stm = context.messageStyle();

	auto right = position.x() + _size.width();
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
	if (!_reactions.isEmpty()) {
		if (_size.height() == _optimalSize.height()) {
			_reactions.drawLeft(
				p,
				position.x(),
				position.y(),
				_reactions.maxWidth(),
				outerWidth);
		} else {
			const auto available = _size.width();
			const auto use = std::min(available, _reactions.maxWidth());
			_reactions.drawLeft(
				p,
				position.x() + _size.width() - use,
				position.y() + st::msgDateFont->height,
				use,
				outerWidth);
		}
	}
}

int BottomInfo::resizeToWidth(int newWidth) {
	if (newWidth >= _optimalSize.width()) {
		_size = _optimalSize;
	} else {
		const auto reactionsWidth = _reactions.maxWidth();
		const auto noReactionsWidth = _optimalSize.width()
			- st::historyReactionsSkip
			- reactionsWidth;
		accumulate_min(newWidth, std::max(noReactionsWidth, reactionsWidth));
		_size = QSize(
			newWidth,
			st::msgDateFont->height + _reactions.countHeight(newWidth));
	}
	return _size.height();
}

void BottomInfo::layout() {
	layoutDateText();
	layoutViewsText();
	layoutRepliesText();
	layoutReactionsText();
	countOptimalSize();
}

void BottomInfo::layoutDateText() {
	const auto edited = (_data.flags & Data::Flag::Edited)
		? (tr::lng_edited(tr::now) + ' ')
		: QString();
	const auto author = _data.author;
	const auto prefix = author.isEmpty() ? qsl(", ") : QString();
	const auto date = _data.date.toString(cTimeFormat());
	_dateWidth = st::msgDateFont->width(date);
	const auto afterAuthor = prefix + edited + date;
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
		: name + date;
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

	auto text = TextWithEntities();
	for (const auto &[string, count] : sorted) {
		if (!text.text.isEmpty()) {
			text.append(" - ");
		}
		const auto chosen = (_data.chosenReaction == string);
		text.append(string);
		if (_data.chosenReaction == string) {
			text.append(Ui::Text::Bold(QString::number(count)));
		} else {
			text.append(QString::number(count));
		}
	}

	_reactions.setMarkedText(
		st::msgDateTextStyle,
		text,
		Ui::NameTextOptions());
}

void BottomInfo::countOptimalSize() {
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
	if (!_reactions.isEmpty()) {
		width += st::historyReactionsSkip + _reactions.maxWidth();
	}
	_optimalSize = QSize(width, st::msgDateFont->height);
}

BottomInfo::Data BottomInfoDataFromMessage(not_null<Message*> message) {
	using Flag = BottomInfo::Data::Flag;

	auto result = BottomInfo::Data();

	const auto item = message->message();
	result.date = message->dateTime();
	result.reactions = item->reactions();
	result.chosenReaction = item->chosenReaction();
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

BottomInfo::Context BottomInfoContextFromMessage(
		not_null<Message*> message) {
	auto result = BottomInfo::Context();
	result.reactions = std::make_shared<LambdaClickHandler>([=] {
		message->delegate()->elementShowReactions(message);
	});
	return result;
}

} // namespace HistoryView
