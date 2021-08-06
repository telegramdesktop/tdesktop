/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/edit_color_box.h"

#include "lang/lang_keys.h"
#include "ui/widgets/shadow.h"
#include "ui/widgets/input_fields.h"
#include "ui/ui_utility.h"
#include "base/platform/base_platform_info.h"
#include "styles/style_boxes.h"
#include "styles/style_media_view.h"

class EditColorBox::Picker : public TWidget {
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

QCursor EditColorBox::Picker::generateCursor() {
	auto diameter = style::ConvertScale(16);
	auto line = style::ConvertScale(1);
	auto size = ((diameter + 2 * line) >= 32) ? 64 : 32;
	auto cursor = QImage(QSize(size, size) * cIntRetinaFactor(), QImage::Format_ARGB32_Premultiplied);
	cursor.setDevicePixelRatio(cRetinaFactor());
	cursor.fill(Qt::transparent);
	{
		Painter p(&cursor);
		PainterHighQualityEnabler hq(p);

		p.setBrush(Qt::NoBrush);
		auto pen = QPen(Qt::white);
		pen.setWidth(3 * line);
		p.setPen(pen);
		p.drawEllipse((size - diameter) / 2, (size - diameter) / 2, diameter, diameter);
		pen = QPen(Qt::black);
		pen.setWidth(line);
		p.setPen(pen);
		p.drawEllipse((size - diameter) / 2, (size - diameter) / 2, diameter, diameter);
	}
	return QCursor(QPixmap::fromImage(cursor));
}

EditColorBox::Picker::Picker(QWidget *parent, Mode mode, QColor color)
: TWidget(parent)
, _mode(mode) {
	setCursor(generateCursor());

	auto size = QSize(st::colorPickerSize, st::colorPickerSize);
	resize(size);

	_palette = QImage(size * cIntRetinaFactor(), QImage::Format_ARGB32_Premultiplied);

	setFromColor(color);
}

void EditColorBox::Picker::paintEvent(QPaintEvent *e) {
	Painter p(this);

	preparePalette();

	p.drawImage(0, 0, _palette);

	auto left = anim::color(_topleft, _bottomleft, _y);
	auto right = anim::color(_topright, _bottomright, _y);
	auto color = anim::color(left, right, _x);
	auto lightness = 0.2989 * color.redF() + 0.5870 * color.greenF() + 0.1140 * color.blueF();
	auto pen = QPen((lightness > 0.6) ? QColor(0, 0, 0) : QColor(255, 255, 255));
	pen.setWidth(st::colorPickerMarkLine);
	p.setPen(pen);
	p.setBrush(Qt::NoBrush);

	auto x = anim::interpolate(0, width() - 1, _x);
	auto y = anim::interpolate(0, height() - 1, _y);
	PainterHighQualityEnabler hq(p);

	p.drawEllipse(QRect(x - st::colorPickerMarkRadius, y - st::colorPickerMarkRadius, 2 * st::colorPickerMarkRadius, 2 * st::colorPickerMarkRadius));
}

void EditColorBox::Picker::mousePressEvent(QMouseEvent *e) {
	_choosing = true;
	updateCurrentPoint(e->pos());
}

void EditColorBox::Picker::mouseMoveEvent(QMouseEvent *e) {
	if (_choosing) {
		updateCurrentPoint(e->pos());
	}
}

void EditColorBox::Picker::mouseReleaseEvent(QMouseEvent *e) {
	_choosing = false;
}

void EditColorBox::Picker::preparePalette() {
	if (!_paletteInvalidated) return;
	_paletteInvalidated = false;

	if (_mode == Mode::RGBA) {
		preparePaletteRGBA();
	} else {
		preparePaletteHSL();
	}
	_palette.setDevicePixelRatio(cRetinaFactor());
}

void EditColorBox::Picker::preparePaletteRGBA() {
	const auto size = _palette.width();
	auto ints = reinterpret_cast<uint32*>(_palette.bits());
	const auto intsAddPerLine = (_palette.bytesPerLine() - size * sizeof(uint32)) / sizeof(uint32);

	constexpr auto Large = 1024 * 1024;
	constexpr auto LargeBit = 20; // n / Large == (n >> LargeBit)
	const auto part = Large / size;

	const auto topleft = anim::shifted(_topleft);
	const auto topright = anim::shifted(_topright);
	const auto bottomleft = anim::shifted(_bottomleft);
	const auto bottomright = anim::shifted(_bottomright);

	auto y_accumulated = 0;
	for (auto y = 0; y != size; ++y, y_accumulated += part) {
		auto y_ratio = y_accumulated >> (LargeBit - 8); // (y_accumulated * 256) / Large;
		// 0 <= y_accumulated < Large
		// 0 <= y_ratio < 256

		const auto top_ratio = 256 - y_ratio;
		const auto bottom_ratio = y_ratio;

		const auto left = anim::reshifted(bottomleft * bottom_ratio + topleft * top_ratio);
		const auto right = anim::reshifted(bottomright * bottom_ratio + topright * top_ratio);

		auto x_accumulated = 0;
		for (auto x = 0; x != size; ++x, x_accumulated += part) {
			auto x_ratio = x_accumulated >> (LargeBit - 8); // (x_accumulated * 256) / Large;
			// 0 <= x_accumulated < Large
			// 0 <= x_ratio < 256

			auto left_ratio = 256 - x_ratio;
			auto right_ratio = x_ratio;

			*ints++ = anim::unshifted(left * left_ratio + right * right_ratio);
		}
		ints += intsAddPerLine;
	}
}

void EditColorBox::Picker::preparePaletteHSL() {
	const auto size = _palette.width();
	const auto intsAddPerLine = (_palette.bytesPerLine() - size * sizeof(uint32)) / sizeof(uint32);
	auto ints = reinterpret_cast<uint32*>(_palette.bits());

	constexpr auto Large = 1024 * 1024;
	constexpr auto LargeBit = 20; // n / Large == (n >> LargeBit)
	const auto part = Large / size;

	const auto lightness = _topleft.lightness();
	const auto right = anim::shifted(_bottomright);

	for (auto y = 0; y != size; ++y) {
		const auto hue = y * 360 / size;
		const auto color = QColor::fromHsl(hue, 255, lightness).toRgb();
		const auto left = anim::shifted(anim::getPremultiplied(color));

		auto x_accumulated = 0;
		for (auto x = 0; x != size; ++x, x_accumulated += part) {
			auto x_ratio = x_accumulated >> (LargeBit - 8); // (x_accumulated * 256) / Large;
			// 0 <= x_accumulated < Large
			// 0 <= x_ratio < 256

			auto left_ratio = 256 - x_ratio;
			auto right_ratio = x_ratio;
			*ints++ = anim::unshifted(left * left_ratio + right * right_ratio);
		}
		ints += intsAddPerLine;
	}

	_palette = std::move(_palette).transformed(
		QTransform(0, 1, 1, 0, 0, 0));
}

void EditColorBox::Picker::updateCurrentPoint(QPoint localPosition) {
	auto x = std::clamp(localPosition.x(), 0, width()) / float64(width());
	auto y = std::clamp(localPosition.y(), 0, height()) / float64(height());
	if (_x != x || _y != y) {
		_x = x;
		_y = y;
		update();
		_changed.fire({});
	}
}

void EditColorBox::Picker::setHSB(HSB hsb) {
	if (_mode == Mode::RGBA) {
		_topleft = QColor(255, 255, 255);
		_topright.setHsv(qMax(0, hsb.hue), 255, 255);
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

void EditColorBox::Picker::setRGB(int red, int green, int blue) {
	setFromColor(QColor(red, green, blue));
}

void EditColorBox::Picker::setFromColor(QColor color) {
	if (_mode == Mode::RGBA) {
		setHSB({ color.hsvHue(), color.hsvSaturation(), color.value() });
	} else {
		setHSB({ color.hslHue(), color.hslSaturation(), color.lightness() });
	}
}

class EditColorBox::Slider : public TWidget {
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

EditColorBox::Slider::Slider(
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

void EditColorBox::Slider::prepareMinSize() {
	auto minSize = st::colorSliderSkip + st::colorSliderWidth + st::colorSliderSkip;
	resize(minSize, minSize);
}

void EditColorBox::Slider::paintEvent(QPaintEvent *e) {
	Painter p(this);
	auto to = rect().marginsRemoved(QMargins(st::colorSliderSkip, st::colorSliderSkip, st::colorSliderSkip, st::colorSliderSkip));
	Ui::Shadow::paint(p, to, width(), st::defaultRoundShadow);
	if (_type == Type::Opacity) {
		p.fillRect(to, _transparent);
	}
	p.drawPixmap(to, _pixmap, _pixmap.rect());
	if (isHorizontal()) {
		auto x = st::colorSliderSkip + qRound(_value * to.width());
		st::colorSliderArrowTop.paint(p, x - st::colorSliderArrowTop.width() / 2, 0, width());
		st::colorSliderArrowBottom.paint(p, x - st::colorSliderArrowBottom.width() / 2, height() - st::colorSliderArrowBottom.height(), width());
	} else {
		auto y = st::colorSliderSkip + qRound(_value * to.height());
		st::colorSliderArrowLeft.paint(p, 0, y - st::colorSliderArrowLeft.height() / 2, width());
		st::colorSliderArrowRight.paint(p, width() - st::colorSliderArrowRight.width(), y - st::colorSliderArrowRight.height() / 2, width());
	}
}

void EditColorBox::Slider::resizeEvent(QResizeEvent *e) {
	generatePixmap();
	update();
}

void EditColorBox::Slider::mousePressEvent(QMouseEvent *e) {
	_choosing = true;
	updateCurrentPoint(e->pos());
}

void EditColorBox::Slider::mouseMoveEvent(QMouseEvent *e) {
	if (_choosing) {
		updateCurrentPoint(e->pos());
	}
}

void EditColorBox::Slider::mouseReleaseEvent(QMouseEvent *e) {
	_choosing = false;
}

void EditColorBox::Slider::generatePixmap() {
	auto size = (isHorizontal() ? width() : height()) * cIntRetinaFactor();
	auto image = QImage(size, cIntRetinaFactor(), QImage::Format_ARGB32_Premultiplied);
	image.setDevicePixelRatio(cRetinaFactor());
	auto ints = reinterpret_cast<uint32*>(image.bits());
	auto intsPerLine = image.bytesPerLine() / sizeof(uint32);
	auto intsPerLineAdded = intsPerLine - size;

	constexpr auto Large = 1024 * 1024;
	constexpr auto LargeBit = 20; // n / Large == (n >> LargeBit)
	auto part = Large / size;

	if (_type == Type::Hue) {
		for (auto x = 0; x != size; ++x) {
			const auto color = QColor::fromHsv(x * 360 / size, 255, 255);
			const auto value = anim::getPremultiplied(color.toRgb());
			for (auto y = 0; y != cIntRetinaFactor(); ++y) {
				ints[y * intsPerLine] = value;
			}
			++ints;
		}
		if (!isHorizontal()) {
			image = std::move(image).transformed(QTransform(0, -1, 1, 0, 0, 0));
		}
		_pixmap = Ui::PixmapFromImage(std::move(image));
	} else if (_type == Type::Opacity) {
		auto color = anim::shifted(QColor(255, 255, 255, 255));
		auto transparent = anim::shifted(QColor(255, 255, 255, 0));
		for (auto y = 0; y != cIntRetinaFactor(); ++y) {
			auto x_accumulated = 0;
			for (auto x = 0; x != size; ++x, x_accumulated += part) {
				auto x_ratio = x_accumulated >> (LargeBit - 8);
				// 0 <= x_accumulated < Large
				// 0 <= x_ratio < 256

				*ints++ = anim::unshifted(color * x_ratio + transparent * (256 - x_ratio));
			}
			ints += intsPerLineAdded;
		}
		if (!isHorizontal()) {
			image = std::move(image).transformed(QTransform(0, -1, 1, 0, 0, 0));
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
			for (auto y = 0; y != cIntRetinaFactor(); ++y) {
				ints[y * intsPerLine] = value;
			}
			++ints;
		}
		if (!isHorizontal()) {
			image = std::move(image).transformed(QTransform(0, -1, 1, 0, 0, 0));
		}
		_pixmap = Ui::PixmapFromImage(std::move(image));
	}
}

void EditColorBox::Slider::setHSB(HSB hsb) {
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

void EditColorBox::Slider::setRGB(int red, int green, int blue) {
	_color = applyLimits(QColor(red, green, blue));
	colorUpdated();
}

void EditColorBox::Slider::colorUpdated() {
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

float64 EditColorBox::Slider::valueFromColor(QColor color) const {
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

float64 EditColorBox::Slider::valueFromHue(int hue) const {
	return (1. - std::clamp(hue, 0, 360) / 360.);
}

void EditColorBox::Slider::setAlpha(int alpha) {
	if (_type == Type::Opacity) {
		_value = std::clamp(alpha, 0, 255) / 255.;
		update();
	}
}

void EditColorBox::Slider::setLightnessLimits(int min, int max) {
	Expects(max > min);

	_lightnessMin = min;
	_lightnessMax = max;
	_color = applyLimits(_color);
	colorUpdated();
}

void EditColorBox::Slider::updatePixmapFromMask() {
	_pixmap = Ui::PixmapFromImage(style::colorizeImage(_mask, _color));
}

void EditColorBox::Slider::updateCurrentPoint(QPoint localPosition) {
	auto coord = (isHorizontal() ? localPosition.x() : localPosition.y()) - st::colorSliderSkip;
	auto maximum = (isHorizontal() ? width() : height()) - 2 * st::colorSliderSkip;
	auto value = std::clamp(coord, 0, maximum) / float64(maximum);
	if (_value != value) {
		_value = value;
		update();
		_changed.fire({});
	}
}

QColor EditColorBox::Slider::applyLimits(QColor color) const {
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

class EditColorBox::Field : public Ui::MaskedInputField {
public:
	Field(QWidget *parent, const style::InputField &st, const QString &placeholder, int limit, const QString &units = QString());

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
	void correctValue(const QString &was, int wasCursor, QString &now, int &nowCursor) override;
	void paintAdditionalPlaceholder(Painter &p) override;

	void wheelEvent(QWheelEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;

private:
	void changeValue(int delta);

	QString _placeholder, _units;
	int _limit = 0;
	int _digitLimit = 1;
	int _wheelDelta = 0;

};

EditColorBox::Field::Field(QWidget *parent, const style::InputField &st, const QString &placeholder, int limit, const QString &units) : Ui::MaskedInputField(parent, st)
, _placeholder(placeholder)
, _units(units)
, _limit(limit)
, _digitLimit(QString::number(_limit).size()) {
}

void EditColorBox::Field::correctValue(const QString &was, int wasCursor, QString &now, int &nowCursor) {
	QString newText;
	int oldPos(nowCursor), newPos(-1), oldLen(now.length());

	newText.reserve(oldLen);
	for (int i = 0; i < oldLen; ++i) {
		if (i == oldPos) {
			newPos = newText.length();
		}

		QChar ch(now[i]);
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

void EditColorBox::Field::paintAdditionalPlaceholder(Painter &p) {
	p.setFont(_st.font);
	p.setPen(_st.placeholderFg);
	auto inner = QRect(_st.textMargins.right(), _st.textMargins.top(), width() - 2 * _st.textMargins.right(), height() - _st.textMargins.top() - _st.textMargins.bottom());
	p.drawText(inner, _placeholder, style::al_topleft);
	if (!_units.isEmpty()) {
		p.drawText(inner, _units, style::al_topright);
	}
}

void EditColorBox::Field::wheelEvent(QWheelEvent *e) {
	if (!hasFocus()) {
		return;
	}

	auto deltaX = e->angleDelta().x(), deltaY = e->angleDelta().y();
	if (Platform::IsMac()) {
		deltaY *= -1;
	} else {
		deltaX *= -1;
	}
	_wheelDelta += (qAbs(deltaX) > qAbs(deltaY)) ? deltaX : deltaY;

	constexpr auto step = 5;
	if (auto delta = _wheelDelta / step) {
		_wheelDelta -= delta * step;
		changeValue(delta);
	}
}

void EditColorBox::Field::changeValue(int delta) {
	auto currentValue = value();
	auto newValue = std::clamp(currentValue + delta, 0, _limit);
	if (newValue != currentValue) {
		setText(QString::number(newValue));
		setFocus();
		selectAll();
		changed();
	}
}

void EditColorBox::Field::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Up) {
		changeValue(1);
	} else if (e->key() == Qt::Key_Down) {
		changeValue(-1);
	} else {
		MaskedInputField::keyPressEvent(e);
	}
}

class EditColorBox::ResultField : public Ui::MaskedInputField {
public:
	ResultField(QWidget *parent, const style::InputField &st);

	void setTextWithFocus(const QString &text) {
		setText(text);
		if (hasFocus()) {
			selectAll();
		}
	}

protected:
	void correctValue(const QString &was, int wasCursor, QString &now, int &nowCursor) override;
	void paintAdditionalPlaceholder(Painter &p) override;

};

EditColorBox::ResultField::ResultField(QWidget *parent, const style::InputField &st) : Ui::MaskedInputField(parent, st) {
}

void EditColorBox::ResultField::correctValue(const QString &was, int wasCursor, QString &now, int &nowCursor) {
	QString newText;
	int oldPos(nowCursor), newPos(-1), oldLen(now.length());

	newText.reserve(oldLen);
	for (int i = 0; i < oldLen; ++i) {
		if (i == oldPos) {
			newPos = newText.length();
		}

		QChar ch(now[i]);
		auto code = ch.unicode();
		if ((code >= '0' && code <= '9') || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F')) {
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

void EditColorBox::ResultField::paintAdditionalPlaceholder(Painter &p) {
	p.setFont(_st.font);
	p.setPen(_st.placeholderFg);
	p.drawText(QRect(_st.textMargins.right(), _st.textMargins.top(), width(), height() - _st.textMargins.top() - _st.textMargins.bottom()), "#", style::al_topleft);
}

EditColorBox::EditColorBox(
	QWidget*,
	const QString &title,
	Mode mode,
	QColor current)
: BoxContent()
, _title(title)
, _mode(mode)
, _picker(this, mode, current)
, _hueField(this, st::colorValueInput, "H", 360, QString() + QChar(176)) // degree character
, _saturationField(this, st::colorValueInput, "S", 100, "%")
, _brightnessField(this, st::colorValueInput, (mode == Mode::RGBA) ? "B" : "L", 100, "%")
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
}

void EditColorBox::setLightnessLimits(int min, int max) {
	Expects(_mode == Mode::HSL);

	_lightnessMin = min;
	_lightnessMax = max;
	_lightnessSlider->setLightnessLimits(min, max);

	const auto adjusted = applyLimits(_new);
	if (_new != adjusted) {
		updateFromColor(adjusted);
	}
}

void EditColorBox::prepare() {
	setTitle(rpl::single(_title));

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

	addButton(tr::lng_settings_save(), [=] { saveColor(); });
	addButton(tr::lng_cancel(), [=] { closeBox(); });

	auto height = st::colorEditSkip + st::colorPickerSize + st::colorEditSkip + st::colorSliderWidth + st::colorEditSkip;
	setDimensions(st::colorEditWidth, height);

	rpl::merge(
		_picker->changed(),
		(_hueSlider ? _hueSlider->changed() : rpl::never<>()),
		(_opacitySlider ? _opacitySlider->changed() : rpl::never<>()),
		(_lightnessSlider ? _lightnessSlider->changed() : rpl::never<>())
	) | rpl::start_with_next([=] {
		updateFromControls();
	}, lifetime());

	boxClosing() | rpl::start_with_next([=] {
		if (_cancelCallback) {
			_cancelCallback();
		}
	}, lifetime());

	updateRGBFields();
	updateHSBFields();
	updateResultField();
	update();
}

void EditColorBox::setInnerFocus() {
	_result->setFocus();
	_result->selectAll();
}

void EditColorBox::fieldSubmitted() {
	Ui::MaskedInputField *fields[] = {
		_hueField,
		_saturationField,
		_brightnessField,
		_redField,
		_greenField,
		_blueField,
		_result
	};
	for (auto i = 0, count = int(base::array_size(fields)); i + 1 != count; ++i) {
		if (fields[i]->hasFocus()) {
			fields[i + 1]->setFocus();
			fields[i + 1]->selectAll();
			return;
		}
	}
	if (_result->hasFocus()) {
		saveColor();
	}
}

void EditColorBox::saveColor() {
	const auto weak = Ui::MakeWeak(this);
	_cancelCallback = nullptr;
	if (_saveCallback) {
		_saveCallback(_new.toRgb());
	}
	if (weak) {
		closeBox();
	}
}

void EditColorBox::updateHSBFields() {
	const auto hsb = hsbFromControls();
	_hueField->setTextWithFocus(QString::number(hsb.hue));
	_saturationField->setTextWithFocus(QString::number(percentFromByte(hsb.saturation)));
	_brightnessField->setTextWithFocus(QString::number(percentFromByte(hsb.brightness)));
}

void EditColorBox::updateRGBFields() {
	_redField->setTextWithFocus(QString::number(_new.red()));
	_greenField->setTextWithFocus(QString::number(_new.green()));
	_blueField->setTextWithFocus(QString::number(_new.blue()));
}

void EditColorBox::updateResultField() {
	auto text = QString();
	auto addHex = [&text](int value) {
		if (value >= 0 && value <= 9) {
			text.append('0' + value);
		} else if (value >= 10 && value <= 15) {
			text.append('a' + (value - 10));
		}
	};
	auto addValue = [addHex](int value) {
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

void EditColorBox::resizeEvent(QResizeEvent *e) {
	const auto fullwidth = _picker->width()
		+ ((_mode == Mode::RGBA)
			? (2 * (st::colorEditSkip - st::colorSliderSkip) + _hueSlider->width())
			: (2 * st::colorEditSkip))
		+ st::colorSampleSize.width();
	auto left = (width() - fullwidth) / 2;
	_picker->moveToLeft(left, st::colorEditSkip);
	if (_hueSlider) {
		_hueSlider->setGeometryToLeft(_picker->x() + _picker->width() + st::colorEditSkip - st::colorSliderSkip, st::colorEditSkip - st::colorSliderSkip, _hueSlider->width(), st::colorPickerSize + 2 * st::colorSliderSkip);
	}
	if (_opacitySlider) {
		_opacitySlider->setGeometryToLeft(_picker->x() - st::colorSliderSkip, _picker->y() + _picker->height() + st::colorEditSkip - st::colorSliderSkip, _picker->width() + 2 * st::colorSliderSkip, _opacitySlider->height());
	}
	if (_lightnessSlider) {
		_lightnessSlider->setGeometryToLeft(_picker->x() - st::colorSliderSkip, _picker->y() + _picker->height() + st::colorEditSkip - st::colorSliderSkip, _picker->width() + 2 * st::colorSliderSkip, _lightnessSlider->height());
	}
	const auto fieldLeft = (_mode == Mode::RGBA)
		? (_hueSlider->x() + _hueSlider->width() + st::colorEditSkip - st::colorSliderSkip)
		: (_picker->x() + _picker->width() + st::colorEditSkip);
	const auto addWidth = (_mode == Mode::RGBA) ? 0 : st::colorEditSkip;
	const auto fieldWidth = st::colorSampleSize.width() + addWidth;
	const auto fieldHeight = _hueField->height();
	_newRect = QRect(fieldLeft, st::colorEditSkip, fieldWidth, st::colorSampleSize.height());
	_currentRect = _newRect.translated(0, st::colorSampleSize.height());
	_hueField->setGeometryToLeft(fieldLeft, _currentRect.y() + _currentRect.height() + st::colorFieldSkip, fieldWidth, fieldHeight);
	_saturationField->setGeometryToLeft(fieldLeft, _hueField->y() + _hueField->height(), fieldWidth, fieldHeight);
	_brightnessField->setGeometryToLeft(fieldLeft, _saturationField->y() + _saturationField->height(), fieldWidth, fieldHeight);
	_redField->setGeometryToLeft(fieldLeft, _brightnessField->y() + _brightnessField->height() + st::colorFieldSkip, fieldWidth, fieldHeight);
	_greenField->setGeometryToLeft(fieldLeft, _redField->y() + _redField->height(), fieldWidth, fieldHeight);
	_blueField->setGeometryToLeft(fieldLeft, _greenField->y() + _greenField->height(), fieldWidth, fieldHeight);
	const auto resultDelta = (_mode == Mode::RGBA)
		? (st::colorEditSkip + st::colorSliderWidth)
		: 0;
	const auto resultBottom = (_mode == Mode::RGBA)
		? (_opacitySlider->y() + _opacitySlider->height())
		: (_lightnessSlider->y() + _lightnessSlider->height());
	_result->setGeometryToLeft(fieldLeft - resultDelta, resultBottom - st::colorSliderSkip - _result->height(), fieldWidth + resultDelta, fieldHeight);
}

void EditColorBox::paintEvent(QPaintEvent *e) {
	BoxContent::paintEvent(e);

	Painter p(this);
	Ui::Shadow::paint(p, _picker->geometry(), width(), st::defaultRoundShadow);

	Ui::Shadow::paint(p, QRect(_newRect.x(), _newRect.y(), _newRect.width(), _newRect.height() + _currentRect.height()), width(), st::defaultRoundShadow);
	if (_new.alphaF() < 1.) {
		p.fillRect(myrtlrect(_newRect), _transparent);
	}
	p.fillRect(myrtlrect(_newRect), _new);
	if (_current.alphaF() < 1.) {
		p.fillRect(myrtlrect(_currentRect), _transparent);
	}
	p.fillRect(myrtlrect(_currentRect), _current);
}

void EditColorBox::mousePressEvent(QMouseEvent *e) {
	if (myrtlrect(_currentRect).contains(e->pos())) {
		updateFromColor(_current);
	}
}

EditColorBox::HSB EditColorBox::hsbFromControls() const {
	const auto hue = (_mode == Mode::RGBA)
		? qRound((1. - _hueSlider->value()) * 360)
		: qRound(_picker->valueX() * 360);
	const auto saturation = (_mode == Mode::RGBA)
		? qRound(_picker->valueX() * 255)
		: qRound((1. - _picker->valueY()) * 255);
	const auto brightness = (_mode == Mode::RGBA)
		? qRound((1. - _picker->valueY()) * 255)
		: (_lightnessMin
			+ qRound(_lightnessSlider->value()
				* (_lightnessMax - _lightnessMin)));
	return { hue, saturation, brightness };
}

QColor EditColorBox::applyLimits(QColor color) const {
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

void EditColorBox::updateFromColor(QColor color) {
	_new = applyLimits(color);
	updateControlsFromColor();
	updateRGBFields();
	updateHSBFields();
	updateResultField();
	update();
}

void EditColorBox::updateFromControls() {
	const auto hsb = hsbFromControls();
	const auto alpha = _opacitySlider
		? qRound(_opacitySlider->value() * 255)
		: 255;
	setHSB(hsb, alpha);
	updateHSBFields();
	updateControlsFromHSB(hsb);
}

void EditColorBox::updateFromHSBFields() {
	const auto hue = _hueField->value();
	const auto saturation = percentToByte(_saturationField->value());
	const auto brightness = std::clamp(
		percentToByte(_brightnessField->value()),
		_lightnessMin,
		_lightnessMax);
	const auto alpha = _opacitySlider
		? qRound(_opacitySlider->value() * 255)
		: 255;
	setHSB({ hue, saturation, brightness }, alpha);
	updateControlsFromHSB({ hue, saturation, brightness });
}

void EditColorBox::updateFromRGBFields() {
	const auto red = _redField->value();
	const auto blue = _blueField->value();
	const auto green = _greenField->value();
	const auto alpha = _opacitySlider
		? qRound(_opacitySlider->value() * 255)
		: 255;
	setRGB(red, green, blue, alpha);
	updateResultField();
}

void EditColorBox::updateFromResultField() {
	auto text = _result->getLastText();
	if (text.size() != 6 && text.size() != 8) {
		return;
	}

	auto fromHex = [](QChar hex) {
		auto code = hex.unicode();
		if (code >= 'A' && code <= 'F') {
			return (code - 'A' + 10);
		} else if (code >= 'a' && code <= 'f') {
			return (code - 'a' + 10);
		}
		return code - '0';
	};
	auto fromChars = [fromHex](QChar a, QChar b) {
		return fromHex(a) * 0x10 + fromHex(b);
	};
	auto red = fromChars(text[0], text[1]);
	auto green = fromChars(text[2], text[3]);
	auto blue = fromChars(text[4], text[5]);
	auto alpha = (text.size() == 8) ? fromChars(text[6], text[7]) : 255;
	setRGB(red, green, blue, alpha);
	updateRGBFields();
}

void EditColorBox::updateControlsFromHSB(HSB hsb) {
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

void EditColorBox::updateControlsFromColor() {
	auto red = _new.red();
	auto green = _new.green();
	auto blue = _new.blue();
	auto alpha = _new.alpha();
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

void EditColorBox::setHSB(HSB hsb, int alpha) {
	if (_mode == Mode::RGBA) {
		_new.setHsv(hsb.hue, hsb.saturation, hsb.brightness, alpha);
	} else {
		_new.setHsl(hsb.hue, hsb.saturation, hsb.brightness, alpha);
	}
	updateRGBFields();
	updateResultField();
	update();
}

void EditColorBox::setRGB(int red, int green, int blue, int alpha) {
	_new = applyLimits(QColor(red, green, blue, alpha));
	updateControlsFromColor();
	updateHSBFields();
	update();
}
