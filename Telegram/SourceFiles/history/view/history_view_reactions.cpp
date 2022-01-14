/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_reactions.h"

#include "history/history_message.h"
#include "history/history.h"
#include "history/view/history_view_message.h"
#include "history/view/history_view_cursor_state.h"
#include "data/data_message_reactions.h"
#include "lang/lang_tag.h"
#include "ui/chat/chat_style.h"
#include "styles/style_chat.h"

namespace HistoryView::Reactions {
namespace {

constexpr auto kInNonChosenOpacity = 0.12;
constexpr auto kOutNonChosenOpacity = 0.18;

[[nodiscard]] QColor AdaptChosenServiceFg(QColor serviceBg) {
	serviceBg.setAlpha(std::max(serviceBg.alpha(), 192));
	return serviceBg;
}

} // namespace

InlineList::InlineList(
	not_null<::Data::Reactions*> owner,
	Fn<ClickHandlerPtr(QString)> handlerFactory,
	Data &&data)
: _owner(owner)
, _handlerFactory(std::move(handlerFactory))
, _data(std::move(data)) {
	layout();
}

void InlineList::update(Data &&data, int availableWidth) {
	_data = std::move(data);
	layout();
	if (width() > 0) {
		resizeGetHeight(std::min(maxWidth(), availableWidth));
	}
}

void InlineList::updateSkipBlock(int width, int height) {
	_skipBlock = { width, height };
}

void InlineList::removeSkipBlock() {
	_skipBlock = {};
}

void InlineList::layout() {
	layoutButtons();
	initDimensions();
}

void InlineList::layoutButtons() {
	if (_data.reactions.empty()) {
		_buttons.clear();
		return;
	}
	auto sorted = ranges::view::all(
		_data.reactions
	) | ranges::view::transform([](const auto &pair) {
		return std::make_pair(pair.first, pair.second);
	}) | ranges::to_vector;
	const auto &list = _owner->list(::Data::Reactions::Type::All);
	ranges::sort(sorted, [&](const auto &p1, const auto &p2) {
		if (p1.second > p2.second) {
			return true;
		} else if (p1.second < p2.second) {
			return false;
		}
		return ranges::find(list, p1.first, &::Data::Reaction::emoji)
			< ranges::find(list, p2.first, &::Data::Reaction::emoji);
	});

	auto buttons = std::vector<Button>();
	buttons.reserve(sorted.size());
	for (const auto &[emoji, count] : sorted) {
		const auto i = ranges::find(_buttons, emoji, &Button::emoji);
		buttons.push_back((i != end(_buttons))
			? std::move(*i)
			: prepareButtonWithEmoji(emoji));
		const auto add = (emoji == _data.chosenReaction) ? 1 : 0;
		setButtonCount(buttons.back(), count + add);
	}
	_buttons = std::move(buttons);
}

InlineList::Button InlineList::prepareButtonWithEmoji(const QString &emoji) {
	auto result = Button{ .emoji = emoji };
	_owner->preloadImageFor(emoji);
	return result;
}

void InlineList::setButtonCount(Button &button, int count) {
	if (button.count == count) {
		return;
	}
	button.count = count;
	button.countText = Lang::FormatCountToShort(count).string;
	button.countTextWidth = st::semiboldFont->width(button.countText);
}

QSize InlineList::countOptimalSize() {
	if (_buttons.empty()) {
		return _skipBlock;
	}
	const auto left = (_data.flags & InlineListData::Flag::InBubble)
		? st::reactionBottomInBubbleLeft
		: 0;
	auto x = left;
	const auto between = st::reactionBottomBetween;
	const auto padding = st::reactionBottomPadding;
	const auto size = st::reactionBottomSize;
	const auto widthBase = padding.left()
		+ size
		+ st::reactionBottomSkip
		+ padding.right();
	const auto height = padding.top() + size + padding.bottom();
	for (auto &button : _buttons) {
		const auto width = widthBase + button.countTextWidth;
		button.geometry.setSize({ width, height });
		x += width + between;
	}
	return QSize(
		x - between + _skipBlock.width(),
		std::max(height, _skipBlock.height()));
}

QSize InlineList::countCurrentSize(int newWidth) {
	if (_buttons.empty()) {
		return optimalSize();
	}
	using Flag = InlineListData::Flag;
	const auto between = st::reactionBottomBetween;
	const auto inBubble = (_data.flags & Flag::InBubble);
	const auto left = inBubble ? st::reactionBottomInBubbleLeft : 0;
	auto x = left;
	auto y = 0;
	for (auto &button : _buttons) {
		const auto size = button.geometry.size();
		if (x > left && x + size.width() > newWidth) {
			x = left;
			y += size.height() + between;
		}
		button.geometry = QRect(QPoint(x, y), size);
		x += size.width() + between;
	}
	const auto &last = _buttons.back().geometry;
	const auto height = y + last.height();
	const auto right = last.x() + last.width() + _skipBlock.width();
	const auto add = (right > newWidth) ? _skipBlock.height() : 0;
	return { newWidth, height + add };
}

void InlineList::flipToRight() {
	for (auto &button : _buttons) {
		button.geometry.moveLeft(
			width() - button.geometry.x() - button.geometry.width());
	}
}

int InlineList::placeAndResizeGetHeight(QRect available) {
	const auto result = resizeGetHeight(available.width());
	for (auto &button : _buttons) {
		button.geometry.translate(available.x(), 0);
	}
	return result;
}

void InlineList::paint(
		Painter &p,
		const PaintContext &context,
		int outerWidth,
		const QRect &clip) const {
	const auto st = context.st;
	const auto stm = context.messageStyle();
	const auto padding = st::reactionBottomPadding;
	const auto size = st::reactionBottomSize;
	const auto inbubble = (_data.flags & InlineListData::Flag::InBubble);
	p.setFont(st::semiboldFont);
	for (const auto &button : _buttons) {
		const auto &geometry = button.geometry;
		const auto inner = geometry.marginsRemoved(padding);
		const auto chosen = (_data.chosenReaction == button.emoji);
		{
			auto hq = PainterHighQualityEnabler(p);
			p.setPen(Qt::NoPen);
			if (inbubble) {
				if (!chosen) {
					p.setOpacity(context.outbg
						? kOutNonChosenOpacity
						: kInNonChosenOpacity);
				}
				p.setBrush(stm->msgFileBg);
			} else {
				p.setBrush(chosen ? st->msgServiceFg() : st->msgServiceBg());
			}
			const auto radius = geometry.height() / 2.;
			p.drawRoundedRect(geometry, radius, radius);
			if (inbubble && !chosen) {
				p.setOpacity(1.);
			}
		}
		if (button.image.isNull()) {
			button.image = _owner->resolveImageFor(
				button.emoji,
				::Data::Reactions::ImageSize::InlineList);
		}
		if (!button.image.isNull()) {
			p.drawImage(inner.topLeft(), button.image);
		}
		p.setPen(!inbubble
			? (chosen
				? QPen(AdaptChosenServiceFg(st->msgServiceBg()->c))
				: st->msgServiceFg())
			: !chosen
			? stm->msgServiceFg
			: context.outbg
			? (context.selected()
				? st->historyFileOutIconFgSelected()
				: st->historyFileOutIconFg())
			: (context.selected()
				? st->historyFileInIconFgSelected()
				: st->historyFileInIconFg()));
		const auto textTop = geometry.y()
			+ ((geometry.height() - st::semiboldFont->height) / 2);
		p.drawText(
			inner.x() + size + st::reactionBottomSkip,
			textTop + st::semiboldFont->ascent,
			button.countText);
	}
}

bool InlineList::getState(
		QPoint point,
		not_null<TextState*> outResult) const {
	const auto left = (_data.flags & InlineListData::Flag::InBubble)
		? st::reactionBottomInBubbleLeft
		: 0;
	if (!QRect(left, 0, width() - left, height()).contains(point)) {
		return false;
	}
	for (const auto &button : _buttons) {
		if (button.geometry.contains(point)) {
			if (!button.link) {
				button.link = _handlerFactory(button.emoji);
			}
			outResult->link = button.link;
			return true;
		}
	}
	return false;
}

InlineListData InlineListDataFromMessage(not_null<Message*> message) {
	using Flag = InlineListData::Flag;
	const auto item = message->message();

	auto result = InlineListData();
	result.reactions = item->reactions();
	result.chosenReaction = item->chosenReaction();
	if (!result.chosenReaction.isEmpty()) {
		--result.reactions[result.chosenReaction];
	}
	result.flags = (message->hasOutLayout() ? Flag::OutLayout : Flag())
		| (message->embedReactionsInBubble() ? Flag::InBubble : Flag());
	return result;
}

} // namespace HistoryView
