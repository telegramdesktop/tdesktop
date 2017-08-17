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
#pragma once

#include "base/timer.h"
#include "base/weak_unique_ptr.h"
#include "mtproto/rpc_sender.h"

namespace MTP {

class SpecialConfigRequest;
class Instance;

namespace internal {

class ConfigLoader : public base::enable_weak_from_this {
public:
	ConfigLoader(not_null<Instance*> instance, RPCDoneHandlerPtr onDone, RPCFailHandlerPtr onFail);
	~ConfigLoader();

	void load();

private:
	mtpRequestId sendRequest(ShiftedDcId shiftedDcId);
	void addSpecialEndpoint(DcId dcId, const std::string &ip, int port);
	void sendSpecialRequest();
	void enumerate();
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
	};
	friend bool operator==(const SpecialEndpoint &a, const SpecialEndpoint &b);
	std::unique_ptr<SpecialConfigRequest> _specialLoader;
	std::vector<SpecialEndpoint> _specialEndpoints;
	std::vector<SpecialEndpoint> _triedSpecialEndpoints;
	base::Timer _specialEnumTimer;
	DcId _specialEnumCurrent = 0;
	mtpRequestId _specialEnumRequest = 0;

	RPCDoneHandlerPtr _doneHandler;
	RPCFailHandlerPtr _failHandler;

};

inline bool operator==(const ConfigLoader::SpecialEndpoint &a, const ConfigLoader::SpecialEndpoint &b) {
	return (a.dcId == b.dcId) && (a.ip == b.ip) && (a.port == b.port);
}

} // namespace internal
} // namespace MTP
