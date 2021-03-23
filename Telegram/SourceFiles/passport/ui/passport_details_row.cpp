/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "passport/ui/passport_details_row.h"

#include "lang/lang_keys.h"
#include "base/platform/base_platform_info.h"
#include "ui/widgets/input_fields.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/layers/box_content.h"
#include "ui/boxes/country_select_box.h"
#include "data/data_countries.h"
#include "styles/style_layers.h"
#include "styles/style_passport.h"

#include <QtCore/QRegularExpression>

namespace Passport::Ui {
namespace {

class PostcodeInput : public MaskedInputField {
public:
	PostcodeInput(
		QWidget *parent,
		const style::InputField &st,
		rpl::producer<QString> placeholder,
		const QString &val);

protected:
	void correctValue(
		const QString &was,
		int wasCursor,
		QString &now,
		int &nowCursor) override;

};

PostcodeInput::PostcodeInput(
	QWidget *parent,
	const style::InputField &st,
	rpl::producer<QString> placeholder,
	const QString &val)
: MaskedInputField(parent, st, std::move(placeholder), val) {
	if (!QRegularExpression("^[a-zA-Z0-9\\-]+$").match(val).hasMatch()) {
		setText(QString());
	}
}

void PostcodeInput::correctValue(
		const QString &was,
		int wasCursor,
		QString &now,
		int &nowCursor) {
	QString newText;
	newText.reserve(now.size());
	auto newPos = nowCursor;
	for (auto i = 0, l = now.size(); i < l; ++i) {
		const auto ch = now[i];
		if ((ch >= '0' && ch <= '9')
			|| (ch >= 'a' && ch <= 'z')
			|| (ch >= 'A' && ch <= 'Z')
			|| (ch == '-')) {
			newText.append(ch);
		} else if (i < nowCursor) {
			--newPos;
		}
	}
	setCorrectedText(now, nowCursor, newText, newPos);
}

template <typename Input>
class AbstractTextRow : public PanelDetailsRow {
public:
	AbstractTextRow(
		QWidget *parent,
		const QString &label,
		int maxLabelWidth,
		const QString &value,
		int limit);

	bool setFocusFast() override;
	rpl::producer<QString> value() const override;
	QString valueCurrent() const override;

private:
	int resizeInner(int left, int top, int width) override;
	void showInnerError() override;
	void finishInnerAnimating() override;

	object_ptr<Input> _field;
	rpl::variable<QString> _value;

};

class CountryRow : public PanelDetailsRow {
public:
	CountryRow(
		QWidget *parent,
		Fn<void(object_ptr<BoxContent>)> showBox,
		const QString &defaultCountry,
		const QString &label,
		int maxLabelWidth,
		const QString &value);

	rpl::producer<QString> value() const override;
	QString valueCurrent() const override;

private:
	int resizeInner(int left, int top, int width) override;
	void showInnerError() override;
	void finishInnerAnimating() override;

	void chooseCountry();
	void hideCountryError();
	void toggleError(bool shown);
	void errorAnimationCallback();

	QString _defaultCountry;
	Fn<void(object_ptr<BoxContent>)> _showBox;
	object_ptr<LinkButton> _link;
	rpl::variable<QString> _value;
	bool _errorShown = false;
	Animations::Simple _errorAnimation;

};

class DateInput final : public MaskedInputField {
public:
	using MaskedInputField::MaskedInputField;

	void setMaxValue(int value);

	rpl::producer<> erasePrevious() const;
	rpl::producer<QChar> putNext() const;

protected:
	void keyPressEvent(QKeyEvent *e) override;

	void correctValue(
		const QString &was,
		int wasCursor,
		QString &now,
		int &nowCursor) override;

private:
	int _maxValue = 0;
	int _maxDigits = 0;
	rpl::event_stream<> _erasePrevious;
	rpl::event_stream<QChar> _putNext;

};

class DateRow : public PanelDetailsRow {
public:
	DateRow(
		QWidget *parent,
		const QString &label,
		int maxLabelWidth,
		const QString &value);

