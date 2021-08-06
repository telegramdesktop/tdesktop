/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "calls/group/calls_group_common.h"

#include "base/platform/base_platform_info.h"
#include "ui/widgets/labels.h"
#include "ui/layers/generic_box.h"
#include "ui/text/text_utilities.h"
#include "lang/lang_keys.h"
#include "styles/style_layers.h"
#include "styles/style_calls.h"

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

} // namespace Calls::Group
