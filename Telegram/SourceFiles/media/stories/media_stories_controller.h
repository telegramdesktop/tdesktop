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
struct StoriesList;
} // namespace Data

namespace Ui {
class RpWidget;
} // namespace Ui

namespace Media::Stories {

class Header;
class Slider;
class ReplyArea;
class Delegate;

struct ShownId {
	UserData *user = nullptr;
	StoryId id = 0;

	explicit operator bool() const {
		return user != nullptr && id != 0;
	}
	friend inline auto operator<=>(ShownId, ShownId) = default;
	friend inline bool operator==(ShownId, ShownId) = default;
};

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

	friend inline auto operator<=>(Layout, Layout) = default;
	friend inline bool operator==(Layout, Layout) = default;
};

class Controller final {
public:
	explicit Controller(not_null<Delegate*> delegate);

	[[nodiscard]] not_null<Ui::RpWidget*> wrap() const;
	[[nodiscard]] Layout layout() const;
	[[nodiscard]] rpl::producer<Layout> layoutValue() const;

	[[nodiscard]] std::shared_ptr<ChatHelpers::Show> uiShow() const;
	[[nodiscard]] auto stickerOrEmojiChosen() const
	-> rpl::producer<ChatHelpers::FileChosen>;

	void show(const Data::StoriesList &list, int index);

private:
	void initLayout();

	const not_null<Delegate*> _delegate;

	rpl::variable<std::optional<Layout>> _layout;

	const not_null<Ui::RpWidget*> _wrap;
	const std::unique_ptr<Header> _header;
	const std::unique_ptr<Slider> _slider;
	const std::unique_ptr<ReplyArea> _replyArea;

	ShownId _shown;

	rpl::lifetime _lifetime;

};

} // namespace Media::Stories
