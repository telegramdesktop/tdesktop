/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace ChatHelpers {
class Show;
struct FileChosen;
} // namespace ChatHelpers

namespace Ui {
class RpWidget;
} // namespace Ui

namespace Media::Stories {

class Delegate {
public:
	[[nodiscard]] virtual not_null<Ui::RpWidget*> storiesWrap() = 0;
	[[nodiscard]] virtual auto storiesShow()
		-> std::shared_ptr<ChatHelpers::Show> = 0;
	[[nodiscard]] virtual auto storiesStickerOrEmojiChosen()
		-> rpl::producer<ChatHelpers::FileChosen> = 0;
};

} // namespace Media::Stories
