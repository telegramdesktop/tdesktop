/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/observer.h"
#include "base/bytes.h"

#include <QtCore/QReadWriteLock>
#include <string>
#include <vector>
#include <map>
#include <set>

namespace MTP {
namespace details {
class RSAPublicKey;
} // namespace details

enum class DcType {
	Regular,
	Temporary,
	MediaCluster,
	Cdn,
};

enum class Environment : uchar {
	Production,
	Test,
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

	explicit DcOptions(Environment environment);
	DcOptions(const DcOptions &other);
	~DcOptions();

	[[nodiscard]] static bool ValidateSecret(bytes::const_span secret);

	[[nodiscard]] Environment environment() const;
	[[nodiscard]] bool isTestMode() const;

	// construct methods don't notify "changed" subscribers.
	bool constructFromSerialized(const QByteArray &serialized);
	void constructFromBuiltIn();
	void constructAddOne(
		int id,
		Flags flags,
		const std::string &ip,
		int port,
		const bytes::vector &secret);
	QByteArray serialize() const;

	[[nodiscard]] rpl::producer<DcId> changed() const;
	[[nodiscard]] rpl::producer<> cdnConfigChanged() const;
	void setFromList(const MTPVector<MTPDcOption> &options);
	void addFromList(const MTPVector<MTPDcOption> &options);
	void addFromOther(DcOptions &&options);

	[[nodiscard]] std::vector<DcId> configEnumDcIds() const;

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
	[[nodiscard]] Variants lookup(
		DcId dcId,
		DcType type,
		bool throughProxy) const;
	[[nodiscard]] DcType dcType(ShiftedDcId shiftedDcId) const;

	void setCDNConfig(const MTPDcdnConfig &config);
	[[nodiscard]] bool hasCDNKeysForDc(DcId dcId) const;
	[[nodiscard]] details::RSAPublicKey getDcRSAKey(
		DcId dcId,
		const QVector<MTPlong> &fingerprints) const;

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
		base::flat_map<DcId, std::vector<Endpoint>> &data,
		DcId dcId,
		Flags flags,
		const std::string &ip,
		int port,
		const bytes::vector &secret);
	static std::vector<DcId> CountOptionsDifference(
		const base::flat_map<DcId, std::vector<Endpoint>> &a,
		const base::flat_map<DcId, std::vector<Endpoint>> &b);
	static void FilterIfHasWithFlag(Variants &variants, Flag flag);

	[[nodiscard]] bool hasMediaOnlyOptionsFor(DcId dcId) const;

	void processFromList(const QVector<MTPDcOption> &options, bool overwrite);
	void computeCdnDcIds();

	void readBuiltInPublicKeys();

	class WriteLocker;
	friend class WriteLocker;

	class ReadLocker;
	friend class ReadLocker;

	const Environment _environment = Environment();
	base::flat_map<DcId, std::vector<Endpoint>> _data;
	base::flat_set<DcId> _cdnDcIds;
	base::flat_map<uint64, details::RSAPublicKey> _publicKeys;
	base::flat_map<
		DcId,
		base::flat_map<uint64, details::RSAPublicKey>> _cdnPublicKeys;
	mutable QReadWriteLock _useThroughLockers;

	rpl::event_stream<DcId> _changed;
	rpl::event_stream<> _cdnConfigChanged;

	// True when we have overriden options from a .tdesktop-endpoints file.
	bool _immutable = false;

};

} // namespace MTP
