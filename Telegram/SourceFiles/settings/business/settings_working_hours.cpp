/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/business/settings_working_hours.h"

#include "core/application.h"
#include "data/data_session.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "settings/business/settings_recipients_helper.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/buttons.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/vertical_list.h"
#include "window/window_session_controller.h"
#include "styles/style_settings.h"

namespace Settings {
namespace {

class WorkingHours : public BusinessSection<WorkingHours> {
public:
	WorkingHours(
		QWidget *parent,
		not_null<Window::SessionController*> controller);
	~WorkingHours();

	[[nodiscard]] rpl::producer<QString> title() override;

private:
	void setupContent(not_null<Window::SessionController*> controller);
	void save();

};

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

rpl::producer<QString> WorkingHours::title() {
	return tr::lng_hours_title();
}

void WorkingHours::setupContent(
	not_null<Window::SessionController*> controller) {
	using namespace rpl::mappers;

	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

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
	))->toggleOn(rpl::single(false));

	const auto wrap = content->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			content,
			object_ptr<Ui::VerticalLayout>(content)));
	const auto inner = wrap->entity();

	Ui::AddSkip(inner);
	Ui::AddDivider(inner);

	wrap->toggleOn(enabled->toggledValue());
	wrap->finishAnimating();

	Ui::ResizeFitChild(this, content);
}

void WorkingHours::save() {
}

} // namespace

Type WorkingHoursId() {
	return WorkingHours::Id();
}

} // namespace Settings
