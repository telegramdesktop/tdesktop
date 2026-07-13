/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "iv/markdown/iv_markdown_prepare_native_richtext.h"

struct GeoPointLocation;

#include "base/algorithm.h"
#include "data/data_location.h"
#include "iv/markdown/iv_markdown_prepare_links.h"
#include "iv/markdown/iv_markdown_prepare_serialize.h"
#include "lang/lang_keys.h"
#include "ui/basic_click_handlers.h"
#include "history/history_location_manager.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace Iv::Markdown {
namespace {

constexpr auto kDefaultMapWidth = 400;
constexpr auto kDefaultMapHeight = 200;

[[nodiscard]] uint64 GeneratePreparedBlockIdValue(
		NativeIvPrepareState *state) {
	return uint64(++state->nextGeneratedId);
}

[[nodiscard]] PreparedMediaBlockId GeneratePreparedMediaBlockId(
		NativeIvPrepareState *state) {
	return { .value = GeneratePreparedBlockIdValue(state) };
}

[[nodiscard]] uint64 CanonicalPhotoId(const Iv::RichPage::Block &data) {
	return data.photoId;
}

[[nodiscard]] uint64 CanonicalPhotoId(
		const Iv::RichPage::GroupedMediaItem &data) {
	return data.photoId;
}

[[nodiscard]] uint64 CanonicalDocumentId(const Iv::RichPage::Block &data) {
	return data.documentId;
}

[[nodiscard]] uint64 CanonicalDocumentId(
		const Iv::RichPage::GroupedMediaItem &data) {
	return data.documentId;
}

[[nodiscard]] int CanonicalWidth(const Iv::RichPage::Block &data) {
	return data.width;
}

[[nodiscard]] int CanonicalWidth(
		const Iv::RichPage::GroupedMediaItem &data) {
	return data.width;
}

[[nodiscard]] int CanonicalHeight(const Iv::RichPage::Block &data) {
	return data.height;
}

[[nodiscard]] int CanonicalHeight(
		const Iv::RichPage::GroupedMediaItem &data) {
	return data.height;
}

[[nodiscard]] bool PrepareNativeIvGroupedMediaItem(
		const Iv::RichPage::GroupedMediaItem &item,
		PreparedGroupedMediaItemData *result) {
	switch (item.kind) {
	case Iv::RichPage::BlockKind::Photo: {
		const auto photoId = CanonicalPhotoId(item);
		const auto width = CanonicalWidth(item);
		const auto height = CanonicalHeight(item);
		if (!photoId || width <= 0 || height <= 0) {
			return false;
		}
		result->media.kind = PreparedMediaItemKind::Photo;
		result->media.id = photoId;
		result->media.width = width;
		result->media.height = height;
		result->media.spoiler = item.spoiler;
		return true;
	}
	case Iv::RichPage::BlockKind::Video: {
		const auto documentId = CanonicalDocumentId(item);
		const auto width = CanonicalWidth(item);
		const auto height = CanonicalHeight(item);
		if (!documentId || width <= 0 || height <= 0) {
			return false;
		}
		result->media.kind = PreparedMediaItemKind::Document;
		result->media.id = documentId;
		result->media.width = width;
		result->media.height = height;
		result->media.spoiler = item.spoiler;
		return true;
	}
	default:
		return false;
	}
}

void SortPreparedIvRichText(PreparedIvRichText *text) {
	SortEntities(&text->text);
}

[[nodiscard]] const QString &NativeIvLinkColorizedEntityData() {
	static const auto result = [] {
		auto value = QString();
		value.reserve(2);
		value.push_back(QChar(kNativeIvLinkSpecialColorIndex));
		value.push_back(QChar(kNativeIvLinkSpecialColorIndex));
		return value;
	}();
	return result;
}

[[nodiscard]] bool AddNativeIvPreparedLink(
		TextWithEntities *text,
		std::vector<PreparedLink> *links,
		int from,
		int length,
		QString target,
		uint64 webpageId = 0) {
	if (!length || target.isEmpty()) {
		return true;
	}
	const auto index = links->size() + 1;
	if (index > std::numeric_limits<uint16>::max()) {
		return true;
	}
	auto prepared = PreparedLink();
	if (webpageId) {
		prepared.index = uint16(index);
		prepared.kind = PreparedLinkKind::InstantViewPage;
		prepared.webpageId = webpageId;
		NormalizePreparedUrlLink(&prepared, target);
	} else {
		prepared = ClassifiedLink(uint16(index), target, nullptr);
	}
	if (prepared.kind == PreparedLinkKind::RejectedRelative
		|| prepared.kind == PreparedLinkKind::LocalFile) {
		return true;
	}
	FinalizePreparedUrlLink(&prepared, QStringView(text->text).mid(from, length));
	if (prepared.kind == PreparedLinkKind::InstantViewPage) {
		text->entities.push_back(EntityInText(
			EntityType::Colorized,
			from,
			length,
			NativeIvLinkColorizedEntityData()));
	}
	text->entities.push_back(EntityInText(
		EntityType::CustomUrl,
		from,
		length,
		InternalLinkData(uint16(index))));
	links->push_back(std::move(prepared));
	return true;
}

void AddNativeIvBlockAnchor(
		QString *blockAnchorId,
		std::vector<QString> *blockAnchorIds,
		QString anchorId) {
	if (anchorId.isEmpty()) {
		return;
	}
	if (blockAnchorId && blockAnchorId->isEmpty()) {
		*blockAnchorId = std::move(anchorId);
		return;
	}
	if (blockAnchorId && *blockAnchorId == anchorId) {
		return;
	}
	if (blockAnchorIds
		&& !ranges::contains(*blockAnchorIds, anchorId)) {
		blockAnchorIds->push_back(std::move(anchorId));
	}
}

[[nodiscard]] bool DropNativeIvClickHandlerEntity(EntityType type) {
	switch (type) {
	case EntityType::Mention:
	case EntityType::Hashtag:
	case EntityType::BotCommand:
	case EntityType::Cashtag:
	case EntityType::Url:
	case EntityType::Email:
	case EntityType::Phone:
	case EntityType::BankCard:
	case EntityType::CustomUrl:
	case EntityType::MentionName:
	case EntityType::FormattedDate:
		return true;
	default:
		return false;
	}
}

void RememberCanonicalInlineFormula(
		const EntityInText &entity,
		NativeIvPrepareState *state,
		NativeIvRichTextContext context) {
	if (entity.type() != EntityType::CustomEmoji) {
		return;
	}
	const auto parsed = ParseInlineTextObjectEntity(entity.data());
	if (!parsed || parsed->kind != InlineTextObjectKind::Formula) {
		return;
	}
	const auto formula = std::get_if<InlineTextObjectFormulaData>(&parsed->data);
	if (!formula) {
		return;
	}
	(void)state->rememberFormula(
		MathKind::Inline,
		formula->trimmedTex,
		context.textSize,
		context.renderWidthCap,
		context.renderHeightCap);
}

void AppendCanonicalNativeIvRichText(
		const Iv::RichPage::RichText &text,
		PreparedIvRichText *result,
		NativeIvPrepareState *state,
		NativeIvRichTextContext context) {
	const auto shift = result->text.text.size();
	result->text.text.append(text.text.text);
	result->text.entities.reserve(
		result->text.entities.size() + text.text.entities.size());
	for (const auto &entity : text.text.entities) {
		if (context.dropClickHandlers
			&& DropNativeIvClickHandlerEntity(entity.type())) {
			continue;
		}
		if (entity.type() == EntityType::CustomUrl) {
			if (entity.offset() < 0
				|| entity.length() <= 0
				|| entity.offset() >= text.text.text.size()) {
				continue;
			}
			const auto length = std::min(
				entity.length(),
				int(text.text.text.size()) - entity.offset());
			const auto decoded = Iv::DecodeRichPageLinkUrl(entity.data());
			(void)AddNativeIvPreparedLink(
				&result->text,
				&result->links,
				shift + entity.offset(),
				length,
				decoded ? decoded->url : entity.data(),
				decoded ? decoded->webpageId : 0);
			continue;
		}
		RememberCanonicalInlineFormula(entity, state, context);
		result->text.entities.push_back(EntityInText(
			entity.type(),
			entity.offset() + shift,
			entity.length(),
			entity.data()));
	}
}

[[nodiscard]] int ScaleNativeIvFormulaCap(
		int cap,
		int textSize,
		int baseTextSize) {
	if (cap <= 0) {
		return 0;
	}
	const auto numerator = int64(cap) * std::max(textSize, 1);
	const auto denominator = std::max(baseTextSize, 1);
	return std::max(int((numerator + denominator - 1) / denominator), 1);
}

[[nodiscard]] NativeIvRichTextContext ResolveNativeIvRichTextContext(
		NativeIvPrepareState *state,
		NativeIvRichTextContext context) {
	const auto bodyTextSize = state->dimensions.bodyTextSize;
	if (!context.textSize) {
		context.textSize = bodyTextSize;
	}
	if (!context.renderWidthCap) {
		context.renderWidthCap = ScaleNativeIvFormulaCap(
			state->dimensions.displayMathMaxRenderWidth,
			context.textSize,
			state->dimensions.displayMathTextSize);
	}
	if (!context.renderHeightCap) {
		context.renderHeightCap = ScaleNativeIvFormulaCap(
			state->dimensions.displayMathMaxRenderHeight,
			context.textSize,
			state->dimensions.displayMathTextSize);
	}
	return context;
}

[[nodiscard]] bool PrepareNativeIvCaption(
		const Iv::RichPage::RichText &caption,
		PreparedIvRichText *result,
		QString *blockAnchorId,
		NativeIvPrepareState *state) {
	return PrepareNativeIvRichText(
		caption,
		result,
		blockAnchorId,
		state);
}

[[nodiscard]] bool PrepareNativeIvCanonicalPlaceholderBlock(
		QString label,
		const Iv::RichPage::RichText &caption,
		QString anchorId,
		std::vector<PreparedBlock> *result,
		NativeIvPrepareState *state) {
	auto prepared = PreparedIvRichText();
	if (!PrepareNativeIvCaption(caption, &prepared, &anchorId, state)) {
		return state->result.failure.failed()
			? false
			: PrepareNativeIvPlainPlaceholderBlock(std::move(label), result);
	}
	SortPreparedIvRichText(&prepared);
	auto block = PreparedBlock();
	block.kind = PreparedBlockKind::Placeholder;
	block.text = std::move(prepared.text);
	block.links = std::move(prepared.links);
	block.anchorId = std::move(anchorId);
	block.anchorIds = std::move(prepared.anchorIds);
	block.supplementary = true;
	block.forceTextSegment = state->editMode;
	block.placeholder.label = std::move(label);
	block.placeholder.copyText = block.text.text.isEmpty()
		? block.placeholder.label
		: (block.placeholder.label + u"\n"_q + block.text.text);
	result->push_back(std::move(block));
	return true;
}

void ApplyEmptyMediaCaptionPlaceholder(
		PreparedBlock *block,
		NativeIvPrepareState *state) {
	if (!state->editMode || !block->text.text.isEmpty()) {
		return;
	}
	block->editPlaceholderText = tr::lng_article_placeholder_caption(tr::now);
}

} // namespace

