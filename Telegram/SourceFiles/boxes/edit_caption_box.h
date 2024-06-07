/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "history/view/controls/history_view_compose_media_edit_manager.h"
#include "ui/layers/box_content.h"
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
enum class AlbumType;
} // namespace Ui

class EditCaptionBox final : public Ui::BoxContent {
public:
	EditCaptionBox(
		QWidget*,
		not_null<Window::SessionController*> controller,
		not_null<HistoryItem*> item,
		TextWithTags &&text,
		bool spoilered,
		bool invertCaption,
		Ui::PreparedList &&list,
		Fn<void()> saved);
	~EditCaptionBox();

	static void StartMediaReplace(
		not_null<Window::SessionController*> controller,
		FullMsgId itemId,
		TextWithTags text,
		bool spoilered,
		bool invertCaption,
		Fn<void()> saved);
	static void StartMediaReplace(
		not_null<Window::SessionController*> controller,
		FullMsgId itemId,
		Ui::PreparedList &&list,
		TextWithTags text,
		bool spoilered,
		bool invertCaption,
		Fn<void()> saved);
	static void StartPhotoEdit(
		not_null<Window::SessionController*> controller,
		std::shared_ptr<Data::PhotoMedia> media,
		FullMsgId itemId,
		TextWithTags text,
		bool spoilered,
		bool invertCaption,
		Fn<void()> saved);

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
	void setupField();
	void setupControls();
	void setInitialText();

	void updateBoxSize();
	void captionResized();

	void setupEmojiPanel();
	void updateEmojiPanelGeometry();
	void emojiFilterForGeometry(not_null<QEvent*> event);

	void setupDragArea();

	bool validateLength(const QString &text) const;
	void applyChanges();
	void save();
	void closeAfterSave();

	bool fileFromClipboard(not_null<const QMimeData*> data);

	[[nodiscard]] int errorTopSkip() const;
	[[nodiscard]] bool hasSpoiler() const;

	bool setPreparedList(Ui::PreparedList &&list);

	const not_null<Window::SessionController*> _controller;
	const not_null<HistoryItem*> _historyItem;
	const bool _isAllowedEditMedia;
	const Ui::AlbumType _albumType;

	const base::unique_qptr<Ui::VerticalLayout> _controls;
	const base::unique_qptr<Ui::ScrollArea> _scroll;
	const base::unique_qptr<Ui::InputField> _field;
	const base::unique_qptr<Ui::EmojiButton> _emojiToggle;

	base::unique_qptr<Ui::AbstractSinglePreview> _content;
	base::unique_qptr<ChatHelpers::TabbedPanel> _emojiPanel;
	base::unique_qptr<QObject> _emojiFilter;

	const TextWithTags _initialText;
	Ui::PreparedList _initialList;
	Fn<void()> _saved;

	std::shared_ptr<Data::PhotoMedia> _photoMedia;

	Ui::PreparedList _preparedList;
	HistoryView::MediaEditManager _mediaEditManager;

	mtpRequestId _saveRequestId = 0;

	base::Timer _checkChangedTimer;
	bool _isPhoto = false;
	bool _asFile = false;

	QString _error;

	rpl::variable<int> _footerHeight = 0;

	rpl::event_stream<> _editMediaClicks;
	rpl::event_stream<> _photoEditorOpens;
	rpl::event_stream<> _previewRebuilds;
	rpl::event_stream<int> _contentHeight;

};
