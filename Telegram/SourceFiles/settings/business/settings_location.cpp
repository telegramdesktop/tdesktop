/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/business/settings_location.h"

#include "core/application.h"
#include "data/data_session.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "settings/business/settings_recipients_helper.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/vertical_list.h"
#include "window/window_session_controller.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"

namespace Settings {
namespace {

class Location : public BusinessSection<Location> {
public:
	Location(
		QWidget *parent,
		not_null<Window::SessionController*> controller);
	~Location();

	[[nodiscard]] rpl::producer<QString> title() override;

	const Ui::RoundRect *bottomSkipRounding() const override {
		return mapSupported() ? nullptr : &_bottomSkipRounding;
	}

private:
	void setupContent(not_null<Window::SessionController*> controller);
	void save();

	[[nodiscard]] bool mapSupported() const;

	Ui::RoundRect _bottomSkipRounding;

};

Location::Location(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: BusinessSection(parent, controller)
, _bottomSkipRounding(st::boxRadius, st::boxDividerBg) {
	setupContent(controller);
}

Location::~Location() {
	if (!Core::Quitting()) {
		save();
	}
}

rpl::producer<QString> Location::title() {
	return tr::lng_location_title();
}

void Location::setupContent(
	not_null<Window::SessionController*> controller) {
	using namespace rpl::mappers;

	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

#if 0 // #TODO location choosing
	AddDividerTextWithLottie(content, {
		.lottie = u"location"_q,
		.lottieSize = st::settingsCloudPasswordIconSize,
		.lottieMargins = st::peerAppearanceIconPadding,
		.showFinished = showFinishes(),
		.about = tr::lng_location_about(Ui::Text::WithEntities),
		.aboutMargins = st::peerAppearanceCoverLabelMargin,
	});

	const auto address = content->add(
		object_ptr<Ui::InputField>(
			content,
			st::settingsLocationAddress,
			Ui::InputField::Mode::MultiLine,
			tr::lng_location_address(),
			QString()),
		st::settingsChatbotsUsernameMargins);

	showFinishes() | rpl::start_with_next([=] {
		address->setFocus();
	}, address->lifetime());
#endif

	if (!mapSupported()) {
		AddDividerTextWithLottie(content, {
			.lottie = u"phone"_q,
			.lottieSize = st::settingsCloudPasswordIconSize,
			.lottieMargins = st::peerAppearanceIconPadding,
			.showFinished = showFinishes(),
			.about = tr::lng_location_fallback(Ui::Text::WithEntities),
			.aboutMargins = st::peerAppearanceCoverLabelMargin,
			.parts = RectPart::Top,
		});
	} else {

	}

	Ui::ResizeFitChild(this, content);
}

void Location::save() {
}

bool Location::mapSupported() const {
	return false;
}

} // namespace

Type LocationId() {
	return Location::Id();
}

} // namespace Settings
