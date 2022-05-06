/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/cloud_password/settings_cloud_password_hint.h"

#include "lang/lang_keys.h"
#include "settings/cloud_password/settings_cloud_password_common.h"
#include "settings/cloud_password/settings_cloud_password_email.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/input_fields.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "styles/style_boxes.h"
#include "styles/style_settings.h"

namespace Settings {
namespace CloudPassword {

class Hint : public TypedAbstractStep<Hint> {
public:
	using TypedAbstractStep::TypedAbstractStep;

	[[nodiscard]] rpl::producer<QString> title() override;
	void setupContent();

};

rpl::producer<QString> Hint::title() {
	return tr::lng_settings_cloud_password_hint_title();
}

void Hint::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	auto currentStepData = stepData();
	const auto currentStepDataHint = base::take(currentStepData.hint);
	setStepData(currentStepData);

	SetupHeader(
		content,
		u"cloud_password/hint"_q,
		showFinishes(),
		tr::lng_settings_cloud_password_hint_subtitle(),
		tr::lng_settings_cloud_password_hint_about());

	AddSkip(content, st::settingLocalPasscodeDescriptionBottomSkip);

	const auto wrap = AddWrappedField(
		content,
		tr::lng_cloud_password_hint(),
		currentStepDataHint);
	const auto newInput = wrap->entity();
	const auto error = AddError(content, nullptr);
	QObject::connect(newInput, &Ui::InputField::changed, [=] {
		error->hide();
	});
	AddSkipInsteadOfField(content);

	const auto save = [=](const QString &hint) {
		auto data = stepData();
		data.hint = hint;
		setStepData(std::move(data));
		showOther(CloudPasswordEmailId());
	};

	const auto skip = Ui::CreateChild<Ui::LinkButton>(
		this,
		tr::lng_settings_cloud_password_skip_hint(tr::now));
	wrap->geometryValue(
	) | rpl::start_with_next([=](QRect r) {
		r.translate(wrap->entity()->pos().x(), 0);
		skip->moveToLeft(r.x(), r.y() + r.height() + st::passcodeTextLine);
	}, skip->lifetime());
	skip->setClickedCallback([=] {
		save(QString());
	});

	const auto button = AddDoneButton(content, tr::lng_continue());
	button->setClickedCallback([=] {
		const auto newText = newInput->getLastText();
		if (newText.isEmpty()) {
			newInput->setFocus();
			newInput->showError();
		} else if (newText == stepData().password) {
			error->show();
			error->setText(tr::lng_cloud_password_bad(tr::now));
			newInput->setFocus();
			newInput->showError();
		} else {
			save(newText);
		}
	});

	const auto submit = [=] { button->clicked({}, Qt::LeftButton); };
	QObject::connect(newInput, &Ui::InputField::submitted, submit);

	setFocusCallback([=] { newInput->setFocus(); });

	Ui::ResizeFitChild(this, content);
}

} // namespace CloudPassword

Type CloudPasswordHintId() {
	return CloudPassword::Hint::Id();
}

} // namespace Settings
