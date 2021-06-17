/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/wrap/slide_wrap.h"
#include "ui/effects/animations.h"
#include "base/object_ptr.h"
#include "base/timer.h"

class Painter;

namespace Ui {

class PlainShadow;
class RoundButton;
struct GroupCallUser;
class GroupCallUserpics;

struct GroupCallBarContent {
	QString title;
	TimeId scheduleDate = 0;
	int count = 0;
	bool shown = false;
	std::vector<GroupCallUser> users;
};

class GroupCallScheduledLeft final {
public:
	enum class Negative {
		Show,
		Ignore,
	};
	explicit GroupCallScheduledLeft(TimeId date);

	void setDate(TimeId date);

	[[nodiscard]] rpl::producer<QString> text(Negative negative) const;
	[[nodiscard]] rpl::producer<bool> late() const;

private:
	[[nodiscard]] crl::time computePreciseDate() const;
	void restart();
	void update();

	rpl::variable<QString> _text;
	rpl::variable<QString> _textNonNegative;
	rpl::variable<bool> _late;
	TimeId _date = 0;
	crl::time _datePrecise = 0;
	base::Timer _timer;
	rpl::lifetime _lifetime;

};

class GroupCallBar final {
public:
	GroupCallBar(
		not_null<QWidget*> parent,
		rpl::producer<GroupCallBarContent> content,
		rpl::producer<bool> &&hideBlobs);
	~GroupCallBar();

	void show();
	void hide();
	void raise();
	void finishAnimating();

	void setShadowGeometryPostprocess(Fn<QRect(QRect)> postprocess);

	void move(int x, int y);
	void resizeToWidth(int width);
	[[nodiscard]] int height() const;
	[[nodiscard]] rpl::producer<int> heightValue() const;
	[[nodiscard]] rpl::producer<> barClicks() const;
	[[nodiscard]] rpl::producer<> joinClicks() const;

	[[nodiscard]] rpl::lifetime &lifetime() {
		return _wrap.lifetime();
	}

private:
	using User = GroupCallUser;

	void refreshOpenBrush();
	void refreshScheduledProcess();
	void updateShadowGeometry(QRect wrapGeometry);
	void updateControlsGeometry(QRect wrapGeometry);
	void updateUserpics();
	void setupInner();
	void setupRightButton(not_null<RoundButton*> button);
	void paint(Painter &p);

	SlideWrap<> _wrap;
	not_null<RpWidget*> _inner;
	std::unique_ptr<RoundButton> _join;
	std::unique_ptr<RoundButton> _open;
	rpl::event_stream<Qt::MouseButton> _joinClicks;
	QBrush _openBrushOverride;
	int _openBrushForWidth = 0;
	std::unique_ptr<PlainShadow> _shadow;
	rpl::event_stream<> _barClicks;
	Fn<QRect(QRect)> _shadowGeometryPostprocess;
	bool _shouldBeShown = false;
	bool _forceHidden = false;

	GroupCallBarContent _content;
	std::unique_ptr<GroupCallScheduledLeft> _scheduledProcess;
	std::unique_ptr<GroupCallUserpics> _userpics;

};

} // namespace Ui
