/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/business/settings_working_hours.h"

#include "base/event_filter.h"
#include "base/unixtime.h"
#include "core/application.h"
#include "data/business/data_business_info.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "settings/business/settings_recipients_helper.h"
#include "ui/layers/generic_box.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/vertical_drum_picker.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/vertical_list.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"

namespace Settings {
namespace {

constexpr auto kDay = Data::WorkingInterval::kDay;
constexpr auto kWeek = Data::WorkingInterval::kWeek;
constexpr auto kInNextDayMax = Data::WorkingInterval::kInNextDayMax;

class WorkingHours : public BusinessSection<WorkingHours> {
public:
	WorkingHours(
		QWidget *parent,
		not_null<Window::SessionController*> controller);
	~WorkingHours();

	[[nodiscard]] bool closeByOutsideClick() const override;
	[[nodiscard]] rpl::producer<QString> title() override;

private:
	void setupContent(not_null<Window::SessionController*> controller);
	void save();

	rpl::variable<Data::WorkingHours> _hours;
	rpl::variable<bool> _enabled;

};

[[nodiscard]] QString TimezoneFullName(const Data::Timezone &data) {
	const auto abs = std::abs(data.utcOffset);
	const auto hours = abs / 3600;
	const auto minutes = (abs % 3600) / 60;
	const auto sign = (data.utcOffset < 0) ? '-' : '+';
	const auto prefix = u"(UTC"_q
		+ sign
		+ QString::number(hours)
		+ u":"_q
		+ QString::number(minutes).rightJustified(2, u'0')
		+ u")"_q;
	return prefix + ' ' + data.name;
}

[[nodiscard]] QString FormatDayTime(
		TimeId time,
		bool showEndAsNextDay = false) {
	const auto wrap = [](TimeId value) {
		const auto hours = value / 3600;
		const auto minutes = (value % 3600) / 60;
		return QString::number(hours).rightJustified(2, u'0')
			+ ':'
			+ QString::number(minutes).rightJustified(2, u'0');
	};
	return (time > kDay || (showEndAsNextDay && time == kDay))
		? tr::lng_hours_next_day(tr::now, lt_time, wrap(time - kDay))
		: wrap(time == kDay ? 0 : time);
}

[[nodiscard]] QString FormatTimeHour(TimeId time) {
	const auto wrap = [](TimeId value) {
		return QString::number(value / 3600).rightJustified(2, u'0');
	};
	if (time < kDay) {
		return wrap(time);
	}
	const auto wrapped = wrap(time - kDay);
	const auto result = tr::lng_hours_on_next_day(tr::now, lt_time, wrapped);
	const auto i = result.indexOf(wrapped);
	return (i >= 0) ? (result.left(i) + wrapped) : result;
}

[[nodiscard]] QString FormatTimeMinute(TimeId time) {
	const auto wrap = [](TimeId value) {
		return QString::number((value / 60) % 60).rightJustified(2, u'0');
	};
	if (time < kDay) {
		return wrap(time);
	}
	const auto wrapped = wrap(time - kDay);
	const auto result = tr::lng_hours_on_next_day(tr::now, lt_time, wrapped);
	const auto i = result.indexOf(wrapped);
	return (i >= 0)
		? (wrapped + result.right(result.size() - i - wrapped.size()))
		: result;
}

[[nodiscard]] QString JoinIntervals(const Data::WorkingIntervals &data) {
	auto result = QStringList();
	result.reserve(data.list.size());
	for (const auto &interval : data.list) {
		const auto start = FormatDayTime(interval.start);
		const auto end = FormatDayTime(interval.end);
		result.push_back(start + u" - "_q + end);
	}
	return result.join(u", "_q);
}

void EditTimeBox(
		not_null<Ui::GenericBox*> box,
		TimeId low,
		TimeId high,
		TimeId value,
		Fn<void(TimeId)> save) {
	Expects(low <= high);

	const auto content = box->addRow(object_ptr<Ui::FixedHeightWidget>(
		box,
		st::settingsWorkingHoursPicker));

	const auto font = st::boxTextFont;
	const auto itemHeight = st::settingsWorkingHoursPickerItemHeight;
	const auto picker = [=](
			int count,
			int startIndex,
			Fn<void(QPainter &p, QRectF rect, int index)> paint) {
		auto paintCallback = [=](
				QPainter &p,
				int index,
				float64 y,
				float64 distanceFromCenter,
				int outerWidth) {
			const auto r = QRectF(0, y, outerWidth, itemHeight);
			const auto progress = std::abs(distanceFromCenter);
			const auto revProgress = 1. - progress;
			p.save();
			p.translate(r.center());
			constexpr auto kMinYScale = 0.2;
			const auto yScale = kMinYScale
				+ (1. - kMinYScale) * anim::easeOutCubic(1., revProgress);
			p.scale(1., yScale);
			p.translate(-r.center());
			p.setOpacity(revProgress);
			p.setFont(font);
			p.setPen(st::defaultFlatLabel.textFg);
			paint(p, r, index);
			p.restore();
		};
		return Ui::CreateChild<Ui::VerticalDrumPicker>(
			content,
			std::move(paintCallback),
			count,
			itemHeight,
			startIndex);
	};

	const auto hoursCount = (high - low + 3600) / 3600;
	const auto hoursStartIndex = (value / 3600) - (low / 3600);
	const auto hoursPaint = [=](QPainter &p, QRectF rect, int index) {
		p.drawText(
			rect,
			FormatTimeHour(((low / 3600) + index) * 3600),
			style::al_right);
	};
	const auto hours = picker(hoursCount, hoursStartIndex, hoursPaint);
	const auto minutes = content->lifetime().make_state<
		rpl::variable<Ui::VerticalDrumPicker*>
	>(nullptr);

	// hours->value() is valid only after size is set.
	const auto separator = u":"_q;
	const auto separatorWidth = st::boxTextFont->width(separator);
	rpl::combine(
		content->sizeValue(),
		minutes->value()
	) | rpl::start_with_next([=](QSize s, Ui::VerticalDrumPicker *minutes) {
		const auto half = (s.width() - separatorWidth) / 2;
		hours->setGeometry(0, 0, half, s.height());
		if (minutes) {
			minutes->setGeometry(half + separatorWidth, 0, half, s.height());
		}
	}, content->lifetime());

	Ui::SendPendingMoveResizeEvents(hours);

	const auto minutesStart = content->lifetime().make_state<TimeId>();
	hours->value() | rpl::start_with_next([=](int hoursIndex) {
		const auto start = std::max(low, (hoursIndex + (low / 3600)) * 3600);
		const auto end = std::min(high, ((start / 3600) * 60 + 59) * 60);
		const auto minutesCount = (end - start + 60) / 60;
		const auto minutesStartIndex = minutes->current()
			? std::clamp(
				((((*minutesStart) / 60 + minutes->current()->index()) % 60)
					- ((start / 60) % 60)),
				0,
				(minutesCount - 1))
			: std::clamp((value / 60) - (start / 60), 0, minutesCount - 1);
		*minutesStart = start;

		const auto minutesPaint = [=](QPainter &p, QRectF rect, int index) {
			p.drawText(
				rect,
				FormatTimeMinute(((start / 60) + index) * 60),
				style::al_left);
		};
		const auto updated = picker(
			minutesCount,
			minutesStartIndex,
			minutesPaint);
		delete minutes->current();
		*minutes = updated;
		minutes->current()->show();
	}, hours->lifetime());

	content->paintRequest(
	) | rpl::start_with_next([=](const QRect &r) {
		auto p = QPainter(content);

		p.fillRect(r, Qt::transparent);

		const auto lineRect = QRect(
			0,
			content->height() / 2,
			content->width(),
			st::defaultInputField.borderActive);
		p.fillRect(lineRect.translated(0, itemHeight / 2), st::activeLineFg);
		p.fillRect(lineRect.translated(0, -itemHeight / 2), st::activeLineFg);
		p.drawText(QRectF(content->rect()), separator, style::al_center);
	}, content->lifetime());

	base::install_event_filter(box, [=](not_null<QEvent*> e) {
		if (e->type() == QEvent::KeyPress) {
			hours->handleKeyEvent(static_cast<QKeyEvent*>(e.get()));
		}
		return base::EventFilterResult::Continue;
	});

	box->addButton(tr::lng_settings_save(), [=] {
		const auto weak = Ui::MakeWeak(box);
		save(std::clamp(
			((*minutesStart) / 60 + minutes->current()->index()) * 60,
			low,
			high));
		if (const auto strong = weak.data()) {
			strong->closeBox();
		}
	});
	box->addButton(tr::lng_cancel(), [=] {
		box->closeBox();
	});
}

void EditDayBox(
		not_null<Ui::GenericBox*> box,
		rpl::producer<QString> title,
		Data::WorkingIntervals intervals,
		Fn<void(Data::WorkingIntervals)> save) {
	box->setTitle(std::move(title));
	box->setWidth(st::boxWideWidth);
	struct State {
		rpl::variable<Data::WorkingIntervals> data;
	};
	const auto state = box->lifetime().make_state<State>(State{
		.data = std::move(intervals),
	});

	const auto container = box->verticalLayout();
	const auto rows = container->add(
		object_ptr<Ui::VerticalLayout>(container));
	const auto makeRow = [=](
			Data::WorkingInterval interval,
			TimeId min,
			TimeId max) {
		auto result = object_ptr<Ui::VerticalLayout>(rows);
		const auto raw = result.data();
		AddDivider(raw);
		AddSkip(raw);
		AddButtonWithLabel(
			raw,
			tr::lng_hours_opening(),
			rpl::single(FormatDayTime(interval.start, true)),
			st::settingsButtonNoIcon
		)->setClickedCallback([=] {
			const auto max = std::max(min, interval.end - 60);
			const auto now = std::clamp(interval.start, min, max);
			const auto save = crl::guard(box, [=](TimeId value) {
				auto now = state->data.current();
				const auto i = ranges::find(now.list, interval);
				if (i != end(now.list)) {
					i->start = value;
					state->data = now.normalized();
				}
			});
			box->getDelegate()->show(Box(EditTimeBox, min, max, now, save));
		});
		AddButtonWithLabel(
			raw,
			tr::lng_hours_closing(),
			rpl::single(FormatDayTime(interval.end, true)),
			st::settingsButtonNoIcon
		)->setClickedCallback([=] {
			const auto min = std::min(max, interval.start + 60);
			const auto now = std::clamp(interval.end, min, max);
			const auto save = crl::guard(box, [=](TimeId value) {
				auto now = state->data.current();
				const auto i = ranges::find(now.list, interval);
				if (i != end(now.list)) {
					i->end = value;
					state->data = now.normalized();
				}
			});
			box->getDelegate()->show(Box(EditTimeBox, min, max, now, save));
		});
		raw->add(object_ptr<Ui::SettingsButton>(
			raw,
			tr::lng_hours_remove(),
			st::settingsAttentionButton
		))->setClickedCallback([=] {
			auto now = state->data.current();
			const auto i = ranges::find(now.list, interval);
			if (i != end(now.list)) {
				now.list.erase(i);
				state->data = std::move(now);
			}
		});
		AddSkip(raw);

		return result;
	};

	const auto addWrap = container->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			container,
			object_ptr<Ui::VerticalLayout>(container)));
	AddDivider(addWrap->entity());
	AddSkip(addWrap->entity());
	const auto add = addWrap->entity()->add(
		object_ptr<Ui::SettingsButton>(
			container,
			tr::lng_hours_add_button(),
			st::settingsButtonLightNoIcon));
	add->setClickedCallback([=] {
		auto now = state->data.current();
		if (now.list.empty()) {
			now.list.push_back({ 8 * 3600, 20 * 3600 });
		} else if (const auto last = now.list.back().end; last + 60 < kDay) {
			const auto from = std::max(
				std::min(last + 30 * 60, kDay - 30 * 60),
				last + 60);
			now.list.push_back({ from, from + 4 * 3600 });
		}
		state->data = std::move(now);
	});

