/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/reactions/history_view_reactions.h"

#include "history/history_message.h"
#include "history/history.h"
#include "history/view/reactions/history_view_reactions_animation.h"
#include "history/view/history_view_message.h"
#include "history/view/history_view_cursor_state.h"
#include "history/view/history_view_group_call_bar.h"
#include "core/click_handler_types.h"
#include "data/stickers/data_custom_emoji.h"
#include "data/data_message_reactions.h"
#include "data/data_peer.h"
#include "data/data_user.h"
#include "data/data_session.h"
#include "main/main_session.h"
#include "lang/lang_tag.h"
#include "ui/text/text_custom_emoji.h"
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

struct InlineList::Button {
	QRect geometry;
	mutable std::unique_ptr<Animation> animation;
	mutable QImage image;
	mutable ClickHandlerPtr link;
	mutable std::unique_ptr<Ui::Text::CustomEmoji> custom;
	std::unique_ptr<Userpics> userpics;
	ReactionId id;
	QString countText;
	int count = 0;
	int countTextWidth = 0;
	bool chosen = false;
};

InlineList::InlineList(
	not_null<::Data::Reactions*> owner,
	Fn<ClickHandlerPtr(ReactionId)> handlerFactory,
	Fn<void()> customEmojiRepaint,
	Data &&data)
: _owner(owner)
, _handlerFactory(std::move(handlerFactory))
, _customEmojiRepaint(std::move(customEmojiRepaint))
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

bool InlineList::hasCustomEmoji() const {
	return _hasCustomEmoji;
}

void InlineList::unloadCustomEmoji() {
	if (!hasCustomEmoji()) {
		return;
	}
	for (const auto &button : _buttons) {
		if (const auto custom = button.custom.get()) {
			custom->unload();
		}
	}
	_customCache = QImage();
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
	) | ranges::view::transform([](const MessageReaction &reaction) {
		return not_null{ &reaction };
	}) | ranges::to_vector;
	const auto &list = _owner->list(::Data::Reactions::Type::All);
	ranges::sort(sorted, [&](
			not_null<const MessageReaction*> a,
			not_null<const MessageReaction*> b) {
		const auto acount = a->count - (a->my ? 1 : 0);
		const auto bcount = b->count - (b->my ? 1 : 0);
		if (acount > bcount) {
			return true;
		} else if (acount < bcount) {
			return false;
		}
		return ranges::find(list, a->id, &::Data::Reaction::id)
			< ranges::find(list, b->id, &::Data::Reaction::id);
	});

	_hasCustomEmoji = false;
	auto buttons = std::vector<Button>();
	buttons.reserve(sorted.size());
	for (const auto &reaction : sorted) {
		const auto &id = reaction->id;
		const auto i = ranges::find(_buttons, id, &Button::id);
		buttons.push_back((i != end(_buttons))
			? std::move(*i)
			: prepareButtonWithId(id));
		const auto j = _data.recent.find(id);
		if (j != end(_data.recent) && !j->second.empty()) {
			setButtonUserpics(buttons.back(), j->second);
		} else {
			setButtonCount(buttons.back(), reaction->count);
		}
		buttons.back().chosen = reaction->my;
		if (id.custom()) {
			_hasCustomEmoji = true;
		}
	}
	_buttons = std::move(buttons);
}

