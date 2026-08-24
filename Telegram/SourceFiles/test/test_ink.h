/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"

#include <QtCore/QRect>
#include <QtCore/QString>
#include <QtGui/QColor>
#include <QtGui/QImage>

#include <vector>

namespace Test {

inline constexpr auto kInkDelta = 45;
inline constexpr auto kSameTolerance = 40;
inline constexpr auto kOnLine = 10.;
inline constexpr auto kInkMargin = 8.;
inline constexpr auto kBackgroundSame = 6;
inline constexpr auto kMinInkPixels = 24;

struct InkCandidate {
	QString name;
	QColor color;
};

struct DerivedBand {
	bool ok = false;
	QRect fillRegion;
	QRect band;
	std::vector<int> rows;
	int rowsExamined = 0;
	int fillRows = 0;
	QString reason;
};

struct InkScan {
	bool ok = false;
	QRect band;
	QColor background;
	QColor ink;
	QColor classifiedInk;
	int total = 0;
	int inkPixels = 0;
	int backgroundPixels = 0;
	int ambiguous = 0;
	int scannedRows = 0;
	int widestRun = 0;
	int widestRunRow = -1;
	std::vector<InkCandidate> candidates;
	std::vector<int> counts;
};

struct InkMeasure {
	DerivedBand derived;
	InkScan scan;
	bool separable = false;
	QString collision;
	float64 contrast = 0.;
	QString report;
};

[[nodiscard]] int ChannelDelta(QColor a, QColor b);

[[nodiscard]] bool Separable(const std::vector<InkCandidate> &candidates);

[[nodiscard]] QString CollisionDump(
	const std::vector<InkCandidate> &candidates);

[[nodiscard]] DerivedBand DeriveBand(
	const QImage &image,
	QRect box,
	QColor fill);

[[nodiscard]] InkScan ScanInk(
	const QImage &image,
	QRect band,
	std::vector<InkCandidate> candidates,
	const std::vector<int> &onlyRows = {});

[[nodiscard]] float64 InkContrast(QColor a, QColor b);

[[nodiscard]] QString FormatInkReport(
	QColor fill,
	QColor ink,
	float64 contrast,
	int inkPixels);

[[nodiscard]] InkMeasure MeasurePaintedInk(
	const QImage &image,
	QRect box,
	QColor fill,
	std::vector<InkCandidate> candidates);

} // namespace Test
