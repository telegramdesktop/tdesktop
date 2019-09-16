/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "boxes/abstract_box.h"
#include "storage/storage_media_prepare.h"
#include "ui/wrap/slide_wrap.h"
#include "media/clip/media_clip_reader.h"

namespace ChatHelpers {
class TabbedPanel;
} // namespace ChatHelpers

namespace Window {
class SessionController;
} // namespace Window

namespace Data {
class Media;
} // namespace Data

namespace Ui {
class InputField;
class EmojiButton;
class IconButton;
class Checkbox;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

class EditCaptionBox
	: public BoxContent
	, public RPCSender
	, private base::Subscriber {
public:
	EditCaptionBox(
		QWidget*,
		not_null<Window::SessionController*> controller,
		not_null<HistoryItem*> item);

protected:
	void prepare() override;
	void setInnerFocus() override;

	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;

private:
	void updateBoxSize();
	void prepareGifPreview(DocumentData* document = nullptr);
	void clipCallback(Media::Clip::Notification notification);

	void setupEmojiPanel();
	void updateEmojiPanelGeometry();
	void emojiFilterForGeometry(not_null<QEvent*> event);

	void save();
	void captionResized();

	void saveDone(const MTPUpdates &updates);
	bool saveFail(const RPCError &error);

	void setName(QString nameString, qint64 size);
	bool fileFromClipboard(not_null<const QMimeData*> data);
	void updateEditPreview();
	void updateEditMediaButton();

	int errorTopSkip() const;

	void createEditMediaButton();

	inline QString getNewMediaPath() {
		return _preparedList.files.empty()
			? QString()
			: _preparedList.files.front().path;
	}

	not_null<Window::SessionController*> _controller;
	FullMsgId _msgId;
	Image *_thumbnailImage = nullptr;
	bool _thumbnailImageLoaded = false;
	Fn<void()> _refreshThumbnail;
	bool _animated = false;
	bool _photo = false;
	bool _doc = false;

	QPixmap _thumb;
	Media::Clip::ReaderPointer _gifPreview;

	object_ptr<Ui::InputField> _field = { nullptr };
	object_ptr<Ui::EmojiButton> _emojiToggle = { nullptr };
	base::unique_qptr<ChatHelpers::TabbedPanel> _emojiPanel;
	base::unique_qptr<QObject> _emojiFilter;

	int _thumbx = 0;
	int _thumbw = 0;
	int _thumbh = 0;
	Ui::Text::String _name;
	QString _status;
	bool _isAudio = false;
	bool _isImage = false;

	int _gifw = 0;
	int _gifh = 0;
	int _gifx = 0;

	Storage::PreparedList _preparedList;

	bool _previewCancelled = false;
	mtpRequestId _saveRequestId = 0;

	object_ptr<Ui::IconButton> _editMedia = nullptr;
	Ui::SlideWrap<Ui::RpWidget> *_wayWrap = nullptr;
	QString _newMediaPath;
	bool _isAllowedEditMedia = false;
	bool _isAlbum = false;
	bool _asFile = false;
	rpl::event_stream<> _editMediaClicks;

	QString _error;

};
