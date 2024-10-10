/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/widgets/color_editor.h"

#include "base/platform/base_platform_info.h"
#include "lang/lang_keys.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "ui/ui_utility.h"
#include "ui/widgets/fields/masked_input_field.h"
#include "ui/widgets/shadow.h"
#include "styles/style_boxes.h"
#include "styles/style_media_view.h"

class ColorEditor::Picker : public TWidget {
public:
	Picker(QWidget *parent, Mode mode, QColor color);

	float64 valueX() const {
		return _x;
	}
	float64 valueY() const {
		return _y;
	}

	rpl::producer<> changed() const {
		return _changed.events();
	}
	void setHSB(HSB hsb);
	void setRGB(int red, int green, int blue);

protected:
	void paintEvent(QPaintEvent *e);

	void mousePressEvent(QMouseEvent *e);
	void mouseMoveEvent(QMouseEvent *e);
	void mouseReleaseEvent(QMouseEvent *e);

private:
	void setFromColor(QColor color);
	QCursor generateCursor();

	void preparePalette();
	void preparePaletteRGBA();
	void preparePaletteHSL();
	void updateCurrentPoint(QPoint localPosition);

	Mode _mode;
	QColor _topleft;
	QColor _topright;
	QColor _bottomleft;
	QColor _bottomright;

	QImage _palette;
	bool _paletteInvalidated = false;
	float64 _x = 0.;
	float64 _y = 0.;

	bool _choosing = false;
	rpl::event_stream<> _changed;

};

QCursor ColorEditor::Picker::generateCursor() {
	const auto diameter = style::ConvertScale(16);
	const auto line = style::ConvertScale(1);
	const auto size = ((diameter + 2 * line) >= 32) ? 64 : 32;
	const auto diff = (size - diameter) / 2;
	auto cursor = QImage(
		QSize(size, size) * style::DevicePixelRatio(),
		QImage::Format_ARGB32_Premultiplied);
	cursor.setDevicePixelRatio(style::DevicePixelRatio());
	cursor.fill(Qt::transparent);
	{
		auto p = QPainter(&cursor);
		PainterHighQualityEnabler hq(p);

		p.setBrush(Qt::NoBrush);
		auto pen = QPen(Qt::white);
		pen.setWidth(3 * line);
		p.setPen(pen);
		p.drawEllipse(diff, diff, diameter, diameter);
		pen = QPen(Qt::black);
		pen.setWidth(line);
		p.setPen(pen);
		p.drawEllipse(diff, diff, diameter, diameter);
	}
	return QCursor(QPixmap::fromImage(cursor));
}

ColorEditor::Picker::Picker(QWidget *parent, Mode mode, QColor color)
: TWidget(parent)
, _mode(mode) {
	setCursor(generateCursor());

	const auto size = QSize(st::colorPickerSize, st::colorPickerSize);
	resize(size);

	_palette = QImage(
		size * style::DevicePixelRatio(),
		QImage::Format_ARGB32_Premultiplied);

	setFromColor(color);
}

void ColorEditor::Picker::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);

	preparePalette();

	p.drawImage(0, 0, _palette);

	const auto left = anim::color(_topleft, _bottomleft, _y);
	const auto right = anim::color(_topright, _bottomright, _y);
	const auto color = anim::color(left, right, _x);
	const auto lightness = 0.2989 * color.redF()
		+ 0.5870 * color.greenF()
		+ 0.1140 * color.blueF();
	auto pen = QPen((lightness > 0.6)
		? QColor(0, 0, 0)
		: QColor(255, 255, 255));
	pen.setWidth(st::colorPickerMarkLine);
	p.setPen(pen);
	p.setBrush(Qt::NoBrush);

	const auto x = anim::interpolate(0, width() - 1, _x);
	const auto y = anim::interpolate(0, height() - 1, _y);
	PainterHighQualityEnabler hq(p);

	p.drawEllipse(QRect(x, y, 0, 0) + Margins(st::colorPickerMarkRadius));
}

void ColorEditor::Picker::mousePressEvent(QMouseEvent *e) {
	_choosing = true;
	updateCurrentPoint(e->pos());
}

void ColorEditor::Picker::mouseMoveEvent(QMouseEvent *e) {
	if (_choosing) {
		updateCurrentPoint(e->pos());
	}
}

void ColorEditor::Picker::mouseReleaseEvent(QMouseEvent *e) {
	_choosing = false;
}

void ColorEditor::Picker::preparePalette() {
	if (!_paletteInvalidated) return;
	_paletteInvalidated = false;

	if (_mode == Mode::RGBA) {
		preparePaletteRGBA();
	} else {
		preparePaletteHSL();
	}
	_palette.setDevicePixelRatio(style::DevicePixelRatio());
}

