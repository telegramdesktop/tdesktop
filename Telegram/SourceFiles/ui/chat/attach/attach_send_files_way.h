/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/flags.h"

namespace Ui {

enum class AttachButtonType {
	Edit,
	Delete,
	None,
};

class SendFilesWay final {
public:
	[[nodiscard]] bool groupFiles() const {
		return (_flags & Flag::GroupFiles) != 0;
	}
	[[nodiscard]] bool sendImagesAsPhotos() const {
		return (_flags & Flag::SendImagesAsPhotos) != 0;
	}
	void setGroupFiles(bool value);
	void setSendImagesAsPhotos(bool value);

	[[nodiscard]] inline bool operator<(const SendFilesWay &other) const {
		return _flags < other._flags;
	}
	[[nodiscard]] inline bool operator>(const SendFilesWay &other) const {
		return other < *this;
	}
	[[nodiscard]] inline bool operator<=(const SendFilesWay &other) const {
		return !(other < *this);
	}
	[[nodiscard]] inline bool operator>=(const SendFilesWay &other) const {
		return !(*this < other);
	}
	[[nodiscard]] inline bool operator==(const SendFilesWay &other) const {
		return _flags == other._flags;
	}
	[[nodiscard]] inline bool operator!=(const SendFilesWay &other) const {
		return !(*this == other);
	}

	[[nodiscard]] int32 serialize() const;
	[[nodiscard]] static std::optional<SendFilesWay> FromSerialized(
		int32 value);

private:
	enum class Flag : uchar {
		GroupFiles = (1 << 0),
		SendImagesAsPhotos = (1 << 1),

		Default = GroupFiles | SendImagesAsPhotos,
	};
	friend inline constexpr bool is_flag_type(Flag) { return true; };

	base::flags<Flag> _flags = Flag::Default;

};

} // namespace Ui
