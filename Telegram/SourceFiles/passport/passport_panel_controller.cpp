/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "passport/passport_panel_controller.h"

#include "lang/lang_keys.h"
#include "passport/passport_panel_edit_document.h"
#include "passport/passport_panel_details_row.h"
#include "passport/passport_panel_edit_contact.h"
#include "passport/passport_panel_edit_scans.h"
#include "passport/passport_panel.h"
#include "boxes/confirm_box.h"
#include "ui/countryinput.h"
#include "layout.h"

namespace Passport {

EditDocumentScheme GetDocumentScheme(
		Scope::Type type,
		base::optional<Value::Type> scansType) {
	using Scheme = EditDocumentScheme;
	using ValueClass = Scheme::ValueClass;
	const auto DontFormat = nullptr;
	const auto CountryFormat = [](const QString &value) {
		const auto result = CountrySelectBox::NameByISO(value);
		return result.isEmpty() ? value : result;
	};
	const auto GenderFormat = [](const QString &value) {
		if (value == qstr("male")) {
			return lang(lng_passport_gender_male);
		} else if (value == qstr("female")) {
			return lang(lng_passport_gender_female);
		}
		return value;
	};
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
	const auto CountryValidate = [=](const QString &value) {
		return !CountryFormat(value).isEmpty();
	};

	switch (type) {
	case Scope::Type::Identity: {
		auto result = Scheme();
		result.rowsHeader = lang(lng_passport_personal_details);
		if (scansType) {
			switch (*scansType) {
			case Value::Type::Passport:
				result.scansHeader = lang(lng_passport_identity_passport);
				break;
			case Value::Type::DriverLicense:
				result.scansHeader = lang(lng_passport_identity_license);
				break;
			case Value::Type::IdentityCard:
				result.scansHeader = lang(lng_passport_identity_card);
				break;
			default:
				Unexpected("scansType in GetDocumentScheme:Identity.");
			}
		}
		result.rows = {
			{
				ValueClass::Fields,
				PanelDetailsType::Text,
				qsl("first_name"),
				lang(lng_passport_first_name),
				NotEmptyValidate,
				DontFormat,
			},
			{
				ValueClass::Fields,
				PanelDetailsType::Text,
				qsl("last_name"),
				lang(lng_passport_last_name),
				DontValidate,
				DontFormat,
			},
			{
				ValueClass::Fields,
				PanelDetailsType::Date,
				qsl("birth_date"),
				lang(lng_passport_birth_date),
				DateValidate,
				DontFormat,
			},
			{
				ValueClass::Fields,
				PanelDetailsType::Gender,
				qsl("gender"),
				lang(lng_passport_gender),
				GenderValidate,
				GenderFormat,
			},
			{
				ValueClass::Fields,
				PanelDetailsType::Country,
				qsl("country_code"),
				lang(lng_passport_country),
				CountryValidate,
				CountryFormat,
			},
			{
				ValueClass::Scans,
				PanelDetailsType::Text,
				qsl("document_no"),
				lang(lng_passport_document_number),
				NotEmptyValidate,
				DontFormat,
			},
			{
				ValueClass::Scans,
				PanelDetailsType::Date,
				qsl("expiry_date"),
				lang(lng_passport_expiry_date),
				DateOrEmptyValidate,
				DontFormat,
			},
		};
		return result;
	} break;

	case Scope::Type::Address: {
		auto result = Scheme();
		result.rowsHeader = lang(lng_passport_address);
		if (scansType) {
			switch (*scansType) {
			case Value::Type::UtilityBill:
				result.scansHeader = lang(lng_passport_address_bill);
				break;
			case Value::Type::BankStatement:
				result.scansHeader = lang(lng_passport_address_statement);
				break;
			case Value::Type::RentalAgreement:
				result.scansHeader = lang(lng_passport_address_agreement);
				break;
			default:
				Unexpected("scansType in GetDocumentScheme:Address.");
			}
		}
		result.rows = {
			{
				ValueClass::Fields,
				PanelDetailsType::Text,
				qsl("street_line1"),
				lang(lng_passport_street),
				NotEmptyValidate,
				DontFormat,
			},
			{
				ValueClass::Fields,
				PanelDetailsType::Text,
				qsl("street_line2"),
				lang(lng_passport_street),
				DontValidate,
				DontFormat,
			},
			{
				ValueClass::Fields,
				PanelDetailsType::Text,
				qsl("city"),
				lang(lng_passport_city),
				NotEmptyValidate,
				DontFormat,
			},
			{
				ValueClass::Fields,
				PanelDetailsType::Text,
				qsl("state"),
				lang(lng_passport_state),
				DontValidate,
				DontFormat,
			},
			{
				ValueClass::Fields,
				PanelDetailsType::Country,
				qsl("country_code"),
				lang(lng_passport_country),
				CountryValidate,
				CountryFormat,
			},
			{
				ValueClass::Fields,
				PanelDetailsType::Text,
				qsl("post_code"),
				lang(lng_passport_postcode),
				NotEmptyValidate,
				DontFormat,
			},
		};
		return result;
	} break;
	}
	Unexpected("Type in GetDocumentScheme().");
}

EditContactScheme GetContactScheme(Scope::Type type) {
	using Scheme = EditContactScheme;
	using ValueType = Scheme::ValueType;

	switch (type) {
	case Scope::Type::Phone: {
		auto result = Scheme(ValueType::Phone);
		result.aboutExisting = lang(lng_passport_use_existing_phone);
		result.newHeader = lang(lng_passport_new_phone);
		result.aboutNew = lang(lng_passport_new_phone_code);
		result.validate = [](const QString &value) {
			return QRegularExpression(
				"^\\d{2,12}$"
			).match(value).hasMatch();
		};
		result.format = [](const QString &value) {
			return App::formatPhone(value);
		};
		result.postprocess = [](QString value) {
			return value.replace(QRegularExpression("[^\\d]"), QString());
		};
		return result;
	} break;

	case Scope::Type::Email: {
		auto result = Scheme(ValueType::Text);
		result.aboutExisting = lang(lng_passport_use_existing_email);
		result.newHeader = lang(lng_passport_new_email);
		result.newPlaceholder = langFactory(lng_passport_email_title);
		result.aboutNew = lang(lng_passport_new_email_code);
		result.validate = [](const QString &value) {
			const auto at = value.indexOf('@');
			const auto dot = value.lastIndexOf('.');
			return (at > 0) && (dot > at);
		};
		result.format = result.postprocess = [](const QString &value) {
			return value.trimmed();
		};
		return result;
	} break;
	}
	Unexpected("Type in GetContactScheme().");
}

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

