/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/business/settings_chatbots.h"

#include "lang/lang_keys.h"
#include "settings/settings_common_session.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/checkbox.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/vertical_list.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"

namespace Settings {
namespace {

constexpr auto kAllExcept = 0;
constexpr auto kSelectedOnly = 1;

class Chatbots : public Section<Chatbots> {
public:
	Chatbots(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	[[nodiscard]] rpl::producer<QString> title() override;

	rpl::producer<> showFinishes() const {
		return _showFinished.events();
	}

	const Ui::RoundRect *bottomSkipRounding() const {
		return &_bottomSkipRounding;
	}

private:
	void setupContent(not_null<Window::SessionController*> controller);

	void showFinished() override {
		_showFinished.fire({});
	}

	rpl::event_stream<> _showFinished;
	Ui::RoundRect _bottomSkipRounding;

};

Chatbots::Chatbots(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent)
, _bottomSkipRounding(st::boxRadius, st::boxDividerBg) {
	setupContent(controller);
}

rpl::producer<QString> Chatbots::title() {
	return tr::lng_chatbots_title();
}

void Chatbots::setupContent(
		not_null<Window::SessionController*> controller) {
	using namespace rpl::mappers;

	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

	struct State {
		rpl::variable<bool> onlySelected = false;
		rpl::variable<bool> replyAllowed = true;
	};
	const auto state = content->lifetime().make_state<State>();

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
			tr::lng_chatbots_placeholder()),
		st::settingsChatbotsUsernameMargins);

	Ui::AddDividerText(
		content,
		tr::lng_chatbots_add_about(),
		st::peerAppearanceDividerTextMargin);
	Ui::AddSkip(content);
	Ui::AddSubsectionTitle(content, tr::lng_chatbots_access_title());

	const auto group = std::make_shared<Ui::RadiobuttonGroup>(
		state->onlySelected.current() ? kSelectedOnly : kAllExcept);
	const auto everyone = content->add(
		object_ptr<Ui::Radiobutton>(
			content,
			group,
			kAllExcept,
			tr::lng_chatbots_all_except(tr::now),
			st::settingsChatbotsAccess),
		st::settingsChatbotsAccessMargins);
	const auto selected = content->add(
		object_ptr<Ui::Radiobutton>(
			content,
			group,
			kSelectedOnly,
			tr::lng_chatbots_selected(tr::now),
			st::settingsChatbotsAccess),
		st::settingsChatbotsAccessMargins);

	Ui::AddSkip(content, st::settingsChatbotsAccessSkip);
	Ui::AddDivider(content);

	const auto excludeWrap = content->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			content,
			object_ptr<Ui::VerticalLayout>(content))
	)->setDuration(0);
	const auto excludeInner = excludeWrap->entity();

	Ui::AddSkip(excludeInner);
	Ui::AddSubsectionTitle(excludeInner, tr::lng_chatbots_excluded_title());
	const auto excludeAdd = AddButtonWithIcon(
		excludeInner,
		tr::lng_chatbots_exclude_button(),
		st::settingsChatbotsAdd,
		{ &st::settingsIconRemove, IconType::Round, &st::windowBgActive });

	excludeWrap->toggleOn(state->onlySelected.value() | rpl::map(!_1));
	excludeWrap->finishAnimating();

	const auto includeWrap = content->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			content,
			object_ptr<Ui::VerticalLayout>(content))
	)->setDuration(0);
	const auto includeInner = includeWrap->entity();

	Ui::AddSkip(includeInner);
	Ui::AddSubsectionTitle(includeInner, tr::lng_chatbots_included_title());
	const auto includeAdd = AddButtonWithIcon(
		includeInner,
		tr::lng_chatbots_include_button(),
		st::settingsChatbotsAdd,
		{ &st::settingsIconAdd, IconType::Round, &st::windowBgActive });

	includeWrap->toggleOn(state->onlySelected.value());
	includeWrap->finishAnimating();

	group->setChangedCallback([=](int value) {
		state->onlySelected = (value == kSelectedOnly);
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
	))->toggleOn(state->replyAllowed.value())->toggledChanges(
	) | rpl::start_with_next([=](bool value) {
		state->replyAllowed = value;
	}, content->lifetime());
	Ui::AddSkip(content);

	Ui::AddDividerText(
		content,
		tr::lng_chatbots_reply_about(),
		st::settingsChatbotsBottomTextMargin,
		RectPart::Top);

	Ui::ResizeFitChild(this, content);
}

} // namespace

Type ChatbotsId() {
	return Chatbots::Id();
}

} // namespace Settings
