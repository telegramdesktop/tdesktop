/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "passport/passport_form_controller.h"

#include "passport/passport_encryption.h"
#include "passport/passport_panel_controller.h"
#include "boxes/confirm_box.h"
#include "boxes/passcode_box.h"
#include "lang/lang_keys.h"
#include "lang/lang_hardcoded.h"
#include "base/openssl_help.h"
#include "base/qthelp_url.h"
#include "data/data_session.h"
#include "mainwindow.h"
#include "window/window_controller.h"
#include "core/click_handler_types.h"
#include "ui/toast/toast.h"
#include "auth_session.h"
#include "storage/localimageloader.h"
#include "storage/localstorage.h"
#include "storage/file_upload.h"
#include "storage/file_download.h"

namespace Passport {
namespace {

constexpr auto kDocumentScansLimit = 20;
constexpr auto kShortPollTimeout = TimeMs(3000);
constexpr auto kRememberCredentialsDelay = TimeMs(1800 * 1000);

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
};

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

QString SpecialScanCredentialsKey(SpecialFile type) {
	switch (type) {
	case SpecialFile::FrontSide: return "front_side";
	case SpecialFile::ReverseSide: return "reverse_side";
	case SpecialFile::Selfie: return "selfie";
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

FormRequest::FormRequest(
	UserId botId,
	const QString &scope,
	const QString &callbackUrl,
	const QString &publicKey,
	const QString &payload,
	const QString &errors)
: botId(botId)
, scope(scope)
, callbackUrl(ValidateUrl(callbackUrl))
, publicKey(publicKey)
, payload(payload)
, errors(errors) {
}

EditFile::EditFile(
	not_null<const Value*> value,
	const File &fields,
	std::unique_ptr<UploadScanData> &&uploadData)
: value(value)
, fields(std::move(fields))
, uploadData(std::move(uploadData))
, guard(std::make_shared<bool>(true)) {
}

UploadScanDataPointer::UploadScanDataPointer(
	std::unique_ptr<UploadScanData> &&value)
: _value(std::move(value)) {
}

UploadScanDataPointer::UploadScanDataPointer(
	UploadScanDataPointer &&other) = default;

UploadScanDataPointer &UploadScanDataPointer::operator=(
	UploadScanDataPointer &&other) = default;

UploadScanDataPointer::~UploadScanDataPointer() {
	if (const auto value = _value.get()) {
		if (const auto fullId = value->fullId) {
			Auth().uploader().cancel(fullId);
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

Value::Value(Type type) : type(type) {
}

bool Value::requiresSpecialScan(
		SpecialFile type,
		bool selfieRequired) const {
	switch (type) {
	case SpecialFile::FrontSide:
		return (this->type == Type::Passport)
			|| (this->type == Type::DriverLicense)
			|| (this->type == Type::IdentityCard)
			|| (this->type == Type::InternalPassport);
	case SpecialFile::ReverseSide:
		return (this->type == Type::DriverLicense)
			|| (this->type == Type::IdentityCard);
	case SpecialFile::Selfie:
		return selfieRequired;
	}
	Unexpected("Special scan type in requiresSpecialScan.");
}

bool Value::scansAreFilled(bool selfieRequired) const {
	if (!requiresSpecialScan(SpecialFile::FrontSide, selfieRequired)) {
		return !scans.empty();
	}
	const auto types = {
		SpecialFile::FrontSide,
		SpecialFile::ReverseSide,
		SpecialFile::Selfie
	};
	for (const auto type : types) {
		if (requiresSpecialScan(type, selfieRequired)
			&& (specialScans.find(type) == end(specialScans))) {
			return false;
		}
	}
	return true;
};


FormController::FormController(
	not_null<Window::Controller*> controller,
	const FormRequest &request)
: _controller(controller)
, _request(PreprocessRequest(request))
, _shortPollTimer([=] { reloadPassword(); })
, _view(std::make_unique<PanelController>(this)) {
}

void FormController::show() {
	requestForm();
	requestPassword();
}

UserData *FormController::bot() const {
	return _bot;
}

QString FormController::privacyPolicyUrl() const {
	return _form.privacyPolicyUrl;
}

bytes::vector FormController::passwordHashForAuth(
		bytes::const_span password) const {
	return openssl::Sha256(bytes::concatenate(
		_password.salt,
		password,
		_password.salt));
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
		if (!value->scans.empty()) {
			auto files = QJsonArray();
			for (const auto &scan : value->scans) {
				files.append(GetJSONFromFile(scan));
			}
			object.insert("files", files);
		}
		for (const auto &[type, scan] : value->specialScans) {
			const auto selfieRequired = _form.identitySelfieRequired;
			if (value->requiresSpecialScan(type, selfieRequired)) {
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
	const auto scopes = ComputeScopes(this);
	for (const auto &scope : scopes) {
		const auto row = ComputeScopeRow(scope);
		if (row.ready.isEmpty() || !row.error.isEmpty()) {
			errors.push_back(scope.fields);
			continue;
		}
		addValue(scope.fields);
		if (!scope.documents.empty()) {
			for (const auto &document : scope.documents) {
				if (document->scansAreFilled(scope.selfieRequired)) {
					addValue(document);
					break;
				}
			}
		}
	}

	auto json = QJsonObject();
	if (errors.empty()) {
		json.insert("secure_data", secureData);
		json.insert("payload", _request.payload);
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

	_submitRequestId = request(MTPaccount_AcceptAuthorization(
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

		_view->showToast(lang(lng_passport_success));

		App::CallDelayed(
			Ui::Toast::DefaultDuration + st::toastFadeOutDuration,
			this,
			[=] { cancel(); });
	}).fail([=](const RPCError &error) {
		_submitRequestId = 0;
		if (AcceptErrorRequiresRestart(error.type())) {
			suggestRestart();
		} else {
			_view->show(Box<InformBox>(
				Lang::Hard::SecureAcceptError() + "\n" + error.type()));
		}
	}).send();

	return {};
}

void FormController::submitPassword(const QByteArray &password) {
	Expects(!_password.salt.empty());

	const auto submitSaved = !base::take(_savedPasswordValue).isEmpty();
	if (_passwordCheckRequestId) {
		return;
	} else if (password.isEmpty()) {
		_passwordError.fire(QString());
		return;
	}
	_passwordCheckRequestId = request(MTPaccount_GetPasswordSettings(
		MTP_bytes(passwordHashForAuth(bytes::make_span(password)))
	)).handleFloodErrors(
	).done([=](const MTPaccount_PasswordSettings &result) {
		Expects(result.type() == mtpc_account_passwordSettings);

		_passwordCheckRequestId = 0;
		_savedPasswordValue = QByteArray();
		const auto &data = result.c_account_passwordSettings();
		const auto hashForAuth = passwordHashForAuth(
			bytes::make_span(password));
		const auto hashForSecret = (data.vsecure_salt.v.isEmpty()
			? bytes::vector()
			: CountPasswordHashForSecret(
				bytes::make_span(data.vsecure_salt.v),
				bytes::make_span(password)));
		_password.confirmedEmail = qs(data.vemail);
		validateSecureSecret(
			bytes::make_span(data.vsecure_secret.v),
			hashForSecret,
			bytes::make_span(password),
			data.vsecure_secret_id.v);
		if (!_secret.empty()) {
			auto saved = SavedCredentials();
			saved.hashForAuth = hashForAuth;
			saved.hashForSecret = hashForSecret;
			saved.secretId = _secretId;
			Auth().data().rememberPassportCredentials(
				std::move(saved),
				kRememberCredentialsDelay);
		}
	}).fail([=](const RPCError &error) {
		_passwordCheckRequestId = 0;
		if (submitSaved) {
			// Force reload and show form.
			_password = PasswordSettings();
			reloadPassword();
		} else if (MTP::isFloodError(error)) {
			_passwordError.fire(lang(lng_flood_error));
		} else if (error.type() == qstr("PASSWORD_HASH_INVALID")) {
			_passwordError.fire(lang(lng_passport_password_wrong));
		} else {
			_passwordError.fire_copy(error.type());
		}
	}).send();
}

void FormController::checkSavedPasswordSettings(
		const SavedCredentials &credentials) {
	_passwordCheckRequestId = request(MTPaccount_GetPasswordSettings(
		MTP_bytes(credentials.hashForAuth)
	)).done([=](const MTPaccount_PasswordSettings &result) {
		Expects(result.type() == mtpc_account_passwordSettings);

		_passwordCheckRequestId = 0;
		const auto &data = result.c_account_passwordSettings();
		if (!data.vsecure_secret.v.isEmpty()
			&& data.vsecure_secret_id.v == credentials.secretId) {
			_password.confirmedEmail = qs(data.vemail);
			validateSecureSecret(
				bytes::make_span(data.vsecure_secret.v),
				credentials.hashForSecret,
				{},
				data.vsecure_secret_id.v);
		}
		if (_secret.empty()) {
			Auth().data().forgetPassportCredentials();
			showForm();
		}
	}).fail([=](const RPCError &error) {
		_passwordCheckRequestId = 0;
		Auth().data().forgetPassportCredentials();
		showForm();
	}).send();
}

void FormController::recoverPassword() {
	if (!_password.hasRecovery) {
		_view->show(Box<InformBox>(lang(lng_signin_no_email_forgot)));
		return;
	} else if (_recoverRequestId) {
		return;
	}
	_recoverRequestId = request(MTPauth_RequestPasswordRecovery(
	)).done([=](const MTPauth_PasswordRecovery &result) {
		Expects(result.type() == mtpc_auth_passwordRecovery);

		_recoverRequestId = 0;

		const auto &data = result.c_auth_passwordRecovery();
		const auto pattern = qs(data.vemail_pattern);
		const auto box = _view->show(Box<RecoverBox>(
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
	}).fail([=](const RPCError &error) {
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
	_passwordRequestId = request(MTPaccount_UpdatePasswordSettings(
		MTP_bytes(QByteArray()),
		MTP_account_passwordInputSettings(
			MTP_flags(MTPDaccount_passwordInputSettings::Flag::f_email),
			MTP_bytes(QByteArray()), // new_salt
			MTP_bytes(QByteArray()), // new_password_hash
			MTP_string(QString()), // hint
			MTP_string(QString()), // email
			MTP_bytes(QByteArray()), // new_secure_salt
			MTP_bytes(QByteArray()), // new_secure_secret
			MTP_long(0)) // new_secure_secret_hash
	)).done([=](const MTPBool &result) {
		_passwordRequestId = 0;
		reloadPassword();
	}).fail([=](const RPCError &error) {
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
		using Flag = MTPDaccount_passwordInputSettings::Flag;
		_saveSecretRequestId = request(MTPaccount_UpdatePasswordSettings(
			MTP_bytes(passwordHashForAuth(password)),
			MTP_account_passwordInputSettings(
				MTP_flags(Flag::f_new_secure_salt
					| Flag::f_new_secure_secret
					| Flag::f_new_secure_secret_id),
				MTPbytes(), // new_salt
				MTPbytes(), // new_password_hash
				MTPstring(), // hint
				MTPstring(), // email
			MTP_bytes(QByteArray()), // new_secure_salt
			MTP_bytes(QByteArray()), // new_secure_secret
			MTP_long(0)) // new_secure_secret_id
		)).done([=](const MTPBool &result) {
			_saveSecretRequestId = 0;
			generateSecret(password);
		}).fail([=](const RPCError &error) {
			_saveSecretRequestId = 0;
			formFail(error.type());
		}).send();
		_secretReady.fire({});
	});
}

void FormController::decryptValues() {
	Expects(!_secret.empty());

	for (auto &[type, value] : _form.values) {
		decryptValue(value);
	}
	fillErrors();
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
	const auto scan = [&](Value &value, bytes::const_span hash) -> File* {
		const auto i = ranges::find_if(value.scans, [&](const File &scan) {
			return !bytes::compare(hash, scan.hash);
		});
		if (i != end(value.scans)) {
			return &*i;
		}
		LOG(("API Error: File not found for error value."));
		return nullptr;
	};
	const auto setSpecialScanError = [&](SpecialFile type, auto &&data) {
		if (const auto value = find(data.vtype)) {
			const auto i = value->specialScans.find(type);
			if (i != value->specialScans.end()) {
				i->second.error = qs(data.vtext);
			} else {
				LOG(("API Error: "
					"Special scan %1 not found for error value."
					).arg(int(type)));
			}
		}
	};
	for (const auto &error : _form.pendingErrors) {
		switch (error.type()) {
		case mtpc_secureValueErrorData: {
			const auto &data = error.c_secureValueErrorData();
			if (const auto value = find(data.vtype)) {
				const auto key = qs(data.vfield);
				value->data.parsed.fields[key].error = qs(data.vtext);
			}
		} break;
		case mtpc_secureValueErrorFile: {
			const auto &data = error.c_secureValueErrorFile();
			const auto hash = bytes::make_span(data.vfile_hash.v);
			if (const auto value = find(data.vtype)) {
				if (const auto file = scan(*value, hash)) {
					file->error = qs(data.vtext);
				}
			}
		} break;
		case mtpc_secureValueErrorFiles: {
			const auto &data = error.c_secureValueErrorFiles();
			if (const auto value = find(data.vtype)) {
				value->scanMissingError = qs(data.vtext);
			}
		} break;
		case mtpc_secureValueErrorFrontSide: {
			const auto &data = error.c_secureValueErrorFrontSide();
			setSpecialScanError(SpecialFile::FrontSide, data);
		} break;
		case mtpc_secureValueErrorReverseSide: {
			const auto &data = error.c_secureValueErrorReverseSide();
			setSpecialScanError(SpecialFile::ReverseSide, data);
		} break;
		case mtpc_secureValueErrorSelfie: {
			const auto &data = error.c_secureValueErrorSelfie();
			setSpecialScanError(SpecialFile::Selfie, data);
		} break;
		default: Unexpected("Error type in FormController::fillErrors.");
		}
	}
}

void FormController::decryptValue(Value &value) {
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
		for (const auto [key, text] : fields) {
			value.data.parsed.fields[key] = { text };
		}
	}
}

bool FormController::validateValueSecrets(Value &value) {
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
	for (auto &scan : value.scans) {
		if (!validateFileSecret(scan)) {
			return false;
		}
	}
	for (auto &[type, file] : value.specialScans) {
		if (!validateFileSecret(file)) {
			return false;
		}
	}
	return true;
}

void FormController::resetValue(Value &value) {
	value = Value(value.type);
}

rpl::producer<QString> FormController::passwordError() const {
	return _passwordError.events();
}

const PasswordSettings &FormController::passwordSettings() const {
	return _password;
}

void FormController::uploadScan(
		not_null<const Value*> value,
		QByteArray &&content) {
	if (!canAddScan(value)) {
		_view->showToast(lang(lng_passport_scans_limit_reached));
		return;
	}
	const auto nonconst = findValue(value);
	auto scanIndex = int(nonconst->scansInEdit.size());
	nonconst->scansInEdit.emplace_back(
		nonconst,
		File(),
		nullptr);
	auto &scan = nonconst->scansInEdit.back();
	encryptFile(scan, std::move(content), [=](UploadScanData &&result) {
		Expects(scanIndex >= 0 && scanIndex < nonconst->scansInEdit.size());

		uploadEncryptedFile(
			nonconst->scansInEdit[scanIndex],
			std::move(result));
	});
}

void FormController::deleteScan(
		not_null<const Value*> value,
		int scanIndex) {
	scanDeleteRestore(value, scanIndex, true);
}

void FormController::restoreScan(
		not_null<const Value*> value,
		int scanIndex) {
	scanDeleteRestore(value, scanIndex, false);
}

void FormController::uploadSpecialScan(
		not_null<const Value*> value,
		SpecialFile type,
		QByteArray &&content) {
	const auto nonconst = findValue(value);
	auto scanInEdit = EditFile{ nonconst, File(), nullptr };
	auto i = nonconst->specialScansInEdit.find(type);
	if (i != nonconst->specialScansInEdit.end()) {
		i->second = std::move(scanInEdit);
	} else {
		i = nonconst->specialScansInEdit.emplace(
			type,
			std::move(scanInEdit)).first;
	}
	auto &file = i->second;
	encryptFile(file, std::move(content), [=](UploadScanData &&result) {
		const auto i = nonconst->specialScansInEdit.find(type);
		Assert(i != nonconst->specialScansInEdit.end());
		uploadEncryptedFile(
			i->second,
			std::move(result));
	});
}

void FormController::deleteSpecialScan(
		not_null<const Value*> value,
		SpecialFile type) {
	specialScanDeleteRestore(value, type, true);
}

void FormController::restoreSpecialScan(
		not_null<const Value*> value,
		SpecialFile type) {
	specialScanDeleteRestore(value, type, false);
}

void FormController::prepareFile(
		EditFile &file,
		const QByteArray &content) {
	const auto fileId = rand_value<uint64>();
	file.fields.size = content.size();
	file.fields.id = fileId;
	file.fields.dcId = MTP::maindc();
	file.fields.secret = GenerateSecretBytes();
	file.fields.date = unixtime();
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
		int scanIndex,
		bool deleted) {
	Expects(scanIndex >= 0 && scanIndex < value->scansInEdit.size());

	const auto nonconst = findValue(value);
	auto &scan = nonconst->scansInEdit[scanIndex];
	if (scan.deleted && !deleted) {
		if (!canAddScan(value)) {
			_view->showToast(lang(lng_passport_scans_limit_reached));
			return;
		}
	}
	scan.deleted = deleted;
	_scanUpdated.fire(&scan);
}

void FormController::specialScanDeleteRestore(
		not_null<const Value*> value,
		SpecialFile type,
		bool deleted) {
	const auto nonconst = findValue(value);
	const auto i = nonconst->specialScansInEdit.find(type);
	Assert(i != nonconst->specialScansInEdit.end());
	auto &scan = i->second;
	scan.deleted = deleted;
	_scanUpdated.fire(&scan);
}

bool FormController::canAddScan(not_null<const Value*> value) const {
	const auto scansCount = ranges::count_if(
		value->scansInEdit,
		[](const EditFile &scan) { return !scan.deleted; });
	return (scansCount < kDocumentScansLimit);
}

void FormController::subscribeToUploader() {
	if (_uploaderSubscriptions) {
		return;
	}

	using namespace Storage;

	Auth().uploader().secureReady(
	) | rpl::start_with_next([=](const UploadSecureDone &data) {
		scanUploadDone(data);
	}, _uploaderSubscriptions);

	Auth().uploader().secureProgress(
	) | rpl::start_with_next([=](const UploadSecureProgress &data) {
		scanUploadProgress(data);
	}, _uploaderSubscriptions);

	Auth().uploader().secureFailed(
	) | rpl::start_with_next([=](const FullMsgId &fullId) {
		scanUploadFail(fullId);
	}, _uploaderSubscriptions);
}

void FormController::uploadEncryptedFile(
		EditFile &file,
		UploadScanData &&data) {
	subscribeToUploader();

	file.uploadData = std::make_unique<UploadScanData>(std::move(data));

	auto prepared = std::make_shared<FileLoadResult>(
		TaskId(),
		file.uploadData->fileId,
		FileLoadTo(PeerId(0), false, MsgId(0)),
		TextWithTags(),
		std::shared_ptr<SendingAlbum>(nullptr));
	prepared->type = SendMediaType::Secure;
	prepared->content = QByteArray::fromRawData(
		reinterpret_cast<char*>(file.uploadData->bytes.data()),
		file.uploadData->bytes.size());
	prepared->setFileData(prepared->content);
	prepared->filemd5 = file.uploadData->md5checksum;

	file.uploadData->fullId = FullMsgId(0, clientMsgId());
	Auth().uploader().upload(file.uploadData->fullId, std::move(prepared));
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
	if (const auto self = App::self()) {
		return self->phone();
	}
	return QString();
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
		verificationError(nonconst, lang(lng_signin_wrong_code));
		return;
	} else if (prepared.isEmpty()) {
		verificationError(nonconst, lang(lng_signin_wrong_code));
		return;
	}
	nonconst->verification.requestId = [&] {
		switch (nonconst->type) {
		case Value::Type::Phone:
			return request(MTPaccount_VerifyPhone(
				MTP_string(getPhoneFromValue(nonconst)),
				MTP_string(nonconst->verification.phoneCodeHash),
				MTP_string(prepared)
			)).done([=](const MTPBool &result) {
				savePlainTextValue(nonconst);
				clearValueVerification(nonconst);
			}).fail([=](const RPCError &error) {
				nonconst->verification.requestId = 0;
				if (error.type() == qstr("PHONE_CODE_INVALID")) {
					verificationError(
						nonconst,
						lang(lng_signin_wrong_code));
				} else {
					verificationError(nonconst, error.type());
				}
			}).send();
		case Value::Type::Email:
			return request(MTPaccount_VerifyEmail(
				MTP_string(getEmailFromValue(nonconst)),
				MTP_string(prepared)
			)).done([=](const MTPBool &result) {
				savePlainTextValue(nonconst);
				clearValueVerification(nonconst);
			}).fail([=](const RPCError &error) {
				nonconst->verification.requestId = 0;
				if (error.type() == qstr("CODE_INVALID")) {
					verificationError(
						nonconst,
						lang(lng_signin_wrong_code));
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
	if (savingValue(nonconst)) {
		return;
	}
	for (auto &scan : nonconst->scans) {
		loadFile(scan);
	}
	for (auto &[type, scan] : nonconst->specialScans) {
		const auto selfieRequired = _form.identitySelfieRequired;
		if (nonconst->requiresSpecialScan(type, selfieRequired)) {
			loadFile(scan);
		}
	}
	nonconst->scansInEdit = ranges::view::all(
		nonconst->scans
	) | ranges::view::transform([=](const File &file) {
		return EditFile(nonconst, file, nullptr);
	}) | ranges::to_vector;

	nonconst->specialScansInEdit.clear();
	for (const auto &[type, scan] : nonconst->specialScans) {
		nonconst->specialScansInEdit.emplace(type, EditFile(
			nonconst,
			scan,
			nullptr));
	}

	nonconst->data.parsedInEdit = nonconst->data.parsed;
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
			file.dcId,
			file.id,
			file.accessHash,
			0,
			SecureFileLocation,
			QString(),
			file.size,
			LoadToCacheAsWell,
			LoadFromCloudOrLocal,
			false));
	const auto loader = j->second.get();
	loader->connect(loader, &mtpFileLoader::progress, [=] {
		if (loader->finished()) {
			fileLoadDone(key, loader->bytes());
		} else {
			fileLoadProgress(key, loader->currentOffset());
		}
	});
	loader->connect(loader, &mtpFileLoader::failed, [=] {
		fileLoadFail(key);
	});
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

bool FormController::savingValue(not_null<const Value*> value) const {
	return (value->saveRequestId != 0)
		|| (value->verification.requestId != 0)
		|| (value->verification.codeLength != 0)
		|| uploadingScan(value);
}

bool FormController::uploadingScan(not_null<const Value*> value) const {
	const auto uploading = [](const EditFile &file) {
		return file.uploadData
			&& file.uploadData->fullId
			&& !file.deleted;
	};
	if (ranges::find_if(value->scansInEdit, uploading)
		!= end(value->scansInEdit)) {
		return true;
	}
	if (ranges::find_if(value->specialScansInEdit, [&](const auto &pair) {
		return uploading(pair.second);
	}) != end(value->specialScansInEdit)) {
		return true;
	}
	for (const auto &scan : value->scansInEdit) {
		if (uploading(scan)) {
			return true;
		}
	}
	for (const auto &[type, scan] : value->specialScansInEdit) {
		if (uploading(scan)) {
			return true;
		}
	}
	return false;
}

void FormController::cancelValueEdit(not_null<const Value*> value) {
	Expects(value->editScreens > 0);

	const auto nonconst = findValue(value);
	--nonconst->editScreens;
	clearValueEdit(nonconst);
}

void FormController::valueEditFailed(not_null<Value*> value) {
	Expects(!savingValue(value));

	if (value->editScreens == 0) {
		clearValueEdit(value);
	}
}

void FormController::clearValueEdit(not_null<Value*> value) {
	if (savingValue(value)) {
		return;
	}
	value->scansInEdit.clear();
	value->specialScansInEdit.clear();
	value->data.encryptedSecretInEdit.clear();
	value->data.hashInEdit.clear();
	value->data.parsedInEdit = ValueMap();
}

void FormController::cancelValueVerification(not_null<const Value*> value) {
	const auto nonconst = findValue(value);
	clearValueVerification(nonconst);
	if (!savingValue(nonconst)) {
		valueEditFailed(nonconst);
	}
}

void FormController::clearValueVerification(not_null<Value*> value) {
	const auto was = (value->verification.codeLength != 0);
	if (const auto requestId = base::take(value->verification.requestId)) {
		request(requestId).cancel();
	}
	value->verification = Verification();
	if (was) {
		_verificationUpdate.fire_copy(value);
	}
}

bool FormController::isEncryptedValue(Value::Type type) const {
	return (type != Value::Type::Phone && type != Value::Type::Email);
}

bool FormController::editFileChanged(const EditFile &file) const {
	if (file.uploadData) {
		return !file.deleted;
	}
	return file.deleted;
}

bool FormController::editValueChanged(
		not_null<const Value*> value,
		const ValueMap &data) const {
	auto filesCount = 0;
	for (const auto &scan : value->scansInEdit) {
		if (editFileChanged(scan)) {
			return true;
		}
	}
	for (const auto &[type, scan] : value->specialScansInEdit) {
		if (editFileChanged(scan)) {
			return true;
		}
	}
	auto existing = value->data.parsed.fields;
	for (const auto &[key, value] : data.fields) {
		const auto i = existing.find(key);
		if (i != existing.end()) {
			if (i->second.text != value.text) {
				return true;
			}
			existing.erase(i);
		} else if (!value.text.isEmpty()) {
			return true;
		}
	}
	return !existing.empty();
}

void FormController::saveValueEdit(
		not_null<const Value*> value,
		ValueMap &&data) {
	if (savingValue(value) || _submitRequestId) {
		return;
	}

	const auto nonconst = findValue(value);
	if (!editValueChanged(nonconst, data)) {
		nonconst->saveRequestId = -1;
		crl::on_main(this, [=] {
			base::take(nonconst->scansInEdit);
			base::take(nonconst->specialScansInEdit);
			base::take(nonconst->data.encryptedSecretInEdit);
			base::take(nonconst->data.hashInEdit);
			base::take(nonconst->data.parsedInEdit);
			nonconst->saveRequestId = 0;
			_valueSaveFinished.fire_copy(nonconst);
		});
		return;
	}
	nonconst->data.parsedInEdit = std::move(data);

	if (isEncryptedValue(nonconst->type)) {
		saveEncryptedValue(nonconst);
	} else {
		savePlainTextValue(nonconst);
	}
}

void FormController::deleteValueEdit(not_null<const Value*> value) {
	if (savingValue(value) || _submitRequestId) {
		return;
	}

	const auto nonconst = findValue(value);
	nonconst->saveRequestId = request(MTPaccount_DeleteSecureValue(
		MTP_vector<MTPSecureValueType>(1, ConvertType(nonconst->type))
	)).done([=](const MTPBool &result) {
		const auto editScreens = value->editScreens;
		*nonconst = Value(nonconst->type);
		nonconst->editScreens = editScreens;

		_valueSaveFinished.fire_copy(value);
	}).fail([=](const RPCError &error) {
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

	const auto inputFile = [](const EditFile &file) {
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

	auto inputFiles = QVector<MTPInputSecureFile>();
	inputFiles.reserve(value->scansInEdit.size());
	for (const auto &scan : value->scansInEdit) {
		if (scan.deleted) {
			continue;
		}
		inputFiles.push_back(inputFile(scan));
	}

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

	const auto hasSpecialFile = [&](SpecialFile type) {
		const auto i = value->specialScansInEdit.find(type);
		return (i != end(value->specialScansInEdit) && !i->second.deleted);
	};
	const auto specialFile = [&](SpecialFile type) {
		const auto i = value->specialScansInEdit.find(type);
		return (i != end(value->specialScansInEdit) && !i->second.deleted)
			? inputFile(i->second)
			: MTPInputSecureFile();
	};
	const auto frontSide = specialFile(SpecialFile::FrontSide);
	const auto reverseSide = specialFile(SpecialFile::ReverseSide);
	const auto selfie = specialFile(SpecialFile::Selfie);

	const auto type = ConvertType(value->type);
	const auto flags = (value->data.parsedInEdit.fields.empty()
			? MTPDinputSecureValue::Flag(0)
			: MTPDinputSecureValue::Flag::f_data)
		| (hasSpecialFile(SpecialFile::FrontSide)
			? MTPDinputSecureValue::Flag::f_front_side
			: MTPDinputSecureValue::Flag(0))
		| (hasSpecialFile(SpecialFile::ReverseSide)
			? MTPDinputSecureValue::Flag::f_reverse_side
			: MTPDinputSecureValue::Flag(0))
		| (hasSpecialFile(SpecialFile::Selfie)
			? MTPDinputSecureValue::Flag::f_selfie
			: MTPDinputSecureValue::Flag(0))
		| (value->scansInEdit.empty()
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
		MTP_vector<MTPInputSecureFile>(inputFiles),
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
		plain(MTP_string(text))));
}

void FormController::sendSaveRequest(
		not_null<Value*> value,
		const MTPInputSecureValue &data) {
	Expects(value->saveRequestId == 0);

	value->saveRequestId = request(MTPaccount_SaveSecureValue(
		data,
		MTP_long(_secretId)
	)).done([=](const MTPSecureValue &result) {
		auto scansInEdit = base::take(value->scansInEdit);
		for (auto &[type, scan] : base::take(value->specialScansInEdit)) {
			scansInEdit.push_back(std::move(scan));
		}

		const auto editScreens = value->editScreens;
		*value = parseValue(result, scansInEdit);
		decryptValue(*value);
		value->editScreens = editScreens;

		_valueSaveFinished.fire_copy(value);
	}).fail([=](const RPCError &error) {
		value->saveRequestId = 0;
		const auto code = error.type();
		if (code == qstr("PHONE_VERIFICATION_NEEDED")) {
			if (value->type == Value::Type::Phone) {
				startPhoneVerification(value);
				return;
			}
		} else if (code == qstr("PHONE_NUMBER_INVALID")) {
			if (value->type == Value::Type::Phone) {
				value->data.parsedInEdit.fields["value"].error
					= lang(lng_bad_phone);
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
					= lang(lng_cloud_password_bad_email);
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
	value->verification.requestId = request(MTPaccount_SendVerifyPhoneCode(
		MTP_flags(MTPaccount_SendVerifyPhoneCode::Flag(0)),
		MTP_string(getPhoneFromValue(value)),
		MTPBool()
	)).done([=](const MTPauth_SentCode &result) {
		Expects(result.type() == mtpc_auth_sentCode);

		value->verification.requestId = 0;

		const auto &data = result.c_auth_sentCode();
		value->verification.phoneCodeHash = qs(data.vphone_code_hash);
		switch (data.vtype.type()) {
		case mtpc_auth_sentCodeTypeApp:
			LOG(("API Error: sentCodeTypeApp not expected "
				"in FormController::startPhoneVerification."));
			return;
		case mtpc_auth_sentCodeTypeFlashCall:
			LOG(("API Error: sentCodeTypeFlashCall not expected "
				"in FormController::startPhoneVerification."));
			return;
		case mtpc_auth_sentCodeTypeCall: {
			const auto &type = data.vtype.c_auth_sentCodeTypeCall();
			value->verification.codeLength = (type.vlength.v > 0)
				? type.vlength.v
				: -1;
			value->verification.call = std::make_unique<SentCodeCall>(
				[=] { requestPhoneCall(value); },
				[=] { _verificationUpdate.fire_copy(value); });
			value->verification.call->setStatus(
				{ SentCodeCall::State::Called, 0 });
			if (data.has_next_type()) {
				LOG(("API Error: next_type is not supported for calls."));
			}
		} break;
		case mtpc_auth_sentCodeTypeSms: {
			const auto &type = data.vtype.c_auth_sentCodeTypeSms();
			value->verification.codeLength = (type.vlength.v > 0)
				? type.vlength.v
				: -1;
			const auto &next = data.vnext_type;
			if (data.has_next_type()
				&& next.type() == mtpc_auth_codeTypeCall) {
				value->verification.call = std::make_unique<SentCodeCall>(
					[=] { requestPhoneCall(value); },
					[=] { _verificationUpdate.fire_copy(value); });
				value->verification.call->setStatus({
					SentCodeCall::State::Waiting,
					data.has_timeout() ? data.vtimeout.v : 60 });
			}
		} break;
		}
		_verificationNeeded.fire_copy(value);
	}).fail([=](const RPCError &error) {
		value->verification.requestId = 0;
		valueSaveShowError(value, error);
	}).send();
}

void FormController::startEmailVerification(not_null<Value*> value) {
	value->verification.requestId = request(MTPaccount_SendVerifyEmailCode(
		MTP_string(getEmailFromValue(value))
	)).done([=](const MTPaccount_SentEmailCode &result) {
		Expects(result.type() == mtpc_account_sentEmailCode);

		value->verification.requestId = 0;
		const auto &data = result.c_account_sentEmailCode();
		value->verification.codeLength = (data.vlength.v > 0)
			? data.vlength.v
			: -1;
		_verificationNeeded.fire_copy(value);
	}).fail([=](const RPCError &error) {
		valueSaveShowError(value, error);
	}).send();
}


void FormController::requestPhoneCall(not_null<Value*> value) {
	Expects(value->verification.call != nullptr);

	value->verification.call->setStatus(
		{ SentCodeCall::State::Calling, 0 });
	request(MTPauth_ResendCode(
		MTP_string(getPhoneFromValue(value)),
		MTP_string(value->verification.phoneCodeHash)
	)).done([=](const MTPauth_SentCode &code) {
		value->verification.call->callDone();
	}).send();
}

void FormController::valueSaveShowError(
		not_null<Value*> value,
		const RPCError &error) {
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

	auto randomSaltPart = bytes::vector(8);
	bytes::set_random(randomSaltPart);
	auto newSecureSaltFull = bytes::concatenate(
		_password.newSecureSalt,
		randomSaltPart);

	auto saved = SavedCredentials();
	saved.hashForAuth = passwordHashForAuth(password);
	saved.hashForSecret = CountPasswordHashForSecret(
		newSecureSaltFull,
		password);
	saved.secretId = CountSecureSecretId(secret);

	auto encryptedSecret = EncryptSecureSecret(
		secret,
		saved.hashForSecret);

	using Flag = MTPDaccount_passwordInputSettings::Flag;
	_saveSecretRequestId = request(MTPaccount_UpdatePasswordSettings(
		MTP_bytes(saved.hashForAuth),
		MTP_account_passwordInputSettings(
			MTP_flags(Flag::f_new_secure_salt
				| Flag::f_new_secure_secret
				| Flag::f_new_secure_secret_id),
			MTPbytes(), // new_salt
			MTPbytes(), // new_password_hash
			MTPstring(), // hint
			MTPstring(), // email
			MTP_bytes(newSecureSaltFull),
			MTP_bytes(encryptedSecret),
			MTP_long(saved.secretId))
	)).done([=](const MTPBool &result) {
		Auth().data().rememberPassportCredentials(
			std::move(saved),
			kRememberCredentialsDelay);

		_saveSecretRequestId = 0;
		_secret = secret;
		_secretId = saved.secretId;
		//_password.salt = newPasswordSaltFull;
		for (const auto &callback : base::take(_secretCallbacks)) {
			callback();
		}
	}).fail([=](const RPCError &error) {
		_saveSecretRequestId = 0;
		suggestRestart();
	}).send();
}

void FormController::suggestRestart() {
	_suggestingRestart = true;
	_view->show(Box<ConfirmBox>(
		lang(lng_passport_restart_sure),
		lang(lng_passport_restart),
		[=] { _controller->showPassportForm(_request); },
		[=] { cancel(); }));
}

void FormController::requestForm() {
	if (_request.payload.isEmpty()) {
		_formRequestId = -1;
		formFail("PAYLOAD_EMPTY");
		return;
	}
	_formRequestId = request(MTPaccount_GetAuthorizationForm(
		MTP_int(_request.botId),
		MTP_string(_request.scope),
		MTP_string(_request.publicKey)
	)).done([=](const MTPaccount_AuthorizationForm &result) {
		_formRequestId = 0;
		formDone(result);
	}).fail([=](const RPCError &error) {
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
-> base::optional<File> {
	switch (data.type()) {
	case mtpc_secureFileEmpty:
		return base::none;

	case mtpc_secureFile: {
		const auto &fields = data.c_secureFile();
		auto result = File();
		result.id = fields.vid.v;
		result.accessHash = fields.vaccess_hash.v;
		result.size = fields.vsize.v;
		result.date = fields.vdate.v;
		result.dcId = fields.vdc_id.v;
		result.hash = bytes::make_vector(fields.vfile_hash.v);
		result.encryptedSecret = bytes::make_vector(fields.vsecret.v);
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
	Local::writeImage(
		StorageKey(
			storageMix32To64(
				SecureFileLocation,
				destination.dcId),
			destination.id),
		StorageImageSaved(QByteArray::fromRawData(
			reinterpret_cast<const char*>(
				i->uploadData->bytes.data()),
			i->uploadData->bytes.size())));
}

auto FormController::parseValue(
		const MTPSecureValue &value,
		const std::vector<EditFile> &editData) const -> Value {
	Expects(value.type() == mtpc_secureValue);

	const auto &data = value.c_secureValue();
	const auto type = ConvertType(data.vtype);
	auto result = Value(type);
	result.submitHash = bytes::make_vector(data.vhash.v);
	if (data.has_data()) {
		Assert(data.vdata.type() == mtpc_secureData);
		const auto &fields = data.vdata.c_secureData();
		result.data.original = fields.vdata.v;
		result.data.hash = bytes::make_vector(fields.vdata_hash.v);
		result.data.encryptedSecret = bytes::make_vector(fields.vsecret.v);
	}
	if (data.has_files()) {
		result.scans = parseFiles(data.vfiles.v, editData);
	}
	const auto parseSpecialScan = [&](
			SpecialFile type,
			const MTPSecureFile &file) {
		if (auto parsed = parseFile(file, editData)) {
			result.specialScans.emplace(type, std::move(*parsed));
		}
	};
	if (data.has_front_side()) {
		parseSpecialScan(SpecialFile::FrontSide, data.vfront_side);
	}
	if (data.has_reverse_side()) {
		parseSpecialScan(SpecialFile::ReverseSide, data.vreverse_side);
	}
	if (data.has_selfie()) {
		parseSpecialScan(SpecialFile::Selfie, data.vselfie);
	}
	if (data.has_plain_data()) {
		switch (data.vplain_data.type()) {
		case mtpc_securePlainPhone: {
			const auto &fields = data.vplain_data.c_securePlainPhone();
			result.data.parsed.fields["value"].text = qs(fields.vphone);
		} break;
		case mtpc_securePlainEmail: {
			const auto &fields = data.vplain_data.c_securePlainEmail();
			result.data.parsed.fields["value"].text = qs(fields.vemail);
		} break;
		}
	}
	return result;
}

auto FormController::findEditFile(const FullMsgId &fullId) -> EditFile* {
	const auto found = [&](const EditFile &file) {
		return (file.uploadData && file.uploadData->fullId == fullId);
	};
	for (auto &[type, value] : _form.values) {
		for (auto &scan : value.scansInEdit) {
			if (found(scan)) {
				return &scan;
			}
		}
		for (auto &[special, scan] : value.specialScansInEdit) {
			if (found(scan)) {
				return &scan;
			}
		}
	}
	return nullptr;
}

auto FormController::findEditFile(const FileKey &key) -> EditFile* {
	const auto found = [&](const EditFile &file) {
		return (file.fields.dcId == key.dcId && file.fields.id == key.id);
	};
	for (auto &[type, value] : _form.values) {
		for (auto &scan : value.scansInEdit) {
			if (found(scan)) {
				return &scan;
			}
		}
		for (auto &[special, scan] : value.specialScansInEdit) {
			if (found(scan)) {
				return &scan;
			}
		}
	}
	return nullptr;
}

auto FormController::findFile(const FileKey &key)
-> std::pair<Value*, File*> {
	const auto found = [&](const File &file) {
		return (file.dcId == key.dcId) && (file.id == key.id);
	};
	for (auto &[type, value] : _form.values) {
		for (auto &scan : value.scans) {
			if (found(scan)) {
				return { &value, &scan };
			}
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
	parseForm(result);
	if (!_passwordRequestId) {
		showForm();
	}
}

void FormController::parseForm(const MTPaccount_AuthorizationForm &result) {
	Expects(result.type() == mtpc_account_authorizationForm);

	const auto &data = result.c_account_authorizationForm();

	App::feedUsers(data.vusers);

	for (const auto &value : data.vvalues.v) {
		auto parsed = parseValue(value);
		const auto type = parsed.type;
		const auto alreadyIt = _form.values.find(type);
		if (alreadyIt != _form.values.end()) {
			LOG(("API Error: Two values for type %1 in authorization form"
				"%1").arg(int(type)));
			continue;
		}
		_form.values.emplace(type, std::move(parsed));
	}
	_form.identitySelfieRequired = data.is_selfie_required();
	if (data.has_privacy_policy_url()) {
		_form.privacyPolicyUrl = qs(data.vprivacy_policy_url);
	}
	for (const auto &required : data.vrequired_types.v) {
		const auto type = ConvertType(required);
		_form.request.push_back(type);
		_form.values.emplace(type, Value(type));
	}
	_bot = App::userLoaded(_request.botId);
	_form.pendingErrors = data.verrors.v;
}

void FormController::formFail(const QString &error) {
	_savedPasswordValue = QByteArray();
	_serviceErrorText = error;
	if (error == "APP_VERSION_OUTDATED") {
		_view->showUpdateAppBox();
	} else {
		_view->showCriticalError(
			lang(lng_passport_form_error) + "\n" + error);
	}
}

void FormController::requestPassword() {
	if (_passwordRequestId) {
		return;
	}
	_passwordRequestId = request(MTPaccount_GetPassword(
	)).done([=](const MTPaccount_Password &result) {
		_passwordRequestId = 0;
		passwordDone(result);
	}).fail([=](const RPCError &error) {
		formFail(error.type());
	}).send();
}

void FormController::passwordDone(const MTPaccount_Password &result) {
	const auto changed = [&] {
		switch (result.type()) {
		case mtpc_account_noPassword:
			return applyPassword(result.c_account_noPassword());
		case mtpc_account_password:
			return applyPassword(result.c_account_password());
		}
		Unexpected("Type in FormController::passwordDone.");
	}();
	if (changed && !_formRequestId) {
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
	if (!_bot) {
		formFail(Lang::Hard::NoAuthorizationBot());
		return;
	}
	if (!_password.salt.empty()) {
		if (!_savedPasswordValue.isEmpty()) {
			submitPassword(base::duplicate(_savedPasswordValue));
		} else if (const auto saved = Auth().data().passportCredentials()) {
			checkSavedPasswordSettings(*saved);
		} else {
			_view->showAskPassword();
		}
	} else {
		_view->showNoPassword();
	}
}

bool FormController::applyPassword(const MTPDaccount_noPassword &result) {
	auto settings = PasswordSettings();
	settings.unconfirmedPattern = qs(result.vemail_unconfirmed_pattern);
	settings.newSalt = bytes::make_vector(result.vnew_salt.v);
	settings.newSecureSalt = bytes::make_vector(result.vnew_secure_salt.v);
	openssl::AddRandomSeed(bytes::make_span(result.vsecure_random.v));
	return applyPassword(std::move(settings));
}

bool FormController::applyPassword(const MTPDaccount_password &result) {
	auto settings = PasswordSettings();
	settings.hint = qs(result.vhint);
	settings.hasRecovery = result.is_has_recovery();
	settings.notEmptyPassport = result.is_has_secure_values();
	settings.salt = bytes::make_vector(result.vcurrent_salt.v);
	settings.unconfirmedPattern = qs(result.vemail_unconfirmed_pattern);
	settings.newSalt = bytes::make_vector(result.vnew_salt.v);
	settings.newSecureSalt = bytes::make_vector(result.vnew_secure_salt.v);
	openssl::AddRandomSeed(bytes::make_span(result.vsecure_random.v));
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
			lang(lng_passport_stop_sure),
			lang(lng_passport_stop),
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
		App::CallDelayed(timeout, this, [=] {
			_controller->clearPassportForm();
		});
	}
}

rpl::lifetime &FormController::lifetime() {
	return _lifetime;
}

FormController::~FormController() = default;

} // namespace Passport
