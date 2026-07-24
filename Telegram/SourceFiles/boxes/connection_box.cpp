/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/connection_box.h"

#include "base/call_delayed.h"
#include "base/qt/qt_key_modifiers.h"
#include "base/qthelp_regex.h"
#include "base/qthelp_url.h"
#include "base/weak_ptr.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "core/local_url_handlers.h"
#include "lang/lang_keys.h"
#include "main/main_account.h"
#include "main/main_session.h"
#include "mtproto/facade.h"
#include "mtproto/mtproto_config.h"
#include "mtproto/proxy_check.h"
#include "qr/qr_generate.h"
#include "settings/settings_common.h"
#include "storage/localstorage.h"
#include "ui/basic_click_handlers.h"
#include "ui/boxes/confirm_box.h"
#include "ui/boxes/peer_qr_box.h"
#include "ui/effects/animations.h"
#include "ui/effects/radial_animation.h"
#include "ui/painter.h"
#include "ui/text/text_options.h"
#include "ui/text/text_utilities.h"
#include "ui/toast/toast.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/dropdown_menu.h"
#include "ui/widgets/discrete_sliders.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/fields/number_input.h"
#include "ui/widgets/fields/password_input.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/menu/menu_add_action_callback.h"
#include "ui/widgets/menu/menu_add_action_callback_factory.h"
#include "ui/widgets/popup_menu.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/table_layout.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/vertical_list.h"
#include "ui/ui_utility.h"
#include "boxes/abstract_box.h" // Ui::show().
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_info.h"
#include "styles/style_intro.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"

#include <QtGui/QGuiApplication>
#include <QtGui/QClipboard>
#include <QtWidgets/QTextEdit>

namespace {

constexpr auto kSaveSettingsDelayedTimeout = crl::time(1000);

using ProxyData = MTP::ProxyData;

[[nodiscard]] int ClosestProxyRotationTimeoutSection(int value) {
	auto result = 0;
	auto bestDistance = 0;
	for (auto i = 0; i != int(Core::SettingsProxy::kProxyRotationTimeouts.size()); ++i) {
		const auto current = Core::SettingsProxy::kProxyRotationTimeouts[i];
		const auto distance = (current > value) ? (current - value) : (value - current);
		if ((i == 0) || (distance < bestDistance)) {
			result = i;
			bestDistance = distance;
		}
	}
	return result;
}

[[nodiscard]] std::vector<QString> ExtractLinkCandidates(const QString &input) {
	auto urls = std::vector<QString>();
	static const auto urlRegex = QRegularExpression(
		R"((?:https?:\/\/[^\s]+|tg:\/\/[^\s]+|(?:www\.)?(?:t\.me|telegram\.me|telegram\.dog)\/[^\s]+))",
		QRegularExpression::CaseInsensitiveOption);

	auto it = urlRegex.globalMatch(input);
	while (it.hasNext()) {
		urls.push_back(it.next().captured(0));
	}

	return urls;
}

[[nodiscard]] bool ProxyDataIsShareable(const ProxyData &proxy) {
	using Type = ProxyData::Type;
	return (proxy.type == Type::Socks5)
		|| (proxy.type == Type::Mtproto);
}

[[nodiscard]] QString ProxyDataToQueryPath(const ProxyData &proxy) {
	using Type = ProxyData::Type;
	const auto path = [&] {
		switch (proxy.type) {
		case Type::Socks5: return u"socks"_q;
		case Type::Mtproto: return u"proxy"_q;
		case Type::None:
		case Type::Http: return QString();
		}
		Unexpected("Proxy type in ProxyDataToQueryPath.");
	}();
	if (path.isEmpty()) {
		return QString();
	}
	return path
		+ "?server=" + proxy.host + "&port=" + QString::number(proxy.port)
		+ ((proxy.type == Type::Socks5 && !proxy.user.isEmpty())
			? "&user=" + qthelp::url_encode(proxy.user) : "")
		+ ((proxy.type == Type::Socks5 && !proxy.password.isEmpty())
			? "&pass=" + qthelp::url_encode(proxy.password) : "")
		+ ((proxy.type == Type::Mtproto && !proxy.password.isEmpty())
			? "&secret=" + proxy.password : "");
}

[[nodiscard]] QString ProxyDataToLocalLink(const ProxyData &proxy) {
	const auto queryPath = ProxyDataToQueryPath(proxy);
	return queryPath.isEmpty() ? QString() : (u"tg://"_q + queryPath);
}

[[nodiscard]] QString ProxyDataToPublicLink(
		not_null<Main::Account*> account,
		const ProxyData &proxy) {
	const auto queryPath = ProxyDataToQueryPath(proxy);
	if (queryPath.isEmpty()) {
		return QString();
	}
	if (const auto session = account->maybeSession()) {
		return session->createInternalLinkFull(queryPath);
	}
	auto domain = MTP::ConfigFields(
		account->mtp().environment()
	).internalLinksDomain;
	if (domain.endsWith('/') && queryPath.startsWith('/')) {
		domain.chop(1);
	} else if (!domain.endsWith('/') && !queryPath.startsWith('/')) {
		domain += '/';
	}
	return domain + queryPath;
}

[[nodiscard]] QColor ProxyQrActiveColor() {
	return QColor(0x40, 0xA7, 0xE3);
}

[[nodiscard]] QImage ProxyQr(const Qr::Data &data, int pixel, int max = 0) {
	Expects(data.size > 0);

	const auto available = max
		? std::max(max - 2 * st::introQrBackgroundSkip, 1)
		: 0;
	if (available > 0 && data.size * pixel > available) {
		pixel = std::max(available / data.size, 1);
	}
	return Qr::Generate(
		data,
		pixel * style::DevicePixelRatio(),
		Qt::black,
		Qt::white);
}

[[nodiscard]] QImage ProxyQrLogo() {
	const auto size = QSize(st::introQrCenterSize, st::introQrCenterSize);
	auto result = QImage(
		size * style::DevicePixelRatio(),
		QImage::Format_ARGB32_Premultiplied);
	result.fill(Qt::transparent);
	result.setDevicePixelRatio(style::DevicePixelRatio());
	{
		auto p = QPainter(&result);
		auto hq = PainterHighQualityEnabler(p);
		p.setBrush(ProxyQrActiveColor());
		p.setPen(Qt::NoPen);
		p.drawEllipse(QRect(QPoint(), size));
		st::introQrPlane.paintInCenter(p, QRect(QPoint(), size));
	}
	return result;
}

[[nodiscard]] QImage ProxyQrTile(const QString &link, int max = 0) {
	const auto data = Qr::Encode(link, Qr::Redundancy::Quartile);
	const auto qr = ProxyQr(
		data,
		st::introQrPixel,
		max);
	const auto qrSize = qr.width() / style::DevicePixelRatio();
	const auto skip = st::introQrBackgroundSkip;
	const auto size = QSize(qrSize + 2 * skip, qrSize + 2 * skip);
	auto result = QImage(
		size * style::DevicePixelRatio(),
		QImage::Format_ARGB32_Premultiplied);
	result.fill(Qt::transparent);
	result.setDevicePixelRatio(style::DevicePixelRatio());
	{
		auto p = QPainter(&result);
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(Qt::white);
		p.drawRoundedRect(
			QRect(QPoint(), size),
			st::introQrBackgroundRadius,
			st::introQrBackgroundRadius);
		p.drawImage(QRect(skip, skip, qrSize, qrSize), qr);
		const auto logo = ProxyQrLogo();
		p.drawImage(
			QRect(
				(size.width() - st::introQrCenterSize) / 2,
				(size.height() - st::introQrCenterSize) / 2,
				st::introQrCenterSize,
				st::introQrCenterSize),
			logo);
	}
	return result;
}

[[nodiscard]] QImage ProxyQrForShare(const QString &link) {
	return ProxyQrTile(
		link,
		st::boxWidth - st::boxRowPadding.left() - st::boxRowPadding.right());
}

void ShowProxyQrBox(std::shared_ptr<Ui::Show> show, const QString &link) {
	show->showBox(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(tr::lng_proxy_edit_share_qr_box_title());
		box->addButton(tr::lng_about_done(), [=] { box->closeBox(); });

		const auto copyCallback = [=, image = ProxyQrForShare(link)] {
			QGuiApplication::clipboard()->setImage(image);
			show->showToast({
				.text = { tr::lng_group_invite_qr_copied(tr::now) },
				.iconLottie = u"toast/copy"_q,
				.iconLottieSize = st::toastLottieIconSize,
			});
		};

		const auto qr = ProxyQrTile(
			link,
			st::boxWidth
				- st::boxRowPadding.left()
				- st::boxRowPadding.right());
		const auto size = qr.width() / style::DevicePixelRatio();
		const auto height = st::inviteLinkQrSkip * 2 + size;
		const auto container = box->addRow(
			object_ptr<Ui::BoxContentDivider>(box, height),
			st::inviteLinkQrMargin);
		const auto button = Ui::CreateChild<Ui::AbstractButton>(container);
		button->resize(size, size);
		button->paintRequest(
		) | rpl::on_next([=] {
			QPainter(button).drawImage(QRect(0, 0, size, size), qr);
		}, button->lifetime());
		container->widthValue(
		) | rpl::on_next([=](int width) {
			button->move((width - size) / 2, st::inviteLinkQrSkip);
		}, button->lifetime());
		button->setClickedCallback(copyCallback);

		box->addLeftButton(tr::lng_group_invite_context_copy(), copyCallback);
	}));
}

