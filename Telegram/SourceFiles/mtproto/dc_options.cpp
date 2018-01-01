/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#include "mtproto/dc_options.h"

#include "storage/serialize_common.h"

namespace MTP {
namespace {

const char *(PublicRSAKeys[]) = { "\
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
	WriteLocker(DcOptions *that) : _that(that), _lock(&_that->_useThroughLockers) {
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
	ReadLocker(const DcOptions *that) : _lock(&that->_useThroughLockers) {
	}

private:
	QReadLocker _lock;

};

void DcOptions::readBuiltInPublicKeys() {
	for (const auto key : PublicRSAKeys) {
		const auto keyBytes = gsl::make_span(key, key + strlen(key));
		auto parsed = internal::RSAPublicKey(gsl::as_bytes(keyBytes));
		if (parsed.isValid()) {
			_publicKeys.emplace(parsed.getFingerPrint(), std::move(parsed));
		} else {
			LOG(("MTP Error: could not read this public RSA key:"));
			LOG((key));
		}
	}
}

void DcOptions::constructFromBuiltIn() {
	WriteLocker lock(this);
	_data.clear();

	readBuiltInPublicKeys();

	auto bdcs = builtInDcs();
	for (auto i = 0, l = builtInDcsCount(); i != l; ++i) {
		auto flags = MTPDdcOption::Flags(0);
		auto idWithShift = MTP::shiftDcId(bdcs[i].id, flags);
		_data.emplace(idWithShift, Option(bdcs[i].id, flags, bdcs[i].ip, bdcs[i].port));
		DEBUG_LOG(("MTP Info: adding built in DC %1 connect option: %2:%3").arg(bdcs[i].id).arg(bdcs[i].ip).arg(bdcs[i].port));
	}

	auto bdcsipv6 = builtInDcsIPv6();
	for (auto i = 0, l = builtInDcsCountIPv6(); i != l; ++i) {
		auto flags = MTPDdcOption::Flags(MTPDdcOption::Flag::f_ipv6);
		auto idWithShift = MTP::shiftDcId(bdcsipv6[i].id, flags);
		_data.emplace(idWithShift, Option(bdcsipv6[i].id, flags, bdcsipv6[i].ip, bdcsipv6[i].port));
		DEBUG_LOG(("MTP Info: adding built in DC %1 IPv6 connect option: %2:%3").arg(bdcsipv6[i].id).arg(bdcsipv6[i].ip).arg(bdcsipv6[i].port));
	}
}

void DcOptions::processFromList(const QVector<MTPDcOption> &options, bool overwrite) {
	if (options.empty() || _immutable) {
		return;
	}

	auto idsChanged = std::vector<DcId>();
	idsChanged.reserve(options.size());
	auto shiftedIdsProcessed = std::vector<ShiftedDcId>();
	shiftedIdsProcessed.reserve(options.size());
	{
		WriteLocker lock(this);
		if (overwrite) {
			idsChanged.reserve(_data.size());
		}
		for (auto &mtpOption : options) {
			if (mtpOption.type() != mtpc_dcOption) {
				LOG(("Wrong type in DcOptions: %1").arg(mtpOption.type()));
				continue;
			}

			auto &option = mtpOption.c_dcOption();
			auto dcId = option.vid.v;
			auto flags = option.vflags.v;
			auto dcIdWithShift = MTP::shiftDcId(dcId, flags);
			if (base::contains(shiftedIdsProcessed, dcIdWithShift)) {
				continue;
			}

			shiftedIdsProcessed.push_back(dcIdWithShift);
			auto ip = std::string(option.vip_address.v.constData(), option.vip_address.v.size());
			auto port = option.vport.v;
			if (applyOneGuarded(dcId, flags, ip, port)) {
				if (!base::contains(idsChanged, dcId)) {
					idsChanged.push_back(dcId);
				}
			}
		}
		if (overwrite && shiftedIdsProcessed.size() < _data.size()) {
			for (auto i = _data.begin(); i != _data.end();) {
				if (base::contains(shiftedIdsProcessed, i->first)) {
					++i;
				} else {
					if (!base::contains(idsChanged, i->second.id)) {
						idsChanged.push_back(i->second.id);
					}
					i = _data.erase(i);
				}
			}
		}
	}

	if (!idsChanged.empty()) {
		_changed.notify(std::move(idsChanged));
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
			for (auto &item : base::take(options._data)) {
				auto dcId = item.second.id;
				auto flags = item.second.flags;
				auto &ip = item.second.ip;
				auto port = item.second.port;
				if (applyOneGuarded(dcId, flags, ip, port)) {
					if (!base::contains(idsChanged, dcId)) {
						idsChanged.push_back(dcId);
					}
				}
			}
			for (auto &keysForDc : options._cdnPublicKeys) {
				for (auto &entry : keysForDc.second) {
					_cdnPublicKeys[keysForDc.first].insert(std::move(entry));
				}
			}
		}
	}

	if (!idsChanged.empty()) {
		_changed.notify(std::move(idsChanged));
	}
}

void DcOptions::constructAddOne(int id, MTPDdcOption::Flags flags, const std::string &ip, int port) {
	WriteLocker lock(this);
	applyOneGuarded(bareDcId(id), flags, ip, port);
}

bool DcOptions::applyOneGuarded(DcId dcId, MTPDdcOption::Flags flags, const std::string &ip, int port) {
	auto dcIdWithShift = MTP::shiftDcId(dcId, flags);
	auto i = _data.find(dcIdWithShift);
	if (i != _data.cend()) {
		if (i->second.ip == ip && i->second.port == port) {
			return false;
		}
		i->second.ip = ip;
		i->second.port = port;
	} else {
		_data.emplace(dcIdWithShift, Option(dcId, flags, ip, port));
	}
	return true;
}

QByteArray DcOptions::serialize() const {
	if (_immutable) {
		// Don't write the overriden options to our settings.
		return DcOptions().serialize();
	}

	ReadLocker lock(this);

	auto size = sizeof(qint32);
	for (auto &item : _data) {
		if (isTemporaryDcId(item.first)) {
			continue;
		}
		size += sizeof(qint32) + sizeof(qint32) + sizeof(qint32); // id + flags + port
		size += sizeof(qint32) + item.second.ip.size();
	}

	auto count = 0;
	for (auto &keysInDc : _cdnPublicKeys) {
		count += keysInDc.second.size();
	}
	struct SerializedPublicKey {
		DcId dcId;
		base::byte_vector n;
		base::byte_vector e;
	};
	std::vector<SerializedPublicKey> publicKeys;
	publicKeys.reserve(count);
	for (auto &keysInDc : _cdnPublicKeys) {
		for (auto &entry : keysInDc.second) {
			publicKeys.push_back({ keysInDc.first, entry.second.getN(), entry.second.getE() });
			size += sizeof(qint32) + Serialize::bytesSize(publicKeys.back().n) + Serialize::bytesSize(publicKeys.back().e);
		}
	}

	auto result = QByteArray();
	result.reserve(size);
	{
		QDataStream stream(&result, QIODevice::WriteOnly);
		stream.setVersion(QDataStream::Qt_5_1);
		stream << qint32(_data.size());
		for (auto &item : _data) {
			if (isTemporaryDcId(item.first)) {
				continue;
			}
			stream << qint32(item.second.id) << qint32(item.second.flags) << qint32(item.second.port);
			stream << qint32(item.second.ip.size());
			stream.writeRawData(item.second.ip.data(), item.second.ip.size());
		}
		stream << qint32(publicKeys.size());
		for (auto &key : publicKeys) {
			stream << qint32(key.dcId) << Serialize::bytes(key.n) << Serialize::bytes(key.e);
		}
	}
	return result;
}

void DcOptions::constructFromSerialized(const QByteArray &serialized) {
	QDataStream stream(serialized);
	stream.setVersion(QDataStream::Qt_5_1);
	auto count = qint32(0);
	stream >> count;
	if (stream.status() != QDataStream::Ok) {
		LOG(("MTP Error: Bad data for DcOptions::constructFromSerialized()"));
		return;
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
			return;
		}

		std::string ip(ipSize, ' ');
		stream.readRawData(&ip[0], ipSize);

		if (stream.status() != QDataStream::Ok) {
			LOG(("MTP Error: Bad data inside DcOptions::constructFromSerialized()"));
			return;
		}

		applyOneGuarded(DcId(id), MTPDdcOption::Flags::from_raw(flags), ip, port);
	}

