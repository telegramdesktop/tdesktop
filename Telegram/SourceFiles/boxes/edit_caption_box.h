/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "boxes/abstract_box.h"

namespace Window {
class Controller;
} // namespace Window

namespace Data {
class Media;
} // namespace Data

namespace Ui {
class InputField;
} // namespace Ui

class EditCaptionBox : public BoxContent, public RPCSender {
public:
	EditCaptionBox(
		QWidget*,
		not_null<Window::Controller*> controller,
		not_null<HistoryItem*> item);

protected:
	void prepare() override;
	void setInnerFocus() override;

	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

private:
	void updateBoxSize();
	void prepareGifPreview(DocumentData *document);
	void clipCallback(Media::Clip::Notification notification);

	void save();
	void captionResized();

	void saveDone(const MTPUpdates &updates);
	bool saveFail(const RPCError &error);

	int errorTopSkip() const;

	not_null<Window::Controller*> _controller;
	FullMsgId _msgId;
	bool _animated = false;
	bool _photo = false;
	bool _doc = false;

	QPixmap _thumb;
	Media::Clip::ReaderPointer _gifPreview;

	object_ptr<Ui::InputField> _field = { nullptr };

	int _thumbx = 0;
	int _thumbw = 0;
	int _thumbh = 0;
	Text _name;
	QString _status;
	int _statusw = 0;
	bool _isAudio = false;
	bool _isImage = false;

	bool _previewCancelled = false;
	mtpRequestId _saveRequestId = 0;

	QString _error;

};
