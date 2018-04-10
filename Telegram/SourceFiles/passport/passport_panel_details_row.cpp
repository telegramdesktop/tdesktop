/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "passport/passport_panel_details_row.h"

#include "passport/passport_panel_controller.h"
#include "lang/lang_keys.h"
#include "platform/platform_specific.h"
#include "ui/widgets/input_fields.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/buttons.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/countryinput.h"
#include "styles/style_boxes.h"
#include "styles/style_passport.h"

namespace Passport {
namespace {

class TextRow : public PanelDetailsRow {
public:
	TextRow(QWidget *parent, const QString &label, const QString &value);

	bool setFocusFast() override;
	rpl::producer<QString> value() const override;
	QString valueCurrent() const override;

private:
	int resizeInner(int left, int top, int width) override;
	void showInnerError() override;
	void finishInnerAnimating() override;

	object_ptr<Ui::InputField> _field;
	rpl::variable<QString> _value;

};

class CountryRow : public PanelDetailsRow {
public:
	CountryRow(
		QWidget *parent,
		not_null<PanelController*> controller,
		const QString &label,
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

	not_null<PanelController*> _controller;
	object_ptr<Ui::LinkButton> _link;
	rpl::variable<QString> _value;
	bool _errorShown = false;
	Animation _errorAnimation;

};

class DateRow : public TextRow {
public:
	using TextRow::TextRow;

};

class GenderRow : public TextRow {
public:
	using TextRow::TextRow;

};

TextRow::TextRow(
	QWidget *parent,
	const QString &label,
	const QString &value)
: PanelDetailsRow(parent, label)
, _field(this, st::passportDetailsField, nullptr, value)
, _value(value) {
	connect(_field, &Ui::InputField::changed, [=] {
		_value = valueCurrent();
	});
}

bool TextRow::setFocusFast() {
	_field->setFocusFast();
	return true;
}

QString TextRow::valueCurrent() const {
	return _field->getLastText();
}

rpl::producer<QString> TextRow::value() const {
	return _value.value();
}

int TextRow::resizeInner(int left, int top, int width) {
	_field->setGeometry(left, top, width, _field->height());
	return st::semiboldFont->height;
}

void TextRow::showInnerError() {
	_field->showError();
}

void TextRow::finishInnerAnimating() {
	_field->finishAnimating();
}

QString CountryString(const QString &code) {
	const auto name = CountrySelectBox::NameByISO(code);
	return name.isEmpty() ? lang(lng_passport_country_choose) : name;
}

CountryRow::CountryRow(
	QWidget *parent,
	not_null<PanelController*> controller,
	const QString &label,
	const QString &value)
: PanelDetailsRow(parent, label)
, _controller(controller)
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
		_errorAnimation.finish();
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
	const auto error = _errorAnimation.current(_errorShown ? 1. : 0.);
	if (error == 0.) {
		_link->setColorOverride(nullptr);
	} else {
		_link->setColorOverride(anim::color(
			st::boxLinkButton.color,
			st::boxTextFgError,
			error));
	}
}

void CountryRow::chooseCountry() {
	const auto top = _value.current();
	const auto name = CountrySelectBox::NameByISO(top);
	const auto box = _controller->show(Box<CountrySelectBox>(
		(name.isEmpty() ? Platform::SystemCountry() : top),
		CountrySelectBox::Type::Countries));
	connect(box, &CountrySelectBox::countryChosen, this, [=](QString iso) {
		_value = iso;
		_link->setText(CountryString(iso));
		hideCountryError();
		box->closeBox();
	});
}

} // namespace

int PanelLabel::naturalWidth() const {
	return -1;
}

void PanelLabel::resizeEvent(QResizeEvent *e) {
	_background->lower();
	_background->setGeometry(rect());
	return PaddingWrap::resizeEvent(e);
}

PanelDetailsRow::PanelDetailsRow(
	QWidget *parent,
	const QString &label)
: _label(label) {
}

object_ptr<PanelDetailsRow> PanelDetailsRow::Create(
		QWidget *parent,
		Type type,
		not_null<PanelController*> controller,
		const QString &label,
		const QString &value,
		const QString &error) {
	auto result = [&]() -> object_ptr<PanelDetailsRow> {
		switch (type) {
		case Type::Text:
			return object_ptr<TextRow>(parent, label, value);
		case Type::Country:
			return object_ptr<CountryRow>(parent, controller, label, value);
		case Type::Gender:
			return object_ptr<GenderRow>(parent, label, value);
		case Type::Date:
			return object_ptr<DateRow>(parent, label, value);
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

bool PanelDetailsRow::setFocusFast() {
	return false;
}

int PanelDetailsRow::resizeGetHeight(int newWidth) {
	const auto padding = st::passportDetailsPadding;
	const auto inputLeft = padding.left() + st::passportDetailsFieldLeft;
	const auto inputTop = st::passportDetailsFieldTop;
	const auto inputRight = padding.right();
	const auto inputWidth = std::max(newWidth - inputLeft - inputRight, 0);
	const auto innerHeight = resizeInner(inputLeft, inputTop, inputWidth);
	return padding.top()
		+ innerHeight
		+ (_error ? _error->height() : 0)
		+ padding.bottom();
}

void PanelDetailsRow::showError(const QString &error) {
	showInnerError();
	startErrorAnimation(true);
	if (!error.isEmpty()) {
		if (!_error) {
			_error.create(
				this,
				object_ptr<Ui::FlatLabel>(
					this,
					error,
					Ui::FlatLabel::InitType::Simple,
					st::passportVerifyErrorLabel));
			value(
			) | rpl::start_with_next([=] {
				hideError();
			}, lifetime());
		} else {
			_error->entity()->setText(error);
		}
		_error->show(anim::type::normal);
	} else if (_error) {
		_error->hide(anim::type::normal);
	}
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
		_errorAnimation.finish();
		update();
	}
}

void PanelDetailsRow::paintEvent(QPaintEvent *e) {
	Painter p(this);

	const auto ms = getms();
	const auto error = _errorAnimation.current(ms, _errorShown ? 1. : 0.);
	p.setFont(st::semiboldFont);
	p.setPen(anim::pen(
		st::passportDetailsField.placeholderFg,
		st::passportDetailsField.placeholderFgError,
		error));
	const auto padding = st::passportDetailsPadding;
	p.drawTextLeft(padding.left(), padding.top(), width(), _label);
}

} // namespace Passport
