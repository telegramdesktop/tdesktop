/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/notify/data_peer_notify_settings.h"

#include "base/unixtime.h"

namespace Data {
namespace {

[[nodiscard]] MTPinputPeerNotifySettings DefaultSettings() {
	return MTP_inputPeerNotifySettings(
		MTP_flags(0),
		MTPBool(),
		MTPBool(),
		MTPint(),
		MTPNotificationSound(),
		MTPBool(),
		MTPBool(),
		MTPNotificationSound());
}

[[nodiscard]] NotifySound ParseSound(const MTPNotificationSound &sound) {
	return sound.match([&](const MTPDnotificationSoundDefault &data) {
		return NotifySound();
	}, [&](const MTPDnotificationSoundNone &data) {
		return NotifySound{ .none = true };
	}, [&](const MTPDnotificationSoundLocal &data) {
		return NotifySound{
			.title = qs(data.vtitle()),
			.data = qs(data.vdata()),
		};
	}, [&](const MTPDnotificationSoundRingtone &data) {
		return NotifySound{ .id = data.vid().v };
	});
}

[[nodiscard]] MTPNotificationSound SerializeSound(
		const std::optional<NotifySound> &sound) {
	return !sound
		? MTPNotificationSound()
		: sound->none
		? MTP_notificationSoundNone()
		: sound->id
		? MTP_notificationSoundRingtone(MTP_long(sound->id))
		: !sound->title.isEmpty()
		? MTP_notificationSoundLocal(
			MTP_string(sound->title),
			MTP_string(sound->data))
		: MTP_notificationSoundDefault();
}

} // namespace

int MuteValue::until() const {
	constexpr auto kMax = std::numeric_limits<int>::max();

	return forever
		? kMax
		: (period > 0)
		? int(std::min(int64(base::unixtime::now()) + period, int64(kMax)))
		: unmute
		? 0
		: -1;
}

class NotifyPeerSettingsValue {
public:
	NotifyPeerSettingsValue(const MTPDpeerNotifySettings &data);

	bool change(const MTPDpeerNotifySettings &data);
	bool change(
		MuteValue muteForSeconds,
		std::optional<bool> silentPosts,
		std::optional<NotifySound> sound,
		std::optional<bool> storiesMuted);

	std::optional<TimeId> muteUntil() const;
	std::optional<bool> silentPosts() const;
	std::optional<NotifySound> sound() const;
	MTPinputPeerNotifySettings serialize() const;

private:
	bool change(
		std::optional<int> mute,
		std::optional<NotifySound> sound,
		std::optional<bool> showPreviews,
		std::optional<bool> silentPosts,
		std::optional<bool> storiesMuted);

