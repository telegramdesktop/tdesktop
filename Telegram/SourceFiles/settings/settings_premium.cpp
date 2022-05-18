/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/settings_premium.h"

#include "lang/lang_keys.h"
#include "settings/settings_common.h"
#include "ui/abstract_button.h"
#include "ui/effects/gradient.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/gradient_round_button.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"

namespace Settings {
namespace {

class Premium : public Section<Premium> {
public:
	Premium(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	[[nodiscard]] rpl::producer<QString> title() override;

	[[nodiscard]] QPointer<Ui::RpWidget> createPinnedToBottom(
		not_null<Ui::RpWidget*> parent) override;

private:
	void setupContent(not_null<Window::SessionController*> controller);

};

Premium::Premium(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent) {
	setupContent(controller);
}

rpl::producer<QString> Premium::title() {
	return tr::lng_premium_summary_title();
}

void Premium::setupContent(not_null<Window::SessionController*> controller) {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

	content->add(
		object_ptr<Ui::CenterWrap<>>(
			content,
			object_ptr<Ui::FlatLabel>(
				content,
				tr::lng_premium_summary_title(),
				st::changePhoneTitle)),
		st::changePhoneTitlePadding);

	const auto wrap = content->add(
		object_ptr<Ui::CenterWrap<>>(
			content,
			object_ptr<Ui::FlatLabel>(
				content,
				tr::lng_premium_summary_top_about(Ui::Text::RichLangValue),
				st::changePhoneDescription)),
		st::changePhoneDescriptionPadding);
	wrap->resize(
		wrap->width(),
		st::settingLocalPasscodeDescriptionHeight);

	AddSkip(content);
	AddDivider(content);
	AddSkip(content);

	const auto &st = st::settingsButton;
	const auto &stLabel = st::defaultFlatLabel;
	const auto iconSize = st::settingsPremiumIconDouble.size();

	const auto icons = std::array<const style::icon *, 10>{ {
		&st::settingsPremiumIconDouble,
		&st::premiumIconFolders, //
		&st::settingsPremiumIconSpeed,
		&st::settingsPremiumIconVoice,
		&st::settingsPremiumIconChannelsOff,
		&st::settingsPremiumIconLike,
		&st::settingsIconStickers,
		&st::settingsIconChat,
		&st::settingsPremiumIconStar,
		&st::settingsPremiumIconPlay,
	} };

	auto iconContainers = std::vector<Ui::AbstractButton*>();
	iconContainers.reserve(int(icons.size()));

	auto titlePadding = st.padding;
	titlePadding.setBottom(0);
	auto descriptionPadding = st.padding;
	descriptionPadding.setTop(0);
	const auto addRow = [&](
			rpl::producer<QString> &&title,
			rpl::producer<QString> &&text) {
		const auto labelAscent = stLabel.style.font->ascent;

		const auto label = content->add(
			object_ptr<Ui::FlatLabel>(
				content,
				std::move(title) | rpl::map(Ui::Text::Bold),
				stLabel),
			titlePadding);
		AddSkip(content, st::settingsPremiumDescriptionSkip);
		content->add(
			object_ptr<Ui::FlatLabel>(
				content,
				std::move(text),
				st::boxDividerLabel),
			descriptionPadding);

		const auto dummy = Ui::CreateChild<Ui::AbstractButton>(content);
		dummy->setAttribute(Qt::WA_TransparentForMouseEvents);

		content->sizeValue(
		) | rpl::start_with_next([=](const QSize &s) {
			dummy->resize(s.width(), iconSize.height());
		}, dummy->lifetime());

		label->geometryValue(
		) | rpl::start_with_next([=](const QRect &r) {
			dummy->moveToLeft(0, r.y() + (r.height() - labelAscent));
		}, dummy->lifetime());

		iconContainers.push_back(dummy);
	};

	using namespace tr;
	addRow(lng_premium_summary_subtitle1(), lng_premium_summary_about1());
	addRow(lng_premium_summary_subtitle2(), lng_premium_summary_about2());
	addRow(lng_premium_summary_subtitle3(), lng_premium_summary_about3());
	addRow(lng_premium_summary_subtitle4(), lng_premium_summary_about4());
	addRow(lng_premium_summary_subtitle5(), lng_premium_summary_about5());
	addRow(lng_premium_summary_subtitle6(), lng_premium_summary_about6());
	addRow(lng_premium_summary_subtitle7(), lng_premium_summary_about7());
	addRow(lng_premium_summary_subtitle8(), lng_premium_summary_about8());
	addRow(lng_premium_summary_subtitle9(), lng_premium_summary_about9());
	addRow(lng_premium_summary_subtitle10(), lng_premium_summary_about10());

	content->resizeToWidth(content->height());

	// Icons.
	Assert(iconContainers.size() > 2);
	const auto from = iconContainers.front()->y();
	const auto to = iconContainers.back()->y() + iconSize.height();
	auto gradient = QLinearGradient(0, 0, 0, to - from);
	gradient.setColorAt(.0, st::premiumButtonBg3->c);
	gradient.setColorAt(.5, st::premiumButtonBg2->c);
	gradient.setColorAt(1., st::premiumButtonBg1->c);
	for (auto i = 0; i < int(icons.size()); i++) {
		const auto &iconContainer = iconContainers[i];

		const auto pointTop = iconContainer->y() - from;
		const auto pointBottom = pointTop + iconContainer->height();
		const auto ratioTop = pointTop / float64(to - from);
		const auto ratioBottom = pointBottom / float64(to - from);

		auto resultGradient = QLinearGradient(
			QPointF(),
			QPointF(0, pointBottom - pointTop));

		resultGradient.setColorAt(
			.0,
			anim::gradient_color_at(gradient, ratioTop));
		resultGradient.setColorAt(
			.1,
			anim::gradient_color_at(gradient, ratioBottom));

		const auto brush = QBrush(resultGradient);
		AddButtonIcon(
			iconContainer,
			st,
			{ .icon = icons[i], .backgroundBrush = brush });
	}

	AddSkip(content);
	AddDivider(content);
	AddSkip(content);

	content->add(
		object_ptr<Ui::FlatLabel>(
			content,
			tr::lng_premium_summary_bottom_subtitle(
			) | rpl::map(Ui::Text::Bold),
			stLabel),
		st::settingsSubsectionTitlePadding);
	content->add(
		object_ptr<Ui::FlatLabel>(
			content,
			tr::lng_premium_summary_bottom_about(Ui::Text::RichLangValue),
			st::aboutLabel),
		st::boxRowPadding);
	AddSkip(content);

	Ui::ResizeFitChild(this, content);

}

QPointer<Ui::RpWidget> Premium::createPinnedToBottom(
		not_null<Ui::RpWidget*> parent) {

	const auto content = Ui::CreateChild<Ui::VerticalLayout>(parent.get());

	auto result = object_ptr<Ui::GradientButton>(
		content,
		QGradientStops{
			{ 0., st::premiumButtonBg1->c },
			{ .6, st::premiumButtonBg2->c },
			{ 1., st::premiumButtonBg3->c },
		});

	const auto &st = st::premiumPreviewBox.button;
	result->resize(content->width(), st.height);

	const auto label = Ui::CreateChild<Ui::FlatLabel>(
		result.data(),
		tr::lng_premium_summary_button(tr::now, lt_cost, "$5"),
		st::premiumPreviewButtonLabel);
	label->setAttribute(Qt::WA_TransparentForMouseEvents);
	rpl::combine(
		result->widthValue(),
		label->widthValue()
	) | rpl::start_with_next([=](int outer, int width) {
		label->moveToLeft(
			(outer - width) / 2,
			st::premiumPreviewBox.button.textTop,
			outer);
	}, label->lifetime());
	content->add(std::move(result), st::settingsPremiumButtonPadding);

	return Ui::MakeWeak(not_null<Ui::RpWidget*>{ content });
}

} // namespace

Type PremiumId() {
	return Premium::Id();
}

} // namespace Settings
