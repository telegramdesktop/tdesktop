/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"

namespace style {
struct Tooltip;
struct ImportantTooltip;
} // namespace style

namespace Ui {

class AbstractTooltipShower {
public:
	virtual QString tooltipText() const = 0;
	virtual QPoint tooltipPos() const = 0;
	virtual bool tooltipWindowActive() const;
	virtual const style::Tooltip *tooltipSt() const;
	virtual ~AbstractTooltipShower();

};

class Tooltip : public TWidget {
	Q_OBJECT

public:
	static void Show(int32 delay, const AbstractTooltipShower *shower);
	static void Hide();

private slots:
	void onWndActiveChanged();

protected:
	void paintEvent(QPaintEvent *e) override;
	void hideEvent(QHideEvent *e) override;

	bool eventFilter(QObject *o, QEvent *e) override;

private:
	void performShow();

	Tooltip();
	~Tooltip();

	void popup(const QPoint &p, const QString &text, const style::Tooltip *st);

	friend class AbstractTooltipShower;
	const AbstractTooltipShower *_shower = nullptr;
	base::Timer _showTimer;

	Text _text;
	QPoint _point;

	const style::Tooltip *_st = nullptr;

	base::Timer _hideByLeaveTimer;
	bool _isEventFilter = false;
	bool _useTransparency = true;

};

class ImportantTooltip : public TWidget {
public:
	ImportantTooltip(QWidget *parent, object_ptr<TWidget> content, const style::ImportantTooltip &st);

	void pointAt(QRect area, RectParts preferSide = RectPart::Top | RectPart::Left);

	void toggleAnimated(bool visible);
	void toggleFast(bool visible);
	void hideAfter(TimeMs timeout);

	void setHiddenCallback(Fn<void()> callback) {
		_hiddenCallback = std::move(callback);
	}

protected:
	void resizeEvent(QResizeEvent *e);
	void paintEvent(QPaintEvent *e);

private:
	void animationCallback();
	QRect countInner() const;
	void setArea(QRect area);
	void countApproachSide(RectParts preferSide);
	void updateGeometry();
	void checkAnimationFinish();
	void refreshAnimationCache();

	base::Timer _hideTimer;
	const style::ImportantTooltip &_st;
	object_ptr<TWidget> _content;
	QRect _area;
	RectParts _side = RectPart::Top | RectPart::Left;
	QPixmap _arrow;

	Animation _visibleAnimation;
	bool _visible = false;
	Fn<void()> _hiddenCallback;
	bool _useTransparency = true;
	QPixmap _cache;

};

} // namespace Ui
