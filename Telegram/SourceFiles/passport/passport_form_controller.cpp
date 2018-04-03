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
#include "lang/lang_keys.h"
#include "base/openssl_help.h"
#include "mainwindow.h"
#include "window/window_controller.h"
#include "auth_session.h"
#include "storage/localimageloader.h"
#include "storage/localstorage.h"
#include "storage/file_upload.h"
#include "storage/file_download.h"

namespace Passport {
namespace {

QImage ReadImage(bytes::const_span buffer) {
	return App::readImage(QByteArray::fromRawData(
		reinterpret_cast<const char*>(buffer.data()),
		buffer.size()));
}

Value::Type ConvertType(const MTPSecureValueType &type) {
	using Type = Value::Type;
	switch (type.type()) {
	case mtpc_secureValueTypePersonalDetails: return Type::PersonalDetails;
	case mtpc_secureValueTypePassport: return Type::Passport;
	case mtpc_secureValueTypeDriverLicense: return Type::DriverLicense;
	case mtpc_secureValueTypeIdentityCard: return Type::IdentityCard;
	case mtpc_secureValueTypeAddress: return Type::Address;
	case mtpc_secureValueTypeUtilityBill: return Type::UtilityBill;
	case mtpc_secureValueTypeBankStatement: return Type::BankStatement;
	case mtpc_secureValueTypeRentalAgreement: return Type::RentalAgreement;
	case mtpc_secureValueTypePhone: return Type::Phone;
	case mtpc_secureValueTypeEmail: return Type::Email;
	}
	Unexpected("Type in secureValueType type.");
};

} // namespace

FormRequest::FormRequest(
	UserId botId,
	const QString &scope,
	const QString &callbackUrl,
	const QString &publicKey)
: botId(botId)
, scope(scope)
, callbackUrl(callbackUrl)
, publicKey(publicKey) {
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

FormController::FormController(
	not_null<Window::Controller*> controller,
	const FormRequest &request)
: _controller(controller)
, _request(request)
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

void FormController::submitPassword(const QString &password) {
	Expects(!_password.salt.empty());

	if (_passwordCheckRequestId) {
		return;
	} else if (password.isEmpty()) {
		_passwordError.fire(QString());
	}
	const auto passwordBytes = password.toUtf8();
	_passwordCheckRequestId = request(MTPaccount_GetPasswordSettings(
		MTP_bytes(passwordHashForAuth(bytes::make_span(passwordBytes)))
	)).handleFloodErrors(
	).done([=](const MTPaccount_PasswordSettings &result) {
		Expects(result.type() == mtpc_account_passwordSettings);

		_passwordCheckRequestId = 0;
		const auto &data = result.c_account_passwordSettings();
		_password.confirmedEmail = qs(data.vemail);
		validateSecureSecret(
			bytes::make_span(data.vsecure_salt.v),
			bytes::make_span(data.vsecure_secret.v),
			bytes::make_span(passwordBytes));
	}).fail([=](const RPCError &error) {
		_passwordCheckRequestId = 0;
		if (MTP::isFloodError(error)) {
			_passwordError.fire(lang(lng_flood_error));
		} else if (error.type() == qstr("PASSWORD_HASH_INVALID")) {
			_passwordError.fire(lang(lng_passport_password_wrong));
		} else {
			_passwordError.fire_copy(error.type());
		}
	}).send();
}

void FormController::validateSecureSecret(
		bytes::const_span salt,
		bytes::const_span encryptedSecret,
		bytes::const_span password) {
	if (!salt.empty() && !encryptedSecret.empty()) {
		_secret = DecryptSecureSecret(salt, encryptedSecret, password);
		if (_secret.empty()) {
			_secretId = 0;
			LOG(("API Error: Failed to decrypt secure secret. "
				"Forgetting all files and data :("));
			for (auto &[type, value] : _form.values) {
				if (!value.data.original.isEmpty()) {
					resetValue(value);
				}
			}
		} else {
			_secretId = CountSecureSecretHash(_secret);
			decryptValues();
		}
	}
	if (_secret.empty()) {
		generateSecret(password);
	}
	_secretReady.fire({});
}

void FormController::decryptValues() {
	Expects(!_secret.empty());

	for (auto &[type, value] : _form.values) {
		if (value.data.original.isEmpty()) {
			continue;
		}
		decryptValue(value);
	}
}

void FormController::decryptValue(Value &value) {
	Expects(!_secret.empty());
	Expects(!value.data.original.isEmpty());

	if (!validateValueSecrets(value)) {
		resetValue(value);
		return;
	}
	value.data.parsed.fields = DeserializeData(DecryptData(
		bytes::make_span(value.data.original),
		value.data.hash,
		value.data.secret));
}

bool FormController::validateValueSecrets(Value &value) {
	value.data.secret = DecryptValueSecret(
		value.data.encryptedSecret,
		_secret,
		value.data.hash);
	if (value.data.secret.empty()) {
		LOG(("API Error: Could not decrypt data secret. "
			"Forgetting files and data :("));
		return false;
	}
	for (auto &file : value.files) {
		file.secret = DecryptValueSecret(
			file.encryptedSecret,
			_secret,
			file.hash);
		if (file.secret.empty()) {
			LOG(("API Error: Could not decrypt file secret. "
				"Forgetting files and data :("));
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

QString FormController::passwordHint() const {
	return _password.hint;
}

void FormController::uploadScan(
		not_null<const Value*> value,
		QByteArray &&content) {
	const auto nonconst = findValue(value);
	auto fileIndex = int(nonconst->filesInEdit.size());
	nonconst->filesInEdit.emplace_back(
		nonconst,
		File(),
		nullptr);
	const auto fileId = rand_value<uint64>();
	auto &file = nonconst->filesInEdit.back();
	file.fields.size = content.size();
	file.fields.id = fileId;
	file.fields.dcId = MTP::maindc();
	file.fields.secret = GenerateSecretBytes();
	file.fields.date = unixtime();
	file.fields.image = ReadImage(bytes::make_span(content));
	file.fields.downloadOffset = file.fields.size;

	_scanUpdated.fire(&file);

	encryptScan(nonconst, fileIndex, std::move(content));
}

void FormController::encryptScan(
		not_null<Value*> value,
		int fileIndex,
		QByteArray &&content) {
	Expects(fileIndex >= 0 && fileIndex < value->filesInEdit.size());

	const auto &file = value->filesInEdit[fileIndex];
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
				uploadEncryptedScan(
					value ,
					fileIndex,
					std::move(encrypted));
			}
		});
	});
}

