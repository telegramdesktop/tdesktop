/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/controls/call_mute_button.h"

#include "base/flat_map.h"
#include "ui/abstract_button.h"
#include "ui/effects/shake_animation.h"
#include "ui/paint/blobs.h"
#include "ui/painter.h"
#include "ui/power_saving.h"
#include "ui/widgets/call_button.h"
#include "ui/widgets/labels.h"
#include "ui/ui_utility.h"
#include "base/random.h"
#include "styles/palette.h"
#include "styles/style_widgets.h"
#include "styles/style_calls.h"

#include <QtCore/QtMath>
#include <QtCore/QCoreApplication>

namespace Ui {
namespace {

using Radiuses = Paint::Blob::Radiuses;

constexpr auto kMaxLevel = 1.;

constexpr auto kLevelDuration = 100. + 500. * 0.33;

constexpr auto kScaleBig = 0.807 - 0.1;
constexpr auto kScaleSmall = 0.704 - 0.1;

constexpr auto kScaleBigMin = 0.878;
constexpr auto kScaleSmallMin = 0.926;

constexpr auto kScaleBigMax = (float)(kScaleBigMin + kScaleBig);
constexpr auto kScaleSmallMax = (float)(kScaleSmallMin + kScaleSmall);

constexpr auto kMainRadiusFactor = (float)(50. / 57.);

constexpr auto kGlowPaddingFactor = 1.2;
constexpr auto kGlowMinScale = 0.6;
constexpr auto kGlowAlpha = 150;

constexpr auto kOverrideColorBgAlpha = 76;
constexpr auto kOverrideColorRippleAlpha = 50;

constexpr auto kSwitchStateDuration = crl::time(120);
constexpr auto kSwitchLabelDuration = crl::time(180);

// Switch state from Connecting animation.
constexpr auto kSwitchRadialDuration = crl::time(350);
constexpr auto kSwitchCirclelDuration = crl::time(275);
constexpr auto kBlobsScaleEnterDuration = crl::time(400);
constexpr auto kSwitchStateFromConnectingDuration = kSwitchRadialDuration
	+ kSwitchCirclelDuration
	+ kBlobsScaleEnterDuration;

constexpr auto kRadialEndPartAnimation = float(kSwitchRadialDuration)
	/ kSwitchStateFromConnectingDuration;
constexpr auto kBlobsWidgetPartAnimation = 1. - kRadialEndPartAnimation;
constexpr auto kFillCirclePartAnimation = float(kSwitchCirclelDuration)
	/ (kSwitchCirclelDuration + kBlobsScaleEnterDuration);
constexpr auto kBlobPartAnimation = float(kBlobsScaleEnterDuration)
	/ (kSwitchCirclelDuration + kBlobsScaleEnterDuration);

constexpr auto kOverlapProgressRadialHide = 1.2;

constexpr auto kRadialFinishArcShift = 1200;

[[nodiscard]] CallMuteButtonType TypeForIcon(CallMuteButtonType type) {
	return (type == CallMuteButtonType::Connecting
		|| type == CallMuteButtonType::ConferenceForceMuted)
		? CallMuteButtonType::Muted
		: (type == CallMuteButtonType::RaisedHand)
		? CallMuteButtonType::ForceMuted
		: type;
};

[[nodiscard]] QSize AdjustedLottieSize(
		not_null<const style::CallMuteButton*> st) {
	const auto &button = st->active.button;
	const auto left = (button.width - st->lottieSize.width()) / 2;
	const auto size = button.width - 2 * left;
	return QSize(size, size);
}

[[nodiscard]] int AdjustedBgSize(
		not_null<const style::CallMuteButton*> st) {
	const auto &button = st->active.button;
	const auto left = (button.width - st->active.bgSize) / 2;
	return button.width - 2 * left;
}

[[nodiscard]] int AdjustedBgSkip(
		not_null<const style::CallMuteButton*> st) {
	const auto &button = st->active.button;
	const auto bgSize = AdjustedBgSize(st);
	return (button.width - bgSize) / 2;
}

auto MuteBlobs() {
	return std::vector<Paint::Blobs::BlobData>{
		{
			.segmentsCount = 9,
			.minScale = kScaleSmallMin / kScaleSmallMax,
			.minRadius = st::callMuteMinorBlobMinRadius
				* kScaleSmallMax
				* kMainRadiusFactor,
			.maxRadius = st::callMuteMinorBlobMaxRadius
				* kScaleSmallMax
				* kMainRadiusFactor,
			.speedScale = 1.,
			.alpha = (76. / 255.),
		},
		{
			.segmentsCount = 12,
			.minScale = kScaleBigMin / kScaleBigMax,
			.minRadius = st::callMuteMajorBlobMinRadius
				* kScaleBigMax
				* kMainRadiusFactor,
			.maxRadius = st::callMuteMajorBlobMaxRadius
				* kScaleBigMax
				* kMainRadiusFactor,
			.speedScale = 1.,
			.alpha = (76. / 255.),
		},
	};
}

auto Colors() {
	using Vector = std::vector<QColor>;
	using Colors = anim::gradient_colors;
	auto result = base::flat_map<CallMuteButtonType, Colors>{
		{
			CallMuteButtonType::Active,
			Colors(Vector{ st::groupCallLive1->c, st::groupCallLive2->c })
		},
		{
			CallMuteButtonType::Connecting,
			Colors(st::callIconBg->c)
		},
		{
			CallMuteButtonType::Muted,
			Colors(Vector{ st::groupCallMuted1->c, st::groupCallMuted2->c })
		},
	};
	const auto forceMutedColors = Colors(QGradientStops{
		{ .0, st::groupCallForceMuted3->c },
		{ .5, st::groupCallForceMuted2->c },
		{ 1., st::groupCallForceMuted1->c } });
	const auto forceMutedTypes = {
		CallMuteButtonType::ForceMuted,
		CallMuteButtonType::RaisedHand,
		CallMuteButtonType::ConferenceForceMuted,
		CallMuteButtonType::ScheduledCanStart,
		CallMuteButtonType::ScheduledNotify,
		CallMuteButtonType::ScheduledSilent,
	};
	for (const auto type : forceMutedTypes) {
		result.emplace(type, forceMutedColors);
	}
	return result;
}

bool IsConnecting(CallMuteButtonType type) {
	return (type == CallMuteButtonType::Connecting);
}

bool IsInactive(CallMuteButtonType type) {
	return IsConnecting(type);
}

auto Clamp(float64 value) {
	return std::clamp(value, 0., 1.);
}

void ComputeRadialFinish(
		int &value,
		float64 progress,
		int to = -RadialState::kFull) {
	value = anim::interpolate(value, to, Clamp(progress));
}

} // namespace

class AnimatedLabel final : public RpWidget {
public:
	AnimatedLabel(
		QWidget *parent,
		rpl::producer<QString> &&text,
		crl::time duration,
		int additionalHeight,
		const style::FlatLabel &st = st::defaultFlatLabel);

