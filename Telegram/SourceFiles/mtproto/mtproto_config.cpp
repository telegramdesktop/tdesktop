/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "mtproto/mtproto_config.h"

#include "storage/serialize_common.h"
#include "mtproto/type_utils.h"
#include "logs.h"

namespace MTP {
namespace {

constexpr auto kVersion = 1;

}
Config::Config(Environment environment) : _dcOptions(environment) {
	_fields.webFileDcId = _dcOptions.isTestMode() ? 2 : 4;
	_fields.txtDomainString = _dcOptions.isTestMode()
		? u"tapv3.stel.com"_q
		: u"apv3.stel.com"_q;
}

Config::Config(const Config &other)
: _dcOptions(other.dcOptions())
, _fields(other._fields) {
}

QByteArray Config::serialize() const {
	auto options = _dcOptions.serialize();
	auto size = sizeof(qint32) * 2; // version + environment
	size += Serialize::bytearraySize(options);
	size += 28 * sizeof(qint32);
	size += Serialize::stringSize(_fields.internalLinksDomain);
	size += Serialize::stringSize(_fields.txtDomainString);

	auto result = QByteArray();
	result.reserve(size);
	{
		QDataStream stream(&result, QIODevice::WriteOnly);
		stream.setVersion(QDataStream::Qt_5_1);
		stream
			<< qint32(kVersion)
			<< qint32(_dcOptions.isTestMode()
				? Environment::Test
				: Environment::Production)
			<< options
			<< qint32(_fields.chatSizeMax)
			<< qint32(_fields.megagroupSizeMax)
			<< qint32(_fields.forwardedCountMax)
			<< qint32(_fields.onlineUpdatePeriod)
			<< qint32(_fields.offlineBlurTimeout)
			<< qint32(_fields.offlineIdleTimeout)
			<< qint32(_fields.onlineFocusTimeout)
			<< qint32(_fields.onlineCloudTimeout)
			<< qint32(_fields.notifyCloudDelay)
			<< qint32(_fields.notifyDefaultDelay)
			<< qint32(_fields.savedGifsLimit)
			<< qint32(_fields.editTimeLimit)
			<< qint32(_fields.revokeTimeLimit)
			<< qint32(_fields.revokePrivateTimeLimit)
			<< qint32(_fields.revokePrivateInbox ? 1 : 0)
			<< qint32(_fields.stickersRecentLimit)
			<< qint32(_fields.stickersFavedLimit)
			<< qint32(_fields.pinnedDialogsCountMax.current())
			<< qint32(_fields.pinnedDialogsInFolderMax.current())
			<< _fields.internalLinksDomain
			<< qint32(_fields.channelsReadMediaPeriod)
			<< qint32(_fields.callReceiveTimeoutMs)
			<< qint32(_fields.callRingTimeoutMs)
			<< qint32(_fields.callConnectTimeoutMs)
			<< qint32(_fields.callPacketTimeoutMs)
			<< qint32(_fields.webFileDcId)
			<< _fields.txtDomainString
			<< qint32(_fields.phoneCallsEnabled.current() ? 1 : 0)
			<< qint32(_fields.blockedMode ? 1 : 0)
			<< qint32(_fields.captionLengthMax);
	}
	return result;
}

std::unique_ptr<Config> Config::FromSerialized(const QByteArray &serialized) {
	auto result = std::unique_ptr<Config>();
	auto raw = result.get();

	QDataStream stream(serialized);
	stream.setVersion(QDataStream::Qt_5_1);

	auto version = qint32();
	stream >> version;
	if (version != kVersion) {
		return result;
	}
	auto environment = qint32();
	stream >> environment;
	switch (environment) {
	case qint32(Environment::Test):
		result = std::make_unique<Config>(Environment::Test);
		break;
	case qint32(Environment::Production):
		result = std::make_unique<Config>(Environment::Production);
		break;
	}
	if (!(raw = result.get())) {
		return result;
	}

	auto dcOptionsSerialized = QByteArray();
	const auto read = [&](auto &field) {
		using Type = std::remove_reference_t<decltype(field)>;
		if constexpr (std::is_same_v<Type, int>
			|| std::is_same_v<Type, rpl::variable<int>>) {
			auto value = qint32();
			stream >> value;
			field = value;
		} else if constexpr (std::is_same_v<Type, bool>
			|| std::is_same_v<Type, rpl::variable<bool>>) {
			auto value = qint32();
			stream >> value;
			field = (value == 1);
		} else if constexpr (std::is_same_v<Type, QByteArray>
			|| std::is_same_v<Type, QString>) {
			stream >> field;
		} else {
			static_assert(false_(field), "Bad read() call.");
		}
	};

	read(dcOptionsSerialized);
	read(raw->_fields.chatSizeMax);
	read(raw->_fields.megagroupSizeMax);
	read(raw->_fields.forwardedCountMax);
	read(raw->_fields.onlineUpdatePeriod);
	read(raw->_fields.offlineBlurTimeout);
	read(raw->_fields.offlineIdleTimeout);
	read(raw->_fields.onlineFocusTimeout);
	read(raw->_fields.onlineCloudTimeout);
	read(raw->_fields.notifyCloudDelay);
	read(raw->_fields.notifyDefaultDelay);
	read(raw->_fields.savedGifsLimit);
	read(raw->_fields.editTimeLimit);
	read(raw->_fields.revokeTimeLimit);
	read(raw->_fields.revokePrivateTimeLimit);
	read(raw->_fields.revokePrivateInbox);
	read(raw->_fields.stickersRecentLimit);
	read(raw->_fields.stickersFavedLimit);
	read(raw->_fields.pinnedDialogsCountMax);
	read(raw->_fields.pinnedDialogsInFolderMax);
	read(raw->_fields.internalLinksDomain);
	read(raw->_fields.channelsReadMediaPeriod);
	read(raw->_fields.callReceiveTimeoutMs);
	read(raw->_fields.callRingTimeoutMs);
	read(raw->_fields.callConnectTimeoutMs);
	read(raw->_fields.callPacketTimeoutMs);
	read(raw->_fields.webFileDcId);
	read(raw->_fields.txtDomainString);
	read(raw->_fields.phoneCallsEnabled);
	read(raw->_fields.blockedMode);
	read(raw->_fields.captionLengthMax);

	if (stream.status() != QDataStream::Ok
		|| !raw->_dcOptions.constructFromSerialized(dcOptionsSerialized)) {
		return nullptr;
	}
	return result;
}

const ConfigFields &Config::values() const {
	return _fields;
}

void Config::apply(const MTPDconfig &data) {
	if (mtpIsTrue(data.vtest_mode()) != _dcOptions.isTestMode()) {
		LOG(("MTP Error: config with wrong test mode field received!"));
		return;
	}

	DEBUG_LOG(("MTP Info: got config, "
		"chat_size_max: %1, "
		"date: %2, "
		"test_mode: %3, "
		"this_dc: %4, "
		"dc_options.length: %5"
		).arg(data.vchat_size_max().v
		).arg(data.vdate().v
		).arg(mtpIsTrue(data.vtest_mode())
		).arg(data.vthis_dc().v
		).arg(data.vdc_options().v.size()));

	_fields.chatSizeMax = data.vchat_size_max().v;
	_fields.megagroupSizeMax = data.vmegagroup_size_max().v;
	_fields.forwardedCountMax = data.vforwarded_count_max().v;
	_fields.onlineUpdatePeriod = data.vonline_update_period_ms().v;
	_fields.offlineBlurTimeout = data.voffline_blur_timeout_ms().v;
	_fields.offlineIdleTimeout = data.voffline_idle_timeout_ms().v;
	_fields.onlineCloudTimeout = data.vonline_cloud_timeout_ms().v;
	_fields.notifyCloudDelay = data.vnotify_cloud_delay_ms().v;
	_fields.notifyDefaultDelay = data.vnotify_default_delay_ms().v;
	_fields.savedGifsLimit = data.vsaved_gifs_limit().v;
	_fields.editTimeLimit = data.vedit_time_limit().v;
	_fields.revokeTimeLimit = data.vrevoke_time_limit().v;
	_fields.revokePrivateTimeLimit = data.vrevoke_pm_time_limit().v;
	_fields.revokePrivateInbox = data.is_revoke_pm_inbox();
	_fields.stickersRecentLimit = data.vstickers_recent_limit().v;
	_fields.stickersFavedLimit = data.vstickers_faved_limit().v;
	_fields.pinnedDialogsCountMax =
		std::max(data.vpinned_dialogs_count_max().v, 1);
	_fields.pinnedDialogsInFolderMax =
		std::max(data.vpinned_infolder_count_max().v, 1);
	_fields.internalLinksDomain = qs(data.vme_url_prefix());
	_fields.channelsReadMediaPeriod = data.vchannels_read_media_period().v;
	_fields.webFileDcId = data.vwebfile_dc_id().v;
	_fields.callReceiveTimeoutMs = data.vcall_receive_timeout_ms().v;
	_fields.callRingTimeoutMs = data.vcall_ring_timeout_ms().v;
	_fields.callConnectTimeoutMs = data.vcall_connect_timeout_ms().v;
	_fields.callPacketTimeoutMs = data.vcall_packet_timeout_ms().v;
	_fields.phoneCallsEnabled = data.is_phonecalls_enabled();
	_fields.blockedMode = data.is_blocked_mode();
	_fields.captionLengthMax = data.vcaption_length_max().v;

	if (data.vdc_options().v.empty()) {
		LOG(("MTP Error: config with empty dc_options received!"));
	} else {
		dcOptions().setFromList(data.vdc_options());
	}

	_updates.fire({});
}

rpl::producer<> Config::updates() const {
	return _updates.events();
}

void Config::setChatSizeMax(int value) {
	_fields.chatSizeMax = value;
}

void Config::setSavedGifsLimit(int value) {
	_fields.savedGifsLimit = value;
}

void Config::setStickersRecentLimit(int value) {
	_fields.stickersRecentLimit = value;
}

void Config::setStickersFavedLimit(int value) {
	_fields.stickersFavedLimit = value;
}

void Config::setMegagroupSizeMax(int value) {
	_fields.megagroupSizeMax = value;
}

void Config::setTxtDomainString(const QString &value) {
	_fields.txtDomainString = value;
}

} // namespace MTP
