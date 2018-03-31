/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "mtproto/sender.h"
#include "base/weak_ptr.h"

class BoxContent;

namespace Storage {
struct UploadSecureDone;
struct UploadSecureProgress;
} // namespace Storage

namespace Window {
class Controller;
} // namespace Window

namespace Passport {

class ViewController;

struct FormRequest {
	FormRequest(
		UserId botId,
		const QString &scope,
		const QString &callbackUrl,
		const QString &publicKey);

	UserId botId;
	QString scope;
	QString callbackUrl;
	QString publicKey;

};

struct UploadScanData {
	FullMsgId fullId;
	uint64 fileId = 0;
	int partsCount = 0;
	QByteArray md5checksum;
	bytes::vector hash;
	bytes::vector bytes;

	int offset = 0;
};

class UploadScanDataPointer {
public:
	UploadScanDataPointer(std::unique_ptr<UploadScanData> &&value);
	UploadScanDataPointer(UploadScanDataPointer &&other);
	UploadScanDataPointer &operator=(UploadScanDataPointer &&other);
	~UploadScanDataPointer();

	UploadScanData *get() const;
	operator UploadScanData*() const;
	explicit operator bool() const;
	UploadScanData *operator->() const;

private:
	std::unique_ptr<UploadScanData> _value;

};

struct File {
	uint64 id = 0;
	uint64 accessHash = 0;
	int32 size = 0;
	int32 dcId = 0;
	TimeId date = 0;
	bytes::vector hash;
	bytes::vector secret;
	bytes::vector encryptedSecret;

	int downloadOffset = 0;
	QImage image;
};

struct EditFile {
	EditFile(
		const File &fields,
		std::unique_ptr<UploadScanData> &&uploadData);

	File fields;
	UploadScanDataPointer uploadData;
	std::shared_ptr<bool> guard;
	bool deleted = false;
};

struct Verification {
	TimeId date;
};

struct ValueMap {
	std::map<QString, QString> fields;
};

struct ValueData {
	QByteArray original;
	ValueMap parsed;
	bytes::vector hash;
	bytes::vector secret;
	bytes::vector encryptedSecret;
};

struct Value {
	enum class Type {
		Identity,
		Address,
		Phone,
		Email,
	};

	explicit Value(Type type);
	Value(Value &&other) = default;
	Value &operator=(Value &&other) = default;

	Type type;
	ValueData data;
	std::vector<File> files;
	std::vector<EditFile> filesInEdit;
	base::optional<Verification> verification;
};

struct Form {
	std::vector<Value> rows;
};

struct PasswordSettings {
	bytes::vector salt;
	bytes::vector newSalt;
	bytes::vector newSecureSalt;
	QString hint;
	QString unconfirmedPattern;
	QString confirmedEmail;
	bool hasRecovery = false;
};

struct FileKey {
	uint64 id = 0;
	int32 dcId = 0;

	inline bool operator==(const FileKey &other) const {
		return (id == other.id) && (dcId == other.dcId);
	}
	inline bool operator!=(const FileKey &other) const {
		return !(*this == other);
	}
	inline bool operator<(const FileKey &other) const {
		return (id < other.id) || ((id == other.id) && (dcId < other.dcId));
	}
	inline bool operator>(const FileKey &other) const {
		return (other < *this);
	}
	inline bool operator<=(const FileKey &other) const {
		return !(other < *this);
	}
	inline bool operator>=(const FileKey &other) const {
		return !(*this < other);
	}

};

class FormController : private MTP::Sender, public base::has_weak_ptr {
public:
	FormController(
		not_null<Window::Controller*> controller,
		const FormRequest &request);

	void show();
	UserData *bot() const;

	void submitPassword(const QString &password);
	rpl::producer<QString> passwordError() const;
	QString passwordHint() const;

	void uploadScan(int valueIndex, QByteArray &&content);
	void deleteScan(int valueIndex, int fileIndex);
	void restoreScan(int valueIndex, int fileIndex);

	rpl::producer<> secretReadyEvents() const;

