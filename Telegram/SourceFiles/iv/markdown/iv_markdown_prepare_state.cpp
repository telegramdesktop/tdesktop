/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "iv/markdown/iv_markdown_prepare_state.h"

#include <algorithm>
#include <utility>

namespace Iv::Markdown {

void PrepareState::rememberFormula(
		int index,
		MathKind kind,
		QString formulaTex,
		int textSize,
		int renderWidthCap,
		int renderHeightCap) {
	if (index < 0) {
		return;
	}
	if (index >= int(result.formulas.size())) {
		result.formulas.resize(index + 1);
	}
	auto &slot = result.formulas[index];
	slot.trimmedTex = formulaTex.trimmed();
	slot.kind = kind;
	slot.textSize = textSize;
	slot.renderWidthCap = renderWidthCap;
	slot.renderHeightCap = renderHeightCap;
	slot.present = true;
}

void PrepareState::rememberFormula(const PreparedBlock &block) {
	rememberFormula(
		block.formulaIndex,
		block.mathKind,
		block.formulaTex,
		request->dimensions.displayMathTextSize,
		request->dimensions.displayMathMaxRenderWidth,
		request->dimensions.displayMathMaxRenderHeight);
}

int NativeIvPrepareState::rememberFormula(
		MathKind kind,
		QString formulaTex,
		int textSize,
		int renderWidthCap,
		int renderHeightCap) {
	const auto index = nextFormulaIndex++;
	if (index >= int(result.formulas.size())) {
		result.formulas.resize(index + 1);
	}
	auto &slot = result.formulas[index];
	slot.trimmedTex = formulaTex.trimmed();
	slot.kind = kind;
	slot.textSize = textSize;
	slot.renderWidthCap = renderWidthCap;
	slot.renderHeightCap = renderHeightCap;
	slot.present = true;
	return index;
}

int NativeIvPrepareState::rememberFormula(const PreparedBlock &block) {
	return rememberFormula(
		block.mathKind,
		block.formulaTex,
		dimensions.displayMathTextSize,
		dimensions.displayMathMaxRenderWidth,
		dimensions.displayMathMaxRenderHeight);
}

void PrepareState::addPrepareWarning() {
	++result.debug.prepareWarningCount;
}

void PrepareState::addFormulaWarning() {
	++result.debug.formulaWarningCount;
}

void NativeIvPrepareState::addFormulaWarning() {
	++result.debug.formulaWarningCount;
}

void PrepareState::addPrepareWarnings(int count) {
	result.debug.prepareWarningCount += count;
}

void PrepareState::addFormulaWarnings(int count) {
	result.debug.formulaWarningCount += count;
}

void PrepareState::setTerminalFailure(
		PrepareTerminalFailure terminal,
		QString debugReason) {
	if (result.failure.failed()) {
		return;
	}
	result.failure.terminal = terminal;
	result.failure.debugReason = std::move(debugReason);
}

QString PrepareState::formulaSourceText(int index) const {
	if (!request
		|| !request->document
		|| index < 0
		|| index >= int(request->document->formulas.size())) {
		return QString();
	}
	const auto &range = request->document->formulas[index].range;
	const auto size = int(sourceUtf8.size());
	const auto from = std::clamp(range.startOffset, 0, size);
	const auto till = std::clamp(range.endOffset, from, size);
	return QString::fromUtf8(sourceUtf8.constData() + from, till - from);
}

void NativeIvPrepareState::setFailure(
		PrepareTerminalFailure terminal,
		QString debugReason) {
	if (result.failure.failed()) {
		return;
	}
	result.failure.terminal = terminal;
	result.failure.debugReason = std::move(debugReason);
}

bool NativeIvPrepareState::blocked() const {
	return result.failure.failed();
}

QString InvalidStyleReason(const MarkdownPrepareDimensions &dimensions) {
	Q_UNUSED(dimensions);
	return QString();
}

void ClearPreparedOutput(MarkdownArticleContent *result) {
	result->blocks.blocks.clear();
	result->footnotes.clear();
	result->formulas.clear();
}

} // namespace Iv::Markdown
