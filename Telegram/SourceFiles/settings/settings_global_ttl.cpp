/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/settings_global_ttl.h"

#include "api/api_self_destruct.h"
#include "apiwrap.h"
#include "lang/lang_keys.h"
#include "lottie/lottie_icon.h"
#include "main/main_session.h"
#include "menu/menu_ttl.h"
#include "settings/settings_common.h"
#include "ui/boxes/confirm_box.h"
#include "ui/text/format_values.h"
#include "ui/text/text_utilities.h"
#include "ui/toasts/common_toasts.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"

namespace Settings {
namespace {

void SetupTopContent(
		not_null<Ui::VerticalLayout*> parent,
		rpl::producer<> showFinished) {
	const auto divider = Ui::CreateChild<Ui::BoxContentDivider>(parent.get());
	const auto verticalLayout = parent->add(
		object_ptr<Ui::VerticalLayout>(parent.get()));

	auto icon = CreateLottieIcon(
		verticalLayout,
		{
			.name = u"ttl"_q,
			.sizeOverride = {
				st::settingsCloudPasswordIconSize,
				st::settingsCloudPasswordIconSize,
			},
		},
		st::settingsFilterIconPadding);
	std::move(
		showFinished
	) | rpl::start_with_next([animate = std::move(icon.animate)] {
		animate(anim::repeat::loop);
	}, verticalLayout->lifetime());
	verticalLayout->add(std::move(icon.widget));

	verticalLayout->geometryValue(
	) | rpl::start_with_next([=](const QRect &r) {
		divider->setGeometry(r);
	}, divider->lifetime());

}

} // namespace

class GlobalTTL : public Section<GlobalTTL> {
public:
	GlobalTTL(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	[[nodiscard]] rpl::producer<QString> title() override;
	void setupContent();

	void showFinished() override final;

private:
	void rebuildButtons(TimeId currentTTL) const;
	void showSure(TimeId ttl, bool rebuild) const;

	void request(TimeId ttl) const;

	const not_null<Window::SessionController*> _controller;
	const std::shared_ptr<Ui::RadiobuttonGroup> _group;
	const std::shared_ptr<Window::Show> _show;

	not_null<Ui::VerticalLayout*> _buttons;

	rpl::event_stream<> _showFinished;
	rpl::lifetime _requestLifetime;

};

GlobalTTL::GlobalTTL(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent)
, _controller(controller)
, _group(std::make_shared<Ui::RadiobuttonGroup>(0))
, _show(std::make_shared<Window::Show>(controller))
, _buttons(Ui::CreateChild<Ui::VerticalLayout>(this)) {
	setupContent();
}

rpl::producer<QString> GlobalTTL::title() {
	return tr::lng_settings_ttl_title();
}

void GlobalTTL::request(TimeId ttl) const {
	_controller->session().api().selfDestruct().updateDefaultHistoryTTL(ttl);
}

void GlobalTTL::showSure(TimeId ttl, bool rebuild) const {
	const auto ttlText = Ui::FormatTTLAfter(ttl);
	const auto confirmed = [=] {
		if (rebuild) {
			rebuildButtons(ttl);
		}
		_group->setChangedCallback([=](int value) {
			_group->setChangedCallback(nullptr);
			Ui::ShowMultilineToast({
				.parentOverride = _show->toastParent(),
				.text = tr::lng_settings_ttl_after_toast(
					tr::now,
					lt_after_duration,
					{ .text = ttlText },
					Ui::Text::WithEntities)
			});
			_show->hideLayer(); // Don't use close().
		});
		request(ttl);
	};
	if (_group->value()) {
		confirmed();
		return;
	}
	_show->showBox(Ui::MakeConfirmBox({
		.text = tr::lng_settings_ttl_after_sure(
			lt_after_duration,
			rpl::single(ttlText)),
		.confirmed = confirmed,
		.cancelled = [=](Fn<void()> &&close) {
			_group->setChangedCallback(nullptr);
			close();
		},
		.confirmText = tr::lng_sure_enable(),
	}));
}

void GlobalTTL::rebuildButtons(TimeId currentTTL) const {
	auto ttls = std::vector<TimeId>{
		0,
		3600 * 24,
		3600 * 24 * 7,
		3600 * 24 * 31,
	};
	if (!ranges::contains(ttls, currentTTL)) {
		ttls.push_back(currentTTL);
		ranges::sort(ttls);
	}
	if (_buttons->count() > ttls.size()) {
		return;
	}
	_buttons->clear();
	for (const auto &ttl : ttls) {
		const auto ttlText = Ui::FormatTTLAfter(ttl);
		const auto button = AddButton(
			_buttons,
			(!ttl)
				? tr::lng_settings_ttl_after_off()
				: tr::lng_settings_ttl_after(
					lt_after_duration,
					rpl::single(ttlText)),
			st::settingsButtonNoIcon);
		button->setClickedCallback([=] {
			if (_group->value() == ttl) {
				return;
			}
			if (!ttl) {
				_group->setChangedCallback(nullptr);
				request(ttl);
				return;
			}
			showSure(ttl, false);
		});
		const auto radio = Ui::CreateChild<Ui::Radiobutton>(
			button.get(),
			_group,
			ttl,
			QString());
		radio->setAttribute(Qt::WA_TransparentForMouseEvents);
		radio->show();
		button->sizeValue(
		) | rpl::start_with_next([=] {
			radio->moveToRight(0, radio->checkRect().top());
		}, radio->lifetime());
	}
	_buttons->resizeToWidth(width());
}

void GlobalTTL::setupContent() {
	setFocusPolicy(Qt::StrongFocus);
	setFocus();

	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

	SetupTopContent(content, _showFinished.events());

	AddSkip(content);
	AddSubsectionTitle(content, tr::lng_settings_ttl_after_subtitle());

	content->add(object_ptr<Ui::VerticalLayout>::fromRaw(_buttons));

	{
		const auto &apiTTL = _controller->session().api().selfDestruct();
		const auto rebuild = [=](TimeId period) {
			rebuildButtons(period);
			_group->setValue(period);
		};
		rebuild(apiTTL.periodDefaultHistoryTTLCurrent());
		apiTTL.periodDefaultHistoryTTL(
		) | rpl::start_with_next(rebuild, content->lifetime());
	}

	const auto show = std::make_shared<Window::Show>(_controller);
	AddButton(
		content,
		tr::lng_settings_ttl_after_custom(),
		st::settingsButtonNoIcon)->setClickedCallback([=] {
		struct Args {
			std::shared_ptr<Ui::Show> show;
			TimeId startTtl;
			rpl::producer<QString> about;
			Fn<void(TimeId)> callback;
		};

		show->showBox(Box(TTLMenu::TTLBox, TTLMenu::Args{
			.show = show,
			.startTtl = _group->value(),
			.callback = [=](TimeId ttl) { showSure(ttl, true); },
			.hideDisable = true,
		}));
	});

	AddSkip(content);

	auto footer = object_ptr<Ui::FlatLabel>(
		content,
		tr::lng_settings_ttl_after_about(
			lt_link,
			tr::lng_settings_ttl_after_about_link(
			) | rpl::map([](QString s) { return Ui::Text::Link(s, 1); }),
			Ui::Text::WithEntities),
		st::boxDividerLabel);
	footer->overrideLinkClickHandler([=] {
	});
	content->add(object_ptr<Ui::DividerLabel>(
		content,
		std::move(footer),
		st::settingsDividerLabelPadding));

	Ui::ResizeFitChild(this, content);
}

void GlobalTTL::showFinished() {
	_showFinished.fire({});
}

Type GlobalTTLId() {
	return GlobalTTL::Id();
}

} // namespace Settings
