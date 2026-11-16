// This file is part of mokaGram, https://github.com/ecronxy/mokagram
// (c) 2026 @ecronx (https://t.me/ecronx). GPLv3 with the attribution term
// of MOKAGRAM_NOTICE.md (section 7(b)): keep this notice when copying.
// mokagram-id: 15dde3b3
// Mokagram: экономия ресурсов. Фоновая экономия, режим тостера, возврат памяти системе.
#include "mokagram/perf/mokagram_perf.h"

#include "mokagram/settings/mokagram_settings.h"
#include "base/timer.h"
#include "storage/localstorage.h"
#include "platform/platform_specific.h"
#include "core/core_settings.h"
#include "core/application.h"
#include "settings.h"
#include "ui/text/text_emoji_budget.h"
#include "ui/style/style_core_scale.h"

#include <QtCore/QEvent>
#include <QtGui/QWindow>
#include <QtWidgets/QLabel>
#include <algorithm>
#include <deque>
#include <vector>

#include <QtCore/QTimer>
#include <QtGui/QGuiApplication>

#ifdef Q_OS_MAC
#include <crl/crl_async.h>
#include <malloc/malloc.h>
#include <atomic>
#else // Q_OS_MAC
#include <malloc.h>
#endif // Q_OS_MAC

namespace Mokagram::Perf {
namespace {

namespace S = ::Mokagram::Settings;

constexpr auto kIdleDelay = crl::time(3000);
constexpr auto kTrimActive = crl::time(3 * 60 * 1000);
constexpr auto kTrimIdle = crl::time(60 * 1000);

struct State {
	rpl::variable<bool> economy = false;
	bool looking = true;
	bool idleEconomy = false;
};

[[nodiscard]] State &Data() {
	static auto result = State();
	return result;
}

void Apply() {
	auto &state = Data();
	Ui::Text::SetCustomEmojiBudget(S::Int(u"lag.emoji_cap"_q));
	Ui::Text::SetStickerBudget(S::Int(u"lag.sticker_cap"_q));
	state.economy = S::Bool(u"lag.toaster"_q)
		|| (state.idleEconomy && S::Bool(u"lag.idle"_q));
}

// Мелкие блоки возвращаем системе: glibc иначе держит их в арене и память растёт.
void Trim() {
#if defined Q_OS_MAC
	static auto trimming = std::atomic_flag();
	if (!trimming.test_and_set()) {
		crl::async([] {
			malloc_zone_pressure_relief(nullptr, 0);
			trimming.clear();
		});
	}
#elif defined Q_OS_WIN
	_heapmin();
#else
	malloc_trim(0);
#endif
}

// Оверлей lag.fps: кадры считаем по UpdateRequest верхних окон, пока включён тумблер.
class FpsMeter final : public QObject {
public:
	FpsMeter() {
		_label = std::make_unique<QLabel>();
		_label->setWindowFlags(Qt::Tool
			| Qt::FramelessWindowHint
			| Qt::WindowStaysOnTopHint
			| Qt::WindowTransparentForInput
			| Qt::WindowDoesNotAcceptFocus);
		_label->setAttribute(Qt::WA_TranslucentBackground);
		_label->setAttribute(Qt::WA_ShowWithoutActivating);
		const auto pad = style::ConvertScale(6);
		_label->setContentsMargins(pad, pad / 2, pad, pad / 2);
		_label->setStyleSheet(u"color:#fff;background:rgba(0,0,0,170);border-radius:%1px;"_q
			.arg(style::ConvertScale(8)));
		_label->setText(u"fps ..."_q);
		_label->adjustSize();
		_label->move(style::ConvertScale(8), style::ConvertScale(8));
		_label->show();
		_timer.setInterval(500);
		QObject::connect(&_timer, &QTimer::timeout, [=] { update(); });
		_timer.start();
		qApp->installEventFilter(this);
	}
	~FpsMeter() {
		qApp->removeEventFilter(this);
	}

	bool eventFilter(QObject *watched, QEvent *e) override {
		if (e->type() == QEvent::UpdateRequest) {
			const auto window = qobject_cast<QWindow*>(watched);
			if (window
				&& window->isTopLevel()
				&& window != _label->windowHandle()) {
				_frames.push_back(crl::now());
			}
		}
		return false;
	}

private:
	void update() {
		const auto now = crl::now();
		while (!_frames.empty() && now - _frames.front() > 5000) {
			_frames.pop_front();
		}
		auto fps = 0;
		for (auto i = _frames.rbegin(); i != _frames.rend() && now - *i <= 1000; ++i) {
			++fps;
		}
		auto low = 0.;
		if (_frames.size() > 10) {
			auto gaps = std::vector<crl::time>();
			gaps.reserve(_frames.size());
			for (auto i = size_t(1); i < _frames.size(); ++i) {
				gaps.push_back(_frames[i] - _frames[i - 1]);
			}
			std::sort(gaps.begin(), gaps.end());
			const auto worst = gaps[std::min(gaps.size() - 1, gaps.size() * 99 / 100)];
			low = worst > 0 ? 1000. / worst : 0.;
		}
		_label->setText(u"%1 fps  1%% low %2"_q.arg(fps).arg(int(low)));
		_label->adjustSize();
		if (const auto bg = _label->windowHandle()) {
			bg->raise();
		}
	}

