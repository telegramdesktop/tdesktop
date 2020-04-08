/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/flags.h"

namespace Data {

class DocumentMedia final {
public:
	explicit DocumentMedia(not_null<DocumentData*> owner);
	~DocumentMedia();

	void goodThumbnailWanted();
	[[nodiscard]] Image *goodThumbnail() const;
	void setGoodThumbnail(QImage thumbnail);

	[[nodiscard]] Image *thumbnailInline() const;

	// For DocumentData.
	static void CheckGoodThumbnail(not_null<DocumentData*> document);

private:
	enum class Flag : uchar {
		GoodThumbnailWanted = 0x01,
	};
	inline constexpr bool is_flag_type(Flag) { return true; };
	using Flags = base::flags<Flag>;

	static void ReadOrGenerateThumbnail(not_null<DocumentData*> document);
	static void GenerateGoodThumbnail(not_null<DocumentData*> document);

	const not_null<DocumentData*> _owner;
	std::unique_ptr<Image> _goodThumbnail;
	mutable std::unique_ptr<Image> _inlineThumbnail;
	Flags _flags;

};

} // namespace Data
