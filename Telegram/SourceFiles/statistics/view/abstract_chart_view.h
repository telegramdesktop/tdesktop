/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Data {
struct StatisticalChart;
} // namespace Data

namespace Statistic {

struct Limits;
class LinesFilterController;

struct PaintContext final {
	const Data::StatisticalChart &chartData;
	const Limits xIndices;
	const Limits xPercentageLimits;
	const Limits heightLimits;
	const QRect &rect;
	bool footer = false;
};

struct CachedSelectedPoints final {
	[[nodiscard]] bool isSame(int x, const PaintContext &c) const;

	int lastXIndex = -1;
	Limits lastHeightLimits;
	Limits lastXLimits;
	base::flat_map<int, QPointF> points;
};

class DoubleLineRatios final : std::pair<float64, float64> {
public:
	DoubleLineRatios(bool isDouble);

	operator bool() const {
		return first > 0;
	}

	void init(const Data::StatisticalChart &chartData);
	[[nodiscard]] float64 ratio(int lineId) const;
};

class AbstractChartView {
public:
	virtual ~AbstractChartView() = default;

	virtual void paint(QPainter &p, const PaintContext &c) = 0;

	virtual void paintSelectedXIndex(
		QPainter &p,
		const PaintContext &c,
		int selectedXIndex,
		float64 progress) = 0;

	[[nodiscard]] virtual int findXIndexByPosition(
		const Data::StatisticalChart &chartData,
		const Limits &xPercentageLimits,
		const QRect &rect,
		float64 x) = 0;

	struct HeightLimits final {
		Limits full;
		Limits ranged;
	};

	[[nodiscard]] virtual HeightLimits heightLimits(
		Data::StatisticalChart &chartData,
		Limits xIndices) = 0;

	struct LocalZoomResult final {
		bool hasZoom = false;
		Limits limitIndices;
		Limits range;
	};

	struct LocalZoomArgs final {
		enum class Type {
			Prepare,
			SkipCalculation,
			CheckAvailability,
			Process,
			SaveZoomFromFooter,
		};
		const Data::StatisticalChart &chartData;
		Type type;
		float64 progress = 0.;
		int xIndex = 0;
	};

	virtual LocalZoomResult maybeLocalZoom(const LocalZoomArgs &args) {
		return {};
	}

	virtual void handleMouseMove(
		const Data::StatisticalChart &chartData,
		const QRect &rect,
		const QPoint &p) {
	}

	void setUpdateCallback(Fn<void()> callback);
	void update();

	void setLinesFilterController(std::shared_ptr<LinesFilterController> c);

protected:
	using LinesFilterControllerPtr = std::shared_ptr<LinesFilterController>;
	[[nodiscard]] LinesFilterControllerPtr linesFilterController() const;

private:
	LinesFilterControllerPtr _linesFilterController;
	Fn<void()> _updateCallback;

};

AbstractChartView::HeightLimits DefaultHeightLimits(
	const DoubleLineRatios &ratios,
	const std::shared_ptr<LinesFilterController> &linesFilter,
	Data::StatisticalChart &chartData,
	Limits xIndices);

} // namespace Statistic
