/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Data {
struct UniqueGift;
struct UniqueGiftBackdrop;
} // namespace Data

namespace Ui::Text {
class CustomEmoji;
} // namespace Ui::Text

class PeerData;

namespace Ui {

struct PatternPoint {
	QPointF position;
	float64 scale = 1.;
	float64 opacity = 1.;
};

[[nodiscard]] QImage CreateTopBgGradient(
	QSize size,
	const Data::UniqueGift &gift);

[[nodiscard]] QImage CreateTopBgGradient(
	QSize size,
	const Data::UniqueGiftBackdrop &backdrop);

[[nodiscard]] QImage CreateTopBgGradient(
	QSize size,
	QColor centerColor,
	QColor edgeColor,
	bool rounded = true,
	QPoint offset = QPoint());

[[nodiscard]] QImage CreateTopBgGradient(
	QSize size,
	not_null<PeerData*> peer,
	QPoint offset = QPoint());

[[nodiscard]] const std::vector<PatternPoint> &PatternBgPoints();
[[nodiscard]] const std::vector<PatternPoint> &PatternBgPointsSmall();

void PaintBgPoints(
	QPainter &p,
	const std::vector<PatternPoint> &points,
	base::flat_map<float64, QImage> &cache,
	not_null<Ui::Text::CustomEmoji*> emoji,
	const Data::UniqueGift &gift,
	const QRect &rect,
	float64 shown = 1.);

void PaintBgPoints(
	QPainter &p,
	const std::vector<PatternPoint> &points,
	base::flat_map<float64, QImage> &cache,
	not_null<Ui::Text::CustomEmoji*> emoji,
	const Data::UniqueGiftBackdrop &backdrop,
	const QRect &rect,
	float64 shown = 1.);

void PaintBgPoints(
	QPainter &p,
	const std::vector<PatternPoint> &points,
	base::flat_map<float64, QImage> &cache,
	not_null<Ui::Text::CustomEmoji*> emoji,
	const QColor &patternColor,
	const QRect &rect,
	float64 shown = 1.);

} // namespace Ui
