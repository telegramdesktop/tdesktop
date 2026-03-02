/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"
#include "base/weak_ptr.h"
#include "ui/effects/animations.h"
#include "ui/effects/round_area_with_shadow.h"
#include "ui/text/text.h"

namespace Ui {
struct ChatPaintContext;
} // namespace Ui

namespace HistoryView {
using PaintContext = Ui::ChatPaintContext;
struct TextState;
} // namespace HistoryView

namespace HistoryView::ReplyButton {

struct ButtonParameters {
	[[nodiscard]] ButtonParameters translated(QPoint delta) const {
		auto result = *this;
		result.center += delta;
		result.pointer += delta;
		return result;
	}

	FullMsgId context;
	QPoint center;
	QPoint pointer;
	QPoint globalPointer;
	ClickHandlerPtr link;
	int visibleTop = 0;
	int visibleBottom = 0;
	bool outside = false;
};

[[nodiscard]] int ComputeInnerWidth();

enum class ButtonState {
	Hidden,
	Shown,
	Active,
	Inside,
};

class Button final {
public:
	Button(
		Fn<void(QRect)> update,
		ButtonParameters parameters,
		Fn<void()> hide,
		QSize outer);
	~Button();

	void applyParameters(ButtonParameters parameters);
	void applyState(ButtonState state);

	[[nodiscard]] bool isHidden() const;
	[[nodiscard]] QRect geometry() const;
	[[nodiscard]] float64 currentScale() const;
	[[nodiscard]] float64 currentOpacity() const;

private:
	void updateGeometry(Fn<void(QRect)> update);
	void applyState(ButtonState state, Fn<void(QRect)> update);
	void applyParameters(
		ButtonParameters parameters,
		Fn<void(QRect)> update);

	const Fn<void(QRect)> _update;

	ButtonState _state = ButtonState::Hidden;
	float64 _finalScale = 0.;
	Ui::Animations::Simple _scaleAnimation;
	Ui::Animations::Simple _opacityAnimation;

	QRect _collapsed;
	QRect _geometry;

	base::Timer _hideTimer;

};

class Manager final : public base::has_weak_ptr {
public:
	Manager(Fn<void(QRect)> buttonUpdate);
	~Manager();

	void updateButton(ButtonParameters parameters);
	void paint(QPainter &p, const PaintContext &context);
	[[nodiscard]] TextState buttonTextState(QPoint position) const;
	void remove(FullMsgId context);

private:
	void showButtonDelayed();
	[[nodiscard]] bool overCurrentButton(QPoint position) const;
	void paintButton(
		QPainter &p,
		const PaintContext &context,
		not_null<Button*> button);
	void paintButton(
		QPainter &p,
		const PaintContext &context,
		not_null<Button*> button,
		int frame,
		float64 scale);
	void removeStaleButtons();
	void clearAppearAnimations();
	[[nodiscard]] QMargins innerMargins() const;
	[[nodiscard]] QRect buttonInner() const;
	[[nodiscard]] QRect buttonInner(not_null<Button*> button) const;

	QSize _outer;
	QRect _inner;
	Ui::RoundAreaWithShadow _cachedRound;

	ClickHandlerPtr _link;
	FullMsgId _buttonContext;

	std::optional<ButtonParameters> _scheduledParameters;
	base::Timer _buttonShowTimer;
	const Fn<void(QRect)> _buttonUpdate;
	std::unique_ptr<Button> _button;
	std::vector<std::unique_ptr<Button>> _buttonHiding;

	Ui::Text::String _text;

};

} // namespace HistoryView::ReplyButton
