/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "iv/iv_rich_page.h"
#include "iv/markdown/iv_markdown_prepare_state.h"

namespace Iv::Markdown {

inline constexpr auto kNativeIvLinkSpecialColorIndex = 9;

static_assert(Iv::kTextDiffInsertedColorIndex
	== kNativeIvLinkSpecialColorIndex + 1);

struct PreparedIvRichText {
	TextWithEntities text;
	std::vector<PreparedLink> links;
	std::vector<QString> anchorIds;
};

struct NativeIvRichTextContext {
	int textSize = 0;
	int renderWidthCap = 0;
	int renderHeightCap = 0;
	bool dropClickHandlers = false;
};

[[nodiscard]] bool PrepareNativeIvPlainPlaceholderBlock(
	QString label,
	std::vector<PreparedBlock> *result);
[[nodiscard]] bool PrepareNativeIvPhotoBlock(
	const Iv::RichPage::Block &data,
	std::vector<PreparedBlock> *result,
	NativeIvPrepareState *state);
[[nodiscard]] bool PrepareNativeIvVideoBlock(
	const Iv::RichPage::Block &data,
	std::vector<PreparedBlock> *result,
	NativeIvPrepareState *state);
[[nodiscard]] bool PrepareNativeIvAudioBlock(
	const Iv::RichPage::Block &data,
	std::vector<PreparedBlock> *result,
	NativeIvPrepareState *state);
[[nodiscard]] bool PrepareNativeIvMapBlock(
	const Iv::RichPage::Block &data,
	std::vector<PreparedBlock> *result,
	NativeIvPrepareState *state);
[[nodiscard]] bool PrepareNativeIvChannelBlock(
	const Iv::RichPage::Block &data,
	std::vector<PreparedBlock> *result,
	NativeIvPrepareState *state);
[[nodiscard]] bool PrepareNativeIvGroupedMediaBlock(
	const Iv::RichPage::Block &data,
	std::vector<PreparedBlock> *result,
	NativeIvPrepareState *state);
[[nodiscard]] bool PrepareNativeIvRichText(
	const Iv::RichPage::RichText &text,
	PreparedIvRichText *result,
	QString *blockAnchorId,
	NativeIvPrepareState *state,
	NativeIvRichTextContext context = {});
[[nodiscard]] bool AppendPreparedIvRichBlock(
	std::vector<PreparedBlock> *result,
	PreparedBlockKind kind,
	int headingLevel,
	PreparedIvRichText prepared,
	QString anchorId = QString(),
	bool allowEmpty = false,
	bool supplementary = false,
	std::optional<PreparedEditBlockSource> editBlock = std::nullopt,
	std::optional<PreparedEditLeafSource> editLeaf = std::nullopt);

} // namespace Iv::Markdown
