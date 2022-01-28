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
#include "history/view/history_view_react_animation.h"
#include "history/view/history_view_group_call_bar.h"
#include "core/click_handler_types.h"
#include "data/data_message_reactions.h"
#include "data/data_peer.h"
#include "lang/lang_tag.h"
#include "ui/chat/chat_style.h"
#include "styles/style_chat.h"

namespace HistoryView::Reactions {
namespace {

constexpr auto kInNonChosenOpacity = 0.12;
constexpr auto kOutNonChosenOpacity = 0.18;
constexpr auto kMaxRecentUserpics = 3;
constexpr auto kMaxNicePerRow = 5;

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

InlineList::~InlineList() = default;

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
		const auto j = _data.recent.find(emoji);
		if (j != end(_data.recent) && !j->second.empty()) {
			setButtonUserpics(buttons.back(), j->second);
		} else {
			setButtonCount(buttons.back(), count + add);
		}
	}
	_buttons = std::move(buttons);
}

InlineList::Button InlineList::prepareButtonWithEmoji(const QString &emoji) {
	auto result = Button{ .emoji = emoji };
	_owner->preloadImageFor(emoji);
	return result;
}

void InlineList::setButtonCount(Button &button, int count) {
	if (button.count == count && !button.userpics) {
		return;
	}
	button.userpics = nullptr;
	button.count = count;
	button.countText = Lang::FormatCountToShort(count).string;
	button.countTextWidth = st::semiboldFont->width(button.countText);
}

void InlineList::setButtonUserpics(
		Button &button,
		const std::vector<not_null<PeerData*>> &peers) {
	if (!button.userpics) {
		button.userpics = std::make_unique<Userpics>();
	}
	const auto count = button.count = int(peers.size());
	auto &list = button.userpics->list;
	const auto regenerate = [&] {
		if (list.size() != count) {
			return true;
		}
		for (auto i = 0; i != count; ++i) {
			if (peers[i] != list[i].peer) {
				return true;
			}
		}
		return false;
	}();
	if (!regenerate) {
		return;
	}
	auto generated = std::vector<UserpicInRow>();
	generated.reserve(count);
	for (auto i = 0; i != count; ++i) {
		if (i == list.size()) {
			list.push_back(UserpicInRow{
				peers[i]
			});
		} else if (list[i].peer != peers[i]) {
			list[i].peer = peers[i];
		}
	}
	while (list.size() > count) {
		list.pop_back();
	}
	button.userpics->image = QImage();
}

QSize InlineList::countOptimalSize() {
	if (_buttons.empty()) {
		return _skipBlock;
	}
	const auto left = (_data.flags & InlineListData::Flag::InBubble)
		? st::reactionInlineInBubbleLeft
		: 0;
	auto x = left;
	const auto between = st::reactionInlineBetween;
	const auto padding = st::reactionInlinePadding;
	const auto size = st::reactionInlineSize;
	const auto widthBaseCount = padding.left()
		+ size
		+ st::reactionInlineSkip
		+ padding.right();
	const auto widthBaseUserpics = padding.left()
		+ size
		+ st::reactionInlineUserpicsPadding.left()
		+ st::reactionInlineUserpicsPadding.right();
	const auto userpicsWidth = [](const Button &button) {
		const auto count = int(button.userpics->list.size());
		const auto single = st::reactionInlineUserpics.size;
		const auto shift = st::reactionInlineUserpics.shift;
		const auto width = single + (count - 1) * (single - shift);
		return width;
	};
	const auto height = padding.top() + size + padding.bottom();
	for (auto &button : _buttons) {
		const auto width = button.userpics
			? (widthBaseUserpics + userpicsWidth(button))
			: (widthBaseCount + button.countTextWidth);
		button.geometry.setSize({ width, height });
		x += width + between;
	}
	return QSize(
		x - between + _skipBlock.width(),
		std::max(height, _skipBlock.height()));
}

