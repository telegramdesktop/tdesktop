/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/view/media_view_playback_sponsored.h"

#include "data/components/sponsored_messages.h"
#include "data/data_file_origin.h"
#include "data/data_photo.h"
#include "data/data_photo_media.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "menu/menu_sponsored.h"
#include "ui/widgets/menu/menu_add_action_callback.h"
#include "ui/widgets/menu/menu_add_action_callback_factory.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/popup_menu.h"
#include "ui/basic_click_handlers.h"
#include "ui/ui_utility.h"
#include "ui/cached_round_corners.h"
#include "styles/style_chat.h"
#include "styles/style_media_view.h"

namespace Media::View {
namespace {

constexpr auto kStartDelayMin = crl::time(1000);
constexpr auto kDurationMin = 5 * crl::time(1000);

enum class Action {
	Close,
	PromotePremium,
	Pause,
	Unpause,
};

[[nodiscard]] style::RoundButton PrepareAboutStyle() {
	static auto textBg = style::complex_color([] {
		auto result = st::mediaviewTextLinkFg->c;
		result.setAlphaF(result.alphaF() * 0.1);
		return result;
	});
	static auto textBgOver = style::complex_color([] {
		auto result = st::mediaviewTextLinkFg->c;
		result.setAlphaF(result.alphaF() * 0.15);
		return result;
	});
	static auto rippleColor = style::complex_color([] {
		auto result = st::mediaviewTextLinkFg->c;
		result.setAlphaF(result.alphaF() * 0.2);
		return result;
	});

	auto result = st::mediaSponsoredAbout;
	result.textFg = st::mediaviewTextLinkFg;
	result.textFgOver = st::mediaviewTextLinkFg;
	result.textBg = textBg.color();
	result.textBgOver = textBgOver.color();
	result.ripple.color = rippleColor.color();
	return result;
}

} // namespace

class PlaybackSponsored::Message final : public Ui::RpWidget {
public:
	Message(
		QWidget *parent,
		std::shared_ptr<ChatHelpers::Show> show,
		const Data::SponsoredMessage &data);

	[[nodiscard]] rpl::producer<Action> actions() const;

	void setFinalPosition(int x, int y);

