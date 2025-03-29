/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "calls/group/calls_group_common.h"

#include "base/platform/base_platform_info.h"
#include "boxes/share_box.h"
#include "core/local_url_handlers.h"
#include "data/data_group_call.h"
#include "info/bot/starref/info_bot_starref_common.h"
#include "ui/boxes/boost_box.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/layers/generic_box.h"
#include "ui/text/text_utilities.h"
#include "ui/vertical_list.h"
#include "lang/lang_keys.h"
#include "window/window_session_controller.h"
#include "styles/style_layers.h"
#include "styles/style_calls.h"
#include "styles/style_chat.h"

#include <QtWidgets/QApplication>
#include <QtGui/QClipboard>

namespace Calls::Group {

object_ptr<Ui::GenericBox> ScreenSharingPrivacyRequestBox() {
#ifdef Q_OS_MAC
	if (!Platform::IsMac10_15OrGreater()) {
		return { nullptr };
	}
	return Box([=](not_null<Ui::GenericBox*> box) {
		box->addRow(
			object_ptr<Ui::FlatLabel>(
				box.get(),
				rpl::combine(
					tr::lng_group_call_mac_screencast_access(),
					tr::lng_group_call_mac_recording()
				) | rpl::map([](QString a, QString b) {
					auto result = Ui::Text::RichLangValue(a);
					result.append("\n\n").append(Ui::Text::RichLangValue(b));
					return result;
				}),
				st::groupCallBoxLabel),
			style::margins(
				st::boxRowPadding.left(),
				st::boxPadding.top(),
				st::boxRowPadding.right(),
				st::boxPadding.bottom()));
		box->addButton(tr::lng_group_call_mac_settings(), [=] {
			Platform::OpenDesktopCapturePrivacySettings();
		});
		box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	});
#else // Q_OS_MAC
	return { nullptr };
#endif // Q_OS_MAC
}

void ConferenceCallJoinConfirm(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<Data::GroupCall> call,
		Fn<void()> join) {
	box->setTitle(tr::lng_confcall_join_title());

	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			tr::lng_confcall_join_text(),
			st::boxLabel));

	box->addButton(tr::lng_confcall_join_button(), [=] {
		const auto weak = Ui::MakeWeak(box);
		join();
		if (const auto strong = weak.data()) {
			strong->closeBox();
		}
	});
	box->addButton(tr::lng_cancel(), [=] {
		box->closeBox();
	});
}

void ShowConferenceCallLinkBox(
		not_null<Window::SessionController*> controller,
		std::shared_ptr<Data::GroupCall> call,
		const QString &link,
		bool initial) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setStyle(st::confcallLinkBox);
		box->setWidth(st::boxWideWidth);
		box->setNoContentMargin(true);
		box->addTopButton(st::boxTitleClose, [=] {
			box->closeBox();
		});

		box->addRow(
			Info::BotStarRef::CreateLinkHeaderIcon(box, &call->session()),
			st::boxRowPadding + st::confcallLinkHeaderIconPadding);
		box->addRow(
			object_ptr<Ui::CenterWrap<Ui::FlatLabel>>(
				box,
				object_ptr<Ui::FlatLabel>(
					box,
					tr::lng_confcall_link_title(),
					st::boxTitle)),
			st::boxRowPadding + st::confcallLinkTitlePadding);
		box->addRow(
			object_ptr<Ui::FlatLabel>(
				box,
				tr::lng_confcall_link_about(),
				st::confcallLinkCenteredText),
			st::boxRowPadding
		)->setTryMakeSimilarLines(true);

		Ui::AddSkip(box->verticalLayout(), st::defaultVerticalListSkip * 2);
		const auto preview = box->addRow(
			Info::BotStarRef::MakeLinkLabel(box, link));
		Ui::AddSkip(box->verticalLayout());

		const auto copyCallback = [=] {
			QApplication::clipboard()->setText(link);
			box->uiShow()->showToast(tr::lng_username_copied(tr::now));
		};
		const auto shareCallback = [=] {
			FastShareLink(controller, link);
		};
		preview->setClickedCallback(copyCallback);
		[[maybe_unused]] const auto share = box->addButton(
			tr::lng_group_invite_share(),
			shareCallback,
			st::confcallLinkShareButton);
		[[maybe_unused]] const auto copy = box->addButton(
			tr::lng_group_invite_copy(),
			copyCallback,
			st::confcallLinkCopyButton);

		rpl::combine(
			box->widthValue(),
			copy->widthValue(),
			share->widthValue()
		) | rpl::start_with_next([=] {
			const auto width = st::boxWideWidth;
			const auto padding = st::confcallLinkBox.buttonPadding;
			const auto available = width - 2 * padding.right();
			const auto buttonWidth = (available - padding.left()) / 2;
			copy->resizeToWidth(buttonWidth);
			share->resizeToWidth(buttonWidth);
			copy->moveToLeft(padding.right(), copy->y(), width);
			share->moveToRight(padding.right(), share->y(), width);
		}, box->lifetime());

		const auto sep = Ui::CreateChild<Ui::FlatLabel>(
			copy->parentWidget(),
			tr::lng_confcall_link_or(),
			st::confcallLinkFooterOr);
		sep->paintRequest() | rpl::start_with_next([=] {
			auto p = QPainter(sep);
			const auto text = sep->textMaxWidth();
			const auto white = (sep->width() - 2 * text) / 2;
			const auto line = st::lineWidth;
			const auto top = st::confcallLinkFooterOrLineTop;
			const auto fg = st::windowSubTextFg->b;
			p.setOpacity(0.4);
			p.fillRect(0, top, white, line, fg);
			p.fillRect(sep->width() - white, top, white, line, fg);
		}, sep->lifetime());

		const auto footer = Ui::CreateChild<Ui::FlatLabel>(
			copy->parentWidget(),
			tr::lng_confcall_link_join(
				lt_link,
				tr::lng_confcall_link_join_link(
					lt_arrow,
					rpl::single(Ui::Text::IconEmoji(&st::textMoreIconEmoji)),
					[](QString v) { return Ui::Text::Link(v); }),
				Ui::Text::WithEntities),
			st::confcallLinkCenteredText);
		footer->setTryMakeSimilarLines(true);
		footer->setClickHandlerFilter([=](const auto &...) {
			const auto local = Core::TryConvertUrlToLocal(link);
			controller->resolveConferenceCall(
				local,
				crl::guard(box, [=](bool ok) { if (ok) box->closeBox(); }),
				true);
			return false;
		});
		copy->geometryValue() | rpl::start_with_next([=](QRect geometry) {
			const auto width = st::boxWideWidth
				- st::boxRowPadding.left()
				- st::boxRowPadding.right();
			footer->resizeToWidth(width);
			const auto top = geometry.y()
				+ geometry.height()
				+ st::confcallLinkFooterOrTop;
			sep->resizeToWidth(width / 2);
			sep->move(
				st::boxRowPadding.left() + (width - sep->width()) / 2,
				top);
			footer->moveToLeft(
				st::boxRowPadding.left(),
				top + sep->height() + st::confcallLinkFooterOrSkip);
		}, footer->lifetime());
	}));
}

} // namespace Calls::Group
