/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"
#include "ui/effects/animations.h"
#include "ui/text/text.h"
#include "ui/widgets/shadow.h"

namespace style {
struct ProfileTabsStrip;
} // namespace style

namespace Ui {
class RippleAnimation;
} // namespace Ui

namespace Info::Profile {

struct StripTab {
	QString id;
	QString text;
};

class TabsStrip final : public Ui::RpWidget {
public:
	TabsStrip(QWidget *parent, const style::ProfileTabsStrip &st);

	void setTabs(std::vector<StripTab> tabs);
	void setActiveTab(const QString &id);

	[[nodiscard]] rpl::producer<QString> activated() const;

private:
	struct Button {
		StripTab tab;
		QRect geometry;
		Ui::Text::String text;
		std::unique_ptr<Ui::RippleAnimation> ripple;
	};

	int resizeGetHeight(int newWidth) override;
	void wheelEvent(QWheelEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void paintEvent(QPaintEvent *e) override;
	bool eventHook(QEvent *e) override;

	void setSelected(int index);
	void setActive(int index);
	void scrollToTab(int index);
	void addRipple(int index, QPoint position);
	void stopPressedRipple();
	[[nodiscard]] int indexAt(QPoint position) const;
	[[nodiscard]] QPoint contentOrigin() const;
	[[nodiscard]] QRect islandRect() const;
	[[nodiscard]] int islandInteriorWidth() const;
	[[nodiscard]] QRect highlightRect(int index) const;
	[[nodiscard]] QRectF currentHighlightRect() const;
	[[nodiscard]] int scrollValue() const;

	const style::ProfileTabsStrip &_st;
	Ui::BoxShadow _shadow;
	std::vector<Button> _buttons;
	rpl::event_stream<QString> _activated;
	std::optional<Qt::Orientation> _locked;
	int _dragx = 0;
	int _pressx = 0;
	float64 _dragscroll = 0.;
	float64 _scroll = 0.;
	float64 _scrollTo = 0.;
	Ui::Animations::Simple _scrollAnimation;
	Ui::Animations::Simple _activeAnimation;
	QRect _activeFrom;
	QRect _activeTo;
	int _scrollMax = 0;
	int _fullWidth = 0;
	int _selected = -1;
	int _pressed = -1;
	int _active = -1;
	int _wasActive = -1;

};

} // namespace Info::Profile