	state->data.value(
	) | rpl::start_with_next([=](const Data::WorkingIntervals &data) {
		const auto count = int(data.list.size());
		for (auto i = 0; i != count; ++i) {
			const auto min = (i == 0) ? 0 : (data.list[i - 1].end + 60);
			const auto max = (i == count - 1)
				? (kDay + kInNextDayMax)
				: (data.list[i + 1].start - 60);
			rows->insert(i, makeRow(data.list[i], min, max));
			if (rows->count() > i + 1) {
				delete rows->widgetAt(i + 1);
			}
		}
		while (rows->count() > count) {
			delete rows->widgetAt(count);
		}
		rows->resizeToWidth(st::boxWideWidth);
		addWrap->toggle(data.list.empty()
			|| data.list.back().end + 60 < kDay, anim::type::instant);
		add->clearState();
	}, add->lifetime());
	addWrap->finishAnimating();

	AddSkip(container);
	AddDividerText(container, tr::lng_hours_about_day());

	box->addButton(tr::lng_settings_save(), [=] {
		const auto weak = Ui::MakeWeak(box);
		save(state->data.current());
		if (const auto strong = weak.data()) {
			strong->closeBox();
		}
	});
	box->addButton(tr::lng_cancel(), [=] {
		box->closeBox();
	});
}

