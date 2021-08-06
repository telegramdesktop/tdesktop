/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_notify_settings.h"

#include "base/unixtime.h"

namespace Data {
namespace {

MTPinputPeerNotifySettings DefaultSettings() {
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
		std::optional<int> muteForSeconds,
		std::optional<bool> silentPosts);

	std::optional<TimeId> muteUntil() const;
	std::optional<bool> silentPosts() const;
	MTPinputPeerNotifySettings serialize() const;

private:
	bool change(
		std::optional<int> mute,
		std::optional<QString> sound,
		std::optional<bool> showPreviews,
		std::optional<bool> silentPosts);

	std::optional<TimeId> _mute;
	std::optional<QString> _sound;
	std::optional<bool> _silent;
	std::optional<bool> _showPreviews;

};

NotifySettingsValue::NotifySettingsValue(
		const MTPDpeerNotifySettings &data) {
	change(data);
}

bool NotifySettingsValue::change(const MTPDpeerNotifySettings &data) {
	const auto mute = data.vmute_until();
	const auto sound = data.vsound();
	const auto showPreviews = data.vshow_previews();
	const auto silent = data.vsilent();
	return change(
		mute ? std::make_optional(mute->v) : std::nullopt,
		sound ? std::make_optional(qs(*sound)) : std::nullopt,
		(showPreviews
			? std::make_optional(mtpIsTrue(*showPreviews))
			: std::nullopt),
		silent ? std::make_optional(mtpIsTrue(*silent)) : std::nullopt);
}

bool NotifySettingsValue::change(
		std::optional<int> muteForSeconds,
		std::optional<bool> silentPosts) {
	const auto now = base::unixtime::now();
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
		std::optional<int> mute,
		std::optional<QString> sound,
		std::optional<bool> showPreviews,
		std::optional<bool> silentPosts) {
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

std::optional<TimeId> NotifySettingsValue::muteUntil() const {
	return _mute;
}

std::optional<bool> NotifySettingsValue::silentPosts() const {
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
	const auto empty = !data.vflags().v;
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
		std::optional<int> muteForSeconds,
		std::optional<bool> silentPosts) {
	if (!muteForSeconds && !silentPosts) {
		return false;
	} else if (_value) {
		return _value->change(muteForSeconds, silentPosts);
	}
	using Flag = MTPDpeerNotifySettings::Flag;
	const auto flags = (muteForSeconds ? Flag::f_mute_until : Flag(0))
		| (silentPosts ? Flag::f_silent : Flag(0));
	const auto muteUntil = muteForSeconds
		? (base::unixtime::now() + *muteForSeconds)
		: 0;
	return change(MTP_peerNotifySettings(
		MTP_flags(flags),
		MTPBool(),
		silentPosts ? MTP_bool(*silentPosts) : MTPBool(),
		MTP_int(muteUntil),
		MTPstring()));
}

std::optional<TimeId> NotifySettings::muteUntil() const {
	return _value
		? _value->muteUntil()
		: std::nullopt;
}

bool NotifySettings::settingsUnknown() const {
	return !_known;
}

std::optional<bool> NotifySettings::silentPosts() const {
	return _value
		? _value->silentPosts()
		: std::nullopt;
}

MTPinputPeerNotifySettings NotifySettings::serialize() const {
	return _value
		? _value->serialize()
		: DefaultSettings();
}

NotifySettings::~NotifySettings() = default;

} // namespace Data
