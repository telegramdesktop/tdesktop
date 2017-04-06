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

#include "base/observer.h"
#include <string>
#include <vector>
#include <map>

namespace MTP {

class DcOptions {
public:
	// construct methods don't notify "changed" subscribers.
	void constructFromSerialized(const QByteArray &serialized);
	void constructFromBuiltIn();
	void constructAddOne(int id, MTPDdcOption::Flags flags, const std::string &ip, int port);
	QByteArray serialize() const;

	using Ids = std::vector<DcId>;
	base::Observable<Ids> &changed() const {
		return _changed;
	}
	void setFromList(const MTPVector<MTPDcOption> &options);
	void addFromList(const MTPVector<MTPDcOption> &options);
	void addFromOther(const DcOptions &options);

	Ids sortedDcIds() const;
	DcId getDefaultDcId() const;

	struct Endpoint {
		std::string ip;
		int port = 0;
		MTPDdcOption::Flags flags = 0;
	};
	struct Variants {
		enum {
			IPv4 = 0,
			IPv6 = 1,
			AddressTypeCount = 2,
		};
		enum {
			Tcp = 0,
			Http = 1,
			ProtocolCount = 2,
		};
		Endpoint data[AddressTypeCount][ProtocolCount];
	};
	enum class DcType {
		Regular,
		MediaDownload,
	};
	Variants lookup(DcId dcId, DcType type) const;

	// Debug feature for now.
	bool loadFromFile(const QString &path);
	bool writeToFile(const QString &path) const;

private:
	struct Option {
		Option(DcId id, MTPDdcOption::Flags flags, const std::string &ip, int port) : id(id), flags(flags), ip(ip), port(port) {
		}

		DcId id;
		MTPDdcOption::Flags flags;
		std::string ip;
		int port;
	};
	bool applyOneGuarded(DcId dcId, MTPDdcOption::Flags flags, const std::string &ip, int port);

	void processFromList(const QVector<MTPDcOption> &options, bool overwrite);

	std::map<int, Option> _data;
	mutable QReadWriteLock _mutex;

	mutable base::Observable<Ids> _changed;

	// True when we have overriden options from a .tdesktop-endpoints file.
	bool _immutable = false;

};

} // namespace MTP
