/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/stories/media_stories_stealth.h"

#include "base/timer_rpl.h"
#include "base/unixtime.h"
#include "boxes/premium_preview_box.h"
#include "chat_helpers/compose/compose_show.h"
#include "data/data_peer_values.h"
#include "data/data_session.h"
#include "data/data_stories.h"
#include "info/profile/info_profile_icon.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "settings/settings_premium.h"
#include "ui/layers/generic_box.h"
#include "ui/text/text_utilities.h"
#include "ui/toast/toast.h"
#include "ui/widgets/buttons.h"
#include "ui/painter.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "styles/style_media_view.h"
#include "styles/style_layers.h"

namespace Media::Stories {
namespace {

constexpr auto kAlreadyToastDuration = 4 * crl::time(1000);
constexpr auto kCooldownButtonLabelOpacity = 0.5;

struct State {
	Data::StealthMode mode;
	TimeId now = 0;
	bool premium = false;
};

struct Feature {
	const style::icon &icon;
	QString title;
	TextWithEntities about;
};

[[nodiscard]] Ui::Toast::Config ToastAlready(TimeId left) {
	return {
		.title = tr::lng_stealth_mode_already_title(tr::now),
		.text = tr::lng_stealth_mode_already_about(
			tr::now,
			lt_left,
			TextWithEntities{ TimeLeftText(left) },
			Ui::Text::RichLangValue),
		.st = &st::storiesStealthToast,
		.adaptive = true,
		.duration = kAlreadyToastDuration,
	};
}

[[nodiscard]] Ui::Toast::Config ToastActivated() {
	return {
		.title = tr::lng_stealth_mode_enabled_tip_title(tr::now),
		.text = tr::lng_stealth_mode_enabled_tip(
			tr::now,
			Ui::Text::RichLangValue),
		.st = &st::storiesStealthToast,
		.adaptive = true,
		.duration = kAlreadyToastDuration,
	};
}

[[nodiscard]] Ui::Toast::Config ToastCooldown() {
	return {
		.text = tr::lng_stealth_mode_cooldown_tip(
			tr::now,
			Ui::Text::RichLangValue),
		.st = &st::storiesStealthToast,
		.adaptive = true,
		.duration = kAlreadyToastDuration,
	};
}

[[nodiscard]] rpl::producer<State> StateValue(
		not_null<Main::Session*> session) {
	return rpl::combine(
		session->data().stories().stealthModeValue(),
		Data::AmPremiumValue(session)
	) | rpl::map([](Data::StealthMode mode, bool premium) {
		return rpl::make_producer<State>([=](auto consumer) {
			struct Info {
				base::Timer timer;
				bool firstSent = false;
				bool enabledSent = false;
				bool cooldownSent = false;
			};
			auto lifetime = rpl::lifetime();
			const auto info = lifetime.make_state<Info>();
			const auto check = [=] {
				auto send = !info->firstSent;
				const auto now = base::unixtime::now();
				const auto left1 = (mode.enabledTill - now);
				const auto left2 = (mode.cooldownTill - now);
				info->firstSent = true;
				if (!info->enabledSent && left1 <= 0) {
					send = true;
					info->enabledSent = true;
				}
				if (!info->cooldownSent && left2 <= 0) {
					send = true;
					info->cooldownSent = true;
				}
				const auto left = (left1 <= 0)
					? left2
					: (left2 <= 0)
					? left1
					: std::min(left1, left2);
				if (left > 0) {
					info->timer.callOnce(left * crl::time(1000));
				}
				if (send) {
					consumer.put_next(State{ mode, now, premium });
				}
				if (left <= 0) {
					consumer.put_done();
				}
			};
			info->timer.setCallback(check);
			check();
			return lifetime;
		});
	}) | rpl::flatten_latest();
}

[[nodiscard]] Feature FeaturePast() {
	return {
		.icon = st::storiesStealthFeaturePastIcon,
		.title = tr::lng_stealth_mode_past_title(tr::now),
		.about = { tr::lng_stealth_mode_past_about(tr::now) },
	};
}

[[nodiscard]] Feature FeatureNext() {
	return {
		.icon = st::storiesStealthFeatureNextIcon,
		.title = tr::lng_stealth_mode_next_title(tr::now),
		.about = { tr::lng_stealth_mode_next_about(tr::now) },
	};
}

[[nodiscard]] object_ptr<Ui::RpWidget> MakeLogo(QWidget *parent) {
	const auto add = st::storiesStealthLogoAdd;
	const auto icon = &st::storiesStealthLogoIcon;
	const auto size = QSize(2 * add, 2 * add) + icon->size();
	auto result = object_ptr<Ui::PaddingWrap<Ui::RpWidget>>(
		parent,
		object_ptr<Ui::RpWidget>(parent),
		st::storiesStealthLogoMargin);
	const auto inner = result->entity();
	inner->resize(size);
	inner->paintRequest(
	) | rpl::start_with_next([=] {
		auto p = QPainter(inner);
		auto hq = PainterHighQualityEnabler(p);
		p.setBrush(st::storiesComposeBlue);
		p.setPen(Qt::NoPen);
		const auto left = (inner->width() - size.width()) / 2;
		const auto top = (inner->height() - size.height()) / 2;
		const auto rect = QRect(QPoint(left, top), size);
		p.drawEllipse(rect);
		icon->paintInCenter(p, rect);
	}, inner->lifetime());
	return result;
}

[[nodiscard]] object_ptr<Ui::RpWidget> MakeTitle(QWidget *parent) {
	return object_ptr<Ui::PaddingWrap<Ui::FlatLabel>>(
		parent,
		object_ptr<Ui::FlatLabel>(
			parent,
			tr::lng_stealth_mode_title(tr::now),
			st::storiesStealthBox.title),
		st::storiesStealthTitleMargin);
}

[[nodiscard]] object_ptr<Ui::RpWidget> MakeAbout(
		QWidget *parent,
		rpl::producer<State> state) {
	auto text = std::move(state) | rpl::map([](const State &state) {
		return state.premium
			? tr::lng_stealth_mode_about(tr::now)
			: tr::lng_stealth_mode_unlock_about(tr::now);
	});
	return object_ptr<Ui::PaddingWrap<Ui::FlatLabel>>(
		parent,
		object_ptr<Ui::FlatLabel>(
			parent,
			std::move(text),
			st::storiesStealthAbout),
		st::storiesStealthAboutMargin);
}

[[nodiscard]] object_ptr<Ui::RpWidget> MakeFeature(
		QWidget *parent,
		Feature feature) {
	auto result = object_ptr<Ui::PaddingWrap<>>(
		parent,
		object_ptr<Ui::RpWidget>(parent),
		st::storiesStealthFeatureMargin);
	const auto widget = result->entity();
	const auto icon = Ui::CreateChild<Info::Profile::FloatingIcon>(
		widget,
		feature.icon,
		st::storiesStealthFeatureIconPosition);
	const auto title = Ui::CreateChild<Ui::FlatLabel>(
		widget,
		feature.title,
		st::storiesStealthFeatureTitle);
	const auto about = Ui::CreateChild<Ui::FlatLabel>(
		widget,
		rpl::single(feature.about),
		st::storiesStealthFeatureAbout);
	icon->show();
	title->show();
	about->show();
	widget->widthValue(
	) | rpl::start_with_next([=](int width) {
		const auto left = st::storiesStealthFeatureLabelLeft;
		const auto available = width - left;
		title->resizeToWidth(available);
		about->resizeToWidth(available);
		auto top = 0;
		title->move(left, top);
		top += title->height() + st::storiesStealthFeatureSkip;
		about->move(left, top);
		top += about->height();
		widget->resize(width, top);
	}, widget->lifetime());
	return result;
}

[[nodiscard]] object_ptr<Ui::RoundButton> MakeButton(
		QWidget *parent,
		rpl::producer<State> state) {
	auto text = rpl::duplicate(state) | rpl::map([](const State &state) {
		if (!state.premium) {
			return tr::lng_stealth_mode_unlock();
		} else if (state.mode.cooldownTill <= state.now) {
			return tr::lng_stealth_mode_enable();
		}
		return rpl::single(
			rpl::empty
		) | rpl::then(
			base::timer_each(250)
		) | rpl::map([=] {
			const auto now = base::unixtime::now();
			const auto left = std::max(state.mode.cooldownTill - now, 1);
			return tr::lng_stealth_mode_cooldown_in(
				tr::now,
				lt_left,
				TimeLeftText(left));
		}) | rpl::type_erased();
	}) | rpl::flatten_latest();

	auto result = object_ptr<Ui::RoundButton>(
		parent,
		rpl::single(QString()),
		st::storiesStealthBox.button);
	const auto raw = result.data();

	const auto label = Ui::CreateChild<Ui::FlatLabel>(
		raw,
		std::move(text),
		st::storiesStealthButtonLabel);
	label->setAttribute(Qt::WA_TransparentForMouseEvents);
	label->show();

	const auto lock = Ui::CreateChild<Ui::RpWidget>(raw);
	lock->setAttribute(Qt::WA_TransparentForMouseEvents);
	lock->resize(st::storiesStealthLockIcon.size());
	lock->paintRequest(
	) | rpl::start_with_next([=] {
		auto p = QPainter(lock);
		st::storiesStealthLockIcon.paintInCenter(p, lock->rect());
	}, lock->lifetime());

	const auto lockLeft = -st::storiesStealthButtonLabel.style.font->height;
	const auto updateLabelLockGeometry = [=] {
		const auto outer = raw->width();
		const auto added = -st::storiesStealthBox.button.width;
		const auto skip = lock->isHidden() ? 0 : (lockLeft + lock->width());
		const auto width = outer - added - skip;
		const auto top = st::storiesStealthBox.button.textTop;
		label->resizeToWidth(width);
		label->move(added / 2, top);
		const auto inner = std::min(label->textMaxWidth(), width);
		const auto right = (added / 2) + (outer - inner) / 2 + inner;
		const auto lockTop = (label->height() - lock->height()) / 2;
		lock->move(right + lockLeft, top + lockTop);
	};

	std::move(state) | rpl::start_with_next([=](const State &state) {
		const auto cooldown = state.premium
			&& (state.mode.cooldownTill > state.now);
		label->setOpacity(cooldown ? kCooldownButtonLabelOpacity : 1.);
		lock->setVisible(!state.premium);
		updateLabelLockGeometry();
	}, label->lifetime());

	raw->widthValue(
	) | rpl::start_with_next(updateLabelLockGeometry, label->lifetime());

	return result;
}

[[nodiscard]] object_ptr<Ui::BoxContent> StealthModeBox(
		std::shared_ptr<ChatHelpers::Show> show) {
	return Box([=](not_null<Ui::GenericBox*> box) {
		struct Data {
			rpl::variable<State> state;
			bool requested = false;
		};
		const auto data = box->lifetime().make_state<Data>();
		data->state = StateValue(&show->session());
		box->setWidth(st::boxWideWidth);
		box->setStyle(st::storiesStealthBox);
		box->addRow(MakeLogo(box));
		box->addRow(MakeTitle(box));
		box->addRow(MakeAbout(box, data->state.value()));
		box->addRow(MakeFeature(box, FeaturePast()));
		box->addRow(
			MakeFeature(box, FeatureNext()),
			(st::boxRowPadding
				+ QMargins(0, 0, 0, st::storiesStealthBoxBottom)));
		box->setNoContentMargin(true);
		box->addTopButton(st::storiesStealthBoxClose, [=] {
			box->closeBox();
		});
		const auto button = box->addButton(
			MakeButton(box, data->state.value()));
		button->resizeToWidth(st::boxWideWidth
			- st::storiesStealthBox.buttonPadding.left()
			- st::storiesStealthBox.buttonPadding.right());
		button->setClickedCallback([=] {
			const auto now = data->state.current();
			if (now.mode.enabledTill > now.now) {
				show->showToast(ToastActivated());
				box->closeBox();
			} else if (!now.premium) {
				data->requested = false;
				if (const auto window = show->resolveWindow()) {
					ShowPremiumPreviewBox(window, PremiumFeature::Stories);
					window->window().activate();
				}
			} else if (now.mode.cooldownTill > now.now) {
				show->showToast(ToastCooldown());
				box->closeBox();
			} else if (!data->requested) {
				data->requested = true;
				show->session().data().stories().activateStealthMode(
					crl::guard(box, [=] { data->requested = false; }));
			}
		});
		data->state.value() | rpl::filter([](const State &state) {
			return state.mode.enabledTill > state.now;
		}) | rpl::start_with_next([=] {
			box->closeBox();
			show->showToast(ToastActivated());
		}, box->lifetime());
	});
}

} // namespace

void SetupStealthMode(std::shared_ptr<ChatHelpers::Show> show) {
	const auto now = base::unixtime::now();
	const auto mode = show->session().data().stories().stealthMode();
	if (const auto left = mode.enabledTill - now; left > 0) {
		show->showToast(ToastAlready(left));
	} else {
		show->show(StealthModeBox(show));
	}
}

QString TimeLeftText(int left) {
	Expects(left >= 0);

	const auto hours = left / 3600;
	const auto minutes = (left % 3600) / 60;
	const auto seconds = left % 60;
	const auto zero = QChar('0');
	if (hours) {
		return u"%1:%2:%3"_q
			.arg(hours)
			.arg(minutes, 2, 10, zero)
			.arg(seconds, 2, 10, zero);
	} else if (minutes) {
		return u"%1:%2"_q.arg(minutes).arg(seconds, 2, 10, zero);
	}
	return u"0:%1"_q.arg(left, 2, 10, zero);
}

} // namespace Media::Stories
