/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/peers/translate_bot_info_box.h"

#include "base/timer.h"
#include "data/data_bot_info_localization.h"
#include "lang/lang_keys.h"
#include "ui/effects/radial_animation.h"
#include "ui/layers/generic_box.h"
#include "ui/text/format_values.h"
#include "ui/widgets/labels.h"
#include "ui/painter.h"
#include "ui/rect.h"

#include "styles/style_edit_peer_members.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"

namespace {

using Job = Data::BotInfoLocalization::Job;
using JobState = Data::BotInfoLocalization::State;

constexpr auto kCountdownStep = crl::time(500);

class Radial final : public Ui::RpWidget {
public:
	explicit Radial(QWidget *parent);

	void setProgress(float64 progress);
	void stop();

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	Ui::RadialAnimation _animation;
	float64 _progress = 0.;

};

Radial::Radial(QWidget *parent)
: RpWidget(parent)
, _animation([=](crl::time now) {
	if (_animation.update(_progress, false, now) || !anim::Disabled()) {
		update();
	}
}) {
	resize(Size(st::botInfoTranslateRadialSize));
}

void Radial::setProgress(float64 progress) {
	_progress = progress;
	if (!_animation.animating()) {
		_animation.start(_progress);
	}
}

void Radial::stop() {
	_animation.stop();
	update();
}

void Radial::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);

	const auto stroke = st::botInfoTranslateRadialStroke;
	const auto side = st::botInfoTranslateRadialSize;
	const auto outer = QRect((width() - side) / 2, 0, side, side);
	const auto inner = QRectF(outer) - Margins(stroke / 2.);
	{
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(QPen(st::windowBgOver, stroke));
		p.setBrush(Qt::NoBrush);
		p.drawEllipse(inner);
	}
	st::menuIconTranslate.paintInCenter(p, outer);
	if (_animation.animating()) {
		_animation.draw(p, inner, stroke, st::windowBgActive);
	}
}

[[nodiscard]] QString StatusText(const JobState &state) {
	if (!state.stopped.isEmpty()) {
		return tr::lng_bot_languages_stopped(tr::now);
	} else if (state.pausedTill) {
		const auto left = std::max(
			state.pausedTill - crl::now(),
			crl::time(0));
		return tr::lng_bot_languages_paused(
			tr::now,
			lt_time,
			Ui::FormatDurationText((left + 999) / 1000));
	}
	return tr::lng_bot_languages_translating(
		tr::now,
		lt_done,
		QString::number(state.done),
		lt_total,
		QString::number(state.total));
}

} // namespace

void TranslateBotInfoBox(
		not_null<Ui::GenericBox*> box,
		not_null<Data::BotInfoLocalization*> localization,
		std::vector<QString> codes,
		Fn<void(int translated)> done) {
	struct State {
		rpl::variable<QString> status;
		base::Timer countdown;
		bool finished = false;
	};
	const auto draft = &localization->draft();
	const auto state = box->lifetime().make_state<State>();
	const auto container = box->verticalLayout();

	box->setTitle(tr::lng_bot_languages_translating_title());
	box->setWidth(st::boxWidth);
	box->setCloseByOutsideClick(false);

	const auto radial = container->add(
		object_ptr<Radial>(container),
		st::botInfoTranslateRadialPadding,
		style::al_top);
	container->add(
		object_ptr<Ui::FlatLabel>(
			container,
			state->status.value(),
			st::botInfoTranslateStatus),
		st::botInfoTranslateStatusPadding,
		style::al_top);
	container->add(
		object_ptr<Ui::FlatLabel>(
			container,
			tr::lng_bot_languages_translate_about(),
			st::botInfoTranslateAbout),
		st::botInfoTranslateAboutPadding,
		style::al_top);

	const auto finish = [=] {
		if (std::exchange(state->finished, true)) {
			return;
		}
		const auto translated = ranges::count_if(codes, [&](
				const QString &code) {
			const auto language = draft->find(code);
			return language && language->hasTranslated();
		});
		done(int(translated));
		box->closeBox();
	};
	const auto stop = [=] {
		if (localization->state().job == Job::Translate) {
			localization->cancel();
		} else {
			finish();
		}
	};
	const auto updateStatus = [=] {
		state->status = StatusText(localization->state());
	};
	state->countdown.setCallback(updateStatus);

	for (const auto &code : codes) {
		draft->add(code);
	}
	localization->translate({
		.codes = codes,
		.fields = { begin(Data::kBotInfoFields), end(Data::kBotInfoFields) },
	});
	localization->stateValue(
	) | rpl::on_next([=](const JobState &now) {
		if (state->finished) {
			return;
		} else if ((now.job != Job::Translate) && now.stopped.isEmpty()) {
			finish();
			return;
		}
		updateStatus();
		if (now.pausedTill) {
			state->countdown.callEach(kCountdownStep);
		} else {
			state->countdown.cancel();
		}
		if (now.job == Job::Translate) {
			radial->setProgress(now.done / float64(now.total));
		} else {
			radial->stop();
			box->clearButtons();
			box->addButton(tr::lng_close(), stop);
		}
	}, box->lifetime());
	box->addButton(tr::lng_bot_languages_stop(), stop);
	box->lifetime().add([=] {
		const auto working = (localization->state().job == Job::Translate);
		if (!std::exchange(state->finished, true) && working) {
			localization->cancel();
		}
	});
}