void ColorEditor::Picker::preparePaletteRGBA() {
	const auto size = _palette.width();
	auto ints = reinterpret_cast<uint32*>(_palette.bits());
	const auto intsAddPerLine = (_palette.bytesPerLine()
			- size * sizeof(uint32))
		/ sizeof(uint32);

	constexpr auto kLarge = 1024 * 1024;
	constexpr auto kLargeBit = 20; // n / kLarge == (n >> kLargeBit)
	const auto part = kLarge / size;

	const auto topleft = anim::shifted(_topleft);
	const auto topright = anim::shifted(_topright);
	const auto bottomleft = anim::shifted(_bottomleft);
	const auto bottomright = anim::shifted(_bottomright);

	auto yAccumulated = 0;
	for (auto y = 0; y != size; ++y, yAccumulated += part) {
		// (yAccumulated * 256) / kLarge;
		const auto yRatio = yAccumulated >> (kLargeBit - 8);
		// 0 <= yAccumulated < kLarge
		// 0 <= yRatio < 256

		const auto topRatio = 256 - yRatio;
		const auto bottomRatio = yRatio;

		const auto left = anim::reshifted(bottomleft * bottomRatio
			+ topleft * topRatio);
		const auto right = anim::reshifted(bottomright * bottomRatio
			+ topright * topRatio);

		auto xAccumulated = 0;
		for (auto x = 0; x != size; ++x, xAccumulated += part) {
			// (xAccumulated * 256) / kLarge;
			const auto xRatio = xAccumulated >> (kLargeBit - 8);
			// 0 <= xAccumulated < kLarge
			// 0 <= xRatio < 256

			const auto leftRatio = 256 - xRatio;
			const auto rightRatio = xRatio;

			*ints++ = anim::unshifted(left * leftRatio + right * rightRatio);
		}
		ints += intsAddPerLine;
	}
}

void ColorEditor::Picker::preparePaletteHSL() {
	const auto size = _palette.width();
	const auto intsAddPerLine = (_palette.bytesPerLine()
			- size * sizeof(uint32))
		/ sizeof(uint32);
	auto ints = reinterpret_cast<uint32*>(_palette.bits());

	constexpr auto kLarge = 1024 * 1024;
	constexpr auto kLargeBit = 20; // n / kLarge == (n >> kLargeBit)
	const auto part = kLarge / size;

	const auto lightness = _topleft.lightness();
	const auto right = anim::shifted(_bottomright);

	for (auto y = 0; y != size; ++y) {
		const auto hue = y * 360 / size;
		const auto color = QColor::fromHsl(hue, 255, lightness).toRgb();
		const auto left = anim::shifted(anim::getPremultiplied(color));

		auto xAccumulated = 0;
		for (auto x = 0; x != size; ++x, xAccumulated += part) {
			// (xAccumulated * 256) / kLarge;
			const auto xRatio = xAccumulated >> (kLargeBit - 8);
			// 0 <= xAccumulated < kLarge
			// 0 <= xRatio < 256

			const auto leftRatio = 256 - xRatio;
			const auto rightRatio = xRatio;
			*ints++ = anim::unshifted(left * leftRatio + right * rightRatio);
		}
		ints += intsAddPerLine;
	}

	_palette = std::move(_palette).transformed(
		QTransform(0, 1, 1, 0, 0, 0));
}

void ColorEditor::Picker::updateCurrentPoint(QPoint localPosition) {
	const auto x = std::clamp(localPosition.x(), 0, width())
		/ float64(width());
	const auto y = std::clamp(localPosition.y(), 0, height())
		/ float64(height());
	if (_x != x || _y != y) {
		_x = x;
		_y = y;
		update();
		_changed.fire({});
	}
}

void ColorEditor::Picker::setHSB(HSB hsb) {
	if (_mode == Mode::RGBA) {
		_topleft = QColor(255, 255, 255);
		_topright.setHsv(std::max(0, hsb.hue), 255, 255);
		_topright = _topright.toRgb();
		_bottomleft = _bottomright = QColor(0, 0, 0);

		_x = std::clamp(hsb.saturation / 255., 0., 1.);
		_y = 1. - std::clamp(hsb.brightness / 255., 0., 1.);
	} else {
		_topleft = _topright = QColor::fromHsl(0, 255, hsb.brightness);
		_bottomleft = _bottomright = QColor::fromHsl(0, 0, hsb.brightness);

		_x = std::clamp(hsb.hue / 360., 0., 1.);
		_y = 1. - std::clamp(hsb.saturation / 255., 0., 1.);
	}

	_paletteInvalidated = true;
	update();
}

void ColorEditor::Picker::setRGB(int red, int green, int blue) {
	setFromColor(QColor(red, green, blue));
}

void ColorEditor::Picker::setFromColor(QColor color) {
	if (_mode == Mode::RGBA) {
		setHSB({ color.hsvHue(), color.hsvSaturation(), color.value() });
	} else {
		setHSB({ color.hslHue(), color.hslSaturation(), color.lightness() });
	}
}

class ColorEditor::Slider : public TWidget {
public:
	enum class Direction {
		Horizontal,
		Vertical,
	};
	enum class Type {
		Hue,
		Opacity,
		Lightness
	};
	Slider(QWidget *parent, Direction direction, Type type, QColor color);

	rpl::producer<> changed() const {
		return _changed.events();
	}
	float64 value() const {
		return _value;
	}
	void setValue(float64 value) {
		_value = std::clamp(value, 0., 1.);
		update();
	}
	void setHSB(HSB hsb);
	void setRGB(int red, int green, int blue);
	void setAlpha(int alpha);

