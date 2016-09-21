/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2016 John Preston, https://desktop.telegram.org
*/
#include "stdafx.h"
#include "title.h"

#include "lang.h"
#include "mainwidget.h"
#include "mainwindow.h"
#include "application.h"
#include "boxes/contactsbox.h"
#include "boxes/aboutbox.h"
#include "media/media_audio.h"
#include "media/player/media_player_button.h"

class TitleWidget::Hider : public TWidget {
public:
	Hider(QWidget *parent);

	using ClickedCallback = base::lambda_unique<void()>;
	void setClickedCallback(ClickedCallback &&callback) {
		_callback = std_::move(callback);
	}
	void setLevel(float64 level);

protected:
	void paintEvent(QPaintEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;

private:
	ClickedCallback _callback;
	float64 _level = 0;

};

TitleWidget::Hider::Hider(QWidget *parent) : TWidget(parent) {
}

void TitleWidget::Hider::paintEvent(QPaintEvent *e) {
	QPainter p(this);
	p.setOpacity(_level * st::layerAlpha);
	p.fillRect(App::main()->dlgsWidth(), 0, width() - App::main()->dlgsWidth(), height(), st::layerBg->b);
}

void TitleWidget::Hider::mousePressEvent(QMouseEvent *e) {
	if (e->button() == Qt::LeftButton && _callback) {
		_callback();
	}
}

void TitleWidget::Hider::setLevel(float64 level) {
	_level = level;
	update();
}

TitleWidget::TitleWidget(QWidget *parent) : TWidget(parent)
, _cancel(this, lang(lng_cancel), st::titleTextButton)
, _settings(this, lang(lng_menu_settings), st::titleTextButton)
, _contacts(this, lang(lng_menu_contacts), st::titleTextButton)
, _about(this, lang(lng_menu_about), st::titleTextButton)
, _lock(this)
, _update(this)
, _minimize(this)
, _maximize(this)
, _restore(this)
, _close(this)
, _a_update(animation(this, &TitleWidget::step_update))
, lastMaximized(!(parent->windowState() & Qt::WindowMaximized)) {
	setGeometry(0, 0, parent->width(), st::titleHeight);
	setAttribute(Qt::WA_OpaquePaintEvent);

	onWindowStateChanged();
	updateControlsVisibility();

	connect(&_cancel, SIGNAL(clicked()), this, SIGNAL(hiderClicked()));
	connect(&_settings, SIGNAL(clicked()), parent, SLOT(showSettings()));
	connect(&_contacts, SIGNAL(clicked()), this, SLOT(onContacts()));
	connect(&_about, SIGNAL(clicked()), this, SLOT(onAbout()));
	connect(parent->windowHandle(), SIGNAL(windowStateChanged(Qt::WindowState)), this, SLOT(onWindowStateChanged(Qt::WindowState)));

#ifndef TDESKTOP_DISABLE_AUTOUPDATE
	Sandbox::connect(SIGNAL(updateReady()), this, SLOT(updateControlsVisibility()));
#endif

	subscribe(Adaptive::Changed(), [this]() { updateAdaptiveLayout(); });
	if (auto player = audioPlayer()) {
		subscribe(player, [this](const AudioMsgId &audio) {
			if (audio.type() == AudioMsgId::Type::Song) {
				handleSongUpdate(audio);
			}
		});
	}

    if (cPlatform() != dbipWindows) {
        _minimize.hide();
        _maximize.hide();
        _restore.hide();
        _close.hide();
    }
}

void TitleWidget::paintEvent(QPaintEvent *e) {
	Painter p(this);
	p.fillRect(rect(), st::titleBg);
	if (!_cancel.isHidden()) {
		p.setPen(st::titleTextButton.color->p);
		p.setFont(st::titleTextButton.font->f);
		bool inlineSwitchChoose = (App::main() && App::main()->selectingPeerForInlineSwitch());
		auto chooseText = lang(inlineSwitchChoose ? lng_inline_switch_choose : lng_forward_choose);
		p.drawText(st::titleMenuOffset - st::titleTextButton.width / 2, st::titleTextButton.textTop + st::titleTextButton.font->ascent, chooseText);
	}
	p.drawSprite(st::titleIconPos, st::titleIconImg);
	if (Adaptive::OneColumn() && !_counter.isNull() && App::main()) {
		p.drawPixmap(st::titleIconPos.x() + st::titleIconImg.pxWidth() - (_counter.width() / cIntRetinaFactor()), st::titleIconPos.y() + st::titleIconImg.pxHeight() - (_counter.height() / cIntRetinaFactor()), _counter);
	}
}

void TitleWidget::step_update(float64 ms, bool timer) {
	float64 phase = sin(M_PI_2 * (ms / st::updateBlinkDuration));
	if (phase < 0) phase = -phase;
	_update.setOverLevel(phase);
}

void TitleWidget::setHideLevel(float64 level) {
	if (level != hideLevel) {
		hideLevel = level;
		if (hideLevel) {
			if (!_hider) {
				_hider.create(this);
				_hider->setGeometry(rect());
				_hider->setClickedCallback([this]() { emit hiderClicked(); });
				_hider->setVisible(!Adaptive::OneColumn());
			}
			_hider->setLevel(hideLevel);
		} else {
			if (_hider) {
				_hider.destroyDelayed();
			}
		}
	}
}

void TitleWidget::handleSongUpdate(const AudioMsgId &audioId) {
	t_assert(audioId.type() == AudioMsgId::Type::Song);

	AudioMsgId playing;
	auto playbackState = audioPlayer()->currentState(&playing, audioId.type());
	if (playing == audioId) {
		auto songIsPlaying = !(playbackState.state & AudioPlayerStoppedMask) && (playbackState.state != AudioPlayerFinishing);
		if (songIsPlaying && !_player) {
			_player.create(this);
			updateControlsVisibility();
		}
	}
}

void TitleWidget::onContacts() {
	if (App::wnd() && App::wnd()->isHidden()) App::wnd()->showFromTray();

	if (!App::self()) return;
	Ui::showLayer(new ContactsBox());
}

void TitleWidget::onAbout() {
	if (App::wnd() && App::wnd()->isHidden()) App::wnd()->showFromTray();
	Ui::showLayer(new AboutBox());
}

void TitleWidget::updateControlsPosition() {
	QPoint p(width() - ((cPlatform() == dbipWindows && lastMaximized) ? 0 : st::sysBtnDelta), 0);

	if (!_update.isHidden()) {
		p.setX(p.x() - _update.width());
		_update.move(p);
		if (!_lock.isHidden()) {
			p.setX(p.x() - _lock.width());
			_lock.move(p);
			p.setX(p.x() + _lock.width());
		}
		p.setX(p.x() + _update.width());
	}
	_cancel.move(p.x() - _cancel.width(), 0);

	if (cPlatform() == dbipWindows) {
		p.setX(p.x() - _close.width());
		_close.move(p);

		p.setX(p.x() - _maximize.width());
		_restore.move(p); _maximize.move(p);

		p.setX(p.x() - _minimize.width());
		_minimize.move(p);
	}
	if (_update.isHidden() && !_lock.isHidden()) {
		p.setX(p.x() - _lock.width());
		_lock.move(p);
	}
	if (_player) {
		p.setX(p.x() - _player->width());
		_player->move(p);
	}

	_settings.move(st::titleMenuOffset, 0);
	if (_contacts.isHidden()) {
		_about.move(_settings.x() + _settings.width(), 0);
	} else {
		_contacts.move(_settings.x() + _settings.width(), 0);
		_about.move(_contacts.x() + _contacts.width(), 0);
	}

	if (_hider) {
		_hider->resize(size());
	}
}

void TitleWidget::resizeEvent(QResizeEvent *e) {
	updateControlsPosition();
}

void TitleWidget::updateControlsVisibility() {
	auto passcoded = App::passcoded();
	auto authed = (App::main() != nullptr);
	auto selecting = authed && App::main()->selectingPeer();
	auto oneColumnSelecting = (Adaptive::OneColumn() && selecting && !passcoded);

	_cancel.setVisible(oneColumnSelecting);

	updateRestartButtonVisibility();
	updateMenuButtonsVisibility();
	updateSystemButtonsVisibility();

	updateControlsPosition();
	update();
}

void TitleWidget::updateRestartButtonVisibility() {
#ifndef TDESKTOP_DISABLE_AUTOUPDATE
	bool updateReady = (Sandbox::updatingState() == Application::UpdatingReady);
#else
	bool updateReady = false;
#endif
	auto scaleRestarting = cEvalScale(cConfigScale()) != cEvalScale(cRealScale());

	auto updateVisible = _cancel.isHidden() && (updateReady || scaleRestarting);
	if (updateVisible) {
		_update.setText(lang(updateReady ? lng_menu_update : lng_menu_restart));
		_update.show();
		_a_update.start();
	} else {
		_update.hide();
		_a_update.stop();
	}
}

void TitleWidget::updateMenuButtonsVisibility() {
	if (_cancel.isHidden()) {
		if (App::passcoded()) {
			_settings.hide();
			_contacts.hide();
			_about.hide();
			_lock.setSysBtnStyle(st::sysUnlock);
		} else {
			_lock.setSysBtnStyle(st::sysLock);
			_settings.show();
			_contacts.setVisible(App::main() != nullptr);
			_about.show();
		}
	} else {
		_settings.hide();
		_contacts.hide();
		_about.hide();
	}
}

void TitleWidget::updateSystemButtonsVisibility() {
	if (_cancel.isHidden()) {
		_lock.setVisible(Global::LocalPasscode());
		if (_player) {
			_player->show();
		}
	} else {
		_lock.hide();
		if (_player) {
			_player->hide();
		}
	}
	if (_update.isHidden() && _cancel.isHidden() && cPlatform() == dbipWindows) {
		_minimize.show();
		maximizedChanged(lastMaximized, true);
		_close.show();
	} else {
		_minimize.hide();
		_restore.hide();
		_maximize.hide();
		_close.hide();
	}
}

void TitleWidget::updateAdaptiveLayout() {
	updateControlsVisibility();
	if (Adaptive::OneColumn()) {
		updateCounter();
	}
	if (_hider) {
		_hider->setVisible(!Adaptive::OneColumn());
	}
}

void TitleWidget::updateCounter() {
	if (!Adaptive::OneColumn() || !MTP::authedId()) return;

	int32 counter = App::histories().unreadBadge();
	bool muted = App::histories().unreadOnlyMuted();

	style::color bg = muted ? st::counterMuteBG : st::counterBG;

	if (counter > 0) {
		int32 size = cRetina() ? -32 : -16;
		switch (cScale()) {
		case dbisOneAndQuarter: size = -20; break;
		case dbisOneAndHalf: size = -24; break;
		case dbisTwo: size = -32; break;
		}
		_counter = App::pixmapFromImageInPlace(App::wnd()->iconWithCounter(size, counter, bg, false));
		_counter.setDevicePixelRatio(cRetinaFactor());
		update(QRect(st::titleIconPos, st::titleIconImg.pxSize()));
	} else {
		if (!_counter.isNull()) {
			_counter = QPixmap();
			update(QRect(st::titleIconPos, st::titleIconImg.pxSize()));
		}
	}
}

void TitleWidget::mousePressEvent(QMouseEvent *e) {
	if (auto wnd = App::wnd()) {
		if (wnd->psHandleTitle()) return;
		if (e->buttons() & Qt::LeftButton) {
			wnd->wStartDrag(e);
			e->accept();
		}
	}
}

void TitleWidget::mouseDoubleClickEvent(QMouseEvent *e) {
	if (auto wnd = App::wnd()) {
		if (wnd->psHandleTitle()) return;
		Qt::WindowStates s(wnd->windowState());
		if (s.testFlag(Qt::WindowMaximized)) {
			wnd->setWindowState(s & ~Qt::WindowMaximized);
		} else {
			wnd->setWindowState(s | Qt::WindowMaximized);
		}
	}
}

void TitleWidget::onWindowStateChanged(Qt::WindowState state) {
	if (state == Qt::WindowMinimized) return;
	maximizedChanged(state == Qt::WindowMaximized);
}

void TitleWidget::maximizedChanged(bool maximized, bool force) {
	if (lastMaximized == maximized && !force) return;

	lastMaximized = maximized;

    if (cPlatform() != dbipWindows || !_update.isHidden()) return;
	if (maximized) {
		_maximize.clearState();
	} else {
		_restore.clearState();
	}

	_maximize.setVisible(!maximized);
	_restore.setVisible(maximized);

	updateControlsPosition();
}

HitTestType TitleWidget::hitTest(const QPoint &p) {
	if (App::wnd() && Ui::isLayerShown()) return HitTestNone;

	int x(p.x()), y(p.y()), w(width()), h(height());
	if (!Adaptive::OneColumn() && _hider && x >= App::main()->dlgsWidth()) return HitTestNone;

	if (x >= st::titleIconPos.x() && y >= st::titleIconPos.y() && x < st::titleIconPos.x() + st::titleIconImg.pxWidth() && y < st::titleIconPos.y() + st::titleIconImg.pxHeight()) {
		return HitTestIcon;
	} else if (false
		|| (_player && _player->geometry().contains(p))
		|| (_lock.hitTest(p - _lock.geometry().topLeft()) == HitTestSysButton && _lock.isVisible())
        || (_update.hitTest(p - _update.geometry().topLeft()) == HitTestSysButton && _update.isVisible())
		|| (_minimize.hitTest(p - _minimize.geometry().topLeft()) == HitTestSysButton)
		|| (_maximize.hitTest(p - _maximize.geometry().topLeft()) == HitTestSysButton)
		|| (_restore.hitTest(p - _restore.geometry().topLeft()) == HitTestSysButton)
		|| (_close.hitTest(p - _close.geometry().topLeft()) == HitTestSysButton)
	) {
		return HitTestSysButton;
	} else if (x >= 0 && x < w && y >= 0 && y < h) {
		if (false
			|| (!_cancel.isHidden() && _cancel.geometry().contains(x, y))
			|| (!_settings.isHidden() && _settings.geometry().contains(x, y))
			|| (!_contacts.isHidden() && _contacts.geometry().contains(x, y))
			|| (!_about.isHidden() && _about.geometry().contains(x, y))
		) {
			return HitTestClient;
		}
		return HitTestCaption;
	}
	return HitTestNone;
}
