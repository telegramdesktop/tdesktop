/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/stories/media_stories_header.h"

#include "base/unixtime.h"
#include "chat_helpers/compose/compose_show.h"
#include "data/data_peer.h"
#include "media/stories/media_stories_controller.h"
#include "lang/lang_keys.h"
#include "ui/controls/userpic_button.h"
#include "ui/layers/box_content.h"
#include "ui/text/format_values.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/continuous_sliders.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/tooltip.h"
#include "ui/wrap/fade_wrap.h"
#include "ui/painter.h"
#include "ui/rp_widget.h"
#include "styles/style_media_view.h"

#include <QtGui/QGuiApplication>

namespace Media::Stories {
namespace {

constexpr auto kNameOpacity = 1.;
constexpr auto kDateOpacity = 0.8;
constexpr auto kControlOpacity = 0.65;
constexpr auto kControlOpacityOver = 1.;
constexpr auto kControlOpacityDisabled = 0.45;
constexpr auto kVolumeHideTimeoutShort = crl::time(20);
constexpr auto kVolumeHideTimeoutLong = crl::time(200);

struct Timestamp {
	QString text;
	TimeId changes = 0;
};

struct PrivacyBadge {
	const style::icon *icon = nullptr;
	const style::color *bg1 = nullptr;
	const style::color *bg2 = nullptr;
};

class UserpicBadge final : public Ui::RpWidget {
public:
	UserpicBadge(not_null<QWidget*> userpic, PrivacyBadge badge);

	[[nodiscard]] QRect badgeGeometry() const;

private:
	bool eventFilter(QObject *o, QEvent *e) override;
	void paintEvent(QPaintEvent *e) override;

	void updateGeometry();