	_form->verificationNeeded(
	) | rpl::start_with_next([=](not_null<const Value*> value) {
		processVerificationNeeded(value);
	}, lifetime());

	_form->verificationUpdate(
	) | rpl::filter([=](not_null<const Value*> field) {
		return (field->verification.codeLength == 0);
	}) | rpl::start_with_next([=](not_null<const Value*> field) {
		_verificationBoxes.erase(field);
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
		const auto row = ComputeScopeRow(scope);
		callback(
			row.title,
			row.ready.isEmpty() ? row.description : row.ready,
			!row.ready.isEmpty());
	}
}

void PanelController::submitForm() {
	_form->submit();
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
	Expects(_editDocumentIndex >= 0
		&& _editDocumentIndex < _editScope->documents.size());

	_form->uploadScan(
		_editScope->documents[_editDocumentIndex],
		std::move(content));
}

void PanelController::deleteScan(int fileIndex) {
	Expects(_editScope != nullptr);
	Expects(_editDocumentIndex >= 0
		&& _editDocumentIndex < _editScope->documents.size());

	_form->deleteScan(
		_editScope->documents[_editDocumentIndex],
		fileIndex);
}

void PanelController::restoreScan(int fileIndex) {
	Expects(_editScope != nullptr);
	Expects(_editDocumentIndex >= 0
		&& _editDocumentIndex < _editScope->documents.size());

	_form->restoreScan(
		_editScope->documents[_editDocumentIndex],
		fileIndex);
}

void PanelController::uploadSelfie(QByteArray &&content) {
	Expects(_editScope != nullptr);
	Expects(_editDocumentIndex >= 0
		&& _editDocumentIndex < _editScope->documents.size());
	Expects(_editScope->selfieRequired);

	_form->uploadSelfie(
		_editScope->documents[_editDocumentIndex],
		std::move(content));
}

void PanelController::deleteSelfie() {
	Expects(_editScope != nullptr);
	Expects(_editDocumentIndex >= 0
		&& _editDocumentIndex < _editScope->documents.size());
	Expects(_editScope->selfieRequired);

	_form->deleteSelfie(
		_editScope->documents[_editDocumentIndex]);
}

void PanelController::restoreSelfie() {
	Expects(_editScope != nullptr);
	Expects(_editDocumentIndex >= 0
		&& _editDocumentIndex < _editScope->documents.size());
	Expects(_editScope->selfieRequired);

	_form->restoreSelfie(
		_editScope->documents[_editDocumentIndex]);
}

rpl::producer<ScanInfo> PanelController::scanUpdated() const {
	return _form->scanUpdated(
	) | rpl::filter([=](not_null<const EditFile*> file) {
		return (_editScope != nullptr)
			&& (_editDocumentIndex >= 0)
			&& (file->value == _editScope->documents[_editDocumentIndex]);
	}) | rpl::map([=](not_null<const EditFile*> file) {
		return collectScanInfo(*file);
	});
}

ScanInfo PanelController::collectScanInfo(const EditFile &file) const {
	Expects(_editScope != nullptr);
	Expects(_editDocumentIndex >= 0);

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
	const auto &documents = _editScope->documents;
	auto isSelfie = (file.value == documents[_editDocumentIndex])
		&& (documents[_editDocumentIndex]->selfieInEdit.has_value())
		&& (&file == &*documents[_editDocumentIndex]->selfieInEdit);
	return {
		FileKey{ file.fields.id, file.fields.dcId },
		status,
		file.fields.image,
		file.deleted,
		isSelfie };
}

QString PanelController::getDefaultContactValue(Scope::Type type) const {
	switch (type) {
	case Scope::Type::Phone:
		return _form->defaultPhoneNumber();
	case Scope::Type::Email:
		return _form->defaultEmail();
	}
	Unexpected("Type in PanelController::getDefaultContactValue().");
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

int PanelController::findNonEmptyIndex(
		const std::vector<not_null<const Value*>> &files) const {
	const auto i = ranges::find_if(files, [](not_null<const Value*> file) {
		return !file->scans.empty();
	});
	if (i != end(files)) {
		return (i - begin(files));
	}
	return -1;
}


void PanelController::editScope(int index) {
	Expects(_panel != nullptr);
	Expects(index >= 0 && index < _scopes.size());

	if (_scopes[index].documents.empty()) {
		editScope(index, -1);
	} else {
		const auto documentIndex = findNonEmptyIndex(
			_scopes[index].documents);
		if (documentIndex >= 0) {
			editScope(index, documentIndex);
		} else if (_scopes[index].documents.size() > 1) {
			requestScopeFilesType(index);
		} else {
			editWithUpload(index, 0);
		}
	}
}

void PanelController::requestScopeFilesType(int index) {
	Expects(_panel != nullptr);
	Expects(index >= 0 && index < _scopes.size());

	const auto type = _scopes[index].type;
	_scopeDocumentTypeBox = [&] {
		if (type == Scope::Type::Identity) {
			return show(RequestIdentityType(
				[=](int documentIndex) {
					editWithUpload(index, documentIndex);
				},
				ranges::view::all(
					_scopes[index].documents
				) | ranges::view::transform([](auto value) {
					return value->type;
				}) | ranges::view::transform([](Value::Type type) {
					switch (type) {
					case Value::Type::Passport:
						return lang(lng_passport_identity_passport);
					case Value::Type::IdentityCard:
						return lang(lng_passport_identity_card);
					case Value::Type::DriverLicense:
						return lang(lng_passport_identity_license);
					default:
						Unexpected("IdentityType in requestScopeFilesType");
					}
				}) | ranges::to_vector));
		} else if (type == Scope::Type::Address) {
			return show(RequestAddressType(
				[=](int documentIndex) {
					editWithUpload(index, documentIndex);
				},
				ranges::view::all(
					_scopes[index].documents
				) | ranges::view::transform([](auto value) {
					return value->type;
				}) | ranges::view::transform([](Value::Type type) {
					switch (type) {
					case Value::Type::UtilityBill:
						return lang(lng_passport_address_bill);
					case Value::Type::BankStatement:
						return lang(lng_passport_address_statement);
					case Value::Type::RentalAgreement:
						return lang(lng_passport_address_agreement);
					default:
						Unexpected("AddressType in requestScopeFilesType");
					}
				}) | ranges::to_vector));
		} else {
			Unexpected("Type in processVerificationNeeded.");
		}
	}();
}

void PanelController::editWithUpload(int index, int documentIndex) {
	Expects(_panel != nullptr);
	Expects(index >= 0 && index < _scopes.size());
	Expects(documentIndex >= 0
		&& documentIndex < _scopes[index].documents.size());

	EditScans::ChooseScan(
		base::lambda_guarded(_panel.get(),
		[=](QByteArray &&content) {
			base::take(_scopeDocumentTypeBox);
			editScope(index, documentIndex);
			uploadScan(std::move(content));
		}));
}

void PanelController::editScope(int index, int documentIndex) {
	Expects(_panel != nullptr);
	Expects(index >= 0 && index < _scopes.size());
	Expects((documentIndex < 0)
		|| (documentIndex >= 0
			&& documentIndex < _scopes[index].documents.size()));

	_editScope = &_scopes[index];
	_editDocumentIndex = documentIndex;

	_form->startValueEdit(_editScope->fields);
	if (_editDocumentIndex >= 0) {
		_form->startValueEdit(_editScope->documents[_editDocumentIndex]);
	}

	auto content = [&]() -> object_ptr<Ui::RpWidget> {
		switch (_editScope->type) {
		case Scope::Type::Identity:
		case Scope::Type::Address: {
			const auto &documents = _editScope->documents;
			auto result = (_editDocumentIndex >= 0)
				? object_ptr<PanelEditDocument>(
					_panel.get(),
					this,
					GetDocumentScheme(
						_editScope->type,
						documents[_editDocumentIndex]->type),
					_editScope->fields->data.parsedInEdit,
					documents[_editDocumentIndex]->data.parsedInEdit,
					valueFiles(*documents[_editDocumentIndex]),
					(_editScope->selfieRequired
						? valueSelfie(*documents[_editDocumentIndex])
						: nullptr))
				: object_ptr<PanelEditDocument>(
					_panel.get(),
					this,
					GetDocumentScheme(_editScope->type),
					_editScope->fields->data.parsedInEdit);
			const auto weak = make_weak(result.data());
			_panelHasUnsavedChanges = [=] {
				return weak ? weak->hasUnsavedChanges() : false;
			};
			return std::move(result);
		} break;
		case Scope::Type::Phone:
		case Scope::Type::Email: {
			const auto &parsed = _editScope->fields->data.parsedInEdit;
			const auto valueIt = parsed.fields.find("value");
			_panelHasUnsavedChanges = nullptr;
			return object_ptr<PanelEditContact>(
				_panel.get(),
				this,
				std::move(GetContactScheme(_editScope->type)),
				(valueIt == end(parsed.fields)
					? QString()
					: valueIt->second),
				getDefaultContactValue(_editScope->type));
		} break;
		}
		Unexpected("Type in PanelController::editScope().");
	}();

	content->lifetime().add([=] {
		cancelValueEdit();
	});

	_panel->setBackAllowed(true);

	_panel->backRequests(
	) | rpl::start_with_next([=] {
		cancelEditScope();
	}, content->lifetime());

	_form->valueSaveFinished(
	) | rpl::start_with_next([=](not_null<const Value*> value) {
		processValueSaveFinished(value);
	}, content->lifetime());

	_panel->showEditValue(std::move(content));
}

void PanelController::processValueSaveFinished(
		not_null<const Value*> value) {
	Expects(_editScope != nullptr);

	const auto boxIt = _verificationBoxes.find(value);
	if (boxIt != end(_verificationBoxes)) {
		const auto saved = std::move(boxIt->second);
		_verificationBoxes.erase(boxIt);
	}

	const auto value1 = _editScope->fields;
	const auto value2 = (_editDocumentIndex >= 0)
		? _editScope->documents[_editDocumentIndex].get()
		: nullptr;
	if (value == value1 || value == value2) {
		if (!_form->savingValue(value1)
			&& (!value2 || !_form->savingValue(value2))) {
			_panel->showForm();
		}
	}
}

void PanelController::processVerificationNeeded(
		not_null<const Value*> value) {
	const auto i = _verificationBoxes.find(value);
	if (i != _verificationBoxes.end()) {
		LOG(("API Error: Requesting for verification repeatedly."));
		return;
	}
	const auto textIt = value->data.parsedInEdit.fields.find("value");
	Assert(textIt != end(value->data.parsedInEdit.fields));
	const auto text = textIt->second;
	const auto type = value->type;
	const auto update = _form->verificationUpdate(
	) | rpl::filter([=](not_null<const Value*> field) {
		return (field == value);
	});
	const auto box = [&] {
		if (type == Value::Type::Phone) {
			return show(VerifyPhoneBox(
				text,
				value->verification.codeLength,
				[=](const QString &code) { _form->verify(value, code); },

				value->verification.call ? rpl::single(
					value->verification.call->getText()
				) | rpl::then(rpl::duplicate(
					update
				) | rpl::filter([=](not_null<const Value*> field) {
					return field->verification.call != nullptr;
				}) | rpl::map([=](not_null<const Value*> field) {
					return field->verification.call->getText();
				})) : (rpl::single(QString()) | rpl::type_erased()),

				rpl::duplicate(
					update
				) | rpl::map([=](not_null<const Value*> field) {
					return field->verification.error;
				}) | rpl::distinct_until_changed()));
		} else if (type == Value::Type::Email) {
			return show(VerifyEmailBox(
				text,
				value->verification.codeLength,
				[=](const QString &code) { _form->verify(value, code); },

				rpl::duplicate(
					update
				) | rpl::map([=](not_null<const Value*> field) {
					return field->verification.error;
				}) | rpl::distinct_until_changed()));
		} else {
			Unexpected("Type in processVerificationNeeded.");
		}
	}();

	box->boxClosing(
	) | rpl::start_with_next([=] {
		_form->cancelValueVerification(value);
	}, lifetime());

	_verificationBoxes.emplace(value, box);
}

std::vector<ScanInfo> PanelController::valueFiles(
		const Value &value) const {
	auto result = std::vector<ScanInfo>();
	for (const auto &scan : value.scansInEdit) {
		result.push_back(collectScanInfo(scan));
	}
	return result;
}

std::unique_ptr<ScanInfo> PanelController::valueSelfie(
		const Value &value) const {
	if (value.selfieInEdit) {
		return std::make_unique<ScanInfo>(
			collectScanInfo(*value.selfieInEdit));
	}
	return std::make_unique<ScanInfo>();
}

void PanelController::cancelValueEdit() {
	if (const auto scope = base::take(_editScope)) {
		_form->cancelValueEdit(scope->fields);
		const auto index = std::exchange(_editDocumentIndex, -1);
		if (index >= 0) {
			_form->cancelValueEdit(scope->documents[index]);
		}
	}
}

void PanelController::saveScope(ValueMap &&data, ValueMap &&filesData) {
	Expects(_panel != nullptr);
	Expects(_editScope != nullptr);

	_form->saveValueEdit(_editScope->fields, std::move(data));
	if (_editDocumentIndex >= 0) {
		_form->saveValueEdit(
			_editScope->documents[_editDocumentIndex],
			std::move(filesData));
	} else {
		Assert(filesData.fields.empty());
	}
}

bool PanelController::editScopeChanged(
		const ValueMap &data,
		const ValueMap &filesData) const {
	Expects(_editScope != nullptr);

	if (_form->editValueChanged(_editScope->fields, data)) {
		return true;
	} else if (_editDocumentIndex >= 0) {
		return _form->editValueChanged(
			_editScope->documents[_editDocumentIndex],
			filesData);
	}
	return false;
}

void PanelController::cancelEditScope() {
	Expects(_editScope != nullptr);

	if (_panelHasUnsavedChanges && _panelHasUnsavedChanges()) {
		if (!_confirmForgetChangesBox) {
			_confirmForgetChangesBox = BoxPointer(show(Box<ConfirmBox>(
				lang(lng_passport_sure_cancel),
				lang(lng_continue),
				[=] {
					_panel->showForm();
					base::take(_confirmForgetChangesBox);
				})).data());
		}
	} else {
		_panel->showForm();
	}
}

void PanelController::cancelAuth() {
	_form->cancel();
}

void PanelController::showBox(object_ptr<BoxContent> box) {
	_panel->showBox(std::move(box));
}

rpl::lifetime &PanelController::lifetime() {
	return _lifetime;
}

} // namespace Passport