	void setLightnessLimits(int min, int max);

protected:
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

	void mousePressEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;

private:
	float64 valueFromColor(QColor color) const;
	float64 valueFromHue(int hue) const;
	bool isHorizontal() const {
		return (_direction == Direction::Horizontal);
	}
	void colorUpdated();
	void prepareMinSize();
	void generatePixmap();
	void updatePixmapFromMask();
	void updateCurrentPoint(QPoint localPosition);
	[[nodiscard]] QColor applyLimits(QColor color) const;

	Direction _direction = Direction::Horizontal;
	Type _type = Type::Hue;

	int _lightnessMin = 0;
	int _lightnessMax = 255;

	QColor _color;
	float64 _value = 0;

	QImage _mask;
	QPixmap _pixmap;
	QBrush _transparent;

	bool _choosing = false;
	rpl::event_stream<> _changed;

};

ColorEditor::Slider::Slider(
	QWidget *parent,
	Direction direction,
	Type type,
	QColor color)
: TWidget(parent)
, _direction(direction)
, _type(type)
, _color(color.red(), color.green(), color.blue())
, _value(valueFromColor(color))
, _transparent((_type == Type::Opacity)
		? style::TransparentPlaceholder()
		: QBrush()) {
	prepareMinSize();
}

void ColorEditor::Slider::prepareMinSize() {
	const auto minSize = st::colorSliderSkip * 2 + st::colorSliderWidth;
	resize(minSize, minSize);
}

void ColorEditor::Slider::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	const auto to = rect() - Margins(st::colorSliderSkip);
	Ui::Shadow::paint(p, to, width(), st::defaultRoundShadow);
	if (_type == Type::Opacity) {
		p.fillRect(to, _transparent);
	}
	p.drawPixmap(to, _pixmap, _pixmap.rect());
	if (isHorizontal()) {
		const auto x = st::colorSliderSkip + std::round(_value * to.width());
		st::colorSliderArrowTop.paint(
			p,
			x - st::colorSliderArrowTop.width() / 2,
			0,
			width());
		st::colorSliderArrowBottom.paint(
			p,
			x - st::colorSliderArrowBottom.width() / 2,
			height() - st::colorSliderArrowBottom.height(),
			width());
	} else {
		const auto y = st::colorSliderSkip + std::round(_value * to.height());
		st::colorSliderArrowLeft.paint(
			p,
			0,
			y - st::colorSliderArrowLeft.height() / 2,
			width());
		st::colorSliderArrowRight.paint(
			p,
			width() - st::colorSliderArrowRight.width(),
			y - st::colorSliderArrowRight.height() / 2,
			width());
	}
}

void ColorEditor::Slider::resizeEvent(QResizeEvent *e) {
	generatePixmap();
	update();
}

void ColorEditor::Slider::mousePressEvent(QMouseEvent *e) {
	_choosing = true;
	updateCurrentPoint(e->pos());
}

void ColorEditor::Slider::mouseMoveEvent(QMouseEvent *e) {
	if (_choosing) {
		updateCurrentPoint(e->pos());
	}
}

void ColorEditor::Slider::mouseReleaseEvent(QMouseEvent *e) {
	_choosing = false;
}

void ColorEditor::Slider::generatePixmap() {
	const auto size = (isHorizontal() ? width() : height())
		* style::DevicePixelRatio();
	auto image = QImage(
		size,
		style::DevicePixelRatio(),
		QImage::Format_ARGB32_Premultiplied);
	image.setDevicePixelRatio(style::DevicePixelRatio());
	auto ints = reinterpret_cast<uint32*>(image.bits());
	const auto intsPerLine = image.bytesPerLine() / sizeof(uint32);
	const auto intsPerLineAdded = intsPerLine - size;

	constexpr auto kLarge = 1024 * 1024;
	constexpr auto kLargeBit = 20; // n / kLarge == (n >> kLargeBit)
	const auto part = kLarge / size;

	if (_type == Type::Hue) {
		for (auto x = 0; x != size; ++x) {
			const auto color = QColor::fromHsv(x * 360 / size, 255, 255);
			const auto value = anim::getPremultiplied(color.toRgb());
			for (auto y = 0; y != style::DevicePixelRatio(); ++y) {
				ints[y * intsPerLine] = value;
			}
			++ints;
		}
		if (!isHorizontal()) {
			image = std::move(image).transformed(
				QTransform(0, -1, 1, 0, 0, 0));
		}
		_pixmap = Ui::PixmapFromImage(std::move(image));
	} else if (_type == Type::Opacity) {
		auto color = anim::shifted(QColor(255, 255, 255, 255));
		auto transparent = anim::shifted(QColor(255, 255, 255, 0));
		for (auto y = 0; y != style::DevicePixelRatio(); ++y) {
			auto xAccumulated = 0;
			for (auto x = 0; x != size; ++x, xAccumulated += part) {
				const auto xRatio = xAccumulated >> (kLargeBit - 8);
				// 0 <= xAccumulated < kLarge
				// 0 <= xRatio < 256

				*ints++ = anim::unshifted(color * xRatio
					+ transparent * (256 - xRatio));
			}
			ints += intsPerLineAdded;
		}
		if (!isHorizontal()) {
			image = std::move(image).transformed(
				QTransform(0, -1, 1, 0, 0, 0));
		}
		_mask = std::move(image);
		updatePixmapFromMask();
	} else {
		const auto range = _lightnessMax - _lightnessMin;
		for (auto x = 0; x != size; ++x) {
			const auto color = QColor::fromHsl(
				_color.hslHue(),
				_color.hslSaturation(),
				_lightnessMin + x * range / size);
			const auto value = anim::getPremultiplied(color.toRgb());
			for (auto y = 0; y != style::DevicePixelRatio(); ++y) {
				ints[y * intsPerLine] = value;
			}
			++ints;
		}
		if (!isHorizontal()) {
			image = std::move(image).transformed(
				QTransform(0, -1, 1, 0, 0, 0));
		}
		_pixmap = Ui::PixmapFromImage(std::move(image));
	}
}

