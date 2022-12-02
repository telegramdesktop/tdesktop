/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "dialogs/ui/dialogs_message_view.h"

#include "history/history.h"
#include "history/history_item.h"
#include "history/view/history_view_item_preview.h"
#include "main/main_session.h"
#include "dialogs/ui/dialogs_layout.h"
#include "dialogs/ui/dialogs_topics_view.h"
#include "ui/text/text_options.h"
#include "ui/text/text_utilities.h"
#include "ui/image/image.h"
#include "ui/painter.h"
#include "core/ui_integration.h"
#include "lang/lang_keys.h"
#include "lang/lang_text_entity.h"
#include "styles/style_dialogs.h"

namespace {

constexpr auto kEmojiLoopCount = 2;

template <ushort kTag>
struct TextWithTagOffset {
	TextWithTagOffset(TextWithEntities text) : text(std::move(text)) {
	}
	TextWithTagOffset(QString text) : text({ std::move(text) }) {
	}
	static TextWithTagOffset FromString(const QString &text) {
		return { { text } };
	}

	TextWithEntities text;
	int offset = -1;
};

} // namespace

namespace Lang {

template <ushort kTag>
struct ReplaceTag<TextWithTagOffset<kTag>> {
	static TextWithTagOffset<kTag> Call(
		TextWithTagOffset<kTag> &&original,
		ushort tag,
		const TextWithTagOffset<kTag> &replacement);
};

template <ushort kTag>
TextWithTagOffset<kTag> ReplaceTag<TextWithTagOffset<kTag>>::Call(
		TextWithTagOffset<kTag> &&original,
		ushort tag,
		const TextWithTagOffset<kTag> &replacement) {
	const auto replacementPosition = FindTagReplacementPosition(
		original.text.text,
		tag);
	if (replacementPosition < 0) {
		return std::move(original);
	}
	original.text = ReplaceTag<TextWithEntities>::Replace(
		std::move(original.text),
		replacement.text,
		replacementPosition);
	if (tag == kTag) {
		original.offset = replacementPosition;
	} else if (original.offset > replacementPosition) {
		constexpr auto kReplaceCommandLength = 4;
		const auto replacementSize = replacement.text.text.size();
		original.offset += replacementSize - kReplaceCommandLength;
	}
	return std::move(original);
}

} // namespace Lang