void ShareProxy(
		std::shared_ptr<Ui::Show> show,
		not_null<Main::Account*> account,
		const ProxyData &proxy,
		bool qr) {
	if (!ProxyDataIsShareable(proxy)) {
		return;
	}
	const auto qrLink = ProxyDataToLocalLink(proxy);
	if (qrLink.isEmpty()) {
		return;
	}
	if (qr) {
		if (account->sessionExists()) {
			show->showBox(Box([=](not_null<Ui::GenericBox*> box) {
				Ui::FillPeerQrBox(
					box,
					nullptr,
					qrLink,
					rpl::single(QString()));
				box->setTitle(tr::lng_proxy_edit_share_qr_box_title());
			}));
		} else {
			ShowProxyQrBox(show, qrLink);
		}
		return;
	}
	const auto internal = base::IsCtrlPressed()
		|| base::IsAltPressed()
		|| base::IsShiftPressed();
	const auto shareLink = internal
		? qrLink
		: ProxyDataToPublicLink(account, proxy);
	if (shareLink.isEmpty()) {
		return;
	}
	TextUtilities::SetClipboardText(TextForMimeData::Simple(shareLink));
	show->showToast({
		.text = { tr::lng_username_copied(tr::now) },
		.iconLottie = u"toast/voip_invite"_q,
		.iconLottieSize = st::toastLottieIconSize,
	});
}

[[nodiscard]] ProxyData ProxyDataFromFields(
		ProxyData::Type type,
		const QMap<QString, QString> &fields) {
	auto proxy = ProxyData();
	proxy.type = type;
	proxy.host = fields.value(u"server"_q);
	proxy.port = fields.value(u"port"_q).toUInt();
	if (type == ProxyData::Type::Socks5) {
		proxy.user = fields.value(u"user"_q);
		proxy.password = fields.value(u"pass"_q);
	} else if (type == ProxyData::Type::Mtproto) {
		proxy.password = fields.value(u"secret"_q);
	}
	return proxy;
};

[[nodiscard]] ProxyData ProxyDataFromLocalUrl(const QString &local) {
	const auto protocol = u"tg://"_q;
	const auto proxyString = u"proxy"_q;
	const auto socksString = u"socks"_q;
	if (!local.startsWith(protocol + proxyString, Qt::CaseInsensitive)
		&& !local.startsWith(protocol + socksString, Qt::CaseInsensitive)) {
		return ProxyData();
	}
	const auto command = base::StringViewMid(local, protocol.size(), 8192);
	using namespace qthelp;
	const auto options = RegExOption::CaseInsensitive;
	for (const auto &[expression, _] : Core::LocalUrlHandlers()) {
		const auto midExpression = base::StringViewMid(expression, 1);
		const auto isSocks = midExpression.startsWith(socksString);
		if (!midExpression.startsWith(proxyString) && !isSocks) {
			continue;
		}
		const auto match = regex_match(expression, command, options);
		if (!match) {
			continue;
		}
		const auto type = isSocks
			? ProxyData::Type::Socks5
			: ProxyData::Type::Mtproto;
		auto fields = url_parse_params(
			match->captured(1),
			qthelp::UrlParamNameTransform::ToLower);
		if (type == ProxyData::Type::Mtproto) {
			auto &secret = fields[u"secret"_q];
			secret.replace('+', '-').replace('/', '_');
		}
		return ProxyDataFromFields(type, fields);
	}
	return ProxyData();
}

[[nodiscard]] ProxyData ProxyDataFromClipboard() {
	const auto candidates = ExtractLinkCandidates(
		QGuiApplication::clipboard()->text());
	if (candidates.size() != 1) {
		return ProxyData();
	}
	const auto trimmed = candidates.front().trimmed();
	const auto converted = Core::TryConvertUrlToLocal(trimmed);
	const auto local = converted.isEmpty() ? trimmed : converted;
	const auto proxy = ProxyDataFromLocalUrl(local);
	return proxy.valid() ? proxy : ProxyData();
}

void AddProxyFromClipboard(
		not_null<ProxiesBoxController*> controller,
		std::shared_ptr<Ui::Show> show) {
	const auto proxyString = u"proxy"_q;
	const auto socksString = u"socks"_q;
	const auto protocol = u"tg://"_q;

	const auto maybeUrls = ExtractLinkCandidates(
		QGuiApplication::clipboard()->text());
	const auto isSingle = maybeUrls.size() == 1;

	enum class Result {
		Success,
		Failed,
		Unsupported,
		IncorrectSecret,
		Invalid,
	};

	const auto proceedUrl = [=](const QString &local) {
		const auto isProxyLink
			= local.startsWith(protocol + proxyString, Qt::CaseInsensitive)
			|| local.startsWith(protocol + socksString, Qt::CaseInsensitive);
		if (!isProxyLink) {
			return Result::Failed;
		}
		const auto proxy = ProxyDataFromLocalUrl(local);
		if (proxy.type == ProxyData::Type::None) {
			return Result::Success;
		} else if (!proxy) {
			const auto status = proxy.status();
			return (status == ProxyData::Status::Unsupported)
				? Result::Unsupported
				: (status == ProxyData::Status::IncorrectSecret)
				? Result::IncorrectSecret
				: Result::Invalid;
		}
		const auto contains = controller->contains(proxy);
		const auto toast = (contains
			? tr::lng_proxy_add_from_clipboard_existing_toast
			: tr::lng_proxy_add_from_clipboard_good_toast)(tr::now);
		if (isSingle) {
			show->showToast(toast);
		}
		if (!contains) {
			controller->addNewItem(proxy);
		}
		return Result::Success;
	};

	auto success = Result::Failed;
	for (const auto &maybeUrl : maybeUrls) {
		const auto trimmed = maybeUrl.trimmed();
		const auto local = Core::TryConvertUrlToLocal(trimmed);
		const auto check = local.isEmpty() ? trimmed : local;
		const auto result = proceedUrl(check);
		if (success != Result::Success) {
			success = result;
		}
	}

	if (success != Result::Success) {
		if (success == Result::Failed) {
			show->showToast(
				tr::lng_proxy_add_from_clipboard_failed_toast(tr::now));
		} else {
			show->showBox(Ui::MakeInformBox(
				((success == Result::IncorrectSecret)
					? tr::lng_proxy_incorrect_secret(tr::now, tr::rich)
					: (success == Result::Unsupported)
					? tr::lng_proxy_unsupported(tr::now, tr::rich)
					: tr::lng_proxy_invalid(tr::now, tr::rich))));
		}
	}
}

class HostInput : public Ui::MaskedInputField {
public:
	HostInput(
		QWidget *parent,
		const style::InputField &st,
		rpl::producer<QString> placeholder,
		const QString &val);

protected:
	void correctValue(
		const QString &was,
		int wasCursor,
		QString &now,
		int &nowCursor) override;
};

HostInput::HostInput(
	QWidget *parent,
	const style::InputField &st,
	rpl::producer<QString> placeholder,
	const QString &val)
: MaskedInputField(parent, st, std::move(placeholder), val) {
}

void HostInput::correctValue(
		const QString &was,
		int wasCursor,
		QString &now,
		int &nowCursor) {
	QString newText;
	int newCursor = nowCursor;
	newText.reserve(now.size());
	for (auto i = 0, l = int(now.size()); i < l; ++i) {
		if (now[i] == ',') {
			newText.append('.');
		} else {
			newText.append(now[i]);
		}
	}
	setCorrectedText(now, nowCursor, newText, newCursor);
}

class Base64UrlInput : public Ui::MaskedInputField {
public:
	Base64UrlInput(
		QWidget *parent,
		const style::InputField &st,
		rpl::producer<QString> placeholder,
		const QString &val);

protected:
	void correctValue(
		const QString &was,
		int wasCursor,
		QString &now,
		int &nowCursor) override;

};

Base64UrlInput::Base64UrlInput(
	QWidget *parent,
	const style::InputField &st,
	rpl::producer<QString> placeholder,
	const QString &val)
: MaskedInputField(parent, st, std::move(placeholder), val) {
	static const auto RegExp = QRegularExpression("^[a-zA-Z0-9_\\-]+$");
	if (!RegExp.match(val).hasMatch()) {
		setText(QString());
	}
}

void Base64UrlInput::correctValue(
		const QString &was,
		int wasCursor,
		QString &now,
		int &nowCursor) {
	QString newText;
	newText.reserve(now.size());
	auto newPos = nowCursor;
	for (auto i = 0, l = int(now.size()); i < l; ++i) {
		const auto ch = now[i];
		if ((ch >= '0' && ch <= '9')
			|| (ch >= 'a' && ch <= 'z')
			|| (ch >= 'A' && ch <= 'Z')
			|| (ch == '-')
			|| (ch == '_')) {
			newText.append(ch);
		} else if (i < nowCursor) {
			--newPos;
		}
	}
	setCorrectedText(now, nowCursor, newText, newPos);
}

class ProxyRow : public Ui::RippleButton {
public:
	using View = ProxiesBoxController::ItemView;
	using State = ProxiesBoxController::ItemState;

	ProxyRow(QWidget *parent, View &&view);

	void updateFields(View &&view);

	rpl::producer<> deleteClicks() const;
	rpl::producer<> restoreClicks() const;
	rpl::producer<> editClicks() const;
	rpl::producer<> shareClicks() const;
	rpl::producer<> showQrClicks() const;

protected:
	int resizeGetHeight(int newWidth) override;

	void paintEvent(QPaintEvent *e) override;

private:
	void setupControls(View &&view);
	int countAvailableWidth() const;
	void radialAnimationCallback();
	void paintCheck(Painter &p);
	void showMenu();

	View _view;

	Ui::Text::String _title;
	object_ptr<Ui::IconButton> _menuToggle;
	rpl::event_stream<> _deleteClicks;
	rpl::event_stream<> _restoreClicks;
	rpl::event_stream<> _editClicks;
	rpl::event_stream<> _shareClicks;
	rpl::event_stream<> _showQrClicks;
	base::unique_qptr<Ui::DropdownMenu> _menu;

	bool _set = false;
	Ui::Animations::Simple _toggled;
	Ui::Animations::Simple _setAnimation;
	std::unique_ptr<Ui::InfiniteRadialAnimation> _progress;
	std::unique_ptr<Ui::InfiniteRadialAnimation> _checking;