void ColorEditor::Slider::setHSB(HSB hsb) {
	if (_type == Type::Hue) {
		// hue == 360 converts to 0 if done in general way
		_value = valueFromHue(hsb.hue);
		update();
	} else if (_type == Type::Opacity) {
		_color.setHsv(hsb.hue, hsb.saturation, hsb.brightness);
		colorUpdated();
	} else {
		_color.setHsl(
			hsb.hue,
			hsb.saturation,
			std::clamp(hsb.brightness, _lightnessMin, _lightnessMax));
		colorUpdated();
	}
}

void ColorEditor::Slider::setRGB(int red, int green, int blue) {
	_color = applyLimits(QColor(red, green, blue));
	colorUpdated();
}

void ColorEditor::Slider::colorUpdated() {
	if (_type == Type::Hue) {
		_value = valueFromColor(_color);
	} else if (!_mask.isNull()) {
		updatePixmapFromMask();
	} else {
		_value = valueFromColor(_color);
		generatePixmap();
	}
	update();
}

float64 ColorEditor::Slider::valueFromColor(QColor color) const {
	return (_type == Type::Hue)
		? valueFromHue(color.hsvHue())
		: (_type == Type::Opacity)
		? color.alphaF()
		: std::clamp(
			((color.lightness() - _lightnessMin)
				/ float64(_lightnessMax - _lightnessMin)),
			0.,
			1.);
}

float64 ColorEditor::Slider::valueFromHue(int hue) const {
	return (1. - std::clamp(hue, 0, 360) / 360.);
}

void ColorEditor::Slider::setAlpha(int alpha) {
	if (_type == Type::Opacity) {
		_value = std::clamp(alpha, 0, 255) / 255.;
		update();
	}
}

void ColorEditor::Slider::setLightnessLimits(int min, int max) {
	Expects(max > min);

	_lightnessMin = min;
	_lightnessMax = max;
	_color = applyLimits(_color);
	colorUpdated();
}

void ColorEditor::Slider::updatePixmapFromMask() {
	_pixmap = Ui::PixmapFromImage(style::colorizeImage(_mask, _color));
}

void ColorEditor::Slider::updateCurrentPoint(QPoint localPosition) {
	const auto coord = (isHorizontal()
		? localPosition.x()
		: localPosition.y()) - st::colorSliderSkip;
	const auto maximum = (isHorizontal()
		? width()
		: height()) - 2 * st::colorSliderSkip;
	const auto value = std::clamp(coord, 0, maximum) / float64(maximum);
	if (_value != value) {
		_value = value;
		update();
		_changed.fire({});
	}
}

QColor ColorEditor::Slider::applyLimits(QColor color) const {
	if (_type != Type::Lightness) {
		return color;
	}

	const auto lightness = color.lightness();
	const auto clamped = std::clamp(lightness, _lightnessMin, _lightnessMax);
	if (clamped == lightness) {
		return color;
	}
	return QColor::fromHsl(color.hslHue(), color.hslSaturation(), clamped);
}

class ColorEditor::Field : public Ui::MaskedInputField {
public:
	Field(
		QWidget *parent,
		const style::InputField &st,
		const QString &placeholder,
		int limit,
		const QString &units = QString());

	int value() const {
		return getLastText().toInt();
	}

	void setTextWithFocus(const QString &text) {
		setText(text);
		if (hasFocus()) {
			selectAll();
		}
	}

protected:
	void correctValue(
		const QString &was,
		int wasCursor,
		QString &now,
		int &nowCursor) override;
	void paintAdditionalPlaceholder(QPainter &p) override;

