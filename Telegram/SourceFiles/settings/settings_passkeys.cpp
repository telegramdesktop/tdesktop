/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/settings_passkeys.h"

#include "settings/cloud_password/settings_cloud_password_common.h"
#include "settings/settings_common_session.h"
#include "data/components/passkeys.h"
#include "data/data_session.h"
#include "window/window_session_controller.h"
#include "main/main_session.h"
#include "platform/platform_webauthn.h"
#include "ui/layers/generic_box.h"
#include "ui/boxes/confirm_box.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "ui/text/text_utilities.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/menu/menu_add_action_callback.h"
#include "ui/widgets/menu/menu_add_action_callback_factory.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/wrap/slide_wrap.h"
#include "lang/lang_keys.h"
#include "lottie/lottie_icon.h"
#include "base/unixtime.h"
#include "data/stickers/data_custom_emoji.h"
#include "ui/text/custom_emoji_instance.h"
#include "styles/style_boxes.h"
#include "styles/style_channel_earn.h"
#include "styles/style_chat.h"
#include "styles/style_layers.h"
#include "styles/style_premium.h"
#include "styles/style_settings.h"
#include "styles/style_menu_icons.h"

namespace Settings {

class Passkeys : public Section<Passkeys> {
public:
	Passkeys(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	[[nodiscard]] rpl::producer<QString> title() override;

	[[nodiscard]] rpl::producer<> showFinishes() const {
		return _showFinished.events();
	}

	const Ui::RoundRect *bottomSkipRounding() const override {
		return &_bottomSkipRounding;
	}

private:
	void showFinished() override {
		_showFinished.fire({});
	}

	void setupContent(not_null<Window::SessionController*> controller);

	Ui::RoundRect _bottomSkipRounding;

	rpl::event_stream<> _showFinished;

};

void PasskeysNoneBox(
		not_null<Ui::GenericBox*> box,
		not_null<Main::Session*> session) {
	box->setWidth(st::boxWideWidth);
	box->setNoContentMargin(true);
	box->setCloseByEscape(true);
	box->addTopButton(st::boxTitleClose, [=] { box->closeBox(); });

	const auto content = box->verticalLayout().get();

	Ui::AddSkip(content);
	{
		const auto &size = st::settingsCloudPasswordIconSize;
		auto icon = CreateLottieIcon(
			content,
			{ .name = u"passkeys"_q, .sizeOverride = { size, size } },
			st::settingLocalPasscodeIconPadding);
		const auto animate = std::move(icon.animate);
		box->addRow(std::move(icon.widget), style::al_top);
		box->showFinishes() | rpl::take(1) | rpl::on_next([=] {
			animate(anim::repeat::once);
		}, content->lifetime());
	}
	Ui::AddSkip(content);
	Ui::AddSkip(content);
	box->addRow(
		object_ptr<Ui::FlatLabel>(
			content,
			tr::lng_settings_passkeys_none_title(),
			st::boxTitle),
		style::al_top);
	Ui::AddSkip(content);
	box->addRow(
		object_ptr<Ui::FlatLabel>(
			content,
			tr::lng_settings_passkeys_none_about(),
			st::channelEarnLearnDescription),
		style::al_top);
	Ui::AddSkip(content);
	Ui::AddSkip(content);
	Ui::AddSkip(content);
	{
		const auto padding = QMargins(
			st::settingsButton.padding.left(),
			st::boxRowPadding.top(),
			st::boxRowPadding.right(),
			st::boxRowPadding.bottom());
		const auto iconLeft = st::settingsButton.iconLeft;
		const auto addEntry = [&](
				rpl::producer<QString> title,
				rpl::producer<QString> about,
				const style::icon &icon) {
			const auto top = content->add(
				object_ptr<Ui::FlatLabel>(
					content,
					std::move(title),
					st::channelEarnSemiboldLabel),
				padding);
			Ui::AddSkip(content, st::channelEarnHistoryThreeSkip);
			content->add(
				object_ptr<Ui::FlatLabel>(
					content,
					std::move(about),
					st::channelEarnHistoryRecipientLabel),
				padding);
			const auto left = Ui::CreateChild<Ui::RpWidget>(
				box->verticalLayout().get());
			left->paintRequest(
			) | rpl::on_next([=] {
				auto p = Painter(left);
				icon.paint(p, 0, 0, left->width());
			}, left->lifetime());
			left->resize(icon.size());
			top->geometryValue(
			) | rpl::on_next([=](const QRect &g) {
				left->moveToLeft(
					iconLeft,
					g.top() + st::channelEarnHistoryThreeSkip);
			}, left->lifetime());
		};
		addEntry(
			tr::lng_settings_passkeys_none_info1_title(),
			tr::lng_settings_passkeys_none_info1_about(),
			st::settingsIconPasskeysAboutIcon1);
		Ui::AddSkip(content);
		Ui::AddSkip(content);
		addEntry(
			tr::lng_settings_passkeys_none_info2_title(),
			tr::lng_settings_passkeys_none_info2_about(),
			st::settingsIconPasskeysAboutIcon2);
		Ui::AddSkip(content);
		Ui::AddSkip(content);
		addEntry(
			tr::lng_settings_passkeys_none_info3_title(),
			tr::lng_settings_passkeys_none_info3_about(),
			st::settingsIconPasskeysAboutIcon3);
		Ui::AddSkip(content);
		Ui::AddSkip(content);
	}
	Ui::AddSkip(content);
	Ui::AddSkip(content);
	{
		const auto &st = st::premiumPreviewDoubledLimitsBox;
		const auto canRegister = session->passkeys().canRegister();
		box->setStyle(st);
		auto button = object_ptr<Ui::RoundButton>(
			box,
			canRegister
				? tr::lng_settings_passkeys_none_button()
				: tr::lng_settings_passkeys_none_button_unsupported(),
			st::defaultActiveButton);
		button->setTextTransform(Ui::RoundButton::TextTransform::NoTransform);
		button->resizeToWidth(box->width()
			- st.buttonPadding.left()
			- st.buttonPadding.left());
		button->setClickedCallback([=] {
			session->passkeys().initRegistration([=](
					const Data::Passkey::RegisterData &data) {
				Platform::WebAuthn::RegisterKey(data, [=](
						Platform::WebAuthn::RegisterResult result) {
					if (!result.success) {
						using Error = Platform::WebAuthn::Error;
						if (result.error == Error::UnsignedBuild) {
							box->uiShow()->showToast(
								tr::lng_settings_passkeys_unsigned_error(
									tr::now));
						}
						return;
					}
					session->passkeys().registerPasskey(result, [=] {
						box->closeBox();
					});
				});
			});
		});
		if (!canRegister) {
			button->setAttribute(Qt::WA_TransparentForMouseEvents);
			button->setTextFgOverride(
				anim::with_alpha(button->st().textFg->c, 0.5));
		}
		box->addButton(std::move(button));
	}
}

Passkeys::Passkeys(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent)
, _bottomSkipRounding(st::boxRadius, st::boxDividerBg) {
	setupContent(controller);
}

rpl::producer<QString> Passkeys::title() {
	return tr::lng_settings_passkeys_title();
}

void Passkeys::setupContent(
		not_null<Window::SessionController*> controller) {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	const auto session = &controller->session();

	CloudPassword::SetupHeader(
		content,
		u"passkeys"_q,
		showFinishes(),
		rpl::single(QString()),
		tr::lng_settings_passkeys_about());

	Ui::AddSkip(content);

	const auto container = content->add(
		object_ptr<Ui::VerticalLayout>(content));

	const auto &st = st::peerListBoxItem;
	const auto nameStyle = &st.nameStyle;
	const auto rebuild = [=] {
		while (container->count()) {
			delete container->widgetAt(0);
		}
		for (const auto &passkey : session->passkeys().list()) {
			const auto button = container->add(
				object_ptr<Ui::AbstractButton>(container));
			button->resize(button->width(), st.height);
			const auto menu = Ui::CreateChild<Ui::IconButton>(
				button,
				st::themesMenuToggle);
			menu->setClickedCallback([=] {
				const auto popup = Ui::CreateChild<Ui::PopupMenu>(
					menu,
					st::popupMenuWithIcons);
				const auto handler = [=, id = passkey.id] {
					controller->show(Ui::MakeConfirmBox({
						.text = rpl::combine(
							tr::lng_settings_passkeys_delete_sure_about(),
							tr::lng_settings_passkeys_delete_sure_about2()
						) | rpl::map([](QString a, QString b) {
							return a + "\n\n" + b;
						}),
						.confirmed = [=](Fn<void()> close) {
							session->passkeys().deletePasskey(
								id,
								close,
								[](QString) {});
						},
						.confirmText = tr::lng_box_delete(),
						.confirmStyle = &st::attentionBoxButton,
						.title
							= tr::lng_settings_passkeys_delete_sure_title(),
					}));
				};
				Ui::Menu::CreateAddActionCallback(popup)({
					.text = tr::lng_proxy_menu_delete(tr::now),
					.handler = handler,
					.icon = &st::menuIconDeleteAttention,
					.isAttention = true,
				});
				popup->setForcedOrigin(Ui::PanelAnimation::Origin::TopRight);
				const auto menuGlobal = menu->mapToGlobal(
					QPoint(menu->width(), menu->height()));
				popup->popup(menuGlobal);
			});
			button->widthValue() | rpl::on_next([=](int width) {
				menu->moveToRight(0, (st.height - menu->height()) / 2, width);
			}, button->lifetime());
			const auto iconSize = st::settingsIconPasskeys.width();
			const auto emoji = iconSize;
			const auto iconLeft = st::settingsButton.iconLeft;
			auto emojiInstance = passkey.softwareEmojiId
				? session->data().customEmojiManager().create(
					passkey.softwareEmojiId,
					[=] { button->update(); },
					Data::CustomEmojiSizeTag::Large,
					emoji)
				: nullptr;
			const auto emojiPtr = emojiInstance.get();
			button->lifetime().add([emoji = std::move(emojiInstance)] {});
			const auto formatDateTime = [](TimeId timestamp) {
				const auto dt = base::unixtime::parse(timestamp);
				return tr::lng_mediaview_date_time(
					tr::now,
					lt_date,
					langDayOfMonthFull(dt.date()),
					lt_time,
					QLocale().toString(dt.time(), QLocale::ShortFormat));
			};
			const auto date = (passkey.lastUsageDate > 0)
				? tr::lng_settings_passkeys_last_used(
					tr::now,
					lt_date,
					formatDateTime(passkey.lastUsageDate))
				: tr::lng_settings_passkeys_created(
					tr::now,
					lt_date,
					formatDateTime(passkey.date));
			const auto nameText = button->lifetime().make_state<
				Ui::Text::String>(
					*nameStyle,
					passkey.name.isEmpty()
						? tr::lng_settings_passkey_unknown(tr::now)
						: passkey.name);
			const auto dateText = button->lifetime().make_state<
				Ui::Text::String>(st::defaultTextStyle, date);
			button->paintOn([=](QPainter &p) {
				const auto iconTop = (st.height - iconSize) / 2;
				if (emojiPtr) {
					emojiPtr->paint(p, {
						.textColor = st.nameFg->c,
						.now = crl::now(),
						.position = QPoint(iconLeft, iconTop),
					});
				} else {
					const auto w = button->width();
					st::settingsIconPasskeys.paint(p, iconLeft, iconTop, w);
				}
				const auto textLeft = st::settingsButton.padding.left();
				const auto textWidth = button->width() - textLeft
					- st::settingsButton.padding.right();
				p.setPen(st.nameFg);
				nameText->draw(p, {
					.position = { textLeft, st.namePosition.y() },
					.outerWidth = button->width(),
					.availableWidth = textWidth,
					.elisionLines = 1,
				});
				p.setPen(st.statusFg);
				dateText->draw(p, {
					.position = { textLeft, st.statusPosition.y() },
					.outerWidth = button->width(),
					.availableWidth = textWidth,
					.elisionLines = 1,
				});
			});
			button->showChildren();
		}
		container->showChildren();
		container->resizeToWidth(content->width());
	};

	const auto buttonWrap = content->add(
		object_ptr<Ui::SlideWrap<Ui::SettingsButton>>(
			content,
			CreateButtonWithIcon(
				content,
				tr::lng_settings_passkeys_button(),
				st::settingsButtonActive,
				{ &st::settingsIconPasskeys })));
	buttonWrap->entity()->setClickedCallback([=] {
		controller->show(Box(PasskeysNoneBox, session));
	});
	buttonWrap->toggleOn(session->passkeys().requestList(
	) | rpl::map([=] { return session->passkeys().canRegister(); }));
	buttonWrap->finishAnimating();

	session->passkeys().requestList(
	) | rpl::on_next(rebuild, content->lifetime());
	rebuild();

	Ui::AddSkip(content);
	const auto label = Ui::AddDividerText(
		content,
		tr::lng_settings_passkeys_button_about(
			lt_link,
			tr::lng_channel_earn_about_link(
				lt_emoji,
				rpl::single(Ui::Text::IconEmoji(&st::textMoreIconEmoji)),
				tr::rich
			) | rpl::map([](TextWithEntities text) {
				return tr::link(std::move(text), u"internal"_q);
			}),
			tr::rich
		));
	label->setClickHandlerFilter([=](const auto &...) {
		controller->show(Box(PasskeysNoneBox, session));
		return false;
	});
	Ui::ResizeFitChild(this, content);
}

Type PasskeysId() {
	return Passkeys::Id();
}

} // namespace Settings
