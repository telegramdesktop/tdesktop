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
struct UploadedSecure;
} // namespace Storage

namespace Window {
class Controller;
} // namespace Window

namespace Passport {

struct FormRequest {
	FormRequest(
		UserId botId,
		const QStringList &scope,
		const QString &callbackUrl,
		const QString &publicKey);

	UserId botId;
	QStringList scope;
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
	QString date;
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

	void uploadScan(int index, QByteArray &&content);

	rpl::producer<> secretReadyEvents() const;

	QString defaultEmail() const;
	QString defaultPhoneNumber() const;
	rpl::producer<ScanInfo> scanUpdated() const;

	void fillRows(
		base::lambda<void(
			QString title,
			QString description,
			bool ready)> callback);
	void editField(int index);

	void saveFieldIdentity(int index, const IdentityData &data);

	~FormController();

private:
	struct UploadedScan {
		~UploadedScan();

		FullMsgId fullId;
		uint64 fileId = 0;
		int partsCount = 0;
		QByteArray md5checksum;
		bytes::vector hash;
		bytes::vector bytes;
	};
	struct File {
		uint64 id = 0;
		uint64 accessHash = 0;
		int32 size = 0;
		int32 dcId = 0;
		bytes::vector fileHash;
		bytes::vector bytes;
	};
	struct EditFile {
		EditFile(
			const File &fields,
			std::unique_ptr<UploadedScan> &&uploaded);

		File fields;
		std::unique_ptr<UploadedScan> uploaded;
	};
	struct Value {
		QString name;

		QByteArray dataEncrypted;
		QByteArray dataHash;
		QByteArray dataSecretEncrypted;
		std::map<QString, QString> values;

		QString text;
		QByteArray textHash;

		std::vector<File> files;
		QByteArray filesHash;
		QByteArray filesSecretEncrypted;
		bytes::vector filesSecret;

		std::vector<EditFile> filesInEdit;
	};
	struct Field {
		enum class Type {
			Identity,
			Address,
			Phone,
			Email,
		};
		explicit Field(Type type);
		Field(Field &&other) = default;

		Type type;
		Value data;
		base::optional<Value> document;
	};
	struct Form {
		bool requestWrite = false;
		std::vector<Field> fields;
	};
	struct PasswordSettings {
		QByteArray salt;
		QByteArray newSalt;
		QString hint;
		QString unconfirmedPattern;
		bool hasRecovery = false;
	};
	Value convertValue(const MTPSecureValue &value) const;
	EditFile *findEditFile(const FullMsgId &fullId);
	std::pair<Field*, File*> findFile(const FileKey &key);

	void requestForm();
	void requestPassword();

	void formDone(const MTPaccount_AuthorizationForm &result);
	void formFail(const RPCError &error);
	void parseForm(const MTPaccount_AuthorizationForm &result);
	void showForm();

	void passwordDone(const MTPaccount_Password &result);
	void passwordFail(const RPCError &error);
	void parsePassword(const MTPDaccount_noPassword &settings);
	void parsePassword(const MTPDaccount_password &settings);

	IdentityData fieldDataIdentity(const Field &field) const;
	std::vector<ScanInfo> fieldFilesIdentity(const Field &field) const;

	void loadFiles(const std::vector<File> &files);
	void fileLoaded(FileKey key, const QByteArray &bytes);
	std::map<QString, QString> fillData(const Value &from) const;
	void saveData(int index);
	void saveFiles(int index);
	void generateSecret(base::lambda<void()> callback);

	template <typename FileHashes>
	bytes::vector computeFilesHash(
		FileHashes fileHashes,
		bytes::const_span valueHash);

	void subscribeToUploader();
	void uploadEncryptedScan(int index, UploadedScan &&data);
	void scanUploaded(const Storage::UploadedSecure &data);

	not_null<Window::Controller*> _controller;
	FormRequest _request;
	UserData *_bot = nullptr;
	QString _origin;

	mtpRequestId _formRequestId = 0;
	mtpRequestId _passwordRequestId = 0;
	mtpRequestId _passwordCheckRequestId = 0;

	PasswordSettings _password;
	Form _form;
	std::map<FileKey, std::unique_ptr<mtpFileLoader>> _fileLoaders;
	rpl::event_stream<ScanInfo> _scanUpdated;

	bytes::vector _passwordHashForSecret;
	bytes::vector _passwordHashForAuth;
	bytes::vector _secret;
	std::vector<base::lambda<void()>> _secretCallbacks;
	mtpRequestId _saveSecretRequestId = 0;
	QString _passwordEmail;
	rpl::event_stream<> _secretReady;
	rpl::event_stream<QString> _passwordError;

	QPointer<BoxContent> _editBox;

	rpl::lifetime _uploaderSubscriptions;

};

} // namespace Passport
