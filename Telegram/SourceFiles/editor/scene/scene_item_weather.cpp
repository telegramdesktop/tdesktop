/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "editor/scene/scene_item_weather.h"

#include "base/algorithm.h"
#include "editor/editor_link_pill.h"
#include "editor/editor_weather.h"
#include "ui/emoji_config.h"
#include "ui/painter.h"
#include "styles/style_basic.h"
#include "styles/style_editor.h"

#include <QtWidgets/QGraphicsSceneMouseEvent>

namespace Editor {
namespace {

constexpr auto kDpHeight = 36.;
constexpr auto kDpFontSize = 24.;
constexpr auto kRadiusRatio = 0.2;

const std::array<QColor, 3> &WeatherColors() {
	static auto result = std::array<QColor, 3>{
		QColor(0, 0, 0, 255),
		QColor(0, 0, 0, 76),
		QColor(255, 255, 255, 255),
	};
	return result;
}

[[nodiscard]] QColor NextWeatherColor(QColor color) {
	const auto &colors = WeatherColors();
	const auto i = ranges::find(colors, color, &QColor::toRgb);
	if (i == end(colors)) {
		return colors.front();
	}
	return (std::next(i) == end(colors)) ? colors.front() : *std::next(i);
}

} // namespace

QSizeF ItemWeather::PillSize(
		const WeatherData &weather,
		QSize imageSize) {
	const auto density = LinkPill::DensityFor(imageSize.width());
	auto font = st::semiboldFont->f;
	font.setPixelSize(int(base::SafeRound(kDpFontSize * density)));
	const auto emoji = Ui::Emoji::Find(weather.emoji);
	const auto textWidth = QFontMetrics(font).horizontalAdvance(
		Editor::WeatherTemperatureText(weather.celsius));
	const auto height = kDpHeight * density;
	const auto emojiSize = height * 2. / 3.;
	const auto margin = 2 * (height / 5.);
	const auto gap = height / 5.;
	const auto width = margin
		+ (emoji ? (emojiSize + gap) : 0.)
		+ textWidth
		+ margin;
	return QSizeF(std::max(width, height), height);
}

ItemWeather::ItemWeather(WeatherData weather, ItemBase::Data data)
: ItemBase(data)
, _weather(std::move(weather))
, _imageSize(data.imageSize)
, _density(LinkPill::DensityFor(_imageSize.width()))
, _font(st::semiboldFont->f) {
	if (!_weather.color.isValid()) {
		_weather.color = WeatherColors().front();
	}
	_font.setPixelSize(int(base::SafeRound(kDpFontSize * _density)));
	setAspectRatio(pillSize().height() / pillSize().width());
}

int ItemWeather::type() const {
	return Type;
}

bool ItemWeather::bakes() const {
	return false;
}

const WeatherData &ItemWeather::weather() const {
	return _weather;
}

QColor ItemWeather::foreground() const {
	return WeatherTextColor(_weather.color);
}

QSizeF ItemWeather::pillSize() const {
	return PillSize(_weather, _imageSize);
}

QRectF ItemWeather::visibleRect() const {
	return contentRect();
}

bool ItemWeather::flippable() const {
	return false;
}

void ItemWeather::paint(
		QPainter *p,
		const QStyleOptionGraphicsItem *option,
		QWidget *w) {
	const auto rect = visibleRect();
	const auto size = pillSize();
	const auto scale = rect.width() / size.width();
	const auto height = size.height() * scale;
	const auto emojiSize = size.height() * 2. / 3.;
	const auto margin = 2 * (size.height() / 5.);
	const auto gap = size.height() / 5.;
	const auto emoji = Ui::Emoji::Find(_weather.emoji);
	const auto text = Editor::WeatherTemperatureText(_weather.celsius);
	const auto textWidth = QFontMetrics(_font).horizontalAdvance(text);
	const auto fullWidth = margin
		+ (emoji ? (emojiSize + gap) : 0.)
		+ textWidth
		+ margin;
	const auto left = rect.x() + (rect.width() - fullWidth * scale) / 2.;

	p->save();
	p->setRenderHint(QPainter::Antialiasing);
	p->setRenderHint(QPainter::SmoothPixmapTransform);
	p->translate(left, rect.y() + (rect.height() - height) / 2.);
	p->scale(scale, scale);
	p->setPen(Qt::NoPen);
	p->setBrush(_weather.color);
	p->drawRoundedRect(
		QRectF(QPointF(), size),
		kRadiusRatio * size.height(),
		kRadiusRatio * size.height());
	if (emoji) {
		const auto base = Ui::Emoji::GetSizeLarge();
		const auto baseLogical = base / float64(style::DevicePixelRatio());
		const auto escale = emojiSize / baseLogical;
		p->save();
		p->translate(margin, (size.height() - emojiSize) / 2.);
		p->scale(escale, escale);
		Ui::Emoji::Draw(*p, emoji, base, 0, 0);
		p->restore();
	}
	p->setFont(_font);
	p->setPen(foreground());
	const auto textLeft = margin + (emoji ? (emojiSize + gap) : 0.);
	p->drawText(
		QRectF(
			textLeft,
			0,
			size.width() - textLeft - margin,
			size.height()),
		text,
		QTextOption(Qt::AlignVCenter | Qt::AlignLeft));
	p->restore();
	ItemBase::paint(p, option, w);
}

void ItemWeather::mousePressEvent(QGraphicsSceneMouseEvent *event) {
	_pressedSelected = isSelected() && (event->button() == Qt::LeftButton);
	ItemBase::mousePressEvent(event);
}

void ItemWeather::mouseReleaseEvent(QGraphicsSceneMouseEvent *event) {
	const auto clicked = _pressedSelected
		&& (event->button() == Qt::LeftButton)
		&& !isHandling()
		&& !dragThresholdPassed(event);
	ItemBase::mouseReleaseEvent(event);
	if (clicked) {
		_weather.color = NextWeatherColor(_weather.color);
		update();
	}
}

std::shared_ptr<ItemBase> ItemWeather::duplicate(ItemBase::Data data) const {
	return std::make_shared<ItemWeather>(_weather, std::move(data));
}

} // namespace Editor
