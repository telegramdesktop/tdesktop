/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"
#include "ui/chat/attach/attach_send_files_way.h"
#include "base/timer.h"

namespace style {
struct ComposeControls;
} // namespace style

namespace Ui {

struct PreparedFile;
struct GroupMediaLayout;
class AlbumThumbnail;
class PopupMenu;

class AlbumPreview final : public RpWidget {
public:
	AlbumPreview(
		QWidget *parent,
		const style::ComposeControls &st,
		gsl::span<Ui::PreparedFile> items,
		SendFilesWay way);
	~AlbumPreview();

	void setSendWay(SendFilesWay way);

	[[nodiscard]] base::flat_set<int> collectSpoileredIndices();
	[[nodiscard]] bool canHaveSpoiler(int index) const;
	void toggleSpoilers(bool enabled);
	[[nodiscard]] std::vector<int> takeOrder();

	[[nodiscard]] rpl::producer<int> thumbDeleted() const {
		return _thumbDeleted.events();
	}

	[[nodiscard]] rpl::producer<int> thumbChanged() const {
		return _thumbChanged.events();
	}

	rpl::producer<int> thumbModified() const;

protected:
	void paintEvent(QPaintEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;

private:
	int countLayoutHeight(
		const std::vector<GroupMediaLayout> &layout) const;
	std::vector<GroupMediaLayout> generateOrderedLayout() const;
	std::vector<int> defaultOrder(int count = -1) const;
	void prepareThumbs(gsl::span<Ui::PreparedFile> items);
	void updateSizeAnimated(const std::vector<GroupMediaLayout> &layout);
	void updateSize();
	void updateFileRows();

	AlbumThumbnail *thumbUnderCursor();
	void deleteThumbByIndex(int index);
	void changeThumbByIndex(int index);
	void modifyThumbByIndex(int index);
	void thumbButtonsCallback(
		not_null<AlbumThumbnail*> thumb,
		AttachButtonType type);

	void switchToDrag();

	void paintAlbum(Painter &p) const;
	void paintPhotos(Painter &p, QRect clip) const;
	void paintFiles(Painter &p, QRect clip) const;

	void applyCursor(style::cursor cursor);
	int contentLeft() const;
	int contentTop() const;
	AlbumThumbnail *findThumb(QPoint position) const;
	not_null<AlbumThumbnail*> findClosestThumb(QPoint position) const;
	void updateSuggestedDrag(QPoint position);
	int orderIndex(not_null<AlbumThumbnail*> thumb) const;
	void cancelDrag();
	void finishDrag();

	void showContextMenu(not_null<AlbumThumbnail*> thumb, QPoint position);

	const style::ComposeControls &_st;
	SendFilesWay _sendWay;
	style::cursor _cursor = style::cur_default;
	std::vector<int> _order;
	std::vector<QSize> _itemsShownDimensions;
	std::vector<std::unique_ptr<AlbumThumbnail>> _thumbs;
	int _thumbsHeight = 0;
	int _photosHeight = 0;
	int _filesHeight = 0;

	bool _hasMixedFileHeights = false;

	AlbumThumbnail *_draggedThumb = nullptr;
	AlbumThumbnail *_suggestedThumb = nullptr;
	AlbumThumbnail *_paintedAbove = nullptr;
	AlbumThumbnail *_pressedThumb = nullptr;
	QPoint _draggedStartPosition;

	base::Timer _dragTimer;
	AttachButtonType _pressedButtonType = AttachButtonType::None;

	rpl::event_stream<int> _thumbDeleted;
	rpl::event_stream<int> _thumbChanged;
	rpl::event_stream<int> _thumbModified;

	base::unique_qptr<PopupMenu> _menu;

	mutable Animations::Simple _thumbsHeightAnimation;
	mutable Animations::Simple _shrinkAnimation;
	mutable Animations::Simple _finishDragAnimation;

};

} // namespace Ui