QSize InlineList::countCurrentSize(int newWidth) {
	_data.flags &= ~Data::Flag::Flipped;
	if (_buttons.empty()) {
		return optimalSize();
	}
	using Flag = InlineListData::Flag;
	const auto between = st::reactionInlineBetween;
	const auto inBubble = (_data.flags & Flag::InBubble);
	const auto left = inBubble ? st::reactionInlineInBubbleLeft : 0;
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

int InlineList::countNiceWidth() const {
	const auto count = _data.reactions.size();
	const auto rows = (count + kMaxNicePerRow - 1) / kMaxNicePerRow;
	const auto columns = (count + rows - 1) / rows;
	const auto between = st::reactionInlineBetween;
	auto result = 0;
	auto inrow = 0;
	auto x = 0;
	for (auto &button : _buttons) {
		if (inrow++ >= columns) {
			x = 0;
			inrow = 0;
		}
		x += button.geometry.width() + between;
		accumulate_max(result, x - between);
	}
	return result;
}

void InlineList::flipToRight() {
	_data.flags |= Data::Flag::Flipped;
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
	struct SingleAnimation {
		not_null<Reactions::Animation*> animation;
		QRect target;
	};
	std::vector<SingleAnimation> animations;

	const auto st = context.st;
	const auto stm = context.messageStyle();
	const auto padding = st::reactionInlinePadding;
	const auto size = st::reactionInlineSize;
	const auto skip = (size - st::reactionInlineImage) / 2;
	const auto inbubble = (_data.flags & InlineListData::Flag::InBubble);
	const auto flipped = (_data.flags & Data::Flag::Flipped);
	p.setFont(st::semiboldFont);
	for (const auto &button : _buttons) {
		if (context.reactionInfo
			&& button.animation
			&& button.animation->finished()) {
			button.animation = nullptr;
		}
		const auto animating = (button.animation != nullptr);
		const auto &geometry = button.geometry;
		const auto mine = (_data.chosenReaction == button.emoji);
		const auto withoutMine = button.count - (mine ? 1 : 0);
		const auto skipImage = animating
			&& (withoutMine < 1 || !button.animation->flying());
		const auto bubbleProgress = skipImage
			? button.animation->flyingProgress()
			: 1.;
		const auto bubbleReady = (bubbleProgress == 1.);
		const auto bubbleSkip = anim::interpolate(
			geometry.height() - geometry.width(),
			0,
			bubbleProgress);
		const auto inner = geometry.marginsRemoved(padding);
		const auto chosen = mine
			&& (!animating || !button.animation->flying() || skipImage);
		if (bubbleProgress > 0.) {
			auto hq = PainterHighQualityEnabler(p);
			p.setPen(Qt::NoPen);
			if (inbubble) {
				if (!chosen) {
					p.setOpacity(bubbleProgress * (context.outbg
						? kOutNonChosenOpacity
						: kInNonChosenOpacity));
				} else if (!bubbleReady) {
					p.setOpacity(bubbleProgress);
				}
				p.setBrush(stm->msgFileBg);
			} else {
				if (!bubbleReady) {
					p.setOpacity(bubbleProgress);
				}
				p.setBrush(chosen ? st->msgServiceFg() : st->msgServiceBg());
			}
			const auto radius = geometry.height() / 2.;
			const auto fill = geometry.marginsAdded({
				flipped ? bubbleSkip : 0,
				0,
				flipped ? 0 : bubbleSkip,
				0,
			});
			p.drawRoundedRect(fill, radius, radius);
			if (inbubble && !chosen) {
				p.setOpacity(bubbleProgress);
			}
		}
		if (button.image.isNull()) {
			button.image = _owner->resolveImageFor(
				button.emoji,
				::Data::Reactions::ImageSize::InlineList);
		}
		const auto image = QRect(
			inner.topLeft() + QPoint(skip, skip),
			QSize(st::reactionInlineImage, st::reactionInlineImage));
		if (!button.image.isNull() && !skipImage) {
			p.drawImage(image.topLeft(), button.image);
		}
		if (animating) {
			animations.push_back({
				.animation = button.animation.get(),
				.target = image,
			});
		}
		if (bubbleProgress == 0.) {
			continue;
		}
		resolveUserpicsImage(button);
		const auto left = inner.x() + (flipped ? 0 : bubbleSkip);
		if (button.userpics) {
			p.drawImage(
				left + size + st::reactionInlineUserpicsPadding.left(),
				geometry.y() + st::reactionInlineUserpicsPadding.top(),
				button.userpics->image);
		} else {
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
				left + size + st::reactionInlineSkip,
				textTop + st::semiboldFont->ascent,
				button.countText);
		}
		if (!bubbleReady) {
			p.setOpacity(1.);
		}
	}
	if (!animations.empty()) {
		context.reactionInfo->effectPaint = [=](QPainter &p) {
			auto result = QRect();
			for (const auto &single : animations) {
				const auto area = single.animation->paintGetArea(
					p,
					QPoint(),
					single.target);
				result = result.isEmpty() ? area : result.united(area);
			}
			return result;
		};
	}
}

