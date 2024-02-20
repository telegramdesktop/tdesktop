/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/business/settings_quick_replies.h"

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

class QuickReplies : public BusinessSection<QuickReplies> {
public:
	QuickReplies(
		QWidget *parent,
		not_null<Window::SessionController*> controller);
	~QuickReplies();

	[[nodiscard]] rpl::producer<QString> title() override;

private:
	void setupContent(not_null<Window::SessionController*> controller);
	void save();

	rpl::variable<Data::BusinessRecipients> _recipients;

};

QuickReplies::QuickReplies(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: BusinessSection(parent, controller) {
	setupContent(controller);
}

QuickReplies::~QuickReplies() {
	if (!Core::Quitting()) {
		save();
	}
}

rpl::producer<QString> QuickReplies::title() {
	return tr::lng_replies_title();
}

void QuickReplies::setupContent(
		not_null<Window::SessionController*> controller) {
	using namespace rpl::mappers;

	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

	AddDividerTextWithLottie(content, {
		.lottie = u"writing"_q,
		.lottieSize = st::settingsCloudPasswordIconSize,
		.lottieMargins = st::peerAppearanceIconPadding,
		.showFinished = showFinishes(),
		.about = tr::lng_replies_about(Ui::Text::WithEntities),
		.aboutMargins = st::peerAppearanceCoverLabelMargin,
	});

	Ui::AddSkip(content);
	const auto enabled = content->add(object_ptr<Ui::SettingsButton>(
		content,
		tr::lng_replies_add(),
		st::settingsButtonNoIcon
	));

	enabled->setClickedCallback([=] {

	});

	const auto wrap = content->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			content,
			object_ptr<Ui::VerticalLayout>(content)));
	const auto inner = wrap->entity();

	Ui::AddSkip(inner);
	Ui::AddDivider(inner);

	Ui::ResizeFitChild(this, content);
}

void QuickReplies::save() {
}

} // namespace

Type QuickRepliesId() {
	return QuickReplies::Id();
}

} // namespace Settings