	QString defaultEmail() const;
	QString defaultPhoneNumber() const;

	rpl::producer<not_null<const EditFile*>> scanUpdated() const;

	void enumerateRows(base::lambda<void(const Value &value)> callback);
	not_null<Value*> startValueEdit(int index);
	void cancelValueEdit(int index);
	void saveValueEdit(int index);

	void cancel();

	rpl::lifetime &lifetime();

	~FormController();

private:
	EditFile *findEditFile(const FullMsgId &fullId);
	EditFile *findEditFile(const FileKey &key);
	std::pair<Value*, File*> findFile(const FileKey &key);

	void requestForm();
	void requestPassword();

	void formDone(const MTPaccount_AuthorizationForm &result);
	void formFail(const RPCError &error);
	void parseForm(const MTPaccount_AuthorizationForm &result);
	void showForm();
	Value parseValue(const MTPSecureValue &value) const;
	template <typename DataType>
	Value parseEncryptedValue(
		Value::Type type,
		const DataType &data) const;
	template <typename DataType>
	Value parsePlainTextValue(
		Value::Type type,
		const QByteArray &text,
		const DataType &data) const;
	Verification parseVerified(const MTPSecureValueVerified &data) const;
	std::vector<File> parseFiles(
		const QVector<MTPSecureFile> &data,
		const std::vector<EditFile> &editData = {}) const;
	void fillDownloadedFile(
		File &destination,
		const std::vector<EditFile> &source) const;

	void passwordDone(const MTPaccount_Password &result);
	void passwordFail(const RPCError &error);
	void parsePassword(const MTPDaccount_noPassword &settings);
	void parsePassword(const MTPDaccount_password &settings);
	bytes::vector passwordHashForAuth(bytes::const_span password) const;
	void validateSecureSecret(
		bytes::const_span salt,
		bytes::const_span encryptedSecret,
		bytes::const_span password);
	void decryptValues();
	void decryptValue(Value &value);
	bool validateValueSecrets(Value &value);
	void resetValue(Value &value);

	void loadFiles(std::vector<File> &files);
	void fileLoadDone(FileKey key, const QByteArray &bytes);
	void fileLoadProgress(FileKey key, int offset);
	void fileLoadFail(FileKey key);
	void generateSecret(bytes::const_span password);

	void subscribeToUploader();
	void encryptScan(
		int valueIndex,
		int fileIndex,
		QByteArray &&content);
	void uploadEncryptedScan(
		int valueIndex,
		int fileIndex,
		UploadScanData &&data);
	void scanUploadDone(const Storage::UploadSecureDone &data);
	void scanUploadProgress(const Storage::UploadSecureProgress &data);
	void scanUploadFail(const FullMsgId &fullId);
	void scanDeleteRestore(int valueIndex, int fileIndex, bool deleted);

	bool isEncryptedValue(Value::Type type) const;
	void saveEncryptedValue(int index);
	void savePlainTextValue(int index);
	void sendSaveRequest(int index, const MTPInputSecureValue &value);

	not_null<Window::Controller*> _controller;
	FormRequest _request;
	UserData *_bot = nullptr;

	mtpRequestId _formRequestId = 0;
	mtpRequestId _passwordRequestId = 0;
	mtpRequestId _passwordCheckRequestId = 0;

	PasswordSettings _password;
	Form _form;
	bool _cancelled = false;
	std::map<FileKey, std::unique_ptr<mtpFileLoader>> _fileLoaders;

	rpl::event_stream<not_null<const EditFile*>> _scanUpdated;

	bytes::vector _secret;
	uint64 _secretId = 0;
	std::vector<base::lambda<void()>> _secretCallbacks;
	mtpRequestId _saveSecretRequestId = 0;
	rpl::event_stream<> _secretReady;
	rpl::event_stream<QString> _passwordError;

	rpl::lifetime _uploaderSubscriptions;
	rpl::lifetime _lifetime;

	std::unique_ptr<ViewController> _view;

};

} // namespace Passport
