/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
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

	template <typename Widget>
	QPointer<Widget> setOwnedWidget(object_ptr<Widget> widget) {
		auto result = doSetOwnedWidget(std::move(widget));
		return QPointer<Widget>(static_cast<Widget*>(result.data()));
	}

	bool overlaps(const QRect &globalRect) {
		if (isHidden() || _a_show.animating() || _a_opacity.animating()) return false;

		return rect().marginsRemoved(_st.padding).contains(QRect(mapFromGlobal(globalRect.topLeft()), globalRect.size()));
	}

	void setAutoHiding(bool autoHiding) {
		_autoHiding = autoHiding;
	}
	void setMaxHeight(int newMaxHeight);
	void resizeToContent();

	void otherEnter();
	void otherLeave();

	void setShowStartCallback(base::lambda<void()> callback) {
		_showStartCallback = std::move(callback);
	}
	void setHideStartCallback(base::lambda<void()> callback) {
		_hideStartCallback = std::move(callback);
	}
	void setHiddenCallback(base::lambda<void()> callback) {
		_hiddenCallback = std::move(callback);
	}

	bool isHiding() const {
		return _hiding && _a_opacity.animating();
	}

	enum class HideOption {
		Default,
		IgnoreShow,
	};
	void showAnimated();
	void setOrigin(PanelAnimation::Origin origin);
	void showAnimated(PanelAnimation::Origin origin);
	void hideAnimated(HideOption option = HideOption::Default);
	void finishAnimating();
	void showFast();
	void hideFast();

protected:
	void resizeEvent(QResizeEvent *e) override;
	void paintEvent(QPaintEvent *e) override;
	void enterEventHook(QEvent *e) override;
	void leaveEventHook(QEvent *e) override;
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
	QPointer<TWidget> doSetOwnedWidget(object_ptr<TWidget> widget);
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
	std::unique_ptr<PanelAnimation> _showAnimation;
	Animation _a_show;

	bool _autoHiding = true;
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
	Container(QWidget *parent, object_ptr<TWidget> child, const style::InnerDropdown &st);

	void resizeToContent();

protected:
	int resizeGetHeight(int newWidth) override;
	void visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) override;

private:
	object_ptr<TWidget> _child;
	const style::InnerDropdown &_st;

};

} // namespace Ui