	bool setFocusFast() override;
	rpl::producer<QString> value() const override;
	QString valueCurrent() const override;

protected:
	void paintEvent(QPaintEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;

private:
	void setInnerFocus();
	void putNext(const object_ptr<DateInput> &field, QChar ch);
	void erasePrevious(const object_ptr<DateInput> &field);
	int resizeInner(int left, int top, int width) override;
	void showInnerError() override;
	void finishInnerAnimating() override;
	void setErrorShown(bool error);
	void setFocused(bool focused);
	void startBorderAnimation();
	template <typename Widget>
	bool insideSeparator(QPoint position, const Widget &widget) const;

	int day() const;
	int month() const;
	int year() const;
	int number(const object_ptr<DateInput> &field) const;

	object_ptr<DateInput> _day;
	object_ptr<PaddingWrap<FlatLabel>> _separator1;
	object_ptr<DateInput> _month;
	object_ptr<PaddingWrap<FlatLabel>> _separator2;
	object_ptr<DateInput> _year;
	rpl::variable<QString> _value;

	style::cursor _cursor = style::cur_default;
	Animations::Simple _a_borderShown;
	int _borderAnimationStart = 0;
	Animations::Simple _a_borderOpacity;
	bool _borderVisible = false;

	Animations::Simple _a_error;
	bool _error = false;
	Animations::Simple _a_focused;
	bool _focused = false;

};

class GenderRow : public PanelDetailsRow {
public:
	GenderRow(
		QWidget *parent,
		const QString &label,
		int maxLabelWidth,
		const QString &value);

	rpl::producer<QString> value() const override;
	QString valueCurrent() const override;

private:
	enum class Gender {
		Male,
		Female,
	};

	static std::optional<Gender> StringToGender(const QString &value);
	static QString GenderToString(Gender gender);

	int resizeInner(int left, int top, int width) override;

	void showInnerError() override;
	void finishInnerAnimating() override;
	void toggleError(bool shown);
	void hideGenderError();
	void errorAnimationCallback();

	std::unique_ptr<AbstractCheckView> createRadioView(
		RadioView* &weak) const;

	std::shared_ptr<RadioenumGroup<Gender>> _group;
	RadioView *_maleRadio = nullptr;
	RadioView *_femaleRadio = nullptr;
	object_ptr<Radioenum<Gender>> _male;
	object_ptr<Radioenum<Gender>> _female;
	rpl::variable<QString> _value;

