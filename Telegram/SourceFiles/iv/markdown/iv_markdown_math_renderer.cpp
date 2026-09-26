/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "iv/markdown/iv_markdown_math_renderer.h"

#include <algorithm>
#include <utility>

namespace Iv::Markdown {
namespace {

[[nodiscard]] FormulaCacheKey NormalizeKey(
		const MicrotexRenderRequest &request) {
	return {
		.trimmedTex = request.trimmedTex.trimmed(),
		.kind = request.kind,
		.textSize = request.textSize,
		.renderWidthCap = request.renderWidthCap,
		.renderHeightCap = request.renderHeightCap,
		.devicePixelRatio = request.devicePixelRatio,
	};
}

[[nodiscard]] MicrotexMeasureRequest NormalizeRequest(
		const MicrotexMeasureRequest &request) {
	return {
		.trimmedTex = request.trimmedTex.trimmed(),
		.kind = request.kind,
		.textSize = request.textSize,
		.renderWidthCap = request.renderWidthCap,
		.renderHeightCap = request.renderHeightCap,
	};
}

[[nodiscard]] QString FallbackText(const QString &trimmedTex) {
	return trimmedTex.isEmpty()
		? u"[math]"_q
		: trimmedTex;
}

[[nodiscard]] int64 EstimateQStringBytes(const QString &value) {
	return int64(value.size()) * sizeof(QChar);
}

[[nodiscard]] int64 EstimateQImageBytes(const QImage &image) {
	return image.isNull() ? 0 : int64(image.sizeInBytes());
}

[[nodiscard]] MeasuredFormula FinalizeMeasured(
		QString trimmedTex,
		MeasuredFormula result) {
	result.fallbackText = FallbackText(trimmedTex);
	return result;
}

[[nodiscard]] RenderedFormula FinalizeRendered(
		QString trimmedTex,
		MicrotexRenderResult result) {
	auto measured = FinalizeMeasured(
		std::move(trimmedTex),
		std::move(result.measured));
	auto rendered = RenderedFormula();
	rendered.image = std::move(result.image);
	rendered.logicalSize = measured.logicalSize;
	rendered.logicalDepth = measured.logicalDepth;
	rendered.exact = measured.exact;
	rendered.fallbackText = std::move(measured.fallbackText);
	rendered.error = std::move(measured.error);
	rendered.success = measured.success && !rendered.image.isNull();
	rendered.overflow = measured.overflow;
	rendered.tooLarge = measured.tooLarge;
	return rendered;
}

} // namespace

const RenderedFormula *FormulaCache::find(const FormulaCacheKey &key) {
	const auto i = _entries.find(key);
	if (i == _entries.end()) {
		return nullptr;
	}
	touch(i);
	return &i->second.value;
}

FormulaCacheMutation FormulaCache::put(
		FormulaCacheKey key,
		RenderedFormula value) {
	if (_budgetBytes <= 0) {
		if (const auto i = _entries.find(key); i != _entries.end()) {
			erase(i);
		}
		return {};
	}
	const auto sizeBytes = estimateBytes(key, value);
	if (sizeBytes > _budgetBytes) {
		if (const auto i = _entries.find(key); i != _entries.end()) {
			erase(i);
		}
		return {};
	}
	if (const auto i = _entries.find(key); i != _entries.end()) {
		erase(i);
	}
	_lru.push_back(key);
	const auto lru = std::prev(_lru.end());
	_entries.emplace(std::move(key), Entry{
		.value = std::move(value),
		.sizeBytes = sizeBytes,
		.lru = lru,
	});
	_sizeBytes += sizeBytes;
	return evictToBudget();
}

FormulaCacheMutation FormulaCache::setBudgetBytes(int64 bytes) {
	_budgetBytes = std::max<int64>(0, bytes);
	return evictToBudget();
}

int64 FormulaCache::budgetBytes() const {
	return _budgetBytes;
}

int64 FormulaCache::sizeBytes() const {
	return _sizeBytes;
}

int FormulaCache::size() const {
	return int(_entries.size());
}

void FormulaCache::clear() {
	_entries.clear();
	_lru.clear();
	_sizeBytes = 0;
}

int64 FormulaCache::estimateBytes(
		const FormulaCacheKey &key,
		const RenderedFormula &value) const {
	return sizeof(FormulaCacheKey)
		+ sizeof(Entry)
		+ EstimateQStringBytes(key.trimmedTex)
		+ EstimateQImageBytes(value.image)
		+ EstimateQStringBytes(value.fallbackText)
		+ EstimateQStringBytes(value.error);
}

void FormulaCache::touch(std::map<FormulaCacheKey, Entry>::iterator i) {
	_lru.erase(i->second.lru);
	_lru.push_back(i->first);
	i->second.lru = std::prev(_lru.end());
}

void FormulaCache::erase(std::map<FormulaCacheKey, Entry>::iterator i) {
	_sizeBytes -= i->second.sizeBytes;
	_lru.erase(i->second.lru);
	_entries.erase(i);
}

FormulaCacheMutation FormulaCache::evictToBudget() {
	auto result = FormulaCacheMutation();
	while (((_budgetBytes <= 0) || (_sizeBytes > _budgetBytes)) && !_lru.empty()) {
		const auto oldest = _lru.front();
		const auto i = _entries.find(oldest);
		if (i == _entries.end()) {
			_lru.pop_front();
			continue;
		}
		result.evictedBytes += i->second.sizeBytes;
		++result.evictedEntries;
		erase(i);
	}
	return result;
}

MeasuredFormula MathRenderer::measureFormula(
		const MicrotexMeasureRequest &request) {
	const auto normalized = NormalizeRequest(request);
	return FinalizeMeasured(
		normalized.trimmedTex,
		MeasureWithMicrotex(normalized));
}

RenderedFormula MathRenderer::renderFormula(
		const MicrotexRenderRequest &request) {
	const auto key = makeKey(request);
	if (const auto cached = _cache.find(key)) {
		++_debugCounters.hits;
		return *cached;
	}
	++_debugCounters.misses;
	auto rendered = FinalizeRendered(
		key.trimmedTex,
		RenderWithMicrotex({
			.trimmedTex = key.trimmedTex,
			.kind = key.kind,
			.textSize = key.textSize,
			.renderWidthCap = key.renderWidthCap,
			.renderHeightCap = key.renderHeightCap,
			.devicePixelRatio = key.devicePixelRatio,
		}));
	if (rendered.success) {
		++_debugCounters.rendered;
	} else {
		++_debugCounters.failed;
	}
	applyCacheMutation(_cache.put(key, rendered));
	return rendered;
}

void MathRenderer::clearCache(bool resetDebugCounters) {
	_cache.clear();
	if (resetDebugCounters) {
		_debugCounters = FormulaDebugCounters();
	} else {
		syncCacheCounters();
	}
}

void MathRenderer::invalidate(bool resetDebugCounters) {
	clearCache(resetDebugCounters);
}

void MathRenderer::resetDebugCounters() {
	_debugCounters = FormulaDebugCounters();
	syncCacheCounters();
}

void MathRenderer::setCacheBudgetBytes(int64 bytes) {
	applyCacheMutation(_cache.setBudgetBytes(bytes));
}

const FormulaDebugCounters &MathRenderer::debugCounters() const {
	return _debugCounters;
}

int64 MathRenderer::cacheBudgetBytes() const {
	return _cache.budgetBytes();
}

int64 MathRenderer::cacheUsageBytes() const {
	return _cache.sizeBytes();
}

FormulaCacheKey MathRenderer::makeKey(
		const MicrotexRenderRequest &request) const {
	return NormalizeKey(request);
}

void MathRenderer::syncCacheCounters() {
	_debugCounters.cacheEntries = _cache.size();
	_debugCounters.cacheBytes = _cache.sizeBytes();
}

void MathRenderer::applyCacheMutation(FormulaCacheMutation mutation) {
	_debugCounters.evictedEntries += mutation.evictedEntries;
	_debugCounters.evictedBytes += mutation.evictedBytes;
	syncCacheCounters();
}

} // namespace Iv::Markdown
