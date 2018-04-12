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
namespace {

PanelEditDocument::Scheme GetDocumentScheme(
		Scope::Type type,
		base::optional<Value::Type> scansType = base::none) {
	using Scheme = PanelEditDocument::Scheme;

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
				Scheme::ValueType::Fields,
				PanelDetailsType::Text,
				qsl("first_name"),
				lang(lng_passport_first_name),
				NotEmptyValidate,
				DontFormat,
			},
			{
				Scheme::ValueType::Fields,
				PanelDetailsType::Text,
				qsl("last_name"),
				lang(lng_passport_last_name),
				DontValidate,
				DontFormat,
			},
			{
				Scheme::ValueType::Fields,
				PanelDetailsType::Date,
				qsl("birth_date"),
				lang(lng_passport_birth_date),
				DateValidate,
				DontFormat,
			},
			{
				Scheme::ValueType::Fields,
				PanelDetailsType::Gender,
				qsl("gender"),
				lang(lng_passport_gender),
				GenderValidate,
				GenderFormat,
			},
			{
				Scheme::ValueType::Fields,
				PanelDetailsType::Country,
				qsl("country_code"),
				lang(lng_passport_country),
				CountryValidate,
				CountryFormat,
			},
			{
				Scheme::ValueType::Scans,
				PanelDetailsType::Text,
				qsl("document_no"),
				lang(lng_passport_document_number),
				NotEmptyValidate,
				DontFormat,
			},
			{
				Scheme::ValueType::Scans,
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
				Scheme::ValueType::Fields,
				PanelDetailsType::Text,
				qsl("street_line1"),
				lang(lng_passport_street),
				NotEmptyValidate,
				DontFormat,
			},
			{
				Scheme::ValueType::Fields,
				PanelDetailsType::Text,
				qsl("street_line2"),
				lang(lng_passport_street),
				DontValidate,
				DontFormat,
			},
			{
				Scheme::ValueType::Fields,
				PanelDetailsType::Text,
				qsl("city"),
				lang(lng_passport_city),
				NotEmptyValidate,
				DontFormat,
			},
			{
				Scheme::ValueType::Fields,
				PanelDetailsType::Text,
				qsl("state"),
				lang(lng_passport_state),
				DontValidate,
				DontFormat,
			},
			{
				Scheme::ValueType::Fields,
				PanelDetailsType::Country,
				qsl("country_code"),
				lang(lng_passport_country),
				CountryValidate,
				CountryFormat,
			},
			{
				Scheme::ValueType::Fields,
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

PanelEditContact::Scheme GetContactScheme(Scope::Type type) {
	using Scheme = PanelEditContact::Scheme;
	switch (type) {
	case Scope::Type::Phone: {
		auto result = Scheme(Scheme::ValueType::Phone);
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
		auto result = Scheme(Scheme::ValueType::Text);
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

auto PanelController::collectRowInfo(const Scope &scope) const -> Row {
	switch (scope.type) {
	case Scope::Type::Identity:
		if (scope.files.empty()) {
			return {
				lang(lng_passport_personal_details),
				lang(lng_passport_personal_details_enter)
			};
		} else if (scope.files.size() == 1) {
			switch (scope.files.front()->type) {
			case Value::Type::Passport:
				return {
					lang(lng_passport_identity_passport),
					lang(lng_passport_identity_passport_upload)
				};
			case Value::Type::IdentityCard:
				return {
					lang(lng_passport_identity_card),
					lang(lng_passport_identity_card_upload)
				};
			case Value::Type::DriverLicense:
				return {
					lang(lng_passport_identity_license),
					lang(lng_passport_identity_license_upload)
				};
			default: Unexpected("Identity type in collectRowInfo.");
			}
		}
		return {
			lang(lng_passport_identity_title),
			lang(lng_passport_identity_description)
		};
	case Scope::Type::Address:
		if (scope.files.empty()) {
			return {
				lang(lng_passport_address),
				lang(lng_passport_address_enter)
			};
		} else if (scope.files.size() == 1) {
			switch (scope.files.front()->type) {
			case Value::Type::BankStatement:
				return {
					lang(lng_passport_address_statement),
					lang(lng_passport_address_statement_upload)
				};
			case Value::Type::UtilityBill:
				return {
					lang(lng_passport_address_bill),
					lang(lng_passport_address_bill_upload)
				};
			case Value::Type::RentalAgreement:
				return {
					lang(lng_passport_address_agreement),
					lang(lng_passport_address_agreement_upload)
				};
			default: Unexpected("Address type in collectRowInfo.");
			}
		}
		return {
			lang(lng_passport_address_title),
			lang(lng_passport_address_description)
		};
	case Scope::Type::Phone:
		return {
			lang(lng_passport_phone_title),
			lang(lng_passport_phone_description)
		};
	case Scope::Type::Email:
		return {
			lang(lng_passport_email_title),
			lang(lng_passport_email_description)
		};
	default: Unexpected("Scope type in collectRowInfo.");
	}
}

QString PanelController::collectRowReadyString(const Scope &scope) const {
	switch (scope.type) {
	case Scope::Type::Identity:
	case Scope::Type::Address: {
		auto list = QStringList();
		const auto &fields = scope.fields->data.parsed.fields;
		const auto files = [&]() -> const Value* {
			for (const auto &files : scope.files) {
				if (!files->files.empty()) {
					return files;
				}
			}
			return nullptr;
		}();
		if (files && scope.files.size() > 1) {
			list.push_back([&] {
				switch (files->type) {
				case Value::Type::Passport:
					return lang(lng_passport_identity_passport);
				case Value::Type::DriverLicense:
					return lang(lng_passport_identity_license);
				case Value::Type::IdentityCard:
					return lang(lng_passport_identity_card);
				case Value::Type::BankStatement:
					return lang(lng_passport_address_statement);
				case Value::Type::UtilityBill:
					return lang(lng_passport_address_bill);
				case Value::Type::RentalAgreement:
					return lang(lng_passport_address_agreement);
				default: Unexpected("Files type in collectRowReadyString.");
				}
			}());
		}
		if (files
			&& (files->files.empty()
				|| (scope.selfieRequired && !files->selfie))) {
			return QString();
		}
		const auto scheme = GetDocumentScheme(scope.type);
		for (const auto &row : scheme.rows) {
			const auto format = row.format;
			if (row.type == PanelEditDocument::Scheme::ValueType::Fields) {
				const auto i = fields.find(row.key);
				if (i == end(fields)) {
					return QString();
				} else if (row.validate && !row.validate(i->second)) {
					return QString();
				}
				list.push_back(format ? format(i->second) : i->second);
			} else if (!files) {
				return QString();
			} else {
				const auto i = files->data.parsed.fields.find(row.key);
				if (i == end(files->data.parsed.fields)) {
					return QString();
				} else if (row.validate && !row.validate(i->second)) {
					return QString();
				}
				list.push_back(i->second);
			}
		}
		return list.join(", ");
	} break;
	case Scope::Type::Phone:
	case Scope::Type::Email: {
		const auto format = GetContactScheme(scope.type).format;
		const auto &fields = scope.fields->data.parsed.fields;
		const auto i = fields.find("value");
		return (i != end(fields))
			? (format ? format(i->second) : i->second)
			: QString();
	} break;
	}
	Unexpected("Scope type in collectRowReadyString.");
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
		const auto row = collectRowInfo(scope);
		const auto ready = collectRowReadyString(scope);
		callback(
			row.title,
			ready.isEmpty() ? row.description : ready,
			!ready.isEmpty());
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
	Expects(_editScopeFilesIndex >= 0
		&& _editScopeFilesIndex < _editScope->files.size());

	_form->uploadScan(
		_editScope->files[_editScopeFilesIndex],
		std::move(content));
}

void PanelController::deleteScan(int fileIndex) {
	Expects(_editScope != nullptr);
	Expects(_editScopeFilesIndex >= 0
		&& _editScopeFilesIndex < _editScope->files.size());

	_form->deleteScan(
		_editScope->files[_editScopeFilesIndex],
		fileIndex);
}

void PanelController::restoreScan(int fileIndex) {
	Expects(_editScope != nullptr);
	Expects(_editScopeFilesIndex >= 0
		&& _editScopeFilesIndex < _editScope->files.size());

	_form->restoreScan(
		_editScope->files[_editScopeFilesIndex],
		fileIndex);
}

void PanelController::uploadSelfie(QByteArray &&content) {
	Expects(_editScope != nullptr);
	Expects(_editScopeFilesIndex >= 0
		&& _editScopeFilesIndex < _editScope->files.size());
	Expects(_editScope->selfieRequired);

	_form->uploadSelfie(
		_editScope->files[_editScopeFilesIndex],
		std::move(content));
}

void PanelController::deleteSelfie() {
	Expects(_editScope != nullptr);
	Expects(_editScopeFilesIndex >= 0
		&& _editScopeFilesIndex < _editScope->files.size());
	Expects(_editScope->selfieRequired);

	_form->deleteSelfie(
		_editScope->files[_editScopeFilesIndex]);
}

void PanelController::restoreSelfie() {
	Expects(_editScope != nullptr);
	Expects(_editScopeFilesIndex >= 0
		&& _editScopeFilesIndex < _editScope->files.size());
	Expects(_editScope->selfieRequired);

	_form->restoreSelfie(
		_editScope->files[_editScopeFilesIndex]);
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
	Expects(_editScope != nullptr);
	Expects(_editScopeFilesIndex >= 0);

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
	const auto &files = _editScope->files;
	auto isSelfie = (file.value == files[_editScopeFilesIndex])
		&& (files[_editScopeFilesIndex]->selfieInEdit.has_value())
		&& (&file == &*files[_editScopeFilesIndex]->selfieInEdit);
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
		return !file->files.empty();
	});
	if (i != end(files)) {
		return (i - begin(files));
	}
	return -1;
}


void PanelController::editScope(int index) {
	Expects(_panel != nullptr);
	Expects(index >= 0 && index < _scopes.size());

	if (_scopes[index].files.empty()) {
		editScope(index, -1);
	} else {
		const auto filesIndex = findNonEmptyIndex(_scopes[index].files);
		if (filesIndex >= 0) {
			editScope(index, filesIndex);
		} else if (_scopes[index].files.size() > 1) {
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
	_scopeFilesTypeBox = [&] {
		if (type == Scope::Type::Identity) {
			return show(RequestIdentityType(
				[=](int filesIndex) {
					editWithUpload(index, filesIndex);
				},
				ranges::view::all(
					_scopes[index].files
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
				[=](int filesIndex) {
					editWithUpload(index, filesIndex);
				},
				ranges::view::all(
					_scopes[index].files
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

void PanelController::editWithUpload(int index, int filesIndex) {
	Expects(_panel != nullptr);
	Expects(index >= 0 && index < _scopes.size());
	Expects(filesIndex >= 0 && filesIndex < _scopes[index].files.size());

	EditScans::ChooseScan(
		base::lambda_guarded(_panel.get(),
		[=](QByteArray &&content) {
			base::take(_scopeFilesTypeBox);
			editScope(index, filesIndex);
			uploadScan(std::move(content));
		}));
}

void PanelController::editScope(int index, int filesIndex) {
	Expects(_panel != nullptr);
	Expects(index >= 0 && index < _scopes.size());
	Expects((filesIndex < 0)
		|| (filesIndex >= 0 && filesIndex < _scopes[index].files.size()));

	_editScope = &_scopes[index];
	_editScopeFilesIndex = filesIndex;

	_form->startValueEdit(_editScope->fields);
	if (_editScopeFilesIndex >= 0) {
		_form->startValueEdit(_editScope->files[_editScopeFilesIndex]);
	}

	auto content = [&]() -> object_ptr<Ui::RpWidget> {
		switch (_editScope->type) {
		case Scope::Type::Identity:
		case Scope::Type::Address: {
			const auto &files = _editScope->files;
			auto result = (_editScopeFilesIndex >= 0)
				? object_ptr<PanelEditDocument>(
					_panel.get(),
					this,
					GetDocumentScheme(
						_editScope->type,
						files[_editScopeFilesIndex]->type),
					_editScope->fields->data.parsedInEdit,
					files[_editScopeFilesIndex]->data.parsedInEdit,
					valueFiles(*files[_editScopeFilesIndex]),
					(_editScope->selfieRequired
						? valueSelfie(*files[_editScopeFilesIndex])
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
	const auto value2 = (_editScopeFilesIndex >= 0)
		? _editScope->files[_editScopeFilesIndex].get()
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
	for (const auto &file : value.filesInEdit) {
		result.push_back(collectScanInfo(file));
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
		const auto index = std::exchange(_editScopeFilesIndex, -1);
		if (index >= 0) {
			_form->cancelValueEdit(scope->files[index]);
		}
	}
}

void PanelController::saveScope(ValueMap &&data, ValueMap &&filesData) {
	Expects(_panel != nullptr);
	Expects(_editScope != nullptr);

	_form->saveValueEdit(_editScope->fields, std::move(data));
	if (_editScopeFilesIndex >= 0) {
		_form->saveValueEdit(
			_editScope->files[_editScopeFilesIndex],
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
	} else if (_editScopeFilesIndex >= 0) {
		return _form->editValueChanged(
			_editScope->files[_editScopeFilesIndex],
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
