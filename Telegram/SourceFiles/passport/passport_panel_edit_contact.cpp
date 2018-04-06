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
#include "boxes/abstract_box.h"
#include "lang/lang_keys.h"
#include "styles/style_passport.h"

namespace Passport {

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
						(_scheme.preprocess
							? _scheme.preprocess(existing)
							: existing));
				}),
				st::passportUploadButton),
			st::passportUploadButtonPadding
		)->addClickHandler([=] {
			save(existing);
		});
		_content->add(
			object_ptr<PanelLabel>(
				_content,
				object_ptr<Ui::FlatLabel>(
					_content,
					_scheme.aboutExisting,
					Ui::FlatLabel::InitType::Simple,
					st::passportFormLabel),
				st::passportFormLabelPadding));
		_content->add(
			object_ptr<Ui::FlatLabel>(
				_content,
				_scheme.newHeader,
				Ui::FlatLabel::InitType::Simple,
				st::passportFormHeader),
			st::passportDetailsHeaderPadding);
		_field = _content->add(
			object_ptr<Ui::InputField>(
				_content,
				st::passportDetailsField),
			st::passportContactNewFieldPadding);
	} else {
		_field = _content->add(
			object_ptr<Ui::InputField>(
				_content,
				st::passportContactField,
				_scheme.newPlaceholder),
			st::passportContactFieldPadding);
	}
	_content->add(
		object_ptr<PanelLabel>(
			_content,
			object_ptr<Ui::FlatLabel>(
				_content,
				_scheme.aboutNew,
				Ui::FlatLabel::InitType::Simple,
				st::passportFormLabel),
			st::passportFormLabelPadding));

	_done->addClickHandler([=] {
		crl::on_main(this, [=] {
			save();
		});
	});
}

void PanelEditContact::focusInEvent(QFocusEvent *e) {
	_field->setFocusFast();
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
	data.fields["value"] = value;
	_controller->saveScope(std::move(data), {});
}

} // namespace Passport
