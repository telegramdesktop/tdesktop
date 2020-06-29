/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/emoji_config.h"

namespace Stickers {

class EmojiImageLoader {
public:
	using UniversalImages = Ui::Emoji::UniversalImages;

	explicit EmojiImageLoader(crl::weak_on_queue<EmojiImageLoader> weak);

	void init(
		std::shared_ptr<UniversalImages> images,
		bool largeEnabled);

	[[nodiscard]] QImage prepare(EmojiPtr emoji) const;
	void switchTo(std::shared_ptr<UniversalImages> images);
	std::shared_ptr<UniversalImages> releaseImages();

private:
	crl::weak_on_queue<EmojiImageLoader> _weak;
	std::shared_ptr<UniversalImages> _images;

};

} // namespace Stickers
