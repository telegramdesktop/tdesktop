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

namespace Data {
struct StoriesContext;
} // namespace Data

namespace Main {
class Session;
} // namespace Main

namespace Ui {
class RpWidget;
} // namespace Ui

namespace Media::Stories {

enum class JumpReason {
	Finished,
	User,
};

enum class SiblingType {
	Left,
	Right,
};

class Delegate {
public:
	[[nodiscard]] virtual not_null<Ui::RpWidget*> storiesWrap() = 0;
	[[nodiscard]] virtual auto storiesShow()
		-> std::shared_ptr<ChatHelpers::Show> = 0;
	[[nodiscard]] virtual auto storiesStickerOrEmojiChosen()
		-> rpl::producer<ChatHelpers::FileChosen> = 0;
	virtual void storiesJumpTo(
		not_null<Main::Session*> session,
		FullStoryId id,
		Data::StoriesContext context) = 0;
	virtual void storiesClose() = 0;
	[[nodiscard]] virtual bool storiesPaused() = 0;
	[[nodiscard]] virtual rpl::producer<bool> storiesLayerShown() = 0;
	[[nodiscard]] virtual float64 storiesSiblingOver(SiblingType type) = 0;
	virtual void storiesTogglePaused(bool paused) = 0;
	virtual void storiesRepaint() = 0;
};

} // namespace Media::Stories
