/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "tgw/tgw_input.h"

#include "base/platform/base_platform_info.h"
#include "base/event_filter.h"
#include "base/qt_signal_producer.h"
#include "tgw/tgw_renderer.h"
#include "ui/style/style_core_scale.h"

#include <QtWidgets/QApplication>

namespace Tgw {
namespace {

constexpr auto kDomAlt = 1;
constexpr auto kDomCtrl = 2;
constexpr auto kDomMeta = 4;
constexpr auto kDomShift = 8;
constexpr auto kLastFunctionKey = int(Qt::Key_F24);
constexpr auto kUnidentified = std::string_view("Unidentified");
constexpr auto kCssPercent = 100.;
constexpr auto kNotchPixels = 100.;
constexpr auto kNotchAngle = 120.;

struct DomKey {
	std::string key;
	std::string code;
};

struct NamedKey {
	int qt = 0;
	std::string_view key;
	std::string_view code;
};

struct CodedKey {
	int qt = 0;
	std::string_view code;
};

struct KeyOwner {
	base::weak_ptr<RendererCore> core;
	QPointer<QObject> filter;
	QPointer<QWidget> focus;
	QPointer<QWidget> window;
	rpl::lifetime lifetime;
};

struct WheelGesture {
	base::weak_ptr<RendererCore> target;
	bool active = false;
	bool captured = false;
};

constexpr auto kNamedKeys = std::array{
	NamedKey{ Qt::Key_Left, "ArrowLeft", "ArrowLeft" },
	NamedKey{ Qt::Key_Right, "ArrowRight", "ArrowRight" },
	NamedKey{ Qt::Key_Up, "ArrowUp", "ArrowUp" },
	NamedKey{ Qt::Key_Down, "ArrowDown", "ArrowDown" },
	NamedKey{ Qt::Key_Home, "Home", "Home" },
	NamedKey{ Qt::Key_End, "End", "End" },
	NamedKey{ Qt::Key_PageUp, "PageUp", "PageUp" },
	NamedKey{ Qt::Key_PageDown, "PageDown", "PageDown" },
	NamedKey{ Qt::Key_Backspace, "Backspace", "Backspace" },
	NamedKey{ Qt::Key_Tab, "Tab", "Tab" },
	NamedKey{ Qt::Key_Backtab, "Tab", "Tab" },
	NamedKey{ Qt::Key_Return, "Enter", "Enter" },
	NamedKey{ Qt::Key_Enter, "Enter", "NumpadEnter" },
	NamedKey{ Qt::Key_Delete, "Delete", "Delete" },
	NamedKey{ Qt::Key_Insert, "Insert", "Insert" },
	NamedKey{ Qt::Key_Clear, "Clear", "NumpadClear" },
	NamedKey{ Qt::Key_Menu, "ContextMenu", "ContextMenu" },
	NamedKey{ Qt::Key_Pause, "Pause", "Pause" },
	NamedKey{ Qt::Key_Print, "PrintScreen", "PrintScreen" },
	NamedKey{ Qt::Key_Shift, "Shift", "ShiftLeft" },
	NamedKey{ Qt::Key_Alt, "Alt", "AltLeft" },
	NamedKey{ Qt::Key_AltGr, "AltGraph", "AltRight" },
	NamedKey{ Qt::Key_CapsLock, "CapsLock", "CapsLock" },
	NamedKey{ Qt::Key_NumLock, "NumLock", "NumLock" },
	NamedKey{ Qt::Key_ScrollLock, "ScrollLock", "ScrollLock" },
};

constexpr auto kKeypadCodes = std::array{
	CodedKey{ Qt::Key_Insert, "Numpad0" },
	CodedKey{ Qt::Key_End, "Numpad1" },
	CodedKey{ Qt::Key_Down, "Numpad2" },
	CodedKey{ Qt::Key_PageDown, "Numpad3" },
	CodedKey{ Qt::Key_Left, "Numpad4" },
	CodedKey{ Qt::Key_Clear, "Numpad5" },
	CodedKey{ Qt::Key_Right, "Numpad6" },
	CodedKey{ Qt::Key_Home, "Numpad7" },
	CodedKey{ Qt::Key_Up, "Numpad8" },
	CodedKey{ Qt::Key_PageUp, "Numpad9" },
	CodedKey{ Qt::Key_Delete, "NumpadDecimal" },
	CodedKey{ Qt::Key_Period, "NumpadDecimal" },
	CodedKey{ Qt::Key_Comma, "NumpadDecimal" },
	CodedKey{ Qt::Key_Asterisk, "NumpadMultiply" },
	CodedKey{ Qt::Key_Plus, "NumpadAdd" },
	CodedKey{ Qt::Key_Minus, "NumpadSubtract" },
	CodedKey{ Qt::Key_Slash, "NumpadDivide" },
	CodedKey{ Qt::Key_Enter, "NumpadEnter" },
	CodedKey{ Qt::Key_Return, "NumpadEnter" },
	CodedKey{ Qt::Key_Equal, "NumpadEqual" },
};

constexpr auto kPrintableCodes = std::array{
	CodedKey{ Qt::Key_Space, "Space" },
	CodedKey{ Qt::Key_Minus, "Minus" },
	CodedKey{ Qt::Key_Underscore, "Minus" },
	CodedKey{ Qt::Key_Equal, "Equal" },
	CodedKey{ Qt::Key_Plus, "Equal" },
	CodedKey{ Qt::Key_BracketLeft, "BracketLeft" },
	CodedKey{ Qt::Key_BraceLeft, "BracketLeft" },
	CodedKey{ Qt::Key_BracketRight, "BracketRight" },
	CodedKey{ Qt::Key_BraceRight, "BracketRight" },
	CodedKey{ Qt::Key_Backslash, "Backslash" },
	CodedKey{ Qt::Key_Bar, "Backslash" },
	CodedKey{ Qt::Key_Semicolon, "Semicolon" },
	CodedKey{ Qt::Key_Colon, "Semicolon" },
	CodedKey{ Qt::Key_Apostrophe, "Quote" },
	CodedKey{ Qt::Key_QuoteDbl, "Quote" },
	CodedKey{ Qt::Key_QuoteLeft, "Backquote" },
	CodedKey{ Qt::Key_AsciiTilde, "Backquote" },
	CodedKey{ Qt::Key_Comma, "Comma" },
	CodedKey{ Qt::Key_Less, "Comma" },
	CodedKey{ Qt::Key_Period, "Period" },
	CodedKey{ Qt::Key_Greater, "Period" },
	CodedKey{ Qt::Key_Slash, "Slash" },
	CodedKey{ Qt::Key_Question, "Slash" },
	CodedKey{ Qt::Key_Exclam, "Digit1" },
	CodedKey{ Qt::Key_At, "Digit2" },
	CodedKey{ Qt::Key_NumberSign, "Digit3" },
	CodedKey{ Qt::Key_Dollar, "Digit4" },
	CodedKey{ Qt::Key_Percent, "Digit5" },
	CodedKey{ Qt::Key_AsciiCircum, "Digit6" },
	CodedKey{ Qt::Key_Ampersand, "Digit7" },
	CodedKey{ Qt::Key_Asterisk, "Digit8" },
	CodedKey{ Qt::Key_ParenLeft, "Digit9" },
	CodedKey{ Qt::Key_ParenRight, "Digit0" },
};

KeyOwner Keys;
base::weak_ptr<RendererCore> WheelHovered;
WheelGesture Gesture;

[[nodiscard]] bool InRange(int key, int from, int till) {
	return (key >= from) && (key <= till);
}

[[nodiscard]] std::string Numbered(std::string_view prefix, int number) {
	return std::string(prefix) + std::to_string(number);
}

[[nodiscard]] const NamedKey *FindNamed(int key) {
	const auto i = ranges::find(kNamedKeys, key, &NamedKey::qt);
	return (i != end(kNamedKeys)) ? &*i : nullptr;
}

template <size_t Size>
[[nodiscard]] std::string_view FindCode(
		const std::array<CodedKey, Size> &table,
		int key) {
	const auto i = ranges::find(table, key, &CodedKey::qt);
	return (i != end(table)) ? i->code : std::string_view();
}

[[nodiscard]] std::string DomKeyName(int key, const QString &text) {
	if (const auto named = FindNamed(key)) {
		return std::string(named->key);
	} else if (InRange(key, Qt::Key_F1, kLastFunctionKey)) {
		return Numbered("F", key - Qt::Key_F1 + 1);
	} else if (!text.isEmpty()
		&& (text.at(0).category() != QChar::Other_Control)) {
		return text.toStdString();
	}
	return std::string(kUnidentified);
}

[[nodiscard]] std::string DomKeyCode(int key, bool keypad) {
	const auto named = FindNamed(key);
	if (keypad && !(Platform::IsMac() && named)) {
		if (InRange(key, Qt::Key_0, Qt::Key_9)) {
			return Numbered("Numpad", key - Qt::Key_0);
		}
		const auto code = FindCode(kKeypadCodes, key);
		if (!code.empty()) {
			return std::string(code);
		}
	}
	if (named) {
		return std::string(named->code);
	} else if (InRange(key, Qt::Key_A, Qt::Key_Z)) {
		return std::string("Key") + char('A' + (key - Qt::Key_A));
	} else if (InRange(key, Qt::Key_0, Qt::Key_9)) {
		return Numbered("Digit", key - Qt::Key_0);
	} else if (InRange(key, Qt::Key_F1, kLastFunctionKey)) {
		return Numbered("F", key - Qt::Key_F1 + 1);
	}
	const auto code = FindCode(kPrintableCodes, key);
	return std::string(code.empty() ? kUnidentified : code);
}

[[nodiscard]] DomKey LookupDomKey(not_null<const QKeyEvent*> e) {
	const auto key = e->key();
	const auto keypad = e->modifiers().testFlag(Qt::KeypadModifier);
	return {
		.key = DomKeyName(key, e->text()),
		.code = DomKeyCode(key, keypad),
	};
}

void ReleaseKeys() {
	const auto was = base::take(Keys);
	if (const auto filter = was.filter.data()) {
		qApp->removeEventFilter(filter);
		filter->deleteLater();
	}
}

[[nodiscard]] RendererCore *KeyTarget() {
	const auto core = Keys.core.get();
	const auto focus = QApplication::focusWidget();
	return (core
		&& core->takesKeys()
		&& core->acceptsInput()
		&& focus
		&& (focus == Keys.focus)
		&& Keys.window
		&& (QApplication::activeWindow() == Keys.window)
		&& !QApplication::activePopupWidget())
		? core
		: nullptr;
}

[[nodiscard]] bool PassesThrough(not_null<const QKeyEvent*> e) {
	const auto key = e->key();
	const auto modifiers = e->modifiers();
	return modifiers.testFlag(Qt::ControlModifier)
		|| modifiers.testFlag(Qt::MetaModifier)
		|| (key == Qt::Key_Control)
		|| (key == Qt::Key_Meta);
}

[[nodiscard]] base::EventFilterResult FilterKey(not_null<QKeyEvent*> e) {
	using Result = base::EventFilterResult;
	const auto type = e->type();
	const auto core = KeyTarget();
	if (!core) {
		ReleaseKeys();
		return Result::Continue;
	} else if (PassesThrough(e)) {
		return Result::Continue;
	} else if (type == QEvent::ShortcutOverride) {
		e->accept();
		return Result::Cancel;
	} else if (e->key() == Qt::Key_Escape) {
		if (type == QEvent::KeyPress) {
			ReleaseKeys();
		}
		return Result::Cancel;
	}
	const auto down = (type == QEvent::KeyPress);
	const auto repeat = e->isAutoRepeat();
	if (down || !repeat) {
		const auto key = LookupDomKey(e);
		core->key(
			down,
			key.key,
			key.code,
			repeat,
			DomModifiers(e->modifiers()));
	}
	return Result::Cancel;
}

[[nodiscard]] base::EventFilterResult FilterKeys(not_null<QEvent*> e) {
	switch (e->type()) {
	case QEvent::MouseButtonPress:
	case QEvent::MouseButtonDblClick:
	case QEvent::NonClientAreaMouseButtonPress:
	case QEvent::TouchBegin:
		ReleaseKeys();
		break;
	case QEvent::ShortcutOverride:
	case QEvent::KeyPress:
	case QEvent::KeyRelease:
		return FilterKey(static_cast<QKeyEvent*>(e.get()));
	case QEvent::InputMethod:
		if (KeyTarget()) {
			return base::EventFilterResult::Cancel;
		}
		ReleaseKeys();
		break;
	}
	return base::EventFilterResult::Continue;
}

[[nodiscard]] RendererCore *Accepting(RendererCore *core) {
	return (core && core->acceptsInput()) ? core : nullptr;
}

[[nodiscard]] bool Captures(RendererCore *core) {
	const auto accepting = Accepting(core);
	return accepting && accepting->wheelPrevented();
}

[[nodiscard]] QPointF WheelDelta(not_null<QWheelEvent*> e) {
	const auto pixels = e->pixelDelta();
	return pixels.isNull()
		? (QPointF(e->angleDelta()) * (-kNotchPixels / kNotchAngle))
		: (QPointF(pixels) * (-kCssPercent / style::Scale()));
}

void DeliverWheel(RendererCore *core, not_null<QWheelEvent*> e) {
	if (const auto accepting = Accepting(core)) {
		accepting->wheel(WheelDelta(e), DomModifiers(e->modifiers()));
	}
}

} // namespace

int DomModifiers(Qt::KeyboardModifiers modifiers) {
	const auto mac = Platform::IsMac();
	const auto bits = std::array{
		std::pair(Qt::AltModifier, kDomAlt),
		std::pair(mac ? Qt::MetaModifier : Qt::ControlModifier, kDomCtrl),
		std::pair(mac ? Qt::ControlModifier : Qt::MetaModifier, kDomMeta),
		std::pair(Qt::ShiftModifier, kDomShift),
	};
	auto result = 0;
	for (const auto &[modifier, bit] : bits) {
		if (modifiers.testFlag(modifier)) {
			result |= bit;
		}
	}
	return result;
}

void GrantKeys(not_null<RendererCore*> core) {
	ReleaseKeys();
	const auto focus = QApplication::focusWidget();
	const auto window = QApplication::activeWindow();
	if (!focus || !window || !core->takesKeys() || !core->acceptsInput()) {
		return;
	}
	Keys.core = core;
	Keys.focus = focus;
	Keys.window = window;
	Keys.filter = base::install_event_filter(qApp, FilterKeys).get();
	core->keysReleased() | rpl::on_next([] {
		ReleaseKeys();
	}, Keys.lifetime);
	base::qt_signal_producer(
		qApp,
		&QApplication::focusChanged
	) | rpl::to_empty | rpl::on_next([] {
		ReleaseKeys();
	}, Keys.lifetime);
}

void HoverWheel(not_null<RendererCore*> core) {
	if (WheelHovered.get() != core.get()) {
		WheelHovered = core;
	}
}

void UnhoverWheel(not_null<const RendererCore*> core) {
	if (WheelHovered.get() == core.get()) {
		WheelHovered = nullptr;
	}
}

bool ConsumeWheel(not_null<QWheelEvent*> e) {
	const auto phase = e->phase();
	if (phase == Qt::NoScrollPhase) {
		const auto target = WheelHovered.get();
		const auto captured = Captures(target);
		DeliverWheel(target, e);
		return captured;
	} else if (phase == Qt::ScrollBegin || !Gesture.active) {
		const auto target = WheelHovered.get();
		Gesture = WheelGesture{
			.target = target,
			.active = true,
			.captured = Captures(target),
		};
	}
	const auto captured = Gesture.captured;
	DeliverWheel(Gesture.target.get(), e);
	if (phase == Qt::ScrollEnd) {
		Gesture = WheelGesture();
	}
	return captured;
}

} // namespace Tgw
