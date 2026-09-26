/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "iv/markdown/iv_markdown_prepare_formulas.h"

#include <QtCore/QElapsedTimer>

#include <algorithm>
#include <memory>
#include <utility>

namespace Iv::Markdown {
namespace {

[[nodiscard]] PreparedFormulaMeasurementSignature FormulaMeasurementSignature(
		const PreparedFormulaSlot &slot,
		const MarkdownPrepareDimensions &dimensions) {
	return {
		.trimmedTex = slot.trimmedTex.trimmed(),
		.kind = slot.kind,
		.textSize = slot.textSize
			? slot.textSize
			: dimensions.displayMathTextSize,
		.renderWidthCap = slot.renderWidthCap
			? slot.renderWidthCap
			: dimensions.displayMathMaxRenderWidth,
		.renderHeightCap = slot.renderHeightCap
			? slot.renderHeightCap
			: dimensions.displayMathMaxRenderHeight,
	};
}

[[nodiscard]] std::shared_ptr<const MeasuredFormula>
FindDocumentFormulaMeasurement(
		const std::shared_ptr<const PreparedDocument> &document,
		int index,
		const PreparedFormulaMeasurementSignature &signature) {
	const auto cache = document ? document->formulaMeasurementCache : nullptr;
	if (!cache) {
		return nullptr;
	}
	if (index >= 0 && index < int(cache->slots.size())) {
		const auto &entry = cache->slots[index];
		if (entry.data && entry.signature == signature) {
			cache->bySignature.emplace(signature, entry.data);
			return entry.data;
		}
	}
	if (const auto i = cache->bySignature.find(signature)
		; i != end(cache->bySignature)) {
		if (index >= 0) {
			if (index >= int(cache->slots.size())) {
				cache->slots.resize(index + 1);
			}
			cache->slots[index] = {
				.signature = signature,
				.data = i->second,
			};
		}
		return i->second;
	}
	return nullptr;
}

void RememberDocumentFormulaMeasurement(
		const std::shared_ptr<const PreparedDocument> &document,
		int index,
		PreparedFormulaMeasurementSignature signature,
		std::shared_ptr<const MeasuredFormula> data) {
	const auto cache = document ? document->formulaMeasurementCache : nullptr;
	if (!cache || index < 0 || !data) {
		return;
	}
	auto shared = std::move(data);
	if (const auto i = cache->bySignature.find(signature)
		; i != end(cache->bySignature)) {
		shared = i->second;
	} else {
		cache->bySignature.emplace(signature, shared);
	}
	if (index >= int(cache->slots.size())) {
		cache->slots.resize(index + 1);
	}
	cache->slots[index] = {
		.signature = std::move(signature),
		.data = std::move(shared),
	};
}

} // namespace

int CountPreparedBlocks(const std::vector<PreparedBlock> &blocks) {
	auto result = 0;
	for (const auto &block : blocks) {
		++result;
		result += CountPreparedBlocks(block.children);
	}
	return result;
}

int FormulaSlotCount(const PreparedDocument &document) {
	auto result = 0;
	for (const auto &formula : document.formulas) {
		result = std::max(result, formula.index + 1);
	}
	return result;
}

void MeasurePreparedFormulas(PrepareState *state) {
	const auto &dimensions = state->request->dimensions;
	auto ownedRenderer = std::shared_ptr<MathRenderer>();
	auto renderer = state->request ? state->request->renderer.get() : nullptr;
	if (!renderer) {
		ownedRenderer = std::make_shared<MathRenderer>();
		renderer = ownedRenderer.get();
	}
	auto timer = QElapsedTimer();
	timer.start();
	for (auto i = 0, count = int(state->result.formulas.size()); i != count; ++i) {
		auto &slot = state->result.formulas[i];
		if (!slot.present) {
			continue;
		}
		const auto signature = FormulaMeasurementSignature(slot, dimensions);
		if (const auto cached = FindDocumentFormulaMeasurement(
				state->request->document,
				i,
				signature)) {
			slot.measuredData = cached;
			slot.measured = *cached;
		} else {
			auto data = std::make_shared<MeasuredFormula>(renderer->measureFormula({
				.trimmedTex = signature.trimmedTex,
				.kind = signature.kind,
				.textSize = signature.textSize,
				.renderWidthCap = signature.renderWidthCap,
				.renderHeightCap = signature.renderHeightCap,
			}));
			slot.measuredData = data;
			slot.measured = *data;
			RememberDocumentFormulaMeasurement(
				state->request->document,
				i,
				signature,
				std::move(data));
		}
		if (!slot.measured.success) {
			state->addFormulaWarning();
		}
	}
	state->result.debug.formulaMeasureMs = int(timer.elapsed());
	state->result.debug.formulaRenderMs
		= state->result.debug.formulaMeasureMs;
}

void MeasureNativeIvPreparedFormulas(NativeIvPrepareState *state) {
	MeasureNativeIvPreparedFormulas(
		state,
		0,
		int(state->result.formulas.size()));
}

void MeasureNativeIvPreparedFormulas(
		NativeIvPrepareState *state,
		int from,
		int till) {
	const auto &dimensions = state->dimensions;
	auto renderer = MathRenderer();
	auto timer = QElapsedTimer();
	timer.start();
	const auto count = int(state->result.formulas.size());
	from = std::clamp(from, 0, count);
	till = std::clamp(till, from, count);
	for (auto i = from; i != till; ++i) {
		auto &slot = state->result.formulas[i];
		if (!slot.present) {
			continue;
		}
		const auto signature = FormulaMeasurementSignature(slot, dimensions);
		auto data = std::make_shared<MeasuredFormula>(renderer.measureFormula({
			.trimmedTex = signature.trimmedTex,
			.kind = signature.kind,
			.textSize = signature.textSize,
			.renderWidthCap = signature.renderWidthCap,
			.renderHeightCap = signature.renderHeightCap,
		}));
		slot.measuredData = data;
		slot.measured = *data;
		if (!slot.measured.success) {
			state->addFormulaWarning();
		}
	}
	state->result.debug.formulaMeasureMs = int(timer.elapsed());
	state->result.debug.formulaRenderMs
		= state->result.debug.formulaMeasureMs;
}

} // namespace Iv::Markdown