	void wheelEvent(QWheelEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;

private:
	void changeValue(int delta);

	QString _placeholder, _units;
	int _limit = 0;
	int _digitLimit = 1;
	int _wheelDelta = 0;

};

ColorEditor::Field::Field(
	QWidget *parent,
	const style::InputField &st,
	const QString &placeholder,
	int limit,
	const QString &units)
: Ui::MaskedInputField(parent, st)
, _placeholder(placeholder)
, _units(units)
, _limit(limit)
, _digitLimit(QString::number(_limit).size()) {
}

void ColorEditor::Field::correctValue(
		const QString &was,
		int wasCursor,
		QString &now,
		int &nowCursor) {
	const auto oldPos = nowCursor;
	const auto oldLen = now.length();
	auto newText = QString();
	auto newPos = -1;

	newText.reserve(oldLen);
	for (int i = 0; i < oldLen; ++i) {
		if (i == oldPos) {
			newPos = newText.length();
		}

		const auto ch = (now[i]);
		if (ch.isDigit()) {
			newText += ch;
		}
		if (newText.size() >= _digitLimit) {
			break;
		}
	}
	if (newPos < 0 || newPos > newText.size()) {
		newPos = newText.size();
	}
	if (newText.toInt() > _limit) {
		newText = QString::number(_limit);
		newPos = newText.size();
	}
	if (newText != now) {
		now = newText;
		setText(now);
		startPlaceholderAnimation();
		nowCursor = -1;
	}
	if (newPos != nowCursor) {
		nowCursor = newPos;
		setCursorPosition(nowCursor);
	}
}

void ColorEditor::Field::paintAdditionalPlaceholder(QPainter &p) {
	p.setFont(_st.style.font);
	p.setPen(_st.placeholderFg);
	const auto inner = QRect(
		_st.textMargins.right(),
		_st.textMargins.top(),
		width() - 2 * _st.textMargins.right(),
		height() - rect::m::sum::v(_st.textMargins));
	p.drawText(inner, _placeholder, style::al_topleft);
	if (!_units.isEmpty()) {
		p.drawText(inner, _units, style::al_topright);
	}
}

void ColorEditor::Field::wheelEvent(QWheelEvent *e) {
	if (!hasFocus()) {
		return;
	}

	auto deltaX = e->angleDelta().x();
	auto deltaY = e->angleDelta().y();
	if (Platform::IsMac()) {
		deltaY *= -1;
	} else {
		deltaX *= -1;
	}
	_wheelDelta += (std::abs(deltaX) > std::abs(deltaY)) ? deltaX : deltaY;

	constexpr auto kStep = 5;
	if (const auto delta = _wheelDelta / kStep) {
		_wheelDelta -= delta * kStep;
		changeValue(delta);
	}
}

void ColorEditor::Field::changeValue(int delta) {
	const auto currentValue = value();
	const auto newValue = std::clamp(currentValue + delta, 0, _limit);
	if (newValue != currentValue) {
		setText(QString::number(newValue));
		setFocus();
		selectAll();
		changed();
	}
}

void ColorEditor::Field::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Up) {
		changeValue(1);
	} else if (e->key() == Qt::Key_Down) {
		changeValue(-1);
	} else {
		MaskedInputField::keyPressEvent(e);
	}
}

class ColorEditor::ResultField : public Ui::MaskedInputField {
public:
	ResultField(QWidget *parent, const style::InputField &st);

	void setTextWithFocus(const QString &text) {
		setText(text);
		if (hasFocus()) {
			selectAll();
		}
	}

protected:
	void correctValue(
		const QString &was,
		int wasCursor,
		QString &now,
		int &nowCursor) override;
	void paintAdditionalPlaceholder(QPainter &p) override;

};

ColorEditor::ResultField::ResultField(
	QWidget *parent,
	const style::InputField &st)
: Ui::MaskedInputField(parent, st) {
}

void ColorEditor::ResultField::correctValue(
		const QString &was,
		int wasCursor,
		QString &now,
		int &nowCursor) {
	const auto oldPos = nowCursor;
	const auto oldLen = now.length();
	auto newText = QString();
	auto newPos = -1;

	newText.reserve(oldLen);
	for (auto i = 0; i < oldLen; ++i) {
		if (i == oldPos) {
			newPos = newText.length();
		}

		const auto ch = (now[i]);
		const auto code = ch.unicode();
		if ((code >= '0' && code <= '9')
			|| (ch >= 'a' && ch <= 'f')
			|| (ch >= 'A' && ch <= 'F')) {
			newText += ch;
		}
		if (newText.size() >= 8) {
			break;
		}
	}
	if (newPos < 0 || newPos > newText.size()) {
		newPos = newText.size();
	}
	if (newText != now) {
		now = newText;
		setText(now);
		startPlaceholderAnimation();
		nowCursor = -1;
	}
	if (newPos != nowCursor) {
		nowCursor = newPos;
		setCursorPosition(nowCursor);
	}
}

void ColorEditor::ResultField::paintAdditionalPlaceholder(QPainter &p) {
	p.setFont(_st.style.font);
	p.setPen(_st.placeholderFg);
	p.drawText(
		QRect(
			_st.textMargins.right(),
			_st.textMargins.top(),
			width(),
			height() - rect::m::sum::v(_st.textMargins)),
		"#",
		style::al_topleft);
}

ColorEditor::ColorEditor(
	QWidget *parent,
	Mode mode,
	QColor current)
