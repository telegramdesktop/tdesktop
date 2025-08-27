/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/reactions/history_view_reactions.h"

#include "history/history_item.h"
#include "history/history.h"
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
#include "ui/effects/reaction_fly_animation.h"
#include "ui/painter.h"
#include "ui/power_saving.h"
#include "styles/style_chat.h"
#include "styles/style_chat_helpers.h"

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
	mutable std::unique_ptr<Ui::ReactionFlyAnimation> animation;
	mutable QImage image;
	mutable ClickHandlerPtr link;
	mutable std::unique_ptr<Ui::Text::CustomEmoji> custom;
	std::unique_ptr<Userpics> userpics;
	ReactionId id;
	QString text;
	int textWidth = 0;
	int count = 0;
	bool chosen = false;
	bool paid = false;
	bool tag = false;
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

bool InlineList::areTags() const {
	return _data.flags & Data::Flag::Tags;
}

std::vector<ReactionId> InlineList::computeTagsList() const {
	if (!areTags()) {
		return {};
	}
	return _buttons | ranges::views::transform(
		&Button::id
	) | ranges::to_vector;
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
	auto sorted = ranges::views::all(
		_data.reactions
	) | ranges::views::transform([](const MessageReaction &reaction) {
		return not_null{ &reaction };
	}) | ranges::to_vector;
	const auto tags = areTags();
	if (!tags) {
		const auto &list = _owner->list(::Data::Reactions::Type::All);
		ranges::sort(sorted, [&](
				not_null<const MessageReaction*> a,
				not_null<const MessageReaction*> b) {
			const auto acount = a->count - (a->my ? 1 : 0);
			const auto bcount = b->count - (b->my ? 1 : 0);
			if (b->id.paid()) {
				return false;
			} else if (a->id.paid()) {
				return true;
			} else if (acount > bcount) {
				return true;
			} else if (acount < bcount) {
				return false;
			}
			return ranges::find(list, a->id, &::Data::Reaction::id)
				< ranges::find(list, b->id, &::Data::Reaction::id);
		});
	}

	_hasCustomEmoji = false;
	auto buttons = std::vector<Button>();
	buttons.reserve(sorted.size());
	for (const auto &reaction : sorted) {
		const auto &id = reaction->id;
		const auto i = ranges::find(_buttons, id, &Button::id);
		buttons.push_back((i != end(_buttons))
			? std::move(*i)
			: prepareButtonWithId(id));
		if (tags) {
			setButtonTag(buttons.back(), _owner->myTagTitle(id));
		} else if (const auto j = _data.recent.find(id)
			; j != end(_data.recent) && !j->second.empty()) {
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

InlineList::Dimension InlineList::countDimension(int width) const {
	using Flag = InlineListData::Flag;
	const auto inBubble = (_data.flags & Flag::InBubble);
	const auto centered = (_data.flags & Flag::Centered);
	const auto useWidth = centered
		? std::min(width, st::chatGiveawayWidth)
		: width;
	const auto left = inBubble
		? st::reactionInlineInBubbleLeft
		: centered
		? ((width - useWidth) / 2)
		: 0;
	return { .left = left, .width = useWidth };
}

InlineList::Button InlineList::prepareButtonWithId(const ReactionId &id) {
	auto result = Button{ .id = id, .paid = id.paid()};
	if (const auto customId = id.custom()) {
		result.custom = _owner->owner().customEmojiManager().create(
			customId,
			_customEmojiRepaint);
	} else {
		_owner->preloadReactionImageFor(id);
	}
	return result;
}

void InlineList::setButtonTag(Button &button, const QString &title) {
	if (button.tag && button.text == title) {
		return;
	}
	button.userpics = nullptr;
	button.count = 0;
	button.tag = true;
	button.text = title;
	button.textWidth = st::reactionInlineTagFont->width(button.text);
}

void InlineList::setButtonCount(Button &button, int count) {
	if (!button.tag && button.count == count && !button.userpics) {
		return;
	}
	button.userpics = nullptr;
	button.count = count;
	button.tag = false;
	button.text = Lang::FormatCountToShort(count).string;
	button.textWidth = st::semiboldFont->width(button.text);
}

void InlineList::setButtonUserpics(
		Button &button,
		const std::vector<not_null<PeerData*>> &peers) {
	button.tag = false;
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
	const auto left = countDimension(width()).left;
	auto x = left;
	const auto between = st::reactionInlineBetween;
	const auto padding = st::reactionInlinePadding;
	const auto size = st::reactionInlineSize;
	const auto widthBaseTag = padding.left()
		+ size
		+ st::reactionInlineTagSkip
		+ padding.right();
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
		const auto width = button.tag
			? (widthBaseTag
				+ button.textWidth
				+ (button.textWidth ? st::reactionInlineSkip : 0))
			: button.userpics
			? (widthBaseUserpics + userpicsWidth(button))
			: (widthBaseCount + button.textWidth);
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
	const auto dimension = countDimension(newWidth);
	const auto left = dimension.left;
	const auto width = dimension.width;
	const auto centered = (_data.flags & Flag::Centered);
	auto x = left;
	auto y = 0;
	const auto recenter = [&](int beforeIndex) {
		const auto added = centered ? (left + width + between - x) : 0;
		if (added <= 0) {
			return;
		}
		const auto shift = added / 2;
		for (auto j = beforeIndex; j != 0;) {
			auto &button = _buttons[--j];
			if (button.geometry.y() != y) {
				break;
			}
			button.geometry.translate(shift, 0);
		}
	};
	for (auto i = 0, count = int(_buttons.size()); i != count; ++i) {
		auto &button = _buttons[i];
		const auto size = button.geometry.size();
		if (x > left && x + size.width() > left + width) {
			recenter(i);
			x = left;
			y += size.height() + between;
		}
		button.geometry = QRect(QPoint(x, y), size);
		x += size.width() + between;
	}
	recenter(_buttons.size());
	const auto &last = _buttons.back().geometry;
	const auto height = y + last.height();
	const auto right = last.x() + last.width() + _skipBlock.width();
	const auto add = (right > width) ? _skipBlock.height() : 0;
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
		not_null<Ui::ReactionFlyAnimation*> animation;
		QColor textColor;
		QRect target;
	};
	std::vector<SingleAnimation> animations;

	auto finished = std::vector<std::unique_ptr<Ui::ReactionFlyAnimation>>();
	const auto st = context.st;
	const auto stm = context.messageStyle();
	const auto padding = st::reactionInlinePadding;
	const auto size = st::reactionInlineSize;
	const auto skip = (size - st::reactionInlineImage) / 2;
	const auto tags = areTags();
	const auto inbubble = (_data.flags & Data::Flag::InBubble);
	const auto flipped = (_data.flags & Data::Flag::Flipped);
	p.setFont(tags ? st::reactionInlineTagFont : st::semiboldFont);
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
			auto opacity = 1.;
			auto color = QColor();
			if (inbubble) {
				if (!chosen) {
					opacity = bubbleProgress * (context.outbg
						? kOutNonChosenOpacity
						: kInNonChosenOpacity);
				} else if (!bubbleReady) {
					opacity = bubbleProgress;
				}
				color = button.paid
					? st->creditsBg3()->c
					: stm->msgFileBg->c;
			} else {
				if (!bubbleReady) {
					opacity = bubbleProgress;
				}
				color = (!chosen
					? st->msgServiceBg()
					: button.paid
					? st->creditsBg2()
					: st->msgServiceFg())->c;
			}

			const auto fill = geometry.marginsAdded({
				flipped ? bubbleSkip : 0,
				0,
				flipped ? 0 : bubbleSkip,
				0,
			});
			paintSingleBg(p, fill, color, opacity);
			if (inbubble && !chosen) {
				p.setOpacity(bubbleProgress);
			}
		}
		if (!button.custom && button.image.isNull()) {
			button.image = _owner->resolveReactionImageFor(button.id);
		}

		const auto textFg = !inbubble
			? (chosen
				? QPen(AdaptChosenServiceFg(st->msgServiceBg()->c))
				: st->msgServiceFg())
			: !chosen
			? (button.paid ? st->creditsFg() : stm->msgServiceFg)
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
					context,
					textFg.color());
			} else if (!button.image.isNull()) {
				p.drawImage(image.topLeft(), button.image);
			}
		}
		if (animating) {
			animations.push_back({
				.animation = button.animation.get(),
				.textColor = textFg.color(),
				.target = image,
			});
		}
		if ((tags && !button.textWidth) || bubbleProgress == 0.) {
			p.setOpacity(1.);
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
			const auto textLeft = tags
				? (left
					- padding.left()
					+ st::reactionInlineTagNamePosition.x())
				: (left + size + st::reactionInlineSkip);
			const auto textTop = geometry.y()
				+ (tags
					? st::reactionInlineTagNamePosition.y()
					: ((geometry.height() - st::semiboldFont->height) / 2));
			const auto font = tags
				? st::reactionInlineTagFont
				: st::semiboldFont;
			p.drawText(textLeft, textTop + font->ascent, button.text);
		}
		if (!bubbleReady) {
			p.setOpacity(1.);
		}
	}
	if (!animations.empty()) {
		const auto now = context.now;
		context.reactionInfo->effectPaint = [
			now,
			list = std::move(animations)
		](QPainter &p) {
			auto result = QRect();
			for (const auto &single : list) {
				const auto area = single.animation->paintGetArea(
					p,
					QPoint(),
					single.target,
					single.textColor,
					QRect(), // Clip, for emoji status.
					now);
				result = result.isEmpty() ? area : result.united(area);
			}
			return result;
		};
	}
}

