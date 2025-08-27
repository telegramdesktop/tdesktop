/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/chat/attach/attach_abstract_single_preview.h"
#include "ui/chat/attach/attach_controls.h"
#include "ui/chat/attach/attach_send_files_way.h"
#include "ui/effects/spoiler_mess.h"
#include "ui/abstract_button.h"

namespace style {
struct ComposeControls;
} // namespace style

namespace Ui {

class PopupMenu;

class AbstractSingleMediaPreview : public AbstractSinglePreview {
public:
	AbstractSingleMediaPreview(
		QWidget *parent,
		const style::ComposeControls &st,
		AttachControls::Type type,
		Fn<bool(AttachActionType)> actionAllowed);
	~AbstractSingleMediaPreview();

	void setSendWay(SendFilesWay way);
	[[nodiscard]] SendFilesWay sendWay() const;

	[[nodiscard]] rpl::producer<> deleteRequests() const override;
	[[nodiscard]] rpl::producer<> editRequests() const override;
	[[nodiscard]] rpl::producer<> modifyRequests() const override;
	[[nodiscard]] rpl::producer<> editCoverRequests() const;
	[[nodiscard]] rpl::producer<> clearCoverRequests() const;

	[[nodiscard]] bool isPhoto() const;

	void setSpoiler(bool spoiler);
	[[nodiscard]] bool hasSpoiler() const;
	[[nodiscard]] bool canHaveSpoiler() const;
	[[nodiscard]] rpl::producer<bool> spoileredChanges() const;

	[[nodiscard]] QImage generatePriceTagBackground() const;

protected:
	virtual bool supportsSpoilers() const = 0;
	virtual bool drawBackground() const = 0;
	virtual bool tryPaintAnimation(QPainter &p) = 0;
	virtual bool isAnimatedPreviewReady() const = 0;

	void preparePreview(QImage preview);

	int previewLeft() const;
	int previewTop() const;
	int previewWidth() const;
	int previewHeight() const;

	void setAnimated(bool animated);

private:
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;

	[[nodiscard]] bool isOverPreview(QPoint position) const;
	void applyCursor(style::cursor cursor);
	void showContextMenu(QPoint position);

	const style::ComposeControls &_st;
	SendFilesWay _sendWay;
	Fn<bool(AttachActionType)> _actionAllowed;
	bool _animated = false;
	QPixmap _preview;
	QPixmap _previewBlurred;
	int _previewLeft = 0;
	int _previewTop = 0;
	int _previewWidth = 0;
	int _previewHeight = 0;

	std::unique_ptr<SpoilerAnimation> _spoiler;
	rpl::event_stream<bool> _spoileredChanges;

	const int _minThumbH;
	const base::unique_qptr<AttachControlsWidget> _controls;
	rpl::event_stream<> _photoEditorRequests;
	rpl::event_stream<> _editCoverRequests;
	rpl::event_stream<> _clearCoverRequests;

	style::cursor _cursor = style::cur_default;
	bool _pressed = false;

	base::unique_qptr<PopupMenu> _menu;

	rpl::event_stream<> _modifyRequests;

};

} // namespace Ui