: RpWidget()
, _mode(mode)
, _picker(this, mode, current)
, _hueField(this, st::colorValueInput, "H", 360, QString(QChar(176))) // degree character
, _saturationField(this, st::colorValueInput, "S", 100, "%")
, _brightnessField(
	this,
	st::colorValueInput,
	(mode == Mode::RGBA) ? "B" : "L", 100, "%")
, _redField(this, st::colorValueInput, "R", 255)
, _greenField(this, st::colorValueInput, "G", 255)
, _blueField(this, st::colorValueInput, "B", 255)
, _result(this, st::colorResultInput)
, _transparent(style::TransparentPlaceholder())
, _current(current)
, _new(current) {
	if (_mode == Mode::RGBA) {
		_hueSlider.create(
			this,
			Slider::Direction::Vertical,
			Slider::Type::Hue,
			current);
		_opacitySlider.create(
			this,
			Slider::Direction::Horizontal,
			Slider::Type::Opacity,
			current);
	} else if (_mode == Mode::HSL) {
		_lightnessSlider.create(
			this,
			Slider::Direction::Horizontal,
			Slider::Type::Lightness,
			current);
	}
	prepare();
}

void ColorEditor::setLightnessLimits(int min, int max) {
	Expects(_mode == Mode::HSL);

	_lightnessMin = min;
	_lightnessMax = max;
	_lightnessSlider->setLightnessLimits(min, max);

	const auto adjusted = applyLimits(_new);
	if (_new != adjusted) {
		updateFromColor(adjusted);
	}
}

void ColorEditor::prepare() {
	const auto hsbChanged = [=] { updateFromHSBFields(); };
	const auto rgbChanged = [=] { updateFromRGBFields(); };
	connect(_hueField, &Ui::MaskedInputField::changed, hsbChanged);
	connect(_saturationField, &Ui::MaskedInputField::changed, hsbChanged);
	connect(_brightnessField, &Ui::MaskedInputField::changed, hsbChanged);
	connect(_redField, &Ui::MaskedInputField::changed, rgbChanged);
	connect(_greenField, &Ui::MaskedInputField::changed, rgbChanged);
	connect(_blueField, &Ui::MaskedInputField::changed, rgbChanged);
	connect(_result, &Ui::MaskedInputField::changed, [=] {
		updateFromResultField();
	});

	const auto submitted = [=] { fieldSubmitted(); };
	connect(_hueField, &Ui::MaskedInputField::submitted, submitted);
	connect(_saturationField, &Ui::MaskedInputField::submitted, submitted);
	connect(_brightnessField, &Ui::MaskedInputField::submitted, submitted);
	connect(_redField, &Ui::MaskedInputField::submitted, submitted);
	connect(_greenField, &Ui::MaskedInputField::submitted, submitted);
	connect(_blueField, &Ui::MaskedInputField::submitted, submitted);
	connect(_result, &Ui::MaskedInputField::submitted, submitted);

	const auto height = st::colorEditSkip
		+ st::colorPickerSize
		+ st::colorEditSkip
		+ st::colorSliderWidth
		+ st::colorEditSkip;
	resize(st::colorEditWidth, height);

	rpl::merge(
		_picker->changed(),
		(_hueSlider ? _hueSlider->changed() : rpl::never<>()),
		(_opacitySlider ? _opacitySlider->changed() : rpl::never<>()),
		(_lightnessSlider ? _lightnessSlider->changed() : rpl::never<>())
	) | rpl::start_with_next([=] {
		updateFromControls();
	}, lifetime());

	updateRGBFields();
	updateHSBFields();
	updateResultField();
	update();
}

void ColorEditor::setInnerFocus() const {
	_result->setFocus();
	_result->selectAll();
}

QColor ColorEditor::color() const {
	return _new.toRgb();
}

rpl::producer<QColor> ColorEditor::colorValue() const {
	return _newChanges.events_starting_with_copy(_new);
}

rpl::producer<> ColorEditor::submitRequests() const {
	return _submitRequests.events();
}

void ColorEditor::fieldSubmitted() {
	const auto fields = std::array<Ui::MaskedInputField*, 7>{ {
		_hueField,
		_saturationField,
		_brightnessField,
		_redField,
		_greenField,
		_blueField,
		_result,
	} };
	for (auto i = 0, count = int(fields.size()); i + 1 != count; ++i) {
		if (fields[i]->hasFocus()) {
			fields[i + 1]->setFocus();
			fields[i + 1]->selectAll();
			return;
		}
	}
	if (_result->hasFocus()) {
		_submitRequests.fire({});
	}
}

void ColorEditor::updateHSBFields() {
	const auto hsb = hsbFromControls();
	_hueField->setTextWithFocus(QString::number(hsb.hue));
	_saturationField->setTextWithFocus(
		QString::number(percentFromByte(hsb.saturation)));
	_brightnessField->setTextWithFocus(
		QString::number(percentFromByte(hsb.brightness)));
}

void ColorEditor::updateRGBFields() {
	_redField->setTextWithFocus(QString::number(_new.red()));
	_greenField->setTextWithFocus(QString::number(_new.green()));
	_blueField->setTextWithFocus(QString::number(_new.blue()));
}

