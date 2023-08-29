/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Data {
class Story;
struct StoriesContext;
struct FileOrigin;
} // namespace Data

namespace Media::Player {
struct TrackState;
} // namespace Media::Player

namespace HistoryView::Reactions {
enum class AttachSelectorResult;
} // namespace HistoryView::Reactions

namespace Ui {
class PopupMenu;
} // namespace Ui

namespace Media::Stories {

class Delegate;
class Controller;

struct ContentLayout {
	QRect geometry;
	float64 fade = 0.;
	float64 scale = 1.;
	int radius = 0;
	bool headerOutside = false;
};

enum class SiblingType;

struct SiblingView {
	QImage image;
	ContentLayout layout;
	QImage userpic;
	QPoint userpicPosition;
	QImage name;
	QPoint namePosition;
	float64 nameOpacity = 0.;
	float64 scale = 1.;

	[[nodiscard]] bool valid() const {
		return !image.isNull();
	}
	explicit operator bool() const {
		return valid();
	}
};

inline constexpr auto kCollapsedCaptionLines = 2;
inline constexpr auto kMaxShownCaptionLines = 4;

class View final {
public:
	explicit View(not_null<Delegate*> delegate);
	~View();

	void show(not_null<Data::Story*> story, Data::StoriesContext context);
	void ready();

	[[nodiscard]] Data::Story *story() const;
	[[nodiscard]] QRect finalShownGeometry() const;
	[[nodiscard]] rpl::producer<QRect> finalShownGeometryValue() const;
	[[nodiscard]] ContentLayout contentLayout() const;
	[[nodiscard]] bool closeByClickAt(QPoint position) const;
	[[nodiscard]] SiblingView sibling(SiblingType type) const;
	[[nodiscard]] Data::FileOrigin fileOrigin() const;
	[[nodiscard]] TextWithEntities captionText() const;
	[[nodiscard]] bool skipCaption() const;
	void showFullCaption();

	void updatePlayback(const Player::TrackState &state);
	[[nodiscard]] ClickHandlerPtr lookupAreaHandler(QPoint point) const;

	[[nodiscard]] bool subjumpAvailable(int delta) const;
	[[nodiscard]] bool subjumpFor(int delta) const;
	[[nodiscard]] bool jumpFor(int delta) const;

	[[nodiscard]] bool paused() const;
	void togglePaused(bool paused);
	void contentPressed(bool pressed);
	void menuShown(bool shown);

	void shareRequested();
	void deleteRequested();
	void reportRequested();
	void togglePinnedRequested(bool pinned);

	[[nodiscard]] bool ignoreWindowMove(QPoint position) const;
	void tryProcessKeyInput(not_null<QKeyEvent*> e);

	[[nodiscard]] bool allowStealthMode() const;
	void setupStealthMode();

	using AttachStripResult = HistoryView::Reactions::AttachSelectorResult;
	[[nodiscard]] AttachStripResult attachReactionsToMenu(
		not_null<Ui::PopupMenu*> menu,
		QPoint desiredPosition);

	[[nodiscard]] rpl::lifetime &lifetime();

private:
	const std::unique_ptr<Controller> _controller;

};

} // namespace Media::Stories
