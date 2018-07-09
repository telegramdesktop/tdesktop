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
	return MTP_inputPeerNotifySettings(
		MTP_flags(0),
		MTPBool(),
		MTPBool(),
		MTPint(),
		MTPstring());
}

} // namespace

class NotifySettingsValue {
public:
	NotifySettingsValue(const MTPDpeerNotifySettings &data);

	bool change(const MTPDpeerNotifySettings &data);
	bool change(
		base::optional<int> muteForSeconds,
		base::optional<bool> silentPosts);

	base::optional<TimeId> muteUntil() const;
	base::optional<bool> silentPosts() const;
	MTPinputPeerNotifySettings serialize() const;

private:
	bool change(
		base::optional<int> mute,
		base::optional<QString> sound,
		base::optional<bool> showPreviews,
		base::optional<bool> silentPosts);

	base::optional<TimeId> _mute;
	base::optional<QString> _sound;
	base::optional<bool> _silent;
	base::optional<bool> _showPreviews;

};

NotifySettingsValue::NotifySettingsValue(
		const MTPDpeerNotifySettings &data) {
	change(data);
}

bool NotifySettingsValue::change(const MTPDpeerNotifySettings &data) {
	return change(data.has_mute_until()
		? base::make_optional(data.vmute_until.v)
		: base::none, data.has_sound()
		? base::make_optional(qs(data.vsound))
		: base::none, data.has_show_previews()
		? base::make_optional(mtpIsTrue(data.vshow_previews))
		: base::none, data.has_silent()
		? base::make_optional(mtpIsTrue(data.vsilent))
		: base::none);
}

bool NotifySettingsValue::change(
		base::optional<int> muteForSeconds,
		base::optional<bool> silentPosts) {
	const auto now = unixtime();
	const auto notMuted = muteForSeconds
		? !(*muteForSeconds)
		: (!_mute || *_mute <= now);
	const auto newMute = muteForSeconds
		? base::make_optional((*muteForSeconds > 0)
			? (now + *muteForSeconds)
			: 0)
		: _mute;
	const auto newSound = (_sound && _sound->isEmpty() && notMuted)
		? qsl("default")
		: _sound;
	const auto newSilentPosts = silentPosts
		? base::make_optional(*silentPosts)
		: _silent;
	return change(
		newMute,
		newSound,
		_showPreviews,
		newSilentPosts);
}

bool NotifySettingsValue::change(
		base::optional<int> mute,
		base::optional<QString> sound,
		base::optional<bool> showPreviews,
		base::optional<bool> silentPosts) {
	if (_mute == mute
		&& _sound == sound
		&& _showPreviews == showPreviews
		&& _silent == silentPosts) {
		return false;
	}
	_mute = mute;
	_sound = sound;
	_showPreviews = showPreviews;
	_silent = silentPosts;
	return true;
}

base::optional<TimeId> NotifySettingsValue::muteUntil() const {
	return _mute;
}

base::optional<bool> NotifySettingsValue::silentPosts() const {
	return _silent;
}

MTPinputPeerNotifySettings NotifySettingsValue::serialize() const {
	using Flag = MTPDinputPeerNotifySettings::Flag;
	const auto flag = [](auto &&optional, Flag flag) {
		return optional.has_value() ? flag : Flag(0);
	};
	return MTP_inputPeerNotifySettings(
		MTP_flags(flag(_mute, Flag::f_mute_until)
			| flag(_sound, Flag::f_sound)
			| flag(_silent, Flag::f_silent)
			| flag(_showPreviews, Flag::f_show_previews)),
		MTP_bool(_showPreviews ? *_showPreviews : true),
		MTP_bool(_silent ? *_silent : false),
		MTP_int(_mute ? *_mute : false),
		MTP_string(_sound ? *_sound : QString()));
}

NotifySettings::NotifySettings() = default;

bool NotifySettings::change(const MTPPeerNotifySettings &settings) {
	Expects(settings.type() == mtpc_peerNotifySettings);

	auto &data = settings.c_peerNotifySettings();
	const auto empty = !data.vflags.v;
	if (empty) {
		if (!_known || _value) {
			_known = true;
			_value = nullptr;
			return true;
		}
		return false;
	}
	if (_value) {
		return _value->change(data);
	}
	_known = true;
	_value = std::make_unique<NotifySettingsValue>(data);
	return true;
}

bool NotifySettings::change(
		base::optional<int> muteForSeconds,
		base::optional<bool> silentPosts) {
	if (!muteForSeconds && !silentPosts) {
		return false;
	} else if (_value) {
		return _value->change(muteForSeconds, silentPosts);
	}
	using Flag = MTPDpeerNotifySettings::Flag;
	const auto flags = (muteForSeconds ? Flag::f_mute_until : Flag(0))
		| (silentPosts ? Flag::f_silent : Flag(0));
	const auto muteUntil = muteForSeconds
		? (unixtime() + *muteForSeconds)
		: 0;
	return change(MTP_peerNotifySettings(
		MTP_flags(flags),
		MTPBool(),
		silentPosts ? MTP_bool(*silentPosts) : MTPBool(),
		muteForSeconds ? MTP_int(unixtime() + *muteForSeconds) : MTPint(),
		MTPstring()));
}

base::optional<TimeId> NotifySettings::muteUntil() const {
	return _value
		? _value->muteUntil()
		: base::none;
}

bool NotifySettings::settingsUnknown() const {
	return !_known;
}

base::optional<bool> NotifySettings::silentPosts() const {
	return _value
		? _value->silentPosts()
		: base::none;
}

MTPinputPeerNotifySettings NotifySettings::serialize() const {
	return _value
		? _value->serialize()
		: DefaultSettings();
}

NotifySettings::~NotifySettings() = default;

} // namespace Data
