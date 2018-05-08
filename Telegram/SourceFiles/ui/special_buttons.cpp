/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/special_buttons.h"

#include "styles/style_boxes.h"
#include "styles/style_history.h"
#include "dialogs/dialogs_layout.h"
#include "ui/effects/ripple_animation.h"
#include "ui/effects/radial_animation.h"
#include "ui/empty_userpic.h"
#include "data/data_photo.h"
#include "data/data_session.h"
#include "data/data_feed.h"
#include "history/history.h"
#include "core/file_utilities.h"
#include "boxes/photo_crop_box.h"
#include "boxes/confirm_box.h"
#include "window/window_controller.h"
#include "lang/lang_keys.h"
#include "auth_session.h"
#include "mainwidget.h"
#include "messenger.h"
#include "observer_peer.h"

namespace Ui {
namespace {

constexpr int kWideScale = 5;

template <typename Callback>
QPixmap CreateSquarePixmap(int width, Callback &&paintCallback) {
	auto size = QSize(width, width) * cIntRetinaFactor();
	auto image = QImage(size, QImage::Format_ARGB32_Premultiplied);
	image.setDevicePixelRatio(cRetinaFactor());
	image.fill(Qt::transparent);
	{
		Painter p(&image);
		paintCallback(p);
	}
	return App::pixmapFromImageInPlace(std::move(image));
};

template <typename Callback>
void SuggestPhoto(
		const QImage &image,
		PeerId peerForCrop,
		Callback &&callback) {
	auto badAspect = [](int a, int b) {
		return (a >= 10 * b);
	};
	if (image.isNull()
		|| badAspect(image.width(), image.height())
		|| badAspect(image.height(), image.width())) {
		Ui::show(
			Box<InformBox>(lang(lng_bad_photo)),
			LayerOption::KeepOther);
		return;
	}

	auto box = Ui::show(
		Box<PhotoCropBox>(image, peerForCrop),
		LayerOption::KeepOther);
	box->ready(
	) | rpl::start_with_next(
		std::forward<Callback>(callback),
		box->lifetime());
}

template <typename Callback>
void SuggestPhotoFile(
		const FileDialog::OpenResult &result,
		PeerId peerForCrop,
		Callback &&callback) {
	if (result.paths.isEmpty() && result.remoteContent.isEmpty()) {
		return;
	}

	auto image = [&] {
		if (!result.remoteContent.isEmpty()) {
			return App::readImage(result.remoteContent);
		} else if (!result.paths.isEmpty()) {
			return App::readImage(result.paths.front());
		}
		return QImage();
	}();
	SuggestPhoto(
		image,
		peerForCrop,
		std::forward<Callback>(callback));
}

template <typename Callback>
void ShowChoosePhotoBox(PeerId peerForCrop, Callback &&callback) {
	auto imgExtensions = cImgExtensions();
	auto filter = qsl("Image files (*")
		+ imgExtensions.join(qsl(" *"))
		+ qsl(");;")
		+ FileDialog::AllFilesFilter();
	auto handleChosenPhoto = [
		peerForCrop,
		callback = std::forward<Callback>(callback)
	](auto &&result) mutable {
		SuggestPhotoFile(result, peerForCrop, std::move(callback));
	};
	FileDialog::GetOpenPath(
		lang(lng_choose_image),
		filter,
		std::move(handleChosenPhoto));
}

} // namespace

HistoryDownButton::HistoryDownButton(QWidget *parent, const style::TwoIconButton &st) : RippleButton(parent, st.ripple)
, _st(st) {
	resize(_st.width, _st.height);
	setCursor(style::cur_pointer);

	hide();
}

QImage HistoryDownButton::prepareRippleMask() const {
	return Ui::RippleAnimation::ellipseMask(QSize(_st.rippleAreaSize, _st.rippleAreaSize));
}

QPoint HistoryDownButton::prepareRippleStartPosition() const {
	return mapFromGlobal(QCursor::pos()) - _st.rippleAreaPosition;
}

void HistoryDownButton::paintEvent(QPaintEvent *e) {
	Painter p(this);

	auto ms = getms();
	auto over = isOver();
	auto down = isDown();
	((over || down) ? _st.iconBelowOver : _st.iconBelow).paint(p, _st.iconPosition, width());
	paintRipple(p, _st.rippleAreaPosition.x(), _st.rippleAreaPosition.y(), ms);
	((over || down) ? _st.iconAboveOver : _st.iconAbove).paint(p, _st.iconPosition, width());
	if (_unreadCount > 0) {
		auto unreadString = QString::number(_unreadCount);
		if (unreadString.size() > 4) {
			unreadString = qsl("..") + unreadString.mid(unreadString.size() - 4);
		}

		Dialogs::Layout::UnreadBadgeStyle st;
		st.align = style::al_center;
		st.font = st::historyToDownBadgeFont;
		st.size = st::historyToDownBadgeSize;
		st.sizeId = Dialogs::Layout::UnreadBadgeInHistoryToDown;
		Dialogs::Layout::paintUnreadCount(p, unreadString, width(), 0, st, nullptr);
	}
}

void HistoryDownButton::setUnreadCount(int unreadCount) {
	if (_unreadCount != unreadCount) {
		_unreadCount = unreadCount;
		update();
	}
}

EmojiButton::EmojiButton(QWidget *parent, const style::IconButton &st)
: RippleButton(parent, st.ripple)
, _st(st) {
	resize(_st.width, _st.height);
	setCursor(style::cur_pointer);
}

void EmojiButton::paintEvent(QPaintEvent *e) {
	Painter p(this);

	auto ms = getms();

	p.fillRect(e->rect(), st::historyComposeAreaBg);
	paintRipple(p, _st.rippleAreaPosition.x(), _st.rippleAreaPosition.y(), ms, _rippleOverride ? &(*_rippleOverride)->c : nullptr);

	const auto loadingState = _loading
		? _loading->computeState()
		: Ui::InfiniteRadialAnimation::State{ 0., 0, FullArcLength };
	p.setOpacity(1. - loadingState.shown);

	auto over = isOver();
	auto icon = _iconOverride ? _iconOverride : &(over ? _st.iconOver : _st.icon);
	icon->paint(p, _st.iconPosition, width());

	p.setOpacity(1.);
	auto pen = _colorOverride ? (*_colorOverride)->p : (over ? st::historyEmojiCircleFgOver : st::historyEmojiCircleFg)->p;
	pen.setWidth(st::historyEmojiCircleLine);
	pen.setCapStyle(Qt::RoundCap);
	p.setPen(pen);
	p.setBrush(Qt::NoBrush);

	PainterHighQualityEnabler hq(p);
	QRect inner(QPoint((width() - st::historyEmojiCircle.width()) / 2, st::historyEmojiCircleTop), st::historyEmojiCircle);
	if (loadingState.arcLength < FullArcLength) {
		p.drawArc(inner, loadingState.arcFrom, loadingState.arcLength);
	} else {
		p.drawEllipse(inner);
	}
}

void EmojiButton::setLoading(bool loading) {
	if (loading && !_loading) {
		_loading = std::make_unique<Ui::InfiniteRadialAnimation>(
			animation(this, &EmojiButton::step_loading),
			st::defaultInfiniteRadialAnimation);
	}
	if (loading) {
		_loading->start();
	} else if (_loading) {
		_loading->stop();
	}
}

void EmojiButton::setColorOverrides(const style::icon *iconOverride, const style::color *colorOverride, const style::color *rippleOverride) {
	_iconOverride = iconOverride;
	_colorOverride = colorOverride;
	_rippleOverride = rippleOverride;
	update();
}

void EmojiButton::onStateChanged(State was, StateChangeSource source) {
	RippleButton::onStateChanged(was, source);
	auto wasOver = static_cast<bool>(was & StateFlag::Over);
	if (isOver() != wasOver) {
		update();
	}
}

QPoint EmojiButton::prepareRippleStartPosition() const {
	return mapFromGlobal(QCursor::pos()) - _st.rippleAreaPosition;
}

QImage EmojiButton::prepareRippleMask() const {
	return RippleAnimation::ellipseMask(QSize(_st.rippleAreaSize, _st.rippleAreaSize));
}

SendButton::SendButton(QWidget *parent) : RippleButton(parent, st::historyReplyCancel.ripple) {
	resize(st::historySendSize);
}

void SendButton::setType(Type type) {
	if (_type != type) {
		_contentFrom = grabContent();
		_type = type;
		_a_typeChanged.finish();
		_contentTo = grabContent();
		_a_typeChanged.start([this] { update(); }, 0., 1., st::historyRecordVoiceDuration);
		update();
	}
	if (_type != Type::Record) {
		_recordActive = false;
		_a_recordActive.finish();
	}
}

void SendButton::setRecordActive(bool recordActive) {
	if (_recordActive != recordActive) {
		_recordActive = recordActive;
		_a_recordActive.start([this] { recordAnimationCallback(); }, _recordActive ? 0. : 1., _recordActive ? 1. : 0, st::historyRecordVoiceDuration);
		update();
	}
}

void SendButton::finishAnimating() {
	_a_typeChanged.finish();
	_a_recordActive.finish();
	update();
}

void SendButton::mouseMoveEvent(QMouseEvent *e) {
	AbstractButton::mouseMoveEvent(e);
	if (_recording) {
		if (_recordUpdateCallback) {
			_recordUpdateCallback(e->globalPos());
		}
	}
}

void SendButton::paintEvent(QPaintEvent *e) {
	Painter p(this);

	auto ms = getms();
	auto over = (isDown() || isOver());
	auto changed = _a_typeChanged.current(ms, 1.);
	if (changed < 1.) {
		PainterHighQualityEnabler hq(p);
		p.setOpacity(1. - changed);
		auto targetRect = QRect((1 - kWideScale) / 2 * width(), (1 - kWideScale) / 2 * height(), kWideScale * width(), kWideScale * height());
		auto hiddenWidth = anim::interpolate(0, (1 - kWideScale) / 2 * width(), changed);
		auto hiddenHeight = anim::interpolate(0, (1 - kWideScale) / 2 * height(), changed);
		p.drawPixmap(targetRect.marginsAdded(QMargins(hiddenWidth, hiddenHeight, hiddenWidth, hiddenHeight)), _contentFrom);
		p.setOpacity(changed);
		auto shownWidth = anim::interpolate((1 - kWideScale) / 2 * width(), 0, changed);
		auto shownHeight = anim::interpolate((1 - kWideScale) / 2 * height(), 0, changed);
		p.drawPixmap(targetRect.marginsAdded(QMargins(shownWidth, shownHeight, shownWidth, shownHeight)), _contentTo);
	} else if (_type == Type::Record) {
		auto recordActive = recordActiveRatio();
		auto rippleColor = anim::color(st::historyAttachEmoji.ripple.color, st::historyRecordVoiceRippleBgActive, recordActive);
		paintRipple(p, (width() - st::historyAttachEmoji.rippleAreaSize) / 2, st::historyAttachEmoji.rippleAreaPosition.y(), ms, &rippleColor);

		auto fastIcon = [&] {
			if (recordActive == 1.) {
				return &st::historyRecordVoiceActive;
			} else if (over) {
				return &st::historyRecordVoiceOver;
			}
			return &st::historyRecordVoice;
		};
		fastIcon()->paintInCenter(p, rect());
		if (recordActive > 0. && recordActive < 1.) {
			p.setOpacity(recordActive);
			st::historyRecordVoiceActive.paintInCenter(p, rect());
			p.setOpacity(1.);
		}
	} else if (_type == Type::Save) {
		auto &saveIcon = over ? st::historyEditSaveIconOver : st::historyEditSaveIcon;
		saveIcon.paint(p, st::historySendIconPosition, width());
	} else if (_type == Type::Cancel) {
		paintRipple(p, (width() - st::historyAttachEmoji.rippleAreaSize) / 2, st::historyAttachEmoji.rippleAreaPosition.y(), ms);

		auto &cancelIcon = over ? st::historyReplyCancelIconOver : st::historyReplyCancelIcon;
		cancelIcon.paintInCenter(p, rect());
	} else {
		auto &sendIcon = over ? st::historySendIconOver : st::historySendIcon;
		sendIcon.paint(p, st::historySendIconPosition, width());
	}
}

void SendButton::onStateChanged(State was, StateChangeSource source) {
	RippleButton::onStateChanged(was, source);

	auto down = (state() & StateFlag::Down);
	if ((was & StateFlag::Down) != down) {
		if (down) {
			if (_type == Type::Record) {
				_recording = true;
				if (_recordStartCallback) {
					_recordStartCallback();
				}
			}
		} else if (_recording) {
			_recording = false;
			if (_recordStopCallback) {
				_recordStopCallback(_recordActive);
			}
		}
	}
}

QPixmap SendButton::grabContent() {
	auto result = QImage(kWideScale * size() * cIntRetinaFactor(), QImage::Format_ARGB32_Premultiplied);
	result.setDevicePixelRatio(cRetinaFactor());
	result.fill(Qt::transparent);
	{
		Painter p(&result);
		p.drawPixmap(
			(kWideScale - 1) / 2 * width(),
			(kWideScale - 1) / 2 * height(),
			GrabWidget(this));
	}
	return App::pixmapFromImageInPlace(std::move(result));
}

QImage SendButton::prepareRippleMask() const {
	auto size = (_type == Type::Record) ? st::historyAttachEmoji.rippleAreaSize : st::historyReplyCancel.rippleAreaSize;
	return Ui::RippleAnimation::ellipseMask(QSize(size, size));
}

QPoint SendButton::prepareRippleStartPosition() const {
	auto real = mapFromGlobal(QCursor::pos());
	auto size = (_type == Type::Record) ? st::historyAttachEmoji.rippleAreaSize : st::historyReplyCancel.rippleAreaSize;
	auto y = (_type == Type::Record) ? st::historyAttachEmoji.rippleAreaPosition.y() : (height() - st::historyReplyCancel.rippleAreaSize) / 2;
	return real - QPoint((width() - size) / 2, y);
}

void SendButton::recordAnimationCallback() {
	update();
	if (_recordAnimationCallback) {
		_recordAnimationCallback();
	}
}

UserpicButton::UserpicButton(
	QWidget *parent,
	PeerId peerForCrop,
	Role role,
	const style::UserpicButton &st)
: RippleButton(parent, st.changeButton.ripple)
, _st(st)
, _peerForCrop(peerForCrop)
, _role(role) {
	Expects(_role == Role::ChangePhoto);

	_waiting = false;
	prepare();
}

UserpicButton::UserpicButton(
	QWidget *parent,
	not_null<Window::Controller*> controller,
	not_null<PeerData*> peer,
	Role role,
	const style::UserpicButton &st)
: RippleButton(parent, st.changeButton.ripple)
, _st(st)
, _controller(controller)
, _peer(peer)
, _peerForCrop(_peer->id)
, _role(role) {
	processPeerPhoto();
	prepare();
	setupPeerViewers();
}

void UserpicButton::prepare() {
	resize(_st.size);
	_notShownYet = _waiting;
	if (!_waiting) {
		prepareUserpicPixmap();
	}
	setClickHandlerByRole();
}

void UserpicButton::setClickHandlerByRole() {
	switch (_role) {
	case Role::ChangePhoto:
		addClickHandler(App::LambdaDelayed(
			_st.changeButton.ripple.hideDuration,
			this,
			[this] { changePhotoLazy(); }));
		break;

	case Role::OpenPhoto:
		addClickHandler([this] {
			openPeerPhoto();
		});
		break;

	case Role::OpenProfile:
		addClickHandler([this] {
			Expects(_controller != nullptr);

			_controller->showPeerInfo(_peer);
		});
		break;
	}
}

void UserpicButton::changePhotoLazy() {
	auto callback = base::lambda_guarded(
		this,
		[this](QImage &&image) { setImage(std::move(image)); });
	ShowChoosePhotoBox(_peerForCrop, std::move(callback));
}

void UserpicButton::uploadNewPeerPhoto() {
	auto callback = base::lambda_guarded(
		this,
		[this](QImage &&image) {
			Messenger::Instance().uploadProfilePhoto(
				std::move(image),
				_peer->id
			);
		});
	ShowChoosePhotoBox(_peerForCrop, std::move(callback));
}

void UserpicButton::openPeerPhoto() {
	Expects(_peer != nullptr);
	Expects(_controller != nullptr);

	if (_changeOverlayEnabled && _cursorInChangeOverlay) {
		uploadNewPeerPhoto();
		return;
	}

	const auto id = _peer->userpicPhotoId();
	if (!id) {
		return;
	}
	const auto photo = Auth().data().photo(id);
	if (photo->date) {
		Messenger::Instance().showPhoto(photo, _peer);
	}
}

void UserpicButton::setupPeerViewers() {
	Notify::PeerUpdateViewer(
		_peer,
		Notify::PeerUpdate::Flag::PhotoChanged
	) | rpl::start_with_next([this] {
		processNewPeerPhoto();
		update();
	}, lifetime());
	base::ObservableViewer(
		Auth().downloaderTaskFinished()
	) | rpl::start_with_next([this] {
		if (_waiting && _peer->userpicLoaded()) {
			_waiting = false;
			startNewPhotoShowing();
		}
	}, lifetime());
}

void UserpicButton::paintEvent(QPaintEvent *e) {
	Painter p(this);
	if (!_waiting && _notShownYet) {
		_notShownYet = false;
		startAnimation();
	}

	auto photoPosition = countPhotoPosition();
	auto photoLeft = photoPosition.x();
	auto photoTop = photoPosition.y();

	auto ms = getms();
	if (showSavedMessages()) {
		Ui::EmptyUserpic::PaintSavedMessages(
			p,
			photoPosition.x(),
			photoPosition.y(),
			width(),
			_st.photoSize);
	} else {
		if (_a_appearance.animating(ms)) {
			p.drawPixmapLeft(photoPosition, width(), _oldUserpic);
			p.setOpacity(_a_appearance.current());
		}
		p.drawPixmapLeft(photoPosition, width(), _userpic);
	}

	if (_role == Role::ChangePhoto) {
		auto over = isOver() || isDown();
		if (over) {
			PainterHighQualityEnabler hq(p);
			p.setPen(Qt::NoPen);
			p.setBrush(_userpicHasImage
				? st::msgDateImgBg
				: _st.changeButton.textBgOver);
			p.drawEllipse(
				photoLeft,
				photoTop,
				_st.photoSize,
				_st.photoSize);
		}
		paintRipple(
			p,
			photoLeft,
			photoTop,
			ms,
			_userpicHasImage
				? &st::shadowFg->c
				: &_st.changeButton.ripple.color->c);
		if (over || !_userpicHasImage) {
			auto iconLeft = (_st.changeIconPosition.x() < 0)
				? (_st.photoSize - _st.changeIcon.width()) / 2
				: _st.changeIconPosition.x();
			auto iconTop = (_st.changeIconPosition.y() < 0)
				? (_st.photoSize - _st.changeIcon.height()) / 2
				: _st.changeIconPosition.y();
			_st.changeIcon.paint(
				p,
				photoLeft + iconLeft,
				photoTop + iconTop,
				width());
		}
	} else if (_changeOverlayEnabled) {
		auto current = _changeOverlayShown.current(
			ms,
			(isOver() || isDown()) ? 1. : 0.);
		auto barHeight = anim::interpolate(
			0,
			_st.uploadHeight,
			current);
		if (barHeight > 0) {
			auto barLeft = photoLeft;
			auto barTop = photoTop + _st.photoSize - barHeight;
			auto rect = QRect(
				barLeft,
				barTop,
				_st.photoSize,
				barHeight);
			p.setClipRect(rect);
			{
				PainterHighQualityEnabler hq(p);
				p.setPen(Qt::NoPen);
				p.setBrush(_st.uploadBg);
				p.drawEllipse(
					photoLeft,
					photoTop,
					_st.photoSize,
					_st.photoSize);
			}
			auto iconLeft = (_st.uploadIconPosition.x() < 0)
				? (_st.photoSize - _st.uploadIcon.width()) / 2
				: _st.uploadIconPosition.x();
			auto iconTop = (_st.uploadIconPosition.y() < 0)
				? (_st.uploadHeight - _st.uploadIcon.height()) / 2
				: _st.uploadIconPosition.y();
			if (iconTop < barHeight) {
				_st.uploadIcon.paint(
					p,
					barLeft + iconLeft,
					barTop + iconTop,
					width());
			}
		}
	}
}

QPoint UserpicButton::countPhotoPosition() const {
	auto photoLeft = (_st.photoPosition.x() < 0)
		? (width() - _st.photoSize) / 2
		: _st.photoPosition.x();
	auto photoTop = (_st.photoPosition.y() < 0)
		? (height() - _st.photoSize) / 2
		: _st.photoPosition.y();
	return { photoLeft, photoTop };
}

QImage UserpicButton::prepareRippleMask() const {
	return Ui::RippleAnimation::ellipseMask(QSize(
		_st.photoSize,
		_st.photoSize));
}

QPoint UserpicButton::prepareRippleStartPosition() const {
	return (_role == Role::ChangePhoto)
		? mapFromGlobal(QCursor::pos()) - countPhotoPosition()
		: DisabledRippleStartPosition();
}

void UserpicButton::processPeerPhoto() {
	Expects(_peer != nullptr);

	_waiting = !_peer->userpicLoaded();
	if (_waiting) {
		_peer->loadUserpic(true);
	}
	if (_role == Role::OpenPhoto) {
		if (_peer->userpicPhotoUnknown()) {
			_peer->updateFullForced();
		}
		_canOpenPhoto = (_peer->userpicPhotoId() != 0);
		updateCursor();
	}
}

void UserpicButton::updateCursor() {
	Expects(_role == Role::OpenPhoto);

	auto pointer = _canOpenPhoto
		|| (_changeOverlayEnabled && _cursorInChangeOverlay);
	setPointerCursor(pointer);
}

void UserpicButton::mouseMoveEvent(QMouseEvent *e) {
	RippleButton::mouseMoveEvent(e);
	if (_role == Role::OpenPhoto) {
		updateCursorInChangeOverlay(e->pos());
	}
}

void UserpicButton::updateCursorInChangeOverlay(QPoint localPos) {
	auto photoPosition = countPhotoPosition();
	auto overlayRect = QRect(
		photoPosition.x(),
		photoPosition.y() + _st.photoSize - _st.uploadHeight,
		_st.photoSize,
		_st.uploadHeight);
	auto inOverlay = overlayRect.contains(localPos);
	setCursorInChangeOverlay(inOverlay);
}

void UserpicButton::leaveEventHook(QEvent *e) {
	if (_role == Role::OpenPhoto) {
		setCursorInChangeOverlay(false);
	}
	return RippleButton::leaveEventHook(e);
}

void UserpicButton::setCursorInChangeOverlay(bool inOverlay) {
	Expects(_role == Role::OpenPhoto);

	if (_cursorInChangeOverlay != inOverlay) {
		_cursorInChangeOverlay = inOverlay;
		updateCursor();
	}
}

void UserpicButton::processNewPeerPhoto() {
	if (_userpicCustom) {
		return;
	}
	processPeerPhoto();
	if (!_waiting) {
		grabOldUserpic();
		startNewPhotoShowing();
	}
}

void UserpicButton::grabOldUserpic() {
	auto photoRect = QRect(
		countPhotoPosition(),
		QSize(_st.photoSize, _st.photoSize)
	);
	_oldUserpic = GrabWidget(this, photoRect);
}

void UserpicButton::startNewPhotoShowing() {
	auto oldUniqueKey = _userpicUniqueKey;
	prepareUserpicPixmap();
	update();

	if (_notShownYet) {
		return;
	}
	if (oldUniqueKey != _userpicUniqueKey
		|| _a_appearance.animating()) {
		startAnimation();
	}
}

void UserpicButton::startAnimation() {
	_a_appearance.finish();
	_a_appearance.start([this] { update(); }, 0, 1, _st.duration);
}

void UserpicButton::switchChangePhotoOverlay(bool enabled) {
	Expects(_role == Role::OpenPhoto);

	if (_changeOverlayEnabled != enabled) {
		_changeOverlayEnabled = enabled;
		if (enabled) {
			if (isOver()) {
				startChangeOverlayAnimation();
			}
			updateCursorInChangeOverlay(
				mapFromGlobal(QCursor::pos()));
		} else {
			_changeOverlayShown.finish();
			update();
		}
	}
}

void UserpicButton::showSavedMessagesOnSelf(bool enabled) {
	if (_showSavedMessagesOnSelf != enabled) {
		_showSavedMessagesOnSelf = enabled;
		update();
	}
}

bool UserpicButton::showSavedMessages() const {
	return _showSavedMessagesOnSelf && _peer && _peer->isSelf();
}

void UserpicButton::startChangeOverlayAnimation() {
	auto over = isOver() || isDown();
	_changeOverlayShown.start(
		[this] { update(); },
		over ? 0. : 1.,
		over ? 1. : 0.,
		st::slideWrapDuration);
	update();
}

void UserpicButton::onStateChanged(
		State was,
		StateChangeSource source) {
	RippleButton::onStateChanged(was, source);
	if (_changeOverlayEnabled) {
		auto mask = (StateFlag::Over | StateFlag::Down);
		auto wasOver = (was & mask) != 0;
		auto nowOver = (state() & mask) != 0;
		if (wasOver != nowOver) {
			startChangeOverlayAnimation();
		}
	}
}

void UserpicButton::setImage(QImage &&image) {
	grabOldUserpic();

	auto size = QSize(_st.photoSize, _st.photoSize);
	auto small = image.scaled(
		size * cIntRetinaFactor(),
		Qt::IgnoreAspectRatio,
		Qt::SmoothTransformation);
	Images::prepareCircle(small);
	_userpic = App::pixmapFromImageInPlace(std::move(small));
	_userpic.setDevicePixelRatio(cRetinaFactor());
	_userpicCustom = _userpicHasImage = true;
	_result = std::move(image);

	startNewPhotoShowing();
}

void UserpicButton::prepareUserpicPixmap() {
	if (_userpicCustom) {
		return;
	}
	auto size = _st.photoSize;
	auto paintButton = [&](Painter &p, const style::color &color) {
		PainterHighQualityEnabler hq(p);
		p.setBrush(color);
		p.setPen(Qt::NoPen);
		p.drawEllipse(0, 0, size, size);
	};
	_userpicHasImage = _peer
		? (_peer->currentUserpic() || _role != Role::ChangePhoto)
		: false;
	_userpic = CreateSquarePixmap(size, [&](Painter &p) {
		if (_userpicHasImage) {
			_peer->paintUserpic(p, 0, 0, _st.photoSize);
		} else {
			paintButton(p, _st.changeButton.textBg);
		}
	});
	_userpicUniqueKey = _userpicHasImage
		? _peer->userpicUniqueKey()
		: StorageKey();
}

FeedUserpicButton::FeedUserpicButton(
	QWidget *parent,
	not_null<Window::Controller*> controller,
	not_null<Data::Feed*> feed,
	const style::FeedUserpicButton &st)
: AbstractButton(parent)
, _st(st)
, _controller(controller)
, _feed(feed) {
	prepare();
}

void FeedUserpicButton::prepare() {
	resize(_st.size);

	Auth().data().feedUpdated(
	) | rpl::filter([=](const Data::FeedUpdate &update) {
		return (update.feed == _feed)
			&& (update.flag == Data::FeedUpdateFlag::Channels);
	}) | rpl::start_with_next([=] {
		crl::on_main(this, [=] { checkParts(); });
	}, lifetime());

	refreshParts();
}

void FeedUserpicButton::checkParts() {
	if (!partsAreValid()) {
		refreshParts();
	}
}

bool FeedUserpicButton::partsAreValid() const {
	const auto &channels = _feed->channels();
	const auto count = std::min(int(channels.size()), 4);
	if (count != _parts.size()) {
		return false;
	}
	for (auto i = 0; i != count; ++i) {
		if (channels[i]->peer != _parts[i].channel) {
			return false;
		}
	}
	return true;
}

void FeedUserpicButton::refreshParts() {
	const auto &channels = _feed->channels();
	const auto count = std::min(int(channels.size()), 4);

	const auto createButton = [&](not_null<ChannelData*> channel) {
		auto result = base::make_unique_q<Ui::UserpicButton>(
			this,
			_controller,
			channel,
			Ui::UserpicButton::Role::Custom,
			_st.innerPart);
		result->setAttribute(Qt::WA_TransparentForMouseEvents);
		result->show();
		return result;
	};

	const auto position = countInnerPosition();
	auto x = position.x();
	auto y = position.y();
	const auto delta = _st.innerSize - _st.innerPart.photoSize;
	_parts.clear();
	for (auto i = 0; i != count; ++i) {
		const auto channel = channels[i]->peer->asChannel();
		_parts.push_back({ channel, createButton(channel) });
		_parts.back().button->moveToLeft(x, y);
		switch (i) {
		case 0:
		case 2: x += delta; break;
		case 1: x -= delta; y += delta; break;
		}
	}
}

QPoint FeedUserpicButton::countInnerPosition() const {
	auto innerLeft = (_st.innerPosition.x() < 0)
		? (width() - _st.innerSize) / 2
		: _st.innerPosition.x();
	auto innerTop = (_st.innerPosition.y() < 0)
		? (height() - _st.innerSize) / 2
		: _st.innerPosition.y();
	return { innerLeft, innerTop };
}

SilentToggle::SilentToggle(QWidget *parent, not_null<ChannelData*> channel)
: IconButton(parent, st::historySilentToggle)
, _channel(channel)
, _checked(_channel->notifySilentPosts()) {
	Expects(!_channel->notifySettingsUnknown());

	if (_checked) {
		refreshIconOverrides();
	}
	setMouseTracking(true);
}

void SilentToggle::mouseMoveEvent(QMouseEvent *e) {
	IconButton::mouseMoveEvent(e);
	if (rect().contains(e->pos())) {
		Ui::Tooltip::Show(1000, this);
	} else {
		Ui::Tooltip::Hide();
	}
}

void SilentToggle::setChecked(bool checked) {
	if (_checked != checked) {
		_checked = checked;
		refreshIconOverrides();
	}
}

void SilentToggle::refreshIconOverrides() {
	const auto iconOverride = _checked
		? &st::historySilentToggleOn
		: nullptr;
	const auto iconOverOverride = _checked
		? &st::historySilentToggleOnOver
		: nullptr;
	setIconOverride(iconOverride, iconOverOverride);
}

void SilentToggle::leaveEventHook(QEvent *e) {
	IconButton::leaveEventHook(e);
	Ui::Tooltip::Hide();
}

void SilentToggle::mouseReleaseEvent(QMouseEvent *e) {
	setChecked(!_checked);
	IconButton::mouseReleaseEvent(e);
	Ui::Tooltip::Show(0, this);
	const auto silentState = _checked
		? Data::NotifySettings::SilentPostsChange::Silent
		: Data::NotifySettings::SilentPostsChange::Notify;
	App::main()->updateNotifySettings(
		_channel,
		Data::NotifySettings::MuteChange::Ignore,
		silentState);
}

QString SilentToggle::tooltipText() const {
	return lang(_checked ? lng_wont_be_notified : lng_will_be_notified);
}

QPoint SilentToggle::tooltipPos() const {
	return QCursor::pos();
}

} // namespace Ui
