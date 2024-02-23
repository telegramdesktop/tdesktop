/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/business/settings_chatbots.h"

#include "core/application.h"
#include "data/business/data_business_chatbots.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "settings/business/settings_recipients_helper.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/buttons.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/vertical_list.h"
#include "window/window_session_controller.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"

namespace Settings {
namespace {

enum class LookupState {
	Empty,
	Loading,
	Ready,
};

struct BotState {
	UserData *bot = nullptr;
	LookupState state = LookupState::Empty;
};

class Chatbots : public BusinessSection<Chatbots> {
public:
	Chatbots(
		QWidget *parent,
		not_null<Window::SessionController*> controller);
	~Chatbots();

	[[nodiscard]] rpl::producer<QString> title() override;

	const Ui::RoundRect *bottomSkipRounding() const {
		return &_bottomSkipRounding;
	}

private:
	void setupContent(not_null<Window::SessionController*> controller);
	void save();

	Ui::RoundRect _bottomSkipRounding;

	rpl::variable<Data::BusinessRecipients> _recipients;
	rpl::variable<QString> _usernameValue;
	rpl::variable<BotState> _botValue = nullptr;
	rpl::variable<bool> _repliesAllowed = true;

};

[[nodiscard]] rpl::producer<QString> DebouncedValue(
		not_null<Ui::InputField*> field) {
	return rpl::single(field->getLastText());
}

[[nodiscard]] rpl::producer<BotState> LookupBot(
		not_null<Main::Session*> session,
		rpl::producer<QString> usernameChanges) {
	return rpl::never<BotState>();
}

[[nodiscard]] object_ptr<Ui::RpWidget> MakeBotPreview(
		not_null<QWidget*> parent,
		rpl::producer<BotState> state,
		Fn<void()> resetBot) {
	return object_ptr<Ui::RpWidget>(parent.get());
}

Chatbots::Chatbots(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: BusinessSection(parent, controller)
, _bottomSkipRounding(st::boxRadius, st::boxDividerBg) {
	setupContent(controller);
}

Chatbots::~Chatbots() {
	if (!Core::Quitting()) {
		save();
	}
}

rpl::producer<QString> Chatbots::title() {
	return tr::lng_chatbots_title();
}

void Chatbots::setupContent(
		not_null<Window::SessionController*> controller) {
	using namespace rpl::mappers;

	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	const auto current = controller->session().data().chatbots().current();

	_recipients = current.recipients;
	_repliesAllowed = current.repliesAllowed;

	AddDividerTextWithLottie(content, {
		.lottie = u"robot"_q,
		.lottieSize = st::settingsCloudPasswordIconSize,
		.lottieMargins = st::peerAppearanceIconPadding,
		.showFinished = showFinishes(),
		.about = tr::lng_chatbots_about(
			lt_link,
			tr::lng_chatbots_about_link(
			) | Ui::Text::ToLink(u"internal:about_business_chatbots"_q),
			Ui::Text::WithEntities),
		.aboutMargins = st::peerAppearanceCoverLabelMargin,
	});

	const auto username = content->add(
		object_ptr<Ui::InputField>(
			content,
			st::settingsChatbotsUsername,
			tr::lng_chatbots_placeholder(),
			(current.bot
				? current.bot->session().createInternalLink(
					current.bot->username())
				: QString())),
		st::settingsChatbotsUsernameMargins);

	_usernameValue = DebouncedValue(username);
	_botValue = rpl::single(BotState{
		current.bot,
		current.bot ? LookupState::Ready : LookupState::Empty
	}) | rpl::then(
		LookupBot(&controller->session(), _usernameValue.changes())
	);

	const auto resetBot = [=] {
		username->setText(QString());
		username->setFocus();
	};
	content->add(object_ptr<Ui::SlideWrap<Ui::RpWidget>>(
		content,
		MakeBotPreview(content, _botValue.value(), resetBot)));

	Ui::AddDividerText(
		content,
		tr::lng_chatbots_add_about(),
		st::peerAppearanceDividerTextMargin);

	AddBusinessRecipientsSelector(content, {
		.controller = controller,
		.title = tr::lng_chatbots_access_title(),
		.data = &_recipients,
	});

	Ui::AddSkip(content, st::settingsChatbotsAccessSkip);
	Ui::AddDividerText(
		content,
		tr::lng_chatbots_exclude_about(),
		st::peerAppearanceDividerTextMargin);

	Ui::AddSkip(content);
	Ui::AddSubsectionTitle(content, tr::lng_chatbots_permissions_title());
	content->add(object_ptr<Ui::SettingsButton>(
		content,
		tr::lng_chatbots_reply(),
		st::settingsButtonNoIcon
	))->toggleOn(_repliesAllowed.value())->toggledChanges(
	) | rpl::start_with_next([=](bool value) {
		_repliesAllowed = value;
	}, content->lifetime());
	Ui::AddSkip(content);

	Ui::AddDividerText(
		content,
		tr::lng_chatbots_reply_about(),
		st::settingsChatbotsBottomTextMargin,
		RectPart::Top);

	Ui::ResizeFitChild(this, content);
}

void Chatbots::save() {
	const auto settings = Data::ChatbotsSettings{
		.bot = _botValue.current().bot,
		.recipients = _recipients.current(),
		.repliesAllowed = _repliesAllowed.current(),
	};
	controller()->session().data().chatbots().save(settings);
}

} // namespace

Type ChatbotsId() {
	return Chatbots::Id();
}

} // namespace Settings