bool PrepareNativeIvRichText(
		const Iv::RichPage::RichText &text,
		PreparedIvRichText *result,
		QString *blockAnchorId,
		NativeIvPrepareState *state,
		NativeIvRichTextContext context) {
	context = ResolveNativeIvRichTextContext(state, context);
	AddNativeIvBlockAnchor(blockAnchorId, &result->anchorIds, text.anchorId);
	for (const auto &anchorId : text.anchorIds) {
		AddNativeIvBlockAnchor(blockAnchorId, &result->anchorIds, anchorId);
	}
	AppendCanonicalNativeIvRichText(text, result, state, context);
	return true;
}

bool AppendPreparedIvRichBlock(
		std::vector<PreparedBlock> *result,
		PreparedBlockKind kind,
		int headingLevel,
		PreparedIvRichText prepared,
		QString anchorId,
		bool allowEmpty,
		bool supplementary,
		std::optional<PreparedEditBlockSource> editBlock,
		std::optional<PreparedEditLeafSource> editLeaf) {
	SortPreparedIvRichText(&prepared);
	if (prepared.text.text.isEmpty() && !allowEmpty) {
		return true;
	}
	auto block = PreparedBlock();
	block.kind = kind;
	block.headingLevel = headingLevel;
	block.text = std::move(prepared.text);
	block.links = std::move(prepared.links);
	block.anchorId = std::move(anchorId);
	block.anchorIds = std::move(prepared.anchorIds);
	block.supplementary = supplementary;
	block.editBlock = std::move(editBlock);
	block.editLeaf = std::move(editLeaf);
	result->push_back(std::move(block));
	return true;
}

