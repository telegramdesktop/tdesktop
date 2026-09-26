/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "iv/markdown/iv_markdown_article.h"

#include "ui/click_handler.h"

#include <memory>
#include <optional>

namespace style {
struct Markdown;
struct TextStyle;
} // namespace style

namespace Iv::Markdown {

class InlineFormulaObjectCache;

[[nodiscard]] ClickHandlerPtr CreatePreparedLinkHandler(PreparedLink link);
[[nodiscard]] std::optional<PreparedLink> ExtractPreparedLink(
	const ClickHandlerPtr &link);
void BindLinks(
	Ui::Text::String *leaf,
	const std::vector<PreparedLink> &links);
void SetTextLeafSpoilerLinkFilter(
	Ui::Text::String *leaf,
	Fn<bool(const ClickContext&)> spoilerLinkFilter = nullptr);

[[nodiscard]] std::shared_ptr<InlineFormulaObjectCache>
CreateInlineFormulaObjectCache(std::shared_ptr<MathRenderer> renderer);
void SetInlineFormulaObjectCacheRenderer(
	const std::shared_ptr<InlineFormulaObjectCache> &cache,
	std::shared_ptr<MathRenderer> renderer);
void ClearInlineFormulaObjectCache(
	const std::shared_ptr<InlineFormulaObjectCache> &cache);
void InvalidateInlineFormulaPaletteCache(
	const std::shared_ptr<InlineFormulaObjectCache> &cache);
void InvalidateInlineFormulaRasterCache(
	const std::shared_ptr<InlineFormulaObjectCache> &cache);

[[nodiscard]] const PreparedFormulaSlot *PreparedFormulaFor(
	const std::vector<PreparedFormulaSlot> &formulas,
	int formulaIndex);
[[nodiscard]] PreparedFormulaSlot *PreparedFormulaFor(
	std::vector<PreparedFormulaSlot> *formulas,
	int formulaIndex);
[[nodiscard]] RenderedFormula *FormulaRasterSlot(
	std::vector<RenderedFormula> *rendered,
	int formulaIndex);
[[nodiscard]] RenderedFormula EnsureFormulaRendered(
	const PreparedFormulaSlot *slot,
	RenderedFormula *rendered,
	MathRenderer *renderer,
	int devicePixelRatio,
	const style::Markdown &st);

struct InlineButtonPaintState {
	const style::Markdown *st = nullptr;
	std::unique_ptr<Ui::RippleAnimation> ripple;
	Fn<void()> repaint;
	QSize rippleSize;
	QRect rippleRect;
	QPoint pressPoint;
	QString lookedUpButton;
	int widthCap = 0;
	bool pressPending = false;
	bool editMode = false;
	bool bubbleGradient = false;
	RichButtonLoading buttonLoading;
};

void SetTextLeaf(
	Ui::Text::String *leaf,
	const style::TextStyle &textStyle,
	const style::Markdown &st,
	const TextWithEntities &text,
	const std::vector<PreparedFormulaSlot> *formulas,
	InlineFormulaObjectCache *inlineFormulaObjects,
	const std::shared_ptr<InlineButtonPaintState> &inlineButtonPaintState,
	int inlineButtonWidthCap,
	const std::shared_ptr<MediaRuntime> &mediaRuntime,
	int minResizeWidth,
	bool rtl,
	Fn<void()> repaint = nullptr,
	Fn<void(QRect)> repaintRect = nullptr,
	Fn<bool(const ClickContext&)> spoilerLinkFilter = nullptr,
	bool richButtonLabel = false);

[[nodiscard]] bool TextHasInlineButton(const TextWithEntities &text);
[[nodiscard]] QString InlineButtonTooltip(QStringView data);

[[nodiscard]] std::unique_ptr<Ui::Text::CustomEmoji> MakeInlineButtonObject(
	QStringView data,
	const style::TextStyle &textStyle,
	const style::Markdown &st,
	const Ui::Text::MarkedContext &context);

} // namespace Iv::Markdown
