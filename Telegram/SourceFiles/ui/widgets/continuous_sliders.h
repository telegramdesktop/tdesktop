/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/effects/animations.h"
#include "ui/rp_widget.h"

namespace base {
class Timer;
} // namespace base

namespace style {
struct FilledSlider;
struct MediaSlider;
} // namespace style

namespace Ui {

class ContinuousSlider : public RpWidget {
public:
	ContinuousSlider(QWidget *parent);

	enum class Direction {
		Horizontal,
		Vertical,
	};
	void setDirection(Direction direction) {
		_direction = direction;
		update();
	}

	float64 value() const;
	void setValue(float64 value);
	void setValue(float64 value, float64 receivedTill);
	void setFadeOpacity(float64 opacity);
	void setDisabled(bool disabled);
	bool isDisabled() const {
		return _disabled;
	}

	void setAdjustCallback(Fn<float64(float64)> callback) {
		_adjustCallback = std::move(callback);
	}
	void setChangeProgressCallback(Fn<void(float64)> callback) {
		_changeProgressCallback = std::move(callback);
	}
	void setChangeFinishedCallback(Fn<void(float64)> callback) {
		_changeFinishedCallback = std::move(callback);
	}
	bool isChanging() const {
		return _mouseDown;
	}

	void setMoveByWheel(bool move);

protected:
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void wheelEvent(QWheelEvent *e) override;
	void enterEventHook(QEnterEvent *e) override;
	void leaveEventHook(QEvent *e) override;

	float64 fadeOpacity() const {
		return _fadeOpacity;
	}
	float64 getCurrentValue() const {
		return _mouseDown ? _downValue : _value;
	}
	float64 getCurrentReceivedTill() const {
		return _receivedTill;
	}
	float64 getCurrentOverFactor() {
		return _disabled ? 0. : _overAnimation.value(_over ? 1. : 0.);
	}
	Direction getDirection() const {
		return _direction;
	}
	bool isHorizontal() const {
		return (_direction == Direction::Horizontal);
	}
	QRect getSeekRect() const;
	virtual QSize getSeekDecreaseSize() const = 0;

private:
	virtual float64 getOverDuration() const = 0;

	bool moveByWheel() const {
		return _byWheelFinished != nullptr;
	}

	void setOver(bool over);
	float64 computeValue(const QPoint &pos) const;
	void updateDownValueFromPos(const QPoint &pos);

	Direction _direction = Direction::Horizontal;
	bool _disabled = false;

	std::unique_ptr<base::Timer> _byWheelFinished;

	Fn<float64(float64)> _adjustCallback;
	Fn<void(float64)> _changeProgressCallback;
	Fn<void(float64)> _changeFinishedCallback;

	bool _over = false;
	Ui::Animations::Simple _overAnimation;

	float64 _value = 0.;
	float64 _receivedTill = 0.;

	bool _mouseDown = false;
	float64 _downValue = 0.;

	float64 _fadeOpacity = 1.;

};

class FilledSlider : public ContinuousSlider {
public:
	FilledSlider(QWidget *parent, const style::FilledSlider &st);

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	QSize getSeekDecreaseSize() const override;
	float64 getOverDuration() const override;

	const style::FilledSlider &_st;

};

class MediaSlider : public ContinuousSlider {
public:
	MediaSlider(QWidget *parent, const style::MediaSlider &st);

	void setAlwaysDisplayMarker(bool alwaysDisplayMarker) {
		_alwaysDisplayMarker = alwaysDisplayMarker;
		update();
	}
	void disablePaint(bool disabled);

	template <
		typename Value,
		typename Convert,
		typename Progress,
		typename = std::enable_if_t<
			rpl::details::is_callable_plain_v<Progress, Value>
			&& std::is_same_v<Value, decltype(std::declval<Convert>()(1))>>>
	void setPseudoDiscrete(
			int valuesCount,
			Convert &&convert,
			Value current,
			Progress &&progress,
			int indexMin = 0) {
		Expects(valuesCount > 1);

		setAlwaysDisplayMarker(true);
		setDirection(Ui::ContinuousSlider::Direction::Horizontal);

		const auto sectionsCount = (valuesCount - 1);
		setValue(1.);
		for (auto index = index_type(); index != valuesCount; ++index) {
			if (current <= convert(index)) {
				setValue(index / float64(sectionsCount));
				break;
			}
		}
		setAdjustCallback([=](float64 value) {
			return std::max(
				base::SafeRound(value * sectionsCount),
				indexMin * 1.
			) / sectionsCount;
		});
		setChangeProgressCallback([=](float64 value) {
			const auto index = std::max(
				int(base::SafeRound(value * sectionsCount)),
				indexMin);
			progress(convert(index));
		});
	}

	template <
		typename Value,
		typename Convert,
		typename Progress,
		typename Finished,
		typename = std::enable_if_t<
			rpl::details::is_callable_plain_v<Progress, Value>
			&& rpl::details::is_callable_plain_v<Finished, Value>
			&& std::is_same_v<Value, decltype(std::declval<Convert>()(1))>>>
	void setPseudoDiscrete(
			int valuesCount,
			Convert &&convert,
			Value current,
			Progress &&progress,
			Finished &&finished,
			int indexMin = 0) {
		setPseudoDiscrete(
			valuesCount,
			std::forward<Convert>(convert),
			current,
			std::forward<Progress>(progress),
			indexMin);
		setChangeFinishedCallback([=](float64 value) {
			const auto sectionsCount = (valuesCount - 1);
			const auto index = std::max(
				int(base::SafeRound(value * sectionsCount)),
				indexMin);
			finished(convert(index));
		});
	}

	void setActiveFgOverride(std::optional<QColor> color);
	void addDivider(float64 atValue, const QSize &size);

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	struct Divider {
		const float64 atValue;
		const QSize size;
	};

	QSize getSeekDecreaseSize() const override;
	float64 getOverDuration() const override;

	const style::MediaSlider &_st;
	bool _alwaysDisplayMarker = false;
	bool _paintDisabled = false;

	std::vector<Divider> _dividers;
	std::optional<QColor> _activeFgOverride;

};

class MediaSliderWheelless : public MediaSlider {
public:
	using Ui::MediaSlider::MediaSlider;

protected:
	void wheelEvent(QWheelEvent *e) override {
		e->ignore();
	}

};

} // namespace Ui
