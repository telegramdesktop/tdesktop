/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"
#include "ui/effects/animations.h"

namespace style {
struct SubTabs;
} // namespace style

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

struct SubTabsReorderUpdate {
	QString id;
	int oldPosition = 0;
	int newPosition = 0;
	enum class State : uchar {
		Started,
		Applied,
		Cancelled,
	} state = State::Started;
};

class SubTabs : public RpWidget {
public:
	using Options = SubTabsOptions;
	using Tab = SubTabsTab;

	explicit SubTabs(
		QWidget *parent,
		const style::SubTabs &st,
		Options options = {},
		std::vector<Tab> tabs = {},
		Text::MarkedContext context = {});

	void setTabs(
		std::vector<Tab> tabs,
		Text::MarkedContext context = {});

	void setActiveTab(const QString &id);

	void setReorderEnabled(bool enabled);
	[[nodiscard]] bool reorderEnabled() const;

	void setPinnedInterval(int from, int to);
	void clearPinnedIntervals();

	[[nodiscard]] rpl::producer<QString> activated() const;
	[[nodiscard]] rpl::producer<QString> contextMenuRequests() const;

	using ReorderUpdate = SubTabsReorderUpdate;
	[[nodiscard]] rpl::producer<ReorderUpdate> reorderUpdates() const;

private:
	struct Button {
		Tab tab;
		QRect geometry;
		Text::String text;
		bool active = false;
		Ui::Animations::Simple shiftAnimation;
		float64 shift = 0.;
		float64 finalShift = 0.;
		float64 deltaShift = 0.;
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
	void shakeTransform(QPainter &p, int index, QPoint position, crl::time now) const;
	[[nodiscard]] bool isIndexPinned(int index) const;

	void startReorder(int index, QPoint globalPos);
	void updateReorder(QPoint globalPos);
	void finishReorder();
	void cancelReorder();
	void moveToShift(int index, float64 shift);
	void updateShift(int index);

	void checkForScrollAnimation();
	void updateScrollCallback();
	[[nodiscard]] int deltaFromEdge();

	const style::SubTabs &_st;
	std::vector<Button> _buttons;
	rpl::event_stream<QString> _activated;
	rpl::event_stream<QString> _contextMenuRequests;
	rpl::event_stream<ReorderUpdate> _reorderUpdates;
	std::optional<Qt::Orientation> _locked;
	int _dragx = 0;
	int _pressx = 0;
	float64 _dragscroll = 0.;
	float64 _scroll = 0.;
	float64 _scrollTo = 0.;
	Ui::Animations::Simple _scrollAnimation;
	Ui::Animations::Basic _reorderScrollAnimation;
	int _scrollMax = 0;
	int _fullShift = 0;
	int _fullWidth = 0;
	int _selected = -1;
	int _pressed = -1;
	int _active = -1;
	bool _centered = false;
	bool _reorderEnable = false;
	Ui::Animations::Basic _shakeAnimation;
	struct PinnedInterval {
		int from;
		int to;
	};
	std::vector<PinnedInterval> _pinnedIntervals;

	int _reorderIndex = -1;
	float64 _reorderStart = 0.;
	int _reorderDesiredIndex = 0;
	SubTabsReorderUpdate::State _reorderState
		= SubTabsReorderUpdate::State::Cancelled;
	QPoint _reorderMousePos;

};

} // namespace Ui
