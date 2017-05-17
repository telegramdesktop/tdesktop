/*
 This file is part of Telegram Desktop,
 the official desktop version of Telegram messaging app, see https://telegram.org

 Telegram Desktop is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 It is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 GNU General Public License for more details.

 Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
 Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
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

	enum class SideFlag {
		Up     = 0x01,
		Down   = 0x02,
		Left   = 0x04,
		Center = 0x08,
		Right  = 0x0c,
	};
	Q_DECLARE_FLAGS(Side, SideFlag);
	void pointAt(QRect area, Side preferSide = Side(SideFlag::Up) | SideFlag::Left);

	void toggleAnimated(bool visible);
	void toggleFast(bool visible);
	void hideAfter(TimeMs timeout);

	void setHiddenCallback(base::lambda<void()> callback) {
		_hiddenCallback = std::move(callback);
	}

protected:
	void resizeEvent(QResizeEvent *e);
	void paintEvent(QPaintEvent *e);

private:
	void animationCallback();
	QRect countInner() const;
	void setArea(QRect area);
	void countApproachSide(Side preferSide);
	void updateGeometry();
	void checkAnimationFinish();
	void refreshAnimationCache();

	base::Timer _hideTimer;
	const style::ImportantTooltip &_st;
	object_ptr<TWidget> _content;
	QRect _area;
	Side _side = Side(SideFlag::Up) | SideFlag::Left;
	QPixmap _arrow;

	Animation _visibleAnimation;
	bool _visible = false;
	base::lambda<void()> _hiddenCallback;
	bool _useTransparency = true;
	QPixmap _cache;

};

Q_DECLARE_OPERATORS_FOR_FLAGS(ImportantTooltip::Side);

} // namespace Ui
