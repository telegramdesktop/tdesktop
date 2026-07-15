/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/unique_qptr.h"
#include "base/timer.h"
#include "base/weak_ptr.h"
#include "ui/effects/animations.h"

class History;
class HistoryInner;

namespace Ui {
class RpWidget;
class ElasticScroll;
struct ElasticScrollPosition;
enum class ElasticScrollMovement;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

namespace HistoryView {

class PullToNextChannel final {
public:
	PullToNextChannel(
		not_null<Ui::RpWidget*> parent,
		not_null<Ui::ElasticScroll*> scroll,
		not_null<Window::SessionController*> controller);
	~PullToNextChannel();

	void attachToContent(not_null<HistoryInner*> inner);

	void setHistory(History *history);

	void updateGeometry();

private:
	class Indicator;
	class HintOverlay;

	[[nodiscard]] bool active() const;
	[[nodiscard]] bool atBottom() const;
	void handleOverscroll(
		Ui::ElasticScrollPosition position,
		Ui::ElasticScrollMovement movement);
	void updatePullCurve();
	void startExpand(bool ready);
	void pushIndicator();
	void clearState();
	void reset();
	void jumpWhenReady(base::weak_ptr<History> next, crl::time waited);
	void jumpTo(not_null<History*> history);

	const not_null<Ui::RpWidget*> _parent;
	const not_null<Ui::ElasticScroll*> _scroll;
	const not_null<Window::SessionController*> _controller;
	const base::unique_qptr<Indicator> _indicator;
	const base::unique_qptr<HintOverlay> _hint;

	base::weak_ptr<History> _history;
	base::weak_ptr<History> _next;

	bool _pulling = false;
	bool _committed = false;
	bool _jumping = false;
	bool _reached = false;
	bool _expandTo = false;
	float64 _pull = 0.;
	float64 _peakPull = 0.;
	float64 _effective = 0.;
	Ui::Animations::Simple _expand;
	base::Timer _dwellTimer;

	rpl::lifetime _lifetime;

};

} // namespace HistoryView
