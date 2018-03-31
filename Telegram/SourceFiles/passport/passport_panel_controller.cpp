/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "passport/passport_panel_controller.h"

#include "lang/lang_keys.h"
#include "passport/passport_panel_edit_identity.h"
#include "passport/passport_panel.h"
#include "boxes/confirm_box.h"
#include "layout.h"

namespace Passport {

BoxPointer::BoxPointer(QPointer<BoxContent> value)
: _value(value) {
}

BoxPointer::BoxPointer(BoxPointer &&other)
: _value(base::take(other._value)) {
}

BoxPointer &BoxPointer::operator=(BoxPointer &&other) {
	std::swap(_value, other._value);
	return *this;
}

BoxPointer::~BoxPointer() {
	if (const auto strong = get()) {
		strong->closeBox();
	}
}

BoxContent *BoxPointer::get() const {
	return _value.data();
}

BoxPointer::operator BoxContent*() const {
	return get();
}

BoxPointer::operator bool() const {
	return get();
}

BoxContent *BoxPointer::operator->() const {
	return get();
}

PanelController::PanelController(not_null<FormController*> form)
: _form(form) {
	_form->secretReadyEvents(
	) | rpl::start_with_next([=] {
		if (_panel) {
			_panel->showForm();
		}
	}, lifetime());
}

not_null<UserData*> PanelController::bot() const {
	return _form->bot();
}

void PanelController::fillRows(
	base::lambda<void(
		QString title,
		QString description,
		bool ready)> callback) {
	_form->enumerateRows([&](const Value &value) {
		switch (value.type) {
		case Value::Type::Identity:
			callback(
				lang(lng_passport_identity_title),
				lang(lng_passport_identity_description),
				false);
			break;
		case Value::Type::Address:
			callback(
				lang(lng_passport_address_title),
				lang(lng_passport_address_description),
				false);
			break;
		case Value::Type::Phone:
			callback(
				lang(lng_passport_phone_title),
				App::self()->phone(),
				true);
			break;
		case Value::Type::Email:
			callback(
				lang(lng_passport_email_title),
				lang(lng_passport_email_description),
				false);
			break;
		}
	});
}

void PanelController::submitPassword(const QString &password) {
	_form->submitPassword(password);
}

rpl::producer<QString> PanelController::passwordError() const {
	return _form->passwordError();
}

QString PanelController::passwordHint() const {
	return _form->passwordHint();
}

QString PanelController::defaultEmail() const {
	return _form->defaultEmail();
}

QString PanelController::defaultPhoneNumber() const {
	return _form->defaultPhoneNumber();
}

void PanelController::uploadScan(int valueIndex, QByteArray &&content) {
	Expects(_panel != nullptr);

	_form->uploadScan(valueIndex, std::move(content));
}

void PanelController::deleteScan(int valueIndex, int fileIndex) {
	Expects(_panel != nullptr);

	_form->deleteScan(valueIndex, fileIndex);
}

void PanelController::restoreScan(int valueIndex, int fileIndex) {
	Expects(_panel != nullptr);

	_form->restoreScan(valueIndex, fileIndex);
}

rpl::producer<ScanInfo> PanelController::scanUpdated() const {
	return _form->scanUpdated(
	) | rpl::map([=](not_null<const EditFile*> file) {
		return collectScanInfo(*file);
	});
}

ScanInfo PanelController::collectScanInfo(const EditFile &file) const {
	const auto status = [&] {
		if (file.fields.accessHash) {
			if (file.fields.downloadOffset < 0) {
				return lang(lng_attach_failed);
			} else if (file.fields.downloadOffset < file.fields.size) {
				return formatDownloadText(
					file.fields.downloadOffset,
					file.fields.size);
			} else {
				return lng_passport_scan_uploaded(
					lt_date,
					langDateTimeFull(ParseDateTime(file.fields.date)));
			}
		} else if (file.uploadData) {
			if (file.uploadData->offset < 0) {
				return lang(lng_attach_failed);
			} else if (file.uploadData->fullId) {
				return formatDownloadText(
					file.uploadData->offset,
					file.uploadData->bytes.size());
			} else {
				return lng_passport_scan_uploaded(
					lt_date,
					langDateTimeFull(ParseDateTime(file.fields.date)));
			}
		} else {
			return formatDownloadText(0, file.fields.size);
		}
	}();
	return {
		FileKey{ file.fields.id, file.fields.dcId },
		status,
		file.fields.image,
		file.deleted };
}

void PanelController::showAskPassword() {
	ensurePanelCreated();
	_panel->showAskPassword();
}

void PanelController::showNoPassword() {
	ensurePanelCreated();
	_panel->showNoPassword();
}

void PanelController::showPasswordUnconfirmed() {
	ensurePanelCreated();
	_panel->showPasswordUnconfirmed();
}

void PanelController::ensurePanelCreated() {
	if (!_panel) {
		_panel = std::make_unique<Panel>(this);
	}
}

void PanelController::editValue(int index) {
	ensurePanelCreated(); // #TODO passport testing
	Expects(_panel != nullptr);

	_editValue = _form->startValueEdit(index);
	Assert(_editValue != nullptr);

	auto content = [&]() -> object_ptr<Ui::RpWidget> {
		switch (_editValue->type) {
		case Value::Type::Identity:
			return object_ptr<PanelEditIdentity>(
				_panel.get(),
				this,
				index,
				_editValue->data.parsed,
				valueFiles(*_editValue));
		}
		return { nullptr };
	}();
	if (content) {
		_panel->setBackAllowed(true);
		_panel->backRequests(
		) | rpl::start_with_next([=] {
			cancelValueEdit(index);
			_panel->setBackAllowed(false);
			_panel->showForm();
		}, content->lifetime());
		_panel->showEditValue(std::move(content));
	} else {
		cancelValueEdit(index);
	}
}

std::vector<ScanInfo> PanelController::valueFiles(const Value &value) const {
	auto result = std::vector<ScanInfo>();
	for (const auto &file : value.filesInEdit) {
		result.push_back(collectScanInfo(file));
	}
	return result;
}

void PanelController::cancelValueEdit(int index) {
	if (base::take(_editValue)) {
		_form->cancelValueEdit(index);
	}
}

void PanelController::saveValue(int index, ValueMap &&data) {
	Expects(_panel != nullptr);
	Expects(_editValue != nullptr);

	_editValue->data.parsed = std::move(data);
	_editValue = nullptr;

	_panel->showForm();

	_form->saveValueEdit(index);
}

void PanelController::cancelAuth() {
	_form->cancel();
}

rpl::lifetime &PanelController::lifetime() {
	return _lifetime;
}

} // namespace Passport