void FormController::deleteScan(
		not_null<const Value*> value,
		int fileIndex) {
	scanDeleteRestore(value, fileIndex, true);
}

void FormController::restoreScan(
		not_null<const Value*> value,
		int fileIndex) {
	scanDeleteRestore(value, fileIndex, false);
}

void FormController::scanDeleteRestore(
		not_null<const Value*> value,
		int fileIndex,
		bool deleted) {
	Expects(fileIndex >= 0 && fileIndex < value->filesInEdit.size());

	const auto nonconst = findValue(value);
	auto &file = nonconst->filesInEdit[fileIndex];
	file.deleted = deleted;
	_scanUpdated.fire(&file);
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

void FormController::uploadEncryptedScan(
		not_null<Value*> value,
		int fileIndex,
		UploadScanData &&data) {
	Expects(fileIndex >= 0 && fileIndex < value->filesInEdit.size());

	subscribeToUploader();

	auto &file = value->filesInEdit[fileIndex];
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
	loadFiles(nonconst->files);
	nonconst->filesInEdit = ranges::view::all(
		value->files
	) | ranges::view::transform([=](const File &file) {
		return EditFile(value, file, nullptr);
	}) | ranges::to_vector;
}

void FormController::loadFiles(std::vector<File> &files) {
	for (auto &file : files) {
		if (!file.image.isNull()) {
			file.downloadOffset = file.size;
			continue;
		}

		const auto key = FileKey{ file.id, file.dcId };
		const auto i = _fileLoaders.find(key);
		if (i == _fileLoaders.end()) {
			file.downloadOffset = 0;
			const auto [i, ok] = _fileLoaders.emplace(
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
			const auto loader = i->second.get();
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
	}
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
	const auto nonconst = findValue(value);
	nonconst->filesInEdit.clear();
}

bool FormController::isEncryptedValue(Value::Type type) const {
	return (type != Value::Type::Phone && type != Value::Type::Email);
}

void FormController::saveValueEdit(
		not_null<const Value*> value,
		ValueMap &&data) {
	const auto nonconst = findValue(value);
	nonconst->data.parsed = std::move(data);

	if (isEncryptedValue(nonconst->type)) {
		saveEncryptedValue(nonconst);
	} else {
		savePlainTextValue(nonconst);
	}
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
	inputFiles.reserve(value->filesInEdit.size());
	for (const auto &file : value->filesInEdit) {
		if (file.deleted) {
			continue;
		}
		inputFiles.push_back(inputFile(file));
	}


	if (value->data.secret.empty()) {
		value->data.secret = GenerateSecretBytes();
	}
	const auto encryptedData = EncryptData(
		SerializeData(value->data.parsed.fields),
		value->data.secret);
	value->data.hash = encryptedData.hash;
	value->data.encryptedSecret = EncryptValueSecret(
		value->data.secret,
		_secret,
		value->data.hash);

	const auto selfie = value->selfieInEdit
		? inputFile(*value->selfieInEdit)
		: MTPInputSecureFile();

	const auto type = [&] {
		switch (value->type) {
		case Value::Type::PersonalDetails:
			return MTP_secureValueTypePersonalDetails();
		case Value::Type::Passport:
			return MTP_secureValueTypePassport();
		case Value::Type::DriverLicense:
			return MTP_secureValueTypeDriverLicense();
		case Value::Type::IdentityCard:
			return MTP_secureValueTypeIdentityCard();
		case Value::Type::Address:
			return MTP_secureValueTypeAddress();
		case Value::Type::UtilityBill:
			return MTP_secureValueTypeUtilityBill();
		case Value::Type::BankStatement:
			return MTP_secureValueTypeBankStatement();
		case Value::Type::RentalAgreement:
			return MTP_secureValueTypeRentalAgreement();
		}
		Unexpected("Value type in saveEncryptedValue().");
	}();
	const auto flags = ((value->filesInEdit.empty()
		&& value->data.parsed.fields.empty())
			? MTPDinputSecureValue::Flag(0)
			: MTPDinputSecureValue::Flag::f_data)
		| (value->filesInEdit.empty()
			? MTPDinputSecureValue::Flag(0)
			: MTPDinputSecureValue::Flag::f_files)
		| (value->selfieInEdit
			? MTPDinputSecureValue::Flag::f_selfie
			: MTPDinputSecureValue::Flag(0));
	if (!flags) {
		request(MTPaccount_DeleteSecureValue(MTP_vector<MTPSecureValueType>(1, type))).send();
	} else {
		sendSaveRequest(value, MTP_inputSecureValue(
			MTP_flags(flags),
			type,
			MTP_secureData(
				MTP_bytes(encryptedData.bytes),
				MTP_bytes(value->data.hash),
				MTP_bytes(value->data.encryptedSecret)),
			MTP_vector<MTPInputSecureFile>(inputFiles),
			MTPSecurePlainData(),
			selfie));
	}
}

void FormController::savePlainTextValue(not_null<Value*> value) {
	Expects(!isEncryptedValue(value->type));

	const auto text = value->data.parsed.fields[QString("value")];
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
		MTPVector<MTPInputSecureFile>(),
		plain(MTP_string(text)),
		MTPInputSecureFile()));
}

void FormController::sendSaveRequest(
		not_null<Value*> value,
		const MTPInputSecureValue &data) {
	request(MTPaccount_SaveSecureValue(
		data,
		MTP_long(_secretId)
	)).done([=](const MTPSecureValue &result) {
		Expects(result.type() == mtpc_secureValue);

		const auto &data = result.c_secureValue();
		value->files = parseFiles(
			data.vfiles.v,
			base::take(value->filesInEdit));

		Ui::show(Box<InformBox>("Saved"), LayerOption::KeepOther);
	}).fail([=](const RPCError &error) {
		Ui::show(Box<InformBox>("Error saving value"));
	}).send();
}

void FormController::generateSecret(bytes::const_span password) {
	if (_saveSecretRequestId) {
		return;
	}
	auto secret = GenerateSecretBytes();

	auto randomSaltPart = bytes::vector(8);
	bytes::set_random(randomSaltPart);
	auto newSecureSaltFull = bytes::concatenate(
		_password.newSecureSalt,
		randomSaltPart);

	auto secureSecretId = CountSecureSecretHash(secret);
	auto encryptedSecret = EncryptSecureSecret(
		newSecureSaltFull,
		secret,
		password);

	const auto hashForAuth = openssl::Sha256(bytes::concatenate(
		_password.salt,
		password,
		_password.salt));

	using Flag = MTPDaccount_passwordInputSettings::Flag;
	_saveSecretRequestId = request(MTPaccount_UpdatePasswordSettings(
		MTP_bytes(hashForAuth),
		MTP_account_passwordInputSettings(
			MTP_flags(Flag::f_new_secure_secret),
			MTPbytes(), // new_salt
			MTPbytes(), // new_password_hash
			MTPstring(), // hint
			MTPstring(), // email
			MTP_bytes(newSecureSaltFull),
			MTP_bytes(encryptedSecret),
			MTP_long(secureSecretId))
	)).done([=](const MTPBool &result) {
		_saveSecretRequestId = 0;
		_secret = secret;
		_secretId = secureSecretId;
		//_password.salt = newPasswordSaltFull;
		for (const auto &callback : base::take(_secretCallbacks)) {
			callback();
		}
	}).fail([=](const RPCError &error) {
		// #TODO wrong password hash error?
		Ui::show(Box<InformBox>("Saving encrypted value failed."));
		_saveSecretRequestId = 0;
	}).send();
}

void FormController::requestForm() {
	auto normalizedKey = _request.publicKey;
	normalizedKey.replace("\r\n", "\n");
	_formRequestId = request(MTPaccount_GetAuthorizationForm(
		MTP_int(_request.botId),
		MTP_string(_request.scope),
		MTP_bytes(normalizedKey.toUtf8())
	)).done([=](const MTPaccount_AuthorizationForm &result) {
		_formRequestId = 0;
		formDone(result);
	}).fail([=](const RPCError &error) {
		formFail(error);
	}).send();
}

auto FormController::parseFiles(
	const QVector<MTPSecureFile> &data,
	const std::vector<EditFile> &editData) const
-> std::vector<File> {
	auto result = std::vector<File>();
	result.reserve(data.size());

	auto index = 0;
	for (const auto &file : data) {
		switch (file.type()) {
		case mtpc_secureFileEmpty: {

		} break;
		case mtpc_secureFile: {
			const auto &fields = file.c_secureFile();
			auto normal = File();
			normal.id = fields.vid.v;
			normal.accessHash = fields.vaccess_hash.v;
			normal.size = fields.vsize.v;
			normal.date = fields.vdate.v;
			normal.dcId = fields.vdc_id.v;
			normal.hash = bytes::make_vector(fields.vfile_hash.v);
			normal.encryptedSecret = bytes::make_vector(fields.vsecret.v);
			fillDownloadedFile(normal, editData);
			result.push_back(std::move(normal));
		} break;
		}
		++index;
	}

	return result;
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
		const MTPSecureValue &value) const -> Value {
	Expects(value.type() == mtpc_secureValue);

	const auto &data = value.c_secureValue();
	const auto type = ConvertType(data.vtype);
	auto result = Value(type);
	if (data.has_data()) {
		Assert(data.vdata.type() == mtpc_secureData);
		const auto &fields = data.vdata.c_secureData();
		result.data.original = fields.vdata.v;
		result.data.hash = bytes::make_vector(fields.vdata_hash.v);
		result.data.encryptedSecret = bytes::make_vector(fields.vsecret.v);
	}
	if (data.has_files()) {
		result.files = parseFiles(data.vfiles.v);
	}
	if (data.has_plain_data()) {
		switch (data.vplain_data.type()) {
		case mtpc_securePlainPhone: {
			const auto &fields = data.vplain_data.c_securePlainPhone();
			result.data.parsed.fields["value"] = qs(fields.vphone);
		} break;
		case mtpc_securePlainEmail: {
			const auto &fields = data.vplain_data.c_securePlainEmail();
			result.data.parsed.fields["value"] = qs(fields.vemail);
		} break;
		}
	}
	// #TODO passport selfie
	return result;
}

auto FormController::findEditFile(const FullMsgId &fullId) -> EditFile* {
	for (auto &[type, value] : _form.values) {
		for (auto &file : value.filesInEdit) {
			if (file.uploadData && file.uploadData->fullId == fullId) {
				return &file;
			}
		}
	}
	return nullptr;
}

auto FormController::findEditFile(const FileKey &key) -> EditFile* {
	for (auto &[type, value] : _form.values) {
		for (auto &file : value.filesInEdit) {
			if (file.fields.dcId == key.dcId && file.fields.id == key.id) {
				return &file;
			}
		}
	}
	return nullptr;
}

auto FormController::findFile(const FileKey &key)
-> std::pair<Value*, File*> {
	for (auto &[type, value] : _form.values) {
		for (auto &file : value.files) {
			if (file.dcId == key.dcId && file.id == key.id) {
				return { &value, &file };
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
}

void FormController::formFail(const RPCError &error) {
	Ui::show(Box<InformBox>(lang(lng_passport_form_error)));
}

void FormController::requestPassword() {
	_passwordRequestId = request(MTPaccount_GetPassword(
	)).done([=](const MTPaccount_Password &result) {
		_passwordRequestId = 0;
		passwordDone(result);
	}).fail([=](const RPCError &error) {
		formFail(error);
	}).send();
}

void FormController::passwordDone(const MTPaccount_Password &result) {
	switch (result.type()) {
	case mtpc_account_noPassword:
		parsePassword(result.c_account_noPassword());
		break;

	case mtpc_account_password:
		parsePassword(result.c_account_password());
		break;
	}
	if (!_formRequestId) {
		showForm();
	}
}

void FormController::showForm() {
	if (!_bot) {
		Ui::show(Box<InformBox>("Could not get authorization bot."));
		return;
	}
	if (!_password.salt.empty()) {
		_view->showAskPassword();
	} else if (!_password.unconfirmedPattern.isEmpty()) {
		_view->showPasswordUnconfirmed();
	} else {
		_view->showNoPassword();
	}
}

void FormController::passwordFail(const RPCError &error) {
	Ui::show(Box<InformBox>("Could not get authorization form."));
}

void FormController::parsePassword(const MTPDaccount_noPassword &result) {
	_password.unconfirmedPattern = qs(result.vemail_unconfirmed_pattern);
	_password.newSalt = bytes::make_vector(result.vnew_salt.v);
	_password.newSecureSalt = bytes::make_vector(result.vnew_secure_salt.v);
	openssl::AddRandomSeed(bytes::make_span(result.vsecure_random.v));
}

void FormController::parsePassword(const MTPDaccount_password &result) {
	_password.hint = qs(result.vhint);
	_password.hasRecovery = mtpIsTrue(result.vhas_recovery);
	_password.salt = bytes::make_vector(result.vcurrent_salt.v);
	_password.unconfirmedPattern = qs(result.vemail_unconfirmed_pattern);
	_password.newSalt = bytes::make_vector(result.vnew_salt.v);
	_password.newSecureSalt = bytes::make_vector(result.vnew_secure_salt.v);
	openssl::AddRandomSeed(bytes::make_span(result.vsecure_random.v));
}

void FormController::cancel() {
	if (!_cancelled) {
		_cancelled = true;
		crl::on_main(this, [=] {
			_controller->clearAuthForm();
		});
	}
}

rpl::lifetime &FormController::lifetime() {
	return _lifetime;
}

FormController::~FormController() = default;

} // namespace Passport
