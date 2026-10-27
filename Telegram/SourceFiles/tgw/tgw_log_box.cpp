/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "tgw/tgw_log_box.h"

#include "core/update_channel.h"
#include "lang/lang_keys.h"
#include "tgw/tgw_runtime.h"
#include "ui/layers/generic_box.h"
#include "ui/layers/show.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/popup_menu.h"

#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"

namespace Tgw {
namespace {

#ifdef _DEBUG
constexpr auto kLogAvailable = true;
#else
constexpr auto kLogAvailable = Core::BuildIsCanary;
#endif // _DEBUG

void LogBox(not_null<Ui::GenericBox*> box, base::weak_ptr<Runtime> weak) {
	const auto text = box->lifetime().make_state<rpl::variable<QString>>();
	if (const auto runtime = weak.get()) {
		runtime->requestLog(crl::guard(box, [=](const QString &value) {
			*text = value;
		}));
		*text = runtime->logText();
	}
	box->setTitle(tr::lng_tgw_log());
	box->setWidth(st::boxWideWidth);
	const auto label = box->addRow(
		object_ptr<Ui::FlatLabel>(box, text->value(), st::boxLabel));
	label->setSelectable(true);
	box->addButton(tr::lng_close(), [=] {
		box->closeBox();
	});
	box->addLeftButton(tr::lng_context_copy_text(), [=] {
		TextUtilities::SetClipboardText(
			TextForMimeData::Simple(text->current()));
		box->showToast(tr::lng_text_copied(tr::now));
	});
}

} // namespace

void AddLogAction(
		not_null<Ui::PopupMenu*> menu,
		not_null<const HistoryView::Element*> view,
		std::shared_ptr<Ui::Show> show) {
	const auto runtime = kLogAvailable ? FindRuntime(view) : nullptr;
	if (!runtime) {
		return;
	}
	const auto weak = base::make_weak(runtime);
	menu->addAction(tr::lng_tgw_log(tr::now), [=] {
		if (weak) {
			show->showBox(Box(LogBox, weak));
		}
	}, &st::menuIconInfo);
}

} // namespace Tgw