	int _skipLeft = 0;
	int _skipRight = 0;

};

class ProxiesBox : public Ui::BoxContent {
public:
	using View = ProxiesBoxController::ItemView;

	ProxiesBox(
		QWidget*,
		not_null<ProxiesBoxController*> controller,
		Core::SettingsProxy &settings,
		const QString &highlightId = QString());

protected:
	void prepare() override;
	void showFinished() override;
	void keyPressEvent(QKeyEvent *e) override;

private:
	void setupContent();
	void setupTopButton();
	void createNoRowsLabel();
	void addNewProxy();
	void applyView(View &&view);
	void setupButtons(int id, not_null<ProxyRow*> button);
	int rowHeight() const;
	void refreshProxyForCalls();
	void refreshProxyRotation();

	not_null<ProxiesBoxController*> _controller;
	Core::SettingsProxy &_settings;
	QPointer<Ui::Checkbox> _tryIPv6;
	std::shared_ptr<Ui::RadioenumGroup<ProxyData::Settings>> _proxySettings;
	QPointer<Ui::SlideWrap<Ui::Checkbox>> _proxyForCalls;
	QPointer<Ui::SlideWrap<Ui::Checkbox>> _proxyRotation;
	QPointer<Ui::SlideWrap<Ui::VerticalLayout>> _proxyRotationOptions;
	QPointer<Ui::SettingsSlider> _proxyRotationTimeout;
	QPointer<Ui::DividerLabel> _about;
	base::unique_qptr<Ui::RpWidget> _noRows;
	object_ptr<Ui::VerticalLayout> _initialWrap;
	QPointer<Ui::VerticalLayout> _wrap;
	int _currentProxySupportsCallsId = 0;

	base::flat_map<int, base::unique_qptr<ProxyRow>> _rows;

	QPointer<Ui::RpWidget> _addProxyButton;
	QPointer<Ui::RpWidget> _shareListButton;
	QString _highlightId;

};

class ProxyBox final : public Ui::BoxContent {
public:
	ProxyBox(
		QWidget*,
		const ProxyData &data,
		Fn<void(ProxyData)> callback,
		Fn<void(ProxyData)> shareCallback);

private:
	using Type = ProxyData::Type;

	void prepare() override;
	void setInnerFocus() override {
		_host->setFocusFast();
	}

	void refreshButtons();
	ProxyData collectData();
	void save();
	void share();
	void setupControls(const ProxyData &data);
	void setupTypes();
	void setupSocketAddress(const ProxyData &data);
	void setupCredentials(const ProxyData &data);
	void setupMtprotoCredentials(const ProxyData &data);

	void addLabel(
		not_null<Ui::VerticalLayout*> parent,
		const QString &text) const;

	const bool _allowShare = false;
	Fn<void(ProxyData)> _callback;
	Fn<void(ProxyData)> _shareCallback;

	object_ptr<Ui::VerticalLayout> _content;

	std::shared_ptr<Ui::RadioenumGroup<Type>> _type;

	QPointer<Ui::SlideWrap<>> _aboutSponsored;
	QPointer<HostInput> _host;
	QPointer<Ui::NumberInput> _port;
	QPointer<Ui::InputField> _user;
	QPointer<Ui::PasswordInput> _password;
	QPointer<Base64UrlInput> _secret;

	QPointer<Ui::SlideWrap<Ui::VerticalLayout>> _credentials;
	QPointer<Ui::SlideWrap<Ui::VerticalLayout>> _mtprotoCredentials;

};

ProxyRow::ProxyRow(QWidget *parent, View &&view)
: RippleButton(parent, st::proxyRowRipple)
, _menuToggle(this, st::topBarMenuToggle) {
	setupControls(std::move(view));
}

rpl::producer<> ProxyRow::deleteClicks() const {
	return _deleteClicks.events();
}

rpl::producer<> ProxyRow::restoreClicks() const {
	return _restoreClicks.events();
}

rpl::producer<> ProxyRow::editClicks() const {
	return _editClicks.events();
}

rpl::producer<> ProxyRow::shareClicks() const {
	return _shareClicks.events();
}

rpl::producer<> ProxyRow::showQrClicks() const {
	return _showQrClicks.events();
}

void ProxyRow::setupControls(View &&view) {
	updateFields(std::move(view));
	_toggled.stop();
	_setAnimation.stop();

	_menuToggle->addClickHandler([=] { showMenu(); });
}

int ProxyRow::countAvailableWidth() const {
	return width() - _skipLeft - _skipRight;
}

void ProxyRow::updateFields(View &&view) {
	if (_view.selected != view.selected) {
		_toggled.start(
			[=] { update(); },
			view.selected ? 0. : 1.,
			view.selected ? 1. : 0.,
			st::defaultRadio.duration);
	}
	_view = std::move(view);
	const auto endpoint = _view.host + ':' + QString::number(_view.port);
	_title.setMarkedText(
		st::proxyRowTitleStyle,
		TextWithEntities()
			.append(_view.type)
			.append(' ')
			.append(tr::link(endpoint, QString())),
		Ui::ItemTextDefaultOptions());

	const auto state = _view.state;
	if (state == State::Connecting) {
		if (!_progress) {
			_progress = std::make_unique<Ui::InfiniteRadialAnimation>(
				[=] { radialAnimationCallback(); },
				st::proxyCheckingAnimation);
		}
		_progress->start();
	} else if (_progress) {
		_progress->stop();
	}
	if (state == State::Checking) {
		if (!_checking) {
			_checking = std::make_unique<Ui::InfiniteRadialAnimation>(
				[=] { radialAnimationCallback(); },
				st::proxyCheckingAnimation);
			_checking->start();
		}
	} else {
		_checking = nullptr;
	}
	const auto set = (state == State::Connecting || state == State::Online);
	if (_set != set) {
		_set = set;
		_setAnimation.start(
			[=] { update(); },
			_set ? 0. : 1.,
			_set ? 1. : 0.,
			st::defaultRadio.duration);
	}

	setPointerCursor(!_view.deleted);

	update();
}

void ProxyRow::radialAnimationCallback() {
	if (!anim::Disabled()) {
		update();
	}
}

int ProxyRow::resizeGetHeight(int newWidth) {
	const auto result = st::proxyRowPadding.top()
		+ st::semiboldFont->height
		+ st::proxyRowSkip
		+ st::normalFont->height
		+ st::proxyRowPadding.bottom();
	auto right = st::proxyRowPadding.right();
	_menuToggle->moveToRight(
		right,
		(result - _menuToggle->height()) / 2,
		newWidth);
	right += _menuToggle->width();
	_skipRight = right;
	_skipLeft = st::proxyRowPadding.left()
		+ st::proxyRowIconSkip;
	return result;
}

void ProxyRow::paintEvent(QPaintEvent *e) {
	Painter p(this);

	if (!_view.deleted) {
		paintRipple(p, 0, 0);
	}

	const auto left = _skipLeft;
	const auto availableWidth = countAvailableWidth();
	auto top = st::proxyRowPadding.top();

	if (_view.deleted) {
		p.setOpacity(st::stickersRowDisabledOpacity);
	}

	paintCheck(p);

	p.setPen(st::proxyRowTitleFg);
	p.setFont(st::semiboldFont);
	p.setTextPalette(st::proxyRowTitlePalette);
	_title.drawLeftElided(p, left, top, availableWidth, width());
	top += st::semiboldFont->height + st::proxyRowSkip;

	const auto statusFg = [&] {
		switch (_view.state) {
		case State::Online:
			return st::proxyRowStatusFgOnline;
		case State::Unavailable:
			return st::proxyRowStatusFgOffline;
		case State::Available:
			return st::proxyRowStatusFgAvailable;
		default:
			return st::proxyRowStatusFg;
		}
	}();
	const auto status = [&] {
		switch (_view.state) {
		case State::Available:
			return tr::lng_proxy_available(
				tr::now,
				lt_ping,
				QString::number(_view.ping));
		case State::Checking:
			return tr::lng_proxy_checking(tr::now);
		case State::Connecting:
			return tr::lng_proxy_connecting(tr::now);
		case State::Online:
			return tr::lng_proxy_online(tr::now);
		case State::Unavailable:
			return tr::lng_proxy_unavailable(tr::now);
		}
		Unexpected("State in ProxyRow::paintEvent.");
	}();
	p.setPen(_view.deleted ? st::proxyRowStatusFg : statusFg);
	p.setFont(st::normalFont);

	auto statusLeft = left;
	if (_checking) {
		_checking->draw(
			p,
			{
				st::proxyCheckingPosition.x() + statusLeft,
				st::proxyCheckingPosition.y() + top
			},
			width());
		statusLeft += st::proxyCheckingPosition.x()
			+ st::proxyCheckingAnimation.size.width()
			+ st::proxyCheckingSkip;
	}
	p.drawTextLeft(statusLeft, top, width(), status);
	top += st::normalFont->height + st::proxyRowPadding.bottom();
}

