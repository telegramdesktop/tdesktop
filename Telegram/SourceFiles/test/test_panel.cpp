/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#ifdef _DEBUG

#include "test/test_panel.h"

#include "base/flat_map.h"
#include "base/weak_qptr.h"
#include "test/test_log.h"

namespace Test {
namespace {

struct Watch {
	crl::time started = 0;
	crl::time deadline = 0;
	PanelShowState last = PanelShowState::Hidden;
	int samples = 0;
	base::weak_qptr<QWidget> panel;
	bool done = false;
};

[[nodiscard]] base::flat_map<QString, Watch> &Watches() {
	static auto result = base::flat_map<QString, Watch>();
	return result;
}

void LogSettle(
		const QString &name,
		const Watch &watch,
		bool settled,
		const QString &state) {
	LogRaw(u"PANEL_SHOW_SETTLE: name=%1 settled=%2 elapsedMs=%3 "
		"deadlineMs=%4 state=%5 samples=%6"_q
		.arg(name)
		.arg(settled ? 1 : 0)
		.arg(qint64(watch.started
			? (crl::now() - watch.started)
			: 0))
		.arg(qint64(watch.deadline))
		.arg(state)
		.arg(watch.samples));
}

} // namespace

QString PanelShowStateName(PanelShowState state) {
	switch (state) {
	case PanelShowState::Hidden:
		return u"hidden"_q;
	case PanelShowState::ShowCache:
		return u"show_cache"_q;
	case PanelShowState::Live:
		return u"live"_q;
	}
	return u"missing"_q;
}

PanelShowState ReadPanelShowState(not_null<QWidget*> panel) {
	if (panel->isHidden()) {
		return PanelShowState::Hidden;
	}
	auto children = 0;
	auto shown = 0;
	for (const auto child : panel->children()) {
		if (const auto widget = qobject_cast<QWidget*>(child)) {
			++children;
			if (!widget->isHidden()) {
				++shown;
			}
		}
	}
	if (children > 0 && shown == 0) {
		return PanelShowState::ShowCache;
	}
	return PanelShowState::Live;
}

bool PanelShowSettled(
		const QString &name,
		not_null<QWidget*> panel,
		crl::time deadline) {
	auto &watch = Watches()[name];
	if (watch.done) {
		return true;
	}
	const auto now = crl::now();
	if (!watch.started) {
		watch.started = now;
		watch.deadline = deadline;
	}
	watch.panel = panel.get();
	++watch.samples;
	const auto strong = watch.panel.get();
	if (!strong) {
		watch.done = true;
		LogSettle(name, watch, false, u"missing"_q);
		Fail(
			u"panel show settle: %1"_q.arg(name),
			u"state=missing deadlineMs=%1"_q.arg(
				QString::number(qint64(watch.deadline))));
		return true;
	}
	strong->update();
	watch.last = ReadPanelShowState(strong);
	const auto state = PanelShowStateName(watch.last);
	if (watch.last == PanelShowState::Live) {
		watch.done = true;
		LogSettle(name, watch, true, state);
		Pass(u"panel show settle: %1"_q.arg(name));
		return true;
	}
	if (now - watch.started >= watch.deadline) {
		watch.done = true;
		LogSettle(name, watch, false, state);
		Fail(
			u"panel show settle: %1"_q.arg(name),
			u"state=%1 deadlineMs=%2"_q.arg(
				state,
				QString::number(qint64(watch.deadline))));
		return true;
	}
	return false;
}

} // namespace Test

#endif // _DEBUG
