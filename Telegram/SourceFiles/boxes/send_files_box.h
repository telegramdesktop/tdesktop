/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <rpl/variable.h>
#include "boxes/abstract_box.h"
#include "ui/chat/attach/attach_prepare.h"
#include "ui/chat/attach/attach_send_files_way.h"
#include "storage/localimageloader.h"
#include "storage/storage_media_prepare.h"

namespace Window {
class SessionController;
} // namespace Window

namespace Api {
struct SendOptions;
enum class SendType;
} // namespace Api

namespace ChatHelpers {
class TabbedPanel;
} // namespace ChatHelpers

namespace Ui {
class Checkbox;
class RoundButton;
class InputField;
struct GroupMediaLayout;
class EmojiButton;
class AlbumPreview;
class VerticalLayout;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

namespace SendMenu {
enum class Type;
} // namespace SendMenu

class SendFilesBox : public Ui::BoxContent {
public:
	enum class SendLimit {
		One,
		Many
	};
	SendFilesBox(
		QWidget*,
		not_null<Window::SessionController*> controller,
		Ui::PreparedList &&list,
		const TextWithTags &caption,
		SendLimit limit,
		Api::SendType sendType,
		SendMenu::Type sendMenuType);

	void setConfirmedCallback(
		Fn<void(
			Ui::PreparedList &&list,
			Ui::SendFilesWay way,
			TextWithTags &&caption,
			Api::SendOptions options,
			bool ctrlShiftEnter)> callback) {
		_confirmedCallback = std::move(callback);
	}
	void setCancelledCallback(Fn<void()> callback) {
		_cancelledCallback = std::move(callback);
	}

	~SendFilesBox();

protected:
	void prepare() override;
	void setInnerFocus() override;

	void keyPressEvent(QKeyEvent *e) override;
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

private:
	struct Block {
		base::unique_qptr<Ui::RpWidget> preview;
		int fromIndex = 0;
		int tillIndex = 0;
	};
	void initSendWay();
	void initPreview();

	void setupControls();
	void setupSendWayControls();
	void setupCaption();
	void setupShadows(
		not_null<Ui::ScrollArea*> wrap,
		not_null<Ui::AlbumPreview*> content);

	void setupEmojiPanel();
	void updateSendWayControlsVisibility();
	void updateEmojiPanelGeometry();
	void emojiFilterForGeometry(not_null<QEvent*> event);

	void preparePreview();
	void applyAlbumOrder(not_null<Ui::AlbumPreview*> preview, int from);

	void send(Api::SendOptions options, bool ctrlShiftEnter = false);
	void sendSilent();
	void sendScheduled();
	void captionResized();

	void setupDragArea();
	void setupTitleText();
	void updateBoxSize();
	void updateControlsGeometry();
	void updateCaptionPlaceholder();

	void addThumbButtonHandlers(not_null<Ui::ScrollArea*> wrap);

	bool canAddFiles(not_null<const QMimeData*> data) const;
	bool addFiles(not_null<const QMimeData*> data);
	bool addFiles(Ui::PreparedList list);

	void openDialogToAddFileToAlbum();
	void refreshAllAfterAlbumChanges();

	const not_null<Window::SessionController*> _controller;
	const Api::SendType _sendType = Api::SendType();

	QString _titleText;
	int _titleHeight = 0;

	Ui::PreparedList _list;

	SendLimit _sendLimit = SendLimit::Many;
	SendMenu::Type _sendMenuType = SendMenu::Type();

	Fn<void(
		Ui::PreparedList &&list,
		Ui::SendFilesWay way,
		TextWithTags &&caption,
		Api::SendOptions options,
		bool ctrlShiftEnter)> _confirmedCallback;
	Fn<void()> _cancelledCallback;
	bool _confirmed = false;

	object_ptr<Ui::InputField> _caption = { nullptr };
	object_ptr<Ui::EmojiButton> _emojiToggle = { nullptr };
	base::unique_qptr<ChatHelpers::TabbedPanel> _emojiPanel;
	base::unique_qptr<QObject> _emojiFilter;

	object_ptr<Ui::Checkbox> _groupMediaInAlbums = { nullptr };
	object_ptr<Ui::Checkbox> _sendImagesAsPhotos = { nullptr };
	object_ptr<Ui::Checkbox> _groupFiles = { nullptr };
	rpl::variable<Ui::SendFilesWay> _sendWay = Ui::SendFilesWay();

	rpl::variable<int> _footerHeight = 0;
	rpl::event_stream<> _albumChanged;
	rpl::lifetime _dimensionsLifetime;

	object_ptr<Ui::ScrollArea> _scroll;
	QPointer<Ui::VerticalLayout> _inner;
	std::vector<Block> _blocks;

	int _lastScrollTop = 0;

	QPointer<Ui::RoundButton> _send;
	QPointer<Ui::RoundButton> _addFile;

};