	void fadeIn();
	void fadeOut(Fn<void()> hidden);

private:
	void paintEvent(QPaintEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;

	int resizeGetHeight(int newWidth) override;

	void populate();
	void startFadeIn();
	void updateShown(Fn<void()> finished = nullptr);

	const not_null<Main::Session*> _session;
	const std::shared_ptr<ChatHelpers::Show> _show;
	const Data::SponsoredMessage _data;

	style::RoundButton _aboutSt;
	std::unique_ptr<Ui::RoundButton> _about;
	std::unique_ptr<Ui::IconButton> _close;

	base::unique_qptr<Ui::PopupMenu> _menu;
	rpl::event_stream<Action> _actions;

	std::shared_ptr<Data::PhotoMedia> _photo;
	Ui::Text::String _title;
	Ui::Text::String _text;

	QPoint _finalPosition;
	int _left = 0;
	int _top = 0;
	int _titleHeight = 0;
	int _textHeight = 0;

	QImage _cache;
	Ui::Animations::Simple _showAnimation;
	bool _shown = false;
	bool _over = false;
	bool _pressed = false;

	rpl::lifetime _photoLifetime;

};

PlaybackSponsored::Message::Message(
	QWidget *parent,
	std::shared_ptr<ChatHelpers::Show> show,
	const Data::SponsoredMessage &data)
: RpWidget(parent)
, _session(&data.history->session())
, _show(std::move(show))
, _data(data)
, _aboutSt(PrepareAboutStyle())
, _about(std::make_unique<Ui::RoundButton>(
	this,
	tr::lng_search_sponsored_button(),
	_aboutSt))
, _close(std::make_unique<Ui::IconButton>(this, st::mediaSponsoredClose)) {
	_about->setTextTransform(Ui::RoundButton::TextTransform::NoTransform);
	setMouseTracking(true);
	populate();
	hide();
}

rpl::producer<Action> PlaybackSponsored::Message::actions() const {
	return _actions.events();
}

void PlaybackSponsored::Message::setFinalPosition(int x, int y) {
	_finalPosition = { x, y };
	if (_shown) {
		updateShown();
	}
}

void PlaybackSponsored::Message::fadeIn() {
	_shown = true;
	if (!_photo || _photo->loaded()) {
		startFadeIn();
		return;
	}
	_photo->owner()->session().downloaderTaskFinished(
	) | rpl::filter([=] {
		return _photo->loaded();
	}) | rpl::start_with_next([=] {
		_photoLifetime.destroy();
		startFadeIn();
	}, _photoLifetime);
}

void PlaybackSponsored::Message::startFadeIn() {
	if (!_shown) {
		return;
	}
	_cache = Ui::GrabWidgetToImage(this);
	_about->hide();
	_close->hide();
	_showAnimation.start([=] {
		updateShown([=] {
			_session->sponsoredMessages().view(_data.randomId);
		});
	}, 0., 1., st::fadeWrapDuration);
	show();
}

void PlaybackSponsored::Message::fadeOut(Fn<void()> hidden) {
	if (!_shown) {
		if (const auto onstack = hidden) {
			onstack();
		}
		return;
	}
	_shown = false;
	_showAnimation.start([=] {
		updateShown(hidden);
	}, 1., 0., st::fadeWrapDuration);
}

void PlaybackSponsored::Message::updateShown(Fn<void()> finished) {
	const auto shown = _showAnimation.value(_shown ? 1. : 0.);
	const auto shift = anim::interpolate(st::mediaSponsoredShift, 0, shown);
	move(_finalPosition.x(), _finalPosition.y() + shift);
	update();
	if (!_showAnimation.animating()) {
		_cache = QImage();
		_close->show();
		_about->show();
		if (const auto onstack = finished) {
			onstack();
		}
	}
}

void PlaybackSponsored::Message::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);

	const auto shown = _showAnimation.value(_shown ? 1. : 0.);
	if (!_cache.isNull()) {
		p.setOpacity(shown);
		p.drawImage(0, 0, _cache);
		return;
	}

	Ui::FillRoundRect(
		p,
		rect(),
		st::mediaviewSaveMsgBg,
		Ui::MediaviewSaveCorners);

	const auto &padding = st::mediaSponsoredPadding;
	if (_photo) {
		if (const auto image = _photo->image(Data::PhotoSize::Large)) {
			const auto size = st::mediaSponsoredThumb;
			const auto x = padding.left();
			const auto y = (height() - size) / 2;
			p.drawPixmap(
				x,
				y,
				image->pixSingle(
					size,
					size,
					{ .options = Images::Option::RoundCircle }));
		}
	}

	p.setPen(st::mediaviewControlFg);

	_title.draw(p, {
		.position = { _left, _top },
		.availableWidth = _about->x() - _left,
		.palette = &st::mediaviewTextPalette,
	});

	_text.draw(p, {
		.position = { _left, _top + _titleHeight },
		.availableWidth = _close->x() - _left,
		.palette = &st::mediaviewTextPalette,
	});
}

void PlaybackSponsored::Message::mouseMoveEvent(QMouseEvent *e) {
	const auto &padding = st::mediaSponsoredPadding;
	const auto point = e->pos();
	const auto about = _about->geometry();
	const auto close = _close->geometry();
	const auto over = !about.marginsAdded(padding).contains(point)
		&& !close.marginsAdded(padding).contains(point);
	if (_over != over) {
		_over = over;
		setCursor(_over ? style::cur_pointer : style::cur_default);
	}
}

void PlaybackSponsored::Message::mousePressEvent(QMouseEvent *e) {
	if (_over) {
		_pressed = true;
	}
}

