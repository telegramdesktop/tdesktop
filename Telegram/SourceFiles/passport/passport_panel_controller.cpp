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
#include "ui/toast/toast.h"
#include "ui/countryinput.h"
#include "layout.h"

namespace Passport {

constexpr auto kMaxNameSize = 255;
constexpr auto kMaxDocumentSize = 24;
constexpr auto kMaxStreetSize = 64;
constexpr auto kMinCitySize = 2;
constexpr auto kMaxCitySize = 64;
constexpr auto kMinPostcodeSize = 2;
constexpr auto kMaxPostcodeSize = 12;

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
	const auto LimitedValidate = [](int max, int min = 1) {
		return [=](const QString &value) {
			return (value.size() >= min) && (value.size() <= max);
		};
	};
	const auto NameValidate = LimitedValidate(kMaxNameSize);
	const auto DocumentValidate = LimitedValidate(kMaxDocumentSize);
	const auto StreetValidate = LimitedValidate(kMaxStreetSize);
	const auto CityValidate = LimitedValidate(kMaxCitySize, kMinCitySize);
	const auto PostcodeValidate = LimitedValidate(
		kMaxPostcodeSize,
		kMinPostcodeSize);
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
				NameValidate,
				DontFormat,
				kMaxNameSize,
			},
			{
				ValueClass::Fields,
				PanelDetailsType::Text,
				qsl("last_name"),
				lang(lng_passport_last_name),
				NameValidate,
				DontFormat,
				kMaxNameSize,
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
				DocumentValidate,
				DontFormat,
				kMaxDocumentSize,
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
				StreetValidate,
				DontFormat,
				kMaxStreetSize,
			},
			{
				ValueClass::Fields,
				PanelDetailsType::Text,
				qsl("street_line2"),
				lang(lng_passport_street),
				DontValidate,
				DontFormat,
				kMaxStreetSize,
			},
			{
				ValueClass::Fields,
				PanelDetailsType::Text,
				qsl("city"),
				lang(lng_passport_city),
				CityValidate,
				DontFormat,
				kMaxStreetSize,
			},
			{
				ValueClass::Fields,
				PanelDetailsType::Text,
				qsl("state"),
				lang(lng_passport_state),
				DontValidate,
				DontFormat,
				kMaxStreetSize,
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
				PostcodeValidate,
				DontFormat,
				kMaxPostcodeSize,
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
		bool ready,
		bool error)> callback) {
	if (_scopes.empty()) {
		_scopes = ComputeScopes(_form);
	}
	for (const auto &scope : _scopes) {
		const auto row = ComputeScopeRow(scope);
		callback(
			row.title,
			(!row.error.isEmpty()
				? row.error
				: !row.ready.isEmpty()
				? row.ready
				: row.description),
			!row.ready.isEmpty(),
			!row.error.isEmpty());
	}
}

rpl::producer<> PanelController::refillRows() const {
	return rpl::merge(
		_submitFailed.events(),
		_form->valueSaveFinished() | rpl::map([] {
			return rpl::empty_value();
		}));
}

