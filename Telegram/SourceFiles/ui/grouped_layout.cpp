/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/grouped_layout.h"

namespace Ui {
namespace {

int Round(float64 value) {
	return int(std::round(value));
}

class Layouter {
public:
	Layouter(
		const std::vector<QSize> &sizes,
		int maxWidth,
		int minWidth,
		int spacing);

	std::vector<GroupMediaLayout> layout() const;

private:
	static std::vector<float64> CountRatios(const std::vector<QSize> &sizes);
	static std::string CountProportions(const std::vector<float64> &ratios);

	std::vector<GroupMediaLayout> layoutTwo() const;
	std::vector<GroupMediaLayout> layoutThree() const;
	std::vector<GroupMediaLayout> layoutFour() const;

	std::vector<GroupMediaLayout> layoutOne() const;
	std::vector<GroupMediaLayout> layoutTwoTopBottom() const;
	std::vector<GroupMediaLayout> layoutTwoLeftRightEqual() const;
	std::vector<GroupMediaLayout> layoutTwoLeftRight() const;
	std::vector<GroupMediaLayout> layoutThreeLeftAndOther() const;
	std::vector<GroupMediaLayout> layoutThreeTopAndOther() const;
	std::vector<GroupMediaLayout> layoutFourLeftAndOther() const;
	std::vector<GroupMediaLayout> layoutFourTopAndOther() const;

	const std::vector<QSize> &_sizes;
	const std::vector<float64> _ratios;
	const std::string _proportions;
	const int _count = 0;
	const int _maxWidth = 0;
	const int _maxHeight = 0;
	const int _minWidth = 0;
	const int _spacing = 0;
	const float64 _averageRatio = 1.;
	const float64 _maxSizeRatio = 1.;

};

class ComplexLayouter {
public:
	ComplexLayouter(
		const std::vector<float64> &ratios,
		float64 averageRatio,
		int maxWidth,
		int minWidth,
		int spacing);

	std::vector<GroupMediaLayout> layout() const;

private:
	struct Attempt {
		std::vector<int> lineCounts;
		std::vector<float64> heights;
	};

	static std::vector<float64> CropRatios(
		const std::vector<float64> &ratios,
		float64 averageRatio);

