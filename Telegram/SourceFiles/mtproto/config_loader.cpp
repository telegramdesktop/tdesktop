/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "mtproto/config_loader.h"

#include "base/openssl_help.h"
#include "mtproto/special_config_request.h"
#include "mtproto/facade.h"
#include "mtproto/mtproto_dc_options.h"
#include "mtproto/mtproto_config.h"
#include "mtproto/mtp_instance.h"

namespace MTP {
namespace details {
namespace {

constexpr auto kEnumerateDcTimeout = 8000; // 8 seconds timeout for help_getConfig to work (then move to other dc)
constexpr auto kSpecialRequestTimeoutMs = 6000; // 4 seconds timeout for it to work in a specially requested dc.

} // namespace

ConfigLoader::ConfigLoader(
	not_null<Instance*> instance,
	const QString &phone,
	Fn<void(const MTPConfig &result)> onDone,
	FailHandler onFail,
	bool proxyEnabled)
: _instance(instance)
, _phone(phone)
, _proxyEnabled(proxyEnabled)
, _doneHandler(onDone)
, _failHandler(onFail) {
	_enumDCTimer.setCallback([this] { enumerate(); });
	_specialEnumTimer.setCallback([this] { sendSpecialRequest(); });
}

void ConfigLoader::load() {
	if (!_instance->isKeysDestroyer()) {
		sendRequest(_instance->mainDcId());
		_enumDCTimer.callOnce(kEnumerateDcTimeout);
	} else {
		auto ids = _instance->dcOptions().configEnumDcIds();
		Assert(!ids.empty());
		_enumCurrent = ids.front();
		enumerate();
	}
}

mtpRequestId ConfigLoader::sendRequest(ShiftedDcId shiftedDcId) {
	auto done = [done = _doneHandler](const Response &response) {
		auto from = response.reply.constData();
		auto result = MTPConfig();
		if (!result.read(from, from + response.reply.size())) {
			return false;
		}
		done(result);
		return true;
	};
	return _instance->send(
		MTPhelp_GetConfig(),
		std::move(done),
		base::duplicate(_failHandler),
		shiftedDcId);
}

DcId ConfigLoader::specialToRealDcId(DcId specialDcId) {
	return getTemporaryIdFromRealDcId(specialDcId);
}

void ConfigLoader::terminateRequest() {
	if (_enumRequest) {
		_instance->cancel(base::take(_enumRequest));
	}
	if (_enumCurrent) {
		_instance->killSession(MTP::configDcId(_enumCurrent));
	}
}

void ConfigLoader::terminateSpecialRequest() {
	if (_specialEnumRequest) {
		_instance->cancel(base::take(_specialEnumRequest));
	}
	if (_specialEnumCurrent) {
		_instance->killSession(_specialEnumCurrent);
	}
}

ConfigLoader::~ConfigLoader() {
	terminateRequest();
	terminateSpecialRequest();
}

void ConfigLoader::enumerate() {
	terminateRequest();
	if (!_enumCurrent) {
		_enumCurrent = _instance->mainDcId();
	}
	auto ids = _instance->dcOptions().configEnumDcIds();
	Assert(!ids.empty());

	auto i = std::find(ids.cbegin(), ids.cend(), _enumCurrent);
	if (i == ids.cend() || (++i) == ids.cend()) {
		_enumCurrent = ids.front();
	} else {
		_enumCurrent = *i;
	}
	_enumRequest = sendRequest(MTP::configDcId(_enumCurrent));

	_enumDCTimer.callOnce(kEnumerateDcTimeout);

	refreshSpecialLoader();
}

void ConfigLoader::refreshSpecialLoader() {
	if (_proxyEnabled) {
		_specialLoader.reset();
		return;
	}
	if (!_specialLoader
		|| (!_specialEnumRequest && _specialEndpoints.empty())) {
		createSpecialLoader();
	}
}

void ConfigLoader::setPhone(const QString &phone) {
	if (_phone != phone) {
		_phone = phone;
		if (_specialLoader) {
			createSpecialLoader();
		}
	}
}

void ConfigLoader::createSpecialLoader() {
	_triedSpecialEndpoints.clear();
	_specialLoader = std::make_unique<SpecialConfigRequest>([=](
			DcId dcId,
			const std::string &ip,
			int port,
			bytes::const_span secret) {
		if (ip.empty()) {
			_specialLoader = nullptr;
		} else {
			addSpecialEndpoint(dcId, ip, port, secret);
		}
	}, _instance->configValues().txtDomainString, _phone);
}

void ConfigLoader::addSpecialEndpoint(
		DcId dcId,
		const std::string &ip,
		int port,
		bytes::const_span secret) {
	const auto endpoint = SpecialEndpoint {
		dcId,
		ip,
		port,
		bytes::make_vector(secret)
	};
	if (base::contains(_specialEndpoints, endpoint)
		|| base::contains(_triedSpecialEndpoints, endpoint)) {
		return;
	}
	DEBUG_LOG(("MTP Info: Special endpoint received, '%1:%2'").arg(ip.c_str()).arg(port));
	_specialEndpoints.push_back(endpoint);

	if (!_specialEnumTimer.isActive()) {
		_specialEnumTimer.callOnce(1);
	}
}

void ConfigLoader::sendSpecialRequest() {
	terminateSpecialRequest();
	if (_proxyEnabled) {
		_specialLoader.reset();
		return;
	}
	if (_specialEndpoints.empty()) {
		refreshSpecialLoader();
		return;
	}

	const auto weak = base::make_weak(this);
	const auto index = openssl::RandomValue<uint32>()
		% _specialEndpoints.size();
	const auto endpoint = _specialEndpoints.begin() + index;
	_specialEnumCurrent = specialToRealDcId(endpoint->dcId);

	using Flag = MTPDdcOption::Flag;
	const auto flags = Flag::f_tcpo_only
		| (endpoint->secret.empty() ? Flag(0) : Flag::f_secret);
	_instance->dcOptions().constructAddOne(
		_specialEnumCurrent,
		flags,
		endpoint->ip,
		endpoint->port,
		endpoint->secret);
	_specialEnumRequest = _instance->send(
		MTPhelp_GetConfig(),
		[weak](const Response &response) {
			auto result = MTPConfig();
			auto from = response.reply.constData();
			if (!result.read(from, from + response.reply.size())) {
				return false;
			}
			if (const auto strong = weak.get()) {
				strong->specialConfigLoaded(result);
			}
			return true;
		},
		base::duplicate(_failHandler),
		_specialEnumCurrent);
	_triedSpecialEndpoints.push_back(*endpoint);
	_specialEndpoints.erase(endpoint);

	_specialEnumTimer.callOnce(kSpecialRequestTimeoutMs);
}

void ConfigLoader::specialConfigLoaded(const MTPConfig &result) {
	Expects(result.type() == mtpc_config);

	const auto &data = result.c_config();
	if (data.vdc_options().v.empty()) {
		LOG(("MTP Error: config with empty dc_options received!"));
		return;
	}

	// We use special config only for dc options.
	// For everything else we wait for normal config from main dc.
	_instance->dcOptions().setFromList(data.vdc_options());
}

void ConfigLoader::setProxyEnabled(bool value) {
	_proxyEnabled = value;
}

} // namespace details
} // namespace MTP