void ProxyRow::paintCheck(Painter &p) {
	const auto loading = _progress
		? _progress->computeState()
		: Ui::RadialState{ 0., 0, arc::kFullLength };
	const auto toggled = _toggled.value(_view.selected ? 1. : 0.)
		* (1. - loading.shown);
	const auto _st = &st::defaultRadio;
	const auto set = _setAnimation.value(_set ? 1. : 0.);

	PainterHighQualityEnabler hq(p);

	const auto left = st::proxyRowPadding.left();
	const auto top = (height() - _st->diameter - _st->thickness) / 2;
	const auto outerWidth = width();

	auto pen = anim::pen(_st->untoggledFg, _st->toggledFg, toggled * set);
	pen.setWidth(_st->thickness);
	pen.setCapStyle(Qt::RoundCap);
	p.setPen(pen);
	p.setBrush(_st->bg);
	const auto rect = style::rtlrect(QRectF(left, top, _st->diameter, _st->diameter).marginsRemoved(QMarginsF(_st->thickness / 2., _st->thickness / 2., _st->thickness / 2., _st->thickness / 2.)), outerWidth);
	if (_progress && loading.shown > 0 && anim::Disabled()) {
		anim::DrawStaticLoading(
			p,
			rect,
			_st->thickness,
			pen.color(),
			_st->bg);
	} else if (loading.arcLength < arc::kFullLength) {
		p.drawArc(rect, loading.arcFrom, loading.arcLength);
	} else {
		p.drawEllipse(rect);
	}

	if (toggled > 0 && (!_progress || !anim::Disabled())) {
		p.setPen(Qt::NoPen);
		p.setBrush(anim::brush(_st->untoggledFg, _st->toggledFg, toggled * set));

		auto skip0 = _st->diameter / 2., skip1 = _st->skip / 10., checkSkip = skip0 * (1. - toggled) + skip1 * toggled;
		p.drawEllipse(style::rtlrect(QRectF(left, top, _st->diameter, _st->diameter).marginsRemoved(QMarginsF(checkSkip, checkSkip, checkSkip, checkSkip)), outerWidth));
	}
}

void ProxyRow::showMenu() {
	if (_menu) {
		return;
	}
	_menu = base::make_unique_q<Ui::DropdownMenu>(
		window(),
		st::dropdownMenuWithIcons);
	const auto weak = _menu.get();
	_menu->setHiddenCallback([=] {
		weak->deleteLater();
		if (_menu == weak) {
			_menuToggle->setForceRippled(false);
		}
	});
	_menu->setShowStartCallback([=] {
		if (_menu == weak) {
			_menuToggle->setForceRippled(true);
		}
	});
	_menu->setHideStartCallback([=] {
		if (_menu == weak) {
			_menuToggle->setForceRippled(false);
		}
	});
	_menuToggle->installEventFilter(_menu);
	const auto addAction = [&](
			const QString &text,
			Fn<void()> callback,
			const style::icon *icon) {
		return _menu->addAction(text, std::move(callback), icon);
	};
	addAction(tr::lng_proxy_menu_edit(tr::now), [=] {
		_editClicks.fire({});
	}, &st::menuIconEdit);
	if (_view.supportsShare) {
		addAction(tr::lng_proxy_edit_share(tr::now), [=] {
			_shareClicks.fire({});
		}, &st::menuIconShare);
		addAction(tr::lng_group_invite_context_qr(tr::now), [=] {
			_showQrClicks.fire({});
		}, &st::menuIconQrCode);
	}
	if (_view.deleted) {
		addAction(tr::lng_proxy_menu_restore(tr::now), [=] {
			_restoreClicks.fire({});
		}, &st::menuIconRestore);
	} else {
		addAction(tr::lng_proxy_menu_delete(tr::now), [=] {
			_deleteClicks.fire({});
		}, &st::menuIconDelete);
	}
	const auto parentTopLeft = window()->mapToGlobal(QPoint());
	const auto buttonTopLeft = _menuToggle->mapToGlobal(QPoint());
	const auto parent = QRect(parentTopLeft, window()->size());
	const auto button = QRect(buttonTopLeft, _menuToggle->size());
	const auto bottom = button.y()
		+ st::proxyDropdownDownPosition.y()
		+ _menu->height()
		- parent.y();
	const auto top = button.y()
		+ st::proxyDropdownUpPosition.y()
		- _menu->height()
		- parent.y();
	if (bottom > parent.height() && top >= 0) {
		const auto left = button.x()
			+ button.width()
			+ st::proxyDropdownUpPosition.x()
			- _menu->width()
			- parent.x();
		_menu->move(left, top);
		_menu->showAnimated(Ui::PanelAnimation::Origin::BottomRight);
	} else {
		const auto left = button.x()
			+ button.width()
			+ st::proxyDropdownDownPosition.x()
			- _menu->width()
			- parent.x();
		_menu->move(left, bottom - _menu->height());
		_menu->showAnimated(Ui::PanelAnimation::Origin::TopRight);
	}
}

ProxiesBox::ProxiesBox(
	QWidget*,
	not_null<ProxiesBoxController*> controller,
	Core::SettingsProxy &settings,
	const QString &highlightId)
: _controller(controller)
, _settings(settings)
, _initialWrap(this)
, _highlightId(highlightId) {
	_controller->views(
	) | rpl::on_next([=](View &&view) {
		applyView(std::move(view));
	}, lifetime());
}

void ProxiesBox::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Copy
		|| (e->key() == Qt::Key_C && e->modifiers() == Qt::ControlModifier)) {
		_controller->shareItems();
	} else if (e->key() == Qt::Key_Paste
		|| (e->key() == Qt::Key_V && e->modifiers() == Qt::ControlModifier)) {
		AddProxyFromClipboard(_controller, uiShow());
	} else {
		BoxContent::keyPressEvent(e);
	}
}

void ProxiesBox::prepare() {
	setTitle(tr::lng_proxy_settings());

	_addProxyButton = addButton(tr::lng_proxy_add(), [=] { addNewProxy(); });
	addButton(tr::lng_close(), [=] { closeBox(); });

	setupTopButton();
	setupContent();
}

void ProxiesBox::showFinished() {
	if (_highlightId == u"proxy/add-proxy"_q) {
		if (_addProxyButton) {
			_highlightId = QString();
			Settings::HighlightWidget(
				_addProxyButton,
				{ .rippleShape = true });
		}
	} else if (_highlightId == u"proxy/share-list"_q) {
		if (_shareListButton) {
			_highlightId = QString();
			Settings::HighlightWidget(_shareListButton);
		}
	}
}

void ProxiesBox::setupTopButton() {
	const auto top = addTopButton(st::infoTopBarMenu);
	const auto menu
		= top->lifetime().make_state<base::unique_qptr<Ui::PopupMenu>>();

	top->setClickedCallback([=] {
		*menu = base::make_unique_q<Ui::PopupMenu>(
			top,
			st::popupMenuWithIcons);
		const auto raw = menu->get();
		const auto addAction = Ui::Menu::CreateAddActionCallback(raw);
		addAction({
			.text = tr::lng_proxy_add_from_clipboard(tr::now),
			.handler = [=] { AddProxyFromClipboard(_controller, uiShow()); },
			.icon = &st::menuIconImportTheme,
		});
		if (!_rows.empty()) {
			addAction({
				.text = tr::lng_group_invite_context_delete_all(tr::now),
				.handler = [=] { _controller->deleteItems(); },
				.icon = &st::menuIconDeleteAttention,
				.isAttention = true,
			});
		}
		raw->setForcedOrigin(Ui::PanelAnimation::Origin::TopRight);
		top->setForceRippled(true);
		raw->setDestroyedCallback([=] {
			if (const auto strong = top.data()) {
				strong->setForceRippled(false);
			}
		});
		raw->popup(
			top->mapToGlobal(
				QPoint(top->width(), top->height() - st::lineWidth * 3)));
		return true;
	});
}

