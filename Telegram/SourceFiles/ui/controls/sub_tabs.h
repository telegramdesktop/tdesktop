/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"

namespace Ui {

struct SubTabsOptions {
	QString selected;
	bool centered = false;
};

struct SubTabsTab {
	QString id;
	TextWithEntities text;

	friend inline bool operator==(
		const SubTabsTab &,
		const SubTabsTab &) = default;
};

class SubTabs : public RpWidget {
public:
	using Options = SubTabsOptions;
	using Tab = SubTabsTab;

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
	[[nodiscard]] rpl::producer<QString> contextMenuRequests() const;

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
	void contextMenuEvent(QContextMenuEvent *e) override;
	void paintEvent(QPaintEvent *e) override;
	bool eventHook(QEvent *e) override;

	void setSelected(int index);
	void setActive(int index);
	[[nodiscard]] QPoint scroll() const;

	std::vector<Button> _buttons;
	rpl::event_stream<QString> _activated;
	rpl::event_stream<QString> _contextMenuRequests;
	std::optional<Qt::Orientation> _locked;
	int _dragx = 0;
	int _pressx = 0;
	float64 _dragscroll = 0.;
	float64 _scroll = 0.;
	float64 _scrollTo = 0.;
	Ui::Animations::Simple _scrollAnimation;
	int _scrollMax = 0;
	int _fullShift = 0;
	int _fullWidth = 0;
	int _selected = -1;
	int _pressed = -1;
	int _active = -1;
	bool _centered = false;

};

} // namespace Ui
