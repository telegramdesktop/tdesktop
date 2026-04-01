/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/about_box.h"

#include "base/platform/base_platform_info.h"
#include "core/application.h"
#include "core/file_utilities.h"
#include "core/update_checker.h"
#include "lang/lang_keys.h"
#include "ui/boxes/confirm_box.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "ui/text/text_utilities.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/wrap/vertical_layout.h"
#include "styles/style_layers.h"
#include "styles/style_boxes.h"
#include "styles/style_channel_earn.h"
#include "styles/style_chat.h"
#include "styles/style_dialogs.h"
#include "styles/style_menu_icons.h"
#include "styles/style_premium.h"
#include "styles/style_settings.h"

#include <QtGui/QGuiApplication>
#include <QtGui/QClipboard>

#include "window/window_session_controller.h"
#include "window/window_session_controller_link_info.h"

namespace {

rpl::producer<TextWithEntities> Text() {
	return tr::lng_about_text2(
		lt_gpl_link,
		rpl::single(Ui::Text::Link(
			"GNU GPL",
			"https://github.com/lavrentijav/TeleForge/blob/main/LICENSE")),
		lt_github_link,
		rpl::single(Ui::Text::Link(
			"GitHub",
			"https://github.com/lavrentijav/TeleForge")),
		tr::marked);
}

} // namespace

void AboutBox(not_null<Ui::GenericBox*> box, Window::SessionController* controller) {
	box->setTitle(rpl::single(u"TeleForge Desktop"_q));

	auto layout = box->verticalLayout();

	const auto version = layout->add(
		object_ptr<Ui::LinkButton>(
			box,
			tr::lng_about_version(
				tr::now,
				lt_version,
				currentVersionText()),
			st::aboutVersionLink),
		QMargins(
			st::boxRowPadding.left(),
			-st::lineWidth * 3,
			st::boxRowPadding.right(),
			st::boxRowPadding.bottom()));
	version->setClickedCallback([=] {
		File::OpenUrl(Core::App().changelogLink());
	});

	Ui::AddSkip(layout, st::aboutTopSkip);

	const auto addText = [&](rpl::producer<TextWithEntities> text) {
		const auto label = layout->add(
			object_ptr<Ui::FlatLabel>(box, std::move(text), st::aboutLabel),
			st::boxRowPadding);
		label->setLinksTrusted();
		Ui::AddSkip(layout, st::aboutSkip);
	};

	addText(Text());

	box->addButton(tr::lng_close(), [=] { box->closeBox(); });
	box->addLeftButton(
		rpl::single(QString("Official Releases")),
		[box, controller]
		{
			box->closeBox();
			controller->showPeerByLink(Window::PeerByLinkInfo{
				.usernameOrId = QString("ayugramreleases"),
			});
		});

	box->setWidth(st::aboutWidth);
}

QString currentVersionText() {
	auto result = QString::fromLatin1(AppVersionStr);
	if (cAlphaVersion()) {
		result += u" alpha %1"_q.arg(cAlphaVersion() % 1000);
	} else if (AppBetaVersion) {
		result += " beta";
	}
	if (Platform::IsWindows64Bit()) {
		result += " x64";
	} else if (Platform::IsWindowsARM64()) {
		result += " arm64";
	}
#ifdef _DEBUG
	result += " DEBUG";
#endif
	return result;
}

void ArchiveHintBox(
		not_null<Ui::GenericBox*> box,
		bool unarchiveOnNewMessage,
		Fn<void()> onUnarchive) {
	box->setNoContentMargin(true);

	const auto content = box->verticalLayout().get();

	Ui::AddSkip(content);
	Ui::AddSkip(content);
	Ui::AddSkip(content);
	{
		const auto &icon = st::dialogsArchiveUserpic;
		const auto rect = Rect(icon.size() * 2);
		auto owned = object_ptr<Ui::RpWidget>(content);
		owned->resize(rect.size());
		owned->setNaturalWidth(rect.width());
		const auto widget = box->addRow(std::move(owned), style::al_top);
		widget->paintRequest(
		) | rpl::on_next([=] {
			auto p = Painter(widget);
			auto hq = PainterHighQualityEnabler(p);
			p.setPen(Qt::NoPen);
			p.setBrush(st::activeButtonBg);
			p.drawEllipse(rect);
			icon.paintInCenter(p, rect);
		}, widget->lifetime());
	}
	Ui::AddSkip(content);
	Ui::AddSkip(content);
	box->addRow(
		object_ptr<Ui::FlatLabel>(
			content,
			tr::lng_archive_hint_title(),
			st::boxTitle),
		style::al_top);
	Ui::AddSkip(content);
	Ui::AddSkip(content);
	{
		const auto label = box->addRow(
			object_ptr<Ui::FlatLabel>(
				content,
				(unarchiveOnNewMessage
						? tr::lng_archive_hint_about_unmuted
						: tr::lng_archive_hint_about)(
					lt_link,
					tr::lng_archive_hint_about_link(
						lt_emoji,
						rpl::single(
							Ui::Text::IconEmoji(&st::textMoreIconEmoji)),
						tr::rich
					) | rpl::map([](TextWithEntities text) {
						return Ui::Text::Link(std::move(text), 1);
					}),
					tr::rich),
				st::channelEarnHistoryRecipientLabel));
		label->resizeToWidth(box->width()
			- rect::m::sum::h(st::boxRowPadding));
		label->setLink(
			1,
			std::make_shared<GenericClickHandler>([=](ClickContext context) {
				if (context.button == Qt::LeftButton) {
					onUnarchive();
				}
			}));
	}
	Ui::AddSkip(content);
	Ui::AddSkip(content);
	Ui::AddSkip(content);
	Ui::AddSkip(content);
	{
		const auto padding = QMargins(
			st::settingsButton.padding.left(),
			st::boxRowPadding.top(),
			st::boxRowPadding.right(),
			st::boxRowPadding.bottom());
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
					(g.left() - left->width()) / 2,
					g.top() + st::channelEarnHistoryThreeSkip);
			}, left->lifetime());
		};
		addEntry(
			tr::lng_archive_hint_section_1(),
			tr::lng_archive_hint_section_1_info(),
			st::menuIconArchive);
		Ui::AddSkip(content);
		Ui::AddSkip(content);
		addEntry(
			tr::lng_archive_hint_section_2(),
			tr::lng_archive_hint_section_2_info(),
			st::menuIconStealth);
		Ui::AddSkip(content);
		Ui::AddSkip(content);
		addEntry(
			tr::lng_archive_hint_section_3(),
			tr::lng_archive_hint_section_3_info(),
			st::menuIconStoriesSavedSection);
		Ui::AddSkip(content);
		Ui::AddSkip(content);
	}
	Ui::AddSkip(content);
	Ui::AddSkip(content);
	Ui::AddSkip(content);
	{
		const auto &st = st::premiumPreviewDoubledLimitsBox;
		box->setStyle(st);
		auto button = object_ptr<Ui::RoundButton>(
			box,
			tr::lng_archive_hint_button(),
			st::defaultActiveButton);
		button->setTextTransform(
			Ui::RoundButton::TextTransform::NoTransform);
		button->resizeToWidth(box->width()
			- st.buttonPadding.left()
			- st.buttonPadding.left());
		button->setClickedCallback([=] { box->closeBox(); });
		box->addButton(std::move(button));
	}
}