	bool _errorShown = false;
	Animations::Simple _errorAnimation;

};

template <typename Input>
AbstractTextRow<Input>::AbstractTextRow(
	QWidget *parent,
	const QString &label,
	int maxLabelWidth,
	const QString &value,
	int limit)
: PanelDetailsRow(parent, label, maxLabelWidth)
, _field(this, st::passportDetailsField, nullptr, value)
, _value(value) {
	_field->setMaxLength(limit);
	connect(_field, &Input::changed, [=] {
		_value = valueCurrent();
	});
}

template <typename Input>
bool AbstractTextRow<Input>::setFocusFast() {
	_field->setFocusFast();
	return true;
}

template <typename Input>
QString AbstractTextRow<Input>::valueCurrent() const {
	return _field->getLastText();
}

template <typename Input>
rpl::producer<QString> AbstractTextRow<Input>::value() const {
	return _value.value();
}

template <typename Input>
int AbstractTextRow<Input>::resizeInner(int left, int top, int width) {
	_field->setGeometry(left, top, width, _field->height());
	return st::semiboldFont->height;
}

template <typename Input>
void AbstractTextRow<Input>::showInnerError() {
	_field->showError();
}

template <typename Input>
void AbstractTextRow<Input>::finishInnerAnimating() {
	_field->finishAnimating();
}

QString CountryString(const QString &code) {
	const auto name = Data::CountryNameByISO2(code);
	return name.isEmpty() ? tr::lng_passport_country_choose(tr::now) : name;
}

CountryRow::CountryRow(
	QWidget *parent,
	Fn<void(object_ptr<BoxContent>)> showBox,
	const QString &defaultCountry,
	const QString &label,
	int maxLabelWidth,
	const QString &value)
: PanelDetailsRow(parent, label, maxLabelWidth)
, _defaultCountry(defaultCountry)
, _showBox(std::move(showBox))
, _link(this, CountryString(value), st::boxLinkButton)
, _value(value) {
	_value.changes(
	) | rpl::start_with_next([=] {
		hideCountryError();
	}, lifetime());

	_link->addClickHandler([=] {
		chooseCountry();
	});
}

QString CountryRow::valueCurrent() const {
	return _value.current();
}

rpl::producer<QString> CountryRow::value() const {
	return _value.value();
}

int CountryRow::resizeInner(int left, int top, int width) {
	_link->move(left, st::passportDetailsField.textMargins.top() + top);
	return st::semiboldFont->height;
}

void CountryRow::showInnerError() {
	toggleError(true);
}

void CountryRow::finishInnerAnimating() {
	if (_errorAnimation.animating()) {
		_errorAnimation.stop();
		errorAnimationCallback();
	}
}

void CountryRow::hideCountryError() {
	toggleError(false);
}

void CountryRow::toggleError(bool shown) {
	if (_errorShown != shown) {
		_errorShown = shown;
		_errorAnimation.start(
			[=] { errorAnimationCallback(); },
			_errorShown ? 0. : 1.,
			_errorShown ? 1. : 0.,
			st::passportDetailsField.duration);
	}
}

void CountryRow::errorAnimationCallback() {
	const auto error = _errorAnimation.value(_errorShown ? 1. : 0.);
	if (error == 0.) {
		_link->setColorOverride(std::nullopt);
	} else {
		_link->setColorOverride(anim::color(
			st::boxLinkButton.color,
			st::boxTextFgError,
			error));
	}
}

void CountryRow::chooseCountry() {
	const auto top = _value.current();
	const auto name = Data::CountryNameByISO2(top);
	const auto country = !name.isEmpty()
		? top
		: !_defaultCountry.isEmpty()
		? _defaultCountry
		: Platform::SystemCountry();
	auto box = Box<CountrySelectBox>(
		country,
		CountrySelectBox::Type::Countries);
	const auto raw = box.data();
	raw->countryChosen(
	) | rpl::start_with_next([=](QString iso) {
		_value = iso;
		_link->setText(CountryString(iso));
		hideCountryError();
		raw->closeBox();
	}, lifetime());
	_showBox(std::move(box));
}

QDate ValidateDate(const QString &value) {
	const auto match = QRegularExpression(
		"^([0-9]{2})\\.([0-9]{2})\\.([0-9]{4})$").match(value);
	if (!match.hasMatch()) {
		return QDate();
	}
	auto result = QDate();
	const auto readInt = [](const QString &value) {
		auto ref = value.midRef(0);
		while (!ref.isEmpty() && ref.at(0) == '0') {
			ref = ref.mid(1);
		}
		return ref.toInt();
	};
	result.setDate(
		readInt(match.captured(3)),
		readInt(match.captured(2)),
		readInt(match.captured(1)));
	return result;
}

QString GetDay(const QString &value) {
	if (const auto date = ValidateDate(value); date.isValid()) {
		return QString("%1").arg(date.day(), 2, 10, QChar('0'));
	}
	return QString();
}

QString GetMonth(const QString &value) {
	if (const auto date = ValidateDate(value); date.isValid()) {
		return QString("%1").arg(date.month(), 2, 10, QChar('0'));
	}
	return QString();
}

QString GetYear(const QString &value) {
	if (const auto date = ValidateDate(value); date.isValid()) {
		return QString("%1").arg(date.year(), 4, 10, QChar('0'));
	}
	return QString();
}

void DateInput::setMaxValue(int value) {
	_maxValue = value;
	_maxDigits = 0;
	while (value > 0) {
		++_maxDigits;
		value /= 10;
	}
}

rpl::producer<> DateInput::erasePrevious() const {
	return _erasePrevious.events();
}

rpl::producer<QChar> DateInput::putNext() const {
	return _putNext.events();
}

void DateInput::keyPressEvent(QKeyEvent *e) {
	const auto isBackspace = (e->key() == Qt::Key_Backspace);
	const auto isBeginning = (cursorPosition() == 0);
	if (isBackspace && isBeginning && !hasSelectedText()) {
		_erasePrevious.fire({});
	} else {
		MaskedInputField::keyPressEvent(e);
	}
}

void DateInput::correctValue(
		const QString &was,
		int wasCursor,
		QString &now,
		int &nowCursor) {
	auto newText = QString();
	auto newCursor = -1;
	const auto oldCursor = nowCursor;
	const auto oldLength = now.size();
	auto accumulated = 0;
	auto limit = 0;
	for (; limit != oldLength; ++limit) {
		if (now[limit].isDigit()) {
			accumulated *= 10;
			accumulated += (now[limit].unicode() - '0');
			if (accumulated > _maxValue || limit == _maxDigits) {
				break;
			}
		}
	}
	for (auto i = 0; i != limit;) {
		if (now[i].isDigit()) {
			newText += now[i];
		}
		if (++i == oldCursor) {
			newCursor = newText.size();
		}
	}
	if (newCursor < 0) {
		newCursor = newText.size();
	}
	if (newText != now) {
		now = newText;
		setText(now);
		startPlaceholderAnimation();
	}
	if (newCursor != nowCursor) {
		nowCursor = newCursor;
		setCursorPosition(nowCursor);
	}
	if (accumulated > _maxValue
		|| (limit == _maxDigits && oldLength > _maxDigits)) {
		if (oldCursor > limit) {
			_putNext.fire('0' + (accumulated % 10));
		} else {
			_putNext.fire(0);
		}
	}
}

DateRow::DateRow(
	QWidget *parent,
	const QString &label,
	int maxLabelWidth,
	const QString &value)
: PanelDetailsRow(parent, label, maxLabelWidth)
, _day(
	this,
	st::passportDetailsDateField,
	tr::lng_date_input_day(),
	GetDay(value))
, _separator1(
	this,
	object_ptr<FlatLabel>(
		this,
		QString(" / "),
		st::passportDetailsSeparator),
	st::passportDetailsSeparatorPadding)
, _month(
	this,
	st::passportDetailsDateField,
	tr::lng_date_input_month(),
	GetMonth(value))
, _separator2(
	this,
	object_ptr<FlatLabel>(
		this,
		QString(" / "),
		st::passportDetailsSeparator),
	st::passportDetailsSeparatorPadding)
, _year(
	this,
	st::passportDetailsDateField,
	tr::lng_date_input_year(),
	GetYear(value))
, _value(valueCurrent()) {
	const auto focused = [=](const object_ptr<DateInput> &field) {
		return [this, pointer = MakeWeak(field.data())]{
			_borderAnimationStart = pointer->borderAnimationStart()
				+ pointer->x()
				- _day->x();
			setFocused(true);
		};
	};
	const auto blurred = [=] {
		setFocused(false);
	};
	const auto changed = [=] {
		_value = valueCurrent();
	};
	connect(_day, &MaskedInputField::focused, focused(_day));
	connect(_month, &MaskedInputField::focused, focused(_month));
	connect(_year, &MaskedInputField::focused, focused(_year));
	connect(_day, &MaskedInputField::blurred, blurred);
	connect(_month, &MaskedInputField::blurred, blurred);
	connect(_year, &MaskedInputField::blurred, blurred);
	connect(_day, &MaskedInputField::changed, changed);
	connect(_month, &MaskedInputField::changed, changed);
	connect(_year, &MaskedInputField::changed, changed);
	_day->setMaxValue(31);
	_day->putNext() | rpl::start_with_next([=](QChar ch) {
		putNext(_month, ch);
	}, lifetime());
	_month->setMaxValue(12);
	_month->putNext() | rpl::start_with_next([=](QChar ch) {
		putNext(_year, ch);
	}, lifetime());
	_month->erasePrevious() | rpl::start_with_next([=] {
		erasePrevious(_day);
	}, lifetime());
	_year->setMaxValue(2999);
	_year->erasePrevious() | rpl::start_with_next([=] {
		erasePrevious(_month);
	}, lifetime());
	_separator1->setAttribute(Qt::WA_TransparentForMouseEvents);
	_separator2->setAttribute(Qt::WA_TransparentForMouseEvents);
	setMouseTracking(true);

	_value.changes(
	) | rpl::start_with_next([=] {
		setErrorShown(false);
	}, lifetime());
}

void DateRow::putNext(const object_ptr<DateInput> &field, QChar ch) {
	field->setCursorPosition(0);
	if (ch.unicode()) {
		field->setText(ch + field->getLastText());
		field->setCursorPosition(1);
	}
	field->setFocus();
}

void DateRow::erasePrevious(const object_ptr<DateInput> &field) {
	const auto text = field->getLastText();
	if (!text.isEmpty()) {
		field->setCursorPosition(text.size() - 1);
		field->setText(text.mid(0, text.size() - 1));
	}
	field->setFocus();
}

bool DateRow::setFocusFast() {
	if (day()) {
		if (month()) {
			_year->setFocusFast();
		} else {
			_month->setFocusFast();
		}
	} else {
		_day->setFocusFast();
	}
	return true;
}

int DateRow::number(const object_ptr<DateInput> &field) const {
	const auto text = field->getLastText();
	auto ref = text.midRef(0);
	while (!ref.isEmpty() && ref.at(0) == '0') {
		ref = ref.mid(1);
	}
	return ref.toInt();
}

int DateRow::day() const {
	return number(_day);
}

int DateRow::month() const {
	return number(_month);
}

int DateRow::year() const {
	return number(_year);
}

QString DateRow::valueCurrent() const {
	const auto result = QString("%1.%2.%3"
		).arg(day(), 2, 10, QChar('0')
		).arg(month(), 2, 10, QChar('0')
		).arg(year(), 4, 10, QChar('0'));
	return ValidateDate(result).isValid() ? result : QString();
}

rpl::producer<QString> DateRow::value() const {
	return _value.value();
}

void DateRow::paintEvent(QPaintEvent *e) {
	PanelDetailsRow::paintEvent(e);

	Painter p(this);

	const auto &_st = st::passportDetailsField;
	const auto height = _st.heightMin;
	const auto width = _year->x() + _year->width() - _day->x();
	p.translate(_day->x(), _day->y());
	if (_st.border) {
		p.fillRect(0, height - _st.border, width, _st.border, _st.borderFg);
	}
	auto errorDegree = _a_error.value(_error ? 1. : 0.);
	auto focusedDegree = _a_focused.value(_focused ? 1. : 0.);
	auto borderShownDegree = _a_borderShown.value(1.);
	auto borderOpacity = _a_borderOpacity.value(_borderVisible ? 1. : 0.);
	if (_st.borderActive && (borderOpacity > 0.)) {
		auto borderStart = std::clamp(_borderAnimationStart, 0, width);
		auto borderFrom = qRound(borderStart * (1. - borderShownDegree));
		auto borderTo = borderStart + qRound((width - borderStart) * borderShownDegree);
		if (borderTo > borderFrom) {
			auto borderFg = anim::brush(_st.borderFgActive, _st.borderFgError, errorDegree);
			p.setOpacity(borderOpacity);
			p.fillRect(borderFrom, height - _st.borderActive, borderTo - borderFrom, _st.borderActive, borderFg);
			p.setOpacity(1);
		}
	}
}

template <typename Widget>
bool DateRow::insideSeparator(QPoint position, const Widget &widget) const {
	const auto x = position.x();
	const auto y = position.y();
	return (x >= widget->x() && x < widget->x() + widget->width())
		&& (y >= _day->y() && y < _day->y() + _day->height());
}

void DateRow::mouseMoveEvent(QMouseEvent *e) {
	const auto cursor = (insideSeparator(e->pos(), _separator1)
		|| insideSeparator(e->pos(), _separator2))
		? style::cur_text
		: style::cur_default;
	if (_cursor != cursor) {
		_cursor = cursor;
		setCursor(_cursor);
	}
}

void DateRow::mousePressEvent(QMouseEvent *e) {
	const auto x = e->pos().x();
	const auto focus1 = [&] {
		if (_day->getLastText().size() > 1) {
			_month->setFocus();
		} else {
			_day->setFocus();
		}
	};
	if (insideSeparator(e->pos(), _separator1)) {
		focus1();
		_borderAnimationStart = x - _day->x();
	} else if (insideSeparator(e->pos(), _separator2)) {
		if (_month->getLastText().size() > 1) {
			_year->setFocus();
		} else {
			focus1();
		}
		_borderAnimationStart = x - _day->x();
	}
}

int DateRow::resizeInner(int left, int top, int width) {
	const auto right = left + width;
	const auto &_st = st::passportDetailsDateField;
	const auto &font = _st.placeholderFont;
	const auto addToWidth = st::passportDetailsSeparatorPadding.left();
	const auto dayWidth = _st.textMargins.left()
		+ _st.placeholderMargins.left()
		+ font->width(tr::lng_date_input_day(tr::now))
		+ _st.placeholderMargins.right()
		+ _st.textMargins.right()
		+ addToWidth;
	const auto monthWidth = _st.textMargins.left()
		+ _st.placeholderMargins.left()
		+ font->width(tr::lng_date_input_month(tr::now))
		+ _st.placeholderMargins.right()
		+ _st.textMargins.right()
		+ addToWidth;
	_day->setGeometry(left, top, dayWidth, _day->height());
	left += dayWidth - addToWidth;
	_separator1->resizeToNaturalWidth(width);
	_separator1->move(left, top);
	left += _separator1->width();
	_month->setGeometry(left, top, monthWidth, _month->height());
	left += monthWidth - addToWidth;
	_separator2->resizeToNaturalWidth(width);
	_separator2->move(left, top);
	left += _separator2->width();
	_year->setGeometry(left, top, right - left, _year->height());
	return st::semiboldFont->height;
}

void DateRow::showInnerError() {
	setErrorShown(true);
	if (_year->getLastText().size() == 2) {
		// We don't support year 95 for 1995 or 03 for 2003.
		// Let's give a hint to our user what is wrong.
		_year->setFocus();
		_year->selectAll();
	} else if (!_focused) {
		setInnerFocus();
	}
}

void DateRow::setInnerFocus() {
	if (day()) {
		if (month()) {
			_year->setFocus();
		} else {
			_month->setFocus();
		}
	} else {
		_day->setFocus();
	}
}

void DateRow::setErrorShown(bool error) {
	if (_error != error) {
		_error = error;
		_a_error.start(
			[=] { update(); },
			_error ? 0. : 1.,
			_error ? 1. : 0.,
			st::passportDetailsField.duration);
		startBorderAnimation();
	}
}

void DateRow::setFocused(bool focused) {
	if (_focused != focused) {
		_focused = focused;
		_a_focused.start(
			[=] { update(); },
			_focused ? 0. : 1.,
			_focused ? 1. : 0.,
			st::passportDetailsField.duration);
		startBorderAnimation();
	}
}

void DateRow::finishInnerAnimating() {
	_day->finishAnimating();
	_month->finishAnimating();
	_year->finishAnimating();
	_a_borderOpacity.stop();
	_a_borderShown.stop();
	_a_error.stop();
}

void DateRow::startBorderAnimation() {
	auto borderVisible = (_error || _focused);
	if (_borderVisible != borderVisible) {
		_borderVisible = borderVisible;
		const auto duration = st::passportDetailsField.duration;
		if (_borderVisible) {
			if (_a_borderOpacity.animating()) {
				_a_borderOpacity.start([=] { update(); }, 0., 1., duration);
			} else {
				_a_borderShown.start([=] { update(); }, 0., 1., duration);
			}
		} else {
			_a_borderOpacity.start([=] { update(); }, 1., 0., duration);
		}
	}
}

GenderRow::GenderRow(
	QWidget *parent,
	const QString &label,
	int maxLabelWidth,
	const QString &value)
: PanelDetailsRow(parent, label, maxLabelWidth)
, _group(StringToGender(value).has_value()
	? std::make_shared<RadioenumGroup<Gender>>(*StringToGender(value))
	: std::make_shared<RadioenumGroup<Gender>>())
, _male(
	this,
	_group,
	Gender::Male,
	tr::lng_passport_gender_male(tr::now),
	st::defaultCheckbox,
	createRadioView(_maleRadio))
, _female(
	this,
	_group,
	Gender::Female,
	tr::lng_passport_gender_female(tr::now),
	st::defaultCheckbox,
	createRadioView(_femaleRadio))
, _value(StringToGender(value) ? value : QString()) {
	_group->setChangedCallback([=](Gender gender) {
		_value = GenderToString(gender);
		hideGenderError();
	});
}

std::unique_ptr<AbstractCheckView> GenderRow::createRadioView(
		RadioView* &weak) const {
	auto result = std::make_unique<RadioView>(st::defaultRadio, false);
	weak = result.get();
	return result;
}

auto GenderRow::StringToGender(const QString &value)
-> std::optional<Gender> {
	if (value == qstr("male")) {
		return Gender::Male;
	} else if (value == qstr("female")) {
		return Gender::Female;
	}
	return std::nullopt;
}

QString GenderRow::GenderToString(Gender gender) {
	return (gender == Gender::Male) ? "male" : "female";
}

QString GenderRow::valueCurrent() const {
	return _value.current();
}

rpl::producer<QString> GenderRow::value() const {
	return _value.value();
}

int GenderRow::resizeInner(int left, int top, int width) {
	top += st::passportDetailsField.textMargins.top();
	top -= st::defaultCheckbox.textPosition.y();
	_male->moveToLeft(left, top);
	left += _male->widthNoMargins() + st::passportDetailsGenderSkip;
	_female->moveToLeft(left, top);
	return st::semiboldFont->height;
}

void GenderRow::showInnerError() {
	toggleError(true);
}

void GenderRow::finishInnerAnimating() {
	if (_errorAnimation.animating()) {
		_errorAnimation.stop();
		errorAnimationCallback();
	}
}

void GenderRow::hideGenderError() {
	toggleError(false);
}

void GenderRow::toggleError(bool shown) {
	if (_errorShown != shown) {
		_errorShown = shown;
		_errorAnimation.start(
			[=] { errorAnimationCallback(); },
			_errorShown ? 0. : 1.,
			_errorShown ? 1. : 0.,
			st::passportDetailsField.duration);
	}
}

void GenderRow::errorAnimationCallback() {
	const auto error = _errorAnimation.value(_errorShown ? 1. : 0.);
	if (error == 0.) {
		_maleRadio->setUntoggledOverride(std::nullopt);
		_femaleRadio->setUntoggledOverride(std::nullopt);
	} else {
		const auto color = anim::color(
			st::defaultRadio.untoggledFg,
			st::boxTextFgError,
			error);
		_maleRadio->setUntoggledOverride(color);
		_femaleRadio->setUntoggledOverride(color);
	}
}

} // namespace

PanelDetailsRow::PanelDetailsRow(
	QWidget *parent,
	const QString &label,
	int maxLabelWidth)
: _label(label)
, _maxLabelWidth(maxLabelWidth) {
}

object_ptr<PanelDetailsRow> PanelDetailsRow::Create(
		QWidget *parent,
		Fn<void(object_ptr<BoxContent>)> showBox,
		const QString &defaultCountry,
		Type type,
		const QString &label,
		int maxLabelWidth,
		const QString &value,
		const QString &error,
		int limit) {
	auto result = [&]() -> object_ptr<PanelDetailsRow> {
		switch (type) {
		case Type::Text:
			return object_ptr<AbstractTextRow<InputField>>(
				parent,
				label,
				maxLabelWidth,
				value,
				limit);
		case Type::Postcode:
			return object_ptr<AbstractTextRow<PostcodeInput>>(
				parent,
				label,
				maxLabelWidth,
				value,
				limit);
		case Type::Country:
			return object_ptr<CountryRow>(
				parent,
				showBox,
				defaultCountry,
				label,
				maxLabelWidth,
				value);
		case Type::Gender:
			return object_ptr<GenderRow>(
				parent,
				label,
				maxLabelWidth,
				value);
		case Type::Date:
			return object_ptr<DateRow>(
				parent,
				label,
				maxLabelWidth,
				value);
		default:
			Unexpected("Type in PanelDetailsRow::Create.");
		}
	}();
	if (!error.isEmpty()) {
		result->showError(error);
		result->finishAnimating();
	}
	return result;
}

int PanelDetailsRow::LabelWidth(const QString &label) {
	return st::semiboldFont->width(label);
}

bool PanelDetailsRow::setFocusFast() {
	return false;
}

int PanelDetailsRow::resizeGetHeight(int newWidth) {
	const auto padding = st::passportDetailsPadding;
	const auto inputLeft = padding.left() + std::max(
		st::passportDetailsFieldLeft,
		_maxLabelWidth + st::passportDetailsFieldSkipMin);
	const auto inputTop = st::passportDetailsFieldTop;
	const auto inputRight = padding.right();
	const auto inputWidth = std::max(newWidth - inputLeft - inputRight, 0);
	const auto innerHeight = resizeInner(inputLeft, inputTop, inputWidth);
	const auto result = padding.top()
		+ innerHeight
		+ (_error ? _error->height() : 0)
		+ padding.bottom();
	if (_error) {
		_error->resizeToWidth(inputWidth);
		_error->moveToLeft(inputLeft, result - _error->height());
	}
	return result;
}

void PanelDetailsRow::showError(std::optional<QString> error) {
	if (!_errorHideSubscription) {
		_errorHideSubscription = true;

		value(
		) | rpl::start_with_next([=] {
			hideError();
		}, lifetime());
	}
	showInnerError();
	startErrorAnimation(true);
	if (!error.has_value()) {
		return;
	}
	if (error->isEmpty()) {
		if (_error) {
			_error->hide(anim::type::normal);
		}
	} else {
		if (!_error) {
			_error.create(
				this,
				object_ptr<FlatLabel>(
					this,
					*error,
					st::passportVerifyErrorLabel));
		} else {
			_error->entity()->setText(*error);
		}
		_error->heightValue(
		) | rpl::start_with_next([=] {
			resizeToWidth(width());
		}, _error->lifetime());
		_error->show(anim::type::normal);
	}
}

bool PanelDetailsRow::errorShown() const {
	return _errorShown;
}

void PanelDetailsRow::hideError() {
	startErrorAnimation(false);
	if (_error) {
		_error->hide(anim::type::normal);
	}
}

void PanelDetailsRow::startErrorAnimation(bool shown) {
	if (_errorShown != shown) {
		_errorShown = shown;
		_errorAnimation.start(
			[=] { update(); },
			_errorShown ? 0. : 1.,
			_errorShown ? 1. : 0.,
			st::passportDetailsField.duration);
	}
}

void PanelDetailsRow::finishAnimating() {
	if (_error) {
		_error->finishAnimating();
	}
	if (_errorAnimation.animating()) {
		_errorAnimation.stop();
		update();
	}
}

void PanelDetailsRow::paintEvent(QPaintEvent *e) {
	Painter p(this);

	const auto error = _errorAnimation.value(_errorShown ? 1. : 0.);
	p.setFont(st::semiboldFont);
	p.setPen(anim::pen(
		st::passportDetailsField.placeholderFg,
		st::passportDetailsField.placeholderFgError,
		error));
	const auto padding = st::passportDetailsPadding;
	p.drawTextLeft(padding.left(), padding.top(), width(), _label);
}

} // namespace Passport::Ui
