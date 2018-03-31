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
	const File &fields,
	std::unique_ptr<UploadScanData> &&uploadData)
: fields(std::move(fields))
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
			for (auto &value : _form.rows) {
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

	for (auto &value : _form.rows) {
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

void FormController::uploadScan(int valueIndex, QByteArray &&content) {
	Expects(valueIndex >= 0 && valueIndex < _form.rows.size());

	auto &value = _form.rows[valueIndex];
	auto fileIndex = int(value.filesInEdit.size());
	value.filesInEdit.emplace_back(
		File(),
		nullptr);
	const auto fileId = rand_value<uint64>();
	auto &file = value.filesInEdit.back();
	file.fields.size = content.size();
	file.fields.id = fileId;
	file.fields.dcId = MTP::maindc();
	file.fields.secret = GenerateSecretBytes();
	file.fields.date = unixtime();
	file.fields.image = ReadImage(bytes::make_span(content));
	file.fields.downloadOffset = file.fields.size;

	_scanUpdated.fire(&file);

	encryptScan(valueIndex, fileIndex, std::move(content));
}

void FormController::encryptScan(
		int valueIndex,
		int fileIndex,
		QByteArray &&content) {
	Expects(valueIndex >= 0 && valueIndex < _form.rows.size());
	Expects(fileIndex >= 0
		&& fileIndex < _form.rows[valueIndex].filesInEdit.size());

	const auto &value = _form.rows[valueIndex];
	const auto &file = value.filesInEdit[fileIndex];
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
					valueIndex,
					fileIndex,
					std::move(encrypted));
			}
		});
	});
}

void FormController::deleteScan(int valueIndex, int fileIndex) {
	scanDeleteRestore(valueIndex, fileIndex, true);
}

void FormController::restoreScan(int valueIndex, int fileIndex) {
	scanDeleteRestore(valueIndex, fileIndex, false);
}

