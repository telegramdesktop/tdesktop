/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/widgets/buttons.h"
#include "ui/widgets/tooltip.h"
#include "ui/effects/animations.h"
#include "ui/effects/cross_line.h"
#include "styles/style_window.h"
#include "styles/style_widgets.h"

class PeerData;

namespace Data {
class CloudImageView;
} // namespace Data

namespace Window {
class SessionController;
} // namespace Window

namespace Media {
namespace Streaming {
class Instance;
struct Update;
enum class Error;
struct Information;
} // namespace Streaming
} // namespace Media

namespace Ui {

class HistoryDownButton : public RippleButton {
public:
	HistoryDownButton(QWidget *parent, const style::TwoIconButton &st);

	void setUnreadCount(int unreadCount);
	int unreadCount() const {
		return _unreadCount;
	}

protected:
	void paintEvent(QPaintEvent *e) override;

	QImage prepareRippleMask() const override;
	QPoint prepareRippleStartPosition() const override;

private:
	const style::TwoIconButton &_st;

	int _unreadCount = 0;

};

class UserpicButton : public RippleButton {
public:
	enum class Role {
		ChangePhoto,
		OpenPhoto,
		OpenProfile,
		Custom,
	};

	UserpicButton(
		QWidget *parent,
		const QString &cropTitle,
		Role role,
		const style::UserpicButton &st);
	UserpicButton(
		QWidget *parent,
		not_null<::Window::SessionController*> controller,
		not_null<PeerData*> peer,
		Role role,
		const style::UserpicButton &st);
	UserpicButton(
		QWidget *parent,
		not_null<PeerData*> peer,
		Role role,
		const style::UserpicButton &st);

	void switchChangePhotoOverlay(bool enabled);
	void showSavedMessagesOnSelf(bool enabled);

	QImage takeResultImage() {
		return std::move(_result);
	}

protected:
	void paintEvent(QPaintEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void leaveEventHook(QEvent *e) override;

	void onStateChanged(State was, StateChangeSource source) override;

	QImage prepareRippleMask() const override;
	QPoint prepareRippleStartPosition() const override;

private:
	void prepare();
	void setImage(QImage &&image);
	void setupPeerViewers();
	void startAnimation();
	void processPeerPhoto();
	void processNewPeerPhoto();
	void startNewPhotoShowing();
	void prepareUserpicPixmap();
	QPoint countPhotoPosition() const;
	void startChangeOverlayAnimation();
	void updateCursorInChangeOverlay(QPoint localPos);
	void setCursorInChangeOverlay(bool inOverlay);
	void updateCursor();
	void updateVideo();
	bool showSavedMessages() const;
	bool showRepliesMessages() const;
	void checkStreamedIsStarted();
	bool createStreamingObjects(not_null<PhotoData*> photo);
	void clearStreaming();
	void handleStreamingUpdate(Media::Streaming::Update &&update);
	void handleStreamingError(Media::Streaming::Error &&error);
	void streamingReady(Media::Streaming::Information &&info);
	void paintUserpicFrame(Painter &p, QPoint photoPosition);

	void grabOldUserpic();
	void setClickHandlerByRole();
	void openPeerPhoto();
	void changePhotoLazy();
	void uploadNewPeerPhoto();

	const style::UserpicButton &_st;
	::Window::SessionController *_controller = nullptr;
	PeerData *_peer = nullptr;
	std::shared_ptr<Data::CloudImageView> _userpicView;
	QString _cropTitle;
	Role _role = Role::ChangePhoto;
	bool _notShownYet = true;
	bool _waiting = false;
	QPixmap _userpic, _oldUserpic;
	bool _userpicHasImage = false;
	bool _userpicCustom = false;
	InMemoryKey _userpicUniqueKey;
	Ui::Animations::Simple _a_appearance;
	QImage _result;
	std::unique_ptr<Media::Streaming::Instance> _streamed;
	PhotoData *_streamedPhoto = nullptr;

	bool _showSavedMessagesOnSelf = false;
	bool _canOpenPhoto = false;
	bool _cursorInChangeOverlay = false;
	bool _changeOverlayEnabled = false;
	Ui::Animations::Simple _changeOverlayShown;

};

class SilentToggle
	: public Ui::RippleButton
	, public Ui::AbstractTooltipShower {
public:
	SilentToggle(QWidget *parent, not_null<ChannelData*> channel);

	void setChecked(bool checked);
	bool checked() const {
		return _checked;
	}

	// AbstractTooltipShower interface
	QString tooltipText() const override;
	QPoint tooltipPos() const override;
	bool tooltipWindowActive() const override;

protected:
	void mouseMoveEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void leaveEventHook(QEvent *e) override;

	QImage prepareRippleMask() const override;
	QPoint prepareRippleStartPosition() const override;

private:
	const style::IconButton &_st;
	const QColor &_colorOver;

	not_null<ChannelData*> _channel;
	bool _checked = false;

	Ui::CrossLineAnimation _crossLine;
	Ui::Animations::Simple _crossLineAnimation;

};

} // namespace Ui
