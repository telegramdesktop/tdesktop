/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/effects/numbers_animation.h"
#include "ui/rp_widget.h"

enum lngtag_count : int;

namespace tr {
template <typename ...Tags>
struct phrase;
} // namespace tr

namespace style {
struct PremiumBubble;
} // namespace style

namespace Ui {
class VerticalLayout;
} // namespace Ui

namespace Ui::Premium {

using TextFactory = Fn<QString(int)>;

[[nodiscard]] TextFactory ProcessTextFactory(
	std::optional<tr::phrase<lngtag_count>> phrase);

class Bubble final {
public:
	using EdgeProgress = float64;

	Bubble(
		const style::PremiumBubble &st,
		Fn<void()> updateCallback,
		TextFactory textFactory,
		const style::icon *icon,
		bool hasTail);

	[[nodiscard]] static crl::time SlideNoDeflectionDuration();

	[[nodiscard]] int counter() const;
	[[nodiscard]] int height() const;
	[[nodiscard]] int width() const;
	[[nodiscard]] int bubbleRadius() const;
	[[nodiscard]] int countMaxWidth(int maxPossibleCounter) const;

	void setCounter(int value);
	void setTailEdge(EdgeProgress edge);
	void setFlipHorizontal(bool value);
	void paintBubble(QPainter &p, const QRect &r, const QBrush &brush);

	[[nodiscard]] rpl::producer<> widthChanges() const;

private:
	[[nodiscard]] int filledWidth() const;

	const style::PremiumBubble &_st;

	const Fn<void()> _updateCallback;
	const TextFactory _textFactory;

	const style::icon *_icon;
	NumbersAnimation _numberAnimation;
	const int _height;
	const int _textTop;
	const bool _hasTail;

	int _counter = -1;
	EdgeProgress _tailEdge = 0.;
	bool _flipHorizontal = false;

	rpl::event_stream<> _widthChanges;

};

struct BubbleRowState {
	int counter = 0;
	float64 ratio = 0.;
	bool animateFromZero = false;
	bool dynamic = false;
};

enum class BubbleType : uchar {
	NoPremium,
	Premium,
	Credits,
};

class BubbleWidget final : public Ui::RpWidget {
public:
	BubbleWidget(
		not_null<Ui::RpWidget*> parent,
		const style::PremiumBubble &st,
		TextFactory textFactory,
		rpl::producer<BubbleRowState> state,
		BubbleType type,
		rpl::producer<> showFinishes,
		const style::icon *icon,
		const style::margins &outerPadding);

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	struct GradientParams {
		int left = 0;
		int width = 0;
		int outer = 0;

		friend inline constexpr bool operator==(
			GradientParams,
			GradientParams) = default;
	};
	void animateTo(BubbleRowState state);

	const style::PremiumBubble &_st;
	BubbleRowState _animatingFrom;
	float64 _animatingFromResultRatio = 0.;
	float64 _animatingFromBubbleEdge = 0.;
	rpl::variable<BubbleRowState> _state;
	Bubble _bubble;
	int _maxBubbleWidth = 0;
	const BubbleType _type;
	const style::margins _outerPadding;

	Ui::Animations::Simple _appearanceAnimation;
	QSize _spaceForDeflection;

	QLinearGradient _cachedGradient;
	std::optional<GradientParams> _cachedGradientParams;

	float64 _deflection;

	bool _ignoreDeflection = false;
	float64 _stepBeforeDeflection;
	float64 _stepAfterDeflection;

};

void AddBubbleRow(
	not_null<Ui::VerticalLayout*> parent,
	const style::PremiumBubble &st,
	rpl::producer<> showFinishes,
	int min,
	int current,
	int max,
	BubbleType type,
	std::optional<tr::phrase<lngtag_count>> phrase,
	const style::icon *icon);

void AddBubbleRow(
	not_null<Ui::VerticalLayout*> parent,
	const style::PremiumBubble &st,
	rpl::producer<> showFinishes,
	rpl::producer<BubbleRowState> state,
	BubbleType type,
	Fn<QString(int)> text,
	const style::icon *icon,
	const style::margins &outerPadding);

} // namespace Ui::Premium
