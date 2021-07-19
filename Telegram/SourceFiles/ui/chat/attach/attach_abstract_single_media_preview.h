/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/chat/attach/attach_abstract_single_preview.h"
#include "ui/chat/attach/attach_controls.h"
#include "ui/abstract_button.h"

namespace Ui {

class AbstractSingleMediaPreview : public AbstractSinglePreview {
public:
	AbstractSingleMediaPreview(QWidget *parent, AttachControls::Type type);
	~AbstractSingleMediaPreview();

	[[nodiscard]] rpl::producer<> deleteRequests() const override;
	[[nodiscard]] rpl::producer<> editRequests() const override;
	[[nodiscard]] rpl::producer<> modifyRequests() const override;

	[[nodiscard]] bool isPhoto() const;

protected:
	virtual bool drawBackground() const = 0;
	virtual bool tryPaintAnimation(Painter &p) = 0;
	virtual bool isAnimatedPreviewReady() const = 0;

	void updatePhotoEditorButton();
	void preparePreview(QImage preview);

	int previewLeft() const;
	int previewTop() const;
	int previewWidth() const;
	int previewHeight() const;

	void setAnimated(bool animated);

private:
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

	bool _animated = false;
	QPixmap _preview;
	int _previewLeft = 0;
	int _previewTop = 0;
	int _previewWidth = 0;
	int _previewHeight = 0;

	const int _minThumbH;
	const base::unique_qptr<AbstractButton> _photoEditorButton;
	const base::unique_qptr<AttachControlsWidget> _controls;

	rpl::event_stream<> _modifyRequests;

};

} // namespace Ui
