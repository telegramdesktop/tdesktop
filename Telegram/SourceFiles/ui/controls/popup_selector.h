// This file is part of Telegram Desktop,
// the official desktop application for the Telegram messaging service.
//
// For license and copyright information please follow this link:
// https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
//
#pragma once

#include "ui/rp_widget.h"
#include "ui/effects/animations.h"
#include "ui/effects/round_area_with_shadow.h"

namespace Ui {

enum class PopupAppearType {
	LeftToRight,
	RightToLeft,
	CenterExpand,
};

class PopupSelector final : public RpWidget {
public:
	PopupSelector(
		not_null<QWidget*> parent,
		QSize size,
		PopupAppearType appearType = PopupAppearType::CenterExpand);

	void updateShowState(
		float64 progress,
		float64 opacity,
		bool appearing);
	void hideAnimated();
	void popup(const QPoint &p);
	void setHideFinishedCallback(Fn<void()> callback);

	[[nodiscard]] QMargins marginsForShadow() const;

protected:
	void paintEvent(QPaintEvent *e) override;
	bool eventFilter(QObject *obj, QEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void focusOutEvent(QFocusEvent *e) override;

private:
	void paintBackgroundToBuffer();
	void paintAppearing(QPainter &p);
	void paintCollapsed(QPainter &p);

	const QSize _innerSize;
	RoundAreaWithShadow _cachedRound;
	QImage _paintBuffer;
	PopupAppearType _appearType = PopupAppearType::CenterExpand;

	float64 _appearProgress = 0.;
	float64 _appearOpacity = 0.;
	bool _appearing = false;
	bool _useTransparency = false;
	bool _hiding = false;
	Animations::Simple _hideAnimation;
	Fn<void()> _hideFinishedCallback;

};

} // namespace Ui
