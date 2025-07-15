/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"
#include "base/weak_ptr.h"
#include "data/data_peer_common.h"

namespace style {
struct Toast;
struct StarsRating;
} // namespace style

namespace Ui::Toast {
class Instance;
} // namespace Ui::Toast

namespace Ui {

class ImportantTooltip;
class AbstractButton;
class FlatLabel;

class StarsRating final {
public:
	StarsRating(
		QWidget *parent,
		const style::StarsRating &st,
		rpl::producer<Data::StarsRating> value,
		Fn<not_null<QWidget*>()> parentForTooltip);
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
	void toggleTooltips(bool shown);
	void updateStarsTooltipGeometry();

	const std::unique_ptr<Ui::AbstractButton> _widget;
	const style::StarsRating &_st;
	const Fn<not_null<QWidget*>()> _parentForTooltip;

	std::unique_ptr<style::Toast> _aboutSt;
	base::weak_ptr<Ui::Toast::Instance> _about;
	std::unique_ptr<Ui::ImportantTooltip> _stars;

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
	mutable int _activeWidth = 0;

	base::Timer _collapseTimer;

};

} // namespace Ui
