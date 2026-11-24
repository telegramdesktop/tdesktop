/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/effects/ministar_particles.h"
#include "ui/effects/numbers_animation.h"
#include "ui/abstract_button.h"
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

struct BubbleText {
	QString counter;
	QString additional;
};

using TextFactory = Fn<BubbleText(int)>;

[[nodiscard]] TextFactory ProcessTextFactory(
	std::optional<tr::phrase<lngtag_count>> phrase);

inline constexpr auto kLimitSlideDuration = crl::time(500);

[[nodiscard]] float64 LimitSlideProgress(float64 delta, float64 progress);

class Bubble final {
public:
	Bubble(
		const style::PremiumBubble &st,
		Fn<void()> updateCallback,
		TextFactory textFactory,
		const style::icon *icon,
		bool hasTail);

	[[nodiscard]] std::optional<int> counter() const;
	[[nodiscard]] int height() const;
	[[nodiscard]] int width() const;
	[[nodiscard]] int widthForCounter(int value) const;

	void setCounter(int value, crl::time duration);
	void setTailPosition(float64 position);
	void paintBubble(QPainter &p, const QRect &r, const QBrush &brush);
	[[nodiscard]] QPainterPath bubblePath(const QRect &r) const;
	void setSubtext(QString subtext);

	[[nodiscard]] rpl::producer<> widthChanges() const;

private:
	[[nodiscard]] int filledWidth() const;
	[[nodiscard]] int topTextWidth() const;
	[[nodiscard]] int bottomTextWidth() const;

	const style::PremiumBubble &_st;

	const Fn<void()> _updateCallback;
	const TextFactory _textFactory;

	const style::icon *_icon;
	NumbersAnimation _numberAnimation;
	Text::String _additional;
	Text::String _subtext;
	const int _height;
	const bool _hasTail;

	std::optional<int> _counter;
	float64 _tailPosition = 0.5;

	rpl::event_stream<> _widthChanges;

};

struct BubbleRowState {
	int counter = 0;
	float64 ratio = 0.;
	bool animateFromZero = false;
	bool dynamic = false;
};

enum class BubbleType : uchar {
	UpgradePrice,
	StarRating,
	NegativeRating,
	NoPremium,
	Premium,
	Credits,
};

class BubbleWidget final : public Ui::AbstractButton {
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

	void setBrushOverride(std::optional<QBrush> brushOverride);
	void setSubtext(QString subtext);

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	void setupParticles(not_null<Ui::RpWidget*> parent);
	void paintParticles();

	struct GradientParams {
		int left = 0;
		int width = 0;
		int outer = 0;

		friend inline constexpr bool operator==(
			GradientParams,
			GradientParams) = default;
	};
	void animateTo(BubbleRowState state, bool instant);
	void resizeToBubble(int targetWidth = 0);
	void refreshPosition();
	void updatePosition();
	[[nodiscard]] float64 anchorForRatio(float64 ratio) const;
	[[nodiscard]] QRect bubbleRect() const;
	[[nodiscard]] QTransform bubbleTransform() const;

	const style::PremiumBubble &_st;
	rpl::variable<BubbleRowState> _state;
	Bubble _bubble;
	std::optional<QBrush> _brushOverride;
	const BubbleType _type;
	const style::margins _outerPadding;

	Ui::Animations::Simple _appearanceAnimation;
	QMargins _bubblePadding;
	float64 _ratio = 0.;
	float64 _anchor = 0.5;
	float64 _tailPosition = 0.5;
	float64 _rotation = 0.;
	float64 _scale = 1.;
	float64 _opacity = 1.;
	float64 _positionOffset = 0.;

	QLinearGradient _cachedGradient;
	std::optional<GradientParams> _cachedGradientParams;

	RpWidget *_particlesWidget = nullptr;
	std::optional<StarParticles> _particles;
	Ui::Animations::Basic _particlesAnimation;

};

not_null<BubbleWidget*> AddBubbleRow(
	not_null<Ui::VerticalLayout*> parent,
	const style::PremiumBubble &st,
	rpl::producer<> showFinishes,
	int min,
	int current,
	int max,
	BubbleType type,
	std::optional<tr::phrase<lngtag_count>> phrase,
	const style::icon *icon);

not_null<BubbleWidget*> AddBubbleRow(
	not_null<Ui::VerticalLayout*> parent,
	const style::PremiumBubble &st,
	rpl::producer<> showFinishes,
	rpl::producer<BubbleRowState> state,
	BubbleType type,
	TextFactory text,
	const style::icon *icon,
	const style::margins &outerPadding);

} // namespace Ui::Premium
