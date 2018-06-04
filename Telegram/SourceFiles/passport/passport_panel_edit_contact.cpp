/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "passport/passport_panel_edit_contact.h"

#include "passport/passport_panel_controller.h"
#include "passport/passport_panel_details_row.h"
#include "info/profile/info_profile_button.h"
#include "info/profile/info_profile_values.h"
#include "ui/widgets/input_fields.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/shadow.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/fade_wrap.h"
#include "boxes/abstract_box.h"
#include "boxes/confirm_phone_box.h"
#include "lang/lang_keys.h"
#include "styles/style_passport.h"
#include "styles/style_boxes.h"

namespace Passport {
namespace {

class VerifyBox : public BoxContent {
public:
	VerifyBox(
		QWidget*,
		const QString &title,
		const QString &text,
		int codeLength,
		Fn<void(QString code)> submit,
		rpl::producer<QString> call,
		rpl::producer<QString> error);

	void setInnerFocus() override;

protected:
	void prepare() override;

private:
	void setupControls(
		const QString &text,
		int codeLength,
		Fn<void(QString code)> submit,
		rpl::producer<QString> call,
		rpl::producer<QString> error);

	QString _title;
	Fn<void()> _submit;
	QPointer<SentCodeField> _code;
	int _height = 0;

};

VerifyBox::VerifyBox(
	QWidget*,
	const QString &title,
	const QString &text,
	int codeLength,
	Fn<void(QString code)> submit,
	rpl::producer<QString> call,
	rpl::producer<QString> error)
: _title(title) {
	setupControls(
		text,
		codeLength,
		submit,
		std::move(call),
		std::move(error));
}

void VerifyBox::setupControls(
		const QString &text,
		int codeLength,
		Fn<void(QString code)> submit,
		rpl::producer<QString> call,
		rpl::producer<QString> error) {
	const auto description = Ui::CreateChild<Ui::FlatLabel>(
		this,
		text,
		Ui::FlatLabel::InitType::Simple,
		st::boxLabel);
	_code = Ui::CreateChild<SentCodeField>(
		this,
		st::defaultInputField,
		langFactory(lng_change_phone_code_title));

	const auto problem = Ui::CreateChild<Ui::FadeWrap<Ui::FlatLabel>>(
		this,
		object_ptr<Ui::FlatLabel>(
			this,
			QString(),
			Ui::FlatLabel::InitType::Simple,
			st::passportVerifyErrorLabel));
	const auto waiter = Ui::CreateChild<Ui::FlatLabel>(
		this,
		std::move(call),
		st::boxDividerLabel);
	std::move(
		error
	) | rpl::start_with_next([=](const QString &error) {
		if (error.isEmpty()) {
			problem->hide(anim::type::normal);
		} else {
			problem->entity()->setText(error);
			problem->show(anim::type::normal);
			_code->showError();
		}
	}, lifetime());

	auto y = 0;
	const auto innerWidth = st::boxWidth
		- st::boxPadding.left()
		- st::boxPadding.right();
	description->resizeToWidth(innerWidth);
	description->moveToLeft(st::boxPadding.left(), y);
	y += description->height() + st::boxPadding.bottom();
	_code->resizeToWidth(innerWidth);
	_code->moveToLeft(st::boxPadding.left(), y);
	y += _code->height() + st::boxPadding.bottom();
	problem->resizeToWidth(innerWidth);
	problem->moveToLeft(st::boxPadding.left(), y);
	y += problem->height() + st::boxPadding.top();
	waiter->resizeToWidth(innerWidth);
	waiter->moveToLeft(st::boxPadding.left(), y);
	y += waiter->height() + st::boxPadding.bottom();
	_height = y;

	_submit = [=] {
		submit(_code->getLastText());
	};
	if (codeLength > 0) {
		_code->setAutoSubmit(codeLength, _submit);
	} else {
		connect(_code, &SentCodeField::submitted, _submit);
	}
	connect(_code, &SentCodeField::changed, [=] {
		problem->hide(anim::type::normal);
	});
}

void VerifyBox::setInnerFocus() {
	_code->setFocusFast();
}

void VerifyBox::prepare() {
	setTitle([=] { return _title; });

	addButton(langFactory(lng_change_phone_new_submit), _submit);
	addButton(langFactory(lng_cancel), [=] { closeBox(); });

	setDimensions(st::boxWidth, _height);
}

} // namespace

EditContactScheme::EditContactScheme(ValueType type) : type(type) {
}

PanelEditContact::PanelEditContact(
	QWidget*,
	not_null<PanelController*> controller,
	Scheme scheme,
	const QString &data,
	const QString &existing)
: _controller(controller)
, _scheme(std::move(scheme))
, _content(this)
, _bottomShadow(this)
, _done(
		this,
		langFactory(lng_passport_save_value),
		st::passportPanelSaveValue) {
	setupControls(data, existing);
}

void PanelEditContact::setupControls(
		const QString &data,
		const QString &existing) {
	widthValue(
	) | rpl::start_with_next([=](int width) {
		_content->resizeToWidth(width);
	}, _content->lifetime());

	_content->add(object_ptr<BoxContentDivider>(
		_content,
		st::passportFormDividerHeight));
	if (!existing.isEmpty()) {
		_content->add(
			object_ptr<Info::Profile::Button>(
				_content,
				Lang::Viewer(
					lng_passport_use_existing__tagged
				) | rpl::map([=] {
					return lng_passport_use_existing(
						lt_existing,
						(_scheme.format
							? _scheme.format(existing)
							: existing));
				}),
				st::passportUploadButton),
			st::passportUploadButtonPadding
		)->addClickHandler([=] {
			save(existing);
		});
		_content->add(
			object_ptr<Ui::DividerLabel>(
				_content,
				object_ptr<Ui::FlatLabel>(
					_content,
					_scheme.aboutExisting,
					Ui::FlatLabel::InitType::Simple,
					st::boxDividerLabel),
				st::passportFormLabelPadding));
		_content->add(
			object_ptr<Ui::FlatLabel>(
				_content,
				_scheme.newHeader,
				Ui::FlatLabel::InitType::Simple,
				st::passportFormHeader),
			st::passportDetailsHeaderPadding);
	}
	const auto &fieldStyle = existing.isEmpty()
		? st::passportContactField
		: st::passportDetailsField;
	const auto fieldPadding = existing.isEmpty()
		? st::passportContactFieldPadding
		: st::passportContactNewFieldPadding;
	const auto fieldPlaceholder = existing.isEmpty()
		? _scheme.newPlaceholder
		: nullptr;
	auto wrap = object_ptr<Ui::RpWidget>(_content);
	if (_scheme.type == Scheme::ValueType::Phone) {
		_field = Ui::CreateChild<Ui::PhoneInput>(
			wrap.data(),
			fieldStyle,
			fieldPlaceholder,
			data);
	} else {
		_field = Ui::CreateChild<Ui::MaskedInputField>(
			wrap.data(),
			fieldStyle,
			fieldPlaceholder,
			data);
	}

	_field->move(0, 0);
	_field->heightValue(
	) | rpl::start_with_next([=, pointer = wrap.data()](int height) {
		pointer->resize(pointer->width(), height);
	}, _field->lifetime());
	wrap->widthValue(
	) | rpl::start_with_next([=](int width) {
		_field->resize(width, _field->height());
	}, _field->lifetime());

	_content->add(std::move(wrap), fieldPadding);
	const auto errorWrap = _content->add(
		object_ptr<Ui::SlideWrap<Ui::FlatLabel>>(
			_content,
			object_ptr<Ui::FlatLabel>(
				_content,
				QString(),
				Ui::FlatLabel::InitType::Simple,
				st::passportVerifyErrorLabel),
			st::passportContactErrorPadding),
		st::passportContactErrorMargin);
	errorWrap->hide(anim::type::instant);

	_content->add(
		object_ptr<Ui::DividerLabel>(
			_content,
			object_ptr<Ui::FlatLabel>(
				_content,
				_scheme.aboutNew,
				Ui::FlatLabel::InitType::Simple,
				st::boxDividerLabel),
			st::passportFormLabelPadding));

	if (auto text = _controller->deleteValueLabel()) {
		_content->add(
			object_ptr<Info::Profile::Button>(
				_content,
				std::move(*text) | Info::Profile::ToUpperValue(),
				st::passportDeleteButton),
			st::passportUploadButtonPadding
		)->addClickHandler([=] {
			_controller->deleteValue();
		});
	}

	_controller->saveErrors(
	) | rpl::start_with_next([=](const ScopeError &error) {
		if (error.key == QString("value")) {
			_field->showError();
			errorWrap->entity()->setText(error.text);
			errorWrap->show(anim::type::normal);
		}
	}, lifetime());

	const auto submit = [=] {
		crl::on_main(this, [=] {
			save();
		});
	};
	connect(_field, &Ui::MaskedInputField::submitted, submit);
	connect(_field, &Ui::MaskedInputField::changed, [=] {
		errorWrap->hide(anim::type::normal);
	});
	_done->addClickHandler(submit);
}

void PanelEditContact::focusInEvent(QFocusEvent *e) {
	crl::on_main(this, [=] {
		_field->setFocusFast();
	});
}

void PanelEditContact::resizeEvent(QResizeEvent *e) {
	updateControlsGeometry();
}

void PanelEditContact::updateControlsGeometry() {
	const auto submitTop = height() - _done->height();
	_bottomShadow->resizeToWidth(width());
	_bottomShadow->moveToLeft(0, submitTop - st::lineWidth);
	_done->resizeToWidth(width());
	_done->moveToLeft(0, submitTop);
}

void PanelEditContact::save() {
	const auto result = _field->getLastText();
	const auto processed = _scheme.postprocess
		? _scheme.postprocess(result)
		: result;
	if (_scheme.validate && !_scheme.validate(processed)) {
		_field->showError();
		return;
	}
	save(processed);
}

void PanelEditContact::save(const QString &value) {
	auto data = ValueMap();
	data.fields["value"].text = value;
	_controller->saveScope(std::move(data), {});
}

object_ptr<BoxContent> VerifyPhoneBox(
		const QString &phone,
		int codeLength,
		Fn<void(QString code)> submit,
		rpl::producer<QString> call,
		rpl::producer<QString> error) {
	return Box<VerifyBox>(
		lang(lng_passport_phone_title),
		lng_passport_confirm_phone(lt_phone, App::formatPhone(phone)),
		codeLength,
		submit,
		std::move(call),
		std::move(error));
}

object_ptr<BoxContent> VerifyEmailBox(
		const QString &email,
		int codeLength,
		Fn<void(QString code)> submit,
		rpl::producer<QString> error) {
	return Box<VerifyBox>(
		lang(lng_passport_email_title),
		lng_passport_confirm_email(lt_email, email),
		codeLength,
		submit,
		rpl::single(QString()),
		std::move(error));
}

} // namespace Passport