bool PrepareNativeIvPlainPlaceholderBlock(
		QString label,
		std::vector<PreparedBlock> *result) {
	auto block = PreparedBlock();
	block.kind = PreparedBlockKind::Placeholder;
	block.placeholder.label = std::move(label);
	block.placeholder.copyText = block.placeholder.label;
	result->push_back(std::move(block));
	return true;
}

bool PrepareNativeIvPhotoBlock(
		const Iv::RichPage::Block &data,
		std::vector<PreparedBlock> *result,
		NativeIvPrepareState *state) {
	if (!CanonicalPhotoId(data)
		|| CanonicalWidth(data) <= 0
		|| CanonicalHeight(data) <= 0) {
		return state->editMode
			? PrepareNativeIvCanonicalPlaceholderBlock(
				u"Photo"_q,
				data.caption,
				data.anchorId,
				result,
				state)
			: true;
	}
	auto caption = PreparedIvRichText();
	auto anchorId = QString();
	if (!PrepareNativeIvCaption(data.caption, &caption, &anchorId, state)) {
		return false;
	}
	SortPreparedIvRichText(&caption);
	auto block = PreparedBlock();
	block.kind = PreparedBlockKind::Photo;
	block.text = std::move(caption.text);
	block.links = std::move(caption.links);
	block.anchorId = data.anchorId.isEmpty() ? std::move(anchorId) : data.anchorId;
	block.anchorIds = std::move(caption.anchorIds);
	block.supplementary = true;
	block.forceTextSegment = state->editMode;
	ApplyEmptyMediaCaptionPlaceholder(&block, state);
	block.photo.id = GeneratePreparedMediaBlockId(state);
	block.photo.photoId = CanonicalPhotoId(data);
	block.photo.width = CanonicalWidth(data);
	block.photo.height = CanonicalHeight(data);
	block.photo.urlOverride = data.url;
	block.photo.caption = block.text;
	block.photo.spoiler = data.spoiler;
	block.photo.viewerOpen = true;
	result->push_back(std::move(block));
	return true;
}

