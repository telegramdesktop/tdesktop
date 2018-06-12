/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/observer.h"
#include "base/bytes.h"
#include "mtproto/rsa_public_key.h"
#include <string>
#include <vector>
#include <map>

namespace MTP {

enum class DcType {
	Regular,
	Temporary,
	MediaDownload,
	Cdn,
};
class DcOptions {
public:
	using Flag = MTPDdcOption::Flag;
	using Flags = MTPDdcOption::Flags;
	struct Endpoint {
		Endpoint(
			DcId id,
			Flags flags,
			const std::string &ip,
			int port,
			const bytes::vector &secret)
			: id(id)
			, flags(flags)
			, ip(ip)
			, port(port)
			, secret(secret) {
		}

		DcId id;
		Flags flags;
		std::string ip;
		int port;
		bytes::vector secret;

	};

	// construct methods don't notify "changed" subscribers.
	void constructFromSerialized(const QByteArray &serialized);
	void constructFromBuiltIn();
	void constructAddOne(
		int id,
		Flags flags,
		const std::string &ip,
		int port,
		const bytes::vector &secret);
	QByteArray serialize() const;

	using Ids = std::vector<DcId>;
	base::Observable<Ids> &changed() const {
		return _changed;
	}
	void setFromList(const MTPVector<MTPDcOption> &options);
	void addFromList(const MTPVector<MTPDcOption> &options);
	void addFromOther(DcOptions &&options);

	Ids configEnumDcIds() const;

	struct Variants {
		enum Address {
			IPv4 = 0,
			IPv6 = 1,
			AddressTypeCount = 2,
		};
		enum Protocol {
			Tcp = 0,
			Http = 1,
			ProtocolCount = 2,
		};
		std::vector<Endpoint> data[AddressTypeCount][ProtocolCount];
	};
	Variants lookup(DcId dcId, DcType type, bool throughProxy) const;
	DcType dcType(ShiftedDcId shiftedDcId) const;

	void setCDNConfig(const MTPDcdnConfig &config);
	bool hasCDNKeysForDc(DcId dcId) const;
	bool getDcRSAKey(DcId dcId, const QVector<MTPlong> &fingerprints, internal::RSAPublicKey *result) const;

	// Debug feature for now.
	bool loadFromFile(const QString &path);
	bool writeToFile(const QString &path) const;

private:
	bool applyOneGuarded(
		DcId dcId,
		Flags flags,
		const std::string &ip,
		int port,
		const bytes::vector &secret);
	static bool ApplyOneOption(
		std::map<DcId, std::vector<Endpoint>> &data,
		DcId dcId,
		Flags flags,
		const std::string &ip,
		int port,
		const bytes::vector &secret);
	static Ids CountOptionsDifference(
		const std::map<DcId, std::vector<Endpoint>> &a,
		const std::map<DcId, std::vector<Endpoint>> &b);
	static void FilterIfHasWithFlag(Variants &variants, Flag flag);

	void processFromList(const QVector<MTPDcOption> &options, bool overwrite);
	void computeCdnDcIds();

	void readBuiltInPublicKeys();

	class WriteLocker;
	friend class WriteLocker;

	class ReadLocker;
	friend class ReadLocker;

	std::map<DcId, std::vector<Endpoint>> _data;
	std::set<DcId> _cdnDcIds;
	std::map<uint64, internal::RSAPublicKey> _publicKeys;
	std::map<DcId, std::map<uint64, internal::RSAPublicKey>> _cdnPublicKeys;
	mutable QReadWriteLock _useThroughLockers;

	mutable base::Observable<Ids> _changed;

	// True when we have overriden options from a .tdesktop-endpoints file.
	bool _immutable = false;

};

} // namespace MTP
