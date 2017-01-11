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

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#pragma once

#include "styles/style_widgets.h"
#include "ui/effects/panel_animation.h"

namespace Ui {

class ScrollArea;

class InnerDropdown : public TWidget {
	Q_OBJECT

public:
	InnerDropdown(QWidget *parent, const style::InnerDropdown &st = st::defaultInnerDropdown);

	void setOwnedWidget(TWidget *widget);

	bool overlaps(const QRect &globalRect) {
		if (isHidden() || _a_show.animating() || _a_opacity.animating()) return false;

		return rect().marginsRemoved(_st.padding).contains(QRect(mapFromGlobal(globalRect.topLeft()), globalRect.size()));
	}

	void setMaxHeight(int newMaxHeight);
	void resizeToContent();

	void otherEnter();
	void otherLeave();
	void hideFast();

	void setShowStartCallback(base::lambda<void()> &&callback) {
		_showStartCallback = std_::move(callback);
	}
	void setHideStartCallback(base::lambda<void()> &&callback) {
		_hideStartCallback = std_::move(callback);
	}
	void setHiddenCallback(base::lambda<void()> &&callback) {
		_hiddenCallback = std_::move(callback);
	}

	bool isHiding() const {
		return _hiding && _a_opacity.animating();
	}

	void setOrigin(PanelAnimation::Origin origin);
	void showAnimated(PanelAnimation::Origin origin);
	enum class HideOption {
		Default,
		IgnoreShow,
	};
	void hideAnimated(HideOption option = HideOption::Default);

protected:
	void resizeEvent(QResizeEvent *e) override;
	void paintEvent(QPaintEvent *e) override;
	void enterEvent(QEvent *e) override;
	void leaveEvent(QEvent *e) override;
	bool eventFilter(QObject *obj, QEvent *e) override;

	int resizeGetHeight(int newWidth) override;

private slots:
	void onHideAnimated() {
		hideAnimated();
	}
	void onWindowActiveChanged();
	void onScroll();
	void onWidgetHeightUpdated() {
		resizeToContent();
	}

private:
	QImage grabForPanelAnimation();
	void startShowAnimation();
	void startOpacityAnimation(bool hiding);
	void prepareCache();

	class Container;
	void showAnimationCallback();
	void opacityAnimationCallback();

	void hideFinished();
	void showStarted();

	void updateHeight();

	const style::InnerDropdown &_st;

	PanelAnimation::Origin _origin = PanelAnimation::Origin::TopLeft;
	std_::unique_ptr<PanelAnimation> _showAnimation;
	Animation _a_show;

	bool _hiding = false;
	QPixmap _cache;
	Animation _a_opacity;

	QTimer _hideTimer;
	bool _ignoreShowEvents = false;
	base::lambda<void()> _showStartCallback;
	base::lambda<void()> _hideStartCallback;
	base::lambda<void()> _hiddenCallback;

	object_ptr<Ui::ScrollArea> _scroll;

	int _maxHeight = 0;

};

class InnerDropdown::Container : public TWidget {
public:
	Container(QWidget *parent, TWidget *child, const style::InnerDropdown &st);
	void setVisibleTopBottom(int visibleTop, int visibleBottom) override;

	void resizeToContent();

protected:
	int resizeGetHeight(int newWidth) override;

private:
	const style::InnerDropdown &_st;

};

} // namespace Ui