	// Read CDN config
	if (!stream.atEnd()) {
		auto count = qint32(0);
		stream >> count;
		if (stream.status() != QDataStream::Ok) {
			LOG(("MTP Error: Bad data for CDN config in DcOptions::constructFromSerialized()"));
			return;
		}

		for (auto i = 0; i != count; ++i) {
			qint32 dcId = 0;
			base::byte_vector n, e;
			stream >> dcId >> Serialize::bytes(n) >> Serialize::bytes(e);
			if (stream.status() != QDataStream::Ok) {
				LOG(("MTP Error: Bad data for CDN config inside DcOptions::constructFromSerialized()"));
				return;
			}

			auto key = internal::RSAPublicKey(n, e);
			if (key.isValid()) {
				_cdnPublicKeys[dcId].emplace(key.getFingerPrint(), std::move(key));
			} else {
				LOG(("MTP Error: Could not read valid CDN public key."));
			}
		}
	}
}

DcOptions::Ids DcOptions::configEnumDcIds() const {
	auto result = Ids();
	{
		ReadLocker lock(this);
		result.reserve(_data.size());
		for (auto &item : _data) {
			if (!isCdnDc(item.second.flags)
				&& !isTemporaryDcId(item.first)
				&& !base::contains(result, item.second.id)) {
				result.push_back(item.second.id);
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
	if (_cdnDcIds.find(bareDcId(shiftedDcId)) != _cdnDcIds.cend()) {
		return DcType::Cdn;
	}
	if (isDownloadDcId(shiftedDcId)) {
		return DcType::MediaDownload;
	}
	return DcType::Regular;
}

void DcOptions::setCDNConfig(const MTPDcdnConfig &config) {
	WriteLocker lock(this);
	_cdnPublicKeys.clear();
	for_const (auto &publicKey, config.vpublic_keys.v) {
		Expects(publicKey.type() == mtpc_cdnPublicKey);
		const auto &keyData = publicKey.c_cdnPublicKey();
		const auto keyBytes = gsl::make_span(keyData.vpublic_key.v);
		auto key = internal::RSAPublicKey(gsl::as_bytes(keyBytes));
		if (key.isValid()) {
			_cdnPublicKeys[keyData.vdc_id.v].emplace(
				key.getFingerPrint(),
				std::move(key));
		} else {
			LOG(("MTP Error: could not read this public RSA key:"));
			LOG((qs(keyData.vpublic_key)));
		}
	}
}

bool DcOptions::hasCDNKeysForDc(DcId dcId) const {
	ReadLocker lock(this);
	return _cdnPublicKeys.find(dcId) != _cdnPublicKeys.cend();
}

bool DcOptions::getDcRSAKey(DcId dcId, const QVector<MTPlong> &fingerprints, internal::RSAPublicKey *result) const {
	auto findKey = [&fingerprints, &result](const std::map<uint64, internal::RSAPublicKey> &keys) {
		for_const (auto &fingerprint, fingerprints) {
			auto it = keys.find(static_cast<uint64>(fingerprint.v));
			if (it != keys.cend()) {
				*result = it->second;
				return true;
			}
		}
		return false;
	};
	{
		ReadLocker lock(this);
		auto it = _cdnPublicKeys.find(dcId);
		if (it != _cdnPublicKeys.cend()) {
			return findKey(it->second);
		}
	}
	return findKey(_publicKeys);
}

DcOptions::Variants DcOptions::lookup(DcId dcId, DcType type) const {
	auto lookupDesiredFlags = [type](int address, int protocol) -> std::vector<MTPDdcOption::Flags> {
		auto throughProxy = (Global::ConnectionType() != dbictAuto);

		switch (type) {
		case DcType::Regular:
		case DcType::Temporary: {
			switch (address) {
			case Variants::IPv4: {
				switch (protocol) {
				case Variants::Tcp: return {
					// Regular TCP IPv4
					throughProxy ? (MTPDdcOption::Flag::f_tcpo_only | MTPDdcOption::Flag::f_static) : (MTPDdcOption::Flag::f_tcpo_only | 0),
					throughProxy ? (MTPDdcOption::Flag::f_static | 0) : MTPDdcOption::Flags(0),
					(MTPDdcOption::Flag::f_tcpo_only | 0),
					0
				};
				case Variants::Http: return {
					// Regular HTTP IPv4
					throughProxy ? (MTPDdcOption::Flag::f_static | 0) : MTPDdcOption::Flags(0),
					0,
				};
				}
			} break;
			case Variants::IPv6: {
				switch (protocol) {
				case Variants::Tcp: return {
					// Regular TCP IPv6
					throughProxy ? (MTPDdcOption::Flag::f_tcpo_only | MTPDdcOption::Flag::f_ipv6 | MTPDdcOption::Flag::f_static) : (MTPDdcOption::Flag::f_tcpo_only | MTPDdcOption::Flag::f_ipv6),
					throughProxy ? (MTPDdcOption::Flag::f_ipv6 | MTPDdcOption::Flag::f_static) : (MTPDdcOption::Flag::f_ipv6 | 0),
					(MTPDdcOption::Flag::f_tcpo_only | MTPDdcOption::Flag::f_ipv6),
					(MTPDdcOption::Flag::f_ipv6 | 0),
				};
				case Variants::Http: return {
					// Regular HTTP IPv6
					throughProxy ? (MTPDdcOption::Flag::f_ipv6 | MTPDdcOption::Flag::f_static) : (MTPDdcOption::Flag::f_ipv6 | 0),
					(MTPDdcOption::Flag::f_ipv6 | 0),
				};
				}
			} break;
			}
		} break;
		case DcType::MediaDownload: {
			switch (address) {
			case Variants::IPv4: {
				switch (protocol) {
				case Variants::Tcp: return {
					// Media download TCP IPv4
					throughProxy ? (MTPDdcOption::Flag::f_media_only | MTPDdcOption::Flag::f_tcpo_only | MTPDdcOption::Flag::f_static) : (MTPDdcOption::Flag::f_media_only | MTPDdcOption::Flag::f_tcpo_only),
					throughProxy ? (MTPDdcOption::Flag::f_media_only | MTPDdcOption::Flag::f_static) : (MTPDdcOption::Flag::f_tcpo_only | 0),
					(MTPDdcOption::Flag::f_media_only | 0),
					throughProxy ? (MTPDdcOption::Flag::f_static | 0) : MTPDdcOption::Flags(0),
					(MTPDdcOption::Flag::f_media_only | MTPDdcOption::Flag::f_tcpo_only),
					throughProxy ? (MTPDdcOption::Flag::f_tcpo_only | MTPDdcOption::Flag::f_static) : (MTPDdcOption::Flag::f_tcpo_only | 0),
					throughProxy ? (MTPDdcOption::Flag::f_tcpo_only | 0) : (MTPDdcOption::Flag::f_media_only | 0),
					0,
				};
				case Variants::Http: return {
					// Media download HTTP IPv4
					throughProxy ? (MTPDdcOption::Flag::f_media_only | MTPDdcOption::Flag::f_static) : (MTPDdcOption::Flag::f_media_only | 0),
					(MTPDdcOption::Flag::f_media_only | 0),
					throughProxy ? (MTPDdcOption::Flag::f_static | 0) : MTPDdcOption::Flags(0),
					0,
				};
				}
			} break;
			case Variants::IPv6: {
				switch (protocol) {
				case Variants::Tcp: return {
					// Media download TCP IPv6
					throughProxy ? (MTPDdcOption::Flag::f_media_only | MTPDdcOption::Flag::f_tcpo_only | MTPDdcOption::Flag::f_ipv6 | MTPDdcOption::Flag::f_static) : (MTPDdcOption::Flag::f_media_only | MTPDdcOption::Flag::f_tcpo_only | MTPDdcOption::Flag::f_ipv6),
					throughProxy ? (MTPDdcOption::Flag::f_media_only | MTPDdcOption::Flag::f_ipv6 | MTPDdcOption::Flag::f_static) : (MTPDdcOption::Flag::f_tcpo_only | MTPDdcOption::Flag::f_ipv6),
					(MTPDdcOption::Flag::f_media_only | MTPDdcOption::Flag::f_ipv6),
					throughProxy ? (MTPDdcOption::Flag::f_ipv6 | MTPDdcOption::Flag::f_static) : (MTPDdcOption::Flag::f_ipv6 | 0),
					(MTPDdcOption::Flag::f_media_only | MTPDdcOption::Flag::f_tcpo_only | MTPDdcOption::Flag::f_ipv6),
					throughProxy ? (MTPDdcOption::Flag::f_tcpo_only | MTPDdcOption::Flag::f_ipv6 | MTPDdcOption::Flag::f_static) : (MTPDdcOption::Flag::f_tcpo_only | MTPDdcOption::Flag::f_ipv6),
					throughProxy ? (MTPDdcOption::Flag::f_tcpo_only | MTPDdcOption::Flag::f_ipv6) : (MTPDdcOption::Flag::f_media_only | MTPDdcOption::Flag::f_ipv6),
					(MTPDdcOption::Flag::f_ipv6 | 0)
				};
				case Variants::Http: return {
					// Media download HTTP IPv6
					throughProxy ? (MTPDdcOption::Flag::f_media_only | MTPDdcOption::Flag::f_ipv6 | MTPDdcOption::Flag::f_static) : (MTPDdcOption::Flag::f_media_only | MTPDdcOption::Flag::f_ipv6),
					(MTPDdcOption::Flag::f_media_only | MTPDdcOption::Flag::f_ipv6),
					throughProxy ? (MTPDdcOption::Flag::f_ipv6 | MTPDdcOption::Flag::f_static) : (MTPDdcOption::Flag::f_ipv6 | 0),
					(MTPDdcOption::Flag::f_ipv6 | 0),
				};
				}
			} break;
			}
		} break;
		case DcType::Cdn: {
			switch (address) {
			case Variants::IPv4: {
				switch (protocol) {
				case Variants::Tcp: return {
					// CDN TCP IPv4
					throughProxy ? (MTPDdcOption::Flag::f_cdn | MTPDdcOption::Flag::f_tcpo_only | MTPDdcOption::Flag::f_static) : (MTPDdcOption::Flag::f_cdn | MTPDdcOption::Flag::f_tcpo_only),
					throughProxy ? (MTPDdcOption::Flag::f_cdn | MTPDdcOption::Flag::f_static) : (MTPDdcOption::Flag::f_cdn | 0),
					(MTPDdcOption::Flag::f_cdn | MTPDdcOption::Flag::f_tcpo_only),
					(MTPDdcOption::Flag::f_cdn | 0),
				};
				case Variants::Http: return {
					// CDN HTTP IPv4
					throughProxy ? (MTPDdcOption::Flag::f_cdn | MTPDdcOption::Flag::f_static) : (MTPDdcOption::Flag::f_cdn | 0),
					(MTPDdcOption::Flag::f_cdn | 0),
				};
				}
			} break;
			case Variants::IPv6: {
				switch (protocol) {
				case Variants::Tcp: return {
					// CDN TCP IPv6
					throughProxy ? (MTPDdcOption::Flag::f_cdn | MTPDdcOption::Flag::f_ipv6 | MTPDdcOption::Flag::f_tcpo_only | MTPDdcOption::Flag::f_static) : (MTPDdcOption::Flag::f_cdn | MTPDdcOption::Flag::f_ipv6 | MTPDdcOption::Flag::f_tcpo_only),
					throughProxy ? (MTPDdcOption::Flag::f_cdn | MTPDdcOption::Flag::f_ipv6 | MTPDdcOption::Flag::f_static) : (MTPDdcOption::Flag::f_cdn | MTPDdcOption::Flag::f_ipv6),
					(MTPDdcOption::Flag::f_cdn | MTPDdcOption::Flag::f_tcpo_only | MTPDdcOption::Flag::f_ipv6),
					(MTPDdcOption::Flag::f_cdn | MTPDdcOption::Flag::f_ipv6),
				};
				case Variants::Http: return {
					// CDN HTTP IPv6
					throughProxy ? (MTPDdcOption::Flag::f_cdn | MTPDdcOption::Flag::f_ipv6 | MTPDdcOption::Flag::f_static) : (MTPDdcOption::Flag::f_cdn | MTPDdcOption::Flag::f_ipv6),
					(MTPDdcOption::Flag::f_cdn | MTPDdcOption::Flag::f_ipv6),
				};
				}
			} break;
			}
		} break;
		}
		Unexpected("Bad type / address / protocol");
	};

	auto result = Variants();
	{
		ReadLocker lock(this);
		for (auto address = 0; address != Variants::AddressTypeCount; ++address) {
			for (auto protocol = 0; protocol != Variants::ProtocolCount; ++protocol) {
				auto desiredFlags = lookupDesiredFlags(address, protocol);
				for (auto flags : desiredFlags) {
					auto shift = static_cast<int>(flags);
					if (shift < 0) continue;

					auto it = _data.find(shiftDcId(dcId, shift));
					if (it != _data.cend()) {
						result.data[address][protocol].ip = it->second.ip;
						result.data[address][protocol].flags = it->second.flags;
						result.data[address][protocol].port = it->second.port;
						break;
					}
				}
			}
		}
	}
	return result;
}

void DcOptions::computeCdnDcIds() {
	_cdnDcIds.clear();
	for (auto &item : _data) {
		if (item.second.flags & MTPDdcOption::Flag::f_cdn) {
			_cdnDcIds.insert(item.second.id);
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
		auto components = line.split(QRegularExpression(R"(\s)"), QString::SkipEmptyParts);
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
		if (dcId <= 0 || dcId >= internal::kDcShift || !host.setAddress(ip) || port <= 0) {
			return error();
		}
		auto flags = MTPDdcOption::Flags(0);
		if (host.protocol() == QAbstractSocket::IPv6Protocol) {
			flags |= MTPDdcOption::Flag::f_ipv6;
		}
		for (auto &option : components.mid(3)) {
			if (option.startsWith('#')) {
				break;
			} else if (option == qstr("tcpo_only")) {
				flags |= MTPDdcOption::Flag::f_tcpo_only;
			} else if (option == qstr("media_only")) {
				flags |= MTPDdcOption::Flag::f_media_only;
			} else {
				return error();
			}
		}
		options.push_back(MTP_dcOption(MTP_flags(flags), MTP_int(dcId), MTP_string(ip), MTP_int(port)));
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
	for (auto &item : _data) {
		auto &endpoint = item.second;
		stream << endpoint.id << ' ' << QString::fromStdString(endpoint.ip) << ' ' << endpoint.port;
		if (endpoint.flags & MTPDdcOption::Flag::f_tcpo_only) {
			stream << " tcpo_only";
		}
		if (endpoint.flags & MTPDdcOption::Flag::f_media_only) {
			stream << " media_only";
		}
		stream << '\n';
	}
	return true;
}

} // namespace MTP