void PlaybackSponsored::Message::mouseReleaseEvent(QMouseEvent *e) {
	if (base::take(_pressed) && _over) {
		_session->sponsoredMessages().clicked(_data.randomId, false, false);
		UrlClickHandler::Open(_data.link);
	}
}

int PlaybackSponsored::Message::resizeGetHeight(int newWidth) {
	const auto &padding = st::mediaSponsoredPadding;
	const auto userpic = st::mediaSponsoredThumb;
	const auto innerWidth = newWidth - _left - _close->width();
	const auto titleWidth = innerWidth - _about->width() - padding.right();
	_titleHeight = _title.countHeight(titleWidth);
	_textHeight = _text.countHeight(innerWidth);

	const auto use = std::max(_titleHeight + _textHeight, userpic);

	const auto height = padding.top() + use + padding.bottom();
	_left = padding.left() + (_photo ? (userpic + padding.left()) : 0);
	_top = padding.top() + (use - _titleHeight - _textHeight) / 2;

	_about->move(
		_left + std::min(titleWidth, _title.maxWidth()) + padding.right(),
		_top);
	_close->move(
		newWidth - _close->width(),
		(height - _close->height()) / 2);

	return height;
}

void PlaybackSponsored::Message::populate() {
	const auto &from = _data.from;
	const auto photo = from.photoId
		? _data.history->owner().photo(from.photoId).get()
		: nullptr;
	if (photo) {
		_photo = photo->createMediaView();
		photo->load({}, LoadFromCloudOrLocal, true);
	}
	_title = Ui::Text::String(
		st::semiboldTextStyle,
		from.title,
		kDefaultTextOptions,
		st::msgMinWidth);
	_text = Ui::Text::String(
		st::defaultTextStyle,
		_data.textWithEntities,
		kMarkupTextOptions,
		st::msgMinWidth);

	_about->setClickedCallback([=] {
		_menu = nullptr;
		const auto parent = parentWidget();
		_menu = base::make_unique_q<Ui::PopupMenu>(
			parent,
			st::mediaviewPopupMenu);
		const auto raw = _menu.get();
		const auto addAction = Ui::Menu::CreateAddActionCallback(raw);
		Menu::FillSponsored(
			addAction,
			_show,
			Menu::SponsoredPhrases::Channel,
			_session->sponsoredMessages().lookupDetails(_data),
			_session->sponsoredMessages().createReportCallback(
				_data.randomId,
				crl::guard(this, [=] { _actions.fire(Action::Close); })),
			{ .dark = true });
		_actions.fire(Action::Pause);
		Ui::Connect(raw, &QObject::destroyed, this, [=] {
			_actions.fire(Action::Unpause);
		});
		raw->popup(QCursor::pos());
	});
	_close->setClickedCallback([=] {

	});
}

PlaybackSponsored::PlaybackSponsored(
	not_null<Ui::RpWidget*> controls,
	std::shared_ptr<ChatHelpers::Show> show,
	not_null<HistoryItem*> item)
: _parent(controls->parentWidget())
, _session(&item->history()->session())
, _show(std::move(show))
, _itemId(item->fullId())
, _controlsGeometry(controls->geometryValue())
, _timer([=] { update(); }) {
	_session->sponsoredMessages().requestForVideo(item, crl::guard(this, [=](
			Data::SponsoredForVideo data) {
		if (data.list.empty()) {
			return;
		}
		_data = std::move(data);
		if (_data->state.initial()
			|| (_data->state.itemIndex > _data->list.size())
			|| (_data->state.itemIndex == _data->list.size()
				&& _data->state.leftTillShow <= 0)) {
			_data->state.itemIndex = 0;
			_data->state.leftTillShow = std::max(
				_data->startDelay,
				kStartDelayMin);
		}
		update();
	}));
}

PlaybackSponsored::~PlaybackSponsored() {
	saveState();
}

void PlaybackSponsored::start() {
	_started = true;
	if (!_paused) {
		_start = crl::now();
		update();
	}
}

