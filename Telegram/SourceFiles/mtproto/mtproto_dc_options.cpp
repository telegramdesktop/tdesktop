/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "mtproto/mtproto_dc_options.h"

#include "mtproto/details/mtproto_rsa_public_key.h"
#include "mtproto/facade.h"
#include "mtproto/connection_tcp.h"
#include "storage/serialize_common.h"
#include "base/qt_adapters.h"

#include <QtCore/QFile>
#include <QtCore/QRegularExpression>

namespace MTP {
namespace {

using namespace details;

struct BuiltInDc {
	int id;
	const char *ip;
	int port;
};

const BuiltInDc kBuiltInDcs[] = {
	{ 1, "149.154.175.50" , 443 },
	{ 2, "149.154.167.51" , 443 },
	{ 2, "95.161.76.100"  , 443 },
	{ 3, "149.154.175.100", 443 },
	{ 4, "149.154.167.91" , 443 },
	{ 5, "149.154.171.5"  , 443 },
};

const BuiltInDc kBuiltInDcsIPv6[] = {
	{ 1, "2001:0b28:f23d:f001:0000:0000:0000:000a", 443 },
	{ 2, "2001:067c:04e8:f002:0000:0000:0000:000a", 443 },
	{ 3, "2001:0b28:f23d:f003:0000:0000:0000:000a", 443 },
	{ 4, "2001:067c:04e8:f004:0000:0000:0000:000a", 443 },
	{ 5, "2001:0b28:f23f:f005:0000:0000:0000:000a", 443 },
};

const BuiltInDc kBuiltInDcsTest[] = {
	{ 1, "149.154.175.10" , 443 },
	{ 2, "149.154.167.40" , 443 },
	{ 3, "149.154.175.117", 443 }
};

const BuiltInDc kBuiltInDcsIPv6Test[] = {
	{ 1, "2001:0b28:f23d:f001:0000:0000:0000:000e", 443 },
	{ 2, "2001:067c:04e8:f002:0000:0000:0000:000e", 443 },
	{ 3, "2001:0b28:f23d:f003:0000:0000:0000:000e", 443 }
};

const char *(kPublicRSAKeys[]) = { "\
-----BEGIN RSA PUBLIC KEY-----\n\
MIIBCgKCAQEAwVACPi9w23mF3tBkdZz+zwrzKOaaQdr01vAbU4E1pvkfj4sqDsm6\n\
lyDONS789sVoD/xCS9Y0hkkC3gtL1tSfTlgCMOOul9lcixlEKzwKENj1Yz/s7daS\n\
an9tqw3bfUV/nqgbhGX81v/+7RFAEd+RwFnK7a+XYl9sluzHRyVVaTTveB2GazTw\n\
Efzk2DWgkBluml8OREmvfraX3bkHZJTKX4EQSjBbbdJ2ZXIsRrYOXfaA+xayEGB+\n\
8hdlLmAjbCVfaigxX0CDqWeR1yFL9kwd9P0NsZRPsmoqVwMbMu7mStFai6aIhc3n\n\
Slv8kg9qv1m6XHVQY3PnEw+QQtqSIXklHwIDAQAB\n\
-----END RSA PUBLIC KEY-----", "\
-----BEGIN PUBLIC KEY-----\n\
MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAruw2yP/BCcsJliRoW5eB\n\
VBVle9dtjJw+OYED160Wybum9SXtBBLXriwt4rROd9csv0t0OHCaTmRqBcQ0J8fx\n\
hN6/cpR1GWgOZRUAiQxoMnlt0R93LCX/j1dnVa/gVbCjdSxpbrfY2g2L4frzjJvd\n\
l84Kd9ORYjDEAyFnEA7dD556OptgLQQ2e2iVNq8NZLYTzLp5YpOdO1doK+ttrltg\n\
gTCy5SrKeLoCPPbOgGsdxJxyz5KKcZnSLj16yE5HvJQn0CNpRdENvRUXe6tBP78O\n\
39oJ8BTHp9oIjd6XWXAsp2CvK45Ol8wFXGF710w9lwCGNbmNxNYhtIkdqfsEcwR5\n\
JwIDAQAB\n\
-----END PUBLIC KEY-----", "\
-----BEGIN PUBLIC KEY-----\n\
MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAvfLHfYH2r9R70w8prHbl\n\
Wt/nDkh+XkgpflqQVcnAfSuTtO05lNPspQmL8Y2XjVT4t8cT6xAkdgfmmvnvRPOO\n\
KPi0OfJXoRVylFzAQG/j83u5K3kRLbae7fLccVhKZhY46lvsueI1hQdLgNV9n1cQ\n\
3TDS2pQOCtovG4eDl9wacrXOJTG2990VjgnIKNA0UMoP+KF03qzryqIt3oTvZq03\n\
DyWdGK+AZjgBLaDKSnC6qD2cFY81UryRWOab8zKkWAnhw2kFpcqhI0jdV5QaSCEx\n\
vnsjVaX0Y1N0870931/5Jb9ICe4nweZ9kSDF/gip3kWLG0o8XQpChDfyvsqB9OLV\n\
/wIDAQAB\n\
-----END PUBLIC KEY-----", "\
-----BEGIN PUBLIC KEY-----\n\
MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAs/ditzm+mPND6xkhzwFI\n\
z6J/968CtkcSE/7Z2qAJiXbmZ3UDJPGrzqTDHkO30R8VeRM/Kz2f4nR05GIFiITl\n\
4bEjvpy7xqRDspJcCFIOcyXm8abVDhF+th6knSU0yLtNKuQVP6voMrnt9MV1X92L\n\
GZQLgdHZbPQz0Z5qIpaKhdyA8DEvWWvSUwwc+yi1/gGaybwlzZwqXYoPOhwMebzK\n\
Uk0xW14htcJrRrq+PXXQbRzTMynseCoPIoke0dtCodbA3qQxQovE16q9zz4Otv2k\n\
4j63cz53J+mhkVWAeWxVGI0lltJmWtEYK6er8VqqWot3nqmWMXogrgRLggv/Nbbo\n\
oQIDAQAB\n\
-----END PUBLIC KEY-----", "\
-----BEGIN PUBLIC KEY-----\n\
MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAvmpxVY7ld/8DAjz6F6q0\n\
5shjg8/4p6047bn6/m8yPy1RBsvIyvuDuGnP/RzPEhzXQ9UJ5Ynmh2XJZgHoE9xb\n\
nfxL5BXHplJhMtADXKM9bWB11PU1Eioc3+AXBB8QiNFBn2XI5UkO5hPhbb9mJpjA\n\
9Uhw8EdfqJP8QetVsI/xrCEbwEXe0xvifRLJbY08/Gp66KpQvy7g8w7VB8wlgePe\n\
xW3pT13Ap6vuC+mQuJPyiHvSxjEKHgqePji9NP3tJUFQjcECqcm0yV7/2d0t/pbC\n\
m+ZH1sadZspQCEPPrtbkQBlvHb4OLiIWPGHKSMeRFvp3IWcmdJqXahxLCUS1Eh6M\n\
AQIDAQAB\n\
-----END PUBLIC KEY-----" };

} // namespace

class DcOptions::WriteLocker {
public:
	WriteLocker(not_null<DcOptions*> that)
	: _that(that)
	, _lock(&_that->_useThroughLockers) {
	}

