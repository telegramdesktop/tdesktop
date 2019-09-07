/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <rpl/variable.h>
#include "boxes/abstract_box.h"
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
template <typename Enum>
class Radioenum;
template <typename Enum>
class RadioenumGroup;
class RoundButton;
class InputField;
struct GroupMediaLayout;
class EmojiButton;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

enum class SendMenuType;

enum class SendFilesWay {
	Album,
	Photos,
	Files,
};

class SendFilesBox : public BoxContent {
public:
	enum class SendLimit {
		One,
		Many
	};
	SendFilesBox(
		QWidget*,
		not_null<Window::SessionController*> controller,
		Storage::PreparedList &&list,
		const TextWithTags &caption,
		CompressConfirm compressed,
		SendLimit limit,
		Api::SendType sendType,
		SendMenuType sendMenuType);

	void setConfirmedCallback(
		Fn<void(
			Storage::PreparedList &&list,
			SendFilesWay way,
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
	class AlbumPreview;

	void initSendWay();
	void initPreview(rpl::producer<int> desiredPreviewHeight);

	void setupControls();
	void setupSendWayControls();
	void setupCaption();
	void setupShadows(
		not_null<Ui::ScrollArea*> wrap,
		not_null<AlbumPreview*> content);

	void setupEmojiPanel();
	void updateEmojiPanelGeometry();
	bool emojiFilter(not_null<QEvent*> event);

	void refreshAlbumMediaCount();
	void preparePreview();
	void prepareSingleFilePreview();
	void prepareAlbumPreview();
	void applyAlbumOrder();

	void send(Api::SendOptions options, bool ctrlShiftEnter = false);
	void sendSilent();
	void sendScheduled();
	void captionResized();

	void setupTitleText();
	void updateBoxSize();
	void updateControlsGeometry();
	void updateCaptionPlaceholder();

	bool canAddFiles(not_null<const QMimeData*> data) const;
	bool canAddUrls(const QList<QUrl> &urls) const;
	bool addFiles(not_null<const QMimeData*> data);

	const not_null<Window::SessionController*> _controller;
	const Api::SendType _sendType = Api::SendType();

	QString _titleText;
	int _titleHeight = 0;

	Storage::PreparedList _list;

	CompressConfirm _compressConfirmInitial = CompressConfirm::None;
	CompressConfirm _compressConfirm = CompressConfirm::None;
	SendLimit _sendLimit = SendLimit::Many;
	SendMenuType _sendMenuType = SendMenuType();

	Fn<void(
		Storage::PreparedList &&list,
		SendFilesWay way,
		TextWithTags &&caption,
		Api::SendOptions options,
		bool ctrlShiftEnter)> _confirmedCallback;
	Fn<void()> _cancelledCallback;
	bool _confirmed = false;

	object_ptr<Ui::InputField> _caption = { nullptr };
	object_ptr<Ui::EmojiButton> _emojiToggle = { nullptr };
	base::unique_qptr<ChatHelpers::TabbedPanel> _emojiPanel;
	base::unique_qptr<QObject> _emojiFilter;

	object_ptr<Ui::Radioenum<SendFilesWay>> _sendAlbum = { nullptr };
	object_ptr<Ui::Radioenum<SendFilesWay>> _sendPhotos = { nullptr };
	object_ptr<Ui::Radioenum<SendFilesWay>> _sendFiles = { nullptr };
	std::shared_ptr<Ui::RadioenumGroup<SendFilesWay>> _sendWay;

	rpl::variable<int> _footerHeight = 0;

	QWidget *_preview = nullptr;
	AlbumPreview *_albumPreview = nullptr;
	int _albumVideosCount = 0;
	int _albumPhotosCount = 0;

	QPointer<Ui::RoundButton> _send;

};
