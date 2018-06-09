/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_notify_settings.h"

namespace Data {
namespace {

MTPinputPeerNotifySettings DefaultSettings() {
	const auto flags = MTPDpeerNotifySettings::Flag::f_show_previews;
	const auto muteValue = TimeId(0);
	return MTP_inputPeerNotifySettings(
		MTP_flags(mtpCastFlags(flags)),
		MTP_int(muteValue),
		MTP_string("default"));
}

} // namespace

class NotifySettingsValue {
public:
	NotifySettingsValue(const MTPDpeerNotifySettings &data);

	using MuteChange = NotifySettings::MuteChange;
	using SilentPostsChange = NotifySettings::SilentPostsChange;

	bool change(const MTPDpeerNotifySettings &data);
	bool change(
		MuteChange mute,
		SilentPostsChange silent,
		int muteForSeconds);
	TimeMs muteFinishesIn() const;
	bool silentPosts() const;
	MTPinputPeerNotifySettings serialize() const;

private:
	bool change(
		MTPDpeerNotifySettings::Flags flags,
		TimeId mute,
		QString sound);

	MTPDpeerNotifySettings::Flags _flags;
	TimeId _mute;
	QString _sound;

};

NotifySettingsValue::NotifySettingsValue(const MTPDpeerNotifySettings &data)
: _flags(data.vflags.v)
, _mute(data.vmute_until.v)
, _sound(qs(data.vsound)) {
}

bool NotifySettingsValue::silentPosts() const {
	return _flags & MTPDpeerNotifySettings::Flag::f_silent;
}

bool NotifySettingsValue::change(const MTPDpeerNotifySettings &data) {
	return change(data.vflags.v, data.vmute_until.v, qs(data.vsound));
}

bool NotifySettingsValue::change(
		MuteChange mute,
		SilentPostsChange silent,
		int muteForSeconds) {
	const auto newFlags = [&] {
		auto result = _flags;
		if (silent == SilentPostsChange::Silent) {
			result |= MTPDpeerNotifySettings::Flag::f_silent;
		} else if (silent == SilentPostsChange::Notify) {
			result &= ~MTPDpeerNotifySettings::Flag::f_silent;
		}
		return result;
	}();
	const auto newMute = (mute == MuteChange::Mute)
		? (unixtime() + muteForSeconds)
		: (mute == MuteChange::Ignore) ? _mute : 0;
	const auto newSound = (newMute == 0 && _sound.isEmpty())
		? qsl("default")
		: _sound;
	return change(newFlags, newMute, newSound);
}

bool NotifySettingsValue::change(
		MTPDpeerNotifySettings::Flags flags,
		TimeId mute,
		QString sound) {
	if (_flags == flags && _mute == mute && _sound == sound) {
		return false;
	}
	_flags = flags;
	_mute = mute;
	_sound = sound;
	return true;
}

TimeMs NotifySettingsValue::muteFinishesIn() const {
	auto now = unixtime();
	if (_mute > now) {
		return (_mute - now + 1) * 1000LL;
	}
	return 0;
}

MTPinputPeerNotifySettings NotifySettingsValue::serialize() const {
	return MTP_inputPeerNotifySettings(
		MTP_flags(mtpCastFlags(_flags)),
		MTP_int(_mute),
		MTP_string(_sound));
}

bool NotifySettings::change(const MTPPeerNotifySettings &settings) {
	Expects(settings.type() == mtpc_peerNotifySettings);

	auto &data = settings.c_peerNotifySettings();
	if (_value) {
		return _value->change(data);
	}
	_known = true;
	_value = std::make_unique<NotifySettingsValue>(data);
	return true;
}

NotifySettings::NotifySettings() = default;

bool NotifySettings::change(
		MuteChange mute,
		SilentPostsChange silent,
		int muteForSeconds) {
	Expects(mute != MuteChange::Mute || muteForSeconds > 0);

	if (mute == MuteChange::Ignore && silent == SilentPostsChange::Ignore) {
		return false;
	}
	if (_value) {
		return _value->change(mute, silent, muteForSeconds);
	}
	const auto flags = MTPDpeerNotifySettings::Flag::f_show_previews
		| ((silent == SilentPostsChange::Silent)
			? MTPDpeerNotifySettings::Flag::f_silent
			: MTPDpeerNotifySettings::Flag(0));
	const auto muteUntil = (mute == MuteChange::Mute)
		? (unixtime() + muteForSeconds)
		: 0;
	return change(MTP_peerNotifySettings(
		MTP_flags(flags),
		MTP_int(muteUntil),
		MTP_string("default")));
}

TimeMs NotifySettings::muteFinishesIn() const {
	return _value
		? _value->muteFinishesIn()
		: 0LL;
}

bool NotifySettings::settingsUnknown() const {
	return !_known;
}

bool NotifySettings::silentPosts() const {
	return _value
		? _value->silentPosts()
		: false;
}

MTPinputPeerNotifySettings NotifySettings::serialize() const {
	return _value
		? _value->serialize()
		: DefaultSettings();
}

NotifySettings::~NotifySettings() = default;

} // namespace Data
