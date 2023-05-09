/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_stories.h"

namespace base {
class PowerSaveBlocker;
} // namespace base

namespace ChatHelpers {
class Show;
struct FileChosen;
} // namespace ChatHelpers

namespace Data {
struct StoriesList;
} // namespace Data

namespace Ui {
class RpWidget;
} // namespace Ui

namespace Media::Player {
struct TrackState;
} // namespace Media::Player

namespace Media::Stories {

class Header;
class Slider;
class ReplyArea;
class Sibling;
class Delegate;
struct SiblingView;

enum class HeaderLayout {
	Normal,
	Outside,
};

struct Layout {
	QRect content;
	QRect header;
	QRect slider;
	int controlsWidth = 0;
	QPoint controlsBottomPosition;
	QRect autocompleteRect;
	HeaderLayout headerLayout = HeaderLayout::Normal;
	QRect siblingLeft;
	QRect siblingRight;

	friend inline auto operator<=>(Layout, Layout) = default;
	friend inline bool operator==(Layout, Layout) = default;
};

class Controller final {
public:
	explicit Controller(not_null<Delegate*> delegate);
	~Controller();

	[[nodiscard]] not_null<Ui::RpWidget*> wrap() const;
	[[nodiscard]] Layout layout() const;
	[[nodiscard]] rpl::producer<Layout> layoutValue() const;

	[[nodiscard]] std::shared_ptr<ChatHelpers::Show> uiShow() const;
	[[nodiscard]] auto stickerOrEmojiChosen() const
	-> rpl::producer<ChatHelpers::FileChosen>;

	void show(
		const std::vector<Data::StoriesList> &lists,
		int index,
		int subindex);
	void ready();

	void updateVideoPlayback(const Player::TrackState &state);

	[[nodiscard]] bool subjumpAvailable(int delta) const;
	[[nodiscard]] bool subjumpFor(int delta);
	[[nodiscard]] bool jumpFor(int delta);
	[[nodiscard]] bool paused() const;
	void togglePaused(bool paused);

	[[nodiscard]] bool canDownload() const;

	void repaintSibling(not_null<Sibling*> sibling);
	[[nodiscard]] SiblingView siblingLeft() const;
	[[nodiscard]] SiblingView siblingRight() const;

	[[nodiscard]] rpl::lifetime &lifetime();

private:
	class PhotoPlayback;

	void initLayout();
	void updatePhotoPlayback(const Player::TrackState &state);
	void updatePlayback(const Player::TrackState &state);
	void updatePowerSaveBlocker(const Player::TrackState &state);

	void showSiblings(
		const std::vector<Data::StoriesList> &lists,
		int index);
	void showSibling(
		std::unique_ptr<Sibling> &sibling,
		const Data::StoriesList *list);

	const not_null<Delegate*> _delegate;

	rpl::variable<std::optional<Layout>> _layout;

	const not_null<Ui::RpWidget*> _wrap;
	const std::unique_ptr<Header> _header;
	const std::unique_ptr<Slider> _slider;
	const std::unique_ptr<ReplyArea> _replyArea;
	std::unique_ptr<PhotoPlayback> _photoPlayback;

	Data::FullStoryId _shown;
	std::optional<Data::StoriesList> _list;
	int _index = 0;
	bool _started = false;

	std::unique_ptr<Sibling> _siblingLeft;
	std::unique_ptr<Sibling> _siblingRight;

	std::unique_ptr<base::PowerSaveBlocker> _powerSaveBlocker;

	rpl::lifetime _lifetime;

};

} // namespace Media::Stories
