/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

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
class Show;

class StarsRating final {
public:
	StarsRating(
		QWidget *parent,
		const style::StarsRating &st,
		std::shared_ptr<Ui::Show> show,
		const QString &name,
		rpl::producer<Data::StarsRating> value);
	~StarsRating();

	void raise();
	void moveTo(int x, int y);

	[[nodiscard]] rpl::producer<int> collapsedWidthValue() const;

	[[nodiscard]] rpl::lifetime &lifetime();

private:
	void init();
	void paint(QPainter &p);
	void updateTexts(Data::StarsRating rating);
	void updateWidth();

	const std::unique_ptr<Ui::AbstractButton> _widget;
	const style::StarsRating &_st;
	const std::shared_ptr<Ui::Show> _show;
	const QString _name;

	Ui::Text::String _collapsedText;

	rpl::variable<Data::StarsRating> _value;
	rpl::variable<int> _collapsedWidthValue;

};

} // namespace Ui