void ProxiesBox::setupContent() {
	const auto inner = setInnerWidget(object_ptr<Ui::VerticalLayout>(this));

	_tryIPv6 = inner->add(
		object_ptr<Ui::Checkbox>(
			inner,
			tr::lng_connection_try_ipv6(tr::now),
			_settings.tryIPv6()),
		st::proxyTryIPv6Padding);
	_proxySettings
		= std::make_shared<Ui::RadioenumGroup<ProxyData::Settings>>(
			_settings.settings());
	inner->add(
		object_ptr<Ui::Radioenum<ProxyData::Settings>>(
			inner,
			_proxySettings,
			ProxyData::Settings::Disabled,
			tr::lng_proxy_disable(tr::now)),
		st::proxyUsePadding);
	inner->add(
		object_ptr<Ui::Radioenum<ProxyData::Settings>>(
			inner,
			_proxySettings,
			ProxyData::Settings::System,
			tr::lng_proxy_use_system_settings(tr::now)),
		st::proxyUsePadding);
	inner->add(
		object_ptr<Ui::Radioenum<ProxyData::Settings>>(
			inner,
			_proxySettings,
			ProxyData::Settings::Enabled,
			tr::lng_proxy_use_custom(tr::now)),
		st::proxyUsePadding);
	_proxyForCalls = inner->add(
		object_ptr<Ui::SlideWrap<Ui::Checkbox>>(
			inner,
			object_ptr<Ui::Checkbox>(
				inner,
				tr::lng_proxy_use_for_calls(tr::now),
				_settings.useProxyForCalls()),
			style::margins(
				0,
				st::proxyUsePadding.top(),
				0,
				st::proxyUsePadding.bottom())),
		style::margins(
			st::proxyTryIPv6Padding.left(),
			0,
			st::proxyTryIPv6Padding.right(),
			st::proxyTryIPv6Padding.top()));
	_proxyRotation = inner->add(
		object_ptr<Ui::SlideWrap<Ui::Checkbox>>(
			inner,
			object_ptr<Ui::Checkbox>(
				inner,
				tr::lng_proxy_auto_switch(tr::now),
				_settings.proxyRotationEnabled()),
			style::margins(
				0,
				st::proxyUsePadding.top(),
				0,
				st::proxyUsePadding.bottom())),
		style::margins(
			st::proxyTryIPv6Padding.left(),
			0,
			st::proxyTryIPv6Padding.right(),
			st::proxyTryIPv6Padding.top()));
	_proxyRotationOptions = inner->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			inner,
			object_ptr<Ui::VerticalLayout>(inner)));
	_proxyRotationTimeout = _proxyRotationOptions->entity()->add(
		object_ptr<Ui::SettingsSlider>(
			_proxyRotationOptions->entity(),
			st::settingsSlider),
		st::settingsBigScalePadding);
	for (const auto seconds : Core::SettingsProxy::kProxyRotationTimeouts) {
		_proxyRotationTimeout->addSection(
			tr::lng_proxy_auto_switch_timeout(
				tr::now,
				lt_count,
				seconds));
	}
	_proxyRotationTimeout->setActiveSectionFast(
		ClosestProxyRotationTimeoutSection(_settings.proxyRotationTimeout()));
	_proxyRotationOptions->entity()->add(
		object_ptr<Ui::FlatLabel>(
			_proxyRotationOptions->entity(),
			tr::lng_proxy_auto_switch_about(tr::now),
			st::boxDividerLabel),
		st::proxyAboutPadding);

	_about = inner->add(
		object_ptr<Ui::DividerLabel>(
			inner,
			object_ptr<Ui::FlatLabel>(
				inner,
				tr::lng_proxy_about(tr::now),
				st::boxDividerLabel),
			st::proxyAboutPadding),
		style::margins(0, 0, 0, st::proxyRowPadding.top()));

	_wrap = inner->add(std::move(_initialWrap));
	inner->add(object_ptr<Ui::FixedHeightWidget>(
		inner,
		st::proxyRowPadding.bottom()));

	_proxySettings->setChangedCallback([=](ProxyData::Settings value) {
		if (!_controller->setProxySettings(value)) {
			_proxySettings->setValue(_settings.settings());
			addNewProxy();
		}
		refreshProxyForCalls();
		refreshProxyRotation();
	});
	_tryIPv6->checkedChanges(
	) | rpl::on_next([=](bool checked) {
		_controller->setTryIPv6(checked);
	}, _tryIPv6->lifetime());

	_controller->proxySettingsValue(
	) | rpl::on_next([=](ProxyData::Settings value) {
		_proxySettings->setValue(value);
		refreshProxyForCalls();
		refreshProxyRotation();
	}, inner->lifetime());

	_proxyForCalls->entity()->checkedChanges(
	) | rpl::on_next([=](bool checked) {
		_controller->setProxyForCalls(checked);
	}, _proxyForCalls->lifetime());
	_proxyRotation->entity()->checkedChanges(
	) | rpl::on_next([=](bool checked) {
		_controller->setProxyRotationEnabled(checked);
		refreshProxyRotation();
	}, _proxyRotation->lifetime());
	_proxyRotationTimeout->sectionActivated(
	) | rpl::on_next([=](int section) {
		_controller->setProxyRotationTimeout(
			Core::SettingsProxy::kProxyRotationTimeouts[section]);
	}, _proxyRotationTimeout->lifetime());

	if (_rows.empty()) {
		createNoRowsLabel();
	}
	refreshProxyForCalls();
	refreshProxyRotation();
	_proxyForCalls->finishAnimating();
	_proxyRotation->finishAnimating();
	_proxyRotationOptions->finishAnimating();

	{
		const auto wrap = inner->add(
			object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
				inner,
				object_ptr<Ui::VerticalLayout>(inner)));
		const auto shareList = Settings::AddButtonWithIcon(
			wrap->entity(),
			tr::lng_proxy_edit_share_list_button(),
			st::settingsButton,
			{ &st::menuIconCopy });
		_shareListButton = shareList;
		shareList->setClickedCallback([=] {
			_controller->shareItems();
		});
		wrap->toggleOn(_controller->listShareableChanges());
		wrap->finishAnimating();
	}

	inner->resizeToWidth(st::boxWideWidth);

	inner->heightValue(
	) | rpl::map([=](int height) {
		return std::min(
			std::max(height, _about->y()
				+ _about->height()
				+ 3 * rowHeight()),
			st::boxMaxListHeight);
	}) | rpl::distinct_until_changed(
	) | rpl::on_next([=](int height) {
		setDimensions(st::boxWideWidth, height);
	}, inner->lifetime());
}

void ProxiesBox::refreshProxyForCalls() {
	if (!_proxyForCalls) {
		return;
	}
	_proxyForCalls->toggle(
		(_proxySettings->current() == ProxyData::Settings::Enabled
			&& _currentProxySupportsCallsId != 0),
		anim::type::normal);
}

void ProxiesBox::refreshProxyRotation() {
	if (!_proxyRotation || !_proxyRotationOptions) {
		return;
	}
	const auto visible = (_proxySettings->current()
			== ProxyData::Settings::Enabled)
		&& _settings.selected()
		&& (_settings.list().size() > 1);
	_proxyRotation->toggle(visible, anim::type::normal);
	_proxyRotationOptions->toggle(
		visible && _proxyRotation->entity()->checked(),
		anim::type::normal);
}

int ProxiesBox::rowHeight() const {
	return st::proxyRowPadding.top()
		+ st::semiboldFont->height
		+ st::proxyRowSkip
		+ st::normalFont->height
		+ st::proxyRowPadding.bottom();
}

void ProxiesBox::addNewProxy() {
	getDelegate()->show(_controller->addNewItemBox());
}

void ProxiesBox::applyView(View &&view) {
	if (view.selected) {
		_currentProxySupportsCallsId = view.supportsCalls ? view.id : 0;
	} else if (view.id == _currentProxySupportsCallsId) {
		_currentProxySupportsCallsId = 0;
	}
	refreshProxyForCalls();

	const auto id = view.id;
	const auto i = _rows.find(id);
	if (i == _rows.end()) {
		const auto wrap = _wrap
			? _wrap.data()
			: _initialWrap.data();
		const auto &[i, ok] = _rows.emplace(id, nullptr);
		i->second.reset(wrap->insert(
			0,
			object_ptr<ProxyRow>(
				wrap,
				std::move(view))));
		setupButtons(id, i->second.get());
		if (_noRows) {
			_noRows.reset();
		}
		wrap->resizeToWidth(width());
	} else if (view.host.isEmpty()) {
		_rows.erase(i);
	} else {
		i->second->updateFields(std::move(view));
	}
	refreshProxyRotation();
}

void ProxiesBox::createNoRowsLabel() {
	_noRows.reset(_wrap->add(
		object_ptr<Ui::FixedHeightWidget>(
			_wrap,
			rowHeight()),
		st::proxyEmptyListPadding));
	_noRows->resize(
		(st::boxWideWidth
			- st::proxyEmptyListPadding.left()
			- st::proxyEmptyListPadding.right()),
		_noRows->height());
	const auto label = Ui::CreateChild<Ui::FlatLabel>(
		_noRows.get(),
		tr::lng_proxy_description(tr::now),
		st::proxyEmptyListLabel);
	_noRows->widthValue(
	) | rpl::on_next([=](int width) {
		label->resizeToWidth(width);
		label->moveToLeft(0, 0);
	}, label->lifetime());
}

void ProxiesBox::setupButtons(int id, not_null<ProxyRow*> button) {
	button->deleteClicks(
	) | rpl::on_next([=] {
		_controller->deleteItem(id);
	}, button->lifetime());

	button->restoreClicks(
	) | rpl::on_next([=] {
		_controller->restoreItem(id);
	}, button->lifetime());

	button->editClicks(
	) | rpl::on_next([=] {
		getDelegate()->show(_controller->editItemBox(id));
	}, button->lifetime());

	rpl::merge(
		button->shareClicks() | rpl::map_to(false),
		button->showQrClicks() | rpl::map_to(true)
	) | rpl::on_next([=](bool qr) {
		_controller->shareItem(id, qr);
	}, button->lifetime());

	button->clicks(
	) | rpl::on_next([=] {
		_controller->applyItem(id);
	}, button->lifetime());
}

ProxyBox::ProxyBox(
	QWidget*,
	const ProxyData &data,
	Fn<void(ProxyData)> callback,
	Fn<void(ProxyData)> shareCallback)
: _allowShare(data.type != Type::None)
, _callback(std::move(callback))
, _shareCallback(std::move(shareCallback))
, _content(this) {
	setupControls(data);
}

void ProxyBox::prepare() {
	setTitle(tr::lng_proxy_edit());

	connect(_host.data(), &HostInput::changed, [=] {
		Ui::PostponeCall(_host, [=] {
			const auto host = _host->getLastText().trimmed();
			static const auto mask = QRegularExpression(
				u"^\\d+\\.\\d+\\.\\d+\\.\\d+:(\\d*)$"_q);
			const auto match = mask.match(host);
			if (_host->cursorPosition() == host.size()
				&& match.hasMatch()) {
				const auto port = match.captured(1);
				_port->setText(port);
				_port->setCursorPosition(port.size());
				_port->setFocus();
				_host->setText(host.mid(0, host.size() - port.size() - 1));
			}
		});
	});
	_port.data()->events(
	) | rpl::on_next([=](not_null<QEvent*> e) {
		if (e->type() == QEvent::KeyPress
			&& (static_cast<QKeyEvent*>(e.get())->key() == Qt::Key_Backspace)
			&& _port->cursorPosition() == 0) {
			_host->setCursorPosition(_host->getLastText().size());
			_host->setFocus();
		}
	}, _port->lifetime());

	const auto submit = [=] {
		if (_host->hasFocus()
			&& !_host->getLastText().trimmed().isEmpty()) {
			_port->setFocus();
		} else if (_port->hasFocus()
			&& !_port->getLastText().trimmed().isEmpty()) {
			if (_type->current() == Type::Mtproto) {
				_secret->setFocus();
			} else {
				_user->setFocus();
			}
		} else if (_user->hasFocus()) {
			_password->setFocus();
		} else {
			save();
		}
	};
	connect(_host.data(), &Ui::MaskedInputField::submitted, submit);
	connect(_port.data(), &Ui::MaskedInputField::submitted, submit);
	_user->submits(
	) | rpl::on_next(submit, _user->lifetime());
	connect(_password.data(), &Ui::MaskedInputField::submitted, submit);
	connect(_secret.data(), &Ui::MaskedInputField::submitted, submit);

	refreshButtons();
	setDimensionsToContent(st::boxWideWidth, _content);
}

