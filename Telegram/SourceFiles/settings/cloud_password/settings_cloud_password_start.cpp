/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/cloud_password/settings_cloud_password_start.h"

#include "lang/lang_keys.h"
#include "settings/cloud_password/settings_cloud_password_common.h"
#include "settings/cloud_password/settings_cloud_password_input.h"
#include "ui/widgets/buttons.h"
#include "ui/wrap/vertical_layout.h"
#include "styles/style_settings.h"

namespace Settings {
namespace CloudPassword {

class Start : public TypedAbstractStep<Start> {
public:
	using TypedAbstractStep::TypedAbstractStep;

	[[nodiscard]] rpl::producer<QString> title() override;
	void setupContent();

};

rpl::producer<QString> Start::title() {
	return tr::lng_settings_cloud_password_start_title();
}

void Start::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

	SetupHeader(
		content,
		u"cloud_password/intro"_q,
		showFinishes(),
		tr::lng_settings_cloud_password_start_title(),
		tr::lng_settings_cloud_password_start_about());

	AddSkip(content, st::settingLocalPasscodeDescriptionBottomSkip);

	AddSkipInsteadOfField(content);
	AddSkipInsteadOfField(content);
	AddSkipInsteadOfError(content);

	AddDoneButton(
		content,
		tr::lng_settings_cloud_password_password_subtitle()
	)->setClickedCallback([=] {
		showOther(CloudPasswordInputId());
	});

	Ui::ResizeFitChild(this, content);
}

} // namespace CloudPassword

Type CloudPasswordStartId() {
	return CloudPassword::Start::Id();
}

} // namespace Settings
