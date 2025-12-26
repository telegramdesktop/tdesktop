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
struct LevelShape;
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
		std::shared_ptr<Ui::Show> show,
		const QString &name,
		rpl::producer<Data::StarsRating> value,
		Fn<Data::StarsRatingPending()> pending);
	~StarsRating();

	void raise();
	void moveTo(int x, int y);
	void setOpacity(float64 opacity);
	void setCustomColors(
		std::optional<QColor> textColor,
		std::optional<QColor> shapeColor);

	[[nodiscard]] rpl::producer<int> widthValue() const;
	[[nodiscard]] int width() const;

	[[nodiscard]] rpl::lifetime &lifetime();

private:
	void init();
	void paint(QPainter &p);
	void updateData(Data::StarsRating rating);
	void updateWidth();

	const std::unique_ptr<Ui::AbstractButton> _widget;
	const std::shared_ptr<Ui::Show> _show;
	const QString _name;

	QString _collapsedText;

	rpl::variable<Data::StarsRating> _value;
	Fn<Data::StarsRatingPending()> _pending;
	rpl::variable<int> _widthValue;
	const style::LevelShape *_shape = nullptr;

	QImage _cache;
	int _cachedLevel = std::numeric_limits<int>::min();

	int _currentLevel = 0;
	float64 _opacity = 1.;

	std::optional<QColor> _customTextColor;
	std::optional<QColor> _customShapeColor;

};

} // namespace Ui
