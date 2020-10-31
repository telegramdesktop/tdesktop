/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/chat/attach/attach_send_files_way.h"
#include "ui/effects/animations.h"
#include "ui/grouped_layout.h"
#include "ui/round_rect.h"
#include "base/object_ptr.h"

namespace Ui {

struct PreparedFile;
class IconButton;

class AlbumThumbnail final {
public:
	AlbumThumbnail(
		const PreparedFile &file,
		const GroupMediaLayout &layout,
		QWidget *parent,
		Fn<void()> editCallback,
		Fn<void()> deleteCallback);

	void moveToLayout(const GroupMediaLayout &layout);
	void animateLayoutToInitial();
	void resetLayoutAnimation();

	int photoHeight() const;

	void paintInAlbum(
		Painter &p,
		int left,
		int top,
		float64 shrinkProgress,
		float64 moveProgress);
	void paintPhoto(Painter &p, int left, int top, int outerWidth);
	void paintFile(Painter &p, int left, int top, int outerWidth);

	bool containsPoint(QPoint position) const;
	bool buttonsContainPoint(QPoint position) const;
	AttachButtonType buttonTypeFromPoint(QPoint position) const;
	int distanceTo(QPoint position) const;
	bool isPointAfter(QPoint position) const;
	void moveInAlbum(QPoint to);
	QPoint center() const;
	void suggestMove(float64 delta, Fn<void()> callback);
	void finishAnimations();

	void updateFileRow(int row);

	static constexpr auto kShrinkDuration = crl::time(150);

private:
	QRect countRealGeometry() const;
	QRect countCurrentGeometry(float64 progress) const;
	void prepareCache(QSize size, int shrink);
	void drawSimpleFrame(Painter &p, QRect to, QSize size) const;
	QRect paintButtons(
		Painter &p,
		QPoint point,
		int outerWidth,
		float64 shrinkProgress);

	GroupMediaLayout _layout;
	std::optional<QRect> _animateFromGeometry;
	const QImage _fullPreview;
	const int _shrinkSize = 0;
	QPixmap _albumImage;
	QImage _albumCache;
	QPoint _albumPosition;
	RectParts _albumCorners = RectPart::None;
	QPixmap _photo;
	QPixmap _fileThumb;
	QString _name;
	QString _status;
	int _nameWidth = 0;
	int _statusWidth = 0;
	bool _isVideo = false;
	float64 _suggestedMove = 0.;
	Animations::Simple _suggestedMoveAnimation;
	int _lastShrinkValue = 0;
	RoundRect _buttonsRect;

	QRect _lastRectOfButtons;

	object_ptr<IconButton> _editMedia = { nullptr };
	object_ptr<IconButton> _deleteMedia = { nullptr };

};

} // namespace Ui
