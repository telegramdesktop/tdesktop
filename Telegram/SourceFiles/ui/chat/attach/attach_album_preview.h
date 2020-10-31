/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"
#include "ui/chat/attach/attach_send_files_way.h"

namespace Ui {

struct PreparedFile;
struct GroupMediaLayout;
class AlbumThumbnail;

class AlbumPreview final : public RpWidget {
public:
	AlbumPreview(
		QWidget *parent,
		gsl::span<Ui::PreparedFile> items,
		SendFilesWay way);
	~AlbumPreview();

	void setSendWay(SendFilesWay way);
	std::vector<int> takeOrder();

	[[nodiscard]] rpl::producer<int> thumbDeleted() const {
		return _thumbDeleted.events();
	}

	[[nodiscard]] rpl::producer<int> thumbChanged() const {
		return _thumbChanged.events();
	}

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

	int thumbIndex(AlbumThumbnail *thumb);
	AlbumThumbnail *thumbUnderCursor();
	void deleteThumbByIndex(int index);
	void changeThumbByIndex(int index);
	void thumbButtonsCallback(
		not_null<AlbumThumbnail*> thumb,
		AttachButtonType type);

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

	SendFilesWay _sendWay;
	style::cursor _cursor = style::cur_default;
	std::vector<int> _order;
	std::vector<QSize> _itemsShownDimensions;
	std::vector<std::unique_ptr<AlbumThumbnail>> _thumbs;
	int _thumbsHeight = 0;
	int _photosHeight = 0;
	int _filesHeight = 0;

	AlbumThumbnail *_draggedThumb = nullptr;
	AlbumThumbnail *_suggestedThumb = nullptr;
	AlbumThumbnail *_paintedAbove = nullptr;
	QPoint _draggedStartPosition;

	rpl::event_stream<int> _thumbDeleted;
	rpl::event_stream<int> _thumbChanged;

	mutable Animations::Simple _thumbsHeightAnimation;
	mutable Animations::Simple _shrinkAnimation;
	mutable Animations::Simple _finishDragAnimation;

};

} // namespace Ui