void ProxyBox::refreshButtons() {
	clearButtons();
	addButton(tr::lng_settings_save(), [=] { save(); });
	addButton(tr::lng_cancel(), [=] { closeBox(); });

	const auto type = _type->current();
	if (_allowShare
		&& (type == Type::Socks5 || type == Type::Mtproto)) {
		addLeftButton(tr::lng_proxy_share(), [=] { share(); });
	}
}

void ProxyBox::save() {
	if (const auto data = collectData()) {
		_callback(data);
		closeBox();
	}
}

void ProxyBox::share() {
	if (const auto data = collectData()) {
		_shareCallback(data);
	}
}

ProxyData ProxyBox::collectData() {
	auto result = ProxyData();
	result.type = _type->current();
	result.host = _host->getLastText().trimmed();
	result.port = _port->getLastText().trimmed().toInt();
	result.user = (result.type == Type::Mtproto)
		? QString()
		: _user->getLastText();
	result.password = (result.type == Type::Mtproto)
		? _secret->getLastText()
		: _password->getLastText();
	if (result.host.isEmpty()) {
		_host->showError();
	} else if (!result.port) {
		_port->showError();
	} else if ((result.type == Type::Http || result.type == Type::Socks5)
		&& !result.password.isEmpty() && result.user.isEmpty()) {
		_user->showError();
	} else if (result.type == Type::Mtproto && !result.valid()) {
		_secret->showError();
	} else if (!result) {
		_host->showError();
	} else {
		return result;
	}
	return ProxyData();
}

void ProxyBox::setupTypes() {
	const auto types = std::vector<std::pair<Type, QString>>{
		{ Type::Mtproto, u"MTPROTO"_q },
		{ Type::Socks5, u"SOCKS5"_q },
		{ Type::Http, u"HTTP"_q },
	};
	for (const auto &[type, label] : types) {
		_content->add(
			object_ptr<Ui::Radioenum<Type>>(
				_content,
				_type,
				type,
				label),
			st::proxyEditTypePadding);
	}
	_aboutSponsored = _content->add(object_ptr<Ui::SlideWrap<>>(
		_content,
		object_ptr<Ui::PaddingWrap<>>(
			_content,
			object_ptr<Ui::FlatLabel>(
				_content,
				tr::lng_proxy_sponsor_warning(tr::now),
				st::boxDividerLabel),
			st::proxyAboutSponsorPadding)));
}

void ProxyBox::setupSocketAddress(const ProxyData &data) {
	addLabel(_content, tr::lng_proxy_address_label(tr::now));
	const auto address = _content->add(
		object_ptr<Ui::FixedHeightWidget>(
			_content,
			st::connectionHostInputField.heightMin),
		st::proxyEditInputPadding);
	_host = Ui::CreateChild<HostInput>(
		address,
		st::connectionHostInputField,
		tr::lng_connection_host_ph(),
		data.host);
	_port = Ui::CreateChild<Ui::NumberInput>(
		address,
		st::connectionPortInputField,
		tr::lng_connection_port_ph(),
		data.port ? QString::number(data.port) : QString(),
		65535);
	address->widthValue(
	) | rpl::on_next([=](int width) {
		_port->moveToRight(0, 0);
		_host->resize(
			width - _port->width() - st::proxyEditSkip,
			_host->height());
		_host->moveToLeft(0, 0);
	}, address->lifetime());
}

void ProxyBox::setupCredentials(const ProxyData &data) {
	_credentials = _content->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			_content,
			object_ptr<Ui::VerticalLayout>(_content)));
	const auto credentials = _credentials->entity();
	addLabel(credentials, tr::lng_proxy_credentials_optional(tr::now));
	_user = credentials->add(
		object_ptr<Ui::InputField>(
			credentials,
			st::connectionUserInputField,
			tr::lng_connection_user_ph(),
			data.user),
		st::proxyEditInputPadding);

	auto passwordWrap = object_ptr<Ui::RpWidget>(credentials);
	_password = Ui::CreateChild<Ui::PasswordInput>(
		passwordWrap.data(),
		st::connectionPasswordInputField,
		tr::lng_connection_password_ph(),
		(data.type == Type::Mtproto) ? QString() : data.password);
	_password->move(0, 0);
	_password->heightValue(
	) | rpl::on_next([=, wrap = passwordWrap.data()](int height) {
		wrap->resize(wrap->width(), height);
	}, _password->lifetime());
	passwordWrap->widthValue(
	) | rpl::on_next([=](int width) {
		_password->resize(width, _password->height());
	}, _password->lifetime());
	credentials->add(std::move(passwordWrap), st::proxyEditInputPadding);
}

void ProxyBox::setupMtprotoCredentials(const ProxyData &data) {
	_mtprotoCredentials = _content->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			_content,
			object_ptr<Ui::VerticalLayout>(_content)));
	const auto mtproto = _mtprotoCredentials->entity();
	addLabel(mtproto, tr::lng_proxy_credentials(tr::now));

	auto secretWrap = object_ptr<Ui::RpWidget>(mtproto);
	_secret = Ui::CreateChild<Base64UrlInput>(
		secretWrap.data(),
		st::connectionUserInputField,
		tr::lng_connection_proxy_secret_ph(),
		(data.type == Type::Mtproto) ? data.password : QString());
	_secret->move(0, 0);
	_secret->heightValue(
	) | rpl::on_next([=, wrap = secretWrap.data()](int height) {
		wrap->resize(wrap->width(), height);
	}, _secret->lifetime());
	secretWrap->widthValue(
	) | rpl::on_next([=](int width) {
		_secret->resize(width, _secret->height());
	}, _secret->lifetime());
	mtproto->add(std::move(secretWrap), st::proxyEditInputPadding);
}

void ProxyBox::setupControls(const ProxyData &data) {
	_type = std::make_shared<Ui::RadioenumGroup<Type>>(
		(data.type == Type::None
			? Type::Mtproto
			: data.type));
	_content.create(this);
	_content->resizeToWidth(st::boxWideWidth);
	_content->moveToLeft(0, 0);

	setupTypes();
	setupSocketAddress(data);
	setupCredentials(data);
	setupMtprotoCredentials(data);

	const auto handleType = [=](Type type) {
		const auto credentialsShown
			= (type == Type::Http || type == Type::Socks5);
		const auto mtprotoShown = (type == Type::Mtproto);
		_credentials->toggle(credentialsShown, anim::type::instant);
		_mtprotoCredentials->toggle(mtprotoShown, anim::type::instant);
		_aboutSponsored->toggle(mtprotoShown, anim::type::instant);
		const auto credentialsPolicy = credentialsShown
			? Qt::StrongFocus
			: Qt::NoFocus;
		_user->rawTextEdit()->setFocusPolicy(credentialsPolicy);
		_password->setFocusPolicy(credentialsPolicy);
		_secret->setFocusPolicy(
			mtprotoShown ? Qt::StrongFocus : Qt::NoFocus);
	};
	_type->setChangedCallback([=](Type type) {
		handleType(type);
		refreshButtons();
	});
	handleType(_type->current());
}

void ProxyBox::addLabel(
		not_null<Ui::VerticalLayout*> parent,
		const QString &text) const {
	parent->add(
		object_ptr<Ui::FlatLabel>(
			parent,
			text,
			st::proxyEditTitle),
		st::proxyEditTitlePadding);
}

using Connection = MTP::details::AbstractConnection;
using Checker = MTP::ProxyCheckConnection;

} // namespace

ProxiesBoxController::ProxiesBoxController(not_null<Main::Account*> account)
: _account(account)
, _settings(Core::App().settings().proxy())
, _saveTimer([] { Local::writeSettings(); }) {
	_list = ranges::views::all(
		_settings.list()
	) | ranges::views::transform([&](const ProxyData &proxy) {
		return Item{ ++_idCounter, proxy };
	}) | ranges::to_vector;

	_settings.connectionTypeChanges(
	) | rpl::on_next([=] {
		_proxySettingsChanges.fire_copy(_settings.settings());
		const auto i = findByProxy(_settings.selected());
		if (i != end(_list)) {
			updateView(*i);
		}
	}, _lifetime);

	for (auto &item : _list) {
		refreshChecker(item);
	}
}

