/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "passport/passport_form_box.h"

#include "passport/passport_form_controller.h"
#include "passport/passport_form_row.h"
#include "lang/lang_keys.h"
#include "ui/widgets/input_fields.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/buttons.h"
#include "ui/text_options.h"
#include "styles/style_boxes.h"
#include "styles/style_widgets.h"
#include "styles/style_passport.h"

namespace Passport {

class FormBox::CheckWidget : public Ui::RpWidget {
public:
	CheckWidget(QWidget *parent, not_null<FormController*> controller);

	void setInnerFocus();
	void submit();

protected:
	int resizeGetHeight(int newWidth) override;

private:
	void showError(const QString &error);
	void hideError();

	not_null<FormController*> _controller;

	object_ptr<Ui::PasswordInput> _password;
	object_ptr<Ui::FlatLabel> _hint = { nullptr };
	object_ptr<Ui::FlatLabel> _error = { nullptr };
	object_ptr<Ui::LinkButton> _forgot;
	object_ptr<Ui::FlatLabel> _about;

};

class FormBox::Inner : public Ui::RpWidget {
public:
	Inner(QWidget *parent, not_null<FormController*> controller);

	void refresh();

protected:
	int resizeGetHeight(int newWidth) override;

	void paintEvent(QPaintEvent *e) override;

private:
	not_null<FormController*> _controller;
	std::vector<object_ptr<FormRow>> _rows;

};

FormBox::CheckWidget::CheckWidget(
	QWidget *parent,
	not_null<FormController*> controller)
: RpWidget(parent)
, _controller(controller)
, _password(
	this,
	st::defaultInputField,
	langFactory(lng_passport_password_placeholder))
, _forgot(this, lang(lng_signin_recover), st::boxLinkButton)
, _about(
		this,
		lang(lng_passport_password_request),
		Ui::FlatLabel::InitType::Simple,
		st::passportPasswordLabel) {
	connect(_password, &Ui::PasswordInput::submitted, this, [=] {
		submit();
	});
	connect(_password, &Ui::PasswordInput::changed, this, [=] {
		hideError();
	});
	if (const auto hint = _controller->passwordHint(); !hint.isEmpty()) {
		_hint.create(
			this,
			hint,
			Ui::FlatLabel::InitType::Simple,
			st::passportPasswordHintLabel);
	}
	_controller->passwordError(
	) | rpl::start_with_next([=](const QString &error) {
		showError(error);
	}, lifetime());
}

void FormBox::CheckWidget::showError(const QString &error) {
	_password->showError();
	_error.create(
		this,
		error,
		Ui::FlatLabel::InitType::Simple,
		st::passportErrorLabel);
	_error->show();
	if (_hint) {
		_hint->hide();
	}
	resizeToWidth(width());
}

void FormBox::CheckWidget::hideError() {
	_error.destroy();
	if (_hint) {
		_hint->show();
	}
}

void FormBox::CheckWidget::submit() {
	_controller->submitPassword(_password->getLastText());
}

void FormBox::CheckWidget::setInnerFocus() {
	_password->setFocusFast();
}

int FormBox::CheckWidget::resizeGetHeight(int newWidth) {
	const auto padding = st::passportPasswordPadding;
	const auto availableWidth = newWidth
		- st::boxPadding.left()
		- st::boxPadding.right();
	auto top = padding.top();

	_about->resizeToWidth(availableWidth);
	_about->moveToLeft(padding.left(), top);
	top += _about->height();

	_password->resize(availableWidth, _password->height());
	_password->moveToLeft(padding.left(), top);
	top += _password->height();

	if (_error) {
		_error->resizeToWidth(availableWidth);
		_error->moveToLeft(padding.left(), top);
	}
	if (_hint) {
		_hint->resizeToWidth(availableWidth);
		_hint->moveToLeft(padding.left(), top);
		top += _hint->height();
	} else {
		top += st::passportPasswordHintLabel.style.font->height;
	}
	_forgot->moveToLeft(padding.left(), top);
	top += _forgot->height();
	return top + padding.bottom();
}

FormBox::Inner::Inner(
	QWidget *parent,
	not_null<FormController*> controller)
: RpWidget(parent)
, _controller(controller) {
	refresh();
}

void FormBox::Inner::refresh() {
	auto index = 0;
	_controller->fillRows([&](
			QString title,
			QString description,
			bool ready) {
		if (_rows.size() <= index) {
			_rows.push_back(object_ptr<FormRow>(this, title, description));
			_rows[index]->addClickHandler([=] {
				_controller->editField(index);
			});
		}
		_rows[index++]->setReady(ready);
	});
	while (_rows.size() > index) {
		_rows.pop_back();
	}
	resizeToWidth(width());
}

int FormBox::Inner::resizeGetHeight(int newWidth) {
	auto result = 0;
	for (auto &row : _rows) {
		row->resizeToWidth(newWidth);
		row->moveToLeft(0, result);
		result += row->height();
	}
	return result;
}

void FormBox::Inner::paintEvent(QPaintEvent *e) {
	Painter p(this);

}

FormBox::FormBox(QWidget*, not_null<FormController*> controller)
: _controller(controller) {
}

void FormBox::prepare() {
	setTitle(langFactory(lng_passport_title));

	addButton(langFactory(lng_create_group_next), [=] {
		submitPassword();
	});
	addButton(langFactory(lng_cancel), [=] {
		closeBox();
	});

	_passwordCheck = setInnerWidget(
		object_ptr<CheckWidget>(this, _controller));
	_passwordCheck->resizeToWidth(st::boxWideWidth);

	_innerCached.create(this, _controller);
	_innerCached->resizeToWidth(st::boxWideWidth);
	const auto desiredHeight = std::min(
		std::max(_innerCached->height(), _passwordCheck->height()),
		st::boxMaxListHeight);
	setDimensions(st::boxWideWidth, desiredHeight);
	_innerCached->hide();

	_controller->secretReadyEvents(
	) | rpl::start_with_next([=] {
		showForm();
	}, lifetime());
}

void FormBox::setInnerFocus() {
	if (_passwordCheck) {
		_passwordCheck->setInnerFocus();
	} else {
		_inner->setFocus();
	}
}

void FormBox::submitPassword() {
	Expects(_passwordCheck != nullptr);

	_passwordCheck->submit();
}

void FormBox::showForm() {
	clearButtons();
	addButton(langFactory(lng_passport_authorize), [=] {
		submitForm();
	});
	addButton(langFactory(lng_cancel), [=] {
		closeBox();
	});

	_inner = setInnerWidget(std::move(_innerCached));
	_inner->show();
}

void FormBox::submitForm() {

}

} // namespace Passport
