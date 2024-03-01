/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/business/settings_greeting.h"

#include "base/event_filter.h"
#include "core/application.h"
#include "data/business/data_business_info.h"
#include "data/business/data_shortcut_messages.h"
#include "data/data_session.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "settings/business/settings_shortcut_messages.h"
#include "settings/business/settings_recipients_helper.h"
#include "ui/layers/generic_box.h"
#include "ui/text/text_utilities.h"
#include "ui/toast/toast.h"
#include "ui/widgets/box_content_divider.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/vertical_drum_picker.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/vertical_list.h"
#include "window/window_session_controller.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"

namespace Settings {
namespace {

constexpr auto kDefaultNoActivityDays = 7;

class Greeting : public BusinessSection<Greeting> {
public:
	Greeting(
		QWidget *parent,
		not_null<Window::SessionController*> controller);
	~Greeting();

	[[nodiscard]] rpl::producer<QString> title() override;

	const Ui::RoundRect *bottomSkipRounding() const {
		return &_bottomSkipRounding;
	}

private:
	void setupContent(not_null<Window::SessionController*> controller);
	void save();

	Ui::RoundRect _bottomSkipRounding;

	rpl::variable<Data::BusinessRecipients> _recipients;
	rpl::variable<bool> _canHave;
	rpl::event_stream<> _deactivateOnAttempt;
	rpl::variable<int> _noActivityDays;
	rpl::variable<bool> _enabled;

};

Greeting::Greeting(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: BusinessSection(parent, controller)
, _bottomSkipRounding(st::boxRadius, st::boxDividerBg) {
	setupContent(controller);
}

void EditPeriodBox(
		not_null<Ui::GenericBox*> box,
		int days,
		Fn<void(int)> save) {
	auto values = base::flat_set<int>{ 7, 14, 21, 28 };
	if (!values.contains(days)) {
		values.emplace(days);
	}
	const auto startIndex = int(values.find(days) - begin(values));

	const auto content = box->addRow(object_ptr<Ui::FixedHeightWidget>(
		box,
		st::settingsWorkingHoursPicker));

	const auto font = st::boxTextFont;
	const auto itemHeight = st::settingsWorkingHoursPickerItemHeight;
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
		p.drawText(
			r,
			tr::lng_days(tr::now, lt_count, *(values.begin() + index)),
			style::al_center);
		p.restore();
	};

	const auto picker = Ui::CreateChild<Ui::VerticalDrumPicker>(
		content,
		std::move(paintCallback),
		int(values.size()),
		itemHeight,
		startIndex);

	content->sizeValue(
	) | rpl::start_with_next([=](const QSize &s) {
		picker->resize(s.width(), s.height());
		picker->moveToLeft((s.width() - picker->width()) / 2, 0);
	}, content->lifetime());

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
	}, content->lifetime());

	base::install_event_filter(content, [=](not_null<QEvent*> e) {
		if ((e->type() == QEvent::MouseButtonPress)
			|| (e->type() == QEvent::MouseButtonRelease)
			|| (e->type() == QEvent::MouseMove)) {
			picker->handleMouseEvent(static_cast<QMouseEvent*>(e.get()));
		} else if (e->type() == QEvent::Wheel) {
			picker->handleWheelEvent(static_cast<QWheelEvent*>(e.get()));
		}
		return base::EventFilterResult::Continue;
	});
	base::install_event_filter(box, [=](not_null<QEvent*> e) {
		if (e->type() == QEvent::KeyPress) {
			picker->handleKeyEvent(static_cast<QKeyEvent*>(e.get()));
		}
		return base::EventFilterResult::Continue;
	});

	box->addButton(tr::lng_settings_save(), [=] {
		const auto weak = Ui::MakeWeak(box);
		save(*(begin(values) + picker->index()));
		if (const auto strong = weak.data()) {
			strong->closeBox();
		}
	});
	box->addButton(tr::lng_cancel(), [=] {
		box->closeBox();
	});
}

Greeting::~Greeting() {
	if (!Core::Quitting()) {
		save();
	}
}

rpl::producer<QString> Greeting::title() {
	return tr::lng_greeting_title();
}