	int contentHeight() const;

private:
	void setText(const QString &text);

	const style::FlatLabel &_st;
	const crl::time _duration;
	const int _additionalHeight;
	const TextParseOptions _options;

	Text::String _text;
	Text::String _previousText;

	Animations::Simple _animation;

};

AnimatedLabel::AnimatedLabel(
	QWidget *parent,
	rpl::producer<QString> &&text,
	crl::time duration,
	int additionalHeight,
	const style::FlatLabel &st)
: RpWidget(parent)
, _st(st)
, _duration(duration)
, _additionalHeight(additionalHeight)
, _options({ 0, 0, 0, Qt::LayoutDirectionAuto }) {
	std::move(
		text
	) | rpl::start_with_next([=](const QString &value) {
		setText(value);
	}, lifetime());

	paintRequest(
	) | rpl::start_with_next([=] {
		Painter p(this);
		const auto progress = _animation.value(1.);

		p.setFont(_st.style.font);
		p.setPen(_st.textFg);
		p.setTextPalette(_st.palette);

		const auto textHeight = contentHeight();
		const auto diffHeight = height() - textHeight;
		const auto center = diffHeight / 2;

		p.setOpacity(1. - progress);
		if (p.opacity()) {
			_previousText.draw(
				p,
				0,
				anim::interpolate(center, diffHeight, progress),
				width(),
				style::al_center);
		}

		p.setOpacity(progress);
		if (p.opacity()) {
			_text.draw(
				p,
				0,
				anim::interpolate(0, center, progress),
				width(),
				style::al_center);
		}
	}, lifetime());
}

int AnimatedLabel::contentHeight() const {
	return _st.style.font->height;
}

void AnimatedLabel::setText(const QString &text) {
	if (_text.toString() == text) {
		return;
	}
	_previousText = std::move(_text);
	_text = Ui::Text::String(_st.style, text, _options);

	const auto width = std::max(
		_st.style.font->width(_text.toString()),
		_st.style.font->width(_previousText.toString()));
	resize(
		width + _additionalHeight,
		contentHeight() + _additionalHeight * 2);

	_animation.stop();
	_animation.start([=] { update(); }, 0., 1., _duration);
}

class BlobsWidget final : public RpWidget {
public:
	BlobsWidget(
		not_null<RpWidget*> parent,
		int diameter,
		rpl::producer<bool> &&hideBlobs);

	void setDiameter(int diameter);
	void setLevel(float level);
	void setBlobBrush(QBrush brush);
	void setGlowBrush(QBrush brush);

	[[nodiscard]] QRectF innerRect() const;

	[[nodiscard]] float64 switchConnectingProgress() const;
	void setSwitchConnectingProgress(float64 progress);

private:
	void init(int diameter);
	void computeCircleRect();

