/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "passport/passport_panel_controller.h"

#include "lang/lang_keys.h"
#include "passport/passport_panel_edit_document.h"
#include "passport/passport_panel.h"
#include "boxes/confirm_box.h"
#include "layout.h"

namespace Passport {
namespace {

PanelEditDocument::Scheme GetDocumentScheme(Scope::Type type) {
	using Scheme = PanelEditDocument::Scheme;

	const auto DontValidate = nullptr;
	const auto NotEmptyValidate = [](const QString &value) {
		return !value.isEmpty();
	};
	const auto DateValidate = [](const QString &value) {
		return QRegularExpression(
			"^\\d{2}\\.\\d{2}\\.\\d{4}$"
		).match(value).hasMatch();
	};
	const auto DateOrEmptyValidate = [=](const QString &value) {
		return value.isEmpty() || DateValidate(value);
	};
	const auto GenderValidate = [](const QString &value) {
		return value == qstr("male") || value == qstr("female");
	};
	const auto CountryValidate = [](const QString &value) {
		return QRegularExpression("^[A-Z]{2}$").match(value).hasMatch();
	};

	switch (type) {
	case Scope::Type::Identity: {
		auto result = Scheme();
		result.rows = {
			{
				Scheme::ValueType::Fields,
				qsl("first_name"),
				lang(lng_passport_first_name),
				NotEmptyValidate
			},
			{
				Scheme::ValueType::Fields,
				qsl("last_name"),
				lang(lng_passport_last_name),
				DontValidate
			},
			{
				Scheme::ValueType::Fields,
				qsl("birth_date"),
				lang(lng_passport_birth_date),
				DateValidate,
			},
			{
				Scheme::ValueType::Fields,
				qsl("gender"),
				lang(lng_passport_gender),
				GenderValidate,
			},
			{
				Scheme::ValueType::Fields,
				qsl("country_code"),
				lang(lng_passport_country),
				CountryValidate,
			},
			{
				Scheme::ValueType::Scans,
				qsl("document_no"),
				lang(lng_passport_document_number),
				NotEmptyValidate,
			},
			{
				Scheme::ValueType::Scans,
				qsl("expiry_date"),
				lang(lng_passport_expiry_date),
				DateOrEmptyValidate,
			},
		};
		return result;
	} break;

	case Scope::Type::Address: {
		auto result = Scheme();
		result.rows = {
			{
				Scheme::ValueType::Fields,
				qsl("street_line1"),
				lang(lng_passport_street),
				NotEmptyValidate
			},
			{
				Scheme::ValueType::Fields,
				qsl("street_line2"),
				lang(lng_passport_street),
				DontValidate
			},
			{
				Scheme::ValueType::Fields,
				qsl("city"),
				lang(lng_passport_city),
				NotEmptyValidate
			},
			{
				Scheme::ValueType::Fields,
				qsl("state"),
				lang(lng_passport_state),
				DontValidate
			},
			{
				Scheme::ValueType::Fields,
				qsl("country_code"),
				lang(lng_passport_country),
				CountryValidate
			},
			{
				Scheme::ValueType::Fields,
				qsl("post_code"),
				lang(lng_passport_postcode),
				NotEmptyValidate
			},
		};
		return result;
	} break;
	}
	Unexpected("Type in GetDocumentScheme().");
}

} // namespace

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
: _form(form)
, _scopes(ComputeScopes(_form)) {
	_form->secretReadyEvents(
	) | rpl::start_with_next([=] {
		if (_panel) {
			_panel->showForm();
		}
	}, lifetime());
	_scopes = ComputeScopes(_form);
}

not_null<UserData*> PanelController::bot() const {
	return _form->bot();
}

QString PanelController::privacyPolicyUrl() const {
	return _form->privacyPolicyUrl();
}

void PanelController::fillRows(
	base::lambda<void(
		QString title,
		QString description,
		bool ready)> callback) {
	if (_scopes.empty()) {
		_scopes = ComputeScopes(_form);
	}
	for (const auto &scope : _scopes) {
		switch (scope.type) {
		case Scope::Type::Identity:
			callback(
				lang(lng_passport_identity_title),
				lang(lng_passport_identity_description),
				false);
			break;
		case Scope::Type::Address:
			callback(
				lang(lng_passport_address_title),
				lang(lng_passport_address_description),
				false);
			break;
		case Scope::Type::Phone:
			callback(
				lang(lng_passport_phone_title),
				lang(lng_passport_phone_description),
				false);
			break;
		case Scope::Type::Email:
			callback(
				lang(lng_passport_email_title),
				lang(lng_passport_email_description),
				false);
			break;
		}
	}
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

void PanelController::uploadScan(QByteArray &&content) {
	Expects(_editScope != nullptr);
	Expects(_editScopeFilesIndex >= 0);

	_form->uploadScan(
		_editScope->files[_editScopeFilesIndex],
		std::move(content));
}

void PanelController::deleteScan(int fileIndex) {
	Expects(_editScope != nullptr);
	Expects(_editScopeFilesIndex >= 0);

	_form->deleteScan(
		_editScope->files[_editScopeFilesIndex],
		fileIndex);
}

void PanelController::restoreScan(int fileIndex) {
	Expects(_editScope != nullptr);
	Expects(_editScopeFilesIndex >= 0);

	_form->restoreScan(
		_editScope->files[_editScopeFilesIndex],
		fileIndex);
}

rpl::producer<ScanInfo> PanelController::scanUpdated() const {
	return _form->scanUpdated(
	) | rpl::filter([=](not_null<const EditFile*> file) {
		return (_editScope != nullptr)
			&& (_editScopeFilesIndex >= 0)
			&& (file->value == _editScope->files[_editScopeFilesIndex]);
	}) | rpl::map([=](not_null<const EditFile*> file) {
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

void PanelController::editScope(int index) {
	Expects(_panel != nullptr);
	Expects(index >= 0 && index < _scopes.size());

	_editScope = &_scopes[index];

	// #TODO select type for files index
	_editScopeFilesIndex = _scopes[index].files.empty() ? -1 : 0;

	_form->startValueEdit(_editScope->fields);
	if (_editScopeFilesIndex >= 0) {
		_form->startValueEdit(_editScope->files[_editScopeFilesIndex]);
	}

	auto content = [&]() -> object_ptr<Ui::RpWidget> {
		switch (_editScope->type) {
		case Scope::Type::Identity:
		case Scope::Type::Address:
			return (_editScopeFilesIndex >= 0)
				? object_ptr<PanelEditDocument>(
					_panel.get(),
					this,
					std::move(GetDocumentScheme(_editScope->type)),
					_editScope->fields->data.parsed,
					_editScope->files[_editScopeFilesIndex]->data.parsed,
					valueFiles(*_editScope->files[_editScopeFilesIndex]))
				: object_ptr<PanelEditDocument>(
					_panel.get(),
					this,
					std::move(GetDocumentScheme(_editScope->type)),
					_editScope->fields->data.parsed);
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
	if (const auto scope = base::take(_editScope)) {
		_form->startValueEdit(scope->fields);
		const auto index = std::exchange(_editScopeFilesIndex, -1);
		if (index >= 0) {
			_form->startValueEdit(scope->files[index]);
		}
	}
}

void PanelController::saveScope(ValueMap &&data, ValueMap &&filesData) {
	Expects(_panel != nullptr);
	Expects(_editScope != nullptr);

	const auto scope = base::take(_editScope);
	_form->saveValueEdit(scope->fields, std::move(data));
	const auto index = std::exchange(_editScopeFilesIndex, -1);
	if (index >= 0) {
		_form->saveValueEdit(scope->files[index], std::move(filesData));
	} else {
		Assert(filesData.fields.empty());
	}

	_panel->showForm();
}

void PanelController::cancelAuth() {
	_form->cancel();
}

rpl::lifetime &PanelController::lifetime() {
	return _lifetime;
}

} // namespace Passport
