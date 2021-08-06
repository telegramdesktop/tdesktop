/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"
#include "ui/effects/animations.h"

namespace Storage {
enum class MimeDataState;
} // namespace Storage

class DragArea : public Ui::RpWidget {

public:
	DragArea(QWidget *parent);

	struct Areas {
		DragArea *document;
		DragArea *photo;
	};

	using CallbackComputeState =
		Fn<Storage::MimeDataState(const QMimeData *data)>;

	static Areas SetupDragAreaToContainer(
		not_null<Ui::RpWidget*> container,
		Fn<bool(not_null<const QMimeData*>)> &&dragEnterFilter = nullptr,
		Fn<void(bool)> &&setAcceptDropsField = nullptr,
		Fn<void()> &&updateControlsGeometry = nullptr,
		CallbackComputeState &&computeState = nullptr,
		bool hideSubtext = false);

	void setText(const QString &text, const QString &subtext);

	void otherEnter();
	void otherLeave();

	bool overlaps(const QRect &globalRect);

	void hideFast();

	void setDroppedCallback(Fn<void(const QMimeData *data)> callback) {
		_droppedCallback = std::move(callback);
	}

protected:
	void paintEvent(QPaintEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void dragMoveEvent(QDragMoveEvent *e) override;
	// These events should be filtered by parent!
	void dragEnterEvent(QDragEnterEvent *e) override;
	void dragLeaveEvent(QDragLeaveEvent *e) override;
	void dropEvent(QDropEvent *e) override;

private:
	void hideStart();
	void hideFinish();

	void showStart();

	void setIn(bool in);
	void opacityAnimationCallback();

	bool _hiding = false;
	bool _in = false;
	QPixmap _cache;
	Fn<void(const QMimeData *data)> _droppedCallback;

	Ui::Animations::Simple _a_opacity;
	Ui::Animations::Simple _a_in;

	QString _text, _subtext;

};
