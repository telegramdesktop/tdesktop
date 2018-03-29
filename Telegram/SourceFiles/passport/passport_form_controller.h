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

struct IdentityData;

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

struct ScanInfo {
	FileKey key;
	QString status;
	QImage thumb;

};

class FormController : private MTP::Sender, public base::has_weak_ptr {
public:
	FormController(
		not_null<Window::Controller*> controller,
		const FormRequest &request);

	void show();

	void submitPassword(const QString &password);
	rpl::producer<QString> passwordError() const;
	QString passwordHint() const;

	void uploadScan(int valueIndex, QByteArray &&content);
	void deleteScan(int valueIndex, int fileIndex);

	rpl::producer<> secretReadyEvents() const;

	QString defaultEmail() const;
	QString defaultPhoneNumber() const;
	rpl::producer<ScanInfo> scanUpdated() const;

	void fillRows(
		base::lambda<void(
			QString title,
			QString description,
			bool ready)> callback);
	void editValue(int index);

	void saveValueIdentity(int index, const IdentityData &data);

	~FormController();

private:
	struct UploadScanData {
		~UploadScanData();

		FullMsgId fullId;
		uint64 fileId = 0;
		int partsCount = 0;
		QByteArray md5checksum;
		bytes::vector hash;
		bytes::vector bytes;

		int offset = 0;
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
		std::unique_ptr<UploadScanData> uploadData;
		bool deleted = false;
	};
	struct Verification {
		TimeId date;
	};
	struct ValueData {
		QByteArray original;
		std::map<QString, QString> parsed;
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
		bytes::vector consistencyHash;
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

	IdentityData valueDataIdentity(const Value &value) const;
	std::vector<ScanInfo> valueFilesIdentity(const Value &value) const;
	void saveIdentity(int index);

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
	ScanInfo collectScanInfo(const EditFile &file) const;

	not_null<Window::Controller*> _controller;
	FormRequest _request;
	UserData *_bot = nullptr;

	mtpRequestId _formRequestId = 0;
	mtpRequestId _passwordRequestId = 0;
	mtpRequestId _passwordCheckRequestId = 0;

	PasswordSettings _password;
	Form _form;
	std::map<FileKey, std::unique_ptr<mtpFileLoader>> _fileLoaders;
	rpl::event_stream<ScanInfo> _scanUpdated;

	bytes::vector _secret;
	uint64 _secretId = 0;
	std::vector<base::lambda<void()>> _secretCallbacks;
	mtpRequestId _saveSecretRequestId = 0;
	rpl::event_stream<> _secretReady;
	rpl::event_stream<QString> _passwordError;

	QPointer<BoxContent> _editBox;

	rpl::lifetime _uploaderSubscriptions;

};

} // namespace Passport