bool InlineList::getState(
		QPoint point,
		not_null<TextState*> outResult) const {
	const auto left = (_data.flags & InlineListData::Flag::InBubble)
		? st::reactionInlineInBubbleLeft
		: 0;
	if (!QRect(left, 0, width() - left, height()).contains(point)) {
		return false;
	}
	for (const auto &button : _buttons) {
		if (button.geometry.contains(point)) {
			if (!button.link) {
				button.link = _handlerFactory(button.emoji);
				button.link->setProperty(
					kReactionsCountEmojiProperty,
					button.emoji);
				_owner->preloadAnimationsFor(button.emoji);
			}
			outResult->link = button.link;
			return true;
		}
	}
	return false;
}

void InlineList::animate(
		ReactionAnimationArgs &&args,
		Fn<void()> repaint) {
	const auto i = ranges::find(_buttons, args.emoji, &Button::emoji);
	if (i == end(_buttons)) {
		return;
	}
	i->animation = std::make_unique<Reactions::Animation>(
		_owner,
		std::move(args),
		std::move(repaint),
		st::reactionInlineImage);
}

void InlineList::resolveUserpicsImage(const Button &button) const {
	const auto userpics = button.userpics.get();
	const auto regenerate = [&] {
		if (!userpics) {
			return false;
		} else if (userpics->image.isNull()) {
			return true;
		}
		for (auto &entry : userpics->list) {
			const auto peer = entry.peer;
			auto &view = entry.view;
			const auto wasView = view.get();
			if (peer->userpicUniqueKey(view) != entry.uniqueKey
				|| view.get() != wasView) {
				return true;
			}
		}
		return false;
	}();
	if (!regenerate) {
		return;
	}
	GenerateUserpicsInRow(
		userpics->image,
		userpics->list,
		st::reactionInlineUserpics,
		kMaxRecentUserpics);
}

auto InlineList::takeAnimations()
-> base::flat_map<QString, std::unique_ptr<Reactions::Animation>> {
	auto result = base::flat_map<
		QString,
		std::unique_ptr<Reactions::Animation>>();
	for (auto &button : _buttons) {
		if (button.animation) {
			result.emplace(button.emoji, std::move(button.animation));
		}
	}
	return result;
}

void InlineList::continueAnimations(base::flat_map<
		QString,
		std::unique_ptr<Reactions::Animation>> animations) {
	for (auto &[emoji, animation] : animations) {
		const auto i = ranges::find(_buttons, emoji, &Button::emoji);
		if (i != end(_buttons)) {
			i->animation = std::move(animation);
		}
	}
}

InlineListData InlineListDataFromMessage(not_null<Message*> message) {
	using Flag = InlineListData::Flag;
	const auto item = message->message();
	auto result = InlineListData();
	result.reactions = item->reactions();
	const auto &recent = item->recentReactions();
	const auto showUserpics = [&] {
		if (recent.size() != result.reactions.size()) {
			return false;
		}
		auto b = begin(recent);
		auto sum = 0;
		for (const auto &[emoji, count] : result.reactions) {
			sum += count;
			if (emoji != b->first
				|| count != b->second.size()
				|| sum > kMaxRecentUserpics) {
				return false;
			}
			++b;
		}
		return true;
	}();
	if (showUserpics) {
		result.recent.reserve(recent.size());
		for (const auto &[emoji, list] : recent) {
			result.recent.emplace(emoji).first->second = list
				| ranges::view::transform(&Data::RecentReaction::peer)
				| ranges::to_vector;
		}
	}
	result.chosenReaction = item->chosenReaction();
	if (!result.chosenReaction.isEmpty()) {
		--result.reactions[result.chosenReaction];
	}
	result.flags = (message->hasOutLayout() ? Flag::OutLayout : Flag())
		| (message->embedReactionsInBubble() ? Flag::InBubble : Flag());
	return result;
}

} // namespace HistoryView