bool PrepareNativeIvVideoBlock(
		const Iv::RichPage::Block &data,
		std::vector<PreparedBlock> *result,
		NativeIvPrepareState *state) {
	if (!CanonicalDocumentId(data)
		|| CanonicalWidth(data) <= 0
		|| CanonicalHeight(data) <= 0) {
		return state->editMode
			? PrepareNativeIvCanonicalPlaceholderBlock(
				u"Video"_q,
				data.caption,
				data.anchorId,
				result,
				state)
			: true;
	}
	auto caption = PreparedIvRichText();
	auto anchorId = QString();
	if (!PrepareNativeIvCaption(data.caption, &caption, &anchorId, state)) {
		return false;
	}
	SortPreparedIvRichText(&caption);
	auto block = PreparedBlock();
	block.kind = PreparedBlockKind::Video;
	block.text = std::move(caption.text);
	block.links = std::move(caption.links);
	block.anchorId = data.anchorId.isEmpty() ? std::move(anchorId) : data.anchorId;
	block.anchorIds = std::move(caption.anchorIds);
	block.supplementary = true;
	block.forceTextSegment = state->editMode;
	ApplyEmptyMediaCaptionPlaceholder(&block, state);
	block.video.id = GeneratePreparedMediaBlockId(state);
	block.video.media.kind = PreparedMediaItemKind::Document;
	block.video.media.id = CanonicalDocumentId(data);
	block.video.media.width = CanonicalWidth(data);
	block.video.media.height = CanonicalHeight(data);
	block.video.media.spoiler = data.spoiler;
	block.video.caption = block.text;
	result->push_back(std::move(block));
	return true;
}

bool PrepareNativeIvAudioBlock(
		const Iv::RichPage::Block &data,
		std::vector<PreparedBlock> *result,
		NativeIvPrepareState *state) {
	if (!CanonicalDocumentId(data)) {
		return state->editMode
			? PrepareNativeIvCanonicalPlaceholderBlock(
				u"Audio"_q,
				data.caption,
				data.anchorId,
				result,
				state)
			: true;
	}
	auto caption = PreparedIvRichText();
	auto anchorId = QString();
	if (!PrepareNativeIvCaption(data.caption, &caption, &anchorId, state)) {
		return false;
	}
	SortPreparedIvRichText(&caption);
	auto block = PreparedBlock();
	block.kind = PreparedBlockKind::Audio;
	block.text = std::move(caption.text);
	block.links = std::move(caption.links);
	block.anchorId = data.anchorId.isEmpty() ? std::move(anchorId) : data.anchorId;
	block.anchorIds = std::move(caption.anchorIds);
	block.supplementary = true;
	block.forceTextSegment = state->editMode;
	ApplyEmptyMediaCaptionPlaceholder(&block, state);
	block.audio.id = GeneratePreparedMediaBlockId(state);
	block.audio.documentId = CanonicalDocumentId(data);
	block.audio.title = data.audioTitle;
	block.audio.performer = data.audioPerformer;
	block.audio.fileName = data.audioFileName;
	block.audio.duration = data.audioDuration;
	result->push_back(std::move(block));
	return true;
}