void ChooseTimezoneBox(
		not_null<Ui::GenericBox*> box,
		std::vector<Data::Timezone> list,
		QString id,
		Fn<void(QString)> save) {
	Expects(!list.empty());
	box->setWidth(st::boxWideWidth);
	box->setTitle(tr::lng_hours_time_zone_title());

	const auto height = st::boxWideWidth;
	box->setMaxHeight(height);

	ranges::sort(list, ranges::less(), [](const Data::Timezone &value) {
		return std::pair(value.utcOffset, value.name);
	});

	if (!ranges::contains(list, id, &Data::Timezone::id)) {
		id = Data::FindClosestTimezoneId(list);
	}
	const auto i = ranges::find(list, id, &Data::Timezone::id);
	const auto value = int(i - begin(list));
	const auto group = std::make_shared<Ui::RadiobuttonGroup>(value);
	const auto radioPadding = st::defaultCheckbox.margin;
	const auto max = std::max(radioPadding.top(), radioPadding.bottom());
	auto index = 0;
	auto padding = st::boxRowPadding + QMargins(0, max, 0, max);
	auto selected = (Ui::Radiobutton*)nullptr;
	for (const auto &entry : list) {
		const auto button = box->addRow(
			object_ptr<Ui::Radiobutton>(
				box,
				group,
				index++,
				TimezoneFullName(entry)),
			padding);
		if (index == value + 1) {
			selected = button;
		}
		padding = st::boxRowPadding + QMargins(0, 0, 0, max);
	}
	if (selected) {
		box->verticalLayout()->resizeToWidth(st::boxWideWidth);
		const auto y = selected->y() - (height - selected->height()) / 2;
		box->setInitScrollCallback([=] {
			box->scrollToY(y);
		});
	}
	group->setChangedCallback([=](int index) {
		const auto weak = Ui::MakeWeak(box);
		save(list[index].id);
		if (const auto strong = weak.data()) {
			strong->closeBox();
		}
	});
	box->addButton(tr::lng_close(), [=] {
		box->closeBox();
	});
}

