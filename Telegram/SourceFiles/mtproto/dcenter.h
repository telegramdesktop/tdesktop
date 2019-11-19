/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace MTP {

class Instance;
class AuthKey;
using AuthKeyPtr = std::shared_ptr<AuthKey>;

namespace internal {

class Dcenter : public QObject {
public:
	// Main thread.
	Dcenter(DcId dcId, AuthKeyPtr &&key);

	// Thread-safe.
	[[nodiscard]] DcId id() const;

	[[nodiscard]] AuthKeyPtr getTemporaryKey() const;
	[[nodiscard]] AuthKeyPtr getPersistentKey() const;
	bool destroyTemporaryKey(uint64 keyId);
	bool destroyConfirmedForgottenKey(uint64 keyId);
	void releaseKeyCreationOnDone(
		const AuthKeyPtr &temporaryKey,
		const AuthKeyPtr &persistentKey);

	[[nodiscard]] bool connectionInited() const;
	void setConnectionInited(bool connectionInited = true);

	[[nodiscard]] bool acquireKeyCreation();
	void releaseKeyCreationOnFail();

private:
	const DcId _id = 0;
	mutable QReadWriteLock _mutex;

	AuthKeyPtr _temporaryKey;
	AuthKeyPtr _persistentKey;
	bool _connectionInited = false;
	std::atomic<bool> _creatingKey = false;

};

} // namespace internal
} // namespace MTP