float64 InlineList::TagDotAlpha() {
	return 0.6;
}

QImage InlineList::PrepareTagBg(QColor tagBg, QColor dotBg) {
	const auto padding = st::reactionInlinePadding;
	const auto size = st::reactionInlineSize;
	const auto width = padding.left()
		+ size
		+ st::reactionInlineTagSkip
		+ padding.right();
	const auto height = padding.top() + size + padding.bottom();
	const auto ratio = style::DevicePixelRatio();

	auto result = QImage(
		QSize(width, height) * ratio,
		QImage::Format_ARGB32_Premultiplied);
	result.setDevicePixelRatio(ratio);

	result.fill(Qt::transparent);
	auto p = QPainter(&result);

	auto path = QPainterPath();
	const auto arrow = st::reactionInlineTagArrow;
	const auto rradius = st::reactionInlineTagRightRadius * 1.;
	const auto radius = st::reactionInlineTagLeftRadius - rradius;
	auto pen = QPen(tagBg);
	pen.setWidthF(rradius * 2.);
	pen.setJoinStyle(Qt::RoundJoin);
	const auto rect = QRectF(0, 0, width, height).marginsRemoved(
		{ rradius, rradius, rradius, rradius });

	const auto right = rect.x() + rect.width();
	const auto bottom = rect.y() + rect.height();
	path.moveTo(rect.x() + radius, rect.y());
	path.lineTo(right - arrow, rect.y());
	path.lineTo(right, rect.y() + rect.height() / 2);
	path.lineTo(right - arrow, bottom);
	path.lineTo(rect.x() + radius, bottom);
	path.arcTo(
		QRectF(rect.x(), bottom - radius * 2, radius * 2, radius * 2),
		270,
		-90);
	path.lineTo(rect.x(), rect.y() + radius);
	path.arcTo(
		QRectF(rect.x(), rect.y(), radius * 2, radius * 2),
		180,
		-90);
	path.closeSubpath();

	const auto dsize = st::reactionInlineTagDot;
	const auto dot = QRectF(
		right - st::reactionInlineTagDotSkip - dsize,
		rect.y() + (rect.height() - dsize) / 2.,
		dsize,
		dsize);

	auto hq = PainterHighQualityEnabler(p);
	p.setCompositionMode(QPainter::CompositionMode_Source);
	p.setPen(pen);
	p.setBrush(tagBg);
	p.drawPath(path);

	if (dotBg.alpha() > 0) {
		p.setPen(Qt::NoPen);
		p.setBrush(dotBg);
		p.drawEllipse(dot);
	}

	p.end();

	return result;
}