	std::optional<TimeId> _mute;
	std::optional<NotifySound> _sound;
	std::optional<bool> _silent;
	std::optional<bool> _showPreviews;
	std::optional<bool> _storiesMuted;

};

NotifyPeerSettingsValue::NotifyPeerSettingsValue(
		const MTPDpeerNotifySettings &data) {
	change(data);
}

bool NotifyPeerSettingsValue::change(const MTPDpeerNotifySettings &data) {
	const auto mute = data.vmute_until();
	const auto sound = data.vother_sound();
	const auto showPreviews = data.vshow_previews();
	const auto silent = data.vsilent();
	const auto storiesMuted = data.vstories_muted();
	return change(
		mute ? std::make_optional(mute->v) : std::nullopt,
		sound ? std::make_optional(ParseSound(*sound)) : std::nullopt,
		(showPreviews
			? std::make_optional(mtpIsTrue(*showPreviews))
			: std::nullopt),
		silent ? std::make_optional(mtpIsTrue(*silent)) : std::nullopt,
		(storiesMuted
			? std::make_optional(mtpIsTrue(*storiesMuted))
			: std::nullopt));
}

bool NotifyPeerSettingsValue::change(
		MuteValue muteForSeconds,
		std::optional<bool> silentPosts,
		std::optional<NotifySound> sound,
		std::optional<bool> storiesMuted) {
	const auto newMute = muteForSeconds
		? base::make_optional(muteForSeconds.until())
		: _mute;
	const auto newSilentPosts = silentPosts
		? base::make_optional(*silentPosts)
		: _silent;
	const auto newSound = sound
		? base::make_optional(*sound)
		: _sound;
	const auto newStoriesMuted = storiesMuted
		? base::make_optional(*storiesMuted)
		: _storiesMuted;
	return change(
		newMute,
		newSound,
		_showPreviews,
		newSilentPosts,
		newStoriesMuted);
}

bool NotifyPeerSettingsValue::change(
		std::optional<int> mute,
		std::optional<NotifySound> sound,
		std::optional<bool> showPreviews,
		std::optional<bool> silentPosts,
		std::optional<bool> storiesMuted) {
	if (_mute == mute
		&& _sound == sound
		&& _showPreviews == showPreviews
		&& _silent == silentPosts
		&& _storiesMuted == storiesMuted) {
		return false;
	}
	_mute = mute;
	_sound = sound;
	_showPreviews = showPreviews;
	_silent = silentPosts;
	_storiesMuted = storiesMuted;
	return true;
}

std::optional<TimeId> NotifyPeerSettingsValue::muteUntil() const {
	return _mute;
}

std::optional<bool> NotifyPeerSettingsValue::silentPosts() const {
	return _silent;
}

std::optional<NotifySound> NotifyPeerSettingsValue::sound() const {
	return _sound;
}

MTPinputPeerNotifySettings NotifyPeerSettingsValue::serialize() const {
	using Flag = MTPDinputPeerNotifySettings::Flag;
	const auto flag = [](auto &&optional, Flag flag) {
		return optional.has_value() ? flag : Flag(0);
	};
	return MTP_inputPeerNotifySettings(
		MTP_flags(flag(_mute, Flag::f_mute_until)
			| flag(_sound, Flag::f_sound)
			| flag(_silent, Flag::f_silent)
			| flag(_showPreviews, Flag::f_show_previews)
			| flag(_storiesMuted, Flag::f_stories_muted)),
		MTP_bool(_showPreviews.value_or(true)),
		MTP_bool(_silent.value_or(false)),
		MTP_int(_mute.value_or(false)),
		SerializeSound(_sound),
		MTP_bool(_storiesMuted.value_or(false)),
		MTP_bool(false), // stories_hide_sender
		SerializeSound(std::nullopt)); // stories_sound
}

PeerNotifySettings::PeerNotifySettings() = default;

bool PeerNotifySettings::change(const MTPPeerNotifySettings &settings) {
	auto &data = settings.data();
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
	_value = std::make_unique<NotifyPeerSettingsValue>(data);
	return true;
}

bool PeerNotifySettings::change(
		MuteValue muteForSeconds,
		std::optional<bool> silentPosts,
		std::optional<NotifySound> sound,
		std::optional<bool> storiesMuted) {
	if (!muteForSeconds && !silentPosts && !sound && !storiesMuted) {
		return false;
	} else if (_value) {
		return _value->change(
			muteForSeconds,
			silentPosts,
			sound,
			storiesMuted);
	}
	using Flag = MTPDpeerNotifySettings::Flag;
	const auto flags = (muteForSeconds ? Flag::f_mute_until : Flag(0))
		| (silentPosts ? Flag::f_silent : Flag(0))
		| (sound ? Flag::f_other_sound : Flag(0))
		| (storiesMuted ? Flag::f_stories_muted : Flag(0));
	return change(MTP_peerNotifySettings(
		MTP_flags(flags),
		MTPBool(),
		silentPosts ? MTP_bool(*silentPosts) : MTPBool(),
		MTP_int(muteForSeconds.until()),
		MTPNotificationSound(),
		MTPNotificationSound(),
		SerializeSound(sound),
		storiesMuted ? MTP_bool(*storiesMuted) : MTPBool(),
		MTPBool(), // stories_hide_sender
		MTPNotificationSound(),
		MTPNotificationSound(),
		SerializeSound(std::nullopt))); // stories_sound
}

bool PeerNotifySettings::resetToDefault() {
	if (_known && !_value) {
		return false;
	}
	_known = true;
	_value = nullptr;
	return true;
}

std::optional<TimeId> PeerNotifySettings::muteUntil() const {
	return _value
		? _value->muteUntil()
		: std::nullopt;
}

bool PeerNotifySettings::settingsUnknown() const {
	return !_known;
}

std::optional<bool> PeerNotifySettings::silentPosts() const {
	return _value
		? _value->silentPosts()
		: std::nullopt;
}

std::optional<NotifySound> PeerNotifySettings::sound() const {
	return _value
		? _value->sound()
		: std::nullopt;
}

MTPinputPeerNotifySettings PeerNotifySettings::serialize() const {
	return _value
		? _value->serialize()
		: DefaultSettings();
}

PeerNotifySettings::~PeerNotifySettings() = default;

} // namespace Data