void ProxiesBoxController::ShowApplyConfirmation(
		Window::SessionController *controller,
		Type type,
		const QMap<QString, QString> &fields) {
	const auto proxy = ProxyDataFromFields(type, fields);
	if (!proxy) {
		const auto status = proxy.status();
		auto box = Ui::MakeInformBox(
			((status == ProxyData::Status::Unsupported)
				? tr::lng_proxy_unsupported(tr::now, tr::rich)
				: (status == ProxyData::Status::IncorrectSecret)
				? tr::lng_proxy_incorrect_secret(tr::now, tr::rich)
				: tr::lng_proxy_invalid(tr::now, tr::rich)));
		if (controller) {
			controller->uiShow()->showBox(std::move(box));
		} else {
			Ui::show(std::move(box));
		}
		return;
	}
	static const auto UrlStartRegExp = QRegularExpression(
		"^https://",
		QRegularExpression::CaseInsensitiveOption);
	static const auto UrlEndRegExp = QRegularExpression("/$");
	const auto displayed = "https://" + proxy.host + "/";
	const auto parsed = QUrl::fromUserInput(displayed);
	const auto displayUrl = !UrlClickHandler::IsSuspicious(displayed)
		? displayed
		: parsed.isValid()
		? QString::fromUtf8(parsed.toEncoded())
		: UrlClickHandler::ShowEncoded(displayed);
	const auto displayServer = QString(
		displayUrl
	).replace(
		UrlStartRegExp,
		QString()
	).replace(UrlEndRegExp, QString());
	const auto box = [=](not_null<Ui::GenericBox*> box) {
		box->setTitle(tr::lng_proxy_box_table_title());
		box->setStyle(st::proxyApplyBox);
		box->addTopButton(st::boxTitleClose, [=] {
			box->closeBox();
		});

		if (ProxyDataIsShareable(proxy)) {
			const auto account = controller
				? &controller->session().account()
				: &Core::App().activeAccount();
			const auto top = box->addTopButton(st::boxTitleMenu);
			const auto menu = top->lifetime().make_state<
				base::unique_qptr<Ui::PopupMenu>>();
			top->setClickedCallback([=] {
				*menu = base::make_unique_q<Ui::PopupMenu>(
					top,
					st::popupMenuWithIcons);
				const auto raw = menu->get();
				const auto addAction = Ui::Menu::CreateAddActionCallback(raw);
				addAction({
					.text = tr::lng_proxy_edit_share(tr::now),
					.handler = [=] {
						ShareProxy(box->uiShow(), account, proxy, false);
					},
					.icon = &st::menuIconShare,
				});
				addAction({
					.text = tr::lng_group_invite_context_qr(tr::now),
					.handler = [=] {
						ShareProxy(box->uiShow(), account, proxy, true);
					},
					.icon = &st::menuIconQrCode,
				});
				raw->setForcedOrigin(Ui::PanelAnimation::Origin::TopRight);
				top->setForceRippled(true);
				raw->setDestroyedCallback([=] {
					if (const auto strong = top.data()) {
						strong->setForceRippled(false);
					}
				});
				raw->popup(top->mapToGlobal(QPoint(
					top->width(),
					top->height() - st::lineWidth * 3)));
				return true;
			});
		}

		const auto table = box->addRow(
			object_ptr<Ui::TableLayout>(
				box,
				st::proxyApplyBoxTable),
			st::proxyApplyBoxTableMargin);
		const auto addRow = [&](
				rpl::producer<QString> label,
				object_ptr<Ui::RpWidget> value) {
			table->addRow(
				object_ptr<Ui::FlatLabel>(
					table,
					std::move(label),
					table->st().defaultLabel),
				std::move(value),
				st::proxyApplyBoxTableLabelMargin,
				st::proxyApplyBoxTableValueMargin);
		};
		const auto add = [&](
				const QString &value,
				rpl::producer<QString> label) {
			if (!value.isEmpty()) {
				constexpr auto kOneLineCount = 20;
				const auto oneLine = value.length() <= kOneLineCount;
				auto widget = object_ptr<Ui::FlatLabel>(
					table,
					rpl::single(Ui::Text::Wrapped(
						{ value },
						EntityType::Code,
						{})),
					(oneLine
						? table->st().defaultValue
						: st::proxyApplyBoxValueMultiline),
					st::defaultPopupMenu);
				addRow(std::move(label), std::move(widget));
			}
		};
		if (!displayServer.isEmpty()) {
			add(displayServer, tr::lng_proxy_box_server());
		}
		add(QString::number(proxy.port), tr::lng_proxy_box_port());
		if (type == Type::Socks5) {
			add(proxy.user, tr::lng_proxy_box_username());
			add(proxy.password, tr::lng_proxy_box_password());
		} else if (type == Type::Mtproto) {
			add(proxy.password, tr::lng_proxy_box_secret());
		}

		{
			struct ProxyCheckStatusState {
				Checker v4;
				Checker v6;
				rpl::variable<TextWithEntities> statusValue;
				bool finished = false;
			};
			const auto state
				= box->lifetime().make_state<ProxyCheckStatusState>();
			state->statusValue = Ui::Text::Link(
				tr::lng_proxy_box_check_status(tr::now));
			const auto weak = base::make_weak(box);
			auto statusWidget = object_ptr<Ui::FlatLabel>(
				table,
				state->statusValue.value(),
				table->st().defaultValue,
				st::defaultPopupMenu);
			const auto statusLabel = statusWidget.data();
			addRow(tr::lng_proxy_box_status(), std::move(statusWidget));
			const auto relayout = [=] {
				table->resizeToWidth(table->width());
			};
			const auto setUnavailable = [=] {
				state->statusValue = TextWithEntities{
					tr::lng_proxy_box_table_unavailable(tr::now),
				};
				statusLabel->setTextColorOverride(
					st::proxyRowStatusFgOffline->c);
				relayout();
			};
			const auto runCheck = [=] {
				if (!weak) {
					return;
				}
				const auto account = controller
					? &controller->session().account()
					: &Core::App().activeAccount();
				state->finished = false;
				state->statusValue = TextWithEntities{
					tr::lng_proxy_box_table_checking(tr::now),
				};
				statusLabel->setTextColorOverride(st::proxyRowStatusFg->c);
				relayout();
				MTP::StartProxyCheck(
					&account->mtp(),
					proxy,
					Core::App().settings().proxy().tryIPv6(),
					state->v4,
					state->v6,
					[=](Connection *raw, int ping) {
						if (!weak || state->finished) {
							return;
						}
						MTP::DropProxyChecker(state->v4, state->v6, raw);
						state->finished = true;
						MTP::ResetProxyCheckers(state->v4, state->v6);
						state->statusValue = TextWithEntities{
							tr::lng_proxy_box_table_available(
								tr::now,
								lt_ping,
								QString::number(ping)),
						};
						statusLabel->setTextColorOverride(
							st::proxyRowStatusFgAvailable->c);
						relayout();
					},
					[=](Connection *raw) {
						if (!weak || state->finished) {
							return;
						}
						MTP::DropProxyChecker(state->v4, state->v6, raw);
						if (!MTP::HasProxyCheckers(state->v4, state->v6)) {
							state->finished = true;
							setUnavailable();
						}
					});
				if (!MTP::HasProxyCheckers(state->v4, state->v6)) {
					state->finished = true;
					setUnavailable();
				}
			};
			statusLabel->setClickHandlerFilter([=](const auto &...) {
				auto &proxy = Core::App().settings().proxy();
				if (proxy.checkIpWarningShown()) {
					runCheck();
				} else {
					box->uiShow()->showBox(Ui::MakeConfirmBox({
						.text = tr::lng_proxy_check_ip_warning(),
						.confirmed = [=](Fn<void()> close) {
							auto &proxy = Core::App().settings().proxy();
							proxy.setCheckIpWarningShown(true);
							Local::writeSettings();
							close();
							runCheck();
						},
						.confirmText = tr::lng_proxy_check_ip_proceed(),
						.title = tr::lng_proxy_check_ip_warning_title(),
					}));
				}
				return false;
			});
		}

		if (type == Type::Mtproto) {
			table->addRow(
				object_ptr<Ui::FlatLabel>(
					table,
					tr::lng_proxy_sponsor_warning(),
					st::proxyApplyBoxSponsorLabel),
				object_ptr<Ui::RpWidget>(nullptr),
				st::proxyApplyBoxSponsorMargin,
				st::proxyApplyBoxSponsorMargin);
		}

		const auto enableButton = box->addButton(
			tr::lng_proxy_box_table_button(),
			[=] {
				auto &settings = Core::App().settings().proxy();
				if (settings.indexInList(proxy) < 0) {
					settings.addToList(proxy);
				}
				Core::App().setCurrentProxy(
					proxy,
					ProxyData::Settings::Enabled);
				Local::writeSettings();
				box->closeBox();
			});
		enableButton->setFullRadius(true);
		box->events() | rpl::on_next([=](not_null<QEvent*> e) {
			if ((e->type() != QEvent::KeyPress) || !enableButton) {
				return;
			}
			const auto k = static_cast<QKeyEvent*>(e.get());
			if (k->key() == Qt::Key_Enter || k->key() == Qt::Key_Return) {
				enableButton->clicked({}, Qt::LeftButton);
			}
		}, box->lifetime());
	};
	if (controller) {
		controller->uiShow()->showBox(Box(box));
	} else {
		Ui::show(Box(box));
	}
}

auto ProxiesBoxController::proxySettingsValue() const
-> rpl::producer<ProxyData::Settings> {
	return _proxySettingsChanges.events_starting_with_copy(
		_settings.settings()
	) | rpl::distinct_until_changed();
}

void ProxiesBoxController::refreshChecker(Item &item) {
	item.state = ItemState::Checking;
	const auto id = item.id;
	MTP::StartProxyCheck(
		&_account->mtp(),
		item.data,
		Core::App().settings().proxy().tryIPv6(),
		item.checker,
		item.checkerv6,
		[=](Connection *raw, int pingTime) {
			const auto item = ranges::find(
				_list,
				id,
				[](const Item &item) { return item.id; });
			if (item == end(_list)) {
				return;
			}
			MTP::DropProxyChecker(item->checker, item->checkerv6, raw);
			MTP::ResetProxyCheckers(item->checker, item->checkerv6);
			if (item->state == ItemState::Checking) {
				item->state = ItemState::Available;
				item->ping = pingTime;
				updateView(*item);
			}
		},
		[=](Connection *raw) {
			const auto item = ranges::find(
				_list,
				id,
				[](const Item &item) { return item.id; });
			if (item == end(_list)) {
				return;
			}
			MTP::DropProxyChecker(item->checker, item->checkerv6, raw);
			if (!MTP::HasProxyCheckers(item->checker, item->checkerv6)
				&& item->state == ItemState::Checking) {
				item->state = ItemState::Unavailable;
				updateView(*item);
			}
		});
	if (!MTP::HasProxyCheckers(item.checker, item.checkerv6)) {
		item.state = ItemState::Unavailable;
	}
}

object_ptr<Ui::BoxContent> ProxiesBoxController::CreateOwningBox(
		not_null<Main::Account*> account,
		const QString &highlightId) {
	auto controller = std::make_unique<ProxiesBoxController>(account);
	auto box = controller->create(highlightId);
	Ui::AttachAsChild(box, std::move(controller));
	return box;
}

object_ptr<Ui::BoxContent> ProxiesBoxController::create(
		const QString &highlightId) {
	auto result = Box<ProxiesBox>(this, _settings, highlightId);
	_show = result->uiShow();
	for (const auto &item : _list) {
		updateView(item);
	}
	return result;
}

