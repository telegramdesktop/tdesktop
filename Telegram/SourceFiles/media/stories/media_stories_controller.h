/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_stories.h"
#include "ui/effects/animations.h"

namespace base {
class PowerSaveBlocker;
} // namespace base

namespace ChatHelpers {
class Show;
struct FileChosen;
} // namespace ChatHelpers

namespace Data {
struct StoriesList;
struct FileOrigin;
} // namespace Data

namespace Ui {
class RpWidget;
} // namespace Ui

namespace Main {
class Session;
} // namespace Main

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
enum class SiblingType;
struct ContentLayout;
class CaptionFullView;

enum class HeaderLayout {
	Normal,
	Outside,
};

struct SiblingLayout {
	QRect geometry;
	QRect userpic;
	QRect nameBoundingRect;
	int nameFontSize = 0;

	friend inline bool operator==(SiblingLayout, SiblingLayout) = default;
};

struct Layout {
	QRect content;
	QRect header;
	QRect slider;
	int controlsWidth = 0;
	QPoint controlsBottomPosition;
	QRect autocompleteRect;
	HeaderLayout headerLayout = HeaderLayout::Normal;
	SiblingLayout siblingLeft;
	SiblingLayout siblingRight;

	friend inline bool operator==(Layout, Layout) = default;
};

class Controller final {
public:
	explicit Controller(not_null<Delegate*> delegate);
	~Controller();

	[[nodiscard]] not_null<Ui::RpWidget*> wrap() const;
	[[nodiscard]] Layout layout() const;
	[[nodiscard]] rpl::producer<Layout> layoutValue() const;
	[[nodiscard]] ContentLayout contentLayout() const;
	[[nodiscard]] Data::FileOrigin fileOrigin() const;
	[[nodiscard]] TextWithEntities captionText() const;
	void showFullCaption();

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
	[[nodiscard]] SiblingView sibling(SiblingType type) const;

	void unfocusReply();

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

	void subjumpTo(int index);
	void checkWaitingFor();

	const not_null<Delegate*> _delegate;

	rpl::variable<std::optional<Layout>> _layout;

	const not_null<Ui::RpWidget*> _wrap;
	const std::unique_ptr<Header> _header;
	const std::unique_ptr<Slider> _slider;
	const std::unique_ptr<ReplyArea> _replyArea;
	std::unique_ptr<PhotoPlayback> _photoPlayback;
	std::unique_ptr<CaptionFullView> _captionFullView;

	Ui::Animations::Simple _contentFadeAnimation;
	bool _contentFaded = false;

	FullStoryId _shown;
	TextWithEntities _captionText;
	std::optional<Data::StoriesList> _list;
	FullStoryId _waitingForId;
	int _index = 0;
	bool _started = false;

	std::unique_ptr<Sibling> _siblingLeft;
	std::unique_ptr<Sibling> _siblingRight;

	std::unique_ptr<base::PowerSaveBlocker> _powerSaveBlocker;

	Main::Session *_session = nullptr;
	rpl::lifetime _sessionLifetime;

	rpl::lifetime _lifetime;

};

} // namespace Media::Stories
