/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/business/settings_greeting.h"

#include "core/application.h"
#include "data/data_session.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "settings/business/settings_recipients_helper.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/buttons.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/vertical_list.h"
#include "window/window_session_controller.h"
#include "styles/style_settings.h"

namespace Settings {
namespace {

class Greeting : public BusinessSection<Greeting> {
public:
	Greeting(
		QWidget *parent,
		not_null<Window::SessionController*> controller);
	~Greeting();

	[[nodiscard]] rpl::producer<QString> title() override;

private:
	void setupContent(not_null<Window::SessionController*> controller);
	void save();

	rpl::variable<Data::BusinessRecipients> _recipients;

};

Greeting::Greeting(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: BusinessSection(parent, controller) {
	setupContent(controller);
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
	//const auto current = controller->session().data().chatbots().current();

	//_recipients = current.recipients;

	AddDividerTextWithLottie(content, {
		.lottie = u"greeting"_q,
		.lottieSize = st::settingsCloudPasswordIconSize,
		.lottieMargins = st::peerAppearanceIconPadding,
		.showFinished = showFinishes(),
		.about = tr::lng_greeting_about(Ui::Text::WithEntities),
		.aboutMargins = st::peerAppearanceCoverLabelMargin,
	});

	Ui::AddSkip(content);
	const auto enabled = content->add(object_ptr<Ui::SettingsButton>(
		content,
		tr::lng_greeting_enable(),
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

	AddBusinessRecipientsSelector(inner, {
		.controller = controller,
		.title = tr::lng_greeting_recipients(),
		.data = &_recipients,
	});

	Ui::AddSkip(inner, st::settingsChatbotsAccessSkip);

	Ui::ResizeFitChild(this, content);
}

void Greeting::save() {
}

} // namespace

Type GreetingId() {
	return Greeting::Id();
}

} // namespace Settings