	void unlock() {
		_lock.unlock();
	}

	~WriteLocker() {
		_that->computeCdnDcIds();
	}

private:
	not_null<DcOptions*> _that;
	QWriteLocker _lock;

};

class DcOptions::ReadLocker {
public:
	ReadLocker(not_null<const DcOptions*> that)
	: _lock(&that->_useThroughLockers) {
	}

	void unlock() {
		_lock.unlock();
	}

private:
	QReadLocker _lock;

};

DcOptions::DcOptions(Environment environment)
: _environment(environment) {
	constructFromBuiltIn();
}

DcOptions::DcOptions(const DcOptions &other)
: _environment(other._environment)
, _data(other._data)
, _cdnDcIds(other._cdnDcIds)
, _publicKeys(other._publicKeys)
, _cdnPublicKeys(other._cdnPublicKeys)
, _immutable(other._immutable) {
}

DcOptions::~DcOptions() = default;

bool DcOptions::ValidateSecret(bytes::const_span secret) {
	// See also TcpConnection::Protocol::Create.
	return (secret.size() >= 21 && secret[0] == bytes::type(0xEE))
		|| (secret.size() == 17 && secret[0] == bytes::type(0xDD))
		|| (secret.size() == 16)
		|| secret.empty();
}

void DcOptions::readBuiltInPublicKeys() {
	for (const auto key : kPublicRSAKeys) {
		const auto keyBytes = bytes::make_span(key, strlen(key));
		auto parsed = RSAPublicKey(keyBytes);
		if (parsed.valid()) {
			_publicKeys.emplace(parsed.fingerprint(), std::move(parsed));
		} else {
			LOG(("MTP Error: could not read this public RSA key:"));
			LOG((key));
		}
	}
}

Environment DcOptions::environment() const {
	return _environment;
}

bool DcOptions::isTestMode() const {
	return (_environment != Environment::Production);
}

void DcOptions::constructFromBuiltIn() {
	WriteLocker lock(this);
	_data.clear();

	readBuiltInPublicKeys();

	const auto list = isTestMode()
		? gsl::make_span(kBuiltInDcsTest)
		: gsl::make_span(kBuiltInDcs).subspan(0);
	for (const auto &entry : list) {
		const auto flags = Flag::f_static | 0;
		applyOneGuarded(entry.id, flags, entry.ip, entry.port, {});
		DEBUG_LOG(("MTP Info: adding built in DC %1 connect option: %2:%3"
			).arg(entry.id
			).arg(entry.ip
			).arg(entry.port));
	}

	const auto listv6 = isTestMode()
		? gsl::make_span(kBuiltInDcsIPv6Test)
		: gsl::make_span(kBuiltInDcsIPv6).subspan(0);
	for (const auto &entry : listv6) {
		const auto flags = Flag::f_static | Flag::f_ipv6;
		applyOneGuarded(entry.id, flags, entry.ip, entry.port, {});
		DEBUG_LOG(("MTP Info: adding built in DC %1 IPv6 connect option: "
			"%2:%3"
			).arg(entry.id
			).arg(entry.ip
			).arg(entry.port));
	}
}

void DcOptions::processFromList(
		const QVector<MTPDcOption> &options,
		bool overwrite) {
	if (options.empty() || _immutable) {
		return;
	}

	auto data = [&] {
		if (overwrite) {
			return base::flat_map<DcId, std::vector<Endpoint>>();
		}
		ReadLocker lock(this);
		return _data;
	}();
	for (auto &mtpOption : options) {
		if (mtpOption.type() != mtpc_dcOption) {
			LOG(("Wrong type in DcOptions: %1").arg(mtpOption.type()));
			continue;
		}

		auto &option = mtpOption.c_dcOption();
		auto dcId = option.vid().v;
		auto flags = option.vflags().v;
		auto ip = std::string(
			option.vip_address().v.constData(),
			option.vip_address().v.size());
		auto port = option.vport().v;
		auto secret = bytes::make_vector(option.vsecret().value_or_empty());
		ApplyOneOption(data, dcId, flags, ip, port, secret);
	}

	const auto difference = [&] {
		WriteLocker lock(this);
		auto result = CountOptionsDifference(_data, data);
		if (!result.empty()) {
			_data = std::move(data);
		}
		return result;
	}();
	for (const auto dcId : difference) {
		_changed.fire_copy(dcId);
	}
}

void DcOptions::setFromList(const MTPVector<MTPDcOption> &options) {
	processFromList(options.v, true);
}

void DcOptions::addFromList(const MTPVector<MTPDcOption> &options) {
	processFromList(options.v, false);
}

void DcOptions::addFromOther(DcOptions &&options) {
	if (this == &options || _immutable) {
		return;
	}

	auto idsChanged = std::vector<DcId>();
	{
		ReadLocker lock(&options);
		if (options._data.empty()) {
			return;
		}

		idsChanged.reserve(options._data.size());
		{
			WriteLocker lock(this);
			const auto changed = [&](const std::vector<Endpoint> &list) {
				auto result = false;
				for (const auto &endpoint : list) {
					const auto dcId = endpoint.id;
					const auto flags = endpoint.flags;
					const auto &ip = endpoint.ip;
					const auto port = endpoint.port;
					const auto &secret = endpoint.secret;
					if (applyOneGuarded(dcId, flags, ip, port, secret)) {
						result = true;
					}
				}
				return result;
			};
			for (const auto &item : base::take(options._data)) {
				if (changed(item.second)) {
					idsChanged.push_back(item.first);
				}
			}
			for (auto &item : options._cdnPublicKeys) {
				for (auto &entry : item.second) {
					_cdnPublicKeys[item.first].insert(std::move(entry));
				}
			}
		}
	}
	for (const auto dcId : idsChanged) {
		_changed.fire_copy(dcId);
	}
}

void DcOptions::constructAddOne(
		int id,
		Flags flags,
		const std::string &ip,
		int port,
		const bytes::vector &secret) {
	WriteLocker lock(this);
	applyOneGuarded(BareDcId(id), flags, ip, port, secret);
}

bool DcOptions::applyOneGuarded(
		DcId dcId,
		Flags flags,
		const std::string &ip,
		int port,
		const bytes::vector &secret) {
	return ApplyOneOption(_data, dcId, flags, ip, port, secret);
}

bool DcOptions::ApplyOneOption(
		base::flat_map<DcId, std::vector<Endpoint>> &data,
		DcId dcId,
		Flags flags,
		const std::string &ip,
		int port,
		const bytes::vector &secret) {
	auto i = data.find(dcId);
	if (i != data.cend()) {
		for (auto &endpoint : i->second) {
			if (endpoint.ip == ip && endpoint.port == port) {
				return false;
			}
		}
		i->second.emplace_back(dcId, flags, ip, port, secret);
	} else {
		data.emplace(dcId, std::vector<Endpoint>(
			1,
			Endpoint(dcId, flags, ip, port, secret)));
	}
	return true;
}

std::vector<DcId> DcOptions::CountOptionsDifference(
		const base::flat_map<DcId, std::vector<Endpoint>> &a,
		const base::flat_map<DcId, std::vector<Endpoint>> &b) {
	auto result = std::vector<DcId>();
	const auto find = [](
			const std::vector<Endpoint> &where,
			const Endpoint &what) {
		for (const auto &endpoint : where) {
			if (endpoint.ip == what.ip && endpoint.port == what.port) {
				return true;
			}
		}
		return false;
	};
	const auto equal = [&](
			const std::vector<Endpoint> &m,
			const std::vector<Endpoint> &n) {
		if (m.size() != n.size()) {
			return false;
		}
		for (const auto &endpoint : m) {
			if (!find(n, endpoint)) {
				return false;
			}
		}
		return true;
	};

	auto i = begin(a);
	auto j = begin(b);
	const auto max = std::numeric_limits<DcId>::max();
	while (i != end(a) || j != end(b)) {
		const auto aId = (i == end(a)) ? max : i->first;
		const auto bId = (j == end(b)) ? max : j->first;
		if (aId < bId) {
			result.push_back(aId);
			++i;
		} else if (bId < aId) {
			result.push_back(bId);
			++j;
		} else {
			if (!equal(i->second, j->second)) {
				result.push_back(aId);
			}
			++i;
			++j;
		}
	}
	return result;
}

QByteArray DcOptions::serialize() const {
	if (_immutable) {
		// Don't write the overriden options to our settings.
		return DcOptions(_environment).serialize();
	}

	ReadLocker lock(this);

	auto size = sizeof(qint32);

	// Dc options.
	auto optionsCount = 0;
	size += sizeof(qint32);
	for (const auto &item : _data) {
		if (isTemporaryDcId(item.first)) {
			continue;
		}
		for (const auto &endpoint : item.second) {
			++optionsCount;
			// id + flags + port
			size += sizeof(qint32) + sizeof(qint32) + sizeof(qint32);
			size += sizeof(qint32) + endpoint.ip.size();
			size += sizeof(qint32) + endpoint.secret.size();
		}
	}

	// CDN public keys.
	auto count = 0;
	for (auto &keysInDc : _cdnPublicKeys) {
		count += keysInDc.second.size();
	}
	struct SerializedPublicKey {
		DcId dcId;
		bytes::vector n;
		bytes::vector e;
	};
	std::vector<SerializedPublicKey> publicKeys;
	publicKeys.reserve(count);
	size += sizeof(qint32);
	for (const auto &keysInDc : _cdnPublicKeys) {
		for (const auto &entry : keysInDc.second) {
			publicKeys.push_back({
				keysInDc.first,
				entry.second.getN(),
				entry.second.getE()
			});
			size += sizeof(qint32)
				+ Serialize::bytesSize(publicKeys.back().n)
				+ Serialize::bytesSize(publicKeys.back().e);
		}
	}

	constexpr auto kVersion = 1;

	auto result = QByteArray();
	result.reserve(size);
	{
		QDataStream stream(&result, QIODevice::WriteOnly);
		stream.setVersion(QDataStream::Qt_5_1);
		stream << qint32(-kVersion);

		// Dc options.
		stream << qint32(optionsCount);
		for (const auto &item : _data) {
			if (isTemporaryDcId(item.first)) {
				continue;
			}
			for (const auto &endpoint : item.second) {
				stream << qint32(endpoint.id)
					<< qint32(endpoint.flags)
					<< qint32(endpoint.port)
					<< qint32(endpoint.ip.size());
				stream.writeRawData(endpoint.ip.data(), endpoint.ip.size());
				stream << qint32(endpoint.secret.size());
				stream.writeRawData(
					reinterpret_cast<const char*>(endpoint.secret.data()),
					endpoint.secret.size());
			}
		}

		// CDN public keys.
		stream << qint32(publicKeys.size());
		for (auto &key : publicKeys) {
			stream << qint32(key.dcId)
				<< Serialize::bytes(key.n)
				<< Serialize::bytes(key.e);
		}
	}
	return result;
}

bool DcOptions::constructFromSerialized(const QByteArray &serialized) {
	QDataStream stream(serialized);
	stream.setVersion(QDataStream::Qt_5_1);

	auto minusVersion = qint32(0);
	stream >> minusVersion;
	const auto version = (minusVersion < 0) ? (-minusVersion) : 0;

	auto count = qint32(0);
	if (version > 0) {
		stream >> count;
	} else {
		count = minusVersion;
	}
	if (stream.status() != QDataStream::Ok) {
		LOG(("MTP Error: Bad data for DcOptions::constructFromSerialized()"));
		return false;
	}

	WriteLocker lock(this);
	_data.clear();
	for (auto i = 0; i != count; ++i) {
		qint32 id = 0, flags = 0, port = 0, ipSize = 0;
		stream >> id >> flags >> port >> ipSize;

		// https://stackoverflow.com/questions/1076714/max-length-for-client-ip-address
		constexpr auto kMaxIpSize = 45;
		if (ipSize <= 0 || ipSize > kMaxIpSize) {
			LOG(("MTP Error: Bad data inside DcOptions::constructFromSerialized()"));
			return false;
		}

		auto ip = std::string(ipSize, ' ');
		stream.readRawData(ip.data(), ipSize);

		constexpr auto kMaxSecretSize = 32;
		auto secret = bytes::vector();
		if (version > 0) {
			auto secretSize = qint32(0);
			stream >> secretSize;
			if (secretSize < 0 || secretSize > kMaxSecretSize) {
				LOG(("MTP Error: Bad data inside DcOptions::constructFromSerialized()"));
				return false;
			} else if (secretSize > 0) {
				secret.resize(secretSize);
				stream.readRawData(
					reinterpret_cast<char*>(secret.data()),
					secretSize);
			}
		}

		if (stream.status() != QDataStream::Ok) {
			LOG(("MTP Error: Bad data inside DcOptions::constructFromSerialized()"));
			return false;
		}

		applyOneGuarded(
			DcId(id),
			Flags::from_raw(flags),
			ip,
			port,
			secret);
	}

	// Read CDN config
	if (!stream.atEnd()) {
		auto count = qint32(0);
		stream >> count;
		if (stream.status() != QDataStream::Ok) {
			LOG(("MTP Error: Bad data for CDN config in DcOptions::constructFromSerialized()"));
			return false;
		}

		for (auto i = 0; i != count; ++i) {
			qint32 dcId = 0;
			bytes::vector n, e;
			stream >> dcId >> Serialize::bytes(n) >> Serialize::bytes(e);
			if (stream.status() != QDataStream::Ok) {
				LOG(("MTP Error: Bad data for CDN config inside DcOptions::constructFromSerialized()"));
				return false;
			}

			auto key = RSAPublicKey(n, e);
			if (key.valid()) {
				_cdnPublicKeys[dcId].emplace(key.fingerprint(), std::move(key));
			} else {
				LOG(("MTP Error: Could not read valid CDN public key."));
				return false;
			}
		}
	}
	return true;
}

rpl::producer<DcId> DcOptions::changed() const {
	return _changed.events();
}

rpl::producer<> DcOptions::cdnConfigChanged() const {
	return _cdnConfigChanged.events();
}

std::vector<DcId> DcOptions::configEnumDcIds() const {
	auto result = std::vector<DcId>();
	{
		ReadLocker lock(this);
		result.reserve(_data.size());
		for (auto &item : _data) {
			const auto dcId = item.first;
			Assert(!item.second.empty());
			if (!isCdnDc(item.second.front().flags)
				&& !isTemporaryDcId(dcId)) {
				result.push_back(dcId);
			}
		}
	}
	ranges::sort(result);
	return result;
}

DcType DcOptions::dcType(ShiftedDcId shiftedDcId) const {
	if (isTemporaryDcId(shiftedDcId)) {
		return DcType::Temporary;
	}
	ReadLocker lock(this);
	if (_cdnDcIds.find(BareDcId(shiftedDcId)) != _cdnDcIds.cend()) {
		return DcType::Cdn;
	}
	const auto dcId = BareDcId(shiftedDcId);
	if (isDownloadDcId(shiftedDcId) && hasMediaOnlyOptionsFor(dcId)) {
		return DcType::MediaCluster;
	}
	return DcType::Regular;
}

void DcOptions::setCDNConfig(const MTPDcdnConfig &config) {
	WriteLocker lock(this);
	_cdnPublicKeys.clear();
	for (const auto &key : config.vpublic_keys().v) {
		key.match([&](const MTPDcdnPublicKey &data) {
			const auto keyBytes = bytes::make_span(data.vpublic_key().v);
			auto key = RSAPublicKey(keyBytes);
			if (key.valid()) {
				_cdnPublicKeys[data.vdc_id().v].emplace(
					key.fingerprint(),
					std::move(key));
			} else {
				LOG(("MTP Error: could not read this public RSA key:"));
				LOG((qs(data.vpublic_key())));
			}
		});
	}
	lock.unlock();

	_cdnConfigChanged.fire({});
}

bool DcOptions::hasCDNKeysForDc(DcId dcId) const {
	ReadLocker lock(this);
	return _cdnPublicKeys.find(dcId) != _cdnPublicKeys.cend();
}

RSAPublicKey DcOptions::getDcRSAKey(
		DcId dcId,
		const QVector<MTPlong> &fingerprints) const {
	const auto findKey = [&](
			const base::flat_map<uint64, RSAPublicKey> &keys) {
		for (const auto &fingerprint : fingerprints) {
			const auto it = keys.find(static_cast<uint64>(fingerprint.v));
			if (it != keys.cend()) {
				return it->second;
			}
		}
		return RSAPublicKey();
	};
	{
		ReadLocker lock(this);
		const auto it = _cdnPublicKeys.find(dcId);
		if (it != _cdnPublicKeys.cend()) {
			return findKey(it->second);
		}
	}
	return findKey(_publicKeys);
}

auto DcOptions::lookup(
		DcId dcId,
		DcType type,
		bool throughProxy) const -> Variants {
	using Flag = Flag;
	auto result = Variants();

	ReadLocker lock(this);
	const auto i = _data.find(dcId);
	if (i == end(_data)) {
		return result;
	}
	for (const auto &endpoint : i->second) {
		const auto flags = endpoint.flags;
		if (type == DcType::Cdn && !(flags & Flag::f_cdn)) {
			continue;
		} else if (type != DcType::MediaCluster
			&& (flags & Flag::f_media_only)) {
			continue;
		} else if (!ValidateSecret(endpoint.secret)) {
			continue;
		}
		const auto address = (flags & Flag::f_ipv6)
			? Variants::IPv6
			: Variants::IPv4;
		result.data[address][Variants::Tcp].push_back(endpoint);
		if (!(flags & (Flag::f_tcpo_only | Flag::f_secret))) {
			result.data[address][Variants::Http].push_back(endpoint);
		}
	}
	if (type == DcType::MediaCluster) {
		FilterIfHasWithFlag(result, Flag::f_media_only);
	}
	if (throughProxy) {
		FilterIfHasWithFlag(result, Flag::f_static);
	}
	return result;
}

bool DcOptions::hasMediaOnlyOptionsFor(DcId dcId) const {
	ReadLocker lock(this);
	const auto i = _data.find(dcId);
	if (i == end(_data)) {
		return false;
	}
	for (const auto &endpoint : i->second) {
		const auto flags = endpoint.flags;
		if (flags & Flag::f_media_only) {
			return true;
		}
	}
	return false;
}

void DcOptions::FilterIfHasWithFlag(Variants &variants, Flag flag) {
	const auto is = [&](const Endpoint &endpoint) {
		return (endpoint.flags & flag) != 0;
	};
	const auto has = [&](const std::vector<Endpoint> &list) {
		return ranges::any_of(list, is);
	};
	for (auto &byAddress : variants.data) {
		for (auto &list : byAddress) {
			if (has(list)) {
				list = ranges::view::all(
					list
				) | ranges::view::filter(
					is
				) | ranges::to_vector;
			}
		}
	}
}

void DcOptions::computeCdnDcIds() {
	_cdnDcIds.clear();
	for (auto &item : _data) {
		Assert(!item.second.empty());
		if (item.second.front().flags & Flag::f_cdn) {
			_cdnDcIds.insert(BareDcId(item.first));
		}
	}
}

bool DcOptions::loadFromFile(const QString &path) {
	QVector<MTPDcOption> options;

	QFile f(path);
	if (!f.open(QIODevice::ReadOnly)) {
		LOG(("MTP Error: could not read '%1'").arg(path));
		return false;
	}
	QTextStream stream(&f);
	stream.setCodec("UTF-8");
	while (!stream.atEnd()) {
		auto line = stream.readLine();
		auto components = line.split(QRegularExpression(R"(\s)"), base::QStringSkipEmptyParts);
		if (components.isEmpty() || components[0].startsWith('#')) {
			continue;
		}

		auto error = [line] {
			LOG(("MTP Error: in .tdesktop-endpoints expected 'dcId host port [tcpo_only] [media_only]', got '%1'").arg(line));
			return false;
		};
		if (components.size() < 3) {
			return error();
		}
		auto dcId = components[0].toInt();
		auto ip = components[1];
		auto port = components[2].toInt();
		auto host = QHostAddress();
		if (dcId <= 0 || dcId >= kDcShift || !host.setAddress(ip) || port <= 0) {
			return error();
		}
		auto flags = Flags(0);
		if (host.protocol() == QAbstractSocket::IPv6Protocol) {
			flags |= Flag::f_ipv6;
		}
		for (auto &option : components.mid(3)) {
			if (option.startsWith('#')) {
				break;
			} else if (option == qstr("tcpo_only")) {
				flags |= Flag::f_tcpo_only;
			} else if (option == qstr("media_only")) {
				flags |= Flag::f_media_only;
			} else {
				return error();
			}
		}
		options.push_back(MTP_dcOption(
			MTP_flags(flags),
			MTP_int(dcId),
			MTP_string(ip),
			MTP_int(port),
			MTPbytes()));
	}
	if (options.isEmpty()) {
		LOG(("MTP Error: in .tdesktop-endpoints expected at least one endpoint being provided."));
		return false;
	}

	_immutable = false;
	setFromList(MTP_vector<MTPDcOption>(options));
	_immutable = true;

	return true;
}

bool DcOptions::writeToFile(const QString &path) const {
	QFile f(path);
	if (!f.open(QIODevice::WriteOnly)) {
		return false;
	}
	QTextStream stream(&f);
	stream.setCodec("UTF-8");

	ReadLocker lock(this);
	for (const auto &item : _data) {
		for (const auto &option : item.second) {
			stream
				<< option.id
				<< ' '
				<< QString::fromStdString(option.ip)
				<< ' ' << option.port;
			if (option.flags & Flag::f_tcpo_only) {
				stream << " tcpo_only";
			}
			if (option.flags & Flag::f_media_only) {
				stream << " media_only";
			}
			stream << '\n';
		}
	}
	return true;
}

} // namespace MTP
