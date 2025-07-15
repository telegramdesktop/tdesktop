/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"
#include "data/data_peer_common.h"

namespace style {
struct StarsRating;
} // namespace style

namespace Ui {

class AbstractButton;
class FlatLabel;

class StarsRating final {
public:
	StarsRating(
		QWidget *parent,
		const style::StarsRating &st,
		rpl::producer<Data::StarsRating> value);
	~StarsRating();

	void raise();
	void moveTo(int x, int y);
	void setMinimalAddedWidth(int addedWidth);

	[[nodiscard]] rpl::producer<int> collapsedWidthValue() const;

	[[nodiscard]] rpl::lifetime &lifetime();

private:
	void init();
	void paint(QPainter &p);
	void updateTexts(Data::StarsRating rating);
	void updateExpandedWidth();
	void updateWidth();

	const std::unique_ptr<Ui::AbstractButton> _widget;
	const style::StarsRating &_st;

	Ui::Text::String _collapsedText;
	Ui::Text::String _expandedText;
	Ui::Text::String _nextText;

	rpl::variable<Data::StarsRating> _value;
	rpl::variable<int> _collapsedWidthValue;
	rpl::variable<int> _expandedWidthValue;
	rpl::variable<int> _minimalContentWidth;
	rpl::variable<int> _minimalAddedWidth;
	rpl::variable<bool> _expanded = false;
	Ui::Animations::Simple _expandedAnimation;

	base::Timer _collapseTimer;

};

} // namespace Ui
