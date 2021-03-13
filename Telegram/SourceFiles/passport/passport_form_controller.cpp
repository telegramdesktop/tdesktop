/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "passport/passport_form_controller.h"

#include "passport/passport_encryption.h"
#include "passport/passport_panel_controller.h"
#include "passport/passport_panel_edit_document.h"
#include "boxes/confirm_box.h"
#include "boxes/passcode_box.h"
#include "lang/lang_keys.h"
#include "lang/lang_hardcoded.h"
#include "base/openssl_help.h"
#include "base/qthelp_url.h"
#include "base/unixtime.h"
#include "base/call_delayed.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "mainwindow.h"
#include "window/window_session_controller.h"
#include "core/click_handler_types.h"
#include "ui/toast/toast.h"
#include "main/main_session.h"
#include "storage/localimageloader.h"
#include "storage/localstorage.h"
#include "storage/file_upload.h"
#include "storage/file_download_mtproto.h"
#include "app.h"

#include <QtCore/QJsonDocument>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>

namespace Passport {
namespace {

constexpr auto kDocumentScansLimit = 20;
constexpr auto kTranslationScansLimit = 20;
constexpr auto kShortPollTimeout = crl::time(3000);
constexpr auto kRememberCredentialsDelay = crl::time(1800 * 1000);

Config GlobalConfig;

bool ForwardServiceErrorRequired(const QString &error) {
	return (error == qstr("BOT_INVALID"))
		|| (error == qstr("PUBLIC_KEY_REQUIRED"))
		|| (error == qstr("PUBLIC_KEY_INVALID"))
		|| (error == qstr("SCOPE_EMPTY"))
		|| (error == qstr("PAYLOAD_EMPTY"));
}

bool SaveErrorRequiresRestart(const QString &error) {
	return (error == qstr("PASSWORD_REQUIRED"))
		|| (error == qstr("SECURE_SECRET_REQUIRED"))
		|| (error == qstr("SECURE_SECRET_INVALID"));
}

bool AcceptErrorRequiresRestart(const QString &error) {
	return (error == qstr("PASSWORD_REQUIRED"))
		|| (error == qstr("SECURE_SECRET_REQUIRED"))
		|| (error == qstr("SECURE_VALUE_EMPTY"))
		|| (error == qstr("SECURE_VALUE_HASH_INVALID"));
}

std::map<QString, QString> GetTexts(const ValueMap &map) {
	auto result = std::map<QString, QString>();
	for (const auto &[key, value] : map.fields) {
		result[key] = value.text;
	}
	return result;
}

QImage ReadImage(bytes::const_span buffer) {
	return App::readImage(QByteArray::fromRawData(
		reinterpret_cast<const char*>(buffer.data()),
		buffer.size()));
}

Value::Type ConvertType(const MTPSecureValueType &type) {
	using Type = Value::Type;
	switch (type.type()) {
	case mtpc_secureValueTypePersonalDetails:
		return Type::PersonalDetails;
	case mtpc_secureValueTypePassport:
		return Type::Passport;
	case mtpc_secureValueTypeDriverLicense:
		return Type::DriverLicense;
	case mtpc_secureValueTypeIdentityCard:
		return Type::IdentityCard;
	case mtpc_secureValueTypeInternalPassport:
		return Type::InternalPassport;
	case mtpc_secureValueTypeAddress:
		return Type::Address;
	case mtpc_secureValueTypeUtilityBill:
		return Type::UtilityBill;
	case mtpc_secureValueTypeBankStatement:
		return Type::BankStatement;
	case mtpc_secureValueTypeRentalAgreement:
		return Type::RentalAgreement;
	case mtpc_secureValueTypePassportRegistration:
		return Type::PassportRegistration;
	case mtpc_secureValueTypeTemporaryRegistration:
		return Type::TemporaryRegistration;
	case mtpc_secureValueTypePhone:
		return Type::Phone;
	case mtpc_secureValueTypeEmail:
		return Type::Email;
	}
	Unexpected("Type in secureValueType type.");
};

MTPSecureValueType ConvertType(Value::Type type) {
	using Type = Value::Type;
	switch (type) {
	case Type::PersonalDetails:
		return MTP_secureValueTypePersonalDetails();
	case Type::Passport:
		return MTP_secureValueTypePassport();
	case Type::DriverLicense:
		return MTP_secureValueTypeDriverLicense();
	case Type::IdentityCard:
		return MTP_secureValueTypeIdentityCard();
	case Type::InternalPassport:
		return MTP_secureValueTypeInternalPassport();
	case Type::Address:
		return MTP_secureValueTypeAddress();
	case Type::UtilityBill:
		return MTP_secureValueTypeUtilityBill();
	case Type::BankStatement:
		return MTP_secureValueTypeBankStatement();
	case Type::RentalAgreement:
		return MTP_secureValueTypeRentalAgreement();
	case Type::PassportRegistration:
		return MTP_secureValueTypePassportRegistration();
	case Type::TemporaryRegistration:
		return MTP_secureValueTypeTemporaryRegistration();
	case Type::Phone:
		return MTP_secureValueTypePhone();
	case Type::Email:
		return MTP_secureValueTypeEmail();
	}
	Unexpected("Type in FormController::submit.");
}

void CollectToRequestedRow(
		RequestedRow &row,
		const MTPSecureRequiredType &data) {
	data.match([&](const MTPDsecureRequiredType &data) {
		row.values.emplace_back(ConvertType(data.vtype()));
		auto &value = row.values.back();
		value.selfieRequired = data.is_selfie_required();
		value.translationRequired = data.is_translation_required();
		value.nativeNames = data.is_native_names();
	}, [&](const MTPDsecureRequiredTypeOneOf &data) {
		row.values.reserve(row.values.size() + data.vtypes().v.size());
		for (const auto &one : data.vtypes().v) {
			CollectToRequestedRow(row, one);
		}
	});
}

void ApplyDataChanges(ValueData &data, ValueMap &&changes) {
	data.parsedInEdit = data.parsed;
	for (auto &[key, value] : changes.fields) {
		data.parsedInEdit.fields[key] = std::move(value);
	}
}

RequestedRow CollectRequestedRow(const MTPSecureRequiredType &data) {
	auto result = RequestedRow();
	CollectToRequestedRow(result, data);
	return result;
}

QJsonObject GetJSONFromMap(
	const std::map<QString, bytes::const_span> &map) {
	auto result = QJsonObject();
	for (const auto &[key, value] : map) {
		const auto raw = QByteArray::fromRawData(
			reinterpret_cast<const char*>(value.data()),
			value.size());
		result.insert(key, QString::fromUtf8(raw.toBase64()));
	}
	return result;
}

QJsonObject GetJSONFromFile(const File &file) {
	return GetJSONFromMap({
		{ "file_hash", file.hash },
		{ "secret", file.secret }
		});
}

FormRequest PreprocessRequest(const FormRequest &request) {
	auto result = request;
	result.publicKey.replace("\r\n", "\n");
	return result;
}

QString ValueCredentialsKey(Value::Type type) {
	using Type = Value::Type;
	switch (type) {
	case Type::PersonalDetails: return "personal_details";
	case Type::Passport: return "passport";
	case Type::DriverLicense: return "driver_license";
	case Type::IdentityCard: return "identity_card";
	case Type::InternalPassport: return "internal_passport";
	case Type::Address: return "address";
	case Type::UtilityBill: return "utility_bill";
	case Type::BankStatement: return "bank_statement";
	case Type::RentalAgreement: return "rental_agreement";
	case Type::PassportRegistration: return "passport_registration";
	case Type::TemporaryRegistration: return "temporary_registration";
	case Type::Phone:
	case Type::Email: return QString();
	}
	Unexpected("Type in ValueCredentialsKey.");
}

QString SpecialScanCredentialsKey(FileType type) {
	switch (type) {
	case FileType::FrontSide: return "front_side";
	case FileType::ReverseSide: return "reverse_side";
	case FileType::Selfie: return "selfie";
	}
	Unexpected("Type in SpecialScanCredentialsKey.");
}

QString ValidateUrl(const QString &url) {
	const auto result = qthelp::validate_url(url);
	return result.startsWith("tg://", Qt::CaseInsensitive)
		? QString()
		: result;
}

} // namespace

Config &ConfigInstance() {
	return GlobalConfig;
}

Config ParseConfig(const MTPhelp_PassportConfig &data) {
	return data.match([](const MTPDhelp_passportConfig &data) {
		auto result = Config();
		result.hash = data.vhash().v;
		auto error = QJsonParseError{ 0, QJsonParseError::NoError };
		const auto document = QJsonDocument::fromJson(
			data.vcountries_langs().c_dataJSON().vdata().v,
			&error);
		if (error.error != QJsonParseError::NoError) {
			LOG(("API Error: Failed to parse passport config, error: %1."
				).arg(error.errorString()));
			return result;
		} else if (!document.isObject()) {
			LOG(("API Error: Not an object received in passport config."));
			return result;
		}
		const auto object = document.object();
		for (auto i = object.constBegin(); i != object.constEnd(); ++i) {
			const auto countryCode = i.key();
			const auto language = i.value();
			if (!language.isString()) {
				LOG(("API Error: Not a string in passport config item."));
				continue;
			}
			result.languagesByCountryCode.emplace(
				countryCode,
				language.toString());
		}
		return result;
	}, [](const MTPDhelp_passportConfigNotModified &data) {
		return ConfigInstance();
	});
}

QString NonceNameByScope(const QString &scope) {
	if (scope.startsWith('{') && scope.endsWith('}')) {
		return qsl("nonce");
	}
	return qsl("payload");
}

bool ValueChanged(not_null<const Value*> value, const ValueMap &data) {
	const auto FileChanged = [](const EditFile &file) {
		if (file.uploadData) {
			return !file.deleted;
		}
		return file.deleted;
	};

	auto filesCount = 0;
	for (const auto &scan : value->filesInEdit(FileType::Scan)) {
		if (FileChanged(scan)) {
			return true;
		}
	}
	for (const auto &scan : value->filesInEdit(FileType::Translation)) {
		if (FileChanged(scan)) {
			return true;
		}
	}
	for (const auto &[type, scan] : value->specialScansInEdit) {
		if (FileChanged(scan)) {
			return true;
		}
	}
	const auto &existing = value->data.parsed.fields;
	for (const auto &[key, value] : data.fields) {
		const auto i = existing.find(key);
		if (i != existing.end()) {
			if (i->second.text != value.text) {
				return true;
			}
		} else if (!value.text.isEmpty()) {
			return true;
		}
	}
	return false;
}

FormRequest::FormRequest(
	UserId botId,
	const QString &scope,
	const QString &callbackUrl,
	const QString &publicKey,
	const QString &nonce,
	const QString &errors)
: botId(botId)
, scope(scope)
, callbackUrl(ValidateUrl(callbackUrl))
, publicKey(publicKey)
, nonce(nonce)
, errors(errors) {
}

EditFile::EditFile(
	not_null<Main::Session*> session,
	not_null<const Value*> value,
	FileType type,
	const File &fields,
	std::unique_ptr<UploadScanData> &&uploadData)
: value(value)
, type(type)
, fields(std::move(fields))
, uploadData(session, std::move(uploadData))
, guard(std::make_shared<bool>(true)) {
}

UploadScanDataPointer::UploadScanDataPointer(
	not_null<Main::Session*> session,
	std::unique_ptr<UploadScanData> &&value)
: _session(session)
, _value(std::move(value)) {
}

UploadScanDataPointer::UploadScanDataPointer(
	UploadScanDataPointer &&other) = default;

UploadScanDataPointer &UploadScanDataPointer::operator=(
	UploadScanDataPointer &&other) = default;

UploadScanDataPointer::~UploadScanDataPointer() {
	if (const auto value = _value.get()) {
		if (const auto fullId = value->fullId) {
			_session->uploader().cancel(fullId);
		}
	}
}

UploadScanData *UploadScanDataPointer::get() const {
	return _value.get();
}

UploadScanDataPointer::operator UploadScanData*() const {
	return _value.get();
}

UploadScanDataPointer::operator bool() const {
	return _value.get();
}

UploadScanData *UploadScanDataPointer::operator->() const {
	return _value.get();
}

RequestedValue::RequestedValue(Value::Type type) : type(type) {
}

Value::Value(Type type) : type(type) {
}

bool Value::requiresScan(FileType type) const {
	if (type == FileType::Scan) {
		return (this->type == Type::UtilityBill)
			|| (this->type == Type::BankStatement)
			|| (this->type == Type::RentalAgreement)
			|| (this->type == Type::PassportRegistration)
			|| (this->type == Type::TemporaryRegistration);
	} else if (type == FileType::Translation) {
		return translationRequired;
	} else {
		return requiresSpecialScan(type);
	}
}

bool Value::requiresSpecialScan(FileType type) const {
	switch (type) {
	case FileType::FrontSide:
		return (this->type == Type::Passport)
			|| (this->type == Type::DriverLicense)
			|| (this->type == Type::IdentityCard)
			|| (this->type == Type::InternalPassport);
	case FileType::ReverseSide:
		return (this->type == Type::DriverLicense)
			|| (this->type == Type::IdentityCard);
	case FileType::Selfie:
		return selfieRequired;
	}
	Unexpected("Special scan type in requiresSpecialScan.");
}

void Value::fillDataFrom(Value &&other) {
	const auto savedSelfieRequired = selfieRequired;
	const auto savedTranslationRequired = translationRequired;
	const auto savedNativeNames = nativeNames;
	const auto savedEditScreens = editScreens;

	*this = std::move(other);

	selfieRequired = savedSelfieRequired;
	translationRequired = savedTranslationRequired;
	nativeNames = savedNativeNames;
	editScreens = savedEditScreens;
}

bool Value::scansAreFilled() const {
	return (whatNotFilled() == 0);
}

int Value::whatNotFilled() const {
	const auto noRequiredSpecialScan = [&](FileType type) {
		return requiresSpecialScan(type)
			&& (specialScans.find(type) == end(specialScans));
	};
	if (requiresScan(FileType::Scan) && _scans.empty()) {
		return kNothingFilled;
	} else if (noRequiredSpecialScan(FileType::FrontSide)) {
		return kNothingFilled;
	}
	auto result = 0;
	if (requiresScan(FileType::Translation) && _translations.empty()) {
		result |= kNoTranslationFilled;
	}
	if (noRequiredSpecialScan(FileType::ReverseSide)
		|| noRequiredSpecialScan(FileType::Selfie)) {
		result |= kNoSelfieFilled;
	}
	return result;
}

void Value::saveInEdit(not_null<Main::Session*> session) {
	const auto saveList = [&](FileType type) {
		filesInEdit(type) = ranges::views::all(
			files(type)
		) | ranges::views::transform([=](const File &file) {
			return EditFile(session, this, type, file, nullptr);
		}) | ranges::to_vector;
	};
	saveList(FileType::Scan);
	saveList(FileType::Translation);

	specialScansInEdit.clear();
	for (const auto &[type, scan] : specialScans) {
		specialScansInEdit.emplace(type, EditFile(
			session,
			this,
			type,
			scan,
			nullptr));
	}
	data.parsedInEdit = data.parsed;
}

void Value::clearEditData() {
	filesInEdit(FileType::Scan).clear();
	filesInEdit(FileType::Translation).clear();
	specialScansInEdit.clear();
	data.encryptedSecretInEdit.clear();
	data.hashInEdit.clear();
	data.parsedInEdit = ValueMap();
}

bool Value::uploadingScan() const {
	const auto uploading = [](const EditFile &file) {
		return file.uploadData
			&& file.uploadData->fullId
			&& !file.deleted;
	};
	const auto uploadingInList = [&](FileType type) {
		const auto &list = filesInEdit(type);
		return ranges::any_of(list, uploading);
	};
	if (uploadingInList(FileType::Scan)
		|| uploadingInList(FileType::Translation)) {
		return true;
	}
	if (ranges::any_of(specialScansInEdit, [&](const auto &pair) {
		return uploading(pair.second);
	})) {
		return true;
	}
	return false;
}

bool Value::saving() const {
	return (saveRequestId != 0)
		|| (verification.requestId != 0)
		|| (verification.codeLength != 0)
		|| uploadingScan();
}

std::vector<File> &Value::files(FileType type) {
	switch (type) {
	case FileType::Scan: return _scans;
	case FileType::Translation: return _translations;
	}
	Unexpected("Type in Value::files().");
}

const std::vector<File> &Value::files(FileType type) const {
	switch (type) {
	case FileType::Scan: return _scans;
	case FileType::Translation: return _translations;
	}
	Unexpected("Type in Value::files() const.");
}

QString &Value::fileMissingError(FileType type) {
	switch (type) {
	case FileType::Scan: return _scanMissingError;
	case FileType::Translation: return _translationMissingError;
	}
	Unexpected("Type in Value::fileMissingError().");
}

const QString &Value::fileMissingError(FileType type) const {
	switch (type) {
	case FileType::Scan: return _scanMissingError;
	case FileType::Translation: return _translationMissingError;
	}
	Unexpected("Type in Value::fileMissingError() const.");
}

std::vector<EditFile> &Value::filesInEdit(FileType type) {
	switch (type) {
	case FileType::Scan: return _scansInEdit;
	case FileType::Translation: return _translationsInEdit;
	}
	Unexpected("Type in Value::filesInEdit().");
}

const std::vector<EditFile> &Value::filesInEdit(FileType type) const {
	switch (type) {
	case FileType::Scan: return _scansInEdit;
	case FileType::Translation: return _translationsInEdit;
	}
	Unexpected("Type in Value::filesInEdit() const.");
}

EditFile &Value::fileInEdit(FileType type, std::optional<int> fileIndex) {
	switch (type) {
	case FileType::Scan:
	case FileType::Translation: {
		auto &list = filesInEdit(type);
		Assert(fileIndex.has_value());
		Assert(*fileIndex >= 0 && *fileIndex < list.size());
		return list[*fileIndex];
	} break;
	}
	const auto i = specialScansInEdit.find(type);
	Assert(!fileIndex.has_value());
	Assert(i != end(specialScansInEdit));
	return i->second;
}

const EditFile &Value::fileInEdit(
		FileType type,
		std::optional<int> fileIndex) const {
	switch (type) {
	case FileType::Scan:
	case FileType::Translation: {
		auto &list = filesInEdit(type);
		Assert(fileIndex.has_value());
		Assert(*fileIndex >= 0 && *fileIndex < list.size());
		return list[*fileIndex];
	} break;
	}
	const auto i = specialScansInEdit.find(type);
	Assert(!fileIndex.has_value());
	Assert(i != end(specialScansInEdit));
	return i->second;
}

std::vector<EditFile> Value::takeAllFilesInEdit() {
	auto result = base::take(filesInEdit(FileType::Scan));
	auto &translation = filesInEdit(FileType::Translation);
	auto &special = specialScansInEdit;
	result.reserve(result.size() + translation.size() + special.size());

	for (auto &scan : base::take(translation)) {
		result.push_back(std::move(scan));
	}
	for (auto &[type, scan] : base::take(special)) {
		result.push_back(std::move(scan));
	}
	return result;
}

FormController::FormController(
	not_null<Window::SessionController*> controller,
	const FormRequest &request)
: _controller(controller)
, _api(&_controller->session().mtp())
, _request(PreprocessRequest(request))
, _shortPollTimer([=] { reloadPassword(); })
, _view(std::make_unique<PanelController>(this)) {
}

Main::Session &FormController::session() const {
	return _controller->session();
}

void FormController::show() {
	requestForm();
	requestPassword();
	requestConfig();
}

UserData *FormController::bot() const {
	return _bot;
}

QString FormController::privacyPolicyUrl() const {
	return _form.privacyPolicyUrl;
}

bytes::vector FormController::passwordHashForAuth(
		bytes::const_span password) const {
	return Core::ComputeCloudPasswordHash(_password.request.algo, password);
}

auto FormController::prepareFinalData() -> FinalData {
	auto errors = std::vector<not_null<const Value*>>();
	auto hashes = QVector<MTPSecureValueHash>();
	auto secureData = QJsonObject();
	const auto addValueToJSON = [&](
			const QString &key,
			not_null<const Value*> value) {
		auto object = QJsonObject();
		if (!value->data.parsed.fields.empty()) {
			object.insert("data", GetJSONFromMap({
				{ "data_hash", value->data.hash },
				{ "secret", value->data.secret }
			}));
		}
		const auto addList = [&](
				const QString &key,
				const std::vector<File> &list) {
			if (!list.empty()) {
				auto files = QJsonArray();
				for (const auto &scan : list) {
					files.append(GetJSONFromFile(scan));
				}
				object.insert(key, files);
			}
		};
		addList("files", value->files(FileType::Scan));
		if (value->translationRequired) {
			addList("translation", value->files(FileType::Translation));
		}
		for (const auto &[type, scan] : value->specialScans) {
			if (value->requiresSpecialScan(type)) {
				object.insert(
					SpecialScanCredentialsKey(type),
					GetJSONFromFile(scan));
			}
		}
		secureData.insert(key, object);
	};
	const auto addValue = [&](not_null<const Value*> value) {
		hashes.push_back(MTP_secureValueHash(
			ConvertType(value->type),
			MTP_bytes(value->submitHash)));
		const auto key = ValueCredentialsKey(value->type);
		if (!key.isEmpty()) {
			addValueToJSON(key, value);
		}
	};
	const auto scopes = ComputeScopes(_form);
	for (const auto &scope : scopes) {
		const auto row = ComputeScopeRow(scope);
		if (row.ready.isEmpty() || !row.error.isEmpty()) {
			errors.push_back(scope.details
				? scope.details
				: scope.documents[0].get());
			continue;
		}
		if (scope.details) {
			addValue(scope.details);
		}
		if (!scope.documents.empty()) {
			for (const auto &document : scope.documents) {
				if (document->scansAreFilled()) {
					addValue(document);
					break;
				}
			}
		}
	}

	auto json = QJsonObject();
	if (errors.empty()) {
		json.insert("secure_data", secureData);
		json.insert(NonceNameByScope(_request.scope), _request.nonce);
	}

	return {
		hashes,
		QJsonDocument(json).toJson(QJsonDocument::Compact),
		errors
	};
}

std::vector<not_null<const Value*>> FormController::submitGetErrors() {
	if (_submitRequestId || _submitSuccess|| _cancelled) {
		return {};
	}

	const auto prepared = prepareFinalData();
	if (!prepared.errors.empty()) {
		return prepared.errors;
	}
	const auto credentialsEncryptedData = EncryptData(
		bytes::make_span(prepared.credentials));
	const auto credentialsEncryptedSecret = EncryptCredentialsSecret(
		credentialsEncryptedData.secret,
		bytes::make_span(_request.publicKey.toUtf8()));

	_submitRequestId = _api.request(MTPaccount_AcceptAuthorization(
		MTP_int(_request.botId),
		MTP_string(_request.scope),
		MTP_string(_request.publicKey),
		MTP_vector<MTPSecureValueHash>(prepared.hashes),
		MTP_secureCredentialsEncrypted(
			MTP_bytes(credentialsEncryptedData.bytes),
			MTP_bytes(credentialsEncryptedData.hash),
			MTP_bytes(credentialsEncryptedSecret))
	)).done([=](const MTPBool &result) {
		_submitRequestId = 0;
		_submitSuccess = true;

		_view->showToast(tr::lng_passport_success(tr::now));

		base::call_delayed(
			(st::defaultToast.durationFadeIn
				+ Ui::Toast::kDefaultDuration
				+ st::defaultToast.durationFadeOut),
			this,
			[=] { cancel(); });
	}).fail([=](const MTP::Error &error) {
		_submitRequestId = 0;
		if (handleAppUpdateError(error.type())) {
		} else if (AcceptErrorRequiresRestart(error.type())) {
			suggestRestart();
		} else {
			_view->show(Box<InformBox>(
				Lang::Hard::SecureAcceptError() + "\n" + error.type()));
		}
	}).send();

	return {};
}

void FormController::checkPasswordHash(
		mtpRequestId &guard,
		bytes::vector hash,
		PasswordCheckCallback callback) {
	_passwordCheckHash = std::move(hash);
	_passwordCheckCallback = std::move(callback);
	if (_password.request.id) {
		passwordChecked();
	} else {
		requestPasswordData(guard);
	}
}

void FormController::passwordChecked() {
	if (!_password.request || !_password.request.id) {
		return passwordServerError();
	}
	const auto check = Core::ComputeCloudPasswordCheck(
		_password.request,
		_passwordCheckHash);
	if (!check) {
		return passwordServerError();
	}
	_password.request.id = 0;
	_passwordCheckCallback(check);
}

void FormController::requestPasswordData(mtpRequestId &guard) {
	if (!_passwordCheckCallback) {
		return passwordServerError();
	}

	_api.request(base::take(guard)).cancel();
	guard = _api.request(
		MTPaccount_GetPassword()
	).done([=, &guard](const MTPaccount_Password &result) {
		guard = 0;
		result.match([&](const MTPDaccount_password &data) {
			_password.request = Core::ParseCloudPasswordCheckRequest(data);
			passwordChecked();
		});
	}).send();
}

void FormController::submitPassword(const QByteArray &password) {
	Expects(!!_password.request);

	const auto submitSaved = !base::take(_savedPasswordValue).isEmpty();
	if (_passwordCheckRequestId) {
		return;
	} else if (password.isEmpty()) {
		_passwordError.fire(QString());
		return;
	}
	const auto callback = [=](const Core::CloudPasswordResult &check) {
		submitPassword(check, password, submitSaved);
	};
	checkPasswordHash(
		_passwordCheckRequestId,
		passwordHashForAuth(bytes::make_span(password)),
		callback);
}

void FormController::submitPassword(
		const Core::CloudPasswordResult &check,
		const QByteArray &password,
		bool submitSaved) {
	_passwordCheckRequestId = _api.request(MTPaccount_GetPasswordSettings(
		check.result
	)).handleFloodErrors(
	).done([=](const MTPaccount_PasswordSettings &result) {
		Expects(result.type() == mtpc_account_passwordSettings);

		_passwordCheckRequestId = 0;
		_savedPasswordValue = QByteArray();
		const auto &data = result.c_account_passwordSettings();
		_password.confirmedEmail = qs(data.vemail().value_or_empty());
		if (const auto wrapped = data.vsecure_settings()) {
			const auto &settings = wrapped->c_secureSecretSettings();
			const auto algo = Core::ParseSecureSecretAlgo(
				settings.vsecure_algo());
			if (v::is_null(algo)) {
				_view->showUpdateAppBox();
				return;
			}
			const auto hashForSecret = Core::ComputeSecureSecretHash(
				algo,
				bytes::make_span(password));
			validateSecureSecret(
				bytes::make_span(settings.vsecure_secret().v),
				hashForSecret,
				bytes::make_span(password),
				settings.vsecure_secret_id().v);
			if (!_secret.empty()) {
				auto saved = SavedCredentials();
				saved.hashForAuth = base::take(_passwordCheckHash);
				saved.hashForSecret = hashForSecret;
				saved.secretId = _secretId;
				session().data().rememberPassportCredentials(
					std::move(saved),
					kRememberCredentialsDelay);
			}
		} else {
			validateSecureSecret(
				bytes::const_span(), // secure_secret
				bytes::const_span(), // hash for secret
				bytes::make_span(password),
				0); // secure_secret_id
		}
	}).fail([=](const MTP::Error &error) {
		_passwordCheckRequestId = 0;
		if (error.type() == qstr("SRP_ID_INVALID")) {
			handleSrpIdInvalid(_passwordCheckRequestId);
		} else if (submitSaved) {
			// Force reload and show form.
			_password = PasswordSettings();
			reloadPassword();
		} else if (MTP::IsFloodError(error)) {
			_passwordError.fire(tr::lng_flood_error(tr::now));
		} else if (error.type() == qstr("PASSWORD_HASH_INVALID")
			|| error.type() == qstr("SRP_PASSWORD_CHANGED")) {
			_passwordError.fire(tr::lng_passport_password_wrong(tr::now));
		} else {
			_passwordError.fire_copy(error.type());
		}
	}).send();
}

bool FormController::handleSrpIdInvalid(mtpRequestId &guard) {
	const auto now = crl::now();
	if (_lastSrpIdInvalidTime > 0
		&& now - _lastSrpIdInvalidTime < Core::kHandleSrpIdInvalidTimeout) {
		_password.request.id = 0;
		_passwordError.fire(Lang::Hard::ServerError());
		return false;
	} else {
		_lastSrpIdInvalidTime = now;
		requestPasswordData(guard);
		return true;
	}
}

void FormController::passwordServerError() {
	_view->showCriticalError(Lang::Hard::ServerError());
}

void FormController::checkSavedPasswordSettings(
		const SavedCredentials &credentials) {
	const auto callback = [=](const Core::CloudPasswordResult &check) {
		checkSavedPasswordSettings(check, credentials);
	};
	checkPasswordHash(
		_passwordCheckRequestId,
		credentials.hashForAuth,
		callback);
}

void FormController::checkSavedPasswordSettings(
		const Core::CloudPasswordResult &check,
		const SavedCredentials &credentials) {
	_passwordCheckRequestId = _api.request(MTPaccount_GetPasswordSettings(
		check.result
	)).done([=](const MTPaccount_PasswordSettings &result) {
		Expects(result.type() == mtpc_account_passwordSettings);

		_passwordCheckRequestId = 0;
		const auto &data = result.c_account_passwordSettings();
		if (const auto wrapped = data.vsecure_settings()) {
			const auto &settings = wrapped->c_secureSecretSettings();
			const auto algo = Core::ParseSecureSecretAlgo(
				settings.vsecure_algo());
			if (v::is_null(algo)) {
				_view->showUpdateAppBox();
				return;
			} else if (!settings.vsecure_secret().v.isEmpty()
				&& settings.vsecure_secret_id().v == credentials.secretId) {
				_password.confirmedEmail = qs(data.vemail().value_or_empty());
				validateSecureSecret(
					bytes::make_span(settings.vsecure_secret().v),
					credentials.hashForSecret,
					{},
					settings.vsecure_secret_id().v);
			}
		}
		if (_secret.empty()) {
			session().data().forgetPassportCredentials();
			showForm();
		}
	}).fail([=](const MTP::Error &error) {
		_passwordCheckRequestId = 0;
		if (error.type() != qstr("SRP_ID_INVALID")
			|| !handleSrpIdInvalid(_passwordCheckRequestId)) {
		} else {
			session().data().forgetPassportCredentials();
			showForm();
		}
	}).send();
}

void FormController::recoverPassword() {
	if (!_password.hasRecovery) {
		_view->show(Box<InformBox>(tr::lng_signin_no_email_forgot(tr::now)));
		return;
	} else if (_recoverRequestId) {
		return;
	}
	_recoverRequestId = _api.request(MTPauth_RequestPasswordRecovery(
	)).done([=](const MTPauth_PasswordRecovery &result) {
		Expects(result.type() == mtpc_auth_passwordRecovery);

		_recoverRequestId = 0;

		const auto &data = result.c_auth_passwordRecovery();
		const auto pattern = qs(data.vemail_pattern());
		const auto box = _view->show(Box<RecoverBox>(
			&_controller->session(),
			pattern,
			_password.notEmptyPassport));

		box->passwordCleared(
		) | rpl::start_with_next([=] {
			reloadPassword();
		}, box->lifetime());

		box->recoveryExpired(
		) | rpl::start_with_next([=] {
			box->closeBox();
		}, box->lifetime());
	}).fail([=](const MTP::Error &error) {
		_recoverRequestId = 0;
		_view->show(Box<InformBox>(Lang::Hard::ServerError()
			+ '\n'
			+ error.type()));
	}).send();
}

void FormController::reloadPassword() {
	requestPassword();
}

void FormController::reloadAndSubmitPassword(const QByteArray &password) {
	_savedPasswordValue = password;
	requestPassword();
}

void FormController::cancelPassword() {
	if (_passwordRequestId) {
		return;
	}
	_passwordRequestId = _api.request(MTPaccount_CancelPasswordEmail(
	)).done([=](const MTPBool &result) {
		_passwordRequestId = 0;
		reloadPassword();
	}).fail([=](const MTP::Error &error) {
		_passwordRequestId = 0;
		reloadPassword();
	}).send();
}

void FormController::validateSecureSecret(
		bytes::const_span encryptedSecret,
		bytes::const_span passwordHashForSecret,
		bytes::const_span passwordBytes,
		uint64 serverSecretId) {
	Expects(!passwordBytes.empty() || !passwordHashForSecret.empty());

	if (!passwordHashForSecret.empty() && !encryptedSecret.empty()) {
		_secret = DecryptSecureSecret(
			encryptedSecret,
			passwordHashForSecret);
		if (_secret.empty()) {
			_secretId = 0;
			LOG(("API Error: Failed to decrypt secure secret."));
			if (!passwordBytes.empty()) {
				suggestReset(bytes::make_vector(passwordBytes));
			}
			return;
		} else if (CountSecureSecretId(_secret) != serverSecretId) {
			_secret.clear();
			_secretId = 0;
			LOG(("API Error: Wrong secure secret id."));
			if (!passwordBytes.empty()) {
				suggestReset(bytes::make_vector(passwordBytes));
			}
			return;
		} else {
			_secretId = serverSecretId;
			decryptValues();
		}
	}
	if (_secret.empty()) {
		generateSecret(passwordBytes);
	}
	_secretReady.fire({});
}

void FormController::suggestReset(bytes::vector password) {
	for (auto &[type, value] : _form.values) {
//		if (!value.data.original.isEmpty()) {
		resetValue(value);
//		}
	}
	_view->suggestReset([=] {
		const auto callback = [=](const Core::CloudPasswordResult &check) {
			resetSecret(check, password);
		};
		checkPasswordHash(
			_saveSecretRequestId,
			passwordHashForAuth(bytes::make_span(password)),
			callback);
		_secretReady.fire({});
	});
}

void FormController::resetSecret(
		const Core::CloudPasswordResult &check,
		const bytes::vector &password) {
	using Flag = MTPDaccount_passwordInputSettings::Flag;
	_saveSecretRequestId = _api.request(MTPaccount_UpdatePasswordSettings(
		check.result,
		MTP_account_passwordInputSettings(
			MTP_flags(Flag::f_new_secure_settings),
			MTPPasswordKdfAlgo(), // new_algo
			MTPbytes(), // new_password_hash
			MTPstring(), // hint
			MTPstring(), // email
			MTP_secureSecretSettings(
				MTP_securePasswordKdfAlgoUnknown(), // secure_algo
				MTP_bytes(), // secure_secret
				MTP_long(0))) // secure_secret_id
	)).done([=](const MTPBool &result) {
		_saveSecretRequestId = 0;
		generateSecret(password);
	}).fail([=](const MTP::Error &error) {
		_saveSecretRequestId = 0;
		if (error.type() != qstr("SRP_ID_INVALID")
			|| !handleSrpIdInvalid(_saveSecretRequestId)) {
			formFail(error.type());
		}
	}).send();
}

void FormController::decryptValues() {
	Expects(!_secret.empty());

	for (auto &[type, value] : _form.values) {
		decryptValue(value);
	}
	fillErrors();
	fillNativeFromFallback();
}

void FormController::fillErrors() {
	const auto find = [&](const MTPSecureValueType &type) -> Value* {
		const auto converted = ConvertType(type);
		const auto i = _form.values.find(ConvertType(type));
		if (i != end(_form.values)) {
			return &i->second;
		}
		LOG(("API Error: Value not found for error type."));
		return nullptr;
	};
	const auto scan = [&](
			Value &value,
			FileType type,
			bytes::const_span hash) -> File* {
		auto &list = value.files(type);
		const auto i = ranges::find_if(list, [&](const File &scan) {
			return !bytes::compare(hash, scan.hash);
		});
		if (i != end(list)) {
			return &*i;
		}
		LOG(("API Error: File not found for error value."));
		return nullptr;
	};
	const auto setSpecialScanError = [&](FileType type, auto &&data) {
		if (const auto value = find(data.vtype())) {
			if (value->requiresSpecialScan(type)) {
				const auto i = value->specialScans.find(type);
				if (i != value->specialScans.end()) {
					i->second.error = qs(data.vtext());
				} else {
					LOG(("API Error: "
						"Special scan %1 not found for error value."
						).arg(int(type)));
				}
			}
		}
	};
	for (const auto &error : std::as_const(_form.pendingErrors)) {
		error.match([&](const MTPDsecureValueError &data) {
			if (const auto value = find(data.vtype())) {
				if (CanHaveErrors(value->type)) {
					value->error = qs(data.vtext());
				}
			}
		}, [&](const MTPDsecureValueErrorData &data) {
			if (const auto value = find(data.vtype())) {
				const auto key = qs(data.vfield());
				if (CanHaveErrors(value->type)
					&& !SkipFieldCheck(value, key)) {
					value->data.parsed.fields[key].error = qs(data.vtext());
				}
			}
		}, [&](const MTPDsecureValueErrorFile &data) {
			const auto hash = bytes::make_span(data.vfile_hash().v);
			if (const auto value = find(data.vtype())) {
				if (const auto file = scan(*value, FileType::Scan, hash)) {
					if (value->requiresScan(FileType::Scan)) {
						file->error = qs(data.vtext());
					}
				}
			}
		}, [&](const MTPDsecureValueErrorFiles &data) {
			if (const auto value = find(data.vtype())) {
				if (value->requiresScan(FileType::Scan)) {
					value->fileMissingError(FileType::Scan)
						= qs(data.vtext());
				}
			}
		}, [&](const MTPDsecureValueErrorTranslationFile &data) {
			const auto hash = bytes::make_span(data.vfile_hash().v);
			if (const auto value = find(data.vtype())) {
				const auto file = scan(*value, FileType::Translation, hash);
				if (file && value->requiresScan(FileType::Translation)) {
					file->error = qs(data.vtext());
				}
			}
		}, [&](const MTPDsecureValueErrorTranslationFiles &data) {
			if (const auto value = find(data.vtype())) {
				if (value->requiresScan(FileType::Translation)) {
					value->fileMissingError(FileType::Translation)
						= qs(data.vtext());
				}
			}
		}, [&](const MTPDsecureValueErrorFrontSide &data) {
			setSpecialScanError(FileType::FrontSide, data);
		}, [&](const MTPDsecureValueErrorReverseSide &data) {
			setSpecialScanError(FileType::ReverseSide, data);
		}, [&](const MTPDsecureValueErrorSelfie &data) {
			setSpecialScanError(FileType::Selfie, data);
		});
	}
}

void FormController::fillNativeFromFallback() {
	// Check if additional values (*_name_native) were requested.
	const auto i = _form.values.find(Value::Type::PersonalDetails);
	if (i == end(_form.values) || !i->second.nativeNames) {
		return;
	}
	auto values = i->second.data.parsed;

	// Check if additional values should be copied from fallback values.
	const auto scheme = GetDocumentScheme(
		Scope::Type::PersonalDetails,
		std::nullopt,
		true);
	const auto dependencyIt = values.fields.find(
		scheme.additionalDependencyKey);
	const auto dependency = (dependencyIt == end(values.fields))
		? QString()
		: dependencyIt->second.text;
	if (scheme.additionalShown(dependency)
		!= EditDocumentScheme::AdditionalVisibility::OnlyIfError) {
		return;
	}

	// Copy additional values from fallback if they're not filled yet.
	auto changed = false;
	using Scheme = EditDocumentScheme;
	for (const auto &row : scheme.rows) {
		if (row.valueClass == Scheme::ValueClass::Additional) {
			const auto nativeIt = values.fields.find(row.key);
			const auto native = (nativeIt == end(values.fields))
				? QString()
				: nativeIt->second.text;
			if (!native.isEmpty()
				|| (nativeIt != end(values.fields)
					&& !nativeIt->second.error.isEmpty())) {
				return;
			}
			const auto latinIt = values.fields.find(
				row.additionalFallbackKey);
			const auto latin = (latinIt == end(values.fields))
				? QString()
				: latinIt->second.text;
			if (row.error(latin).has_value()) {
				return;
			} else if (native != latin) {
				values.fields[row.key].text = latin;
				changed = true;
			}
		}
	}
	if (changed) {
		startValueEdit(&i->second);
		saveValueEdit(&i->second, std::move(values));
	}
}

void FormController::decryptValue(Value &value) const {
	Expects(!_secret.empty());

	if (!validateValueSecrets(value)) {
		resetValue(value);
		return;
	}
	if (!value.data.original.isEmpty()) {
		const auto decrypted = DecryptData(
			bytes::make_span(value.data.original),
			value.data.hash,
			value.data.secret);
		if (decrypted.empty()) {
			LOG(("API Error: Could not decrypt value fields."));
			resetValue(value);
			return;
		}
		const auto fields = DeserializeData(decrypted);
		value.data.parsed.fields.clear();
		for (const auto &[key, text] : fields) {
			value.data.parsed.fields[key] = { text };
		}
	}
}

bool FormController::validateValueSecrets(Value &value) const {
	if (!value.data.original.isEmpty()) {
		value.data.secret = DecryptValueSecret(
			value.data.encryptedSecret,
			_secret,
			value.data.hash);
		if (value.data.secret.empty()) {
			LOG(("API Error: Could not decrypt data secret."));
			return false;
		}
	}
	const auto validateFileSecret = [&](File &file) {
		file.secret = DecryptValueSecret(
			file.encryptedSecret,
			_secret,
			file.hash);
		if (file.secret.empty()) {
			LOG(("API Error: Could not decrypt file secret."));
			return false;
		}
		return true;
	};
	for (auto &scan : value.files(FileType::Scan)) {
		if (!validateFileSecret(scan)) {
			return false;
		}
	}
	for (auto &scan : value.files(FileType::Translation)) {
		if (!validateFileSecret(scan)) {
			return false;
		}
	}
	for (auto &[type, scan] : value.specialScans) {
		if (!validateFileSecret(scan)) {
			return false;
		}
	}
	return true;
}

void FormController::resetValue(Value &value) const {
	value.fillDataFrom(Value(value.type));
}

rpl::producer<QString> FormController::passwordError() const {
	return _passwordError.events();
}

const PasswordSettings &FormController::passwordSettings() const {
	return _password;
}

void FormController::uploadScan(
		not_null<const Value*> value,
		FileType type,
		QByteArray &&content) {
	if (!canAddScan(value, type)) {
		_view->showToast(tr::lng_passport_scans_limit_reached(tr::now));
		return;
	}
	const auto nonconst = findValue(value);
	const auto fileIndex = [&]() -> std::optional<int> {
		auto scanInEdit = EditFile(
			&session(),
			nonconst,
			type,
			File(),
			nullptr);
		if (type == FileType::Scan || type == FileType::Translation) {
			auto &list = nonconst->filesInEdit(type);
			auto scanIndex = int(list.size());
			list.push_back(std::move(scanInEdit));
			return list.size() - 1;
		}
		auto i = nonconst->specialScansInEdit.find(type);
		if (i != nonconst->specialScansInEdit.end()) {
			i->second = std::move(scanInEdit);
		} else {
			i = nonconst->specialScansInEdit.emplace(
				type,
				std::move(scanInEdit)).first;
		}
		return std::nullopt;
	}();
	auto &scan = nonconst->fileInEdit(type, fileIndex);
	encryptFile(scan, std::move(content), [=](UploadScanData &&result) {
		uploadEncryptedFile(
			nonconst->fileInEdit(type, fileIndex),
			std::move(result));
	});
}

void FormController::deleteScan(
		not_null<const Value*> value,
		FileType type,
		std::optional<int> fileIndex) {
	scanDeleteRestore(value, type, fileIndex, true);
}

void FormController::restoreScan(
		not_null<const Value*> value,
		FileType type,
		std::optional<int> fileIndex) {
	scanDeleteRestore(value, type, fileIndex, false);
}

void FormController::prepareFile(
		EditFile &file,
		const QByteArray &content) {
	const auto fileId = openssl::RandomValue<uint64>();
	file.fields.size = content.size();
	file.fields.id = fileId;
	file.fields.dcId = _controller->session().mainDcId();
	file.fields.secret = GenerateSecretBytes();
	file.fields.date = base::unixtime::now();
	file.fields.image = ReadImage(bytes::make_span(content));
	file.fields.downloadOffset = file.fields.size;

	_scanUpdated.fire(&file);
}

void FormController::encryptFile(
		EditFile &file,
		QByteArray &&content,
		Fn<void(UploadScanData &&result)> callback) {
	prepareFile(file, content);

	const auto weak = std::weak_ptr<bool>(file.guard);
	crl::async([
		=,
		fileId = file.fields.id,
		bytes = std::move(content),
		fileSecret = file.fields.secret
	] {
		auto data = EncryptData(
			bytes::make_span(bytes),
			fileSecret);
		auto result = UploadScanData();
		result.fileId = fileId;
		result.hash = std::move(data.hash);
		result.bytes = std::move(data.bytes);
		result.md5checksum.resize(32);
		hashMd5Hex(
			result.bytes.data(),
			result.bytes.size(),
			result.md5checksum.data());
		crl::on_main([=, encrypted = std::move(result)]() mutable {
			if (weak.lock()) {
				callback(std::move(encrypted));
			}
		});
	});
}

void FormController::scanDeleteRestore(
		not_null<const Value*> value,
		FileType type,
		std::optional<int> fileIndex,
		bool deleted) {
	const auto nonconst = findValue(value);
	auto &scan = nonconst->fileInEdit(type, fileIndex);
	if (scan.deleted && !deleted) {
		if (!canAddScan(value, type)) {
			_view->showToast(tr::lng_passport_scans_limit_reached(tr::now));
			return;
		}
	}
	scan.deleted = deleted;
	_scanUpdated.fire(&scan);
}

bool FormController::canAddScan(
		not_null<const Value*> value,
		FileType type) const {
	const auto limit = (type == FileType::Scan)
		? kDocumentScansLimit
		: (type == FileType::Translation)
		? kTranslationScansLimit
		: -1;
	if (limit < 0) {
		return true;
	}
	const auto scansCount = ranges::count_if(
		value->filesInEdit(type),
		[](const EditFile &scan) { return !scan.deleted; });
	return (scansCount < limit);
}

void FormController::subscribeToUploader() {
	if (_uploaderSubscriptions) {
		return;
	}

	using namespace Storage;

	session().uploader().secureReady(
	) | rpl::start_with_next([=](const UploadSecureDone &data) {
		scanUploadDone(data);
	}, _uploaderSubscriptions);

	session().uploader().secureProgress(
	) | rpl::start_with_next([=](const UploadSecureProgress &data) {
		scanUploadProgress(data);
	}, _uploaderSubscriptions);

	session().uploader().secureFailed(
	) | rpl::start_with_next([=](const FullMsgId &fullId) {
		scanUploadFail(fullId);
	}, _uploaderSubscriptions);
}

void FormController::uploadEncryptedFile(
		EditFile &file,
		UploadScanData &&data) {
	subscribeToUploader();

	file.uploadData = UploadScanDataPointer(
		&session(),
		std::make_unique<UploadScanData>(std::move(data)));

	auto prepared = std::make_shared<FileLoadResult>(
		TaskId(),
		file.uploadData->fileId,
		FileLoadTo(PeerId(0), Api::SendOptions(), MsgId(0), MsgId(0)),
		TextWithTags(),
		std::shared_ptr<SendingAlbum>(nullptr));
	prepared->type = SendMediaType::Secure;
	prepared->content = QByteArray::fromRawData(
		reinterpret_cast<char*>(file.uploadData->bytes.data()),
		file.uploadData->bytes.size());
	prepared->setFileData(prepared->content);
	prepared->filemd5 = file.uploadData->md5checksum;

	file.uploadData->fullId = FullMsgId(
		0,
		session().data().nextLocalMessageId());
	session().uploader().upload(
		file.uploadData->fullId,
		std::move(prepared));
}

void FormController::scanUploadDone(const Storage::UploadSecureDone &data) {
	if (const auto file = findEditFile(data.fullId)) {
		Assert(file->uploadData != nullptr);
		Assert(file->uploadData->fileId == data.fileId);

		file->uploadData->partsCount = data.partsCount;
		file->fields.hash = std::move(file->uploadData->hash);
		file->fields.encryptedSecret = EncryptValueSecret(
			file->fields.secret,
			_secret,
			file->fields.hash);
		file->uploadData->fullId = FullMsgId();

		_scanUpdated.fire(file);
	}
}

void FormController::scanUploadProgress(
		const Storage::UploadSecureProgress &data) {
	if (const auto file = findEditFile(data.fullId)) {
		Assert(file->uploadData != nullptr);

		file->uploadData->offset = data.offset;

		_scanUpdated.fire(file);
	}
}

void FormController::scanUploadFail(const FullMsgId &fullId) {
	if (const auto file = findEditFile(fullId)) {
		Assert(file->uploadData != nullptr);

		file->uploadData->offset = -1;

		_scanUpdated.fire(file);
	}
}

rpl::producer<> FormController::secretReadyEvents() const {
	return _secretReady.events();
}

QString FormController::defaultEmail() const {
	return _password.confirmedEmail;
}

QString FormController::defaultPhoneNumber() const {
	return session().user()->phone();
}

auto FormController::scanUpdated() const
-> rpl::producer<not_null<const EditFile*>> {
	return _scanUpdated.events();
}

auto FormController::valueSaveFinished() const
-> rpl::producer<not_null<const Value*>> {
	return _valueSaveFinished.events();
}

auto FormController::verificationNeeded() const
-> rpl::producer<not_null<const Value*>> {
	return _verificationNeeded.events();
}

auto FormController::verificationUpdate() const
-> rpl::producer<not_null<const Value*>> {
	return _verificationUpdate.events();
}

void FormController::verify(
		not_null<const Value*> value,
		const QString &code) {
	if (value->verification.requestId) {
		return;
	}
	const auto nonconst = findValue(value);
	const auto prepared = code.trimmed();
	Assert(nonconst->verification.codeLength != 0);
	verificationError(nonconst, QString());
	if (nonconst->verification.codeLength > 0
		&& nonconst->verification.codeLength != prepared.size()) {
		verificationError(nonconst, tr::lng_signin_wrong_code(tr::now));
		return;
	} else if (prepared.isEmpty()) {
		verificationError(nonconst, tr::lng_signin_wrong_code(tr::now));
		return;
	}
	nonconst->verification.requestId = [&] {
		switch (nonconst->type) {
		case Value::Type::Phone:
			return _api.request(MTPaccount_VerifyPhone(
				MTP_string(getPhoneFromValue(nonconst)),
				MTP_string(nonconst->verification.phoneCodeHash),
				MTP_string(prepared)
			)).done([=](const MTPBool &result) {
				savePlainTextValue(nonconst);
				clearValueVerification(nonconst);
			}).fail([=](const MTP::Error &error) {
				nonconst->verification.requestId = 0;
				if (error.type() == qstr("PHONE_CODE_INVALID")) {
					verificationError(
						nonconst,
						tr::lng_signin_wrong_code(tr::now));
				} else {
					verificationError(nonconst, error.type());
				}
			}).send();
		case Value::Type::Email:
			return _api.request(MTPaccount_VerifyEmail(
				MTP_string(getEmailFromValue(nonconst)),
				MTP_string(prepared)
			)).done([=](const MTPBool &result) {
				savePlainTextValue(nonconst);
				clearValueVerification(nonconst);
			}).fail([=](const MTP::Error &error) {
				nonconst->verification.requestId = 0;
				if (error.type() == qstr("CODE_INVALID")) {
					verificationError(
						nonconst,
						tr::lng_signin_wrong_code(tr::now));
				} else {
					verificationError(nonconst, error.type());
				}
			}).send();
		}
		Unexpected("Type in FormController::verify().");
	}();
}

void FormController::verificationError(
		not_null<Value*> value,
		const QString &text) {
	value->verification.error = text;
	_verificationUpdate.fire_copy(value);
}

const Form &FormController::form() const {
	return _form;
}

not_null<Value*> FormController::findValue(not_null<const Value*> value) {
	const auto i = _form.values.find(value->type);
	Assert(i != end(_form.values));
	const auto result = &i->second;

	Ensures(result == value);
	return result;
}

void FormController::startValueEdit(not_null<const Value*> value) {
	const auto nonconst = findValue(value);
	++nonconst->editScreens;
	if (nonconst->saving()) {
		return;
	}
	for (auto &scan : nonconst->files(FileType::Scan)) {
		loadFile(scan);
	}
	if (nonconst->translationRequired) {
		for (auto &scan : nonconst->files(FileType::Translation)) {
			loadFile(scan);
		}
	}
	for (auto &[type, scan] : nonconst->specialScans) {
		if (nonconst->requiresSpecialScan(type)) {
			loadFile(scan);
		}
	}
	nonconst->saveInEdit(&session());
}

void FormController::loadFile(File &file) {
	if (!file.image.isNull()) {
		file.downloadOffset = file.size;
		return;
	}

	const auto key = FileKey{ file.id, file.dcId };
	const auto i = _fileLoaders.find(key);
	if (i != _fileLoaders.end()) {
		return;
	}
	file.downloadOffset = 0;
	const auto [j, ok] = _fileLoaders.emplace(
		key,
		std::make_unique<mtpFileLoader>(
			&_controller->session(),
			StorageFileLocation(
				file.dcId,
				session().userId(),
				MTP_inputSecureFileLocation(
					MTP_long(file.id),
					MTP_long(file.accessHash))),
			Data::FileOrigin(),
			SecureFileLocation,
			QString(),
			file.size,
			file.size,
			LoadToCacheAsWell,
			LoadFromCloudOrLocal,
			false,
			Data::kImageCacheTag));
	const auto loader = j->second.get();
	loader->updates(
	) | rpl::start_with_next_error_done([=] {
		fileLoadProgress(key, loader->currentOffset());
	}, [=](bool started) {
		fileLoadFail(key);
	}, [=] {
		fileLoadDone(key, loader->bytes());
	}, loader->lifetime());
	loader->start();
}

void FormController::fileLoadDone(FileKey key, const QByteArray &bytes) {
	if (const auto [value, file] = findFile(key); file != nullptr) {
		const auto decrypted = DecryptData(
			bytes::make_span(bytes),
			file->hash,
			file->secret);
		if (decrypted.empty()) {
			fileLoadFail(key);
			return;
		}
		file->downloadOffset = file->size;
		file->image = App::readImage(QByteArray::fromRawData(
			reinterpret_cast<const char*>(decrypted.data()),
			decrypted.size()));
		if (const auto fileInEdit = findEditFile(key)) {
			fileInEdit->fields.image = file->image;
			fileInEdit->fields.downloadOffset = file->downloadOffset;
			_scanUpdated.fire(fileInEdit);
		}
	}
}

void FormController::fileLoadProgress(FileKey key, int offset) {
	if (const auto [value, file] = findFile(key); file != nullptr) {
		file->downloadOffset = offset;
		if (const auto fileInEdit = findEditFile(key)) {
			fileInEdit->fields.downloadOffset = file->downloadOffset;
			_scanUpdated.fire(fileInEdit);
		}
	}
}

void FormController::fileLoadFail(FileKey key) {
	if (const auto [value, file] = findFile(key); file != nullptr) {
		file->downloadOffset = -1;
		if (const auto fileInEdit = findEditFile(key)) {
			fileInEdit->fields.downloadOffset = file->downloadOffset;
			_scanUpdated.fire(fileInEdit);
		}
	}
}

void FormController::cancelValueEdit(not_null<const Value*> value) {
	Expects(value->editScreens > 0);

	const auto nonconst = findValue(value);
	--nonconst->editScreens;
	clearValueEdit(nonconst);
}

void FormController::valueEditFailed(not_null<Value*> value) {
	Expects(!value->saving());

	if (value->editScreens == 0) {
		clearValueEdit(value);
	}
}

void FormController::clearValueEdit(not_null<Value*> value) {
	if (value->saving()) {
		return;
	}
	value->clearEditData();
}

void FormController::cancelValueVerification(not_null<const Value*> value) {
	const auto nonconst = findValue(value);
	clearValueVerification(nonconst);
	if (!nonconst->saving()) {
		valueEditFailed(nonconst);
	}
}

void FormController::clearValueVerification(not_null<Value*> value) {
	const auto was = (value->verification.codeLength != 0);
	if (const auto requestId = base::take(value->verification.requestId)) {
		_api.request(requestId).cancel();
	}
	value->verification = Verification();
	if (was) {
		_verificationUpdate.fire_copy(value);
	}
}

bool FormController::isEncryptedValue(Value::Type type) const {
	return (type != Value::Type::Phone && type != Value::Type::Email);
}

void FormController::saveValueEdit(
		not_null<const Value*> value,
		ValueMap &&data) {
	if (value->saving() || _submitRequestId) {
		return;
	}

	// If we didn't change anything, we don't send save request
	// and we don't reset value->error/[scan|translation]MissingError.
	// Otherwise we reset them after save by re-parsing the value.
	const auto nonconst = findValue(value);
	if (!ValueChanged(nonconst, data)) {
		nonconst->saveRequestId = -1;
		crl::on_main(this, [=] {
			nonconst->clearEditData();
			nonconst->saveRequestId = 0;
			_valueSaveFinished.fire_copy(nonconst);
		});
		return;
	}
	ApplyDataChanges(nonconst->data, std::move(data));

	if (isEncryptedValue(nonconst->type)) {
		saveEncryptedValue(nonconst);
	} else {
		savePlainTextValue(nonconst);
	}
}

void FormController::deleteValueEdit(not_null<const Value*> value) {
	if (value->saving() || _submitRequestId) {
		return;
	}

	const auto nonconst = findValue(value);
	nonconst->saveRequestId = _api.request(MTPaccount_DeleteSecureValue(
		MTP_vector<MTPSecureValueType>(1, ConvertType(nonconst->type))
	)).done([=](const MTPBool &result) {
		resetValue(*nonconst);
		_valueSaveFinished.fire_copy(value);
	}).fail([=](const MTP::Error &error) {
		nonconst->saveRequestId = 0;
		valueSaveShowError(nonconst, error);
	}).send();
}

void FormController::saveEncryptedValue(not_null<Value*> value) {
	Expects(isEncryptedValue(value->type));

	if (_secret.empty()) {
		_secretCallbacks.push_back([=] {
			saveEncryptedValue(value);
		});
		return;
	}

	const auto wrapFile = [](const EditFile &file) {
		if (const auto uploadData = file.uploadData.get()) {
			return MTP_inputSecureFileUploaded(
				MTP_long(file.fields.id),
				MTP_int(uploadData->partsCount),
				MTP_bytes(uploadData->md5checksum),
				MTP_bytes(file.fields.hash),
				MTP_bytes(file.fields.encryptedSecret));
		}
		return MTP_inputSecureFile(
			MTP_long(file.fields.id),
			MTP_long(file.fields.accessHash));
	};
	const auto wrapList = [&](not_null<const Value*> value, FileType type) {
		const auto &list = value->filesInEdit(type);
		auto result = QVector<MTPInputSecureFile>();
		result.reserve(list.size());
		for (const auto &scan : value->filesInEdit(type)) {
			if (scan.deleted) {
				continue;
			}
			result.push_back(wrapFile(scan));
		}
		return result;
	};

	const auto files = wrapList(value, FileType::Scan);
	const auto translations = wrapList(value, FileType::Translation);

	if (value->data.secret.empty()) {
		value->data.secret = GenerateSecretBytes();
	}
	const auto encryptedData = EncryptData(
		SerializeData(GetTexts(value->data.parsedInEdit)),
		value->data.secret);
	value->data.hashInEdit = encryptedData.hash;
	value->data.encryptedSecretInEdit = EncryptValueSecret(
		value->data.secret,
		_secret,
		value->data.hashInEdit);

	const auto hasSpecialFile = [&](FileType type) {
		const auto i = value->specialScansInEdit.find(type);
		return (i != end(value->specialScansInEdit) && !i->second.deleted);
	};
	const auto specialFile = [&](FileType type) {
		const auto i = value->specialScansInEdit.find(type);
		return (i != end(value->specialScansInEdit) && !i->second.deleted)
			? wrapFile(i->second)
			: MTPInputSecureFile();
	};
	const auto frontSide = specialFile(FileType::FrontSide);
	const auto reverseSide = specialFile(FileType::ReverseSide);
	const auto selfie = specialFile(FileType::Selfie);

	const auto type = ConvertType(value->type);
	const auto flags = (value->data.parsedInEdit.fields.empty()
			? MTPDinputSecureValue::Flag(0)
			: MTPDinputSecureValue::Flag::f_data)
		| (hasSpecialFile(FileType::FrontSide)
			? MTPDinputSecureValue::Flag::f_front_side
			: MTPDinputSecureValue::Flag(0))
		| (hasSpecialFile(FileType::ReverseSide)
			? MTPDinputSecureValue::Flag::f_reverse_side
			: MTPDinputSecureValue::Flag(0))
		| (hasSpecialFile(FileType::Selfie)
			? MTPDinputSecureValue::Flag::f_selfie
			: MTPDinputSecureValue::Flag(0))
		| (translations.empty()
			? MTPDinputSecureValue::Flag(0)
			: MTPDinputSecureValue::Flag::f_translation)
		| (files.empty()
			? MTPDinputSecureValue::Flag(0)
			: MTPDinputSecureValue::Flag::f_files);
	Assert(flags != MTPDinputSecureValue::Flags(0));

	sendSaveRequest(value, MTP_inputSecureValue(
		MTP_flags(flags),
		type,
		MTP_secureData(
			MTP_bytes(encryptedData.bytes),
			MTP_bytes(value->data.hashInEdit),
			MTP_bytes(value->data.encryptedSecretInEdit)),
		frontSide,
		reverseSide,
		selfie,
		MTP_vector<MTPInputSecureFile>(translations),
		MTP_vector<MTPInputSecureFile>(files),
		MTPSecurePlainData()));
}

void FormController::savePlainTextValue(not_null<Value*> value) {
	Expects(!isEncryptedValue(value->type));

	const auto text = getPlainTextFromValue(value);
	const auto type = [&] {
		switch (value->type) {
		case Value::Type::Phone: return MTP_secureValueTypePhone();
		case Value::Type::Email: return MTP_secureValueTypeEmail();
		}
		Unexpected("Value type in savePlainTextValue().");
	}();
	const auto plain = [&] {
		switch (value->type) {
		case Value::Type::Phone: return MTP_securePlainPhone;
		case Value::Type::Email: return MTP_securePlainEmail;
		}
		Unexpected("Value type in savePlainTextValue().");
	}();
	sendSaveRequest(value, MTP_inputSecureValue(
		MTP_flags(MTPDinputSecureValue::Flag::f_plain_data),
		type,
		MTPSecureData(),
		MTPInputSecureFile(),
		MTPInputSecureFile(),
		MTPInputSecureFile(),
		MTPVector<MTPInputSecureFile>(),
		MTPVector<MTPInputSecureFile>(),
		plain(MTP_string(text))));
}

void FormController::sendSaveRequest(
		not_null<Value*> value,
		const MTPInputSecureValue &data) {
	Expects(value->saveRequestId == 0);

	value->saveRequestId = _api.request(MTPaccount_SaveSecureValue(
		data,
		MTP_long(_secretId)
	)).done([=](const MTPSecureValue &result) {
		auto scansInEdit = value->takeAllFilesInEdit();

		auto refreshed = parseValue(result, scansInEdit);
		decryptValue(refreshed);
		value->fillDataFrom(std::move(refreshed));

		_valueSaveFinished.fire_copy(value);
	}).fail([=](const MTP::Error &error) {
		value->saveRequestId = 0;
		const auto code = error.type();
		if (handleAppUpdateError(code)) {
		} else if (code == qstr("PHONE_VERIFICATION_NEEDED")) {
			if (value->type == Value::Type::Phone) {
				startPhoneVerification(value);
				return;
			}
		} else if (code == qstr("PHONE_NUMBER_INVALID")) {
			if (value->type == Value::Type::Phone) {
				value->data.parsedInEdit.fields["value"].error
					= tr::lng_bad_phone(tr::now);
				valueSaveFailed(value);
				return;
			}
		} else if (code == qstr("EMAIL_VERIFICATION_NEEDED")) {
			if (value->type == Value::Type::Email) {
				startEmailVerification(value);
				return;
			}
		} else if (code == qstr("EMAIL_INVALID")) {
			if (value->type == Value::Type::Email) {
				value->data.parsedInEdit.fields["value"].error
					= tr::lng_cloud_password_bad_email(tr::now);
				valueSaveFailed(value);
				return;
			}
		}
		if (SaveErrorRequiresRestart(code)) {
			suggestRestart();
		} else {
			valueSaveShowError(value, error);
		}
	}).send();
}

QString FormController::getPhoneFromValue(
		not_null<const Value*> value) const {
	Expects(value->type == Value::Type::Phone);

	return getPlainTextFromValue(value);
}

QString FormController::getEmailFromValue(
		not_null<const Value*> value) const {
	Expects(value->type == Value::Type::Email);

	return getPlainTextFromValue(value);
}

QString FormController::getPlainTextFromValue(
		not_null<const Value*> value) const {
	Expects(value->type == Value::Type::Phone
		|| value->type == Value::Type::Email);

	const auto i = value->data.parsedInEdit.fields.find("value");
	Assert(i != end(value->data.parsedInEdit.fields));
	return i->second.text;
}

void FormController::startPhoneVerification(not_null<Value*> value) {
	value->verification.requestId = _api.request(MTPaccount_SendVerifyPhoneCode(
		MTP_string(getPhoneFromValue(value)),
		MTP_codeSettings(MTP_flags(0))
	)).done([=](const MTPauth_SentCode &result) {
		Expects(result.type() == mtpc_auth_sentCode);

		value->verification.requestId = 0;

		const auto &data = result.c_auth_sentCode();
		value->verification.phoneCodeHash = qs(data.vphone_code_hash());
		switch (data.vtype().type()) {
		case mtpc_auth_sentCodeTypeApp:
			LOG(("API Error: sentCodeTypeApp not expected "
				"in FormController::startPhoneVerification."));
			return;
		case mtpc_auth_sentCodeTypeFlashCall:
			LOG(("API Error: sentCodeTypeFlashCall not expected "
				"in FormController::startPhoneVerification."));
			return;
		case mtpc_auth_sentCodeTypeCall: {
			const auto &type = data.vtype().c_auth_sentCodeTypeCall();
			value->verification.codeLength = (type.vlength().v > 0)
				? type.vlength().v
				: -1;
			value->verification.call = std::make_unique<SentCodeCall>(
				[=] { requestPhoneCall(value); },
				[=] { _verificationUpdate.fire_copy(value); });
			value->verification.call->setStatus(
				{ SentCodeCall::State::Called, 0 });
			if (data.vnext_type()) {
				LOG(("API Error: next_type is not supported for calls."));
			}
		} break;
		case mtpc_auth_sentCodeTypeSms: {
			const auto &type = data.vtype().c_auth_sentCodeTypeSms();
			value->verification.codeLength = (type.vlength().v > 0)
				? type.vlength().v
				: -1;
			const auto next = data.vnext_type();
			if (next && next->type() == mtpc_auth_codeTypeCall) {
				value->verification.call = std::make_unique<SentCodeCall>(
					[=] { requestPhoneCall(value); },
					[=] { _verificationUpdate.fire_copy(value); });
				value->verification.call->setStatus({
					SentCodeCall::State::Waiting,
					data.vtimeout().value_or(60) });
			}
		} break;
		}
		_verificationNeeded.fire_copy(value);
	}).fail([=](const MTP::Error &error) {
		value->verification.requestId = 0;
		valueSaveShowError(value, error);
	}).send();
}

void FormController::startEmailVerification(not_null<Value*> value) {
	value->verification.requestId = _api.request(MTPaccount_SendVerifyEmailCode(
		MTP_string(getEmailFromValue(value))
	)).done([=](const MTPaccount_SentEmailCode &result) {
		Expects(result.type() == mtpc_account_sentEmailCode);

		value->verification.requestId = 0;
		const auto &data = result.c_account_sentEmailCode();
		value->verification.codeLength = (data.vlength().v > 0)
			? data.vlength().v
			: -1;
		_verificationNeeded.fire_copy(value);
	}).fail([=](const MTP::Error &error) {
		valueSaveShowError(value, error);
	}).send();
}


void FormController::requestPhoneCall(not_null<Value*> value) {
	Expects(value->verification.call != nullptr);

	value->verification.call->setStatus(
		{ SentCodeCall::State::Calling, 0 });
	_api.request(MTPauth_ResendCode(
		MTP_string(getPhoneFromValue(value)),
		MTP_string(value->verification.phoneCodeHash)
	)).done([=](const MTPauth_SentCode &code) {
		value->verification.call->callDone();
	}).send();
}

void FormController::valueSaveShowError(
		not_null<Value*> value,
		const MTP::Error &error) {
	_view->show(Box<InformBox>(
		Lang::Hard::SecureSaveError() + "\n" + error.type()));
	valueSaveFailed(value);
}

void FormController::valueSaveFailed(not_null<Value*> value) {
	valueEditFailed(value);
	_valueSaveFinished.fire_copy(value);
}

void FormController::generateSecret(bytes::const_span password) {
	Expects(!password.empty());

	if (_saveSecretRequestId) {
		return;
	}
	auto secret = GenerateSecretBytes();

	auto saved = SavedCredentials();
	saved.hashForAuth = _passwordCheckHash;
	saved.hashForSecret = Core::ComputeSecureSecretHash(
		_password.newSecureAlgo,
		password);
	saved.secretId = CountSecureSecretId(secret);

	const auto callback = [=](const Core::CloudPasswordResult &check) {
		saveSecret(check, saved, secret);
	};
	checkPasswordHash(_saveSecretRequestId, saved.hashForAuth, callback);
}

void FormController::saveSecret(
		const Core::CloudPasswordResult &check,
		const SavedCredentials &saved,
		const bytes::vector &secret) {
	const auto encryptedSecret = EncryptSecureSecret(
		secret,
		saved.hashForSecret);

	using Flag = MTPDaccount_passwordInputSettings::Flag;
	_saveSecretRequestId = _api.request(MTPaccount_UpdatePasswordSettings(
		check.result,
		MTP_account_passwordInputSettings(
			MTP_flags(Flag::f_new_secure_settings),
			MTPPasswordKdfAlgo(), // new_algo
			MTPbytes(), // new_password_hash
			MTPstring(), // hint
			MTPstring(), // email
			MTP_secureSecretSettings(
				Core::PrepareSecureSecretAlgo(_password.newSecureAlgo),
				MTP_bytes(encryptedSecret),
				MTP_long(saved.secretId)))
	)).done([=](const MTPBool &result) {
		session().data().rememberPassportCredentials(
			std::move(saved),
			kRememberCredentialsDelay);

		_saveSecretRequestId = 0;
		_secret = secret;
		_secretId = saved.secretId;
		//_password.salt = newPasswordSaltFull;
		for (const auto &callback : base::take(_secretCallbacks)) {
			callback();
		}
	}).fail([=](const MTP::Error &error) {
		_saveSecretRequestId = 0;
		if (error.type() != qstr("SRP_ID_INVALID")
			|| !handleSrpIdInvalid(_saveSecretRequestId)) {
			suggestRestart();
		}
	}).send();
}

void FormController::suggestRestart() {
	_suggestingRestart = true;
	_view->show(Box<ConfirmBox>(
		tr::lng_passport_restart_sure(tr::now),
		tr::lng_passport_restart(tr::now),
		[=] { _controller->showPassportForm(_request); },
		[=] { cancel(); }));
}

void FormController::requestForm() {
	if (_request.nonce.isEmpty()) {
		_formRequestId = -1;
		formFail(NonceNameByScope(_request.scope).toUpper() + "_EMPTY");
		return;
	}
	_formRequestId = _api.request(MTPaccount_GetAuthorizationForm(
		MTP_int(_request.botId),
		MTP_string(_request.scope),
		MTP_string(_request.publicKey)
	)).done([=](const MTPaccount_AuthorizationForm &result) {
		_formRequestId = 0;
		formDone(result);
	}).fail([=](const MTP::Error &error) {
		formFail(error.type());
	}).send();
}

auto FormController::parseFiles(
	const QVector<MTPSecureFile> &data,
	const std::vector<EditFile> &editData) const
-> std::vector<File> {
	auto result = std::vector<File>();
	result.reserve(data.size());

	for (const auto &file : data) {
		if (auto normal = parseFile(file, editData)) {
			result.push_back(std::move(*normal));
		}
	}

	return result;
}

auto FormController::parseFile(
	const MTPSecureFile &data,
	const std::vector<EditFile> &editData) const
-> std::optional<File> {
	switch (data.type()) {
	case mtpc_secureFileEmpty:
		return std::nullopt;

	case mtpc_secureFile: {
		const auto &fields = data.c_secureFile();
		auto result = File();
		result.id = fields.vid().v;
		result.accessHash = fields.vaccess_hash().v;
		result.size = fields.vsize().v;
		result.date = fields.vdate().v;
		result.dcId = fields.vdc_id().v;
		result.hash = bytes::make_vector(fields.vfile_hash().v);
		result.encryptedSecret = bytes::make_vector(fields.vsecret().v);
		fillDownloadedFile(result, editData);
		return result;
	} break;
	}
	Unexpected("Type in FormController::parseFile.");
}

void FormController::fillDownloadedFile(
		File &destination,
		const std::vector<EditFile> &source) const {
	const auto i = ranges::find(
		source,
		destination.hash,
		[](const EditFile &file) { return file.fields.hash; });
	if (i == source.end()) {
		return;
	}
	destination.image = i->fields.image;
	destination.downloadOffset = i->fields.downloadOffset;
	if (!i->uploadData) {
		return;
	}
	const auto &bytes = i->uploadData->bytes;
	if (bytes.size() > Storage::kMaxFileInMemory) {
		return;
	}
	session().data().cache().put(
		Data::DocumentCacheKey(destination.dcId, destination.id),
		Storage::Cache::Database::TaggedValue(
			QByteArray(
				reinterpret_cast<const char*>(bytes.data()),
				bytes.size()),
			Data::kImageCacheTag));
}

auto FormController::parseValue(
		const MTPSecureValue &value,
		const std::vector<EditFile> &editData) const -> Value {
	Expects(value.type() == mtpc_secureValue);

	const auto &data = value.c_secureValue();
	const auto type = ConvertType(data.vtype());
	auto result = Value(type);
	result.submitHash = bytes::make_vector(data.vhash().v);
	if (const auto secureData = data.vdata()) {
		secureData->match([&](const MTPDsecureData &data) {
			result.data.original = data.vdata().v;
			result.data.hash = bytes::make_vector(data.vdata_hash().v);
			result.data.encryptedSecret = bytes::make_vector(data.vsecret().v);
		});
	}
	if (const auto files = data.vfiles()) {
		result.files(FileType::Scan) = parseFiles(files->v, editData);
	}
	if (const auto translation = data.vtranslation()) {
		result.files(FileType::Translation) = parseFiles(
			translation->v,
			editData);
	}
	const auto parseSpecialScan = [&](
			FileType type,
			const MTPSecureFile &file) {
		if (auto parsed = parseFile(file, editData)) {
			result.specialScans.emplace(type, std::move(*parsed));
		}
	};
	if (const auto side = data.vfront_side()) {
		parseSpecialScan(FileType::FrontSide, *side);
	}
	if (const auto side = data.vreverse_side()) {
		parseSpecialScan(FileType::ReverseSide, *side);
	}
	if (const auto selfie = data.vselfie()) {
		parseSpecialScan(FileType::Selfie, *selfie);
	}
	if (const auto plain = data.vplain_data()) {
		plain->match([&](const MTPDsecurePlainPhone &data) {
			result.data.parsed.fields["value"].text = qs(data.vphone());
		}, [&](const MTPDsecurePlainEmail &data) {
			result.data.parsed.fields["value"].text = qs(data.vemail());
		});
	}
	return result;
}

template <typename Condition>
EditFile *FormController::findEditFileByCondition(Condition &&condition) {
	for (auto &pair : _form.values) {
		auto &value = pair.second;
		const auto foundInList = [&](FileType type) -> EditFile* {
			for (auto &scan : value.filesInEdit(type)) {
				if (condition(scan)) {
					return &scan;
				}
			}
			return nullptr;
		};
		if (const auto result = foundInList(FileType::Scan)) {
			return result;
		} else if (const auto other = foundInList(FileType::Translation)) {
			return other;
		}
		for (auto &[special, scan] : value.specialScansInEdit) {
			if (condition(scan)) {
				return &scan;
			}
		}
	}
	return nullptr;
}

EditFile *FormController::findEditFile(const FullMsgId &fullId) {
	return findEditFileByCondition([&](const EditFile &file) {
		return (file.uploadData && file.uploadData->fullId == fullId);
	});
}

EditFile *FormController::findEditFile(const FileKey &key) {
	return findEditFileByCondition([&](const EditFile &file) {
		return (file.fields.dcId == key.dcId && file.fields.id == key.id);
	});
}

auto FormController::findFile(const FileKey &key)
-> std::pair<Value*, File*> {
	const auto found = [&](const File &file) {
		return (file.dcId == key.dcId) && (file.id == key.id);
	};
	for (auto &pair : _form.values) {
		auto &value = pair.second;
		const auto foundInList = [&](FileType type) -> File* {
			for (auto &scan : value.files(type)) {
				if (found(scan)) {
					return &scan;
				}
			}
			return nullptr;
		};
		if (const auto result = foundInList(FileType::Scan)) {
			return { &value, result };
		} else if (const auto other = foundInList(FileType::Translation)) {
			return { &value, other };
		}
		for (auto &[special, scan] : value.specialScans) {
			if (found(scan)) {
				return { &value, &scan };
			}
		}
	}
	return { nullptr, nullptr };
}

void FormController::formDone(const MTPaccount_AuthorizationForm &result) {
	if (!parseForm(result)) {
		_view->showCriticalError(tr::lng_passport_form_error(tr::now));
	} else {
		showForm();
	}
}

void FormController::requestConfig() {
	const auto hash = ConfigInstance().hash;
	_configRequestId = _api.request(MTPhelp_GetPassportConfig(
		MTP_int(hash)
	)).done([=](const MTPhelp_PassportConfig &result) {
		_configRequestId = 0;
		ConfigInstance() = ParseConfig(result);
		showForm();
	}).fail([=](const MTP::Error &error) {
		_configRequestId = 0;
		showForm();
	}).send();
}

bool FormController::parseForm(const MTPaccount_AuthorizationForm &result) {
	Expects(result.type() == mtpc_account_authorizationForm);

	const auto &data = result.c_account_authorizationForm();

	session().data().processUsers(data.vusers());

	for (const auto &value : data.vvalues().v) {
		auto parsed = parseValue(value);
		const auto type = parsed.type;
		const auto alreadyIt = _form.values.find(type);
		if (alreadyIt != _form.values.end()) {
			LOG(("API Error: Two values for type %1 in authorization form"
				"%1").arg(int(type)));
			return false;
		}
		_form.values.emplace(type, std::move(parsed));
	}
	if (const auto url = data.vprivacy_policy_url()) {
		_form.privacyPolicyUrl = qs(*url);
	}
	for (const auto &required : data.vrequired_types().v) {
		const auto row = CollectRequestedRow(required);
		for (const auto requested : row.values) {
			const auto type = requested.type;
			const auto [i, ok] = _form.values.emplace(type, Value(type));
			auto &value = i->second;
			value.translationRequired = requested.translationRequired;
			value.selfieRequired = requested.selfieRequired;
			value.nativeNames = requested.nativeNames;
		}
		_form.request.push_back(row.values
			| ranges::views::transform([](const RequestedValue &value) {
				return value.type;
			}) | ranges::to_vector);
	}
	if (!ValidateForm(_form)) {
		return false;
	}
	_bot = session().data().userLoaded(_request.botId);
	_form.pendingErrors = data.verrors().v;
	return true;
}

void FormController::formFail(const QString &error) {
	_savedPasswordValue = QByteArray();
	_serviceErrorText = error;
	if (!handleAppUpdateError(error)) {
		_view->showCriticalError(
			tr::lng_passport_form_error(tr::now) + "\n" + error);
	}
}

bool FormController::handleAppUpdateError(const QString &error) {
	if (error == qstr("APP_VERSION_OUTDATED")) {
		_view->showUpdateAppBox();
		return true;
	}
	return false;
}

void FormController::requestPassword() {
	if (_passwordRequestId) {
		return;
	}
	_passwordRequestId = _api.request(MTPaccount_GetPassword(
	)).done([=](const MTPaccount_Password &result) {
		_passwordRequestId = 0;
		passwordDone(result);
	}).fail([=](const MTP::Error &error) {
		formFail(error.type());
	}).send();
}

void FormController::passwordDone(const MTPaccount_Password &result) {
	Expects(result.type() == mtpc_account_password);

	const auto changed = applyPassword(result.c_account_password());
	if (changed) {
		showForm();
	}
	shortPollEmailConfirmation();
}

void FormController::shortPollEmailConfirmation() {
	if (_password.unconfirmedPattern.isEmpty()) {
		_shortPollTimer.cancel();
		return;
	}
	_shortPollTimer.callOnce(kShortPollTimeout);
}

void FormController::showForm() {
	if (_formRequestId || _passwordRequestId || _configRequestId) {
		return;
	} else if (!_bot) {
		formFail(Lang::Hard::NoAuthorizationBot());
		return;
	}
	if (_password.unknownAlgo
		|| v::is_null(_password.newAlgo)
		|| v::is_null(_password.newSecureAlgo)) {
		_view->showUpdateAppBox();
		return;
	} else if (_password.request) {
		if (!_savedPasswordValue.isEmpty()) {
			submitPassword(base::duplicate(_savedPasswordValue));
		} else if (const auto saved = session().data().passportCredentials()) {
			checkSavedPasswordSettings(*saved);
		} else {
			_view->showAskPassword();
		}
	} else {
		_view->showNoPassword();
	}
}

bool FormController::applyPassword(const MTPDaccount_password &result) {
	auto settings = PasswordSettings();
	settings.hint = qs(result.vhint().value_or_empty());
	settings.hasRecovery = result.is_has_recovery();
	settings.notEmptyPassport = result.is_has_secure_values();
	settings.request = Core::ParseCloudPasswordCheckRequest(result);
	settings.unknownAlgo = result.vcurrent_algo() && !settings.request;
	settings.unconfirmedPattern =
		qs(result.vemail_unconfirmed_pattern().value_or_empty());
	settings.newAlgo = Core::ValidateNewCloudPasswordAlgo(
		Core::ParseCloudPasswordAlgo(result.vnew_algo()));
	settings.newSecureAlgo = Core::ValidateNewSecureSecretAlgo(
		Core::ParseSecureSecretAlgo(result.vnew_secure_algo()));
	openssl::AddRandomSeed(bytes::make_span(result.vsecure_random().v));
	return applyPassword(std::move(settings));
}

bool FormController::applyPassword(PasswordSettings &&settings) {
	if (_password != settings) {
		_password = std::move(settings);
		return true;
	}
	return false;
}

void FormController::cancel() {
	if (!_submitSuccess && _serviceErrorText.isEmpty()) {
		_view->show(Box<ConfirmBox>(
			tr::lng_passport_stop_sure(tr::now),
			tr::lng_passport_stop(tr::now),
			[=] { cancelSure(); },
			[=] { cancelAbort(); }));
	} else {
		cancelSure();
	}
}

void FormController::cancelAbort() {
	if (_cancelled || _submitSuccess) {
		return;
	} else if (_suggestingRestart) {
		suggestRestart();
	}
}

void FormController::cancelSure() {
	if (!_cancelled) {
		_cancelled = true;

		if (!_request.callbackUrl.isEmpty()
			&& (_serviceErrorText.isEmpty()
				|| ForwardServiceErrorRequired(_serviceErrorText))) {
			const auto url = qthelp::url_append_query_or_hash(
				_request.callbackUrl,
				(_submitSuccess
					? "tg_passport=success"
					: (_serviceErrorText.isEmpty()
						? "tg_passport=cancel"
						: "tg_passport=error&error=" + _serviceErrorText)));
			UrlClickHandler::Open(url);
		}
		const auto timeout = _view->closeGetDuration();
		base::call_delayed(timeout, this, [=] {
			_controller->clearPassportForm();
		});
	}
}

rpl::lifetime &FormController::lifetime() {
	return _lifetime;
}

FormController::~FormController() = default;

} // namespace Passport
