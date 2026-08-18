/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "core/external_control.h"

#include "core/application.h"
#include "core/core_settings.h"
#include "boxes/connection_box.h"
#include "main/main_domain.h"
#include "storage/localstorage.h"
#include "storage/storage_domain.h"
#include "window/window_controller.h"
#include "ui/layers/generic_box.h"
#include "ui/text/text_utilities.h"
#include "ui/toast/toast.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/vertical_list.h"
#include "base/random.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_widgets.h"

#include <QtWidgets/QApplication>
#include <QtWidgets/QWidget>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>

namespace Core {
namespace {

constexpr auto kAutomationKey = std::string_view("automation.enabled");
constexpr auto kProxyUndoDuration = crl::time(8000);

struct WindowEntry {
	not_null<Window::Controller*> controller;
	QWidget *window = nullptr;
};

[[nodiscard]] bool AutomationEnabled() {
	return Core::App().settings().readPref<bool>(kAutomationKey, false);
}

void FillAutomationConfirmBox(
		not_null<Ui::GenericBox*> box,
		const QString &text,
		Fn<void()> enable) {
	box->setTitle(rpl::single(u"Local automation"_q));
	box->addRow(object_ptr<Ui::FlatLabel>(
		box,
		rpl::single(text),
		st::boxLabel));

	const auto cancel = [=] {
		box->closeBox();
	};
	struct Entry {
		QString text;
		Fn<void()> callback;
	};
	auto entries = std::vector<Entry>{
		{ u"Enable"_q, enable },
		{ u"Cancel"_q, cancel },
		{ u"Not sure"_q, cancel },
	};
	for (auto i = int(entries.size()) - 1; i > 0; --i) {
		std::swap(entries[i], entries[base::RandomIndex(i + 1)]);
	}

	const auto content = box->verticalLayout();
	for (const auto &entry : entries) {
		Ui::AddSkip(content);
		const auto button = content->add(
			object_ptr<Ui::RoundButton>(
				content,
				rpl::single(entry.text),
				st::defaultLightButton),
			st::boxRowPadding,
			style::al_justify);
		button->setFullRadius(true);
		button->setClickedCallback(entry.callback);
	}
	box->setStyle(st::localAutomationBox);
}

void RequestEnableAutomation() {
	const auto window = Core::App().activePrimaryWindow();
	if (!window) {
		return;
	}
	static QPointer<Ui::GenericBox> current;
	if (current) {
		return;
	}
	const auto show = window->uiShow();

	const auto second = [=](not_null<Ui::GenericBox*> box) {
		current = box.get();
		FillAutomationConfirmBox(
			box,
			u"Just to be sure — confirm once more to enable local "
			u"automation."_q,
			[=] {
				Core::App().settings().writePref<bool>(
					kAutomationKey,
					true);
				box->closeBox();
			});
	};
	const auto first = [=](not_null<Ui::GenericBox*> box) {
		current = box.get();
		FillAutomationConfirmBox(
			box,
			u"An external program is trying to control "
			u"Telegram Desktop over the local socket — read open "
			u"windows and activate them.\n\nEnable local "
			u"automation? While it is on, anything running under your "
			u"user account can control the app."_q,
			[=] {
				box->closeBox();
				show->showBox(Box(second));
			});
	};
	show->show(Box(first));
}

[[nodiscard]] QByteArray Pack(QJsonObject object) {
	return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

[[nodiscard]] QByteArray Error(const QString &text) {
	auto object = QJsonObject();
	object.insert(u"ok"_q, false);
	object.insert(u"error"_q, text);
	return Pack(object);
}

[[nodiscard]] std::vector<WindowEntry> CollectWindows() {
	auto result = std::vector<WindowEntry>();
	for (const auto widget : QApplication::topLevelWidgets()) {
		const auto controller = App().findWindow(widget);
		if (!controller) {
			continue;
		}
		auto already = false;
		for (const auto &entry : result) {
			if (entry.controller.get() == controller) {
				already = true;
				break;
			}
		}
		if (!already) {
			result.push_back({ controller, widget->window() });
		}
	}
	ranges::sort(result, [](const WindowEntry &a, const WindowEntry &b) {
		if (a.controller->isPrimary() != b.controller->isPrimary()) {
			return a.controller->isPrimary();
		}
		return a.controller.get() < b.controller.get();
	});
	return result;
}

[[nodiscard]] QByteArray HandleWindows() {
	const auto active = App().activeWindow();
	auto list = QJsonArray();
	auto index = 0;
	for (const auto &entry : CollectWindows()) {
		auto object = QJsonObject();
		object.insert(u"id"_q, index++);
		object.insert(u"title"_q, entry.window->windowTitle());
		object.insert(u"primary"_q, entry.controller->isPrimary());
		object.insert(u"active"_q, (entry.controller.get() == active));
		list.append(object);
	}
	auto object = QJsonObject();
	object.insert(u"ok"_q, true);
	object.insert(u"windows"_q, list);
	return Pack(object);
}

[[nodiscard]] QByteArray HandleActivate(int index) {
	const auto windows = CollectWindows();
	if (index < 0 || index >= int(windows.size())) {
		return Error(u"no such window"_q);
	}
	windows[index].controller->activate();
	auto object = QJsonObject();
	object.insert(u"ok"_q, true);
	object.insert(u"activated"_q, index);
	return Pack(object);
}

[[nodiscard]] QByteArray HandleCycle() {
	const auto windows = CollectWindows();
	if (windows.empty()) {
		return Error(u"no windows"_q);
	}
	const auto active = App().activeWindow();
	auto current = 0;
	for (auto i = 0, count = int(windows.size()); i != count; ++i) {
		if (windows[i].controller.get() == active) {
			current = i;
			break;
		}
	}
	const auto next = (current + 1) % int(windows.size());
	windows[next].controller->activate();
	auto object = QJsonObject();
	object.insert(u"ok"_q, true);
	object.insert(u"activated"_q, next);
	return Pack(object);
}

[[nodiscard]] QByteArray HandleLock() {
	if (!App().domain().local().hasLocalPasscode()) {
		return Error(u"no local passcode set"_q);
	}
	const auto already = App().passcodeLocked();
	if (!already) {
		App().lockByPasscode();
	}
	auto object = QJsonObject();
	object.insert(u"ok"_q, true);
	object.insert(u"locked"_q, true);
	object.insert(u"changed"_q, !already);
	return Pack(object);
}

[[nodiscard]] QString ProxyTypeName(MTP::ProxyData::Type type) {
	switch (type) {
	case MTP::ProxyData::Type::Socks5: return u"socks5"_q;
	case MTP::ProxyData::Type::Http: return u"http"_q;
	case MTP::ProxyData::Type::Mtproto: return u"mtproto"_q;
	case MTP::ProxyData::Type::Web: return u"web"_q;
	case MTP::ProxyData::Type::None: break;
	}
	return u"none"_q;
}

[[nodiscard]] QString ProxyModeName(MTP::ProxyData::Settings settings) {
	switch (settings) {
	case MTP::ProxyData::Settings::Enabled: return u"enabled"_q;
	case MTP::ProxyData::Settings::Disabled: return u"disabled"_q;
	case MTP::ProxyData::Settings::System: break;
	}
	return u"system"_q;
}

[[nodiscard]] QString ProxyLabel(const MTP::ProxyData &proxy) {
	return ProxyTypeName(proxy.type)
		+ u" "_q
		+ proxy.host
		+ u":"_q
		+ QString::number(proxy.port);
}

void ShowProxyToast(const QString &text, Fn<void()> undo) {
	const auto window = App().activePrimaryWindow();
	if (!window) {
		return;
	}
	auto content = TextWithEntities{ text + u" "_q };
	content.append(Ui::Text::Link(u"Undo"_q));
	const auto instance
		= std::make_shared<base::weak_ptr<Ui::Toast::Instance>>();
	*instance = window->uiShow()->showToast({
		.text = std::move(content),
		.filter = [=](const auto &...) {
			undo();
			if (const auto strong = instance->get()) {
				strong->hideAnimated();
			}
			return false;
		},
		.duration = kProxyUndoDuration,
	});
}

[[nodiscard]] int ProxyIndexByArgument(const QString &argument) {
	const auto &proxies = App().settings().proxy();
	auto ok = false;
	const auto index = argument.toInt(&ok);
	if (ok) {
		return (index >= 0 && index < int(proxies.list().size()))
			? index
			: -1;
	}
	const auto proxy = ProxiesBoxController::ProxyFromLink(argument);
	return proxy ? proxies.indexInList(proxy) : -1;
}

[[nodiscard]] QByteArray HandleProxyList() {
	const auto &proxies = App().settings().proxy();
	const auto selected = proxies.selected();
	auto list = QJsonArray();
	auto index = 0;
	for (const auto &proxy : proxies.list()) {
		auto object = QJsonObject();
		object.insert(u"id"_q, index++);
		object.insert(u"type"_q, ProxyTypeName(proxy.type));
		object.insert(u"host"_q, proxy.host);
		object.insert(u"port"_q, int(proxy.port));
		object.insert(u"selected"_q, (proxy == selected));
		list.append(object);
	}
	auto object = QJsonObject();
	object.insert(u"ok"_q, true);
	object.insert(u"mode"_q, ProxyModeName(proxies.settings()));
	object.insert(u"enabled"_q, proxies.isEnabled());
	object.insert(u"proxies"_q, list);
	return Pack(object);
}

[[nodiscard]] QByteArray HandleProxyAdd(const QString &link) {
	const auto proxy = ProxiesBoxController::ProxyFromLink(link);
	if (proxy.type == MTP::ProxyData::Type::None) {
		return Error(u"invalid proxy link"_q);
	} else if (!proxy) {
		const auto status = proxy.status();
		return Error((status == MTP::ProxyData::Status::Unsupported)
			? u"unsupported proxy"_q
			: (status == MTP::ProxyData::Status::IncorrectSecret)
			? u"incorrect proxy secret"_q
			: u"invalid proxy"_q);
	}
	auto &proxies = App().settings().proxy();
	auto object = QJsonObject();
	object.insert(u"ok"_q, true);
	const auto already = proxies.indexInList(proxy);
	if (already >= 0) {
		object.insert(u"id"_q, already);
		object.insert(u"added"_q, false);
		return Pack(object);
	}
	proxies.addToList(proxy);
	Local::writeSettings();
	ShowProxyToast(u"Proxy added: "_q + ProxyLabel(proxy), [=] {
		auto &current = App().settings().proxy();
		const auto selected = (current.selected() == proxy);
		if (current.removeFromList(proxy) && selected) {
			App().setCurrentProxy(
				MTP::ProxyData(),
				MTP::ProxyData::Settings::System);
		}
		Local::writeSettings();
	});
	object.insert(u"id"_q, proxies.indexInList(proxy));
	object.insert(u"added"_q, true);
	return Pack(object);
}

[[nodiscard]] QByteArray HandleProxyRemove(const QString &argument) {
	const auto index = ProxyIndexByArgument(argument);
	if (index < 0) {
		return Error(u"no such proxy"_q);
	}
	auto &proxies = App().settings().proxy();
	const auto proxy = proxies.list()[index];
	const auto wasSelected = (proxies.selected() == proxy);
	const auto wasSettings = proxies.settings();
	if (!proxies.removeFromList(proxy)) {
		return Error(u"no such proxy"_q);
	}
	if (wasSelected) {
		if (wasSettings == MTP::ProxyData::Settings::Enabled) {
			App().setCurrentProxy(
				MTP::ProxyData(),
				MTP::ProxyData::Settings::System);
		} else {
			proxies.setSelected(MTP::ProxyData());
		}
	}
	Local::writeSettings();
	ShowProxyToast(u"Proxy removed: "_q + ProxyLabel(proxy), [=] {
		auto &current = App().settings().proxy();
		if (current.indexInList(proxy) < 0) {
			current.insertToList(index, proxy);
		}
		if (wasSelected) {
			App().setCurrentProxy(proxy, wasSettings);
		}
		Local::writeSettings();
	});
	auto object = QJsonObject();
	object.insert(u"ok"_q, true);
	object.insert(u"removed"_q, index);
	return Pack(object);
}

[[nodiscard]] QByteArray HandleProxySelect(MTP::ProxyData proxy) {
	auto &proxies = App().settings().proxy();
	const auto wasSelected = proxies.selected();
	const auto wasSettings = proxies.settings();
	App().setCurrentProxy(proxy, MTP::ProxyData::Settings::Enabled);
	Local::writeSettings();
	ShowProxyToast(u"Proxy enabled: "_q + ProxyLabel(proxy), [=] {
		App().setCurrentProxy(wasSelected, wasSettings);
		Local::writeSettings();
	});
	auto object = QJsonObject();
	object.insert(u"ok"_q, true);
	object.insert(u"selected"_q, proxies.indexInList(proxy));
	return Pack(object);
}

[[nodiscard]] QByteArray HandleProxyUse(const QString &argument) {
	const auto index = ProxyIndexByArgument(argument);
	if (index < 0) {
		return Error(u"no such proxy"_q);
	}
	return HandleProxySelect(App().settings().proxy().list()[index]);
}

[[nodiscard]] QByteArray HandleProxyNext() {
	const auto &proxies = App().settings().proxy();
	const auto count = int(proxies.list().size());
	if (!count) {
		return Error(u"no proxies configured"_q);
	}
	const auto current = proxies.indexInList(proxies.selected());
	return HandleProxySelect(proxies.list()[(current + 1) % count]);
}

[[nodiscard]] QByteArray HandleProxyToggle() {
	auto &proxies = App().settings().proxy();
	const auto wasSelected = proxies.selected();
	const auto wasSettings = proxies.settings();
	if (!proxies.isEnabled()) {
		if (!wasSelected && proxies.list().empty()) {
			return Error(u"no proxies configured"_q);
		}
		return HandleProxySelect(wasSelected
			? wasSelected
			: proxies.list().back());
	}
	App().setCurrentProxy(wasSelected, MTP::ProxyData::Settings::Disabled);
	Local::writeSettings();
	ShowProxyToast(u"Proxy disabled."_q, [=] {
		App().setCurrentProxy(wasSelected, wasSettings);
		Local::writeSettings();
	});
	auto object = QJsonObject();
	object.insert(u"ok"_q, true);
	object.insert(u"enabled"_q, false);
	return Pack(object);
}

} // namespace

QByteArray HandleExternalControl(const QString &command) {
	if (!IsAppLaunched()) {
		return Error(u"application is not launched"_q);
	} else if (!AutomationEnabled()) {
		RequestEnableAutomation();
		return Error(u"local automation is disabled — confirm in the "
			u"Telegram window to enable"_q);
	} else if (command == u"automation-off"_q) { // TEMP test helper.
		Core::App().settings().writePref<bool>(kAutomationKey, false);
		auto object = QJsonObject();
		object.insert(u"ok"_q, true);
		return Pack(object);
	} else if (command == u"ping"_q) {
		auto object = QJsonObject();
		object.insert(u"ok"_q, true);
		object.insert(u"result"_q, u"pong"_q);
		return Pack(object);
	} else if (command == u"windows"_q) {
		return HandleWindows();
	} else if (command.startsWith(u"activate:"_q)) {
		return HandleActivate(command.mid(9).toInt());
	} else if (command == u"cycle"_q) {
		return HandleCycle();
	} else if (command == u"lock"_q) {
		return HandleLock();
	} else if (command == u"proxies"_q) {
		return HandleProxyList();
	} else if (command.startsWith(u"proxy-add:"_q)) {
		return HandleProxyAdd(command.mid(10));
	} else if (command.startsWith(u"proxy-remove:"_q)) {
		return HandleProxyRemove(command.mid(13));
	} else if (command.startsWith(u"proxy-use:"_q)) {
		return HandleProxyUse(command.mid(10));
	} else if (command == u"proxy-next"_q) {
		return HandleProxyNext();
	} else if (command == u"proxy-toggle"_q) {
		return HandleProxyToggle();
	}
	return Error(u"unknown control command"_q);
}

} // namespace Core