void InlineList::validateTagBg(const QColor &color) const {
	if (!_tagBg.isNull() && _tagBgColor == color) {
		return;
	}
	_tagBgColor = color;
	auto dot = color;
	dot.setAlphaF(dot.alphaF() * TagDotAlpha());
	_tagBg = PrepareTagBg(color, anim::with_alpha(color, TagDotAlpha()));
}

void InlineList::paintSingleBg(
		Painter &p,
		const QRect &fill,
		const QColor &color,
		float64 opacity) const {
	p.setOpacity(opacity);
	if (!areTags()) {
		const auto radius = fill.height() / 2.;
		p.setBrush(color);
		p.drawRoundedRect(fill, radius, radius);
		return;
	}
	validateTagBg(color);
	const auto ratio = style::DevicePixelRatio();
	const auto left = st::reactionInlineTagLeftRadius;
	const auto right = (_tagBg.width() / ratio) - left;
	Assert(right > 0);
	const auto useLeft = std::min(fill.width(), left);
	p.drawImage(
		QRect(fill.x(), fill.y(), useLeft, fill.height()),
		_tagBg,
		QRect(0, 0, useLeft * ratio, _tagBg.height()));
	const auto middle = fill.width() - left - right;
	if (middle > 0) {
		p.fillRect(fill.x() + left, fill.y(), middle, fill.height(), color);
	}
	if (const auto useRight = fill.width() - left; useRight > 0) {
		p.drawImage(
			QRect(
				fill.x() + fill.width() - useRight,
				fill.y(),
				useRight,
				fill.height()),
			_tagBg,
			QRect(_tagBg.width() - useRight * ratio,
				0,
				useRight * ratio,
				_tagBg.height()));
	}
}

