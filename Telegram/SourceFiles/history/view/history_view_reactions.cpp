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
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_message_reactions.h"
#include "data/data_session.h"
#include "main/main_session.h"
#include "lang/lang_tag.h"
#include "ui/chat/chat_style.h"
#include "styles/style_chat.h"

namespace HistoryView::Reactions {
namespace {

constexpr auto kInNonChosenOpacity = 0.12;
constexpr auto kOutNonChosenOpacity = 0.18;

} // namespace

InlineList::InlineList(Data &&data)
: _data(std::move(data)) {
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
	ranges::sort(sorted, std::greater<>(), &std::pair<QString, int>::second);

	auto buttons = std::vector<Button>();
	buttons.reserve(sorted.size());
	for (const auto &[emoji, count] : sorted) {
		const auto i = ranges::find(_buttons, emoji, &Button::emoji);
		buttons.push_back((i != end(_buttons))
			? std::move(*i)
			: prepareButtonWithEmoji(emoji));
		setButtonCount(buttons.back(), count);
	}
	_buttons = std::move(buttons);
}

InlineList::Button InlineList::prepareButtonWithEmoji(const QString &emoji) {
	auto result = Button{ .emoji = emoji };
	auto &reactions = _data.owner->reactions();
	const auto &list = reactions.list();
	const auto i = ranges::find(
		list,
		emoji,
		&::Data::Reaction::emoji);
	const auto document = (i != end(list))
		? i->staticIcon.get()
		: nullptr;
	if (document) {
		loadButtonImage(result, document);
	} else if (!_waitingForReactionsList) {
		reactions.refresh();
		reactions.updates(
		) | rpl::filter([=] {
			return _waitingForReactionsList;
		}) | rpl::start_with_next([=] {
			reactionsListLoaded();
		}, _assetsLoadLifetime);
	}
	return result;
}

void InlineList::reactionsListLoaded() {
	_waitingForReactionsList = false;
	if (assetsLoaded()) {
		_assetsLoadLifetime.destroy();
	}

	const auto &list = _data.owner->reactions().list();
	for (auto &button : _buttons) {
		if (!button.image.isNull() || button.media) {
			continue;
		}
		const auto i = ranges::find(
			list,
			button.emoji,
			&::Data::Reaction::emoji);
		const auto document = (i != end(list))
			? i->staticIcon.get()
			: nullptr;
		if (document) {
			loadButtonImage(button, document);
		} else {
			LOG(("API Error: Reaction for emoji '%1' not found!"
				).arg(button.emoji));
		}
	}
}

void InlineList::setButtonCount(Button &button, int count) {
	if (button.count == count) {
		return;
	}
	button.count = count;
	button.countText = Lang::FormatCountToShort(count).string;
	button.countTextWidth = st::semiboldFont->width(button.countText);
}

void InlineList::loadButtonImage(
		Button &button,
		not_null<DocumentData*> document) {
	if (!button.image.isNull()) {
		return;
	} else if (!button.media) {
		button.media = document->createMediaView();
	}
	if (const auto image = button.media->getStickerLarge()) {
		setButtonImage(button, image->original());
	} else if (!_waitingForDownloadTask) {
		_waitingForDownloadTask = true;
		document->session().downloaderTaskFinished(
		) | rpl::start_with_next([=] {
			downloadTaskFinished();
		}, _assetsLoadLifetime);
	}
}

void InlineList::setButtonImage(Button &button, QImage large) {
	button.media = nullptr;
	const auto size = st::reactionBottomSize;
	const auto factor = style::DevicePixelRatio();
	button.image = Images::prepare(
		std::move(large),
		size * factor,
		size * factor,
		Images::Option::Smooth,
		size,
		size);
}

void InlineList::downloadTaskFinished() {
	auto hasOne = false;
	for (auto &button : _buttons) {
		if (!button.media) {
			continue;
		} else if (const auto image = button.media->getStickerLarge()) {
			setButtonImage(button, image->original());
		} else {
			hasOne = true;
		}
	}
	if (!hasOne) {
		_waitingForDownloadTask = false;
		if (assetsLoaded()) {
			_assetsLoadLifetime.destroy();
		}
	}
}

bool InlineList::assetsLoaded() const {
	return !_waitingForReactionsList && !_waitingForDownloadTask;
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
		button.geometry = QRect(x, 0, width, height);
		x += width + between;
	}
	return QSize(
		x - between + _skipBlock.width(),
		std::max(height, _skipBlock.height()));
}

QSize InlineList::countCurrentSize(int newWidth) {
	if (newWidth >= maxWidth() || _buttons.empty()) {
		return optimalSize();
	}
	const auto between = st::reactionBottomBetween;
	const auto left = (_data.flags & InlineListData::Flag::InBubble)
		? st::reactionBottomInBubbleLeft
		: 0;
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

void InlineList::paint(
		Painter &p,
		const PaintContext &context,
		int outerWidth,
		const QRect &clip) const {
	const auto st = context.st;
	const auto stm = context.messageStyle();
	const auto between = st::reactionBottomBetween;
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
				p.setBrush(chosen
					? st->msgServiceBgSelected()
					: st->msgServiceBg());
			}
			const auto radius = geometry.height() / 2.;
			p.drawRoundedRect(geometry, radius, radius);
			if (inbubble && !chosen) {
				p.setOpacity(1.);
			}
		}
		p.drawImage(inner.topLeft(), button.image);
		p.setPen(!inbubble
			? st->msgServiceFg()
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

InlineListData InlineListDataFromMessage(not_null<Message*> message) {
	const auto owner = &message->data()->history()->owner();
	auto result = InlineListData{ .owner = owner };

	using Flag = InlineListData::Flag;
	const auto item = message->message();
	result.reactions = item->reactions();
	result.chosenReaction = item->chosenReaction();
	result.flags = (message->hasOutLayout() ? Flag::OutLayout : Flag())
		| (message->embedReactionsInBubble() ? Flag::InBubble : Flag());
	return result;
}

} // namespace HistoryView
