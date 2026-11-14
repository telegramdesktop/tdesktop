/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/player/media_player_widget.h"
#include "mokagram/music/mokagram_soundcloud.h"
#include "mokagram/music/mokagram_music_ui.h"
#include "mokagram/settings/mokagram_settings.h"
#include "mokagram/lang/mokagram_lang.h"
#include "data/data_document.h"
#include "ui/painter.h"
#include "styles/style_menu_icons.h"

#include "platform/platform_specific.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_file_origin.h"
#include "data/data_session.h"
#include "data/data_peer.h"
#include "data/data_user.h"
#include "data/data_changes.h"
#include "ui/userpic_view.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/continuous_sliders.h"
#include "ui/widgets/shadow.h"
#include "ui/abstract_button.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/dropdown_menu.h"
#include "ui/widgets/menu/menu_action.h"
#include "ui/wrap/fade_wrap.h"
#include "ui/effects/ripple_animation.h"
#include "ui/text/format_values.h"
#include "ui/text/format_song_document_name.h"
#include "lang/lang_keys.h"
#include "media/audio/media_audio.h"
#include "media/view/media_view_playback_progress.h"
#include "media/player/media_player_button.h"
#include "media/player/media_player_instance.h"
#include "media/player/media_player_dropdown.h"
#include "media/player/media_player_volume_controller.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_helpers.h"
#include "storage/storage_account.h"
#include "main/main_session.h"
#include "window/window_session_controller.h"
#include "styles/style_media_player.h"
#include "styles/style_mokagram_motion.h"
#include "ui/effects/animations.h"
#include "ui/power_saving.h"
#include <QtGui/QGuiApplication>
#include <QtGui/QPainterPath>
#include <cmath>

namespace Media {
namespace Player {
namespace {

// Высота полосы с волной и её половина (середина волны лежит на границе плашки и шапки).
[[nodiscard]] int WaveHeight() {
	return st::mediaPlayerPlayback.fullWidth + style::ConvertScale(2);
}
[[nodiscard]] int WaveHalfHeight() {
	return WaveHeight() / 2;
}

// Mokagram: круглый виниловый диск, обложка внутри вращается.
void PaintDisc(
		QPainter &p,
		const QRectF &box,
		const QPixmap &label,
		double angle) {
	const auto radius = std::min(box.width(), box.height()) / 2.;
	const auto center = box.center();
	auto hq = PainterHighQualityEnabler(p);
	p.setPen(Qt::NoPen);
	auto base = QRadialGradient(center, radius);
	base.setColorAt(0., QColor(0x1c, 0x1c, 0x1e));
	base.setColorAt(0.6, QColor(0x10, 0x10, 0x11));
	base.setColorAt(1., QColor(0x07, 0x07, 0x08));
	p.setBrush(base);
	p.drawEllipse(center, radius, radius);
	p.setBrush(Qt::NoBrush);
	p.setPen(QPen(QColor(255, 255, 255, 14), 1.));
	for (auto r = radius * 0.7; r < radius * 0.97; r += radius * 0.09) {
		p.drawEllipse(center, r, r);
	}
	auto sheen = QConicalGradient(center, 35.);
	sheen.setColorAt(0., QColor(255, 255, 255, 0));
	sheen.setColorAt(0.07, QColor(255, 255, 255, 34));
	sheen.setColorAt(0.14, QColor(255, 255, 255, 0));
	sheen.setColorAt(0.5, QColor(255, 255, 255, 0));
	sheen.setColorAt(0.57, QColor(255, 255, 255, 26));
	sheen.setColorAt(0.64, QColor(255, 255, 255, 0));
	sheen.setColorAt(1., QColor(255, 255, 255, 0));
	p.setPen(Qt::NoPen);
	p.setBrush(sheen);
	p.drawEllipse(center, radius * 0.98, radius * 0.98);
	const auto inner = radius * 0.64;
	p.save();
	p.translate(center);
	p.rotate(angle);
	auto clip = QPainterPath();
	clip.addEllipse(QPointF(), inner, inner);
	p.setClipPath(clip);
	const auto field = QRectF(-inner, -inner, inner * 2, inner * 2);
	if (label.isNull()) {
		p.fillRect(field, QColor(0x23, 0x23, 0x25));
	} else {
		const auto scaled = QSizeF(label.size()).scaled(field.size(), Qt::KeepAspectRatioByExpanding);
		p.drawPixmap(
			QRectF(-scaled.width() / 2, -scaled.height() / 2, scaled.width(), scaled.height()),
			label,
			QRectF(QPointF(), QSizeF(label.size())));
	}
	p.restore();
	p.setBrush(Qt::NoBrush);
	p.setPen(QPen(QColor(0, 0, 0, 120), 1.));
	p.drawEllipse(center, inner, inner);
	p.setPen(Qt::NoPen);
	p.setBrush(QColor(0x0b, 0x0b, 0x0c));
	p.drawEllipse(center, radius * 0.09, radius * 0.09);
}

// Mokagram: верхняя полоса плеера, волнистая как в полноэкранном плеере.
class WavySlider final : public Ui::FilledSlider {
public:
	WavySlider(QWidget *parent, const style::FilledSlider &st)
	: FilledSlider(parent, st)
	, _st(st)
	, _waveAnimation([=] {
		if ((!_playing && !_strength.animating())
			|| !isVisible()
			|| PowerSaving::On(PowerSaving::kAnimations)) {
			return false;
		}
		update();
		return true;
	}) {
	}