	Paint::Blobs _blobs;

	float _circleRadius = 0.;
	QBrush _blobBrush;
	QBrush _glowBrush;
	int _center = 0;
	QRectF _circleRect;

	float64 _switchConnectingProgress = 0.;

	crl::time _blobsLastTime = 0;
	crl::time _blobsHideLastTime = 0;

	float64 _blobsScaleEnter = 0.;
	crl::time _blobsScaleLastTime = 0;

	bool _hideBlobs = true;

	Animations::Basic _animation;

};

BlobsWidget::BlobsWidget(
	not_null<RpWidget*> parent,
	int diameter,
	rpl::producer<bool> &&hideBlobs)
: RpWidget(parent)
, _blobs(MuteBlobs(), kLevelDuration, kMaxLevel)
, _blobBrush(Qt::transparent)
, _glowBrush(Qt::transparent)
, _blobsLastTime(crl::now())
, _blobsScaleLastTime(crl::now()) {
	init(diameter);

	std::move(
		hideBlobs
	) | rpl::start_with_next([=](bool hide) {
		if (_hideBlobs != hide) {
			const auto now = crl::now();
			if ((now - _blobsScaleLastTime) >= kBlobsScaleEnterDuration) {
				_blobsScaleLastTime = now;
			}
			_hideBlobs = hide;
		}
		if (hide) {
			setLevel(0.);
		}
		_blobsHideLastTime = hide ? crl::now() : 0;
		if (!hide && !_animation.animating()) {
			_animation.start();
		}
	}, lifetime());
}

void BlobsWidget::setDiameter(int diameter) {
	_circleRadius = diameter / 2.;
	const auto defaultSize = _blobs.maxRadius() * 2 * kGlowPaddingFactor;
	const auto s = int(std::ceil((defaultSize * diameter)
		/ float64(st::callMuteBlobRadiusForDiameter)));
	const auto size = QSize{ s, s };
	if (this->size() != size) {
		resize(size);
	}
	computeCircleRect();
}

void BlobsWidget::computeCircleRect() {
	const auto &r = _circleRadius;
	const auto left = (size().width() - r * 2.) / 2.;
	const auto add = st::callConnectingRadial.thickness / 2;
	_circleRect = QRectF(left, left, r * 2, r * 2).marginsAdded(
		style::margins(add, add, add, add));
}

void BlobsWidget::init(int diameter) {
	setAttribute(Qt::WA_TransparentForMouseEvents);

	const auto cutRect = [](Painter &p, const QRectF &r) {
		p.save();
		p.setOpacity(1.);
		p.setBrush(st::groupCallBg);
		p.setCompositionMode(QPainter::CompositionMode_Source);
		p.drawEllipse(r);
		p.restore();
	};

	setDiameter(diameter);

	sizeValue(
	) | rpl::start_with_next([=](QSize size) {
		_center = size.width() / 2;
		computeCircleRect();
	}, lifetime());

	paintRequest(
	) | rpl::start_with_next([=] {
		Painter p(this);
		PainterHighQualityEnabler hq(p);

		p.setPen(Qt::NoPen);

		// Glow.
		const auto s = kGlowMinScale
			+ (1. - kGlowMinScale) * _blobs.currentLevel();
		p.translate(_center, _center);
		p.scale(s, s);
		p.translate(-_center, -_center);
		p.fillRect(rect(), _glowBrush);
		p.resetTransform();

		// Blobs.
		p.translate(_center, _center);
		const auto scale = (_switchConnectingProgress > 0.)
			? anim::easeOutBack(
				1.,
				_blobsScaleEnter * (1. - Clamp(
					_switchConnectingProgress / kBlobPartAnimation)))
			: _blobsScaleEnter;
		const auto sizeScale = (2. * _circleRadius)
			/ st::callMuteBlobRadiusForDiameter;
		_blobs.paint(p, _blobBrush, scale * sizeScale);
		p.translate(-_center, -_center);

		if (scale < 1.) {
			cutRect(p, _circleRect);
		}

		// Main circle.
		const auto circleProgress
			= Clamp(_switchConnectingProgress - kBlobPartAnimation)
				/ kFillCirclePartAnimation;
		const auto skipColoredCircle = (circleProgress == 1.);

		if (!skipColoredCircle) {
			p.setBrush(_blobBrush);
			p.drawEllipse(_circleRect);
		}

		if (_switchConnectingProgress > 0.) {
			p.resetTransform();

			const auto mF = (_circleRect.width() / 2) * (1. - circleProgress);
			const auto cutOutRect = _circleRect.marginsRemoved(
				QMarginsF(mF, mF, mF, mF));

			if (!skipColoredCircle) {
				p.setBrush(st::callConnectingRadial.color);
				p.setOpacity(circleProgress);
				p.drawEllipse(_circleRect);
			}

			p.setOpacity(1.);

			cutRect(p, cutOutRect);

			p.setBrush(st::callIconBg);
			p.drawEllipse(cutOutRect);
		}
	}, lifetime());

	_animation.init([=](crl::time now) {
		if (const auto &last = _blobsHideLastTime; (last > 0)
			&& (now - last >= kBlobsScaleEnterDuration)) {
			_animation.stop();
			return false;
		}
		_blobs.updateLevel(now - _blobsLastTime);
		_blobsLastTime = now;

		const auto dt = Clamp(
			(now - _blobsScaleLastTime) / float64(kBlobsScaleEnterDuration));
		_blobsScaleEnter = _hideBlobs
			? (1. - anim::easeInCirc(1., dt))
			: anim::easeOutBack(1., dt);

		update();
		return true;
	});
	shownValue(
	) | rpl::start_with_next([=](bool shown) {
		if (shown) {
			_animation.start();
		} else {
			_animation.stop();
		}
	}, lifetime());
}

QRectF BlobsWidget::innerRect() const {
	return _circleRect;
}

void BlobsWidget::setBlobBrush(QBrush brush) {
	if (_blobBrush == brush) {
		return;
	}
	_blobBrush = brush;
}

void BlobsWidget::setGlowBrush(QBrush brush) {
	if (_glowBrush == brush) {
		return;
	}
	_glowBrush = brush;
}

void BlobsWidget::setLevel(float level) {
	if (_blobsHideLastTime) {
		 return;
	}
	_blobs.setLevel(level);
}

float64 BlobsWidget::switchConnectingProgress() const {
	return _switchConnectingProgress;
}

void BlobsWidget::setSwitchConnectingProgress(float64 progress) {
	_switchConnectingProgress = progress;
}

CallMuteButton::CallMuteButton(
	not_null<RpWidget*> parent,
	const style::CallMuteButton &st,
	rpl::producer<bool> &&hideBlobs,
	CallMuteButtonState initial)
: _state(initial)
, _st(&st)
, _lottieSize(AdjustedLottieSize(_st))
, _bgSize(AdjustedBgSize(_st))
, _bgSkip(AdjustedBgSkip(_st))
, _blobs(base::make_unique_q<BlobsWidget>(
	parent,
	_bgSize,
	rpl::combine(
		PowerSaving::OnValue(PowerSaving::kCalls),
		std::move(hideBlobs),
		_state.value(
		) | rpl::map([](const CallMuteButtonState &state) {
			return IsInactive(state.type);
		})
	) | rpl::map(rpl::mappers::_1 || rpl::mappers::_2 || rpl::mappers::_3)))
, _content(base::make_unique_q<AbstractButton>(parent))
, _colors(Colors())
, _iconState(iconStateFrom(initial.type)) {
	init();
}

void CallMuteButton::refreshLabels() {
	_centerLabel = base::make_unique_q<AnimatedLabel>(
		_content->parentWidget(),
		_state.value(
		) | rpl::map([](const CallMuteButtonState &state) {
			return state.subtext.isEmpty() ? state.text : QString();
		}),
		kSwitchLabelDuration,
		_st->labelAdditional,
		_st->active.label);
	_label = base::make_unique_q<AnimatedLabel>(
		_content->parentWidget(),
		_state.value(
		) | rpl::map([](const CallMuteButtonState &state) {
			return state.subtext.isEmpty() ? QString() : state.text;
		}),
		kSwitchLabelDuration,
		_st->labelAdditional,
		_st->active.label);
	_sublabel = base::make_unique_q<AnimatedLabel>(
		_content->parentWidget(),
		_state.value(
		) | rpl::map([](const CallMuteButtonState &state) {
			return state.subtext;
		}),
		kSwitchLabelDuration,
		_st->labelAdditional,
		_st->sublabel);

	_label->show();
	rpl::combine(
		_content->geometryValue(),
		_label->sizeValue()
	) | rpl::start_with_next([=](QRect my, QSize size) {
		updateLabelGeometry(my, size);
	}, _label->lifetime());
	_label->setAttribute(Qt::WA_TransparentForMouseEvents);

	_sublabel->show();
	rpl::combine(
		_content->geometryValue(),
		_sublabel->sizeValue()
	) | rpl::start_with_next([=](QRect my, QSize size) {
		updateSublabelGeometry(my, size);
	}, _sublabel->lifetime());
	_sublabel->setAttribute(Qt::WA_TransparentForMouseEvents);

	_centerLabel->show();
	rpl::combine(
		_content->geometryValue(),
		_centerLabel->sizeValue()
	) | rpl::start_with_next([=](QRect my, QSize size) {
		updateCenterLabelGeometry(my, size);
	}, _centerLabel->lifetime());
	_centerLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
}

void CallMuteButton::refreshIcons() {
	_icons[0].emplace(Lottie::IconDescriptor{
		.path = u":/icons/calls/voice.lottie"_q,
		.color = &st::groupCallIconFg,
		.sizeOverride = _lottieSize,
		.frame = (_iconState.index ? 0 : _iconState.frameTo),
	});
	_icons[1].emplace(Lottie::IconDescriptor{
		.path = u":/icons/calls/hands.lottie"_q,
		.color = &st::groupCallIconFg,
		.sizeOverride = _lottieSize,
		.frame = (_iconState.index ? _iconState.frameTo : 0),
	});

}

auto CallMuteButton::iconStateAnimated(CallMuteButtonType previous)
-> IconState {
	using Type = CallMuteButtonType;
	using Key = std::pair<Type, Type>;
	struct Animation {
		int from = 0;
		int to = 0;
	};
	static const auto kAnimations = std::vector<std::pair<Key, Animation>>{
		{ { Type::ForceMuted, Type::Muted }, { 0, 35 } },
		{ { Type::Muted, Type::Active }, { 36, 68 } },
		{ { Type::Active, Type::Muted }, { 69, 98 } },
		{ { Type::Muted, Type::ForceMuted }, { 99, 135 } },
		{ { Type::Active, Type::ForceMuted }, { 136, 172 } },
		{ { Type::ScheduledSilent, Type::ScheduledNotify }, { 173, 201 } },
		{ { Type::ScheduledSilent, Type::Muted }, { 202, 236 } },
		{ { Type::ScheduledSilent, Type::ForceMuted }, { 237, 273 } },
		{ { Type::ScheduledNotify, Type::ForceMuted }, { 274, 310 } },
		{ { Type::ScheduledNotify, Type::ScheduledSilent }, { 311, 343 } },
		{ { Type::ScheduledNotify, Type::Muted }, { 344, 375 } },
		{ { Type::ScheduledCanStart, Type::Muted }, { 376, 403 } },
	};
	static const auto kMap = [] {
		// flat_multi_map_pair_type lacks some required constructors :(
		auto &&list = kAnimations | ranges::views::transform([](auto &&pair) {
			return base::flat_multi_map_pair_type<Key, Animation>(
				pair.first,
				pair.second);
		});
		return base::flat_map<Key, Animation>(begin(list), end(list));
	}();
	const auto was = TypeForIcon(previous);
	const auto now = TypeForIcon(_state.current().type);
	if (was == now) {
		return {};
	}

	if (const auto i = kMap.find(Key{ was, now }); i != end(kMap)) {
		return { 0, i->second.from, i->second.to };
	}
	return {};
}

CallMuteButton::IconState CallMuteButton::iconStateFrom(
		CallMuteButtonType previous) {
	if (const auto animated = iconStateAnimated(previous)) {
		return animated;
	}

	using Type = CallMuteButtonType;
	static const auto kFinal = base::flat_map<Type, int>{
		{ Type::ForceMuted, 0 },
		{ Type::Muted, 36 },
		{ Type::Active, 69 },
		{ Type::ScheduledSilent, 173 },
		{ Type::ScheduledNotify, 274 },
		{ Type::ScheduledCanStart, 376 },
	};

	const auto now = TypeForIcon(_state.current().type);
	const auto i = kFinal.find(now);

	Ensures(i != end(kFinal));
	return { 0, i->second, i->second };
}

CallMuteButton::IconState CallMuteButton::randomWavingState() {
	struct Animation {
		int from = 0;
		int to = 0;
	};
	static const auto kAnimations = std::vector<Animation>{
		{ 0, 119 },
		{ 120, 239 },
		{ 240, 419 },
		{ 420, 539 },
	};
	const auto index = base::RandomIndex(kAnimations.size());
	return { 1, kAnimations[index].from, kAnimations[index].to };
}

void CallMuteButton::init() {
	refreshLabels();
	refreshIcons();

	const auto &button = _st->active.button;
	_content->resize(button.width, button.height);

	_content->events(
	) | rpl::start_with_next([=](not_null<QEvent*> e) {
		if (e->type() == QEvent::MouseMove) {
			if (!_state.current().tooltip.isEmpty()) {
				Ui::Tooltip::Show(1000, this);
			}
		} else if (e->type() == QEvent::Leave) {
			Ui::Tooltip::Hide();
		}
	}, _content->lifetime());

	rpl::combine(
		_radialInfo.rawShowProgress.value(),
		anim::Disables()
	) | rpl::start_with_next([=](float64 value, bool disabled) {
		auto &info = _radialInfo;
		info.realShowProgress = (1. - value) / kRadialEndPartAnimation;

		const auto guard = gsl::finally([&] {
			_content->update();
		});

		if (((value == 0.) || disabled) && _radial) {
			_radial->stop();
			_radial = nullptr;
			return;
		}
		if ((value > 0.) && !disabled && !_radial) {
			_radial = std::make_unique<InfiniteRadialAnimation>(
				[=] { _content->update(); },
				_radialInfo.st);
			_radial->start();
		}
		if ((info.realShowProgress < 1.) && !info.isDirectionToShow) {
			if (_radial) {
				_radial->stop(anim::type::instant);
				_radial->start();
			}
			info.state = std::nullopt;
			return;
		}

		if (value == 1.) {
			info.state = std::nullopt;
		} else {
			if (_radial && !info.state.has_value()) {
				info.state = _radial->computeState();
			}
		}
	}, lifetime());

	// State type.
	const auto previousType
		= lifetime().make_state<CallMuteButtonType>(_state.current().type);
	setHandleMouseState(HandleMouseState::Disabled);

	refreshGradients();

	_state.value(
	) | rpl::map([](const CallMuteButtonState &state) {
		return state.type;
	}) | rpl::start_with_next([=](CallMuteButtonType type) {
		const auto previous = *previousType;
		*previousType = type;

		const auto mouseState = HandleMouseStateFromType(type);
		setHandleMouseState(HandleMouseState::Disabled);
		if (mouseState != HandleMouseState::Enabled) {
			setHandleMouseState(mouseState);
		}

		const auto fromConnecting = IsConnecting(previous);
		const auto toConnecting = IsConnecting(type);

		const auto radialShowFrom = fromConnecting ? 1. : 0.;
		const auto radialShowTo = toConnecting ? 1. : 0.;

		const auto from = (_switchAnimation.animating() && !fromConnecting)
			? (1. - _switchAnimation.value(0.))
			: 0.;
		const auto to = 1.;

		_radialInfo.isDirectionToShow = fromConnecting;

		scheduleIconState(iconStateFrom(previous));

		auto callback = [=](float64 value) {
			const auto brushProgress = fromConnecting ? 1. : value;
			_blobs->setBlobBrush(QBrush(
				_linearGradients.gradient(previous, type, brushProgress)));
			_blobs->setGlowBrush(QBrush(
				_glowGradients.gradient(previous, type, value)));
			_blobs->update();

			const auto radialShowProgress = (radialShowFrom == radialShowTo)
				? radialShowTo
				: anim::interpolateToF(radialShowFrom, radialShowTo, value);
			if (radialShowProgress != _radialInfo.rawShowProgress.current()) {
				_radialInfo.rawShowProgress = radialShowProgress;
				_blobs->setSwitchConnectingProgress(Clamp(
					radialShowProgress / kBlobsWidgetPartAnimation));
			}

			overridesColors(previous, type, value);

			if (value == to) {
				setHandleMouseState(mouseState);
			}
		};

		_switchAnimation.stop();
		const auto duration = (1. - from) * ((fromConnecting || toConnecting)
			? kSwitchStateFromConnectingDuration
			: kSwitchStateDuration);
		_switchAnimation.start(std::move(callback), from, to, duration);
	}, lifetime());

	// Icon rect.
	_content->sizeValue(
	) | rpl::start_with_next([=](QSize size) {
		const auto icon = _lottieSize;
		_muteIconRect = QRect(
			(size.width() - icon.width()) / 2,
			_st->lottieTop,
			icon.width(),
			icon.height());
	}, lifetime());

	// Paint.
	_content->paintRequest(
	) | rpl::start_with_next([=](QRect clip) {
		Painter p(_content);

		const auto expand = _state.current().expandType;
		if (expand == CallMuteButtonExpandType::Expanded) {
			st::callMuteFromFullScreen.paintInCenter(p, _muteIconRect);
		} else if (expand == CallMuteButtonExpandType::Normal) {
			st::callMuteToFullScreen.paintInCenter(p, _muteIconRect);
		} else {
			_icons[_iconState.index]->paint(
				p,
				_muteIconRect.x(),
				_muteIconRect.y());
		}

		if (_radialInfo.state.has_value() && _switchAnimation.animating()) {
			const auto radialProgress = _radialInfo.realShowProgress;

			auto r = *_radialInfo.state;
			r.shown = 1.;
			if (_radialInfo.isDirectionToShow) {
				const auto to = r.arcFrom - kRadialFinishArcShift;
				ComputeRadialFinish(r.arcFrom, radialProgress, to);
				ComputeRadialFinish(r.arcLength, radialProgress);
			} else {
				r.arcLength = RadialState::kFull;
			}

			const auto opacity = (radialProgress > kOverlapProgressRadialHide)
				? 0.
				: _blobs->switchConnectingProgress();
			p.setOpacity(opacity);
			InfiniteRadialAnimation::Draw(
				p,
				r,
				QPoint(_bgSkip, _bgSkip),
				QSize(_bgSize, _bgSize),
				_content->width(),
				QPen(_radialInfo.st.color),
				_radialInfo.st.thickness);
		} else if (_radial) {
			auto state = _radial->computeState();
			state.shown = 1.;

			InfiniteRadialAnimation::Draw(
				p,
				std::move(state),
				QPoint(_bgSkip, _bgSkip),
				QSize(_bgSize, _bgSize),
				_content->width(),
				QPen(_radialInfo.st.color),
				_radialInfo.st.thickness);
		}
	}, _content->lifetime());
}

void CallMuteButton::refreshGradients() {
	const auto blobsInner = [&] {
		// The point of the circle at 45 degrees.
		const auto w = _blobs->innerRect().width();
		const auto mF = (1 - std::cos(M_PI / 4.)) * (w / 2.);
		return _blobs->innerRect().marginsRemoved(QMarginsF(mF, mF, mF, mF));
	}();

	_linearGradients = anim::linear_gradients<CallMuteButtonType>(
		_colors,
		QPointF(blobsInner.x() + blobsInner.width(), blobsInner.y()),
		QPointF(blobsInner.x(), blobsInner.y() + blobsInner.height()));

	auto glowColors = [&] {
		auto copy = _colors;
		for (auto &[type, stops] : copy) {
			auto firstColor = IsInactive(type)
				? st::groupCallBg->c
				: stops.stops[(stops.stops.size() - 1) / 2].second;
			firstColor.setAlpha(kGlowAlpha);
			stops.stops = QGradientStops{
				{ 0., std::move(firstColor) },
				{ 1., QColor(Qt::transparent) }
			};
		}
		return copy;
	}();
	_glowGradients = anim::radial_gradients<CallMuteButtonType>(
		std::move(glowColors),
		blobsInner.center(),
		_blobs->width() / 2);
}

void CallMuteButton::scheduleIconState(const IconState &state) {
	if (_iconState != state) {
		if (_icons[_iconState.index]->animating()) {
			_scheduledState = state;
		} else {
			startIconState(state);
		}
	} else if (_scheduledState) {
		_scheduledState = std::nullopt;
	}
}

void CallMuteButton::startIconState(const IconState &state) {
	_iconState = state;
	_scheduledState = std::nullopt;
	_icons[_iconState.index]->animate(
		[=] { iconAnimationCallback(); },
		_iconState.frameFrom,
		_iconState.frameTo);
}

void CallMuteButton::iconAnimationCallback() {
	_content->update(_muteIconRect);
	if (!_icons[_iconState.index]->animating() && _scheduledState) {
		startIconState(*_scheduledState);
	}
}

QString CallMuteButton::tooltipText() const {
	return _state.current().tooltip;
}

QPoint CallMuteButton::tooltipPos() const {
	return QCursor::pos();
}

bool CallMuteButton::tooltipWindowActive() const {
	return Ui::AppInFocus()
		&& Ui::InFocusChain(_content->window())
		&& _content->mapToGlobal(_content->rect()).contains(QCursor::pos());
}

const style::Tooltip *CallMuteButton::tooltipSt() const {
	return &st::groupCallTooltip;
}

void CallMuteButton::updateLabelsGeometry() {
	updateLabelGeometry(_content->geometry(), _label->size());
	updateCenterLabelGeometry(_content->geometry(), _centerLabel->size());
	updateSublabelGeometry(_content->geometry(), _sublabel->size());
}

void CallMuteButton::updateLabelGeometry(QRect my, QSize size) {
	const auto skip = _st->sublabelSkip + _st->labelsSkip;
	const auto contentHeight = _label->contentHeight();
	const auto contentTop = my.y() + my.height() - contentHeight - skip;
	_label->moveToLeft(
		my.x() + (my.width() - size.width()) / 2 + _labelShakeShift,
		contentTop - (size.height() - contentHeight) / 2,
		my.width());
}

void CallMuteButton::updateCenterLabelGeometry(QRect my, QSize size) {
	const auto skip = (_st->sublabelSkip / 2) + _st->labelsSkip;
	const auto contentHeight = _centerLabel->contentHeight();
	const auto contentTop = my.y() + my.height() - contentHeight - skip;
	_centerLabel->moveToLeft(
		my.x() + (my.width() - size.width()) / 2 + _labelShakeShift,
		contentTop - (size.height() - contentHeight) / 2,
		my.width());
}

void CallMuteButton::updateSublabelGeometry(QRect my, QSize size) {
	const auto skip = _st->labelsSkip;
	const auto contentHeight = _sublabel->contentHeight();
	const auto contentTop = my.y() + my.height() - contentHeight - skip;
	_sublabel->moveToLeft(
		my.x() + (my.width() - size.width()) / 2 + _labelShakeShift,
		contentTop - (size.height() - contentHeight) / 2,
		my.width());
}

void CallMuteButton::shake() {
	if (_shakeAnimation.animating()) {
		return;
	}
	_shakeAnimation.start(DefaultShakeCallback([=](int shift) {
		_labelShakeShift = shift;
		updateLabelsGeometry();
	}), 0., 1., st::shakeDuration);
}

CallMuteButton::HandleMouseState CallMuteButton::HandleMouseStateFromType(
		CallMuteButtonType type) {
	switch (type) {
	case CallMuteButtonType::Active:
	case CallMuteButtonType::Muted:
		return HandleMouseState::Enabled;
	case CallMuteButtonType::Connecting:
		return HandleMouseState::Disabled;
	case CallMuteButtonType::ScheduledCanStart:
	case CallMuteButtonType::ScheduledNotify:
	case CallMuteButtonType::ScheduledSilent:
	case CallMuteButtonType::ConferenceForceMuted:
	case CallMuteButtonType::ForceMuted:
	case CallMuteButtonType::RaisedHand:
		return HandleMouseState::Enabled;
	}
	Unexpected("Type in HandleMouseStateFromType.");
}

void CallMuteButton::setStyle(const style::CallMuteButton &st) {
	if (_st == &st) {
		return;
	}
	_st = &st;
	_lottieSize = AdjustedLottieSize(_st);
	_bgSize = AdjustedBgSize(_st);
	_bgSkip = AdjustedBgSkip(_st);
	const auto &button = _st->active.button;
	_content->resize(button.width, button.height);
	_blobs->setDiameter(_st->active.bgSize);

	refreshIcons();
	refreshLabels();
	updateLabelsGeometry();
	refreshGradients();
}

void CallMuteButton::setState(const CallMuteButtonState &state) {
	_state = state;
}

void CallMuteButton::setLevel(float level) {
	_level = level;
	_blobs->setLevel(level);
}

rpl::producer<Qt::MouseButton> CallMuteButton::clicks() {
	return _content->clicks() | rpl::before_next([=] {
		const auto type = _state.current().type;
		if (type == CallMuteButtonType::ForceMuted
			|| type == CallMuteButtonType::RaisedHand) {
			scheduleIconState(randomWavingState());
		}
	});
}

QSize CallMuteButton::innerSize() const {
	return QSize(
		_content->width() - 2 * _bgSkip,
		_content->width() - 2 * _bgSkip);
}

void CallMuteButton::moveInner(QPoint position) {
	_content->move(position - QPoint(_bgSkip, _bgSkip));

	{
		const auto offset = QPoint(
			(_blobs->width() - _content->width()) / 2,
			(_blobs->height() - _content->width()) / 2);
		_blobs->move(_content->pos() - offset);
	}
}

void CallMuteButton::setVisible(bool visible) {
	_centerLabel->setVisible(visible);
	_label->setVisible(visible);
	_sublabel->setVisible(visible);
	_content->setVisible(visible);
	_blobs->setVisible(visible);
}

bool CallMuteButton::isHidden() const {
	return _content->isHidden();
}

void CallMuteButton::raise() {
	_blobs->raise();
	_content->raise();
	_centerLabel->raise();
	_label->raise();
	_sublabel->raise();
}

void CallMuteButton::lower() {
	_centerLabel->lower();
	_label->lower();
	_sublabel->lower();
	_content->lower();
	_blobs->lower();
}

void CallMuteButton::setHandleMouseState(HandleMouseState state) {
	if (_handleMouseState == state) {
		return;
	}
	_handleMouseState = state;
	const auto handle = (_handleMouseState != HandleMouseState::Disabled);
	const auto pointer = (_handleMouseState == HandleMouseState::Enabled);
	_content->setAttribute(Qt::WA_TransparentForMouseEvents, !handle);
	_content->setPointerCursor(pointer);
}

void CallMuteButton::overridesColors(
		CallMuteButtonType fromType,
		CallMuteButtonType toType,
		float64 progress) {
	const auto toInactive = IsInactive(toType);
	const auto fromInactive = IsInactive(fromType);
	if (toInactive && (progress == 1)) {
		_colorOverrides = CallButtonColors();
		return;
	}
	const auto &fromStops = _colors.find(fromType)->second.stops;
	const auto &toStops = _colors.find(toType)->second.stops;
	auto from = fromStops[(fromStops.size() - 1) / 2].second;
	auto to = toStops[(toStops.size() - 1) / 2].second;
	auto fromRipple = from;
	auto toRipple = to;
	if (!toInactive) {
		toRipple.setAlpha(kOverrideColorRippleAlpha);
		to.setAlpha(kOverrideColorBgAlpha);
	}
	if (!fromInactive) {
		fromRipple.setAlpha(kOverrideColorRippleAlpha);
		from.setAlpha(kOverrideColorBgAlpha);
	}
	const auto resultBg = anim::color(from, to, progress);
	const auto resultRipple = anim::color(fromRipple, toRipple, progress);
	_colorOverrides = CallButtonColors{ resultBg, resultRipple };
}

rpl::producer<CallButtonColors> CallMuteButton::colorOverrides() const {
	return _colorOverrides.value();
}

not_null<RpWidget*> CallMuteButton::outer() const {
	return _content.get();
}

rpl::lifetime &CallMuteButton::lifetime() {
	return _blobs->lifetime();
}

CallMuteButton::~CallMuteButton() = default;

} // namespace Ui
