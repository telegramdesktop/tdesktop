/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "iv/markdown/iv_markdown_microtex.h"

#include "base/basic_types.h"

#include <QtCore/QSize>
#include <QtCore/QString>
#include <QtGui/QImage>

#include <list>
#include <map>
#include <tuple>

namespace Iv::Markdown {

struct FormulaCacheKey {
	QString trimmedTex;
	MathKind kind = MathKind::Display;
	int textSize = 0;
	int renderWidthCap = 0;
	int renderHeightCap = 0;
	int devicePixelRatio = 1;
};

inline bool operator==(const FormulaCacheKey &a, const FormulaCacheKey &b) {
	return std::tie(
		a.trimmedTex,
		a.kind,
		a.textSize,
		a.renderWidthCap,
		a.renderHeightCap,
		a.devicePixelRatio) == std::tie(
		b.trimmedTex,
		b.kind,
		b.textSize,
		b.renderWidthCap,
		b.renderHeightCap,
		b.devicePixelRatio);
}

inline bool operator<(const FormulaCacheKey &a, const FormulaCacheKey &b) {
	return std::tie(
		a.trimmedTex,
		a.kind,
		a.textSize,
		a.renderWidthCap,
		a.renderHeightCap,
		a.devicePixelRatio) < std::tie(
		b.trimmedTex,
		b.kind,
		b.textSize,
		b.renderWidthCap,
		b.renderHeightCap,
		b.devicePixelRatio);
}

struct RenderedFormula {
	QImage image;
	QSize logicalSize;
	int logicalDepth = 0;
	FormulaExactMetrics exact;
	QString fallbackText;
	QString error;
	bool success = false;
	bool overflow = false;
	bool tooLarge = false;
};

struct FormulaDebugCounters {
	int rendered = 0;
	int failed = 0;
	int hits = 0;
	int misses = 0;
	int evictedEntries = 0;
	int64 evictedBytes = 0;
	int cacheEntries = 0;
	int64 cacheBytes = 0;
};

struct FormulaCacheMutation {
	int evictedEntries = 0;
	int64 evictedBytes = 0;
};

class FormulaCache {
public:
	[[nodiscard]] const RenderedFormula *find(
		const FormulaCacheKey &key);
	[[nodiscard]] FormulaCacheMutation put(
		FormulaCacheKey key,
		RenderedFormula value);
	[[nodiscard]] FormulaCacheMutation setBudgetBytes(int64 bytes);
	[[nodiscard]] int64 budgetBytes() const;
	[[nodiscard]] int64 sizeBytes() const;
	[[nodiscard]] int size() const;
	void clear();

private:
	struct Entry {
		RenderedFormula value;
		int64 sizeBytes = 0;
		std::list<FormulaCacheKey>::iterator lru;
	};

	[[nodiscard]] int64 estimateBytes(
		const FormulaCacheKey &key,
		const RenderedFormula &value) const;
	void touch(std::map<FormulaCacheKey, Entry>::iterator i);
	void erase(std::map<FormulaCacheKey, Entry>::iterator i);
	[[nodiscard]] FormulaCacheMutation evictToBudget();

	std::map<FormulaCacheKey, Entry> _entries;
	std::list<FormulaCacheKey> _lru;
	int64 _budgetBytes = 32 * 1024 * 1024;
	int64 _sizeBytes = 0;

};

class MathRenderer {
public:
	[[nodiscard]] MeasuredFormula measureFormula(
		const MicrotexMeasureRequest &request);
	[[nodiscard]] RenderedFormula renderFormula(
		const MicrotexRenderRequest &request);
	void clearCache(bool resetDebugCounters = false);
	void invalidate(bool resetDebugCounters = false);
	void resetDebugCounters();
	void setCacheBudgetBytes(int64 bytes);

	[[nodiscard]] const FormulaDebugCounters &debugCounters() const;
	[[nodiscard]] int64 cacheBudgetBytes() const;
	[[nodiscard]] int64 cacheUsageBytes() const;

private:
	[[nodiscard]] FormulaCacheKey makeKey(
		const MicrotexRenderRequest &request) const;
	void syncCacheCounters();
	void applyCacheMutation(FormulaCacheMutation mutation);

	FormulaCache _cache;
	FormulaDebugCounters _debugCounters;

};

} // namespace Iv::Markdown