	void setPlaying(bool playing) {
		if (_playing == playing) {
			return;
		}
		_playing = playing;
		_strength.start([=] { update(); }, playing ? 0. : 1., playing ? 1. : 0., 450, anim::easeOutCubic);
		if (!_waveAnimation.animating()) {
			_waveAnimation.start();
		}
		update();
	}

protected:
	void paintEvent(QPaintEvent *e) override {
		auto p = QPainter(this);
		PainterHighQualityEnabler hq(p);
		const auto disabled = isDisabled();
		const auto over = getCurrentOverFactor();
		const auto seekRect = getSeekRect();
		const auto from = double(seekRect.x());
		const auto end = from + seekRect.width();
		const auto mid = from + getCurrentValue() * seekRect.width();
		const auto strength = (disabled || isChanging())
			? 0.
			: _strength.value(_playing ? 1. : 0.);
		const auto animate = strength > 0.
			&& !PowerSaving::On(PowerSaving::kAnimations);
		if (animate && !_waveAnimation.animating()) {
			_waveAnimation.start();
		}
		const auto stroke = style::ConvertScaleExact(2.);
		// Мелкая волна: ниже и короче, чем в полноэкранном плеере.
		const auto length = style::ConvertScaleExact(14.);
		const auto amplitude = animate
			? strength * std::min(style::ConvertScaleExact(1.5), (height() - stroke) / 2.)
			: 0.;
		// Волна по середине своей полосы: середина лежит на границе плашки и шапки чата.
		const auto y = height() / 2.;
		auto active = QColor(Qt::white);
		if (disabled) {
			active.setAlphaF(0.4);
		}
		p.setOpacity(fadeOpacity());
		p.setBrush(Qt::NoBrush);
		if (end > mid && over > 0.) {
			auto inactive = _st.inactiveFg->c;
			inactive.setAlphaF(inactive.alphaF() * over);
			p.setPen(QPen(inactive, stroke, Qt::SolidLine, Qt::RoundCap));
			p.drawLine(QPointF(mid, y), QPointF(end, y));
		}
		if (mid <= from) {
			return;
		}
		const auto phase = animate ? crl::now() / 500. : 0.;
		auto path = QPainterPath();
		for (auto x = from; x <= mid; x += std::max(st::motionWaveSample, 1)) {
			const auto envelope = std::clamp((mid - x) / length, 0., 1.);
			const auto point = QPointF(x, y + amplitude * envelope
				* std::sin(x * 2. * M_PI / length - phase));
			if (x == from) {
				path.moveTo(point);
			} else {
				path.lineTo(point);
			}
		}
		p.setPen(QPen(active, stroke, Qt::SolidLine, Qt::RoundCap));
		p.drawPath(path);
		p.setPen(Qt::NoPen);
		p.setBrush(active);
		const auto handle = style::ConvertScaleExact(3.);
		const auto handleHeight = stroke * 2.;
		p.drawRoundedRect(
			QRectF(mid - handle / 2., y - handleHeight / 2., handle, handleHeight),
			handle / 2.,
			handle / 2.);
	}

private:
	const style::FilledSlider &_st;
	Ui::Animations::Basic _waveAnimation;
	Ui::Animations::Simple _strength;
	bool _playing = false;

};

Ui::Text::FormatSongName::ComposedName SongNameForPlayback(
		const AudioMsgId &id) {
	auto result = Ui::Text::FormatSongName::ComposedName();
	if (Mokagram::Music::OwnsTrack(id)
		&& Mokagram::Music::CurrentTrackInfo(result.title, result.performer)) {
		return result;
	}
	const auto document = id.audio();
	return document
		? Ui::Text::FormatSongNameFor(document).composedName()
		: result;
}

} // namespace

Widget::Widget(
	QWidget *parent,
	not_null<Ui::RpWidget*> dropdownsParent,
	not_null<Window::SessionController*> controller)
: RpWidget(parent)
, _controller(controller)
, _orderMenuParent(dropdownsParent)
, _nameLabel(this, st::mediaPlayerName)
, _rightControls(this, object_ptr<Ui::RpWidget>(this))
, _timeLabel(rightControls(), st::mediaPlayerTime)
, _playPause(this, st::mediaPlayerPlayButton)
, _volumeToggle(rightControls(), st::mediaPlayerVolumeToggle)
, _repeatToggle(rightControls(), st::mediaPlayerRepeatButton)
, _orderToggle(rightControls(), st::mediaPlayerOrderButton)
, _lyricsToggle(rightControls())
, _fullscreenToggle(rightControls())
, _speedToggle(rightControls(), st::mediaPlayerSpeedButton)
, _close(this, st::mediaPlayerClose)
, _shadow(this)
, _playbackSlider(object_ptr<WavySlider>(this, st::mediaPlayerPlayback))
, _volume(std::in_place, dropdownsParent.get())
, _playbackProgress(std::make_unique<View::PlaybackProgress>())
, _orderController(
	std::make_unique<OrderController>(
		_orderToggle.data(),
		dropdownsParent,
		[=](bool over) { markOver(over); },
		Core::App().settings().playerOrderModeValue(),
		[=](OrderMode value) { saveOrder(value); }))
, _speedController(
	std::make_unique<SpeedController>(
		_speedToggle.data(),
		_speedToggle->st(),
		dropdownsParent,
		[=](bool over) { markOver(over); },
		[=](bool lastNonDefault) { return speedLookup(lastNonDefault); },
		[=](float64 speed) { saveSpeed(speed); })) {
	_speedController->setApplySpeed([=](float64 speed) {
		instance()->setPlaybackSpeed(speed);
	});
	_speedController->realtimeValue(
	) | rpl::on_next([=](float64 speed) {
		_speedToggle->setSpeed(speed);
		_speedToggle->setAccessibleName(tr::lng_mediaview_playback_speed(
			tr::now,
			lt_speed,
			QString::number(base::SafeRound(speed * 10) / 10.) + "x"));
	}, _speedToggle->lifetime());
	_speedToggle->finishAnimating();

	setAttribute(Qt::WA_OpaquePaintEvent);
	setMouseTracking(true);
	resize(width(), st::mediaPlayerHeight + st::lineWidth);
	Mokagram::Music::CoversChanged(
	) | rpl::on_next([=] {
		update();
	}, lifetime());
	Mokagram::Music::QueueChanges(
	) | rpl::on_next([=] {
		handlePlaylistUpdate();
	}, lifetime());
	events(
	) | rpl::filter([](not_null<QEvent*> e) {
		return e->type() == QEvent::ContextMenu;
	}) | rpl::on_next([=](not_null<QEvent*> e) {
		if (Mokagram::Music::OwnsTrack(_lastSongId)) {
			Mokagram::Music::ShowCurrentTrackMenu(
				_controller,
				static_cast<QContextMenuEvent*>(e.get())->globalPos());
		}
	}, lifetime());

	setupRightControls();

	_volumeToggle->setAccessibleName(tr::lng_ringtones_box_volume(tr::now));
	_repeatToggle->setAccessibleName(tr::lng_schedule_repeat_label(tr::now));
	_orderToggle->setAccessibleName(tr::lng_sr_playback_order(tr::now));
	_close->setAccessibleName(tr::lng_sr_player_close(tr::now));

	_nameLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
	_timeLabel->setAttribute(Qt::WA_TransparentForMouseEvents);

	_playbackProgress->setInLoadingStateChangedCallback([=](bool loading) {
		_playbackSlider->setDisabled(loading);
	});
	_playbackProgress->setValueChangedCallback([=](float64 value, float64) {
		_playbackSlider->setValue(value);
	});
	_playbackSlider->setChangeProgressCallback([=](float64 value) {
		if (_type != AudioMsgId::Type::Song) {
			return; // Round video seek is not supported for now :(
		}
		_playbackProgress->setValue(value, false);
		handleSeekProgress(value);
	});
	_playbackSlider->setChangeFinishedCallback([=](float64 value) {
		if (_type != AudioMsgId::Type::Song) {
			return; // Round video seek is not supported for now :(
		}
		_playbackProgress->setValue(value, false);
		handleSeekFinished(value);
	});
	_playPause->setClickedCallback([=] {
		instance()->playPauseCancelClicked(_type);
	});

	// Mokagram: кнопка «Текст песни», открывает панель справа от чата.
	_lyricsToggle->resize(st::mediaPlayerRepeatButton.width, st::mediaPlayerRepeatButton.height);
	_lyricsToggle->setPointerCursor(true);
	_lyricsToggle->setToolTip(QString::fromUtf8(Mokagram::Lang::Tr("Текст песни")));
	_lyricsToggle->paintOn([=](QPainter &p) {
		const auto on = Mokagram::Settings::Bool(u"lyrics.enabled"_q);
		const auto usable = on || Mokagram::Music::LyricsAvailable();
		auto hq = PainterHighQualityEnabler(p);
		auto color = (on ? st::mediaPlayerActiveFg : st::windowSubTextFg)->c;
		color.setAlphaF(!usable
			? 0.22
			: (on || _lyricsToggle->isOver())
			? 1.
			: 0.55);
		auto pen = QPen(color, 1.7);
		pen.setCapStyle(Qt::RoundCap);
		p.setPen(pen);
		const auto x = (_lyricsToggle->width() - 16) / 2.;
		auto y = (_lyricsToggle->height() - 10) / 2. + 3.;
		p.drawLine(QPointF(x, y), QPointF(x + 16, y));
		p.drawLine(QPointF(x, y + 5), QPointF(x + 11, y + 5));
		p.drawLine(QPointF(x, y + 10), QPointF(x + 14, y + 10));
	});
	// Mokagram: кнопка «Во весь экран», открывает полноэкранный плеер.
	_fullscreenToggle->resize(st::mediaPlayerRepeatButton.width, st::mediaPlayerRepeatButton.height);
	_fullscreenToggle->setPointerCursor(true);
	_fullscreenToggle->setToolTip(QString::fromUtf8(Mokagram::Lang::Tr("Во весь экран")));
	_fullscreenToggle->paintOn([=](QPainter &p) {
		auto hq = PainterHighQualityEnabler(p);
		auto color = (_fullscreenToggle->isOver()
			? st::windowFg
			: st::windowSubTextFg)->c;
		auto pen = QPen(color, 1.7);
		pen.setCapStyle(Qt::RoundCap);
		pen.setJoinStyle(Qt::RoundJoin);
		p.setPen(pen);
		p.setBrush(Qt::NoBrush);
		const auto x = (_fullscreenToggle->width() - 12) / 2.;
		const auto y = (_fullscreenToggle->height() - 12) / 2. + 3.;
		const auto corner = [&](double cx, double cy, double dx, double dy) {
			p.drawPolyline(QPolygonF({
				QPointF(cx + dx * 4, cy),
				QPointF(cx, cy),
				QPointF(cx, cy + dy * 4) }));
		};
		corner(x, y, 1, 1);
		corner(x + 12, y, -1, 1);
		corner(x, y + 12, 1, -1);
		corner(x + 12, y + 12, -1, -1);
	});
	_fullscreenToggle->setClickedCallback([=] {
		Mokagram::Music::ShowFullscreenPlayer(_controller);
	});
	const auto updateLyricsToggle = [=] {
		const auto usable = Mokagram::Settings::Bool(u"lyrics.enabled"_q)
			|| Mokagram::Music::LyricsAvailable();
		_lyricsToggle->setDisabled(!usable);
		_lyricsToggle->setPointerCursor(usable);
		_lyricsToggle->update();
	};
	Mokagram::Music::LyricsAvailabilityChanges(
	) | rpl::on_next(updateLyricsToggle, lifetime());
	updateLyricsToggle();
	// Mokagram: без lyrics аватарки комментариев стоят на полосе прогресса.
	Mokagram::Music::AttachCommentsToPlayer(
		_controller, this, _playbackSlider.data(), true,
		[=] { return _type == AudioMsgId::Type::Song; });
	_lyricsToggle->setClickedCallback([=] {
		Mokagram::Music::ToggleLyricsPanel(_controller, this);
		_lyricsToggle->update();
	});
	Mokagram::Settings::Changed(
	) | rpl::filter([](const QString &key) {
		return key == u"lyrics.enabled"_q;
	}) | rpl::on_next([=] {
		updateLyricsToggle();
		Mokagram::Music::SyncLyricsPanel(_controller, this);
	}, lifetime());
	crl::on_main(this, [=] {
		Mokagram::Music::SyncLyricsPanel(_controller, this);
	});

	updateVolumeToggleIcon();
	_volumeToggle->setClickedCallback([=] {
		const auto volume = (Core::App().settings().songVolume() > 0)
			? 0.
			: Core::App().settings().rememberedSongVolume();
		Core::App().settings().setSongVolume(volume);
		Core::App().saveSettingsDelayed();
		mixer()->setSongVolume(volume);
	});
	Core::App().settings().songVolumeChanges(
	) | rpl::on_next([=] {
		updateVolumeToggleIcon();
	}, lifetime());

	Core::App().settings().playerRepeatModeValue(
	) | rpl::on_next([=] {
		updateRepeatToggleIcon();
	}, lifetime());

	_repeatToggle->setClickedCallback([=] {
		auto &settings = Core::App().settings();
		settings.setPlayerRepeatMode([&] {
			switch (settings.playerRepeatMode()) {
			case RepeatMode::None: return RepeatMode::One;
			case RepeatMode::One: return RepeatMode::All;
			case RepeatMode::All: return RepeatMode::None;
			}
			Unexpected("Repeat mode in Settings.");
		}());
		Core::App().saveSettingsDelayed();
	});

	_speedController->saved(
	) | rpl::on_next([=] {
		instance()->updatePlaybackSpeed();
	}, lifetime());

	rpl::merge(
		Core::App().settings().voicePlaybackSpeedChanges() | rpl::to_empty,
		Core::App().settings().audioPlaybackSpeedChanges() | rpl::to_empty
	) | rpl::on_next([=] {
		_speedController->reloadFromLookup();
	}, lifetime());

	instance()->trackChanged(
	) | rpl::filter([=](AudioMsgId::Type type) {
		return (type == _type);
	}) | rpl::on_next([=](AudioMsgId::Type type) {
		handleSongChange();
		updateControlsVisibility();
		updateLabelsGeometry();
	}, lifetime());

	rpl::merge(
		instance()->tracksFinished(
		) | rpl::filter([=](AudioMsgId::Type type) {
			return (type == AudioMsgId::Type::Voice);
		}) | rpl::to_empty,
		instance()->stops(AudioMsgId::Type::Voice)
	) | rpl::on_next([=] {
		_voiceIsActive = false;
		const auto currentSong = instance()->current(AudioMsgId::Type::Song);
		const auto songState = instance()->getState(AudioMsgId::Type::Song);
		if (currentSong == songState.id && !IsStoppedOrStopping(songState.state)) {
			setType(AudioMsgId::Type::Song);
		}
	}, lifetime());

	instance()->updatedNotifier(
	) | rpl::on_next([=](const TrackState &state) {
		handleSongUpdate(state);
	}, lifetime());

	PrepareVolumeDropdown(_volume.get(), controller, _volumeToggle->events(
	) | rpl::filter([=](not_null<QEvent*> e) {
		return (e->type() == QEvent::Wheel);
	}) | rpl::map([=](not_null<QEvent*> e) {
		return not_null{ static_cast<QWheelEvent*>(e.get()) };
	}));
	_volumeToggle->installEventFilter(_volume.get());
	_volume->events(
	) | rpl::on_next([=](not_null<QEvent*> e) {
		if (e->type() == QEvent::Enter) {
			markOver(true);
		} else if (e->type() == QEvent::Leave) {
			markOver(false);
		}
	}, _volume->lifetime());

	hidePlaylistOn(_playPause);
	hidePlaylistOn(_close);
	hidePlaylistOn(_rightControls);

	hideDropdownsOn(_volumeToggle);
	hideDropdownsOn(_orderToggle);
	hideDropdownsOn(_speedToggle);

	const auto otherDropdownCheck = [=](QPoint globalPosition) {
		return overDropdownControl(globalPosition);
	};
	_orderController->setOtherDropdownCheck(otherDropdownCheck);
	_speedController->setOtherDropdownCheck(otherDropdownCheck);

	setType(AudioMsgId::Type::Song);
	_playPause->finishTransform();
}

void Widget::hidePlaylistOn(not_null<Ui::RpWidget*> widget) {
	widget->events(
	) | rpl::filter([=](not_null<QEvent*> e) {
		return (e->type() == QEvent::Enter);
	}) | rpl::on_next([=] {
		updateOverLabelsState(false);
	}, widget->lifetime());
}

void Widget::hideDropdownsOn(not_null<Ui::RpWidget*> widget) {
	widget->events(
	) | rpl::filter([=](not_null<QEvent*> e) {
		return (e->type() == QEvent::Enter);
	}) | rpl::on_next([=] {
		hideDropdowns(widget);
	}, widget->lifetime());
}

bool Widget::overDropdownControl(QPoint globalPosition) const {
	const auto over = [&](not_null<Ui::RpWidget*> control) {
		return !control->isHidden()
			&& control->rect().contains(
				control->mapFromGlobal(globalPosition));
	};
	return over(_volumeToggle)
		|| over(_orderToggle)
		|| over(_speedToggle);
}

void Widget::hideDropdowns(not_null<Ui::RpWidget*> except) {
	if (except.get() != _volumeToggle.data()) {
		_volume->hideFast();
	}
	if (except.get() != _orderToggle.data()) {
		if (const auto menu = _orderController->menu()) {
			menu->hideFast();
		}
	}
	if (except.get() != _speedToggle.data()) {
		if (const auto menu = _speedController->menu()) {
			menu->hideFast();
		}
	}
}

void Widget::setupRightControls() {
	_rightControls->show(anim::type::instant);
}

void Widget::updateVolumeToggleIcon() {
	_volumeToggle->setIconOverride([] {
		const auto volume = Core::App().settings().songVolume();
		return (volume == 0.)
			? &st::mediaPlayerVolumeIcon0
			: (volume < 0.66)
			? &st::mediaPlayerVolumeIcon1
			: nullptr;
	}());
}

void Widget::setCloseCallback(Fn<void()> callback) {
	_closeCallback = std::move(callback);
	_close->setClickedCallback([this] { stopAndClose(); });
}

void Widget::setShowItemCallback(
		Fn<void(not_null<const HistoryItem*>)> callback) {
	_showItemCallback = std::move(callback);
}

void Widget::stopAndClose() {
	_voiceIsActive = false;
	if (_type == AudioMsgId::Type::Voice) {
		const auto songData = instance()->current(AudioMsgId::Type::Song);
		const auto songState = instance()->getState(AudioMsgId::Type::Song);
		if (songData == songState.id && !IsStoppedOrStopping(songState.state)) {
			instance()->stop(AudioMsgId::Type::Voice);
			return;
		}
	}
	if (_closeCallback) {
		_closeCallback();
	}
}

void Widget::setShadowGeometryToLeft(int x, int y, int w, int h) {
	_shadow->setGeometryToLeft(x, y, w, h);
}

void Widget::showShadowAndDropdowns() {
	_shadow->show();
	_playbackSlider->setVisible(_type == AudioMsgId::Type::Song);
	if (_volumeHidden) {
		_volumeHidden = false;
		_volume->show();
	}
	_speedController->showBack();
	_orderController->showBack();
}

void Widget::updateDropdownsGeometry() {
	const auto dropdownWidth = st::mediaPlayerVolumeSize.width();
	const auto position = _volume->parentWidget()->mapFromGlobal(
		_volumeToggle->mapToGlobal(
			QPoint(
				(_volumeToggle->width() - dropdownWidth) / 2,
				_volumeToggle->height())));
	const auto playerMargins = _volume->getMargin();
	const auto shift = QPoint(playerMargins.left(), playerMargins.top());
	_volume->move(position - shift + QPoint(0, 4));

	_orderController->updateDropdownGeometry();
	_speedController->updateDropdownGeometry();
}

void Widget::hideShadowAndDropdowns() {
	_shadow->hide();
	_playbackSlider->hide();
	if (!_volume->isHidden()) {
		_volumeHidden = true;
		_volume->hide();
	}
	_speedController->hideTemporarily();
	_orderController->hideTemporarily();
}

void Widget::raiseDropdowns() {
	_volume->raise();
}

Widget::~Widget() = default;

not_null<Ui::RpWidget*> Widget::rightControls() {
	return _rightControls->entity();
}

void Widget::handleSeekProgress(float64 progress) {
	if (!_lastDurationMs) return;

	const auto positionMs = std::clamp(
		static_cast<crl::time>(progress * _lastDurationMs),
		crl::time(0),
		_lastDurationMs);
	if (_seekPositionMs != positionMs) {
		_seekPositionMs = positionMs;
		updateTimeLabel();

		instance()->startSeeking(_type);
	}
}

void Widget::handleSeekFinished(float64 progress) {
	if (!_lastDurationMs) return;

	_seekPositionMs = -1;

	instance()->finishSeeking(_type, progress);
}

void Widget::resizeEvent(QResizeEvent *e) {
	updateControlsGeometry();
	_narrow = (width() < st::mediaPlayerWideWidth);
	updateControlsWrapVisibility();
}

void Widget::updateControlsGeometry() {
	_close->moveToRight(st::mediaPlayerCloseRight, st::mediaPlayerPlayTop);
	auto right = 0;
	if (hasPlaybackSpeedControl()) {
		_speedToggle->moveToRight(right, 0); right += _speedToggle->width();
	}
	if (_type == AudioMsgId::Type::Song) {
		_repeatToggle->moveToRight(right, 0); right += _repeatToggle->width();
		_orderToggle->moveToRight(right, 0); right += _orderToggle->width();
	}
	_volumeToggle->moveToRight(right, 0); right += _volumeToggle->width();
	if (_type == AudioMsgId::Type::Song) {
		_lyricsToggle->moveToRight(right, 0); right += _lyricsToggle->width();
		_fullscreenToggle->moveToRight(right, 0); right += _fullscreenToggle->width();
	}

	updateControlsWrapGeometry();

	updatePlayPrevNextPositions();

	// Волнистой линии нужна высота под амплитуду, но не настолько, чтобы заходить на обложку.
	const auto sliderHeight = WaveHeight();
	_playbackSlider->setGeometry(
		0,
		height() - sliderHeight,
		width(),
		sliderHeight);

	updateDropdownsGeometry();
}

void Widget::updateControlsWrapGeometry() {
	const auto fade = st::mediaPlayerControlsFade.width();
	const auto controls = getTimeRight() + _timeLabel->width() + fade;
	rightControls()->resize(controls, _repeatToggle->height());
	_rightControls->move(
		width() - st::mediaPlayerCloseRight - _close->width() - controls,
		st::mediaPlayerPlayTop);
}

void Widget::updateControlsWrapVisibility() {
	_rightControls->toggle(
		_over || !_narrow,
		isHidden() ? anim::type::instant : anim::type::normal);
}

void Widget::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	auto fill = e->rect().intersected(
		QRect(0, 0, width(), st::mediaPlayerHeight + st::lineWidth));
	if (!fill.isEmpty()) {
		p.fillRect(fill, st::mediaPlayerBg);
	}
	if (_type == AudioMsgId::Type::Song) {
		// Нижняя половина полосы волны закрашена цветом шапки под плашкой:
		// граница цветов проходит ровно по середине волны.
		p.fillRect(
			QRect(0, height() - WaveHalfHeight(), width(), WaveHalfHeight()),
			st::topBarBg);
	}
	if (_type == AudioMsgId::Type::Song) {
		auto tint = Mokagram::Music::CurrentCoverAccent();
		if (tint.isValid()) {
			tint.setAlpha(6);
			p.fillRect(fill, tint);
		}
	}
	// Mokagram: обложка слева.
	if (_type == AudioMsgId::Type::Song) {
		const auto size = 52;
		const auto art = QRect(
			14,
			(st::mediaPlayerHeight - size) / 2,
			size,
			size);
		auto hq = PainterHighQualityEnabler(p);
		auto path = QPainterPath();
		path.addRoundedRect(art, 12, 12);
		const auto current = instance()->current(AudioMsgId::Type::Song);
		const auto cover = Mokagram::Music::CoverForPlayback(current);
		const auto vinyl = Mokagram::Settings::Bool(u"player.vinyl"_q);
		if (vinyl) {
			const auto now = crl::now();
			const auto speed = PowerSaving::On(PowerSaving::kAnimations)
				? 0.
				: _spin.value(_discPlaying ? 1. : 0.);
			if (_discTime) {
				_discAngle = std::fmod(
					_discAngle + 80. * speed * std::min<crl::time>(now - _discTime, 100) / 1000.,
					360.);
			}
			_discTime = now;
			if (!cover.isNull() && _discLabelKey != cover.cacheKey()) {
				_discLabelKey = cover.cacheKey();
				_discLabel = QPixmap::fromImage(cover.scaled(
					QSize(96, 96),
					Qt::KeepAspectRatioByExpanding,
					Qt::SmoothTransformation));
			}
			PaintDisc(
				p,
				QRectF(art),
				cover.isNull() ? QPixmap() : _discLabel,
				_discAngle);
		}
		if (vinyl) {
		} else if (!cover.isNull()) {
			p.save();
			p.setClipPath(path);
			p.drawImage(art, cover);
			p.restore();
		} else {
			p.setPen(Qt::NoPen);
			p.setBrush(st::windowBgOver);
			p.drawPath(path);
			st::menuIconSoundOn.paintInCenter(p, art);
		}
		const auto name = SongNameForPlayback(current);
		if (!name.performer.isEmpty()) {
			p.setFont(st::normalFont);
			p.setPen(st::windowSubTextFg);
			const auto left = getNameLeft();
			const auto room = width() - left - getNameRight();
			p.drawText(
				QRect(left, st::mediaPlayerNameTop - 4, std::max(room, 40), 20),
				Qt::AlignLeft | Qt::AlignVCenter,
				st::normalFont->elided(name.performer, std::max(room, 40)));
		}
	} else if (const auto peer = voiceSender()) {
		// Mokagram: аватарка отправителя вместо пустоты слева у голосовых.
		const auto size = 52;
		peer->paintUserpicLeft(
			p,
			_voiceUserpic,
			14,
			(st::mediaPlayerHeight - size) / 2,
			width(),
			size);
	}
}

PeerData *Widget::voiceSender() const {
	const auto document = _lastSongId.audio();
	if (!document) {
		return nullptr;
	}
	const auto item = document->owner().message(_lastSongId.contextId());
	if (!item) {
		return nullptr;
	}
	return (!item->out() || item->isPost())
		? item->fromOriginal().get()
		: static_cast<PeerData*>(item->history()->session().user().get());
}

void Widget::enterEventHook(QEnterEvent *e) {
	markOver(true);
}

void Widget::leaveEventHook(QEvent *e) {
	markOver(false);
}

void Widget::markOver(bool over) {
	if (over) {
		_over = true;
		_wontBeOver = false;
		InvokeQueued(this, [=] {
			updateControlsWrapVisibility();
		});
	} else {
		_wontBeOver = true;
		InvokeQueued(this, [=] {
			if (!_wontBeOver) {
				return;
			}
			_wontBeOver = false;
			_over = false;
			updateControlsWrapVisibility();
		});
		updateOverLabelsState(false);
	}
}

void Widget::saveOrder(OrderMode mode) {
	Core::App().settings().setPlayerOrderMode(mode);
	Core::App().saveSettingsDelayed();
}

float64 Widget::speedLookup(bool lastNonDefault) const {
	const auto &settings = Core::App().settings();
	return (_type == AudioMsgId::Type::Song)
		? settings.audioPlaybackSpeed(lastNonDefault)
		: settings.voicePlaybackSpeed(lastNonDefault);
}

void Widget::saveSpeed(float64 speed) {
	auto &settings = Core::App().settings();
	if (_type == AudioMsgId::Type::Song) {
		settings.setAudioPlaybackSpeed(speed);
	} else {
		settings.setVoicePlaybackSpeed(speed);
	}
	Core::App().saveSettingsDelayed();
}

void Widget::mouseMoveEvent(QMouseEvent *e) {
	updateOverLabelsState(e->pos());
}

void Widget::mousePressEvent(QMouseEvent *e) {
	_labelsDown = _labelsOver;
}

void Widget::mouseReleaseEvent(QMouseEvent *e) {
	// Mokagram: клик по обложке открывает очередь и управление, по названию
	// музыку с последним запросом.
	if (_type == AudioMsgId::Type::Song) {
		const auto cover = QRect(14, (st::mediaPlayerHeight - 52) / 2, 52, 52);
		if (cover.contains(e->pos())) {
			Mokagram::Music::ShowPlayerBox(_controller);
			return;
		}
		const auto left = getNameLeft();
		const auto labels = QRect(
			left,
			0,
			width() - left - _rightControls->width() - _close->width(),
			height() - st::mediaPlayerPlayback.fullWidth);
		if (labels.contains(e->pos())) {
			Mokagram::Music::ShowMusicBox(_controller);
			return;
		}
	}
	if (auto downLabels = base::take(_labelsDown)) {
		if (_labelsOver != downLabels) {
			return;
		}
		if ((_type == AudioMsgId::Type::Voice)
				|| _lastSongFromAnotherSession) {
			const auto current = instance()->current(_type);
			const auto document = current.audio();
			const auto context = current.contextId();
			if (document && context && _showItemCallback) {
				if (const auto item = document->owner().message(context)) {
					_showItemCallback(item);
				}
			}
		}
	}
}

void Widget::updateOverLabelsState(QPoint pos) {
	const auto left = getNameLeft();
	const auto right = width()
		- _rightControls->x()
		- _rightControls->width()
		+ getTimeRight();
	const auto labels = myrtlrect(left, 0, width() - right - left, height() - st::mediaPlayerPlayback.fullWidth);
	const auto over = labels.contains(pos);
	updateOverLabelsState(over);
}

void Widget::updateOverLabelsState(bool over) {
	_labelsOver = over;
	const auto pressShowsItem = _labelsOver
		&& ((_type == AudioMsgId::Type::Voice)
			|| _lastSongFromAnotherSession);
	setCursor(pressShowsItem ? style::cur_pointer : style::cur_default);
	_togglePlaylistRequests.fire(over && (_type == AudioMsgId::Type::Song));
}

int Widget::songTextWidth() const {
	const auto fade = st::mediaPlayerControlsFade.width();
	const auto controls = getTimeRight() + _timeLabel->width() + fade;
	const auto available = width()
		- getNameLeft()
		- st::mediaPlayerCloseRight
		- _close->width()
		- controls
		- buttonsBlockWidth()
		- 12;
	auto natural = _nameLabel->naturalWidth();
	const auto name = SongNameForPlayback(_lastSongId);
	natural = std::max(natural, st::normalFont->width(name.performer));
	const auto beforeButtons = songButtonsLeft() - 12 - getNameLeft();
	return std::clamp(
		natural,
		40,
		std::max({ std::min(available, beforeButtons), 40 }));
}

int Widget::songButtonsLeft() const {
	// По центру между названием и таймером.
	const auto fade = st::mediaPlayerControlsFade.width();
	const auto controls = getTimeRight() + _timeLabel->width() + fade;
	// Не зависит от ширины названия, чтобы кнопки не прыгали между треками.
	const auto textStart = getNameLeft();
	const auto controlsLeft = _rightControls->x()
		+ _timeLabel->x()
		- 12;
	const auto block = buttonsBlockWidth() - st::mediaPlayerPadding;
	return textStart + 40 + std::max(
		12,
		(controlsLeft - textStart - 40 - block) / 2);
}

int Widget::buttonsBlockWidth() const {
	if (_type != AudioMsgId::Type::Song) {
		return 0;
	}
	auto result = _playPause->width();
	if (_previousTrack) {
		result += _previousTrack->width() + st::mediaPlayerPlaySkip
			+ _nextTrack->width() + st::mediaPlayerPlaySkip;
	}
	return result + st::mediaPlayerPadding;
}

void Widget::updatePlayPrevNextPositions() {
	movePlayPrevNext();
	updateLabelsGeometry();
}

void Widget::movePlayPrevNext() {
	const auto song = (_type == AudioMsgId::Type::Song);
	// Mokagram: у песен кнопки справа, название рядом с обложкой.
	auto left = song
		? songButtonsLeft()
		: st::mediaPlayerPlayLeft;
	auto top = (st::mediaPlayerHeight - _playPause->height()) / 2;
	if (_previousTrack) {
		_previousTrack->moveToLeft(left, top); left += _previousTrack->width() + st::mediaPlayerPlaySkip;
		_playPause->moveToLeft(left, top); left += _playPause->width() + st::mediaPlayerPlaySkip;
		_nextTrack->moveToLeft(left, top);
	} else {
		_playPause->moveToLeft(left, top);
	}
}

int Widget::getNameLeft() const {
	if (_type == AudioMsgId::Type::Song) {
		return 14 + 52 + 14;
	}
	auto result = st::mediaPlayerPlayLeft + _playPause->width();
	if (_previousTrack) {
		result += _previousTrack->width() + st::mediaPlayerPlaySkip + _nextTrack->width() + st::mediaPlayerPlaySkip;
	}
	result += st::mediaPlayerPadding;
	return result;
}

int Widget::getNameRight() const {
	if (_type == AudioMsgId::Type::Song) {
		return width() - getNameLeft() - songTextWidth();
	}
	return st::mediaPlayerCloseRight
		+ _close->width()
		+ st::mediaPlayerPadding;
}

int Widget::getTimeRight() const {
	auto result = 0;
	result += _volumeToggle->width();
	if (_type == AudioMsgId::Type::Song) {
		result += _lyricsToggle->width()
			+ _fullscreenToggle->width()
			+ _repeatToggle->width()
			+ _orderToggle->width();
	}
	if (hasPlaybackSpeedControl()) {
		result += _speedToggle->width();
	}
	result += st::mediaPlayerPadding;
	return result;
}

void Widget::updateLabelsGeometry() {
	const auto left = getNameLeft();
	const auto widthForName = width()
		- left
		- getNameRight();
	_nameLabel->resizeToNaturalWidth(widthForName);
	const auto song = (_type == AudioMsgId::Type::Song);
	_nameLabel->moveToLeft(
		left,
		st::mediaPlayerNameTop
			- (song ? 6 : 0)
			- st::mediaPlayerName.style.font->ascent);

	const auto right = getTimeRight();
	_timeLabel->moveToRight(right, st::mediaPlayerNameTop - st::mediaPlayerPlayTop - st::mediaPlayerTime.font->ascent);

	updateControlsWrapGeometry();
	if (_type == AudioMsgId::Type::Song) {
		movePlayPrevNext();
	}
}

void Widget::updateRepeatToggleIcon() {
	switch (Core::App().settings().playerRepeatMode()) {
	case RepeatMode::None:
		_repeatToggle->setIconOverride(
			&st::mediaPlayerRepeatDisabledIcon,
			&st::mediaPlayerRepeatDisabledIconOver);
		_repeatToggle->setRippleColorOverride(
			&st::mediaPlayerRepeatDisabledRippleBg);
		break;
	case RepeatMode::One:
		_repeatToggle->setIconOverride(&st::mediaPlayerRepeatOneIcon);
		_repeatToggle->setRippleColorOverride(nullptr);
		break;
	case RepeatMode::All:
		_repeatToggle->setIconOverride(nullptr);
		_repeatToggle->setRippleColorOverride(nullptr);
		break;
	}
}

void Widget::checkForTypeChange() {
	auto hasActiveType = [](AudioMsgId::Type type) {
		const auto current = instance()->current(type);
		const auto state = instance()->getState(type);
		return (current == state.id && !IsStoppedOrStopping(state.state));
	};
	if (hasActiveType(AudioMsgId::Type::Voice)) {
		_voiceIsActive = true;
		setType(AudioMsgId::Type::Voice);
	} else if (!_voiceIsActive && hasActiveType(AudioMsgId::Type::Song)) {
		setType(AudioMsgId::Type::Song);
	}
}

bool Widget::hasPlaybackSpeedControl() const {
	return _lastSongId.changeablePlaybackSpeed()
		&& Media::Audio::SupportsSpeedControl();
}

void Widget::updateControlsVisibility() {
	_repeatToggle->setVisible(_type == AudioMsgId::Type::Song);
	_orderToggle->setVisible(_type == AudioMsgId::Type::Song);
	_lyricsToggle->setVisible(_type == AudioMsgId::Type::Song);
	_fullscreenToggle->setVisible(_type == AudioMsgId::Type::Song);
	_speedToggle->setVisible(hasPlaybackSpeedControl());
	if (!_shadow->isHidden()) {
		_playbackSlider->setVisible(_type == AudioMsgId::Type::Song);
	}
	updateControlsGeometry();
}

void Widget::setType(AudioMsgId::Type type) {
	if (_type != type) {
		_type = type;
		handleSongChange();
		updateControlsVisibility();
		updateLabelsGeometry();
		handleSongUpdate(instance()->getState(_type));
		updateOverLabelsState(_labelsOver);
		_playlistChangesLifetime = instance()->playlistChanges(
			_type
		) | rpl::on_next([=] {
			handlePlaylistUpdate();
		});
		// maybe the type change causes a change of the button layout
		QResizeEvent event = { size(), size() };
		resizeEvent(&event);
	}
}

void Widget::handleSongUpdate(const TrackState &state) {
	checkForTypeChange();
	if (state.id.type() != _type || !state.id.audio()) {
		return;
	}

	if (state.id.audio()->loading()) {
		_playbackProgress->updateLoadingState(state.id.audio()->progress());
	} else {
		_playbackProgress->updateState(state);
	}

	auto showPause = ShowPauseIcon(state.state);
	if (instance()->isSeeking(_type)) {
		showPause = true;
	}
	_playPause->setState(state.id.audio()->loading()
		? PlayButton::State::Cancel
		: showPause
		? PlayButton::State::Pause
		: PlayButton::State::Play);
	_playPause->setAccessibleName(showPause
		? tr::lng_shortcuts_media_pause(tr::now)
		: tr::lng_shortcuts_media_play(tr::now));

	const auto spinning = showPause && !state.id.audio()->loading();
	static_cast<WavySlider*>(_playbackSlider.data())->setPlaying(spinning);
	if (_discPlaying != spinning) {
		_discPlaying = spinning;
		_spin.start([=] { update(); }, spinning ? 0. : 1., spinning ? 1. : 0., spinning ? 500 : 1100, anim::easeOutCubic);
		if (!_discAnimation.animating()) {
			_discAnimation.init([=] {
				const auto more = isVisible()
					&& (_discPlaying || _spin.animating())
					&& !PowerSaving::On(PowerSaving::kAnimations);
				if (more) {
					update(0, 0, 100, st::mediaPlayerHeight);
				}
				return more;
			});
			_discAnimation.start();
		}
	}
	updateTimeText(state);
}

void Widget::updateTimeText(const TrackState &state) {
	qint64 display = 0;
	const auto frequency = state.frequency;
	const auto document = state.id.audio();
	if (!IsStoppedOrStopping(state.state)) {
		display = state.position;
	} else if (state.length) {
		display = state.length;
	} else if (document->song()) {
		display = (document->duration() * frequency) / 1000;
	}

	_lastDurationMs = (state.length * 1000LL) / frequency;

	if (document->loading()) {
		const auto progress = document->progress() * 100;
		_time = QString::number(int(base::SafeRound(progress))) + '%';
		_playbackSlider->setDisabled(true);
	} else {
		display = display / frequency;
		_time = Ui::FormatDurationText(display);
		_playbackSlider->setDisabled(false);
	}
	if (_seekPositionMs < 0) {
		updateTimeLabel();
	}
}

void Widget::updateTimeLabel() {
	auto timeLabelWidth = _timeLabel->width();
	if (_seekPositionMs >= 0) {
		auto playAlready = _seekPositionMs / 1000LL;
		_timeLabel->setText(Ui::FormatDurationText(playAlready));
	} else {
		_timeLabel->setText(_time);
	}
	if (timeLabelWidth != _timeLabel->width()) {
		updateLabelsGeometry();
	}
}

void Widget::handleSongChange() {
	const auto current = instance()->current(_type);
	const auto document = current.audio();
	_lastSongFromAnotherSession = document
		&& (document->session().uniqueId()
			!= _controller->session().uniqueId());
	if (!current
		|| !document
		|| ((_lastSongId.audio() == document)
			&& (_lastSongId.contextId() == current.contextId()))) {
		return;
	}
	_lastSongId = current;
	_voiceUserpic = {};
	refreshSongCover();
	_speedController->reloadFromLookup();

	auto textWithEntities = TextWithEntities();
	if (document->isVoiceMessage() || document->isVideoMessage()) {
		textWithEntities = Ui::Text::FormatVoiceName(
			document,
			current.contextId()).textWithEntities(true);
	} else {
		textWithEntities = tr::marked(SongNameForPlayback(current).title);
	}
	_nameLabel->setMarkedText(textWithEntities);
	handlePlaylistUpdate();
	updateLabelsGeometry();
}

void Widget::refreshSongCover() {
	_songMediaLifetime.destroy();
	_songMedia = nullptr;
	if (const auto peer = (_type == AudioMsgId::Type::Voice)
		? voiceSender()
		: nullptr) {
		peer->session().changes().peerUpdates(
			peer,
			Data::PeerUpdate::Flag::Photo
		) | rpl::on_next([=] {
			update();
		}, _songMediaLifetime);
		peer->session().downloaderTaskFinished(
		) | rpl::on_next([=] {
			update();
		}, _songMediaLifetime);
	}
	const auto document = _lastSongId.audio();
	if (_type == AudioMsgId::Type::Song
		&& document
		&& !Mokagram::Music::OwnsTrack(_lastSongId)) {
		_songMedia = document->createMediaView();
		if (document->hasThumbnail() && !_songMedia->thumbnail()) {
			document->session().downloaderTaskFinished(
			) | rpl::on_next([=] {
				update();
				if (_songMedia->thumbnail()) {
					_songMediaLifetime.destroy();
				}
			}, _songMediaLifetime);
			_songMedia->thumbnailWanted(_lastSongId.contextId());
		}
	}
	update();
}

void Widget::handlePlaylistUpdate() {
	const auto previousEnabled = instance()->previousAvailable(_type);
	const auto nextEnabled = instance()->nextAvailable(_type);
	if (!previousEnabled && !nextEnabled) {
		destroyPrevNextButtons();
	} else {
		createPrevNextButtons();
		_previousTrack->setIconOverride(previousEnabled ? nullptr : &st::mediaPlayerPreviousDisabledIcon);
		_previousTrack->setRippleColorOverride(previousEnabled ? nullptr : &st::mediaPlayerBg);
		_previousTrack->setPointerCursor(previousEnabled);
		_nextTrack->setIconOverride(nextEnabled ? nullptr : &st::mediaPlayerNextDisabledIcon);
		_nextTrack->setRippleColorOverride(nextEnabled ? nullptr : &st::mediaPlayerBg);
		_nextTrack->setPointerCursor(nextEnabled);
	}
}

void Widget::createPrevNextButtons() {
	if (!_previousTrack) {
		_previousTrack.create(this, st::mediaPlayerPreviousButton);
		_previousTrack->show();
		_previousTrack->setClickedCallback([=]() {
			instance()->previous(_type);
		});
		_previousTrack->setAccessibleName(tr::lng_shortcuts_media_previous(tr::now));
		_nextTrack.create(this, st::mediaPlayerNextButton);
		_nextTrack->show();
		_nextTrack->setClickedCallback([=]() {
			instance()->next(_type);
		});
		_nextTrack->setAccessibleName(tr::lng_shortcuts_media_next(tr::now));
		hidePlaylistOn(_previousTrack);
		hidePlaylistOn(_nextTrack);
		updatePlayPrevNextPositions();
	}
}

void Widget::destroyPrevNextButtons() {
	if (_previousTrack) {
		_previousTrack.destroy();
		_nextTrack.destroy();
		updatePlayPrevNextPositions();
	}
}

} // namespace Player
} // namespace Media