void AddWeekButton(
		not_null<Ui::VerticalLayout*> container,
		not_null<Window::SessionController*> controller,
		int index,
		not_null<rpl::variable<Data::WorkingHours>*> data) {
	auto label = [&] {
		switch (index) {
		case 0: return tr::lng_hours_monday();
		case 1: return tr::lng_hours_tuesday();
		case 2: return tr::lng_hours_wednesday();
		case 3: return tr::lng_hours_thursday();
		case 4: return tr::lng_hours_friday();
		case 5: return tr::lng_hours_saturday();
		case 6: return tr::lng_hours_sunday();
		}
		Unexpected("Index in AddWeekButton.");
	}();
	const auto &st = st::settingsWorkingHoursWeek;
	const auto button = AddButtonWithIcon(
		container,
		rpl::duplicate(label),
		st);
	button->setClickedCallback([=] {
		const auto done = [=](Data::WorkingIntervals intervals) {
			auto now = data->current();
			now.intervals = ReplaceDayIntervals(
				now.intervals,
				index,
				std::move(intervals));
			*data = now.normalized();
		};
		controller->show(Box(
			EditDayBox,
			rpl::duplicate(label),
			ExtractDayIntervals(data->current().intervals, index),
			crl::guard(button, done)));
	});

	const auto toggleButton = Ui::CreateChild<Ui::SettingsButton>(
		container.get(),
		nullptr,
		st);
	const auto checkView = button->lifetime().make_state<Ui::ToggleView>(
		st.toggle,
		false,
		[=] { toggleButton->update(); });

	auto status = data->value(
	) | rpl::map([=](const Data::WorkingHours &data) -> rpl::producer<QString> {
		using namespace Data;

		const auto intervals = ExtractDayIntervals(data.intervals, index);
		const auto empty = intervals.list.empty();
		if (checkView->checked() == empty) {
			checkView->setChecked(!empty, anim::type::instant);
		}
		if (!intervals) {
			return tr::lng_hours_closed();
		} else if (IsFullOpen(intervals)) {
			return tr::lng_hours_open_full();
		}
		return rpl::single(JoinIntervals(intervals));
	}) | rpl::flatten_latest();
	const auto details = Ui::CreateChild<Ui::FlatLabel>(
		button.get(),
		std::move(status),
		st::settingsWorkingHoursDetails);
	details->show();
	details->moveToLeft(
		st.padding.left(),
		st.padding.top() + st.height - details->height());
	details->setAttribute(Qt::WA_TransparentForMouseEvents);

	const auto separator = Ui::CreateChild<Ui::RpWidget>(container.get());
	separator->paintRequest(
	) | rpl::start_with_next([=, bg = st.textBgOver] {
		auto p = QPainter(separator);
		p.fillRect(separator->rect(), bg);
	}, separator->lifetime());
	const auto separatorHeight = st.height - 2 * st.toggle.border;
	button->geometryValue(
	) | rpl::start_with_next([=](const QRect &r) {
		const auto w = st::rightsButtonToggleWidth;
		toggleButton->setGeometry(
			r.x() + r.width() - w,
			r.y(),
			w,
			r.height());
		separator->setGeometry(
			toggleButton->x() - st::lineWidth,
			r.y() + (r.height() - separatorHeight) / 2,
			st::lineWidth,
			separatorHeight);
	}, toggleButton->lifetime());

	const auto checkWidget = Ui::CreateChild<Ui::RpWidget>(toggleButton);
	checkWidget->resize(checkView->getSize());
	checkWidget->paintRequest(
	) | rpl::start_with_next([=] {
		auto p = QPainter(checkWidget);
		checkView->paint(p, 0, 0, checkWidget->width());
	}, checkWidget->lifetime());
	toggleButton->sizeValue(
	) | rpl::start_with_next([=](const QSize &s) {
		checkWidget->moveToRight(
			st.toggleSkip,
			(s.height() - checkWidget->height()) / 2);
	}, toggleButton->lifetime());

	toggleButton->setClickedCallback([=] {
		const auto enabled = !checkView->checked();
		checkView->setChecked(enabled, anim::type::normal);
		auto now = data->current();
		now.intervals = ReplaceDayIntervals(
			now.intervals,
			index,
			(enabled
				? Data::WorkingIntervals{ { { 0, kDay } } }
				: Data::WorkingIntervals()));
		*data = now.normalized();
	});
}

