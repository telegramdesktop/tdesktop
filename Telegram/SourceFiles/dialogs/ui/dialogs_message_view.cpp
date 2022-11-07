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
, _topicCache(st::dialogsTextWidthMin)
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

bool MessageView::prepared(not_null<const HistoryItem*> item) const {
	return (_textCachedFor == item.get());
}

void MessageView::prepare(
		not_null<const HistoryItem*> item,
		Fn<void()> customEmojiRepaint,
		ToPreviewOptions options) {
	options.existing = &_imagesCache;
	auto preview = item->toPreview(options);
	const auto hasImages = !preview.images.empty();
	const auto hasArrow = (preview.arrowInTextPosition > 0)
		&& (preview.imagesInTextPosition > preview.arrowInTextPosition);
	const auto history = item->history();
	const auto context = Core::MarkedTextContext{
		.session = &history->session(),
		.customEmojiRepaint = customEmojiRepaint,
		.customEmojiLoopLimit = kEmojiLoopCount,
	};
	const auto senderTill = (preview.arrowInTextPosition > 0)
		? preview.arrowInTextPosition
		: preview.imagesInTextPosition;
	if ((hasImages || hasArrow) && senderTill > 0) {
		auto sender = Text::Mid(preview.text, 0, senderTill);
		TextUtilities::Trim(sender);
		_senderCache.setMarkedText(
			st::dialogsTextStyle,
			std::move(sender),
			DialogTextOptions());
		const auto topicTill = preview.imagesInTextPosition;
		if (hasArrow && hasImages) {
			auto topic = Text::Mid(
				preview.text,
				senderTill,
				topicTill - senderTill);
			TextUtilities::Trim(topic);
			_topicCache.setMarkedText(
				st::dialogsTextStyle,
				std::move(topic),
				DialogTextOptions(),
				context);
			preview.text = Text::Mid(preview.text, topicTill);
		} else {
			preview.text = Text::Mid(preview.text, senderTill);
			_topicCache = { st::dialogsTextWidthMin };
		}
	} else {
		_topicCache = { st::dialogsTextWidthMin };
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
	const auto palette = &(context.active
		? st::dialogsTextPaletteActive
		: context.selected
		? st::dialogsTextPaletteOver
		: st::dialogsTextPalette);

	auto rect = geometry;
	const auto lines = rect.height() / st::dialogsTextFont->height;
	if (!_senderCache.isEmpty()) {
		_senderCache.draw(p, {
			.position = rect.topLeft(),
			.availableWidth = rect.width(),
			.palette = palette,
			.elisionLines = lines,
		});
		rect.setLeft(rect.x() + _senderCache.maxWidth());
		if (!_topicCache.isEmpty() || _imagesCache.empty()) {
			const auto skip = st::dialogsTopicArrowSkip;
			if (rect.width() >= skip) {
				const auto &icon = st::dialogsTopicArrow;
				icon.paint(
					p,
					rect.x() + (skip - icon.width()) / 2,
					rect.y() + st::dialogsTopicArrowTop,
					geometry.width());
			}
			rect.setLeft(rect.x() + skip);
		}
		if (!_topicCache.isEmpty()) {
			if (!rect.isEmpty()) {
				_topicCache.draw(p, {
					.position = rect.topLeft(),
					.availableWidth = rect.width(),
					.palette = palette,
					.spoiler = Text::DefaultSpoilerCache(),
					.now = context.now,
					.paused = context.paused,
					.elisionLines = lines,
				});
			}
			rect.setLeft(rect.x() + _topicCache.maxWidth());
		}
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
	if (rect.isEmpty()) {
		return;
	}
	_textCache.draw(p, {
		.position = rect.topLeft(),
		.availableWidth = rect.width(),
		.palette = palette,
		.spoiler = Text::DefaultSpoilerCache(),
		.now = context.now,
		.paused = context.paused,
		.elisionLines = lines,
	});
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