auto ProxiesBoxController::findById(int id) -> std::vector<Item>::iterator {
	const auto result = ranges::find(
		_list,
		id,
		[](const Item &item) { return item.id; });
	Assert(result != end(_list));
	return result;
}

auto ProxiesBoxController::findByProxy(const ProxyData &proxy)
->std::vector<Item>::iterator {
	return ranges::find(
		_list,
		proxy,
		[](const Item &item) { return item.data; });
}

void ProxiesBoxController::deleteItem(int id) {
	setDeleted(id, true);
}

void ProxiesBoxController::deleteItems() {
	for (const auto &item : _list) {
		setDeleted(item.id, true);
	}
}

void ProxiesBoxController::restoreItem(int id) {
	setDeleted(id, false);
}

void ProxiesBoxController::shareItem(int id, bool qr) {
	share(findById(id)->data, qr);
}

void ProxiesBoxController::shareItems() {
	auto result = QString();
	for (const auto &item : _list) {
		if (!item.deleted && ProxyDataIsShareable(item.data)) {
			const auto link = ProxyDataToPublicLink(_account, item.data);
			if (!link.isEmpty()) {
				result += (result.isEmpty() ? QString() : u"\n\n"_q) + link;
			}
		}
	}
	if (result.isEmpty()) {
		return;
	}
	QGuiApplication::clipboard()->setText(result);
	_show->showToast({
		.text = { tr::lng_proxy_edit_share_list_toast(tr::now) },
		.iconLottie = u"toast/copy"_q,
		.iconLottieSize = st::toastLottieIconSize,
	});
}

void ProxiesBoxController::applyItem(int id) {
	auto item = findById(id);
	if (_settings.isEnabled() && (_settings.selected() == item->data)) {
		return;
	} else if (item->deleted) {
		return;
	}

	auto j = findByProxy(_settings.selected());

	Core::App().setCurrentProxy(
		item->data,
		ProxyData::Settings::Enabled);
	saveDelayed();

	if (j != end(_list)) {
		updateView(*j);
	}
	updateView(*item);
}

void ProxiesBoxController::setDeleted(int id, bool deleted) {
	auto item = findById(id);
	item->deleted = deleted;

	if (deleted) {
		const auto removed = _settings.removeFromList(item->data);
		Assert(removed);

		if (item->data == _settings.selected()) {
			_lastSelectedProxy = _settings.selected();
			_settings.setSelected(MTP::ProxyData());
			if (_settings.isEnabled()) {
				_lastSelectedProxyUsed = true;
				Core::App().setCurrentProxy(
					ProxyData(),
					ProxyData::Settings::System);
				saveDelayed();
			} else {
				_lastSelectedProxyUsed = false;
			}
		}
	} else {
		if (_settings.indexInList(item->data) < 0) {
			const auto &proxies = _settings.list();
			auto insertBefore = item + 1;
			while (insertBefore != end(_list) && insertBefore->deleted) {
				++insertBefore;
			}
			const auto foundIndex = (insertBefore == end(_list))
				? int(proxies.size())
				: _settings.indexInList(insertBefore->data);
			const auto insertIndex = (foundIndex >= 0)
				? foundIndex
				: int(proxies.size());
			_settings.insertToList(insertIndex, item->data);
		}

		if (!_settings.selected() && _lastSelectedProxy == item->data) {
			Assert(!_settings.isEnabled());

			if (base::take(_lastSelectedProxyUsed)) {
				Core::App().setCurrentProxy(
					base::take(_lastSelectedProxy),
					ProxyData::Settings::Enabled);
			} else {
				_settings.setSelected(base::take(_lastSelectedProxy));
			}
		}
	}
	saveDelayed();
	updateView(*item);
}

object_ptr<Ui::BoxContent> ProxiesBoxController::editItemBox(int id) {
	return Box<ProxyBox>(findById(id)->data, [=](const ProxyData &result) {
		auto i = findById(id);
		auto j = ranges::find(
			_list,
			result,
			[](const Item &item) { return item.data; });
		if (j != end(_list) && j != i) {
			replaceItemWith(i, j);
		} else {
			replaceItemValue(i, result);
		}
	}, [=](const ProxyData &proxy) {
		share(proxy);
	});
}

void ProxiesBoxController::replaceItemWith(
		std::vector<Item>::iterator which,
		std::vector<Item>::iterator with) {
	const auto removed = _settings.removeFromList(which->data);
	Assert(removed);

	_views.fire({ which->id });
	_list.erase(which);

	if (with->deleted) {
		restoreItem(with->id);
	}
	applyItem(with->id);
	saveDelayed();
}

void ProxiesBoxController::replaceItemValue(
		std::vector<Item>::iterator which,
		const ProxyData &proxy) {
	if (which->deleted) {
		restoreItem(which->id);
	}

	const auto replaced = _settings.replaceInList(which->data, proxy);
	Assert(replaced);
	which->data = proxy;
	refreshChecker(*which);

	applyItem(which->id);
	saveDelayed();
}

object_ptr<Ui::BoxContent> ProxiesBoxController::addNewItemBox() {
	const auto fromClipboard = ProxyDataFromClipboard();
	return Box<ProxyBox>(fromClipboard, [=](const ProxyData &result) {
		auto j = ranges::find(
			_list,
			result,
			[](const Item &item) { return item.data; });
		if (j != end(_list)) {
			if (j->deleted) {
				restoreItem(j->id);
			}
			applyItem(j->id);
		} else {
			addNewItem(result);
		}
	}, [=](const ProxyData &proxy) {
		share(proxy);
	});
}

bool ProxiesBoxController::contains(const ProxyData &proxy) const {
	const auto j = ranges::find(
		_list,
		proxy,
		[](const Item &item) { return item.data; });
	return (j != end(_list));
}

void ProxiesBoxController::addNewItem(const ProxyData &proxy) {
	_settings.addToList(proxy);

	_list.push_back({ ++_idCounter, proxy });
	refreshChecker(_list.back());
	applyItem(_list.back().id);
}

bool ProxiesBoxController::setProxySettings(ProxyData::Settings value) {
	if (_settings.settings() == value) {
		return true;
	} else if (value == ProxyData::Settings::Enabled) {
		if (_settings.list().empty()) {
			return false;
		} else if (!_settings.selected()) {
			_settings.setSelected(_settings.list().back());
			auto j = findByProxy(_settings.selected());
			if (j != end(_list)) {
				updateView(*j);
			}
		}
	}
	Core::App().setCurrentProxy(_settings.selected(), value);
	saveDelayed();
	return true;
}

void ProxiesBoxController::setProxyForCalls(bool enabled) {
	if (_settings.useProxyForCalls() == enabled) {
		return;
	}
	_settings.setUseProxyForCalls(enabled);
	if (_settings.isEnabled() && _settings.selected().supportsCalls()) {
		_settings.connectionTypeChangesNotify();
	}
	saveDelayed();
}

void ProxiesBoxController::setProxyRotationEnabled(bool enabled) {
	if (_settings.proxyRotationEnabled() == enabled) {
		return;
	}
	_settings.setProxyRotationEnabled(enabled);
	saveDelayed();
}

void ProxiesBoxController::setProxyRotationTimeout(int value) {
	if (_settings.proxyRotationTimeout() == value) {
		return;
	}
	_settings.setProxyRotationTimeout(value);
	saveDelayed();
}

void ProxiesBoxController::setTryIPv6(bool enabled) {
	if (Core::App().settings().proxy().tryIPv6() == enabled) {
		return;
	}
	Core::App().settings().proxy().setTryIPv6(enabled);
	_account->mtp().restart();
	_settings.connectionTypeChangesNotify();
	saveDelayed();
}

void ProxiesBoxController::saveDelayed() {
	Core::App().proxyRotationSettingsChanged();
	_saveTimer.callOnce(kSaveSettingsDelayedTimeout);
}

auto ProxiesBoxController::views() const -> rpl::producer<ItemView> {
	return _views.events();
}

rpl::producer<bool> ProxiesBoxController::listShareableChanges() const {
	return _views.events_starting_with(ItemView()) | rpl::map([=] {
		for (const auto &item : _list) {
			if (!item.deleted && ProxyDataIsShareable(item.data)) {
				return true;
			}
		}
		return false;
	});
}

void ProxiesBoxController::updateView(const Item &item) {
	const auto selected = (_settings.selected() == item.data);
	const auto deleted = item.deleted;
	const auto type = [&] {
		switch (item.data.type) {
		case Type::Http: return u"HTTP"_q;
		case Type::Socks5: return u"SOCKS5"_q;
		case Type::Mtproto: return u"MTPROTO"_q;
		}
		Unexpected("Proxy type in ProxiesBoxController::updateView.");
	}();
	const auto state = [&] {
		if (!selected || !_settings.isEnabled()) {
			return item.state;
		} else if (_account->mtp().dcstate() == MTP::ConnectedState) {
			return ItemState::Online;
		}
		return ItemState::Connecting;
	}();
	const auto supportsShare = ProxyDataIsShareable(item.data);
	const auto supportsCalls = item.data.supportsCalls();
	_views.fire({
		item.id,
		type,
		item.data.host,
		item.data.port,
		item.ping,
		!deleted && selected,
		deleted,
		!deleted && supportsShare,
		supportsCalls,
		state,
	});
}

void ProxiesBoxController::share(const ProxyData &proxy, bool qr) {
	ShareProxy(_show, _account, proxy, qr);
}

void ProxiesBoxController::Show(
		not_null<Window::SessionController*> controller,
		const QString &highlightId) {
	controller->show(
		CreateOwningBox(&controller->session().account(), highlightId));
}

ProxiesBoxController::~ProxiesBoxController() {
	if (_saveTimer.isActive()) {
		base::call_delayed(
			kSaveSettingsDelayedTimeout,
			QCoreApplication::instance(),
			[] { Local::writeSettings(); });
	}
}
