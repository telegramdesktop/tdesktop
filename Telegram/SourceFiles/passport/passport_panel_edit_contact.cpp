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
		Fn<void()> resend,
		rpl::producer<QString> call,
		rpl::producer<QString> error,
		rpl::producer<QString> resent);

	void setInnerFocus() override;

protected:
	void prepare() override;

private:
	void setupControls(
		const QString &text,
		int codeLength,
		Fn<void(QString code)> submit,
		Fn<void()> resend,
		rpl::producer<QString> call,
		rpl::producer<QString> error,
		rpl::producer<QString> resent);

	QString _title;
	Fn<void()> _submit;
	QPointer<SentCodeField> _code;
	QPointer<Ui::VerticalLayout> _content;

};

VerifyBox::VerifyBox(
	QWidget*,
	const QString &title,
	const QString &text,
	int codeLength,
	Fn<void(QString code)> submit,
	Fn<void()> resend,
	rpl::producer<QString> call,
	rpl::producer<QString> error,
	rpl::producer<QString> resent)
: _title(title) {
	setupControls(
		text,
		codeLength,
		submit,
		resend,
		std::move(call),
		std::move(error),
		std::move(resent));
}

void VerifyBox::setupControls(
		const QString &text,
		int codeLength,
		Fn<void(QString code)> submit,
		Fn<void()> resend,
		rpl::producer<QString> call,
		rpl::producer<QString> error,
		rpl::producer<QString> resent) {
	_content = Ui::CreateChild<Ui::VerticalLayout>(this);

	const auto small = style::margins(
		st::boxPadding.left(),
		0,
		st::boxPadding.right(),
		st::boxPadding.bottom());
	const auto description = _content->add(
		object_ptr<Ui::FlatLabel>(
			_content,
			text,
			Ui::FlatLabel::InitType::Simple,
			st::boxLabel),
		small);
	_code = _content->add(
		object_ptr<SentCodeField>(
			_content,
			st::defaultInputField,
			langFactory(lng_change_phone_code_title)),
		small);

	const auto problem = _content->add(
		object_ptr<Ui::FadeWrap<Ui::FlatLabel>>(
			_content,
			object_ptr<Ui::FlatLabel>(
				_content,
				QString(),
				Ui::FlatLabel::InitType::Simple,
				st::passportVerifyErrorLabel)),
		small);
	const auto waiter = _content->add(
		object_ptr<Ui::FlatLabel>(
			_content,
			std::move(call),
			st::boxDividerLabel),
		small);
	if (resend) {
		auto link = TextWithEntities{ lang(lng_cloud_password_resend) };
		link.entities.push_back(EntityInText(
			EntityInTextCustomUrl,
			0,
			link.text.size(),
			QString("internal:resend")));
		const auto label = _content->add(
			object_ptr<Ui::FlatLabel>(
				_content,
				rpl::single(
					link
				) | rpl::then(rpl::duplicate(
					resent
				) | rpl::map([](const QString &value) {
					return TextWithEntities{ value };
				})),
				st::boxDividerLabel),
			small);
		std::move(
			resent
		) | rpl::start_with_next([=] {
			_content->resizeToWidth(st::boxWidth);
		}, _content->lifetime());
		label->setClickHandlerFilter([=](auto&&...) {
			resend();
			return false;
		});
	}
	std::move(
		error
	) | rpl::start_with_next([=](const QString &error) {
		if (error.isEmpty()) {
			problem->hide(anim::type::normal);
		} else {
			problem->entity()->setText(error);
			_content->resizeToWidth(st::boxWidth);
			problem->show(anim::type::normal);
			_code->showError();
		}
	}, lifetime());

	_submit = [=] {
		submit(_code->getDigitsOnly());
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

	_content->resizeToWidth(st::boxWidth);
	_content->heightValue(
	) | rpl::start_with_next([=](int height) {
		setDimensions(st::boxWidth, height);
	}, _content->lifetime());
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
			_content->resizeToWidth(width());
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
		nullptr,
		std::move(call),
		std::move(error),
		rpl::never<QString>());
}

object_ptr<BoxContent> VerifyEmailBox(
		const QString &email,
		int codeLength,
		Fn<void(QString code)> submit,
		Fn<void()> resend,
		rpl::producer<QString> error,
		rpl::producer<QString> resent) {
	return Box<VerifyBox>(
		lang(lng_passport_email_title),
		lng_passport_confirm_email(lt_email, email),
		codeLength,
		submit,
		resend,
		rpl::single(QString()),
		std::move(error),
		std::move(resent));
}

} // namespace Passport