InlineList::Button InlineList::prepareButtonWithId(const ReactionId &id) {
	auto result = Button{ .id = id };
	if (const auto customId = id.custom()) {
		result.custom = _owner->owner().customEmojiManager().create(
			customId,
			_customEmojiRepaint);
	} else {
		_owner->preloadImageFor(id);
	}
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

	auto finished = std::vector<std::unique_ptr<Animation>>();
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
			// Let the animation (and its custom emoji) live while painting.
			finished.push_back(std::move(button.animation));
		}
		const auto animating = (button.animation != nullptr);
		const auto &geometry = button.geometry;
		const auto mine = button.chosen;
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
		if (!button.custom && button.image.isNull()) {
			button.image = _owner->resolveImageFor(
				button.id,
				::Data::Reactions::ImageSize::InlineList);
		}
		const auto textFg = !inbubble
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
				: st->historyFileInIconFg());
		const auto image = QRect(
			inner.topLeft() + QPoint(skip, skip),
			QSize(st::reactionInlineImage, st::reactionInlineImage));
		if (!skipImage) {
			if (const auto custom = button.custom.get()) {
				paintCustomFrame(
					p,
					custom,
					inner.topLeft(),
					context.now,
					textFg.color());
			} else if (!button.image.isNull()) {
				p.drawImage(image.topLeft(), button.image);
			}
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
			p.setPen(textFg);
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
		const auto now = context.now;
		context.reactionInfo->effectPaint = [=](QPainter &p) {
			auto result = QRect();
			for (const auto &single : animations) {
				const auto area = single.animation->paintGetArea(
					p,
					QPoint(),
					single.target,
					now);
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
				button.link = _handlerFactory(button.id);
				button.link->setProperty(
					kReactionsCountEmojiProperty,
					QVariant::fromValue(button.id));
				_owner->preloadAnimationsFor(button.id);
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
	const auto i = ranges::find(_buttons, args.id, &Button::id);
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

void InlineList::paintCustomFrame(
		Painter &p,
		not_null<Ui::Text::CustomEmoji*> emoji,
		QPoint innerTopLeft,
		crl::time now,
		const QColor &preview) const {
	if (_customCache.isNull()) {
		using namespace Ui::Text;
		const auto size = st::emojiSize;
		const auto factor = style::DevicePixelRatio();
		const auto adjusted = AdjustCustomEmojiSize(size);
		_customCache = QImage(
			QSize(adjusted, adjusted) * factor,
			QImage::Format_ARGB32_Premultiplied);
		_customCache.setDevicePixelRatio(factor);
		_customSkip = (size - adjusted) / 2;
	}
	_customCache.fill(Qt::transparent);
	auto q = QPainter(&_customCache);
	emoji->paint(q, {
		.preview = preview,
		.now = now,
		.paused = p.inactive(),
	});
	q.end();
	_customCache = Images::Round(
		std::move(_customCache),
		(Images::Option::RoundLarge
			| Images::Option::RoundSkipTopRight
			| Images::Option::RoundSkipBottomRight));

	p.drawImage(
		innerTopLeft + QPoint(_customSkip, _customSkip),
		_customCache);
}

auto InlineList::takeAnimations()
-> base::flat_map<ReactionId, std::unique_ptr<Reactions::Animation>> {
	auto result = base::flat_map<
		ReactionId,
		std::unique_ptr<Reactions::Animation>>();
	for (auto &button : _buttons) {
		if (button.animation) {
			result.emplace(button.id, std::move(button.animation));
		}
	}
	return result;
}

void InlineList::continueAnimations(base::flat_map<
		ReactionId,
		std::unique_ptr<Reactions::Animation>> animations) {
	for (auto &[id, animation] : animations) {
		const auto i = ranges::find(_buttons, id, &Button::id);
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
	if (const auto user = item->history()->peer->asUser()) {
		// Always show userpics, we have all information.
		result.recent.reserve(result.reactions.size());
		const auto self = user->session().user();
		for (const auto &reaction : result.reactions) {
			auto &list = result.recent[reaction.id];
			list.reserve(reaction.count);
			if (!reaction.my || reaction.count > 1) {
				list.push_back(user);
			}
			if (reaction.my) {
				list.push_back(self);
			}
		}
	} else {
		const auto &recent = item->recentReactions();
		const auto showUserpics = [&] {
			if (recent.size() != result.reactions.size()) {
				return false;
			}
			auto sum = 0;
			for (const auto &reaction : result.reactions) {
				if ((sum += reaction.count) > kMaxRecentUserpics) {
					return false;
				}
				const auto i = recent.find(reaction.id);
				if (i == end(recent) || reaction.count != i->second.size()) {
					return false;
				}
			}
			return true;
		}();
		if (showUserpics) {
			result.recent.reserve(recent.size());
			for (const auto &[id, list] : recent) {
				result.recent.emplace(id).first->second = list
					| ranges::view::transform(&Data::RecentReaction::peer)
					| ranges::to_vector;
			}
		}
	}
	result.flags = (message->hasOutLayout() ? Flag::OutLayout : Flag())
		| (message->embedReactionsInBubble() ? Flag::InBubble : Flag());
	return result;
}

} // namespace HistoryView