	const std::vector<float64> _ratios;
	const int _count = 0;
	const int _maxWidth = 0;
	const int _maxHeight = 0;
	const int _minWidth = 0;
	const int _spacing = 0;
	const float64 _averageRatio = 1.;

};

Layouter::Layouter(
	const std::vector<QSize> &sizes,
	int maxWidth,
	int minWidth,
	int spacing)
: _sizes(sizes)
, _ratios(CountRatios(_sizes))
, _proportions(CountProportions(_ratios))
, _count(int(_ratios.size()))
// All apps currently use square max size first.
// In complex case they use maxWidth * 4 / 3 as maxHeight.
, _maxWidth(maxWidth)
, _maxHeight(maxWidth)
, _minWidth(minWidth)
, _spacing(spacing)
, _averageRatio(ranges::accumulate(_ratios, 1.) / _count)
, _maxSizeRatio(_maxWidth / float64(_maxHeight)) {
}

std::vector<float64> Layouter::CountRatios(const std::vector<QSize> &sizes) {
	return ranges::views::all(
		sizes
	) | ranges::views::transform([](const QSize &size) {
		return size.width() / float64(size.height());
	}) | ranges::to_vector;
}

std::string Layouter::CountProportions(const std::vector<float64> &ratios) {
	return ranges::views::all(
		ratios
	) | ranges::views::transform([](float64 ratio) {
		return (ratio > 1.2) ? 'w' : (ratio < 0.8) ? 'n' : 'q';
	}) | ranges::to<std::string>();
}

std::vector<GroupMediaLayout> Layouter::layout() const {
	if (!_count) {
		return {};
	} else if (_count == 1) {
		return layoutOne();
	}

	using namespace rpl::mappers;
	if (_count >= 5 || ranges::find_if(_ratios, _1 > 2) != _ratios.end()) {
		return ComplexLayouter(
			_ratios,
			_averageRatio,
			_maxWidth,
			_minWidth,
			_spacing).layout();
	}

	if (_count == 2) {
		return layoutTwo();
	} else if (_count == 3) {
		return layoutThree();
	}
	return layoutFour();
}

std::vector<GroupMediaLayout> Layouter::layoutTwo() const {
	Expects(_count == 2);

	if ((_proportions == "ww")
		&& (_averageRatio > 1.4 * _maxSizeRatio)
		&& (_ratios[1] - _ratios[0] < 0.2)) {
		return layoutTwoTopBottom();
	} else if (_proportions == "ww" || _proportions == "qq") {
		return layoutTwoLeftRightEqual();
	}
	return layoutTwoLeftRight();
}

std::vector<GroupMediaLayout> Layouter::layoutThree() const {
	Expects(_count == 3);

	auto result = std::vector<GroupMediaLayout>(_count);
	if (_proportions[0] == 'n') {
		return layoutThreeLeftAndOther();
	}
	return layoutThreeTopAndOther();
}

std::vector<GroupMediaLayout> Layouter::layoutFour() const {
	Expects(_count == 4);

	auto result = std::vector<GroupMediaLayout>(_count);
	if (_proportions[0] == 'w') {
		return layoutFourTopAndOther();
	}
	return layoutFourLeftAndOther();
}

std::vector<GroupMediaLayout> Layouter::layoutOne() const {
	Expects(_count == 1);

	const auto width = _maxWidth;
	const auto height = (_sizes[0].height() * width) / _sizes[0].width();

	return {
		{
			QRect(0, 0, width, height),
			RectPart::Left | RectPart::Top | RectPart::Right | RectPart::Bottom
		},
	};
}

std::vector<GroupMediaLayout> Layouter::layoutTwoTopBottom() const {
	Expects(_count == 2);

	const auto width = _maxWidth;
	const auto height = Round(std::min(
		width / _ratios[0],
		std::min(
			width / _ratios[1],
			(_maxHeight - _spacing) / 2.)));

	return {
		{
			QRect(0, 0, width, height),
			RectPart::Left | RectPart::Top | RectPart::Right
		},
		{
			QRect(0, height + _spacing, width, height),
			RectPart::Left | RectPart::Bottom | RectPart::Right
		},
	};
}

std::vector<GroupMediaLayout> Layouter::layoutTwoLeftRightEqual() const {
	Expects(_count == 2);

	const auto width = (_maxWidth - _spacing) / 2;
	const auto height = Round(std::min(
		width / _ratios[0],
		std::min(width / _ratios[1], _maxHeight * 1.)));

	return {
		{
			QRect(0, 0, width, height),
			RectPart::Top | RectPart::Left | RectPart::Bottom
		},
		{
			QRect(width + _spacing, 0, width, height),
			RectPart::Top | RectPart::Right | RectPart::Bottom
		},
	};
}

std::vector<GroupMediaLayout> Layouter::layoutTwoLeftRight() const {
	Expects(_count == 2);

	const auto minimalWidth = Round(_minWidth * 1.5);
	const auto secondWidth = std::min(
		Round(std::max(
			0.4 * (_maxWidth - _spacing),
			(_maxWidth - _spacing) / _ratios[0]
				/ (1. / _ratios[0] + 1. / _ratios[1]))),
		_maxWidth - _spacing - minimalWidth);
	const auto firstWidth = _maxWidth
		- secondWidth
		- _spacing;
	const auto height = std::min(
		_maxHeight,
		Round(std::min(
			firstWidth / _ratios[0],
			secondWidth / _ratios[1])));

	return {
		{
			QRect(0, 0, firstWidth, height),
			RectPart::Top | RectPart::Left | RectPart::Bottom
		},
		{
			QRect(firstWidth + _spacing, 0, secondWidth, height),
			RectPart::Top | RectPart::Right | RectPart::Bottom
		},
	};
}

std::vector<GroupMediaLayout> Layouter::layoutThreeLeftAndOther() const {
	Expects(_count == 3);

	const auto firstHeight = _maxHeight;
	const auto thirdHeight = Round(std::min(
		(_maxHeight - _spacing) / 2.,
		(_ratios[1] * (_maxWidth - _spacing)
			/ (_ratios[2] + _ratios[1]))));
	const auto secondHeight = firstHeight
		- thirdHeight
		- _spacing;
	const auto rightWidth = std::max(
		_minWidth,
		Round(std::min(
			(_maxWidth - _spacing) / 2.,
			std::min(
				thirdHeight * _ratios[2],
				secondHeight * _ratios[1]))));
	const auto leftWidth = std::min(
		Round(firstHeight * _ratios[0]),
		_maxWidth - _spacing - rightWidth);

	return {
		{
			QRect(0, 0, leftWidth, firstHeight),
			RectPart::Top | RectPart::Left | RectPart::Bottom
		},
		{
			QRect(leftWidth + _spacing, 0, rightWidth, secondHeight),
			RectPart::Top | RectPart::Right
		},
		{
			QRect(leftWidth + _spacing, secondHeight + _spacing, rightWidth, thirdHeight),
			RectPart::Bottom | RectPart::Right
		},
	};
}

std::vector<GroupMediaLayout> Layouter::layoutThreeTopAndOther() const {
	Expects(_count == 3);

	const auto firstWidth = _maxWidth;
	const auto firstHeight = Round(std::min(
		firstWidth / _ratios[0],
		(_maxHeight - _spacing) * 0.66));
	const auto secondWidth = (_maxWidth - _spacing) / 2;
	const auto secondHeight = std::min(
		_maxHeight - firstHeight - _spacing,
		Round(std::min(
			secondWidth / _ratios[1],
			secondWidth / _ratios[2])));
	const auto thirdWidth = firstWidth - secondWidth - _spacing;

	return {
		{
			QRect(0, 0, firstWidth, firstHeight),
			RectPart::Left | RectPart::Top | RectPart::Right
		},
		{
			QRect(0, firstHeight + _spacing, secondWidth, secondHeight),
			RectPart::Bottom | RectPart::Left
		},
		{
			QRect(secondWidth + _spacing, firstHeight + _spacing, thirdWidth, secondHeight),
			RectPart::Bottom | RectPart::Right
		},
	};
}

std::vector<GroupMediaLayout> Layouter::layoutFourTopAndOther() const {
	Expects(_count == 4);

	const auto w = _maxWidth;
	const auto h0 = Round(std::min(
		w / _ratios[0],
		(_maxHeight - _spacing) * 0.66));
	const auto h = Round(
		(_maxWidth - 2 * _spacing)
			/ (_ratios[1] + _ratios[2] + _ratios[3]));
	const auto w0 = std::max(
		_minWidth,
		Round(std::min(
			(_maxWidth - 2 * _spacing) * 0.4,
			h * _ratios[1])));
	const auto w2 = Round(std::max(
		std::max(
			_minWidth * 1.,
			(_maxWidth - 2 * _spacing) * 0.33),
		h * _ratios[3]));
	const auto w1 = w - w0 - w2 - 2 * _spacing;
	const auto h1 = std::min(
		_maxHeight - h0 - _spacing,
		h);

	return {
		{
			QRect(0, 0, w, h0),
			RectPart::Left | RectPart::Top | RectPart::Right
		},
		{
			QRect(0, h0 + _spacing, w0, h1),
			RectPart::Bottom | RectPart::Left
		},
		{
			QRect(w0 + _spacing, h0 + _spacing, w1, h1),
			RectPart::Bottom,
		},
		{
			QRect(w0 + _spacing + w1 + _spacing, h0 + _spacing, w2, h1),
			RectPart::Right | RectPart::Bottom
		},
	};
}

std::vector<GroupMediaLayout> Layouter::layoutFourLeftAndOther() const {
	Expects(_count == 4);

	const auto h = _maxHeight;
	const auto w0 = Round(std::min(
		h * _ratios[0],
		(_maxWidth - _spacing) * 0.6));

	const auto w = Round(
		(_maxHeight - 2 * _spacing)
			/ (1. / _ratios[1] + 1. / _ratios[2] + 1. / _ratios[3])
	);
	const auto h0 = Round(w / _ratios[1]);
	const auto h1 = Round(w / _ratios[2]);
	const auto h2 = h - h0 - h1 - 2 * _spacing;
	const auto w1 = std::max(
		_minWidth,
		std::min(_maxWidth - w0 - _spacing, w));

	return {
		{
			QRect(0, 0, w0, h),
			RectPart::Top | RectPart::Left | RectPart::Bottom
		},
		{
			QRect(w0 + _spacing, 0, w1, h0),
			RectPart::Top | RectPart::Right
		},
		{
			QRect(w0 + _spacing, h0 + _spacing, w1, h1),
			RectPart::Right
		},
		{
			QRect(w0 + _spacing, h0 + h1 + 2 * _spacing, w1, h2),
			RectPart::Bottom | RectPart::Right
		},
	};
}

ComplexLayouter::ComplexLayouter(
	const std::vector<float64> &ratios,
	float64 averageRatio,
	int maxWidth,
	int minWidth,
	int spacing)
: _ratios(CropRatios(ratios, averageRatio))
, _count(int(_ratios.size()))
// All apps currently use square max size first.
// In complex case they use maxWidth * 4 / 3 as maxHeight.
, _maxWidth(maxWidth)
, _maxHeight(maxWidth * 4 / 3)
, _minWidth(minWidth)
, _spacing(spacing)
, _averageRatio(averageRatio) {
}

std::vector<float64> ComplexLayouter::CropRatios(
		const std::vector<float64> &ratios,
		float64 averageRatio) {
	return ranges::views::all(
		ratios
	) | ranges::views::transform([&](float64 ratio) {
		constexpr auto kMaxRatio = 2.75;
		constexpr auto kMinRatio = 0.6667;
		return (averageRatio > 1.1)
			? std::clamp(ratio, 1., kMaxRatio)
			: std::clamp(ratio, kMinRatio, 1.);
	}) | ranges::to_vector;
}

std::vector<GroupMediaLayout> ComplexLayouter::layout() const {
	Expects(_count > 1);

	auto result = std::vector<GroupMediaLayout>(_count);

	auto attempts = std::vector<Attempt>();
	const auto multiHeight = [&](int offset, int count) {
		const auto ratios = gsl::make_span(_ratios).subspan(offset, count);
		const auto sum = ranges::accumulate(ratios, 0.);
		return (_maxWidth - (count - 1) * _spacing) / sum;
	};
	const auto pushAttempt = [&](std::vector<int> lineCounts) {
		auto heights = std::vector<float64>();
		heights.reserve(lineCounts.size());
		auto offset = 0;
		for (auto count : lineCounts) {
			heights.push_back(multiHeight(offset, count));
			offset += count;
		}
		attempts.push_back({ std::move(lineCounts), std::move(heights) });
	};

	for (auto first = 1; first != _count; ++first) {
		const auto second = _count - first;
		if (first > 3 || second > 3) {
			continue;
		}
		pushAttempt({ first, second });
	}
	for (auto first = 1; first != _count - 1; ++first) {
		for (auto second = 1; second != _count - first; ++second) {
			const auto third = _count - first - second;
			if ((first > 3)
				|| (second > ((_averageRatio < 0.85) ? 4 : 3))
				|| (third > 3)) {
				continue;
			}
			pushAttempt({ first, second, third });
		}
	}
	for (auto first = 1; first != _count - 1; ++first) {
		for (auto second = 1; second != _count - first; ++second) {
			for (auto third = 1; third != _count - first - second; ++third) {
				const auto fourth = _count - first - second - third;
				if (first > 3 || second > 3 || third > 3 || fourth > 3) {
					continue;
				}
				pushAttempt({ first, second, third, fourth });
			}
		}
	}

	auto optimalAttempt = (const Attempt*)nullptr;
	auto optimalDiff = 0.;
	for (const auto &attempt : attempts) {
		const auto &heights = attempt.heights;
		const auto &counts = attempt.lineCounts;
		const auto lineCount = int(counts.size());
		const auto totalHeight = ranges::accumulate(heights, 0.)
			+ _spacing * (lineCount - 1);
		const auto minLineHeight = ranges::min(heights);
		const auto bad1 = (minLineHeight < _minWidth) ? 1.5 : 1.;
		const auto bad2 = [&] {
			for (auto line = 1; line != lineCount; ++line) {
				if (counts[line - 1] > counts[line]) {
					return 1.5;
				}
			}
			return 1.;
		}();
		const auto diff = std::abs(totalHeight - _maxHeight) * bad1 * bad2;
		if (!optimalAttempt || diff < optimalDiff) {
			optimalAttempt = &attempt;
			optimalDiff = diff;
		}
	}
	Assert(optimalAttempt != nullptr);

	const auto &optimalCounts = optimalAttempt->lineCounts;
	const auto &optimalHeights = optimalAttempt->heights;
	const auto rowCount = int(optimalCounts.size());

	auto index = 0;
	auto y = 0.;
	for (auto row = 0; row != rowCount; ++row) {
		const auto colCount = optimalCounts[row];
		const auto lineHeight = optimalHeights[row];
		const auto height = Round(lineHeight);

		auto x = 0;
		for (auto col = 0; col != colCount; ++col) {
			const auto sides = RectPart::None
				| (row == 0 ? RectPart::Top : RectPart::None)
				| (row == rowCount - 1 ? RectPart::Bottom : RectPart::None)
				| (col == 0 ? RectPart::Left : RectPart::None)
				| (col == colCount - 1 ? RectPart::Right : RectPart::None);

			const auto ratio = _ratios[index];
			const auto width = (col == colCount - 1)
				? (_maxWidth - x)
				: Round(ratio * lineHeight);
			result[index] = {
				QRect(x, y, width, height),
				sides
			};

			x += width + _spacing;
			++index;
		}
		y += height + _spacing;
	}

	return result;
}

} // namespace

std::vector<GroupMediaLayout> LayoutMediaGroup(
		const std::vector<QSize> &sizes,
		int maxWidth,
		int minWidth,
		int spacing) {
	return Layouter(sizes, maxWidth, minWidth, spacing).layout();
}

RectParts GetCornersFromSides(RectParts sides) {
	const auto convert = [&](
			RectPart side1,
			RectPart side2,
			RectPart corner) {
		return ((sides & side1) && (sides & side2))
			? corner
			: RectPart::None;
	};
	return RectPart::None
		| convert(RectPart::Top, RectPart::Left, RectPart::TopLeft)
		| convert(RectPart::Top, RectPart::Right, RectPart::TopRight)
		| convert(RectPart::Bottom, RectPart::Left, RectPart::BottomLeft)
		| convert(RectPart::Bottom, RectPart::Right, RectPart::BottomRight);
}

QSize GetImageScaleSizeForGeometry(QSize original, QSize geometry) {
	const auto width = geometry.width();
	const auto height = geometry.height();
	auto tw = original.width();
	auto th = original.height();
	if (tw * height > th * width) {
		if (th > height || tw * height < 2 * th * width) {
			tw = (height * tw) / th;
			th = height;
		} else if (tw < width) {
			th = (width * th) / tw;
			tw = width;
		}
	} else {
		if (tw > width || th * width < 2 * tw * height) {
			th = (width * th) / tw;
			tw = width;
		} else if (tw > 0 && th < height) {
			tw = (height * tw) / th;
			th = height;
		}
	}
	if (tw < 1) tw = 1;
	if (th < 1) th = 1;
	return { tw, th };
}

} // namespace Ui
