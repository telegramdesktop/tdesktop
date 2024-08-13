/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_bottom_info.h"

#include "ui/chat/message_bubble.h"
#include "ui/chat/chat_style.h"
#include "ui/effects/reaction_fly_animation.h"
#include "ui/text/text_options.h"
#include "ui/text/text_utilities.h"
#include "ui/painter.h"
#include "lang/lang_keys.h"
#include "history/history_item_components.h"
#include "history/history_item.h"
#include "history/history.h"
#include "history/view/history_view_message.h"
#include "history/view/history_view_cursor_state.h"
#include "chat_helpers/emoji_interactions.h"
#include "core/click_handler_types.h"
#include "main/main_session.h"
#include "lottie/lottie_icon.h"
#include "data/data_channel.h"
#include "data/data_session.h"
#include "data/data_message_reactions.h"
#include "window/window_session_controller.h"
#include "styles/style_chat.h"
#include "styles/style_dialogs.h"

namespace HistoryView {

struct BottomInfo::Effect {
	mutable std::unique_ptr<Ui::ReactionFlyAnimation> animation;
	mutable QImage image;
	EffectId id = 0;
};

BottomInfo::BottomInfo(
	not_null<::Data::Reactions*> reactionsOwner,
	Data &&data)
: _reactionsOwner(reactionsOwner)
, _data(std::move(data)) {
	layout();
}

BottomInfo::~BottomInfo() = default;

void BottomInfo::update(Data &&data, int availableWidth) {
	_data = std::move(data);
	layout();
	if (width() > 0) {
		resizeGetHeight(std::min(maxWidth(), availableWidth));
	}
}

int BottomInfo::countEffectMaxWidth() const {
	auto result = 0;
	if (_effect) {
		result += st::reactionInfoSize;
		result += st::reactionInfoBetween;
	}
	if (result) {
		result += (st::reactionInfoSkip - st::reactionInfoBetween);
	}
	return result;
}

int BottomInfo::countEffectHeight(int newWidth) const {
	const auto left = 0;
	auto x = 0;
	auto y = 0;
	auto widthLeft = newWidth;
	if (_effect) {
		const auto add = st::reactionInfoBetween;
		const auto width = st::reactionInfoSize;
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
	return maxWidth() - _effectMaxWidth;
}

bool BottomInfo::isWide() const {
	return (_data.flags & Data::Flag::Edited)
		|| !_data.author.isEmpty()
		|| !_views.isEmpty()
		|| !_replies.isEmpty()
		|| _effect;
}

TextState BottomInfo::textState(
		not_null<const Message*> view,
		QPoint position) const {
	const auto item = view->data();
	auto result = TextState(item);
	if (const auto link = replayEffectLink(view, position)) {
		result.link = link;
		return result;
	}
	const auto textWidth = _authorEditedDate.maxWidth();
	auto withTicksWidth = textWidth;
	if (_data.flags & (Data::Flag::OutLayout | Data::Flag::Sending)) {
		withTicksWidth += st::historySendStateSpace;
	}
	if (!_views.isEmpty()) {
		const auto viewsWidth = _views.maxWidth();
		const auto right = width()
			- withTicksWidth
			- ((_data.flags & Data::Flag::Pinned) ? st::historyPinWidth : 0)
			- st::historyViewsSpace
			- st::historyViewsWidth
			- viewsWidth;
		const auto inViews = QRect(
			right,
			0,
			withTicksWidth + st::historyViewsWidth,
			st::msgDateFont->height
		).contains(position);
		if (inViews) {
			result.customTooltip = true;
			const auto fullViews = tr::lng_views_tooltip(
				tr::now,
				lt_count_decimal,
				*_data.views);
			const auto fullForwards = _data.forwardsCount
				? ('\n' + tr::lng_forwards_tooltip(
					tr::now,
					lt_count_decimal,
					*_data.forwardsCount))
				: QString();
			result.customTooltipText = fullViews + fullForwards;
		}
	}
	const auto inTime = QRect(
		width() - withTicksWidth,
		0,
		withTicksWidth,
		st::msgDateFont->height
	).contains(position);
	if (inTime) {
		result.cursor = CursorState::Date;
	}
	return result;
}

ClickHandlerPtr BottomInfo::replayEffectLink(
		not_null<const Message*> view,
		QPoint position) const {
	if (!_effect) {
		return nullptr;
	}
	auto left = 0;
	auto top = 0;
	auto available = width();
	if (height() != minHeight()) {
		available = std::min(available, _effectMaxWidth);
		left += width() - available;
		top += st::msgDateFont->height;
	}
	if (_effect) {
		const auto image = QRect(
			left,
			top,
			st::reactionInfoSize,
			st::msgDateFont->height);
		if (image.contains(position)) {
			if (!_replayLink) {
				_replayLink = replayEffectLink(view);
			}
			return _replayLink;
		}
	}
	return nullptr;
}

ClickHandlerPtr BottomInfo::replayEffectLink(
		not_null<const Message*> view) const {
	const auto weak = base::make_weak(view);
	return std::make_shared<LambdaClickHandler>([=](ClickContext context) {
		const auto my = context.other.value<ClickHandlerContext>();
		if (const auto controller = my.sessionWindow.get()) {
			if (const auto strong = weak.get()) {
				strong->delegate()->elementStartEffect(strong, nullptr);
			}
		}
	});
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

	if (_data.flags & Data::Flag::Pinned) {
		const auto &icon = inverted
			? st->historyPinInvertedIcon()
			: stm->historyPinIcon;
		right -= st::historyPinWidth;
		icon.paint(
			p,
			right,
			firstLineBottom + st::historyPinTop,
			outerWidth);
	}
	if (!_views.isEmpty()) {
		const auto viewsWidth = _views.maxWidth();
		right -= st::historyViewsSpace + viewsWidth;
		_views.drawLeft(p, right, position.y(), viewsWidth, outerWidth);

		const auto &icon = inverted
			? st->historyViewsInvertedIcon()
			: stm->historyViewsIcon;
		right -= st::historyViewsWidth;
		icon.paint(
			p,
			right,
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
		right -= st::historyViewsWidth;
		icon.paint(
			p,
			right,
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
	if (_effect) {
		auto left = position.x();
		auto top = position.y();
		auto available = width();
		if (height() != minHeight()) {
			available = std::min(available, _effectMaxWidth);
			left += width() - available;
			top += st::msgDateFont->height;
		}
		paintEffect(p, position, left, top, available, context);
	}
}

void BottomInfo::paintEffect(
		Painter &p,
		QPoint origin,
		int left,
		int top,
		int availableWidth,
		const PaintContext &context) const {
	struct SingleAnimation {
		not_null<Ui::ReactionFlyAnimation*> animation;
		QRect target;
	};
	std::vector<SingleAnimation> animations;

	auto x = left;
	auto y = top;
	auto widthLeft = availableWidth;
	if (_effect) {
		const auto animating = (_effect->animation != nullptr);
		const auto add = st::reactionInfoBetween;
		const auto width = st::reactionInfoSize;
		if (x > left && widthLeft < width) {
			x = left;
			y += st::msgDateFont->height;
			widthLeft = availableWidth;
		}
		if (_effect->image.isNull()) {
			_effect->image = _reactionsOwner->resolveEffectImageFor(
				_effect->id);
		}
		const auto image = QRect(
			x + (st::reactionInfoSize - st::effectInfoImage) / 2,
			y + (st::msgDateFont->height - st::effectInfoImage) / 2,
			st::effectInfoImage,
			st::effectInfoImage);
		if (!_effect->image.isNull()) {
			p.drawImage(image.topLeft(), _effect->image);
		}
		if (animating) {
			animations.push_back({
				.animation = _effect->animation.get(),
				.target = image,
			});
		}
		x += width + add;
		widthLeft -= width + add;
	}
	if (!animations.empty()) {
		const auto now = context.now;
		context.reactionInfo->effectPaint = [
			now,
			origin,
			list = std::move(animations)
		](QPainter &p) {
			auto result = QRect();
			for (const auto &single : list) {
				const auto area = single.animation->paintGetArea(
					p,
					origin,
					single.target,
					QColor(255, 255, 255, 0), // Colored, for emoji status.
					QRect(), // Clip, for emoji status.
					now);
				result = result.isEmpty() ? area : result.united(area);
			}
			return result;
		};
	}
}

QSize BottomInfo::countCurrentSize(int newWidth) {
	if (newWidth >= maxWidth() || (_data.flags & Data::Flag::Shortcut)) {
		return optimalSize();
	}
	const auto dateHeight = (_data.flags & Data::Flag::Sponsored)
		? 0
		: st::msgDateFont->height;
	const auto noReactionsWidth = maxWidth() - _effectMaxWidth;
	accumulate_min(newWidth, std::max(noReactionsWidth, _effectMaxWidth));
	return QSize(
		newWidth,
		dateHeight + countEffectHeight(newWidth));
}

void BottomInfo::layout() {
	layoutDateText();
	layoutViewsText();
	layoutRepliesText();
	layoutEffectText();
	initDimensions();
}

void BottomInfo::layoutDateText() {
	const auto edited = (_data.flags & Data::Flag::Edited)
		? (tr::lng_edited(tr::now) + ' ')
		: QString();
	const auto author = _data.author;
	const auto prefix = !author.isEmpty() ? u", "_q : QString();
	const auto date = edited + QLocale().toString(
		_data.date.time(),
		QLocale::ShortFormat);
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
		? QString()
		: (_data.flags & Data::Flag::Imported)
		? (date + ' ' + tr::lng_imported(tr::now))
		: name.isEmpty()
		? date
		: (name + afterAuthor);
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
		|| (_data.flags & Data::Flag::Sending)
		|| (_data.flags & Data::Flag::Shortcut)) {
		_replies.clear();
		return;
	}
	_replies.setText(
		st::msgDateTextStyle,
		Lang::FormatCountToShort(*_data.replies).string,
		Ui::NameTextOptions());
}

void BottomInfo::layoutEffectText() {
	if (!_data.effectId) {
		_effect = nullptr;
		return;
	}
	_effect = std::make_unique<Effect>(prepareEffectWithId(_data.effectId));
}

QSize BottomInfo::countOptimalSize() {
	if (_data.flags & Data::Flag::Shortcut) {
		return { st::historyShortcutStateSpace, st::msgDateFont->height };
	}
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
	if (_data.flags & Data::Flag::Pinned) {
		width += st::historyPinWidth;
	}
	_effectMaxWidth = countEffectMaxWidth();
	width += _effectMaxWidth;
	const auto dateHeight = (_data.flags & Data::Flag::Sponsored)
		? 0
		: st::msgDateFont->height;
	return QSize(width, dateHeight);
}

BottomInfo::Effect BottomInfo::prepareEffectWithId(EffectId id) {
	auto result = Effect{ .id = id };
	_reactionsOwner->preloadEffectImageFor(id);
	return result;
}

void BottomInfo::animateEffect(
		Ui::ReactionFlyAnimationArgs &&args,
		Fn<void()> repaint) {
	if (!_effect || args.id.custom() != _effect->id) {
		return;
	}
	_effect->animation = std::make_unique<Ui::ReactionFlyAnimation>(
		_reactionsOwner,
		args.translated(QPoint(width(), height())),
		std::move(repaint),
		st::effectInfoImage);
}

auto BottomInfo::takeEffectAnimation()
-> std::unique_ptr<Ui::ReactionFlyAnimation> {
	return _effect ? std::move(_effect->animation) : nullptr;
}

void BottomInfo::continueEffectAnimation(
		std::unique_ptr<Ui::ReactionFlyAnimation> animation) {
	if (_effect) {
		_effect->animation = std::move(animation);
	}
}

QRect BottomInfo::effectIconGeometry() const {
	if (!_effect) {
		return {};
	}
	auto left = 0;
	auto top = 0;
	auto available = width();
	if (height() != minHeight()) {
		available = std::min(available, _effectMaxWidth);
		left += width() - available;
		top += st::msgDateFont->height;
	}
	return QRect(
		left + (st::reactionInfoSize - st::effectInfoImage) / 2,
		top + (st::msgDateFont->height - st::effectInfoImage) / 2,
		st::effectInfoImage,
		st::effectInfoImage);
}

BottomInfo::Data BottomInfoDataFromMessage(not_null<Message*> message) {
	using Flag = BottomInfo::Data::Flag;
	const auto item = message->data();

	auto result = BottomInfo::Data();
	result.date = message->dateTime();
	result.effectId = item->effectId();
	if (message->hasOutLayout()) {
		result.flags |= Flag::OutLayout;
	}
	if (message->context() == Context::Replies) {
		result.flags |= Flag::RepliesContext;
	}
	if (item->isSponsored()) {
		result.flags |= Flag::Sponsored;
	}
	if (item->isPinned() && message->context() != Context::Pinned) {
		result.flags |= Flag::Pinned;
	}
	if (message->context() == Context::ShortcutMessages) {
		result.flags |= Flag::Shortcut;
	}
	if (!item->isPost()
		|| !item->hasRealFromId()
		|| !item->history()->peer->asChannel()->signatureProfiles()) {
		if (const auto msgsigned = item->Get<HistoryMessageSigned>()) {
			if (!msgsigned->isAnonymousRank) {
				result.author = msgsigned->author;
			}
		}
	}
	if (message->displayedEditDate()) {
		result.flags |= Flag::Edited;
	}
	if (const auto views = item->Get<HistoryMessageViews>()) {
		if (views->views.count >= 0) {
			result.views = views->views.count;
		}
		if (views->replies.count >= 0 && !views->commentsMegagroupId) {
			result.replies = views->replies.count;
		}
		if (views->forwardsCount > 0) {
			result.forwardsCount = views->forwardsCount;
		}
	}
	if (item->isSending() || item->hasFailed()) {
		result.flags |= Flag::Sending;
	}
	const auto forwarded = item->Get<HistoryMessageForwarded>();
	if (forwarded && forwarded->imported) {
		result.flags |= Flag::Imported;
	}
	// We don't want to pass and update it in Data for now.
	//if (item->unread()) {
	//	result.flags |= Flag::Unread;
	//}
	return result;
}

} // namespace HistoryView
