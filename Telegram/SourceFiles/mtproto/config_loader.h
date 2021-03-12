/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"
#include "base/weak_ptr.h"
#include "base/bytes.h"
#include "mtproto/mtproto_response.h"

namespace MTP {

class Instance;

namespace details {

class SpecialConfigRequest;

class ConfigLoader : public base::has_weak_ptr {
public:
	ConfigLoader(
		not_null<Instance*> instance,
		const QString &phone,
		Fn<void(const MTPConfig &result)> onDone,
		FailHandler onFail);
	~ConfigLoader();

	void load();
	void setPhone(const QString &phone);

private:
	mtpRequestId sendRequest(ShiftedDcId shiftedDcId);
	void addSpecialEndpoint(
		DcId dcId,
		const std::string &ip,
		int port,
		bytes::const_span secret);
	void sendSpecialRequest();
	void enumerate();
	void refreshSpecialLoader();
	void createSpecialLoader();
	DcId specialToRealDcId(DcId specialDcId);
	void specialConfigLoaded(const MTPConfig &result);
	void terminateRequest();
	void terminateSpecialRequest();

	not_null<Instance*> _instance;
	base::Timer _enumDCTimer;
	DcId _enumCurrent = 0;
	mtpRequestId _enumRequest = 0;

	struct SpecialEndpoint {
		DcId dcId;
		std::string ip;
		int port;
		bytes::vector secret;
	};
	friend bool operator==(const SpecialEndpoint &a, const SpecialEndpoint &b);
	std::unique_ptr<SpecialConfigRequest> _specialLoader;
	std::vector<SpecialEndpoint> _specialEndpoints;
	std::vector<SpecialEndpoint> _triedSpecialEndpoints;
	base::Timer _specialEnumTimer;
	DcId _specialEnumCurrent = 0;
	mtpRequestId _specialEnumRequest = 0;
	QString _phone;

	Fn<void(const MTPConfig &result)> _doneHandler;
	FailHandler _failHandler;

};

inline bool operator==(const ConfigLoader::SpecialEndpoint &a, const ConfigLoader::SpecialEndpoint &b) {
	return (a.dcId == b.dcId) && (a.ip == b.ip) && (a.port == b.port);
}

} // namespace details
} // namespace MTP
