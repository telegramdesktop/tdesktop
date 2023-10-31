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

	void setUpdateCallback(Fn<void()> callback) {
		_updateCallback = std::move(callback);
	}
	void update() {
		if (_updateCallback) {
			_updateCallback();
		}
	}

	void setLinesFilterController(std::shared_ptr<LinesFilterController> c) {
		_linesFilterController = std::move(c);
	}

protected:
	using LinesFilterControllerPtr = std::shared_ptr<LinesFilterController>;
	[[nodiscard]] LinesFilterControllerPtr linesFilterController() {
		return _linesFilterController;
	}

private:
	LinesFilterControllerPtr _linesFilterController;
	Fn<void()> _updateCallback;

};

} // namespace Statistic