bool PrepareNativeIvMapBlock(
		const Iv::RichPage::Block &data,
		std::vector<PreparedBlock> *result,
		NativeIvPrepareState *state) {
	const auto width = (data.width > 0) ? data.width : kDefaultMapWidth;
	const auto height = (data.height > 0) ? data.height : kDefaultMapHeight;
	if (data.zoom <= 0 || (!data.accessHash && !state->editMode)) {
		return state->editMode
			? PrepareNativeIvCanonicalPlaceholderBlock(
				u"Map"_q,
				data.caption,
				data.anchorId,
				result,
				state)
			: true;
	}
	auto caption = PreparedIvRichText();
	auto anchorId = QString();
	if (!PrepareNativeIvCaption(data.caption, &caption, &anchorId, state)) {
		return false;
	}
	SortPreparedIvRichText(&caption);
	auto block = PreparedBlock();
	block.kind = PreparedBlockKind::Map;
	block.text = std::move(caption.text);
	block.links = std::move(caption.links);
	block.anchorId = data.anchorId.isEmpty() ? std::move(anchorId) : data.anchorId;
	block.anchorIds = std::move(caption.anchorIds);
	block.supplementary = true;
	block.forceTextSegment = state->editMode;
	ApplyEmptyMediaCaptionPlaceholder(&block, state);
	block.map.id = GeneratePreparedMediaBlockId(state);
	block.map.latitude = data.latitude;
	block.map.longitude = data.longitude;
	block.map.accessHash = data.accessHash;
	block.map.width = width;
	block.map.height = height;
	block.map.zoom = data.zoom;
	block.map.url = LocationClickHandler::Url(Data::LocationPoint(
		data.latitude,
		data.longitude,
		Data::LocationPoint::NoAccessHash));
	result->push_back(std::move(block));
	return true;
}

bool PrepareNativeIvChannelBlock(
		const Iv::RichPage::Block &data,
		std::vector<PreparedBlock> *result,
		NativeIvPrepareState *state) {
	auto prepared = PreparedChannelBlockData();
	prepared.channelId = data.channelId;
	prepared.title = data.channelTitle;
	prepared.username = data.username;
	if (!prepared.channelId || prepared.title.isEmpty()) {
		return true;
	}
	auto block = PreparedBlock();
	block.kind = PreparedBlockKind::Channel;
	prepared.id = GeneratePreparedMediaBlockId(state);
	block.channel = std::move(prepared);
	result->push_back(std::move(block));
	return true;
}

bool PrepareNativeIvGroupedMediaBlock(
		const Iv::RichPage::Block &data,
		std::vector<PreparedBlock> *result,
		NativeIvPrepareState *state) {
	auto block = PreparedBlock();
	block.kind = PreparedBlockKind::GroupedMedia;
	block.groupedMedia.id = GeneratePreparedMediaBlockId(state);
	block.groupedMedia.intent = (data.mediaIntent
		== Iv::RichPage::GroupedMediaIntent::Slideshow)
		? PreparedGroupedMediaIntent::Slideshow
		: PreparedGroupedMediaIntent::Collage;
	block.groupedMedia.items.reserve(data.mediaItems.size());
	for (const auto &item : data.mediaItems) {
		auto prepared = PreparedGroupedMediaItemData();
		prepared.id = GeneratePreparedMediaBlockId(state);
		if (!PrepareNativeIvGroupedMediaItem(item, &prepared)) {
			continue;
		}
		block.groupedMedia.items.push_back(std::move(prepared));
	}
	if (block.groupedMedia.items.empty()) {
		return true;
	}
	auto preparedCaption = PreparedIvRichText();
	auto anchorId = QString();
	if (!PrepareNativeIvCaption(
			data.caption,
			&preparedCaption,
			&anchorId,
			state)) {
		return false;
	}
	SortPreparedIvRichText(&preparedCaption);
	block.text = std::move(preparedCaption.text);
	block.links = std::move(preparedCaption.links);
	block.anchorId = data.anchorId.isEmpty() ? std::move(anchorId) : data.anchorId;
	block.anchorIds = std::move(preparedCaption.anchorIds);
	block.supplementary = true;
	block.forceTextSegment = state->editMode;
	ApplyEmptyMediaCaptionPlaceholder(&block, state);
	block.groupedMedia.caption = block.text;
	result->push_back(std::move(block));
	return true;
}

} // namespace Iv::Markdown