	const not_null<QWidget*> _userpic;
	const PrivacyBadge _badgeData;
	QRect _badge;
	QImage _layer;
	bool _grabbing = false;

};

[[nodiscard]] PrivacyBadge LookupPrivacyBadge(Data::StoryPrivacy privacy) {
	using namespace Data;
	static const auto badges = base::flat_map<StoryPrivacy, PrivacyBadge>{
		{ StoryPrivacy::CloseFriends, PrivacyBadge{
			&st::storiesBadgeCloseFriends,
			&st::historyPeer2UserpicBg,
			&st::historyPeer2UserpicBg2,
		} },
		{ StoryPrivacy::Contacts, PrivacyBadge{
			&st::storiesBadgeContacts,
			&st::historyPeer5UserpicBg,
			&st::historyPeer5UserpicBg2,
		} },
		{ StoryPrivacy::SelectedContacts, PrivacyBadge{
			&st::storiesBadgeSelectedContacts,
			&st::historyPeer8UserpicBg,
			&st::historyPeer8UserpicBg2,
		} },
	};
	if (const auto i = badges.find(privacy); i != end(badges)) {
		return i->second;
	}
	return {};
}

UserpicBadge::UserpicBadge(not_null<QWidget*> userpic, PrivacyBadge badge)
: RpWidget(userpic->parentWidget())
, _userpic(userpic)
, _badgeData(badge) {
	userpic->installEventFilter(this);
	updateGeometry();
	setAttribute(Qt::WA_TransparentForMouseEvents);
	Ui::PostponeCall(this, [=] {
		_userpic->raise();
	});
	show();
}

QRect UserpicBadge::badgeGeometry() const {
	return _badge;
}

bool UserpicBadge::eventFilter(QObject *o, QEvent *e) {
	if (o != _userpic) {
		return false;
	}
	const auto type = e->type();
	switch (type) {
	case QEvent::Move:
	case QEvent::Resize:
		updateGeometry();
		return false;
	case QEvent::Paint:
		return !_grabbing;
	}
	return false;
}

void UserpicBadge::paintEvent(QPaintEvent *e) {
	const auto ratio = style::DevicePixelRatio();
	const auto layerSize = size() * ratio;
	if (_layer.size() != layerSize) {
		_layer = QImage(layerSize, QImage::Format_ARGB32_Premultiplied);
		_layer.setDevicePixelRatio(ratio);
	}
	_layer.fill(Qt::transparent);
	auto q = QPainter(&_layer);

	_grabbing = true;
	Ui::RenderWidget(q, _userpic);
	_grabbing = false;

	auto hq = PainterHighQualityEnabler(q);
	auto pen = st::transparent->p;
	pen.setWidthF(st::storiesBadgeOutline);
	const auto half = st::storiesBadgeOutline / 2.;
	auto outer = QRectF(_badge).marginsAdded({ half, half, half, half });
	auto gradient = QLinearGradient(outer.topLeft(), outer.bottomLeft());
	gradient.setStops({
		{ 0., (*_badgeData.bg1)->c },
		{ 1., (*_badgeData.bg2)->c },
	});
	q.setPen(pen);
	q.setBrush(gradient);
	q.setCompositionMode(QPainter::CompositionMode_Source);
	q.drawEllipse(outer);
	q.setCompositionMode(QPainter::CompositionMode_SourceOver);
	_badgeData.icon->paintInCenter(q, _badge);
	q.end();

	QPainter(this).drawImage(0, 0, _layer);
}

void UserpicBadge::updateGeometry() {
	const auto width = _userpic->width() + st::storiesBadgeShift.x();
	const auto height = _userpic->height() + st::storiesBadgeShift.y();
	setGeometry(QRect(_userpic->pos(), QSize{ width, height }));
	const auto inner = QRect(QPoint(), _badgeData.icon->size());
	const auto badge = inner.marginsAdded(st::storiesBadgePadding).size();
	_badge = QRect(
		QPoint(width - badge.width(), height - badge.height()),
		badge);
	update();
}

struct MadePrivacyBadge {
	std::unique_ptr<Ui::RpWidget> widget;
	QRect geometry;
};

[[nodiscard]] MadePrivacyBadge MakePrivacyBadge(
		not_null<QWidget*> userpic,
		Data::StoryPrivacy privacy) {
	const auto badge = LookupPrivacyBadge(privacy);
	if (!badge.icon) {
		return {};
	}
	auto widget = std::make_unique<UserpicBadge>(userpic, badge);
	const auto geometry = widget->badgeGeometry();
	return {
		.widget = std::move(widget),
		.geometry = geometry,
	};
}

[[nodiscard]] Timestamp ComposeTimestamp(TimeId when, TimeId now) {
	const auto minutes = (now - when) / 60;
	if (!minutes) {
		return { tr::lng_mediaview_just_now(tr::now), 61 - (now - when) };
	} else if (minutes < 60) {
		return {
			tr::lng_mediaview_minutes_ago(tr::now, lt_count, minutes),
			61 - ((now - when) % 60),
		};
	}
	const auto hours = (now - when) / 3600;
	if (hours < 12) {
		return {
			tr::lng_mediaview_hours_ago(tr::now, lt_count, hours),
			3601 - ((now - when) % 3600),
		};
	}
	const auto whenFull = base::unixtime::parse(when);
	const auto nowFull = base::unixtime::parse(now);
	const auto locale = QLocale();
	auto tomorrow = nowFull;
	tomorrow.setDate(nowFull.date().addDays(1));
	tomorrow.setTime(QTime(0, 0, 1));
	const auto seconds = int(nowFull.secsTo(tomorrow));
	if (whenFull.date() == nowFull.date()) {
		const auto whenTime = locale.toString(
			whenFull.time(),
			QLocale::ShortFormat);
		return {
			tr::lng_mediaview_today(tr::now, lt_time, whenTime),
			seconds,
		};
	} else if (whenFull.date().addDays(1) == nowFull.date()) {
		const auto whenTime = locale.toString(
			whenFull.time(),
			QLocale::ShortFormat);
		return {
			tr::lng_mediaview_yesterday(tr::now, lt_time, whenTime),
			seconds,
		};
	}
	return { Ui::FormatDateTime(whenFull) };
}

[[nodiscard]] QString ComposeCounter(HeaderData data) {
	const auto index = data.fullIndex + 1;
	const auto count = data.fullCount;
	return count
		? QString::fromUtf8(" \xE2\x80\xA2 %1/%2").arg(index).arg(count)
		: QString();
}

[[nodiscard]] Timestamp ComposeDetails(HeaderData data, TimeId now) {
	auto result = ComposeTimestamp(data.date, now);
	if (data.edited) {
		result.text.append(
			QString::fromUtf8(" \xE2\x80\xA2 ") + tr::lng_edited(tr::now));
	}
	return result;
}

} // namespace

Header::Header(not_null<Controller*> controller)
: _controller(controller)
, _dateUpdateTimer([=] { updateDateText(); }) {
}

Header::~Header() = default;

void Header::show(HeaderData data) {
	if (_data == data) {
		return;
	}
	const auto peerChanged = !_data || (_data->peer != data.peer);
	_data = data;
	const auto updateInfoGeometry = [=] {
		if (_name && _date) {
			const auto namex = st::storiesHeaderNamePosition.x();
			const auto namer = namex + _name->width();
			const auto datex = st::storiesHeaderDatePosition.x();
			const auto dater = datex + _date->width();
			const auto r = std::max(namer, dater);
			_info->setGeometry({ 0, 0, r, _widget->height() });
		}
	};
	_tooltip = nullptr;
	_tooltipShown = false;
	if (peerChanged) {
		_volume = nullptr;
		_date = nullptr;
		_name = nullptr;
		_counter = nullptr;
		_userpic = nullptr;
		_info = nullptr;
		_privacy = nullptr;
		_playPause = nullptr;
		_volumeToggle = nullptr;
		const auto parent = _controller->wrap();
		auto widget = std::make_unique<Ui::RpWidget>(parent);
		const auto raw = widget.get();

		_info = std::make_unique<Ui::AbstractButton>(raw);
		_info->setClickedCallback([=] {
			_controller->uiShow()->show(PrepareShortInfoBox(_data->peer));
		});

		_userpic = std::make_unique<Ui::UserpicButton>(
			raw,
			data.peer,
			st::storiesHeaderPhoto);
		_userpic->setAttribute(Qt::WA_TransparentForMouseEvents);
		_userpic->show();
		_userpic->move(
			st::storiesHeaderMargin.left(),
			st::storiesHeaderMargin.top());

		_name = std::make_unique<Ui::FlatLabel>(
			raw,
			rpl::single(data.peer->isSelf()
				? tr::lng_stories_my_name(tr::now)
				: data.peer->name()),
			st::storiesHeaderName);
		_name->setAttribute(Qt::WA_TransparentForMouseEvents);
		_name->setOpacity(kNameOpacity);
		_name->show();
		_name->move(st::storiesHeaderNamePosition);

		rpl::combine(
			_name->widthValue(),
			raw->heightValue()
		) | rpl::start_with_next(updateInfoGeometry, _name->lifetime());

		raw->show();
		_widget = std::move(widget);

		_controller->layoutValue(
		) | rpl::start_with_next([=](const Layout &layout) {
			raw->setGeometry(layout.header);
			_contentGeometry = layout.content;
			updateTooltipGeometry();
		}, raw->lifetime());
	}
	auto timestamp = ComposeDetails(data, base::unixtime::now());
	_date = std::make_unique<Ui::FlatLabel>(
		_widget.get(),
		std::move(timestamp.text),
		st::storiesHeaderDate);
	_date->setAttribute(Qt::WA_TransparentForMouseEvents);
	_date->setOpacity(kDateOpacity);
	_date->show();
	_date->move(st::storiesHeaderDatePosition);

	_date->widthValue(
	) | rpl::start_with_next(updateInfoGeometry, _date->lifetime());

	auto counter = ComposeCounter(data);
	if (!counter.isEmpty()) {
		_counter = std::make_unique<Ui::FlatLabel>(
			_widget.get(),
			std::move(counter),
			st::storiesHeaderDate);
		_counter->resizeToWidth(_counter->textMaxWidth());
		_counter->setAttribute(Qt::WA_TransparentForMouseEvents);
		_counter->setOpacity(kNameOpacity);
		_counter->show();
	} else {
		_counter = nullptr;
	}

	auto made = MakePrivacyBadge(_userpic.get(), data.privacy);
	_privacy = std::move(made.widget);
	_privacyBadgeOver = false;
	_privacyBadgeGeometry = _privacy
		? Ui::MapFrom(_info.get(), _privacy.get(), made.geometry)
		: QRect();
	if (_privacy) {
		_info->setMouseTracking(true);
		_info->events(
		) | rpl::filter([=](not_null<QEvent*> e) {
			const auto type = e->type();
			if (type != QEvent::Leave && type != QEvent::MouseMove) {
				return false;
			}
			const auto over = (type == QEvent::MouseMove)
				&& _privacyBadgeGeometry.contains(
					static_cast<QMouseEvent*>(e.get())->pos());
			return (_privacyBadgeOver != over);
		}) | rpl::start_with_next([=] {
			_privacyBadgeOver = !_privacyBadgeOver;
			toggleTooltip(Tooltip::Privacy, _privacyBadgeOver);
		}, _privacy->lifetime());
	}

	if (data.video) {
		createPlayPause();
		createVolumeToggle();

		_widget->widthValue() | rpl::start_with_next([=](int width) {
			const auto playPause = st::storiesPlayButtonPosition;
			_playPause->moveToRight(playPause.x(), playPause.y(), width);
			const auto volume = st::storiesVolumeButtonPosition;
			_volumeToggle->moveToRight(volume.x(), volume.y(), width);
			updateTooltipGeometry();
		}, _playPause->lifetime());

		_pauseState = _controller->pauseState();
		applyPauseState();
	} else {
		_playPause = nullptr;
		_volumeToggle = nullptr;
		_volume = nullptr;
	}

	rpl::combine(
		_widget->widthValue(),
		_counter ? _counter->widthValue() : rpl::single(0),
		_dateUpdated.events_starting_with_copy(rpl::empty)
	) | rpl::start_with_next([=](int outer, int counter, auto) {
		const auto right = _playPause
			? _playPause->x()
			: (outer - st::storiesHeaderMargin.right());
		const auto nameLeft = st::storiesHeaderNamePosition.x();
		if (counter) {
			counter += st::normalFont->spacew;
		}
		const auto nameAvailable = right - nameLeft - counter;
		auto counterLeft = nameLeft;
		if (nameAvailable <= 0) {
			_name->hide();
		} else {
			_name->show();
			_name->resizeToNaturalWidth(nameAvailable);
			counterLeft += _name->width() + st::normalFont->spacew;
		}
		if (_counter) {
			_counter->move(counterLeft, _name->y());
		}
		const auto dateLeft = st::storiesHeaderDatePosition.x();
		const auto dateAvailable = right - dateLeft;
		if (dateAvailable <= 0) {
			_date->hide();
		} else {
			_date->show();
			_date->resizeToNaturalWidth(dateAvailable);
		}
	}, _date->lifetime());

	if (timestamp.changes > 0) {
		_dateUpdateTimer.callOnce(timestamp.changes * crl::time(1000));
	}
}

void Header::createPlayPause() {
	struct PlayPauseState {
		Ui::Animations::Simple overAnimation;
		bool over = false;
		bool down = false;
	};
	_playPause = std::make_unique<Ui::RpWidget>(_widget.get());
	auto &lifetime = _playPause->lifetime();
	const auto state = lifetime.make_state<PlayPauseState>();

	_playPause->events(
	) | rpl::start_with_next([=](not_null<QEvent*> e) {
		const auto type = e->type();
		if (type == QEvent::Enter || type == QEvent::Leave) {
			const auto over = (e->type() == QEvent::Enter);
			if (state->over != over) {
				state->over = over;
				state->overAnimation.start(
					[=] { _playPause->update(); },
					over ? 0. : 1.,
					over ? 1. : 0.,
					st::mediaviewFadeDuration);
			}
		} else if (type == QEvent::MouseButtonPress && state->over) {
			state->down = true;
		} else if (type == QEvent::MouseButtonRelease) {
			const auto down = base::take(state->down);
			if (down && state->over) {
				const auto paused = (_pauseState == PauseState::Paused)
					|| (_pauseState == PauseState::InactivePaused);
				_controller->togglePaused(!paused);
			}
		}
	}, lifetime);

	_playPause->paintRequest() | rpl::start_with_next([=] {
		auto p = QPainter(_playPause.get());
		const auto paused = (_pauseState == PauseState::Paused)
			|| (_pauseState == PauseState::InactivePaused);
		const auto icon = paused
			? &st::storiesPlayIcon
			: &st::storiesPauseIcon;
		const auto over = state->overAnimation.value(
			state->over ? 1. : 0.);
		p.setOpacity(over * kControlOpacityOver
			+ (1. - over) * kControlOpacity);
		icon->paint(
			p,
			st::storiesPlayButton.iconPosition,
			_playPause->width());
	}, lifetime);

	_playPause->resize(
		st::storiesPlayButton.width,
		st::storiesPlayButton.height);
	_playPause->show();
	_playPause->setCursor(style::cur_pointer);
}

void Header::createVolumeToggle() {
	Expects(_data.has_value());

	struct VolumeState {
		base::Timer hideTimer;
		bool over = false;
		bool silent = false;
		bool dropdownOver = false;
	};
	_volumeToggle = std::make_unique<Ui::RpWidget>(_widget.get());
	auto &lifetime = _volumeToggle->lifetime();
	const auto state = lifetime.make_state<VolumeState>();
	state->silent = _data->silent;
	state->hideTimer.setCallback([=] {
		_volume->toggle(false, anim::type::normal);
	});

	_volumeToggle->events(
	) | rpl::start_with_next([=](not_null<QEvent*> e) {
		const auto type = e->type();
		if (type == QEvent::Enter || type == QEvent::Leave) {
			const auto over = (e->type() == QEvent::Enter);
			if (state->over != over) {
				state->over = over;
				if (state->silent) {
					toggleTooltip(Tooltip::SilentVideo, over);
				} else if (over) {
					state->hideTimer.cancel();
					_volume->toggle(true, anim::type::normal);
				} else if (!state->dropdownOver) {
					state->hideTimer.callOnce(kVolumeHideTimeoutShort);
				}
			}
		}
	}, lifetime);

	_volumeToggle->paintRequest() | rpl::start_with_next([=] {
		auto p = QPainter(_volumeToggle.get());
		p.setOpacity(state->silent
			? kControlOpacityDisabled
			: kControlOpacity);
		_volumeIcon.current()->paint(
			p,
			st::storiesVolumeButton.iconPosition,
			_volumeToggle->width());
	}, lifetime);
	updateVolumeIcon();

	_volume = std::make_unique<Ui::FadeWrap<Ui::RpWidget>>(
		_widget->parentWidget(),
		object_ptr<Ui::RpWidget>(_widget->parentWidget()));
	_volume->toggle(false, anim::type::instant);
	_volume->events(
	) | rpl::start_with_next([=](not_null<QEvent*> e) {
		const auto type = e->type();
		if (type == QEvent::Enter || type == QEvent::Leave) {
			const auto over = (e->type() == QEvent::Enter);
			if (state->dropdownOver != over) {
				state->dropdownOver = over;
				if (over) {
					state->hideTimer.cancel();
					_volume->toggle(true, anim::type::normal);
				} else if (!state->over) {
					state->hideTimer.callOnce(kVolumeHideTimeoutLong);
				}
			}
		}
	}, lifetime);
	rebuildVolumeControls(_volume->entity(), false);

	rpl::combine(
		_widget->positionValue(),
		_volumeToggle->positionValue(),
		rpl::mappers::_1 + rpl::mappers::_2
	) | rpl::start_with_next([=](QPoint position) {
		_volume->move(position);
	}, _volume->lifetime());

	_volumeToggle->resize(
		st::storiesVolumeButton.width,
		st::storiesVolumeButton.height);
	_volumeToggle->show();
	if (!state->silent) {
		_volumeToggle->setCursor(style::cur_pointer);
	}
}

void Header::toggleTooltip(Tooltip type, bool show) {
	const auto guard = gsl::finally([&] {
		_tooltipShown = (_tooltip != nullptr);
	});
	if (const auto was = _tooltip.release()) {
		was->toggleAnimated(false);
	}
	if (!show) {
		return;
	}
	const auto text = [&]() -> TextWithEntities {
		using Privacy = Data::StoryPrivacy;
		const auto boldName = Ui::Text::Bold(_data->peer->shortName());
		const auto self = _data->peer->isSelf();
		switch (type) {
		case Tooltip::SilentVideo:
			return { tr::lng_stories_about_silent(tr::now) };
		case Tooltip::Privacy: switch (_data->privacy) {
			case Privacy::CloseFriends:
				return self
					? tr::lng_stories_about_close_friends_my(
						tr::now,
						Ui::Text::RichLangValue)
					: tr::lng_stories_about_close_friends(
						tr::now,
						lt_user,
						boldName,
						Ui::Text::RichLangValue);
			case Privacy::Contacts:
				return self
					? tr::lng_stories_about_contacts_my(
						tr::now,
						Ui::Text::RichLangValue)
					: tr::lng_stories_about_contacts(
						tr::now,
						lt_user,
						boldName,
						Ui::Text::RichLangValue);
			case Privacy::SelectedContacts:
				return self
					? tr::lng_stories_about_selected_contacts_my(
						tr::now,
						Ui::Text::RichLangValue)
					: tr::lng_stories_about_selected_contacts(
						tr::now,
						lt_user,
						boldName,
						Ui::Text::RichLangValue);
			}
		}
		return {};
	}();
	if (text.empty()) {
		return;
	}
	_tooltipType = type;
	_tooltip = std::make_unique<Ui::ImportantTooltip>(
		_widget->parentWidget(),
		Ui::MakeNiceTooltipLabel(
			_widget.get(),
			rpl::single(text),
			st::storiesInfoTooltipMaxWidth,
			st::storiesInfoTooltipLabel),
		st::storiesInfoTooltip);
	const auto tooltip = _tooltip.get();
	const auto weak = QPointer<QWidget>(tooltip);
	const auto destroy = [=] {
		delete weak.data();
	};
	tooltip->setAttribute(Qt::WA_TransparentForMouseEvents);
	tooltip->setHiddenCallback(destroy);
	updateTooltipGeometry();
	tooltip->toggleAnimated(true);
}

void Header::updateTooltipGeometry() {
	if (!_tooltip) {
		return;
	}
	const auto geometry = [&] {
		switch (_tooltipType) {
		case Tooltip::SilentVideo:
			return Ui::MapFrom(
				_widget->parentWidget(),
				_volumeToggle.get(),
				_volumeToggle->rect());
		case Tooltip::Privacy:
			return Ui::MapFrom(
				_widget->parentWidget(),
				_info.get(),
				_privacyBadgeGeometry.marginsAdded(
					st::storiesInfoTooltip.padding));
		}
		return QRect();
	}();
	if (geometry.isEmpty()) {
		toggleTooltip(Tooltip::None, false);
		return;
	}
	const auto weak = QPointer<QWidget>(_tooltip.get());
	const auto countPosition = [=](QSize size) {
		const auto result = geometry.bottomLeft()
			- QPoint(size.width() / 2, 0);
		const auto inner = _contentGeometry.marginsRemoved(
			st::storiesInfoTooltip.padding);
		if (size.width() > inner.width()) {
			return QPoint(
				inner.x() + (inner.width() - size.width()) / 2,
				result.y());
		} else if (result.x() < inner.x()) {
			return QPoint(inner.x(), result.y());
		}
		return result;
	};
	_tooltip->pointAt(geometry, RectPart::Bottom, countPosition);
}

void Header::rebuildVolumeControls(
		not_null<Ui::RpWidget*> dropdown,
		bool horizontal) {
	auto removed = false;
	do {
		removed = false;
		for (const auto &child : dropdown->children()) {
			if (child->isWidgetType()) {
				removed = true;
				delete child;
				break;
			}
		}
	} while (removed);

	const auto button = Ui::CreateChild<Ui::IconButton>(
		dropdown.get(),
		st::storiesVolumeButton);
	_volumeIcon.value(
	) | rpl::start_with_next([=](const style::icon *icon) {
		button->setIconOverride(icon, icon);
	}, button->lifetime());

	const auto slider = Ui::CreateChild<Ui::MediaSlider>(
		dropdown.get(),
		st::storiesVolumeSlider);
	slider->setMoveByWheel(true);
	slider->setAlwaysDisplayMarker(true);
	using Direction = Ui::MediaSlider::Direction;
	slider->setDirection(horizontal
		? Direction::Horizontal
		: Direction::Vertical);

	slider->setChangeProgressCallback([=](float64 value) {
		_ignoreWindowMove = true;
		_controller->changeVolume(value);
		updateVolumeIcon();
	});
	slider->setChangeFinishedCallback([=](float64 value) {
		_ignoreWindowMove = false;
		_controller->volumeChangeFinished();
	});
	button->setClickedCallback([=] {
		_controller->toggleVolume();
		slider->setValue(_controller->currentVolume());
		updateVolumeIcon();
	});
	slider->setValue(_controller->currentVolume());

	const auto size = button->width()
		+ st::storiesVolumeSize
		+ st::storiesVolumeBottom;
	const auto seekSize = st::storiesVolumeSlider.seekSize;

	button->move(0, 0);
	if (horizontal) {
		dropdown->resize(size, button->height());
		slider->resize(st::storiesVolumeSize, seekSize.height());
		slider->move(
			button->width(),
			(button->height() - slider->height()) / 2);
	} else {
		dropdown->resize(button->width(), size);
		slider->resize(seekSize.width(), st::storiesVolumeSize);
		slider->move(
			(button->width() - slider->width()) / 2,
			button->height());
	}

	dropdown->paintRequest(
	) | rpl::start_with_next([=] {
		auto p = QPainter(dropdown);
		auto hq = PainterHighQualityEnabler(p);
		const auto radius = button->width() / 2.;
		p.setPen(Qt::NoPen);
		p.setBrush(st::mediaviewSaveMsgBg);
		p.drawRoundedRect(dropdown->rect(), radius, radius);
	}, button->lifetime());
}

void Header::updatePauseState() {
	if (!_playPause) {
		return;
	} else if (const auto s = _controller->pauseState(); _pauseState != s) {
		_pauseState = s;
		applyPauseState();
	}
}

void Header::updateVolumeIcon() {
	const auto volume = _controller->currentVolume();
	_volumeIcon = (volume <= 0. || (_data && _data->silent))
		? &st::mediaviewVolumeIcon0Over
		: (volume < 1 / 2.)
		? &st::mediaviewVolumeIcon1Over
		: &st::mediaviewVolumeIcon2Over;
}

void Header::applyPauseState() {
	Expects(_playPause != nullptr);

	const auto inactive = (_pauseState == PauseState::Inactive)
		|| (_pauseState == PauseState::InactivePaused);
	_playPause->setAttribute(Qt::WA_TransparentForMouseEvents, inactive);
	if (inactive) {
		QEvent e(QEvent::Leave);
		QGuiApplication::sendEvent(_playPause.get(), &e);
	}
	_playPause->update();
}

void Header::raise() {
	if (_widget) {
		_widget->raise();
	}
}

bool Header::ignoreWindowMove(QPoint position) const {
	return _ignoreWindowMove;
}

rpl::producer<bool> Header::tooltipShownValue() const {
	return _tooltipShown.value();
}

void Header::updateDateText() {
	if (!_date || !_data || !_data->date) {
		return;
	}
	auto timestamp = ComposeDetails(*_data, base::unixtime::now());
	_date->setText(timestamp.text);
	_dateUpdated.fire({});
	if (timestamp.changes > 0) {
		_dateUpdateTimer.callOnce(timestamp.changes * crl::time(1000));
	}
}

} // namespace Media::Stories
