/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/controls/history_view_compose_stash_hint.h"

#include "core/shortcuts.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "main/main_session_settings.h"
#include "ui/toast/toast.h"
#include "ui/widgets/buttons.h"

#include "styles/style_chat.h"

#include <xxhash.h> // XXH64.

namespace HistoryView::Controls {
namespace {

constexpr auto kMinLength = 20;
constexpr auto kMemoryTtl = crl::time(120000);
constexpr auto kToastDuration = crl::time(5000);

[[nodiscard]] uint64_t TextHash(const QString &text) {
	return XXH64(text.data(), text.size() * sizeof(ushort), 0);
}

[[nodiscard]] QString ShortcutText() {
	for (const auto &[keys, commands] : Shortcuts::KeysCurrents()) {
		if (commands.contains(Shortcuts::Command::StashMessage)) {
			auto result = keys.toString();
#ifdef Q_OS_MAC
			result = result.replace(u"Ctrl+"_q, QString() + QChar(0x2318));
			result = result.replace(u"Meta+"_q, QString() + QChar(0x2303));
			result = result.replace(u"Alt+"_q, QString() + QChar(0x2325));
			result = result.replace(u"Shift+"_q, QString() + QChar(0x21E7));
#endif // Q_OS_MAC
			return result;
		}
	}
	return u"Ctrl+S"_q;
}

} // namespace

StashHintManager::StashHintManager(StashHintDescriptor &&descriptor)
: _data(std::move(descriptor)) {
}

StashHintManager::~StashHintManager() = default;

void StashHintManager::trackChange(bool userDriven) {
	if (_memory && (crl::now() - _memory->at) > kMemoryTtl) {
		_memory.reset();
	}
	const auto text = _data.fieldText();
	if (!userDriven) {
		if (text.isEmpty()) {
			_peakLength = 0;
		} else {
			_peakLength = text.size();
			_peakHash = TextHash(text);
		}
		return;
	}
	if (!text.isEmpty()) {
		if (text.size() > _peakLength) {
			_peakLength = text.size();
			_peakHash = TextHash(text);
		}
		hide();
		if (_memory
			&& text.size() == _memory->length
			&& TextHash(text) == _memory->hash
			&& _data.canUse()) {
			_memory.reset();
			maybeShow();
		}
		return;
	}
	if (_peakLength >= kMinLength) {
		_memory = CutMemory{ _peakHash, _peakLength, crl::now() };
	}
	_peakLength = 0;
}

void StashHintManager::markUsed() {
	hide();
	auto &settings = _data.session->settings();
	if (settings.shouldShowStashHint()) {
		settings.markStashHintUsed();
		_data.session->saveSettings();
	}
}

void StashHintManager::hide() {
	if (const auto strong = _toast.get()) {
		strong->hideAnimated();
	}
	_toast = nullptr;
}

void StashHintManager::maybeShow() {
	auto &settings = _data.session->settings();
	if (!settings.shouldShowStashHint()) {
		return;
	}
	const auto parent = _data.toastParent();
	if (!parent) {
		return;
	}
	const auto button = tr::lng_archive_hint_button(tr::now);
	const auto st = std::make_shared<style::Toast>(st::historyPremiumToast);
	st->padding.setRight(
		st::historyPremiumViewSet.style.font->width(button)
		- st::historyPremiumViewSet.width);
	_toast = Ui::Toast::Show(parent, Ui::Toast::Config{
		.text = tr::lng_stash_hint(
			tr::now,
			lt_shortcut,
			tr::bold(ShortcutText()),
			tr::marked),
		.similarLines = true,
		.st = st.get(),
		.attach = RectPart::Bottom,
		.acceptinput = true,
		.duration = kToastDuration,
	});
	const auto strong = _toast.get();
	if (!strong) {
		return;
	}
	const auto widget = strong->widget();
	widget->lifetime().add([st] {});
	const auto activate = Ui::CreateChild<Ui::RoundButton>(
		widget.get(),
		rpl::single(button),
		st::historyPremiumViewSet);
	activate->show();
	activate->setClickedCallback(crl::guard(this, [=] {
		markUsed();
	}));
	rpl::combine(
		widget->sizeValue(),
		activate->sizeValue()
	) | rpl::on_next([=](QSize outer, QSize inner) {
		activate->moveToRight(
			0,
			(outer.height() - inner.height()) / 2,
			outer.width());
	}, widget->lifetime());
	settings.incrementStashHintShown();
	_data.session->saveSettings();
}

} // namespace HistoryView::Controls
