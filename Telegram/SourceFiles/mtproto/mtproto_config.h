/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "mtproto/mtproto_dc_options.h"

namespace MTP {

struct ConfigFields {
	int chatSizeMax = 200;
	int megagroupSizeMax = 10000;
	int forwardedCountMax = 100;
	int onlineUpdatePeriod = 120000;
	int offlineBlurTimeout = 5000;
	int offlineIdleTimeout = 30000;
	int onlineFocusTimeout = 1000; // Not from the server config.
	int onlineCloudTimeout = 300000;
	int notifyCloudDelay = 30000;
	int notifyDefaultDelay = 1500;
	int savedGifsLimit = 200;
	int editTimeLimit = 172800;
	int revokeTimeLimit = 172800;
	int revokePrivateTimeLimit = 172800;
	bool revokePrivateInbox = false;
	int stickersRecentLimit = 30;
	int stickersFavedLimit = 5;
	rpl::variable<int> pinnedDialogsCountMax = 5;
	rpl::variable<int> pinnedDialogsInFolderMax = 100;
	QString internalLinksDomain = u"https://t.me/"_q;
	int channelsReadMediaPeriod = 86400 * 7;
	int callReceiveTimeoutMs = 20000;
	int callRingTimeoutMs = 90000;
	int callConnectTimeoutMs = 30000;
	int callPacketTimeoutMs = 10000;
	int webFileDcId = 4;
	QString txtDomainString;
	rpl::variable<bool> phoneCallsEnabled = true;
	bool blockedMode = false;
	int captionLengthMax = 1024;
};

class Config final {
	struct PrivateTag {
	};

public:
	explicit Config(Environment environment);
	Config(const Config &other);

	[[nodiscard]] QByteArray serialize() const;
	[[nodiscard]] static std::unique_ptr<Config> FromSerialized(
		const QByteArray &serialized);

	[[nodiscard]] DcOptions &dcOptions() {
		return _dcOptions;
	}
	[[nodiscard]] const DcOptions &dcOptions() const {
		return _dcOptions;
	}
	[[nodiscard]] MTP::Environment environment() const {
		return _dcOptions.environment();
	}
	[[nodiscard]] bool isTestMode() const {
		return _dcOptions.isTestMode();
	}

	void apply(const MTPDconfig &data);

	[[nodiscard]] const ConfigFields &values() const;
	[[nodiscard]] rpl::producer<> updates() const;

	// Set from legacy local stored values.
	void setChatSizeMax(int value);
	void setSavedGifsLimit(int value);
	void setStickersRecentLimit(int value);
	void setStickersFavedLimit(int value);
	void setMegagroupSizeMax(int value);
	void setTxtDomainString(const QString &value);

private:
	DcOptions _dcOptions;
	ConfigFields _fields;

	rpl::event_stream<> _updates;

};

} // namespace MTP