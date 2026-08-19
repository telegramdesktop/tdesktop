/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "iv/iv_controller.h"

#include "base/event_filter.h"
#include "base/platform/base_platform_info.h"
#include "base/qt/qt_key_modifiers.h"
#include "base/qt_signal_producer.h"
#include "core/file_utilities.h"
#include "lang/lang_keys.h"
#include "ui/chat/attach/attach_bot_webview.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/rp_window.h"
#include "ui/basic_click_handlers.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "webview/webview_embed.h"
#include "webview/webview_interface.h"
#include "styles/palette.h"
#include "styles/style_iv.h"
#include "styles/style_payments.h"
#include "styles/style_window.h"

#include <QtCore/QUrl>
#include <QtGui/QGuiApplication>
#include <QtGui/QKeyEvent>
#include <QtGui/QPainter>
#include <QtGui/QWindow>

#include <string>

#include <ada.h>

namespace Iv {
namespace {

constexpr auto kZoomStep = int(10);

[[nodiscard]] QString TonsiteToHttps(QString value) {
	const auto ChangeHost = [](QString tonsite) {
		const auto fake = "http://" + tonsite.toStdString();
		const auto parsed = ada::parse<ada::url>(fake);
		if (!parsed) {
			return QString();
		}
		tonsite = QString::fromStdString(parsed->get_hostname());
		tonsite = tonsite.replace('-', "-h");
		tonsite = tonsite.replace('.', "-d");
		return tonsite + ".magic.org";
	};
	const auto prefix = u"tonsite://"_q;
	if (!value.toLower().startsWith(prefix)) {
		return QString();
	}
	const auto part = value.mid(prefix.size());
	const auto split = part.indexOf('/');
	const auto host = ChangeHost((split < 0) ? part : part.left(split));
	if (host.isEmpty()) {
		return QString();
	}
	return "https://" + host + ((split < 0) ? u"/"_q : part.mid(split));
}

[[nodiscard]] QString HttpsToTonsite(QString value) {
	const auto ChangeHost = [](QString https) {
		const auto dot = https.indexOf('.');
		if (dot < 0 || https.mid(dot).toLower() != u".magic.org"_q) {
			return QString();
		}
		https = https.mid(0, dot);
		https = https.replace("-d", ".");
		https = https.replace("-h", "-");
		auto parts = https.split('.');
		for (auto &part : parts) {
			if (part.startsWith(u"xn--"_q)) {
				const auto utf8 = part.mid(4).toStdString();
				auto out = std::u32string();
				if (ada::idna::punycode_to_utf32(utf8, out)) {
					part = QString::fromUcs4(out.data(), out.size());
				}
			}
		}
		return parts.join('.');
	};
	const auto prefix = u"https://"_q;
	if (!value.toLower().startsWith(prefix)) {
		return value;
	}
	const auto part = value.mid(prefix.size());
	const auto split = part.indexOf('/');
	const auto host = ChangeHost((split < 0) ? part : part.left(split));
	if (host.isEmpty()) {
		return value;
	}
	return "tonsite://"
		+ host
		+ ((split < 0) ? u"/"_q : part.mid(split));
}

} // namespace

Controller::Controller(not_null<Delegate*> delegate)
: _delegate(delegate) {
	createWindow();
}

Controller::~Controller() {
	if (_window) {
		_window->hide();
	}
	base::take(_webview);
	_back = nullptr;
	_forward = nullptr;
	_subtitle = nullptr;
	_subtitleWrap = nullptr;
	_window = nullptr;
}

void Controller::updateTitleGeometry(int newWidth) const {
	_subtitleWrap->setGeometry(
		0,
		0,
		newWidth,
		st::ivSubtitleHeight);
	_subtitleWrap->paintRequest() | rpl::on_next([=](QRect clip) {
		QPainter(_subtitleWrap.get()).fillRect(clip, st::windowBg);
	}, _subtitleWrap->lifetime());

	const auto left = (_back ? _back->width() : 0)
		+ (_forward ? _forward->width() : 0)
		+ st::ivSubtitleSkip;
	const auto right = st::ivSubtitleSkip;
	_subtitle->resizeToWidth(std::max(newWidth - left - right, 0));
	_subtitle->moveToLeft(left, st::ivSubtitleTop);

	if (_back) {
		_back->moveToLeft(0, 0);
	}
	if (_forward) {
		_forward->moveToLeft(_back ? _back->width() : 0, 0);
	}
}

bool Controller::IsGoodTonSiteUrl(const QString &uri) {
	return !TonsiteToHttps(uri).isEmpty();
}

void Controller::showTonSite(
		const Webview::StorageId &storageId,
		QString uri) {
	const auto url = TonsiteToHttps(uri);
	Assert(!url.isEmpty());

	if (!_webview) {
		createWebview(storageId);
	}
	if (_webview && _webview->widget()) {
		_webview->navigate(url);
		activate();
	}
	_url = url;
	_subtitleText = _url.value(
	) | rpl::filter([=](const QString &url) {
		return !url.isEmpty() && url != u"about:blank"_q;
	}) | rpl::map([=](QString value) {
		return HttpsToTonsite(value);
	});
	_windowTitleText = _subtitleText.value();
}

void Controller::createWindow() {
	_window = std::make_unique<Ui::RpWindow>();
	const auto window = _window.get();

	_subtitleWrap = std::make_unique<Ui::RpWidget>(_window->body().get());
	_subtitle = Ui::CreateChild<Ui::FlatLabel>(
		_subtitleWrap.get(),
		_subtitleText.value(),
		st::ivSubtitle);
	_subtitle->setSelectable(true);

	_windowTitleText = _subtitleText.value(
	) | rpl::map([=](const QString &subtitle) {
		const auto prefix = tr::lng_iv_window_title(tr::now);
		return prefix + ' ' + QChar(0x2014) + ' ' + subtitle;
	});
	_windowTitleText.value(
	) | rpl::on_next([=](const QString &title) {
		_window->setWindowTitle(title);
	}, _subtitle->lifetime());

	_back = Ui::CreateChild<Ui::IconButton>(_subtitleWrap.get(), st::ivBack);
	_back->setClickedCallback([=] {
		if (_webview) {
			_webview->eval("window.history.back();");
		}
	});
	_back->setDisabled(true);

	_forward = Ui::CreateChild<Ui::IconButton>(
		_subtitleWrap.get(),
		st::ivForward);
	_forward->setClickedCallback([=] {
		if (_webview) {
			_webview->eval("window.history.forward();");
		}
	});
	_forward->setDisabled(true);

	base::qt_signal_producer(
		qApp,
		&QGuiApplication::focusWindowChanged
	) | rpl::filter([=](QWindow *focused) {
		const auto handle = window->window()->windowHandle();
		return _webview && handle && (focused == handle);
	}) | rpl::on_next([=] {
		setInnerFocus();
	}, window->lifetime());

	window->body()->widthValue() | rpl::on_next([=](int width) {
		updateTitleGeometry(width);
	}, _subtitle->lifetime());

	window->events(
	) | rpl::on_next([=](not_null<QEvent*> e) {
		if (e->type() == QEvent::Close) {
			close();
		} else if (e->type() == QEvent::KeyPress) {
			processKey(static_cast<QKeyEvent*>(e.get()));
		}
	}, window->lifetime());

	base::install_event_filter(window, qApp, [=](not_null<QEvent*> e) {
		if (e->type() != QEvent::ShortcutOverride
			|| !window->isActiveWindow()) {
			return base::EventFilterResult::Continue;
		}
		const auto event = static_cast<QKeyEvent*>(e.get());
		const auto command = Platform::IsMac()
			? Qt::MetaModifier
			: Qt::ControlModifier;
		if (!(event->modifiers() & command)) {
			return base::EventFilterResult::Continue;
		} else if (event->key() == Qt::Key_Plus
			|| event->key() == Qt::Key_Equal) {
			_delegate->ivSetZoom(_delegate->ivZoom() + kZoomStep);
			return base::EventFilterResult::Cancel;
		} else if (event->key() == Qt::Key_Minus) {
			_delegate->ivSetZoom(_delegate->ivZoom() - kZoomStep);
			return base::EventFilterResult::Cancel;
		} else if (event->key() == Qt::Key_0) {
			_delegate->ivSetZoom(0);
			return base::EventFilterResult::Cancel;
		}
		return base::EventFilterResult::Continue;
	});

	window->setGeometry(_delegate->ivGeometry(window));
	window->setMinimumSize({ st::windowMinWidth, st::windowMinHeight });

	window->geometryValue(
	) | rpl::distinct_until_changed(
	) | rpl::skip(1) | rpl::on_next([=] {
		_delegate->ivSaveGeometry(window);
	}, window->lifetime());

	_container = Ui::CreateChild<Ui::RpWidget>(window->body().get());
	rpl::combine(
		window->body()->sizeValue(),
		_subtitleWrap->heightValue()
	) | rpl::on_next([=](QSize size, int title) {
		_container->setGeometry(QRect(QPoint(), size).marginsRemoved(
			{ 0, title, 0, 0 }));
	}, _container->lifetime());

	_container->paintRequest() | rpl::on_next([=](QRect clip) {
		QPainter(_container).fillRect(clip, st::windowBg);
	}, _container->lifetime());

	_container->show();
	window->show();
}

void Controller::createWebview(const Webview::StorageId &storageId) {
	Expects(!_webview);

	const auto window = _window.get();
	_webview = std::make_unique<Webview::Window>(
		_container,
		Webview::WindowConfig{
			.opaqueBg = st::windowBg->c,
			.storageId = storageId,
			.safe = true,
		});
	const auto raw = _webview.get();

	if (const auto webviewZoomController = raw->zoomController()) {
		webviewZoomController->zoomValue(
		) | rpl::on_next([this](int value) {
			if (value > 0) {
				_delegate->ivSetZoom(value);
			}
		}, lifetime());
		_delegate->ivZoomValue(
		) | rpl::on_next([=](int value) {
			webviewZoomController->setZoom(value);
		}, lifetime());
		webviewZoomController->setZoom(_delegate->ivZoom());
	}

	window->lifetime().add([=] {
		base::take(_webview);
	});

	const auto widget = raw->widget();
	if (!widget) {
		base::take(_webview);
		showWebviewError();
		return;
	}
	widget->show();

	QObject::connect(widget, &QObject::destroyed, [=] {
		if (!_webview) {
			return;
		}
		crl::on_main(window, [=] {
			showWebviewError({ "Error: WebView has crashed." });
		});
		base::take(_webview);
	});

	_container->sizeValue(
	) | rpl::on_next([=](QSize size) {
		if (const auto widget = raw->widget()) {
			widget->setGeometry(QRect(QPoint(), size));
		}
	}, _container->lifetime());

	raw->setNavigationStartHandler([=](const QString &uri, bool newWindow) {
		Q_UNUSED(newWindow);

		if (uri == u"about:blank"_q
			|| QUrl(uri).host().toLower().endsWith(u".magic.org"_q)) {
			return true;
		}
		_events.fire({ .type = Event::Type::OpenLink, .url = uri });
		return false;
	});
	raw->setNavigationDoneHandler([=](bool success) {
		Q_UNUSED(success);
	});
	raw->navigationHistoryState(
	) | rpl::on_next([=](Webview::NavigationHistoryState state) {
		_back->setDisabled(!state.canGoBack);
		_forward->setDisabled(!state.canGoForward);
		_url = QString::fromStdString(state.url);
	}, _webview->lifetime());

	raw->init(R"()");
}

void Controller::processKey(QKeyEvent *event) {
	const auto command = Platform::IsMac()
		? Qt::MetaModifier
		: Qt::ControlModifier;
	if (event->key() == Qt::Key_Escape) {
		close();
	} else if (event->modifiers() & command) {
		if (event->key() == Qt::Key_W) {
			close();
		} else if (event->key() == Qt::Key_M) {
			minimize();
		} else if (event->key() == Qt::Key_Q) {
			quit();
		} else if (event->key() == Qt::Key_0) {
			_delegate->ivSetZoom(0);
		}
	}
}

void Controller::activate() {
	if (_window->isMinimized()) {
		_window->showNormal();
	} else if (_window->isHidden()) {
		_window->show();
	}
	_window->raise();
	_window->activateWindow();
	_window->setFocus();
	setInnerFocus();
}

void Controller::setInnerFocus() {
	if (_webview) {
		_webview->focus();
	}
}

bool Controller::active() const {
	return _window && _window->isActiveWindow();
}

void Controller::minimize() {
	if (_window) {
		_window->setWindowState(_window->windowState()
			| Qt::WindowMinimized);
	}
}

void Controller::close() {
	_events.fire({ Event::Type::Close });
}

void Controller::quit() {
	_events.fire({ Event::Type::Quit });
}

rpl::lifetime &Controller::lifetime() {
	return _lifetime;
}

void Controller::showWebviewError() {
	const auto available = Webview::Availability();
	if (available.error != Webview::Available::Error::None) {
		showWebviewError(Ui::BotWebView::ErrorText(available));
	} else {
		showWebviewError({ "Error: Could not initialize WebView." });
	}
}

void Controller::showWebviewError(TextWithEntities text) {
	const auto wrap = Ui::CreateChild<Ui::RpWidget>(_container);

	const auto error = Ui::CreateChild<Ui::PaddingWrap<Ui::FlatLabel>>(
		wrap,
		object_ptr<Ui::FlatLabel>(
			wrap,
			rpl::single(text),
			st::paymentsCriticalError),
		st::paymentsCriticalErrorPadding);
	error->entity()->setClickHandlerFilter([=](
			const ClickHandlerPtr &handler,
			Qt::MouseButton) {
		const auto entity = handler->getTextEntity();
		if (entity.type != EntityType::CustomUrl) {
			return true;
		}
		File::OpenUrl(entity.data);
		return false;
	});
	wrap->show();

	wrap->widthValue() | rpl::on_next([=](int width) {
		error->resizeToWidth(width);
		wrap->resize(width, error->height());
	}, wrap->lifetime());

	_container->sizeValue() | rpl::on_next([=](QSize size) {
		wrap->setGeometry(0, 0, size.width(), size.height() * 2 / 3);
	}, wrap->lifetime());
}

} // namespace Iv