void FormController::scanDeleteRestore(
		int valueIndex,
		int fileIndex,
		bool deleted) {
	Expects(valueIndex >= 0 && valueIndex < _form.rows.size());
	Expects(fileIndex >= 0
		&& fileIndex < _form.rows[valueIndex].filesInEdit.size());

	auto &file = _form.rows[valueIndex].filesInEdit[fileIndex];
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
		int valueIndex,
		int fileIndex,
		UploadScanData &&data) {
	Expects(valueIndex >= 0 && valueIndex < _form.rows.size());
	Expects(fileIndex >= 0
		&& fileIndex < _form.rows[valueIndex].filesInEdit.size());

	subscribeToUploader();

	auto &file = _form.rows[valueIndex].filesInEdit[fileIndex];
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

rpl::producer<not_null<const EditFile*>> FormController::scanUpdated() const {
	return _scanUpdated.events();
}

void FormController::enumerateRows(
		base::lambda<void(const Value &value)> callback) {
	ranges::for_each(_form.rows, callback);
}

not_null<Value*> FormController::startValueEdit(int index) {
	Expects(index >= 0 && index < _form.rows.size());

	auto &value = _form.rows[index];
	loadFiles(value.files);
	value.filesInEdit = ranges::view::all(
		value.files
	) | ranges::view::transform([](const File &file) {
		return EditFile(file, nullptr);
	}) | ranges::to_vector;

	return &value;
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

void FormController::cancelValueEdit(int index) {
	Expects(index >= 0 && index < _form.rows.size());

	_form.rows[index].filesInEdit.clear();
}

bool FormController::isEncryptedValue(Value::Type type) const {
	return (type == Value::Type::Identity || type == Value::Type::Address);
}

void FormController::saveValueEdit(int index) {
	Expects(index >= 0 && index < _form.rows.size());

	if (isEncryptedValue(_form.rows[index].type)) {
		saveEncryptedValue(index);
	} else {
		savePlainTextValue(index);
	}
}

void FormController::saveEncryptedValue(int index) {
	Expects(index >= 0 && index < _form.rows.size());
	Expects(isEncryptedValue(_form.rows[index].type));

	auto &value = _form.rows[index];
	if (_secret.empty()) {
		_secretCallbacks.push_back([=] {
			saveEncryptedValue(index);
		});
		return;
	}

	auto inputFiles = QVector<MTPInputSecureFile>();
	inputFiles.reserve(value.filesInEdit.size());
	for (const auto &file : value.filesInEdit) {
		if (file.deleted) {
			continue;
		} else if (const auto uploadData = file.uploadData.get()) {
			inputFiles.push_back(MTP_inputSecureFileUploaded(
				MTP_long(file.fields.id),
				MTP_int(uploadData->partsCount),
				MTP_bytes(uploadData->md5checksum),
				MTP_bytes(file.fields.hash),
				MTP_bytes(file.fields.encryptedSecret)));
		} else {
			inputFiles.push_back(MTP_inputSecureFile(
				MTP_long(file.fields.id),
				MTP_long(file.fields.accessHash)));
		}
	}

	if (value.data.secret.empty()) {
		value.data.secret = GenerateSecretBytes();
	}
	const auto encryptedData = EncryptData(
		SerializeData(value.data.parsed.fields),
		value.data.secret);
	value.data.hash = encryptedData.hash;
	value.data.encryptedSecret = EncryptValueSecret(
		value.data.secret,
		_secret,
		value.data.hash);

	const auto wrap = [&] {
		switch (value.type) {
		case Value::Type::Identity: return MTP_inputSecureValueIdentity;
		case Value::Type::Address: return MTP_inputSecureValueAddress;
		}
		Unexpected("Value type in saveEncryptedValue().");
	}();
	sendSaveRequest(index, wrap(
		MTP_secureData(
			MTP_bytes(encryptedData.bytes),
			MTP_bytes(value.data.hash),
			MTP_bytes(value.data.encryptedSecret)),
		MTP_vector<MTPInputSecureFile>(inputFiles)));
}

void FormController::savePlainTextValue(int index) {
	Expects(index >= 0 && index < _form.rows.size());
	Expects(!isEncryptedValue(_form.rows[index].type));

	auto &value = _form.rows[index];
	const auto text = value.data.parsed.fields[QString("value")];
	const auto wrap = [&] {
		switch (value.type) {
		case Value::Type::Phone: return MTP_inputSecureValuePhone;
		case Value::Type::Email: return MTP_inputSecureValueEmail;
		}
		Unexpected("Value type in savePlainTextValue().");
	}();
	sendSaveRequest(index, wrap(MTP_string(text)));
}

void FormController::sendSaveRequest(
		int index,
		const MTPInputSecureValue &value) {
	Expects(index >= 0 && index < _form.rows.size());

	request(MTPaccount_SaveSecureValue(
		value,
		MTP_long(_secretId)
	)).done([=](const MTPSecureValueSaved &result) {
		Expects(result.type() == mtpc_secureValueSaved);

		const auto &data = result.c_secureValueSaved();
		_form.rows[index].files = parseFiles(
			data.vfiles.v,
			base::take(_form.rows[index].filesInEdit));

		Ui::show(Box<InformBox>("Saved"), LayerOption::KeepOther);
	}).fail([=](const RPCError &error) {
		Ui::show(Box<InformBox>("Error saving value."));
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

template <typename DataType>
auto FormController::parseEncryptedValue(
		Value::Type type,
		const DataType &data) const -> Value {
	Expects(data.vdata.type() == mtpc_secureData);

	auto result = Value(type);
	if (data.has_verified()) {
		result.verification = parseVerified(data.vverified);
	}
	const auto &fields = data.vdata.c_secureData();
	result.data.original = fields.vdata.v;
	result.data.hash = bytes::make_vector(fields.vdata_hash.v);
	result.data.encryptedSecret = bytes::make_vector(fields.vsecret.v);
	result.files = parseFiles(data.vfiles.v);
	return result;
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

template <typename DataType>
auto FormController::parsePlainTextValue(
		Value::Type type,
		const QByteArray &text,
		const DataType &data) const -> Value {
	auto result = Value(type);
	result.data.parsed.fields[QString("value")] = QString::fromUtf8(text);
	if (data.has_verified()) {
		result.verification = parseVerified(data.vverified);
	}
	return result;
}

auto FormController::parseValue(
		const MTPSecureValue &value) const -> Value {
	switch (value.type()) {
	case mtpc_secureValueIdentity: {
		return parseEncryptedValue(
			Value::Type::Identity,
			value.c_secureValueIdentity());
	} break;
	case mtpc_secureValueAddress: {
		return parseEncryptedValue(
			Value::Type::Address,
			value.c_secureValueAddress());
	} break;
	case mtpc_secureValuePhone: {
		const auto &data = value.c_secureValuePhone();
		return parsePlainTextValue(
			Value::Type::Phone,
			data.vphone.v,
			data);
	} break;
	case mtpc_secureValueEmail: {
		const auto &data = value.c_secureValueEmail();
		return parsePlainTextValue(
			Value::Type::Phone,
			data.vemail.v,
			data);
	} break;
	}
	Unexpected("secureValue type.");
}

auto FormController::parseVerified(const MTPSecureValueVerified &data) const
-> Verification {
	Expects(data.type() == mtpc_secureValueVerified);

	const auto &fields = data.c_secureValueVerified();
	return Verification{ fields.vdate.v };
}

auto FormController::findEditFile(const FullMsgId &fullId) -> EditFile* {
	for (auto &value : _form.rows) {
		for (auto &file : value.filesInEdit) {
			if (file.uploadData && file.uploadData->fullId == fullId) {
				return &file;
			}
		}
	}
	return nullptr;
}

auto FormController::findEditFile(const FileKey &key) -> EditFile* {
	for (auto &value : _form.rows) {
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
	for (auto &value : _form.rows) {
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

	auto values = std::vector<Value>();
	for (const auto &value : data.vvalues.v) {
		values.push_back(parseValue(value));
	}
	const auto findValue = [&](Value::Type type) -> Value* {
		for (auto &value : values) {
			if (value.type == type) {
				return &value;
			}
		}
		return nullptr;
	};

	App::feedUsers(data.vusers);
	for (const auto &required : data.vrequired_types.v) {
		using Type = Value::Type;

		const auto type = [&] {
			switch (required.type()) {
			case mtpc_secureValueTypeIdentity: return Type::Identity;
			case mtpc_secureValueTypeAddress: return Type::Address;
			case mtpc_secureValueTypeEmail: return Type::Email;
			case mtpc_secureValueTypePhone: return Type::Phone;
			}
			Unexpected("Type in secureValueType type.");
		}();

		if (auto value = findValue(type)) {
			_form.rows.push_back(std::move(*value));
		} else {
			_form.rows.push_back(Value(type));
		}
	}
	_bot = App::userLoaded(_request.botId);
}

void FormController::formFail(const RPCError &error) {
	// #TODO passport testing
	_form.rows.push_back(Value(Value::Type::Identity));
	_form.rows.back().data.parsed.fields["first_name"] = "test1";
	_form.rows.back().data.parsed.fields["last_name"] = "test2";
	_view->editValue(0);
//	Ui::show(Box<InformBox>(lang(lng_passport_form_error)));
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