WorkingHours::WorkingHours(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: BusinessSection(parent, controller) {
	setupContent(controller);
}

WorkingHours::~WorkingHours() {
	if (!Core::Quitting()) {
		save();
	}
}

bool WorkingHours::closeByOutsideClick() const {
	return false;
}

rpl::producer<QString> WorkingHours::title() {
	return tr::lng_hours_title();
}

void WorkingHours::setupContent(
		not_null<Window::SessionController*> controller) {
	using namespace rpl::mappers;

	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

	struct State {
		rpl::variable<Data::Timezones> timezones;
		bool timezoneEditPending = false;
	};
	const auto info = &controller->session().data().businessInfo();
	const auto state = content->lifetime().make_state<State>(State{
		.timezones = info->timezonesValue(),
	});
	_hours = controller->session().user()->businessDetails().hours;

	AddDividerTextWithLottie(content, {
		.lottie = u"hours"_q,
		.lottieSize = st::settingsCloudPasswordIconSize,
		.lottieMargins = st::peerAppearanceIconPadding,
		.showFinished = showFinishes(),
		.about = tr::lng_hours_about(Ui::Text::WithEntities),
		.aboutMargins = st::peerAppearanceCoverLabelMargin,
	});

	Ui::AddSkip(content);
	const auto enabled = content->add(object_ptr<Ui::SettingsButton>(
		content,
		tr::lng_hours_show(),
		st::settingsButtonNoIcon
	))->toggleOn(rpl::single(bool(_hours.current())));

	_enabled = enabled->toggledValue();

	const auto wrap = content->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			content,
			object_ptr<Ui::VerticalLayout>(content)));
	const auto inner = wrap->entity();

	Ui::AddSkip(inner);
	Ui::AddDivider(inner);
	Ui::AddSkip(inner);

	for (auto i = 0; i != 7; ++i) {
		AddWeekButton(inner, controller, i, &_hours);
	}

	Ui::AddSkip(inner);
	Ui::AddDivider(inner);
	Ui::AddSkip(inner);

	state->timezones.value(
	) | rpl::filter([=](const Data::Timezones &value) {
		return !value.list.empty();
	}) | rpl::start_with_next([=](const Data::Timezones &value) {
		const auto now = _hours.current().timezoneId;
		if (!ranges::contains(value.list, now, &Data::Timezone::id)) {
			auto copy = _hours.current();
			copy.timezoneId = Data::FindClosestTimezoneId(value.list);
			_hours = std::move(copy);
		}
	}, inner->lifetime());

	auto timezoneLabel = rpl::combine(
		_hours.value(),
		state->timezones.value()
	) | rpl::map([](
			const Data::WorkingHours &hours,
			const Data::Timezones &timezones) {
		const auto i = ranges::find(
			timezones.list,
			hours.timezoneId,
			&Data::Timezone::id);
		return (i != end(timezones.list)) ? TimezoneFullName(*i) : QString();
	});
	const auto editTimezone = [=](const std::vector<Data::Timezone> &list) {
		const auto was = _hours.current().timezoneId;
		controller->show(Box(ChooseTimezoneBox, list, was, [=](QString id) {
			if (id != was) {
				auto copy = _hours.current();
				copy.timezoneId = id;
				_hours = std::move(copy);
			}
		}));
	};
	AddButtonWithLabel(
		inner,
		tr::lng_hours_time_zone(),
		std::move(timezoneLabel),
		st::settingsButtonNoIcon
	)->setClickedCallback([=] {
		const auto &list = state->timezones.current().list;
		if (!list.empty()) {
			editTimezone(list);
		} else {
			state->timezoneEditPending = true;
		}
	});

	if (state->timezones.current().list.empty()) {
		state->timezones.value(
		) | rpl::filter([](const Data::Timezones &value) {
			return !value.list.empty();
		}) | rpl::start_with_next([=](const Data::Timezones &value) {
			if (state->timezoneEditPending) {
				state->timezoneEditPending = false;
				editTimezone(value.list);
			}
		}, inner->lifetime());
	}

	wrap->toggleOn(enabled->toggledValue());
	wrap->finishAnimating();

	Ui::ResizeFitChild(this, content);
}

void WorkingHours::save() {
	const auto show = controller()->uiShow();
	controller()->session().data().businessInfo().saveWorkingHours(
		_enabled.current() ? _hours.current() : Data::WorkingHours(),
		[=](QString error) { show->showToast(error); });
}

} // namespace

Type WorkingHoursId() {
	return WorkingHours::Id();
}

} // namespace Settings
