/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "chat_helpers/rich_paste_toast.h"

#include "boxes/premium_preview_box.h"
#include "iv/editor/iv_editor_clipboard_import.h"
#include "iv/editor/iv_editor_session.h"
#include "iv/iv_rich_page.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "window/window_session_controller.h"
#include "ui/text/text_utilities.h"
#include "ui/toast/toast.h"
#include "ui/toast/toast_widget.h"
#include "ui/widgets/buttons.h"
#include "styles/style_chat.h"
#include "styles/style_chat_helpers.h"

#include <QtCore/QMimeData>

namespace ChatHelpers {
namespace {

constexpr auto kToastDuration = 9 * crl::time(1000);

} // namespace

bool MimeDataLosesRichFormatting(
		not_null<Main::Session*> session,
		not_null<const QMimeData*> data) {
	if (!Iv::Editor::CanAuthorRichMessages(session)) {
		return false;
	}
	const auto limits = Iv::ResolveRichMessageLimits(session);
	if (Iv::Editor::MimeDataHasRichStructure(session, data, limits)) {
		return true;
	}
	return !data->hasHtml()
		&& data->hasText()
		&& Iv::Editor::TextHasMarkdownStructure(data->text(), limits);
}

std::shared_ptr<QMimeData> CloneMimeData(not_null<const QMimeData*> data) {
	auto result = std::make_shared<QMimeData>();
	for (const auto &format : data->formats()) {
		result->setData(format, data->data(format));
	}
	return result;
}

void ShowRichPasteToast(RichPasteToastArgs &&args) {
	const auto session = args.session;
	const auto editor = (args.offer == RichPasteOffer::Editor);
	const auto locked = editor && !Iv::Editor::SessionPremium(session);
	const auto button = locked
		? QString()
		: editor
		? tr::lng_rich_paste_toast_open(tr::now)
		: tr::lng_rich_paste_toast_undo(tr::now);
	auto text = tr::bold(editor
		? tr::lng_rich_paste_toast(tr::now)
		: tr::lng_rich_paste_toast_markdown(tr::now)
	).append('\n').append(locked
		? tr::lng_rich_paste_toast_premium(
			tr::now,
			lt_link,
			tr::link(tr::bold(
				tr::lng_rich_paste_toast_premium_link(tr::now))),
			tr::marked)
		: editor
		? tr::lng_rich_paste_toast_editor(tr::now, tr::rich)
		: tr::lng_rich_paste_toast_plain(tr::now, tr::rich));
	auto filter = Ui::Toast::ClickHandlerFilter();
	if (locked) {
		filter = [=](const ClickHandlerPtr &handler, Qt::MouseButton mouse) {
			if (mouse != Qt::LeftButton) {
				return false;
			} else if (auto show = Iv::Editor::ActiveWindowShow(session)) {
				ShowPremiumPreviewToBuy(
					std::move(show),
					PremiumFeature::RichFormatting);
			} else if (const auto window = session->tryResolveWindow()) {
				ShowPremiumPreviewToBuy(
					window,
					PremiumFeature::RichFormatting);
			}
			return true;
		};
	}
	const auto st = std::make_shared<style::Toast>(st::historyPremiumToast);
	if (!button.isEmpty()) {
		st->padding.setRight(
			st::historyPremiumViewSet.style.font->width(button)
			- st::historyPremiumViewSet.width);
	}
	const auto weak = Ui::Toast::Show(args.parent, Ui::Toast::Config{
		.text = std::move(text),
		.filter = std::move(filter),
		.similarLines = true,
		.iconLottie = (locked ? u"toast/star_premium_2"_q : QString()),
		.iconLottieSize = st::toastLottieIconSize,
		.st = st.get(),
		.attach = RectPart::Bottom,
		.addToAttachSide = std::move(args.bottomOffset),
		.acceptinput = true,
		.duration = kToastDuration,
	});
	const auto strong = weak.get();
	if (!strong) {
		return;
	}
	const auto widget = strong->widget();
	widget->lifetime().add([st] {});
	if (args.cancel) {
		std::move(args.cancel) | rpl::on_next([=] {
			if (const auto strong = weak.get()) {
				strong->hideAnimated();
			}
		}, widget->lifetime());
	}
	if (button.isEmpty()) {
		return;
	}
	const auto activate = Ui::CreateChild<Ui::RoundButton>(
		widget.get(),
		rpl::single(button),
		st::historyPremiumViewSet);
	activate->show();
	activate->setClickedCallback([=, action = std::move(args.action)] {
		if (const auto strong = weak.get()) {
			strong->hideAnimated();
		}
		if (action) {
			action();
		}
	});
	rpl::combine(
		widget->sizeValue(),
		activate->sizeValue()
	) | rpl::on_next([=](QSize outer, QSize inner) {
		activate->moveToRight(
			0,
			(outer.height() - inner.height()) / 2,
			outer.width());
	}, widget->lifetime());
}

} // namespace ChatHelpers