namespace Dialogs::Ui {

TextWithEntities DialogsPreviewText(TextWithEntities text) {
	auto result = Ui::Text::Filtered(
		std::move(text),
		{
			EntityType::Pre,
			EntityType::Code,
			EntityType::Spoiler,
			EntityType::StrikeOut,
			EntityType::Underline,
			EntityType::Italic,
			EntityType::CustomEmoji,
			EntityType::PlainLink,
		});
	for (auto &entity : result.entities) {
		if (entity.type() == EntityType::Pre) {
			entity = EntityInText(
				EntityType::Code,
				entity.offset(),
				entity.length());
		}
	}
	return result;
}

struct MessageView::LoadingContext {
	std::any context;
	rpl::lifetime lifetime;
};

MessageView::MessageView()
: _senderCache(st::dialogsTextWidthMin)
, _textCache(st::dialogsTextWidthMin) {
}

MessageView::~MessageView() = default;

void MessageView::itemInvalidated(not_null<const HistoryItem*> item) {
	if (_textCachedFor == item.get()) {
		_textCachedFor = nullptr;
	}
}

bool MessageView::dependsOn(not_null<const HistoryItem*> item) const {
	return (_textCachedFor == item.get());
}

bool MessageView::prepared(
		not_null<const HistoryItem*> item,
		Data::Forum *forum) const {
	return (_textCachedFor == item.get())
		&& (!forum
			|| (_topics
				&& _topics->forum() == forum
				&& _topics->prepared()));
}

void MessageView::prepare(
		not_null<const HistoryItem*> item,
		Data::Forum *forum,
		Fn<void()> customEmojiRepaint,
		ToPreviewOptions options) {
	if (!forum) {
		_topics = nullptr;
	} else if (!_topics || _topics->forum() != forum) {
		_topics = std::make_unique<TopicsView>(forum);
		_topics->prepare(item->topicRootId(), customEmojiRepaint);
	} else if (!_topics->prepared()) {
		_topics->prepare(item->topicRootId(), customEmojiRepaint);
	}
	if (_textCachedFor == item.get()) {
		return;
	}
	options.existing = &_imagesCache;
	options.ignoreTopic = true;
	auto preview = item->toPreview(options);
	const auto hasImages = !preview.images.empty();
	const auto history = item->history();
	const auto context = Core::MarkedTextContext{
		.session = &history->session(),
		.customEmojiRepaint = customEmojiRepaint,
		.customEmojiLoopLimit = kEmojiLoopCount,
	};
	const auto senderTill = (preview.arrowInTextPosition > 0)
		? preview.arrowInTextPosition
		: preview.imagesInTextPosition;
	if (hasImages && senderTill > 0) {
		auto sender = Text::Mid(preview.text, 0, senderTill);
		TextUtilities::Trim(sender);
		_senderCache.setMarkedText(
			st::dialogsTextStyle,
			std::move(sender),
			DialogTextOptions());
		preview.text = Text::Mid(preview.text, senderTill);
	} else {
		_senderCache = { st::dialogsTextWidthMin };
	}
	TextUtilities::Trim(preview.text);
	_textCache.setMarkedText(
		st::dialogsTextStyle,
		DialogsPreviewText(std::move(preview.text)),
		DialogTextOptions(),
		context);
	_textCachedFor = item;
	_imagesCache = std::move(preview.images);
	if (preview.loadingContext.has_value()) {
		if (!_loadingContext) {
			_loadingContext = std::make_unique<LoadingContext>();
			item->history()->session().downloaderTaskFinished(
			) | rpl::start_with_next([=] {
				_textCachedFor = nullptr;
			}, _loadingContext->lifetime);
		}
		_loadingContext->context = std::move(preview.loadingContext);
	} else {
		_loadingContext = nullptr;
	}
}

bool MessageView::isInTopicJump(int x, int y) const {
	return _topics && _topics->isInTopicJumpArea(x, y);
}

void MessageView::addTopicJumpRipple(
		QPoint origin,
		not_null<TopicJumpCache*> topicJumpCache,
		Fn<void()> updateCallback) {
	if (_topics) {
		_topics->addTopicJumpRipple(
			origin,
			topicJumpCache,
			std::move(updateCallback));
	}
}

void MessageView::stopLastRipple() {
	if (_topics) {
		_topics->stopLastRipple();
	}
}

int MessageView::countWidth() const {
	auto result = 0;
	if (!_senderCache.isEmpty()) {
		result += _senderCache.maxWidth();
		if (!_imagesCache.empty()) {
			result += st::dialogsMiniPreviewSkip
				+ st::dialogsMiniPreviewRight;
		}
	}
	if (!_imagesCache.empty()) {
		result += (_imagesCache.size()
			* (st::dialogsMiniPreview + st::dialogsMiniPreviewSkip))
			+ st::dialogsMiniPreviewRight;
	}
	return result + _textCache.maxWidth();
}

void MessageView::paint(
		Painter &p,
		const QRect &geometry,
		const PaintContext &context) const {
	if (geometry.isEmpty()) {
		return;
	}
	p.setFont(st::dialogsTextFont);
	p.setPen(context.active
		? st::dialogsTextFgActive
		: context.selected
		? st::dialogsTextFgOver
		: st::dialogsTextFg);
	const auto withTopic = _topics && context.st->topicsHeight;
	const auto palette = &(withTopic
		? (context.active
			? st::dialogsTextPaletteInTopicActive
			: context.selected
			? st::dialogsTextPaletteInTopicOver
			: st::dialogsTextPaletteInTopic)
		: (context.active
			? st::dialogsTextPaletteActive
			: context.selected
			? st::dialogsTextPaletteOver
			: st::dialogsTextPalette));

	auto rect = geometry;
	const auto checkJump = withTopic && !context.active;
	const auto jump1 = checkJump ? _topics->jumpToTopicWidth() : 0;
	if (jump1) {
		paintJumpToLast(p, rect, context, jump1);
	} else if (_topics) {
		_topics->clearTopicJumpGeometry();
	}

	if (withTopic) {
		_topics->paint(p, rect, context);
		rect.setTop(rect.top() + context.st->topicsHeight);
	}

	auto finalRight = rect.x() + rect.width();
	if (jump1) {
		rect.setWidth(rect.width() - st::forumDialogJumpArrowSkip);
		finalRight -= st::forumDialogJumpArrowSkip;
	}
	const auto lines = rect.height() / st::dialogsTextFont->height;
	if (!_senderCache.isEmpty()) {
		_senderCache.draw(p, {
			.position = rect.topLeft(),
			.availableWidth = rect.width(),
			.palette = palette,
			.elisionLines = lines,
		});
		rect.setLeft(rect.x() + _senderCache.maxWidth());
		if (!_imagesCache.empty()) {
			const auto skip = st::dialogsMiniPreviewSkip
				+ st::dialogsMiniPreviewRight;
			rect.setLeft(rect.x() + skip);
		}
	}
	for (const auto &image : _imagesCache) {
		if (rect.width() < st::dialogsMiniPreview) {
			break;
		}
		p.drawImage(
			rect.x(),
			rect.y() + st::dialogsMiniPreviewTop,
			image.data);
		rect.setLeft(rect.x()
			+ st::dialogsMiniPreview
			+ st::dialogsMiniPreviewSkip);
	}
	if (!_imagesCache.empty()) {
		rect.setLeft(rect.x() + st::dialogsMiniPreviewRight);
	}
	if (!rect.isEmpty()) {
		_textCache.draw(p, {
			.position = rect.topLeft(),
			.availableWidth = rect.width(),
			.palette = palette,
			.spoiler = Text::DefaultSpoilerCache(),
			.now = context.now,
			.paused = context.paused,
			.elisionLines = lines,
		});
		rect.setLeft(rect.x() + _textCache.maxWidth());
	}
	if (jump1) {
		const auto x = (rect.width() > st::forumDialogJumpArrowSkip)
			? rect.x()
			: finalRight;
		const auto add = st::forumDialogJumpArrowLeft;
		const auto y = rect.y() + st::forumDialogJumpArrowTop;
		(context.selected
			? st::forumDialogJumpArrowOver
			: st::forumDialogJumpArrow).paint(p, x + add, y, context.width);
	}
}

void MessageView::paintJumpToLast(
		Painter &p,
		const QRect &rect,
		const PaintContext &context,
		int width1) const {
	if (!context.topicJumpCache) {
		_topics->clearTopicJumpGeometry();
		return;
	}
	const auto width2 = countWidth() + st::forumDialogJumpArrowSkip;
	const auto geometry = FillJumpToLastBg(p, {
		.st = context.st,
		.corners = (context.selected
			? &context.topicJumpCache->over
			: &context.topicJumpCache->corners),
		.geometry = rect,
		.bg = (context.selected
			? st::dialogsRippleBg
			: st::dialogsBgOver),
		.width1 = width1,
		.width2 = width2,
	});
	if (context.topicJumpSelected) {
		p.setOpacity(0.1);
		FillJumpToLastPrepared(p, {
			.st = context.st,
			.corners = &context.topicJumpCache->selected,
			.bg = st::dialogsTextFg,
			.prepared = geometry,
		});
		p.setOpacity(1.);
	}
	if (!_topics->changeTopicJumpGeometry(geometry)) {
		auto color = st::dialogsTextFg->c;
		color.setAlpha(color.alpha() / 10);
		if (color.alpha() > 0) {
			_topics->paintRipple(p, 0, 0, context.width, &color);
		}
	}
}

HistoryView::ItemPreview PreviewWithSender(
		HistoryView::ItemPreview &&preview,
		const QString &sender,
		TextWithEntities topic) {
	auto senderWithOffset = topic.empty()
		? TextWithTagOffset<lt_from>::FromString(sender)
		: tr::lng_dialogs_text_from_in_topic(
			tr::now,
			lt_from,
			{ sender },
			lt_topic,
			std::move(topic),
			TextWithTagOffset<lt_from>::FromString);
	auto wrappedWithOffset = tr::lng_dialogs_text_from_wrapped(
		tr::now,
		lt_from,
		std::move(senderWithOffset.text),
		TextWithTagOffset<lt_from>::FromString);
	const auto wrappedSize = wrappedWithOffset.text.text.size();
	auto fullWithOffset = tr::lng_dialogs_text_with_from(
		tr::now,
		lt_from_part,
		Ui::Text::PlainLink(std::move(wrappedWithOffset.text)),
		lt_message,
		std::move(preview.text),
		TextWithTagOffset<lt_from_part>::FromString);
	preview.text = std::move(fullWithOffset.text);
	preview.arrowInTextPosition = (fullWithOffset.offset < 0
		|| wrappedWithOffset.offset < 0
		|| senderWithOffset.offset < 0)
		? -1
		: (fullWithOffset.offset
			+ wrappedWithOffset.offset
			+ senderWithOffset.offset
			+ sender.size());
	preview.imagesInTextPosition = (fullWithOffset.offset < 0)
		? 0
		: (fullWithOffset.offset + wrappedSize);
	return std::move(preview);
}

} // namespace Dialogs::Ui