	std::unique_ptr<QLabel> _label;
	QTimer _timer;
	std::deque<crl::time> _frames;

};

std::unique_ptr<FpsMeter> FpsOverlay;

void ApplyFps() {
	const auto on = S::Bool(u"lag.fps"_q);
	if (on && !FpsOverlay) {
		FpsOverlay = std::make_unique<FpsMeter>();
	} else if (!on) {
		FpsOverlay = nullptr;
	}
}

} // namespace

rpl::producer<bool> EconomyValue() {
	return Data().economy.value();
}

bool UserIsLooking() {
	return Data().looking;
}

void Start() {
#ifdef Q_OS_LINUX
	// Меньше арен и раньше отдаём память: заметно для долго работающего клиента.
	mallopt(M_ARENA_MAX, 2);
	mallopt(M_TRIM_THRESHOLD, 128 * 1024);
	mallopt(M_MMAP_THRESHOLD, 256 * 1024);
#endif // Q_OS_LINUX

	static auto idleTimer = QTimer();
	idleTimer.setSingleShot(true);
	idleTimer.setTimerType(Qt::VeryCoarseTimer);
	idleTimer.setInterval(int(kIdleDelay));
	QObject::connect(&idleTimer, &QTimer::timeout, [] {
		Data().idleEconomy = true;
		Apply();
		Trim();
	});

	// Mokagram: on macOS trim once on background entry, not periodically.
#ifndef Q_OS_MAC
	static auto trimTimer = QTimer();
	trimTimer.setTimerType(Qt::VeryCoarseTimer);
	trimTimer.setInterval(int(kTrimActive));
	QObject::connect(&trimTimer, &QTimer::timeout, [] {
		Trim();
		trimTimer.setInterval(int(Data().looking ? kTrimActive : kTrimIdle));
	});
	trimTimer.start();
#endif // Q_OS_MAC

	QObject::connect(qApp, &QGuiApplication::applicationStateChanged, [](
			Qt::ApplicationState state) {
		auto &data = Data();
		if (state == Qt::ApplicationActive) {
			data.looking = true;
			idleTimer.stop();
			data.idleEconomy = false;
			// Смена фокуса приходит как системное событие, вне цикла событий приложения.
			// Apply() запускает анимации и откладывает вызовы, а это допустимо только
			// внутри обработки события, поэтому уходим в очередь.
			QMetaObject::invokeMethod(qApp, [] {
				Apply();
			}, Qt::QueuedConnection);
		} else {
			data.looking = false;
			idleTimer.start();
		}
	});

	// Оверлей статический и переживает Sandbox: убираем его, пока qApp ещё жив.
	QObject::connect(qApp, &QCoreApplication::aboutToQuit, [] {
		FpsOverlay = nullptr;
	});

	// Переключатели в настройках действуют сразу.
	static auto lifetime = rpl::lifetime();
	S::Changed() | rpl::on_next([](QString key) {
		if (key == u"lag.autostart"_q) {
			// Автозапуск и «крестик сворачивает в фон» это обычные настройки клиента,
			// здесь только удобные переключатели рядом с остальным.
			const auto on = S::Bool(key);
			if (on != cAutoStart() && Platform::AutostartSupported()) {
				cSetAutoStart(on);
				Platform::AutostartToggle(on, [=](bool enabled) {
					if (enabled != on) {
						cSetAutoStart(enabled);
						S::SetBool(u"lag.autostart"_q, enabled);
					}
					Local::writeSettings();
				});
			}
		} else if (key == u"lag.background"_q) {
			const auto behavior = S::Bool(key)
				? Core::Settings::CloseBehavior::RunInBackground
				: Core::Settings::CloseBehavior::Quit;
			auto &settings = Core::App().settings();
			if (settings.closeBehavior() != behavior) {
				settings.setCloseBehavior(behavior);
				Local::writeSettings();
			}
		} else if (key == u"lag.fps"_q) {
			ApplyFps();
		} else if (key.startsWith(u"lag."_q)) {
			Apply();
		}
	}, lifetime);
	// Переключатели показывают то, что реально включено в клиенте.
	S::SetBool(u"lag.autostart"_q, cAutoStart());
	S::SetBool(
		u"lag.background"_q,
		Core::App().settings().closeBehavior()
			== Core::Settings::CloseBehavior::RunInBackground);
	Apply();
	ApplyFps();
}

} // namespace Mokagram::Perf