void ColorEditor::updateResultField() {
	auto text = QString();
	const auto addHex = [&text](int value) {
		if (value >= 0 && value <= 9) {
			text.append(QChar('0' + value));
		} else if (value >= 10 && value <= 15) {
			text.append(QChar('a' + (value - 10)));
		}
	};
	const auto addValue = [&](int value) {
		addHex(value / 16);
		addHex(value % 16);
	};
	addValue(_new.red());
	addValue(_new.green());
	addValue(_new.blue());
	if (_new.alpha() != 255) {
		addValue(_new.alpha());
	}
	_result->setTextWithFocus(text);
}

void ColorEditor::resizeEvent(QResizeEvent *e) {
	const auto fullwidth = _picker->width()
		+ ((_mode == Mode::RGBA)
			? (2 * (st::colorEditSkip - st::colorSliderSkip)
				+ _hueSlider->width())
			: (2 * st::colorEditSkip))
		+ st::colorSampleSize.width();
	const auto left = (width() - fullwidth) / 2;
	_picker->moveToLeft(left, st::colorEditSkip);
	if (_hueSlider) {
		_hueSlider->setGeometryToLeft(
			rect::right(_picker) + st::colorEditSkip - st::colorSliderSkip,
			st::colorEditSkip - st::colorSliderSkip,
			_hueSlider->width(),
			st::colorPickerSize + 2 * st::colorSliderSkip);
	}
	{
		const auto rectSlider = QRect(
			_picker->x() - st::colorSliderSkip,
			rect::bottom(_picker) + st::colorEditSkip - st::colorSliderSkip,
			_picker->width() + 2 * st::colorSliderSkip,
			0);
		if (_opacitySlider) {
			_opacitySlider->setGeometryToLeft(rectSlider
				+ QMargins(0, 0, 0, _opacitySlider->height()));
		}
		if (_lightnessSlider) {
			_lightnessSlider->setGeometryToLeft(rectSlider
				+ QMargins(0, 0, 0, _lightnessSlider->height()));
		}
	}
	const auto fieldLeft = (_mode == Mode::RGBA)
		? (rect::right(_hueSlider) + st::colorEditSkip - st::colorSliderSkip)
		: (rect::right(_picker) + st::colorEditSkip);
	const auto addWidth = (_mode == Mode::RGBA) ? 0 : st::colorEditSkip;
	const auto fieldWidth = st::colorSampleSize.width() + addWidth;
	const auto fieldHeight = _hueField->height();
	_newRect = QRect(
		fieldLeft,
		st::colorEditSkip,
		fieldWidth,
		st::colorSampleSize.height());
	_currentRect = _newRect.translated(0, st::colorSampleSize.height());
	{
		const auto fieldRect = QRect(fieldLeft, 0, fieldWidth, fieldHeight);
		_hueField->setGeometryToLeft(fieldRect.translated(
			0,
			rect::bottom(_currentRect) + st::colorFieldSkip));
		_saturationField->setGeometryToLeft(fieldRect.translated(
			0,
			rect::bottom(_hueField)));
		_brightnessField->setGeometryToLeft(fieldRect.translated(
			0,
			rect::bottom(_saturationField)));
		_redField->setGeometryToLeft(fieldRect.translated(
			0,
			rect::bottom(_brightnessField) + st::colorFieldSkip));
		_greenField->setGeometryToLeft(fieldRect.translated(
			0,
			rect::bottom(_redField)));
		_blueField->setGeometryToLeft(fieldRect.translated(
			0,
			rect::bottom(_greenField)));
	}
	const auto resultDelta = (_mode == Mode::RGBA)
		? (st::colorEditSkip + st::colorSliderWidth)
		: 0;
	const auto resultBottom = (_mode == Mode::RGBA)
		? (rect::bottom(_opacitySlider))
		: (rect::bottom(_lightnessSlider));
	_result->setGeometryToLeft(
		fieldLeft - resultDelta,
		resultBottom - st::colorSliderSkip - _result->height(),
		fieldWidth + resultDelta,
		fieldHeight);
}

void ColorEditor::paintEvent(QPaintEvent *e) {
	Painter p(this);
	Ui::Shadow::paint(
		p,
		_picker->geometry(),
		width(),
		st::defaultRoundShadow);

	Ui::Shadow::paint(
		p,
		_newRect + QMargins(0, 0, 0, _currentRect.height()),
		width(),
		st::defaultRoundShadow);
	if (_new.alphaF() < 1.) {
		p.fillRect(myrtlrect(_newRect), _transparent);
	}
	p.fillRect(myrtlrect(_newRect), _new);
	if (_current.alphaF() < 1.) {
		p.fillRect(myrtlrect(_currentRect), _transparent);
	}
	p.fillRect(myrtlrect(_currentRect), _current);
}

void ColorEditor::mousePressEvent(QMouseEvent *e) {
	if (myrtlrect(_currentRect).contains(e->pos())) {
		updateFromColor(_current);
	}
}