void PanelController::submitForm() {
	if (!_form->submit()) {
		_submitFailed.fire({});
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

bool PanelController::canAddScan() const {
	Expects(_editScope != nullptr);
	Expects(_editDocument != nullptr);

	return _form->canAddScan(_editDocument);
}

void PanelController::uploadScan(QByteArray &&content) {
	Expects(_editScope != nullptr);
	Expects(_editDocument != nullptr);

	_form->uploadScan(_editDocument, std::move(content));
}

void PanelController::deleteScan(int fileIndex) {
	Expects(_editScope != nullptr);
	Expects(_editDocument != nullptr);

	_form->deleteScan(_editDocument, fileIndex);
}

void PanelController::restoreScan(int fileIndex) {
	Expects(_editScope != nullptr);
	Expects(_editDocument != nullptr);

	_form->restoreScan(_editDocument, fileIndex);
}

void PanelController::uploadSelfie(QByteArray &&content) {
	Expects(_editScope != nullptr);
	Expects(_editDocument != nullptr);
	Expects(_editScope->selfieRequired);

	_form->uploadSelfie(_editDocument, std::move(content));
}

void PanelController::deleteSelfie() {
	Expects(_editScope != nullptr);
	Expects(_editDocument != nullptr);
	Expects(_editScope->selfieRequired);

	_form->deleteSelfie(_editDocument);
}

void PanelController::restoreSelfie() {
	Expects(_editScope != nullptr);
	Expects(_editDocument != nullptr);
	Expects(_editScope->selfieRequired);

	_form->restoreSelfie(_editDocument);
}

rpl::producer<ScanInfo> PanelController::scanUpdated() const {
	return _form->scanUpdated(
	) | rpl::filter([=](not_null<const EditFile*> file) {
		return (file->value == _editDocument);
	}) | rpl::map([=](not_null<const EditFile*> file) {
		return collectScanInfo(*file);
	});
}

ScanInfo PanelController::collectScanInfo(const EditFile &file) const {
	Expects(_editScope != nullptr);
	Expects(_editDocument != nullptr);

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
	auto isSelfie = (file.value == _editDocument)
		&& (_editDocument->selfieInEdit.has_value())
		&& (&file == &*_editDocument->selfieInEdit);
	return {
		FileKey{ file.fields.id, file.fields.dcId },
		status,
		file.fields.image,
		file.deleted,
		isSelfie };
}

auto PanelController::deleteValueLabel() const
-> base::optional<rpl::producer<QString>> {
	Expects(_editScope != nullptr);

	if (hasValueDocument()) {
		return Lang::Viewer(lng_passport_delete_document);
	}
	if (!hasValueFields()) {
		return base::none;
	}
	switch (_editScope->type) {
	case Scope::Type::Identity:
		return Lang::Viewer(lng_passport_delete_details);
	case Scope::Type::Address:
		return Lang::Viewer(lng_passport_delete_address);
	case Scope::Type::Email:
		return Lang::Viewer(lng_passport_delete_email);
	case Scope::Type::Phone:
		return Lang::Viewer(lng_passport_delete_phone);
	}
	Unexpected("Type in PanelController::deleteValueLabel.");
}

bool PanelController::hasValueDocument() const {
	Expects(_editScope != nullptr);

	if (!_editDocument) {
		return false;
	}
	return !_editDocument->data.parsed.fields.empty()
		|| !_editDocument->scans.empty()
		|| _editDocument->selfie.has_value();
}

bool PanelController::hasValueFields() const {
	Expects(_editValue != nullptr);

	return !_editValue->data.parsed.fields.empty();
}

void PanelController::deleteValue() {
	Expects(_editScope != nullptr);

	if (savingScope()) {
		return;
	}
	const auto text = [&] {
		switch (_editScope->type) {
		case Scope::Type::Identity:
			return lang(hasValueDocument()
				? lng_passport_delete_document_sure
				: lng_passport_delete_details_sure);
		case Scope::Type::Address:
			return lang(hasValueDocument()
				? lng_passport_delete_document_sure
				: lng_passport_delete_address_sure);
		case Scope::Type::Phone:
			return lang(lng_passport_delete_phone_sure);
		case Scope::Type::Email:
			return lang(lng_passport_delete_email_sure);
		}
		Unexpected("Type in deleteValue.");
	}();
	const auto checkbox = (hasValueDocument() && hasValueFields()) ? [&] {
		switch (_editScope->type) {
		case Scope::Type::Identity:
			return lang(lng_passport_delete_details);
		case Scope::Type::Address:
			return lang(lng_passport_delete_address);
		}
		Unexpected("Type in deleteValue.");
	}() : QString();

	_editScopeBoxes.emplace_back(show(ConfirmDeleteDocument(
		[=](bool withDetails) { deleteValueSure(withDetails); },
		text,
		checkbox)));
}

void PanelController::deleteValueSure(bool withDetails) {
	Expects(_editValue != nullptr);

	if (hasValueDocument()) {
		_form->deleteValueEdit(_editDocument);
	}
	if (withDetails || !hasValueDocument()) {
		_form->deleteValueEdit(_editValue);
	}
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
	_editValue = _editScope->fields;
	_editDocument = (documentIndex >= 0)
		? _scopes[index].documents[documentIndex].get()
		: nullptr;

	_form->startValueEdit(_editValue);
	if (_editDocument) {
		_form->startValueEdit(_editDocument);
	}

	auto content = [&]() -> object_ptr<Ui::RpWidget> {
		switch (_editScope->type) {
		case Scope::Type::Identity:
		case Scope::Type::Address: {
			auto result = _editDocument
				? object_ptr<PanelEditDocument>(
					_panel.get(),
					this,
					GetDocumentScheme(
						_editScope->type,
						_editDocument->type),
					_editValue->data.parsedInEdit,
					_editDocument->data.parsedInEdit,
					valueFiles(*_editDocument),
					(_editScope->selfieRequired
						? valueSelfie(*_editDocument)
						: nullptr))
				: object_ptr<PanelEditDocument>(
					_panel.get(),
					this,
					GetDocumentScheme(_editScope->type),
					_editValue->data.parsedInEdit);
			const auto weak = make_weak(result.data());
			_panelHasUnsavedChanges = [=] {
				return weak ? weak->hasUnsavedChanges() : false;
			};
			return std::move(result);
		} break;
		case Scope::Type::Phone:
		case Scope::Type::Email: {
			const auto &parsed = _editValue->data.parsedInEdit;
			const auto valueIt = parsed.fields.find("value");
			const auto value = (valueIt == end(parsed.fields)
				? QString()
				: valueIt->second);
			const auto existing = getDefaultContactValue(_editScope->type);
			_panelHasUnsavedChanges = nullptr;
			return object_ptr<PanelEditContact>(
				_panel.get(),
				this,
				GetContactScheme(_editScope->type),
				value,
				(existing.toLower().trimmed() != value.toLower().trimmed()
					? existing
					: QString()));
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

	if ((_editValue == value || _editDocument == value) && !savingScope()) {
		_panel->showForm();
	}
}

bool PanelController::savingScope() const {
	Expects(_editValue != nullptr);

	return _form->savingValue(_editValue)
		|| (_editDocument && _form->savingValue(_editDocument));
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
	Expects(_editScope != nullptr);

	_editScopeBoxes.clear();
	_form->cancelValueEdit(base::take(_editValue));
	if (const auto document = base::take(_editDocument)) {
		_form->cancelValueEdit(document);
	}
	_editScope = nullptr;
}

void PanelController::saveScope(ValueMap &&data, ValueMap &&filesData) {
	Expects(_panel != nullptr);
	Expects(_editValue != nullptr);

	if (savingScope()) {
		return;
	}

	_form->saveValueEdit(_editValue, std::move(data));
	if (_editDocument) {
		_form->saveValueEdit(_editDocument, std::move(filesData));
	} else {
		Assert(filesData.fields.empty());
	}
}

bool PanelController::editScopeChanged(
		const ValueMap &data,
		const ValueMap &filesData) const {
	Expects(_editValue != nullptr);

	if (_form->editValueChanged(_editValue, data)) {
		return true;
	} else if (_editDocument) {
		return _form->editValueChanged(_editDocument, filesData);
	}
	return false;
}

void PanelController::cancelEditScope() {
	Expects(_editScope != nullptr);

	if (_panelHasUnsavedChanges && _panelHasUnsavedChanges()) {
		if (!_confirmForgetChangesBox) {
			_confirmForgetChangesBox = show(Box<ConfirmBox>(
				lang(lng_passport_sure_cancel),
				lang(lng_continue),
				[=] { _panel->showForm(); }));
			_editScopeBoxes.emplace_back(_confirmForgetChangesBox);
		}
	} else {
		_panel->showForm();
	}
}

int PanelController::closeGetDuration() {
	if (_panel) {
		return _panel->hideAndDestroyGetDuration();
	}
	return 0;
}

void PanelController::cancelAuth() {
	_form->cancel();
}

void PanelController::showBox(object_ptr<BoxContent> box) {
	_panel->showBox(std::move(box));
}

void PanelController::showToast(const QString &text) {
	Expects(_panel != nullptr);

	auto toast = Ui::Toast::Config();
	toast.text = text;
	Ui::Toast::Show(_panel.get(), toast);
}

rpl::lifetime &PanelController::lifetime() {
	return _lifetime;
}

PanelController::~PanelController() = default;

} // namespace Passport
