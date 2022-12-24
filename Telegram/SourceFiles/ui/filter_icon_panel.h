/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"
#include "ui/rp_widget.h"
#include "ui/effects/animations.h"
#include "ui/round_rect.h"

namespace Ui {

enum class FilterIcon : uchar;
class PanelAnimation;

class FilterIconPanel final : public Ui::RpWidget {
public:
	FilterIconPanel(QWidget *parent);
	~FilterIconPanel();

	void hideFast();
	[[nodiscard]] bool hiding() const {
		return _hiding || _hideTimer.isActive();
	}

	[[nodiscard]] style::margins innerPadding() const;

	void showAnimated();
	void hideAnimated();
	void toggleAnimated();

	[[nodiscard]] rpl::producer<FilterIcon> chosen() const;

private:
	void enterEventHook(QEnterEvent *e) override;
	void leaveEventHook(QEvent *e) override;
	void otherEnter();
	void otherLeave();

	void paintEvent(QPaintEvent *e) override;
	bool eventFilter(QObject *obj, QEvent *e) override;

	void setup();
	void setupInner();
	void hideByTimerOrLeave();

	// Rounded rect which has shadow around it.
	[[nodiscard]] QRect innerRect() const;

	[[nodiscard]] QImage grabForAnimation();
	void startShowAnimation();
	void startOpacityAnimation(bool hiding);
	void prepareCacheFor(bool hiding);

	void opacityAnimationCallback();

	void hideFinished();
	void showStarted();
	void setSelected(int selected);
	void setPressed(int pressed);
	[[nodiscard]] QRect countRect(int index) const;
	void updateRect(int index);
	void mouseMove(QPoint position);
	void mousePress(Qt::MouseButton button);
	void mouseRelease(Qt::MouseButton button);

	const not_null<Ui::RpWidget*> _inner;
	rpl::event_stream<FilterIcon> _chosen;
	Ui::RoundRect _innerBg;

	int _selected = -1;
	int _pressed = -1;

	std::unique_ptr<Ui::PanelAnimation> _showAnimation;
	Ui::Animations::Simple _a_show;

	bool _hiding = false;
	QPixmap _cache;
	Ui::Animations::Simple _a_opacity;
	base::Timer _hideTimer;

};

} // namespace Ui