ColorEditor::HSB ColorEditor::hsbFromControls() const {
	const auto hue = (_mode == Mode::RGBA)
		? std::round((1. - _hueSlider->value()) * 360)
		: std::round(_picker->valueX() * 360);
	const auto saturation = (_mode == Mode::RGBA)
		? std::round(_picker->valueX() * 255)
		: std::round((1. - _picker->valueY()) * 255);
	const auto brightness = (_mode == Mode::RGBA)
		? std::round((1. - _picker->valueY()) * 255)
		: (_lightnessMin
			+ std::round(_lightnessSlider->value()
				* (_lightnessMax - _lightnessMin)));
	return { int(hue), int(saturation), int(brightness) };
}

QColor ColorEditor::applyLimits(QColor color) const {
	if (_mode != Mode::HSL) {
		return color;
	}

	const auto lightness = color.lightness();
	const auto clamped = std::clamp(lightness, _lightnessMin, _lightnessMax);
	if (clamped == lightness) {
		return color;
	}
	return QColor::fromHsl(color.hslHue(), color.hslSaturation(), clamped);
}

void ColorEditor::updateFromColor(QColor color) {
	_new = applyLimits(color);
	_newChanges.fire_copy(_new);
	updateControlsFromColor();
	updateRGBFields();
	updateHSBFields();
	updateResultField();
	update();
}

void ColorEditor::updateFromControls() {
	const auto hsb = hsbFromControls();
	const auto alpha = _opacitySlider
		? std::round(_opacitySlider->value() * 255)
		: 255;
	setHSB(hsb, alpha);
	updateHSBFields();
	updateControlsFromHSB(hsb);
}

void ColorEditor::updateFromHSBFields() {
	const auto hue = _hueField->value();
	const auto saturation = percentToByte(_saturationField->value());
	const auto brightness = std::clamp(
		percentToByte(_brightnessField->value()),
		_lightnessMin,
		_lightnessMax);
	const auto alpha = _opacitySlider
		? std::round(_opacitySlider->value() * 255)
		: 255;
	setHSB({ hue, saturation, brightness }, alpha);
	updateControlsFromHSB({ hue, saturation, brightness });
}

void ColorEditor::updateFromRGBFields() {
	const auto red = _redField->value();
	const auto blue = _blueField->value();
	const auto green = _greenField->value();
	const auto alpha = _opacitySlider
		? std::round(_opacitySlider->value() * 255)
		: 255;
	setRGB(red, green, blue, alpha);
	updateResultField();
}

void ColorEditor::updateFromResultField() {
	const auto text = _result->getLastText();
	if (text.size() != 6 && text.size() != 8) {
		return;
	}

	const auto fromHex = [](QChar hex) {
		auto code = hex.unicode();
		if (code >= 'A' && code <= 'F') {
			return (code - 'A' + 10);
		} else if (code >= 'a' && code <= 'f') {
			return (code - 'a' + 10);
		}
		return code - '0';
	};
	const auto fromChars = [&](QChar a, QChar b) {
		return fromHex(a) * 0x10 + fromHex(b);
	};
	const auto red = fromChars(text[0], text[1]);
	const auto green = fromChars(text[2], text[3]);
	const auto blue = fromChars(text[4], text[5]);
	const auto alpha = (text.size() == 8) ? fromChars(text[6], text[7]) : 255;
	setRGB(red, green, blue, alpha);
	updateRGBFields();
}

void ColorEditor::updateControlsFromHSB(HSB hsb) {
	_picker->setHSB(hsb);
	if (_hueSlider) {
		_hueSlider->setHSB(hsb);
	}
	if (_opacitySlider) {
		_opacitySlider->setHSB(hsb);
	}
	if (_lightnessSlider) {
		_lightnessSlider->setHSB(hsb);
	}
}

void ColorEditor::updateControlsFromColor() {
	const auto red = _new.red();
	const auto green = _new.green();
	const auto blue = _new.blue();
	const auto alpha = _new.alpha();
	_picker->setRGB(red, green, blue);
	if (_hueSlider) {
		_hueSlider->setRGB(red, green, blue);
	}
	if (_opacitySlider) {
		_opacitySlider->setRGB(red, green, blue);
		_opacitySlider->setAlpha(alpha);
	}
	if (_lightnessSlider) {
		_lightnessSlider->setRGB(red, green, blue);
	}
}

void ColorEditor::setHSB(HSB hsb, int alpha) {
	if (_mode == Mode::RGBA) {
		_new.setHsv(hsb.hue, hsb.saturation, hsb.brightness, alpha);
	} else {
		_new.setHsl(hsb.hue, hsb.saturation, hsb.brightness, alpha);
	}
	_newChanges.fire_copy(_new);
	updateRGBFields();
	updateResultField();
	update();
}

void ColorEditor::setRGB(int red, int green, int blue, int alpha) {
	_new = applyLimits(QColor(red, green, blue, alpha));
	_newChanges.fire_copy(_new);
	updateControlsFromColor();
	updateHSBFields();
	update();
}

void ColorEditor::showColor(QColor color) {
	updateFromColor(color);
}

void ColorEditor::setCurrent(QColor color) {
	_current = color;
	update();
}