void Greeting::setupContent(
		not_null<Window::SessionController*> controller) {
	using namespace rpl::mappers;

	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	const auto info = &controller->session().data().businessInfo();
	const auto current = info->greetingSettings();
	const auto disabled = !current.noActivityDays;

	_recipients = current.recipients;
	_noActivityDays = disabled
		? kDefaultNoActivityDays
		: current.noActivityDays;

	AddDividerTextWithLottie(content, {
		.lottie = u"greeting"_q,
		.lottieSize = st::settingsCloudPasswordIconSize,
		.lottieMargins = st::peerAppearanceIconPadding,
		.showFinished = showFinishes(),
		.about = tr::lng_greeting_about(Ui::Text::WithEntities),
		.aboutMargins = st::peerAppearanceCoverLabelMargin,
	});

	const auto session = &controller->session();
	_canHave = rpl::combine(
		ShortcutsCountValue(session),
		ShortcutsLimitValue(session),
		ShortcutExistsValue(session, u"hello"_q),
		(_1 < _2) || _3);

	Ui::AddSkip(content);
	const auto enabled = content->add(object_ptr<Ui::SettingsButton>(
		content,
		tr::lng_greeting_enable(),
		st::settingsButtonNoIcon
	))->toggleOn(rpl::single(
		!disabled
	) | rpl::then(rpl::merge(
		_canHave.value() | rpl::filter(!_1),
		_deactivateOnAttempt.events() | rpl::map_to(false)
	)));

	_enabled = enabled->toggledValue();
	_enabled.value() | rpl::filter(_1) | rpl::start_with_next([=] {
		if (!_canHave.current()) {
			controller->showToast({
				.text = tr::lng_greeting_limit_reached(tr::now),
				.adaptive = true,
			});
			_deactivateOnAttempt.fire({});
		}
	}, lifetime());

	Ui::AddSkip(content);

	content->add(
		object_ptr<Ui::SlideWrap<Ui::BoxContentDivider>>(
			content,
			object_ptr<Ui::BoxContentDivider>(
				content,
				st::boxDividerHeight,
				st::boxDividerBg,
				RectPart::Top))
	)->setDuration(0)->toggleOn(enabled->toggledValue() | rpl::map(!_1));
	content->add(
		object_ptr<Ui::SlideWrap<Ui::BoxContentDivider>>(
			content,
			object_ptr<Ui::BoxContentDivider>(
				content))
	)->setDuration(0)->toggleOn(enabled->toggledValue());

	const auto wrap = content->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			content,
			object_ptr<Ui::VerticalLayout>(content)));
	const auto inner = wrap->entity();

	const auto createWrap = inner->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			inner,
			object_ptr<Ui::VerticalLayout>(inner)));
	const auto createInner = createWrap->entity();
	Ui::AddSkip(createInner);
	const auto create = AddButtonWithLabel(
		createInner,
		rpl::conditional(
			ShortcutExistsValue(session, u"hello"_q),
			tr::lng_business_edit_messages(),
			tr::lng_greeting_create()),
		ShortcutMessagesCountValue(
			session,
			u"hello"_q
		) | rpl::map([=](int count) {
			return count
				? tr::lng_forum_messages(tr::now, lt_count, count)
				: QString();
		}),
		st::settingsButtonLightNoIcon);
	create->setClickedCallback([=] {
		const auto owner = &controller->session().data();
		const auto id = owner->shortcutMessages().emplaceShortcut("hello");
		showOther(ShortcutMessagesId(id));
	});
	Ui::AddSkip(createInner);
	Ui::AddDivider(createInner);

	createWrap->toggleOn(rpl::single(true));

	Ui::AddSkip(inner);
	AddBusinessRecipientsSelector(inner, {
		.controller = controller,
		.title = tr::lng_greeting_recipients(),
		.data = &_recipients,
	});

	Ui::AddSkip(inner);
	Ui::AddDivider(inner);
	Ui::AddSkip(inner);

	AddButtonWithLabel(
		inner,
		tr::lng_greeting_period_title(),
		_noActivityDays.value(
		) | rpl::map(
			[](int days) { return tr::lng_days(tr::now, lt_count, days); }
		),
		st::settingsButtonNoIcon
	)->setClickedCallback([=] {
		controller->show(Box(
			EditPeriodBox,
			_noActivityDays.current(),
			[=](int days) { _noActivityDays = days; }));
	});

	Ui::AddSkip(inner);
	Ui::AddDividerText(
		inner,
		tr::lng_greeting_period_about(),
		st::settingsChatbotsBottomTextMargin,
		RectPart::Top);

	wrap->toggleOn(enabled->toggledValue());
	wrap->finishAnimating();

	Ui::ResizeFitChild(this, content);
}

void Greeting::save() {
	controller()->session().data().businessInfo().saveGreetingSettings(
		_enabled.current() ? Data::GreetingSettings{
			.recipients = _recipients.current(),
			.noActivityDays = _noActivityDays.current(),
		} : Data::GreetingSettings());
}

} // namespace

Type GreetingId() {
	return Greeting::Id();
}

} // namespace Settings
