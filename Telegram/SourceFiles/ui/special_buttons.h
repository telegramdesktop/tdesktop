/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/widgets/buttons.h"
#include "ui/widgets/tooltip.h"
#include "styles/style_window.h"
#include "styles/style_widgets.h"

class PeerData;

namespace Window {
class Controller;
} // namespace Window

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

class EmojiButton : public RippleButton {
public:
	EmojiButton(QWidget *parent, const style::IconButton &st);

	void setLoading(bool loading);
	void setColorOverrides(const style::icon *iconOverride, const style::color *colorOverride, const style::color *rippleOverride);

protected:
	void paintEvent(QPaintEvent *e) override;
	void onStateChanged(State was, StateChangeSource source) override;

	QImage prepareRippleMask() const override;
	QPoint prepareRippleStartPosition() const override;

private:
	void step_loading(TimeMs ms, bool timer) {
		if (timer) {
			update();
		}
	}

	const style::IconButton &_st;

	bool _loading = false;
	Animation a_loading;
	BasicAnimation _a_loading;

	const style::icon *_iconOverride = nullptr;
	const style::color *_colorOverride = nullptr;
	const style::color *_rippleOverride = nullptr;

};

class SendButton : public RippleButton {
public:
	SendButton(QWidget *parent);

	enum class Type {
		Send,
		Save,
		Record,
		Cancel,
	};
	Type type() const {
		return _type;
	}
	void setType(Type state);
	void setRecordActive(bool recordActive);
	void finishAnimating();

	void setRecordStartCallback(base::lambda<void()> callback) {
		_recordStartCallback = std::move(callback);
	}
	void setRecordUpdateCallback(base::lambda<void(QPoint globalPos)> callback) {
		_recordUpdateCallback = std::move(callback);
	}
	void setRecordStopCallback(base::lambda<void(bool active)> callback) {
		_recordStopCallback = std::move(callback);
	}
	void setRecordAnimationCallback(base::lambda<void()> callback) {
		_recordAnimationCallback = std::move(callback);
	}

	float64 recordActiveRatio() {
		return _a_recordActive.current(getms(), _recordActive ? 1. : 0.);
	}

protected:
	void mouseMoveEvent(QMouseEvent *e) override;
	void paintEvent(QPaintEvent *e) override;
	void onStateChanged(State was, StateChangeSource source) override;

	QImage prepareRippleMask() const override;
	QPoint prepareRippleStartPosition() const override;

private:
	void recordAnimationCallback();
	QPixmap grabContent();

	Type _type = Type::Send;
	bool _recordActive = false;
	QPixmap _contentFrom, _contentTo;

	Animation _a_typeChanged;
	Animation _a_recordActive;

	bool _recording = false;
	base::lambda<void()> _recordStartCallback;
	base::lambda<void(bool active)> _recordStopCallback;
	base::lambda<void(QPoint globalPos)> _recordUpdateCallback;
	base::lambda<void()> _recordAnimationCallback;

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
		PeerId peerForCrop,
		Role role,
		const style::UserpicButton &st);
	UserpicButton(
		QWidget *parent,
		not_null<Window::Controller*> controller,
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
	bool showSavedMessages() const;

	void grabOldUserpic();
	void setClickHandlerByRole();
	void openPeerPhoto();
	void changePhotoLazy();
	void uploadNewPeerPhoto();

	const style::UserpicButton &_st;
	Window::Controller *_controller = nullptr;
	PeerData *_peer = nullptr;
	PeerId _peerForCrop = 0;
	Role _role = Role::ChangePhoto;
	bool _notShownYet = true;
	bool _waiting = false;
	QPixmap _userpic, _oldUserpic;
	bool _userpicHasImage = false;
	bool _userpicCustom = false;
	StorageKey _userpicUniqueKey;
	Animation _a_appearance;
	QImage _result;

	bool _showSavedMessagesOnSelf = false;
	bool _canOpenPhoto = false;
	bool _cursorInChangeOverlay = false;
	bool _changeOverlayEnabled = false;
	Animation _changeOverlayShown;

};

class SilentToggle : public Ui::IconButton, public Ui::AbstractTooltipShower {
public:
	SilentToggle(QWidget *parent, not_null<ChannelData*> channel);

	void setChecked(bool checked);
	bool checked() const {
		return _checked;
	}

	// AbstractTooltipShower interface
	QString tooltipText() const override;
	QPoint tooltipPos() const override;

protected:
	void mouseMoveEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void leaveEventHook(QEvent *e) override;

private:
	void refreshIconOverrides();

	not_null<ChannelData*> _channel;
	bool _checked = false;

};

} // namespace Ui