void PlaybackSponsored::setPaused(bool paused) {
	setPausedOutside(paused);
}

void PlaybackSponsored::updatePaused() {
	const auto paused = _pausedInside || _pausedOutside;
	if (_paused == paused) {
		return;
	}
	_paused = paused;
	if (!_started) {
		return;
	} else if (_paused) {
		update();
		const auto state = computeState();
		_start = 0;
		_timer.cancel();
	} else {
		_start = crl::now();
		update();
	}
}

void PlaybackSponsored::setPausedInside(bool paused) {
	if (_pausedInside == paused) {
		return;
	}
	_pausedInside = paused;
	updatePaused();
}

void PlaybackSponsored::setPausedOutside(bool paused) {
	if (_pausedOutside == paused) {
		return;
	}
	_pausedOutside = paused;
	updatePaused();
}

void PlaybackSponsored::finish() {
	_timer.cancel();
	if (_data) {
		saveState();
		_data = std::nullopt;
	}
}

void PlaybackSponsored::update() {
	if (!_data || !_start) {
		return;
	}

	const auto [now, state] = computeState();
	const auto message = (_data->state.itemIndex < _data->list.size())
		? &_data->list[state.itemIndex]
		: nullptr;
	const auto duration = message
		? std::max(
			message->durationMin + kDurationMin,
			message->durationMax)
		: crl::time(0);
	if (_data->state.leftTillShow > 0 && state.leftTillShow <= 0) {
		_data->state.leftTillShow = 0;
		if (duration) {
			show(*message);

			_start = now;
			_timer.callOnce(duration);
			saveState();
		} else {
			finish();
		}
	} else if (_data->state.leftTillShow <= 0
		&& state.leftTillShow <= -duration) {
		hide();

		++_data->state.itemIndex;
		_data->state.leftTillShow = std::max(
			_data->betweenDelay,
			kStartDelayMin);
		_start = now;
		_timer.callOnce(_data->state.leftTillShow);
		saveState();
	} else {
		if (state.leftTillShow <= 0 && duration && !_widget) {
			show(*message);
		}
		_data->state = state;
		_timer.callOnce((state.leftTillShow > 0)
			? state.leftTillShow
			: (state.leftTillShow + duration));
	}
}


void PlaybackSponsored::show(const Data::SponsoredMessage &data) {
	_widget = std::make_unique<Message>(_parent, _show, data);
	const auto raw = _widget.get();

	_controlsGeometry.value() | rpl::start_with_next([=](QRect controls) {
		raw->resizeToWidth(controls.width());
		raw->setFinalPosition(
			controls.x(),
			controls.y() - st::mediaSponsoredSkip - raw->height());
	}, raw->lifetime());

	raw->actions() | rpl::start_with_next([=](Action action) {
		switch (action) {
		case Action::Close: hide(); break;
		case Action::PromotePremium: break;
		case Action::Pause: setPausedInside(true); break;
		case Action::Unpause: setPausedInside(false); break;
		}
	}, raw->lifetime());

	raw->fadeIn();
}

void PlaybackSponsored::hide() {
	_widget->fadeOut([this, raw = _widget.get()] {
		if (_widget.get() == raw) {
			_widget = nullptr;
		}
	});
}

void PlaybackSponsored::saveState() {
	_session->sponsoredMessages().updateForVideo(
		_itemId,
		computeState().data);
}

PlaybackSponsored::State PlaybackSponsored::computeState() const {
	auto result = State{ crl::now() };
	if (!_data) {
		return result;
	}
	result.data = _data->state;
	if (!_start) {
		return result;
	}
	const auto elapsed = result.now - _start;
	result.data.leftTillShow -= elapsed;
	return result;
}

rpl::lifetime &PlaybackSponsored::lifetime() {
	return _lifetime;
}

bool PlaybackSponsored::Has(HistoryItem *item) {
	return item
		&& item->history()->session().sponsoredMessages().canHaveFor(item);
}

} // namespace Media::View
