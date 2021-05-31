// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "base/unique_qptr.h"
#include "ui/effects/animations.h"
#include "ui/effects/cross_line.h"
#include "ui/effects/gradient.h"
#include "ui/effects/radial_animation.h"
#include "ui/widgets/call_button.h"
#include "ui/widgets/tooltip.h"
#include "lottie/lottie_icon.h"

namespace style {
struct CallMuteButton;
} // namespace style

namespace st {
extern const style::InfiniteRadialAnimation &callConnectingRadial;
} // namespace st

namespace Ui {

class BlobsWidget;

class AbstractButton;
class FlatLabel;
class RpWidget;
class AnimatedLabel;

enum class CallMuteButtonType {
	Connecting,
	Active,
	Muted,
	ForceMuted,
	RaisedHand,
	ScheduledCanStart,
	ScheduledSilent,
	ScheduledNotify,
};

struct CallMuteButtonState {
	QString text;
	QString subtext;
	QString tooltip;
	CallMuteButtonType type = CallMuteButtonType::Connecting;
};

class CallMuteButton final : private AbstractTooltipShower {
public:
	explicit CallMuteButton(
		not_null<RpWidget*> parent,
		const style::CallMuteButton &st,
		rpl::producer<bool> &&hideBlobs,
		CallMuteButtonState initial = CallMuteButtonState());
	~CallMuteButton();

	void setState(const CallMuteButtonState &state);
	void setStyle(const style::CallMuteButton &st);
	void setLevel(float level);
	[[nodiscard]] rpl::producer<Qt::MouseButton> clicks();

	[[nodiscard]] QSize innerSize() const;
	[[nodiscard]] QRect innerGeometry() const;
	void moveInner(QPoint position);

	void shake();

	void setVisible(bool visible);
	void show() {
		setVisible(true);
	}
	void hide() {
		setVisible(false);
	}
	[[nodiscard]] bool isHidden() const;
	void raise();
	void lower();

	[[nodiscard]] not_null<RpWidget*> outer() const;
	[[nodiscard]] rpl::producer<CallButtonColors> colorOverrides() const;

	[[nodiscard]] rpl::lifetime &lifetime();

private:
	enum class HandleMouseState {
		Enabled,
		Blocked,
		Disabled,
	};
	struct RadialInfo {
		std::optional<RadialState> state = std::nullopt;
		bool isDirectionToShow = false;
		rpl::variable<float64> rawShowProgress = 0.;
		float64 realShowProgress = 0.;
		const style::InfiniteRadialAnimation &st = st::callConnectingRadial;
	};
	struct IconState {
		int index = -1;
		int frameFrom = 0;
		int frameTo = 0;

		inline bool operator==(const IconState &other) const {
			return (index == other.index)
				&& (frameFrom == other.frameFrom)
				&& (frameTo == other.frameTo);
		}
		inline bool operator!=(const IconState &other) const {
			return !(*this == other);
		}

		bool valid() const {
			return (index >= 0);
		}
		explicit operator bool() const {
			return valid();
		}
	};

	void init();
	void refreshIcons();
	void refreshGradients();
	void refreshLabels();
	void overridesColors(
		CallMuteButtonType fromType,
		CallMuteButtonType toType,
		float64 progress);

	void setHandleMouseState(HandleMouseState state);
	void updateCenterLabelGeometry(QRect my, QSize size);
	void updateLabelGeometry(QRect my, QSize size);
	void updateSublabelGeometry(QRect my, QSize size);
	void updateLabelsGeometry();

	[[nodiscard]] IconState iconStateFrom(CallMuteButtonType previous);
	[[nodiscard]] IconState randomWavingState();
	[[nodiscard]] IconState iconStateAnimated(CallMuteButtonType previous);
	void scheduleIconState(const IconState &state);
	void startIconState(const IconState &state);
	void iconAnimationCallback();

	QString tooltipText() const override;
	QPoint tooltipPos() const override;
	bool tooltipWindowActive() const override;
	const style::Tooltip *tooltipSt() const override;

	[[nodiscard]] static HandleMouseState HandleMouseStateFromType(
		CallMuteButtonType type);

	rpl::variable<CallMuteButtonState> _state;
	float _level = 0.;
	QRect _muteIconRect;
	HandleMouseState _handleMouseState = HandleMouseState::Enabled;

	not_null<const style::CallMuteButton*> _st;

	const base::unique_qptr<BlobsWidget> _blobs;
	const base::unique_qptr<AbstractButton> _content;
	base::unique_qptr<AnimatedLabel> _centerLabel;
	base::unique_qptr<AnimatedLabel> _label;
	base::unique_qptr<AnimatedLabel> _sublabel;
	int _labelShakeShift = 0;

	RadialInfo _radialInfo;
	std::unique_ptr<InfiniteRadialAnimation> _radial;
	const base::flat_map<CallMuteButtonType, anim::gradient_colors> _colors;
	anim::linear_gradients<CallMuteButtonType> _linearGradients;
	anim::radial_gradients<CallMuteButtonType> _glowGradients;

	std::array<std::optional<Lottie::Icon>, 2> _icons;
	IconState _iconState;
	std::optional<IconState> _scheduledState;

	Animations::Simple _switchAnimation;
	Animations::Simple _shakeAnimation;

	rpl::variable<CallButtonColors> _colorOverrides;

};

} // namespace Ui
