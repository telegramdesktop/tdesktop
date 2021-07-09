/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "boxes/abstract_box.h"
#include "ui/chat/attach/attach_prepare.h"

namespace ChatHelpers {
class TabbedPanel;
} // namespace ChatHelpers

namespace Window {
class SessionController;
} // namespace Window

namespace Data {
class PhotoMedia;
} // namespace Data

namespace Ui {
class AbstractSinglePreview;
class InputField;
class EmojiButton;
class VerticalLayout;
class FadeShadow;
enum class AlbumType;
} // namespace Ui

class EditCaptionBox final : public Ui::BoxContent {
public:
	EditCaptionBox(
		QWidget*,
		not_null<Window::SessionController*> controller,
		not_null<HistoryItem*> item);
	~EditCaptionBox();

protected:
	void prepare() override;
	void setInnerFocus() override;

	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;

private:
	void rebuildPreview();
	void setupEditEventHandler();
	void setupPhotoEditorEventHandler();
	void setupShadows();
	void setupField();
	void setupControls();

	void updateBoxSize();
	void captionResized();

	void setupEmojiPanel();
	void updateEmojiPanelGeometry();
	void emojiFilterForGeometry(not_null<QEvent*> event);

	void setupDragArea();

	void save();

	bool fileFromClipboard(not_null<const QMimeData*> data);

	int errorTopSkip() const;

	bool setPreparedList(Ui::PreparedList &&list);

	const not_null<Window::SessionController*> _controller;
	const not_null<HistoryItem*> _historyItem;
	const bool _isAllowedEditMedia = false;
	const Ui::AlbumType _albumType;

	const base::unique_qptr<Ui::VerticalLayout> _controls;
	const base::unique_qptr<Ui::ScrollArea> _scroll;
	const base::unique_qptr<Ui::InputField> _field;
	const base::unique_qptr<Ui::EmojiButton> _emojiToggle;
	const base::unique_qptr<Ui::FadeShadow> _topShadow,_bottomShadow;

	base::unique_qptr<Ui::AbstractSinglePreview> _content;
	base::unique_qptr<ChatHelpers::TabbedPanel> _emojiPanel;
	base::unique_qptr<QObject> _emojiFilter;

	std::shared_ptr<Data::PhotoMedia> _photoMedia;

	Ui::PreparedList _preparedList;

	mtpRequestId _saveRequestId = 0;

	bool _asFile = false;

	QString _error;

	rpl::variable<bool> _isPhoto = false;
	rpl::variable<int> _footerHeight = 0;

	rpl::event_stream<> _editMediaClicks;
	rpl::event_stream<> _photoEditorOpens;
	rpl::event_stream<int> _contentHeight;

};
