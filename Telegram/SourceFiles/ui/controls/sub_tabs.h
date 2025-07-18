/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"

namespace Ui {

class SubTabs : public RpWidget {
public:
	struct Options {
		QString selected;
		bool centered = false;
	};
	struct Tab {
		QString id;
		TextWithEntities text;

		friend inline bool operator==(const Tab &, const Tab &) = default;
	};

	explicit SubTabs(
		QWidget *parent,
		Options options = {},
		std::vector<Tab> tabs = {},
		Text::MarkedContext context = {});

	void setTabs(
		std::vector<Tab> tabs,
		Text::MarkedContext context = {});

	void setActiveTab(const QString &id);

	[[nodiscard]] rpl::producer<QString> activated() const;

private:
	struct Button {
		Tab tab;
		QRect geometry;
		Text::String text;
		bool active = false;
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
	[[nodiscard]] QPoint scroll() const;

	std::vector<Button> _buttons;
	rpl::event_stream<QString> _activated;
	int _dragx = 0;
	int _pressx = 0;
	float64 _dragscroll = 0.;
	float64 _scroll = 0.;
	int _scrollMax = 0;
	int _fullShift = 0;
	int _fullWidth = 0;
	int _selected = -1;
	int _pressed = -1;
	int _active = -1;
	bool _centered = false;

};

} // namespace Ui