bool InlineList::getState(
		QPoint point,
		not_null<TextState*> outResult) const {
	const auto dimension = countDimension(width());
	const auto left = dimension.left;
	if (!QRect(left, 0, dimension.width, height()).contains(point)) {
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
		Ui::ReactionFlyAnimationArgs &&args,
		Fn<void()> repaint) {
	const auto i = ranges::find(_buttons, args.id, &Button::id);
	if (i == end(_buttons)) {
		return;
	}
	i->animation = std::make_unique<Ui::ReactionFlyAnimation>(
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
			const auto wasView = view.cloud.get();
			if (peer->userpicUniqueKey(view) != entry.uniqueKey
				|| view.cloud.get() != wasView) {
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
		const PaintContext &context,
		const QColor &textColor) const {
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
		.textColor = textColor,
		.now = context.now,
		.paused = context.paused || On(PowerSaving::kEmojiChat),
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
-> base::flat_map<ReactionId, std::unique_ptr<Ui::ReactionFlyAnimation>> {
	auto result = base::flat_map<
		ReactionId,
		std::unique_ptr<Ui::ReactionFlyAnimation>>();
	for (auto &button : _buttons) {
		if (button.animation) {
			result.emplace(button.id, std::move(button.animation));
		}
	}
	return result;
}

void InlineList::continueAnimations(base::flat_map<
		ReactionId,
		std::unique_ptr<Ui::ReactionFlyAnimation>> animations) {
	for (auto &[id, animation] : animations) {
		const auto i = ranges::find(_buttons, id, &Button::id);
		if (i != end(_buttons)) {
			i->animation = std::move(animation);
		}
	}
}

InlineListData InlineListDataFromMessage(not_null<Element*> view) {
	using Flag = InlineListData::Flag;
	const auto item = view->data();
	auto result = InlineListData();
	result.reactions = item->reactionsWithLocal();
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
					| ranges::views::transform(&Data::RecentReaction::peer)
					| ranges::to_vector;
			}
		}
	}
	result.flags = (view->hasOutLayout() ? Flag::OutLayout : Flag())
		| (view->embedReactionsInBubble() ? Flag::InBubble : Flag())
		| (item->reactionsAreTags() ? Flag::Tags : Flag())
		| (item->isService() ? Flag::Centered : Flag());
	return result;
}

ReactionId ReactionIdOfLink(const ClickHandlerPtr &link) {
	return link
		? link->property(kReactionsCountEmojiProperty).value<ReactionId>()
		: ReactionId();
}

ReactionCount ReactionCountOfLink(
		HistoryItem *item,
		const ClickHandlerPtr &link) {
	const auto id = ReactionIdOfLink(link);
	if (!item || !id) {
		return {};
	}
	const auto groups = &item->history()->owner().groups();
	if (const auto group = groups->find(item)) {
		item = group->items.front();
	}
	const auto &list = item->reactions();
	const auto i = ranges::find(list, id, &Data::MessageReaction::id);
	if (i == end(list) || !i->count) {
		return {};
	}
	const auto formatted = Lang::FormatCountToShort(i->count);
	return { .count = i->count, .shortened = formatted.shortened };
}

} // namespace HistoryView::Reactions
