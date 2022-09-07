/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/settings_premium.h"

#include "core/application.h"
#include "core/click_handler_types.h"
#include "data/data_peer_values.h"
#include "info/info_wrap_widget.h" // Info::Wrap.
#include "info/settings/info_settings_widget.h" // SectionCustomTopBarData.
#include "lang/lang_keys.h"
#include "boxes/premium_preview_box.h"
#include "settings/settings_common.h"
#include "settings/settings_premium.h"
#include "ui/abstract_button.h"
#include "ui/basic_click_handlers.h"
#include "ui/effects/gradient.h"
#include "ui/effects/premium_graphics.h"
#include "ui/effects/premium_stars.h"
#include "ui/text/text_utilities.h"
#include "ui/text/format_values.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/gradient_round_button.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/fade_wrap.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_controller.h"
#include "data/data_session.h"
#include "main/main_session.h"
#include "main/main_account.h"
#include "main/main_app_config.h"
#include "window/window_session_controller.h"
#include "base/unixtime.h"
#include "apiwrap.h"
#include "api/api_premium.h"
#include "styles/style_boxes.h"
#include "styles/style_premium.h"
#include "styles/style_info.h"
#include "styles/style_intro.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"

namespace Settings {
namespace {

using SectionCustomTopBarData = Info::Settings::SectionCustomTopBarData;

constexpr auto kBodyAnimationPart = 0.90;
constexpr auto kTitleAdditionalScale = 0.15;

struct GiftRef {
	PeerId peerId;
	int months;
	bool me;
};

[[nodiscard]] QString SerializeRef(const GiftRef &gift) {
	return QString::number(gift.peerId.value)
		+ ':'
		+ QString::number(gift.months)
		+ ':'
		+ QString::number(gift.me ? 1 : 0);
}

[[nodiscard]] GiftRef ParseGiftRef(QStringView data) {
	const auto components = data.split(':');
	if (components.size() != 3) {
		return {};
	}
	return {
		.peerId = PeerId(components[0].toULongLong()),
		.months = components[1].toInt(),
		.me = (components[2].toInt() == 1),
	};
}

struct Entry {
	const style::icon *icon;
	rpl::producer<QString> title;
	rpl::producer<QString> description;
	std::optional<PremiumPreview> section;
};

using Order = std::vector<QString>;

[[nodiscard]] Order FallbackOrder() {
	return Order{
		u"double_limits"_q,
		u"more_upload"_q,
		u"faster_download"_q,
		u"voice_to_text"_q,
		u"no_ads"_q,
		u"unique_reactions"_q,
		u"premium_stickers"_q,
		u"animated_emoji"_q,
		u"advanced_chat_management"_q,
		u"profile_badge"_q,
		u"animated_userpics"_q,
	};
}

[[nodiscard]] base::flat_map<QString, Entry> EntryMap() {
	return base::flat_map<QString, Entry>{
		{
			u"double_limits"_q,
			Entry{
				&st::settingsPremiumIconDouble,
				tr::lng_premium_summary_subtitle_double_limits(),
				tr::lng_premium_summary_about_double_limits(),
			},
		},
		{
			u"more_upload"_q,
			Entry{
				&st::settingsPremiumIconFiles,
				tr::lng_premium_summary_subtitle_more_upload(),
				tr::lng_premium_summary_about_more_upload(),
				PremiumPreview::MoreUpload,
			},
		},
		{
			u"faster_download"_q,
			Entry{
				&st::settingsPremiumIconSpeed,
				tr::lng_premium_summary_subtitle_faster_download(),
				tr::lng_premium_summary_about_faster_download(),
				PremiumPreview::FasterDownload,
			},
		},
		{
			u"voice_to_text"_q,
			Entry{
				&st::settingsPremiumIconVoice,
				tr::lng_premium_summary_subtitle_voice_to_text(),
				tr::lng_premium_summary_about_voice_to_text(),
				PremiumPreview::VoiceToText,
			},
		},
		{
			u"no_ads"_q,
			Entry{
				&st::settingsPremiumIconChannelsOff,
				tr::lng_premium_summary_subtitle_no_ads(),
				tr::lng_premium_summary_about_no_ads(),
				PremiumPreview::NoAds,
			},
		},
		{
			u"unique_reactions"_q,
			Entry{
				&st::settingsPremiumIconLike,
				tr::lng_premium_summary_subtitle_unique_reactions(),
				tr::lng_premium_summary_about_unique_reactions(),
				PremiumPreview::Reactions,
			},
		},
		{
			u"premium_stickers"_q,
			Entry{
				&st::settingsIconStickers,
				tr::lng_premium_summary_subtitle_premium_stickers(),
				tr::lng_premium_summary_about_premium_stickers(),
				PremiumPreview::Stickers,
			},
		},
		{
			u"animated_emoji"_q,
			Entry{
				&st::settingsIconEmoji,
				tr::lng_premium_summary_subtitle_animated_emoji(),
				tr::lng_premium_summary_about_animated_emoji(),
				PremiumPreview::AnimatedEmoji,
			},
		},
		{
			u"advanced_chat_management"_q,
			Entry{
				&st::settingsIconChat,
				tr::lng_premium_summary_subtitle_advanced_chat_management(),
				tr::lng_premium_summary_about_advanced_chat_management(),
				PremiumPreview::AdvancedChatManagement,
			},
		},
		{
			u"profile_badge"_q,
			Entry{
				&st::settingsPremiumIconStar,
				tr::lng_premium_summary_subtitle_profile_badge(),
				tr::lng_premium_summary_about_profile_badge(),
				PremiumPreview::ProfileBadge,
			},
		},
		{
			u"animated_userpics"_q,
			Entry{
				&st::settingsPremiumIconPlay,
				tr::lng_premium_summary_subtitle_animated_userpics(),
				tr::lng_premium_summary_about_animated_userpics(),
				PremiumPreview::AnimatedUserpics,
			},
		},
	};
}

void SendAppLog(
		not_null<Main::Session*> session,
		const QString &type,
		const MTPJSONValue &data) {
	const auto now = double(base::unixtime::now())
		+ (QTime::currentTime().msec() / 1000.);
	session->api().request(MTPhelp_SaveAppLog(
		MTP_vector<MTPInputAppEvent>(1, MTP_inputAppEvent(
			MTP_double(now),
			MTP_string(type),
			MTP_long(0),
			data
		))
	)).send();
}

[[nodiscard]] QString ResolveRef(const QString &ref) {
	return ref.isEmpty() ? "settings" : ref;
}

void SendScreenShow(
		not_null<Window::SessionController*> controller,
		const std::vector<QString> &order,
		const QString &ref) {
	auto list = QVector<MTPJSONValue>();
	list.reserve(order.size());
	for (const auto &element : order) {
		list.push_back(MTP_jsonString(MTP_string(element)));
	}
	auto values = QVector<MTPJSONObjectValue>{
		MTP_jsonObjectValue(
			MTP_string("premium_promo_order"),
			MTP_jsonArray(MTP_vector<MTPJSONValue>(std::move(list)))),
		MTP_jsonObjectValue(
			MTP_string("source"),
			MTP_jsonString(MTP_string(ResolveRef(ref)))),
	};
	const auto data = MTP_jsonObject(
		MTP_vector<MTPJSONObjectValue>(std::move(values)));
	SendAppLog(
		&controller->session(),
		"premium.promo_screen_show",
		data);
}

void SendScreenAccept(not_null<Window::SessionController*> controller) {
	SendAppLog(
		&controller->session(),
		"premium.promo_screen_accept",
		MTP_jsonNull());
}

class TopBar final : public Ui::RpWidget {
public:
	TopBar(
		not_null<QWidget*> parent,
		not_null<Window::SessionController*> controller,
		rpl::producer<QString> title,
		rpl::producer<TextWithEntities> about);

	void setPaused(bool paused);
	void setRoundEdges(bool value);
	void setTextPosition(int x, int y);

protected:
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

private:
	[[nodiscard]] QRectF starRect(
		float64 topProgress,
		float64 sizeProgress) const;

	const style::font &_titleFont;
	const style::margins &_titlePadding;
	object_ptr<Ui::FlatLabel> _about;
	Ui::Premium::MiniStars _ministars;
	QSvgRenderer _star;

	struct {
		float64 top = 0.;
		float64 body = 0.;
		float64 title = 0.;
		float64 scaleTitle = 0.;
	} _progress;

	QRectF _ministarsRect;
	QRectF _starRect;

	QPoint _titlePosition;
	QPainterPath _titlePath;
	bool _roundEdges = true;

};

TopBar::TopBar(
	not_null<QWidget*> parent,
	not_null<Window::SessionController*> controller,
	rpl::producer<QString> title,
	rpl::producer<TextWithEntities> about)
: Ui::RpWidget(parent)
, _titleFont(st::boxTitle.style.font)
, _titlePadding(st::settingsPremiumTitlePadding)
, _about(this, std::move(about), st::settingsPremiumAbout)
, _ministars([=](const QRect &r) { update(r); })
, _star(u":/gui/icons/settings/star.svg"_q) {
	std::move(
		title
	) | rpl::start_with_next([=](QString text) {
		_titlePath = QPainterPath();
		_titlePath.addText(0, _titleFont->ascent, _titleFont, text);
		update();
	}, lifetime());

	_about->setClickHandlerFilter([=](
			const ClickHandlerPtr &handler,
			Qt::MouseButton button) {
		ActivateClickHandler(_about, handler, {
			button,
			QVariant::fromValue(ClickHandlerContext{
				.sessionWindow = base::make_weak(controller.get()),
				.botStartAutoSubmit = true,
			})
		});
		return false;
	});
}

void TopBar::setPaused(bool paused) {
	_ministars.setPaused(paused);
}

void TopBar::setRoundEdges(bool value) {
	_roundEdges = value;
	update();
}

void TopBar::setTextPosition(int x, int y) {
	_titlePosition = { x, y };
}

QRectF TopBar::starRect(float64 topProgress, float64 sizeProgress) const {
	const auto starSize = st::settingsPremiumStarSize * sizeProgress;
	return QRectF(
		QPointF(
			(width() - starSize.width()) / 2,
			st::settingsPremiumStarTopSkip * topProgress),
		starSize);
};

void TopBar::resizeEvent(QResizeEvent *e) {
	const auto progress = (e->size().height() - minimumHeight())
		/ float64(maximumHeight() - minimumHeight());
	_progress.top = 1. -
		std::clamp(
			(1. - progress) / kBodyAnimationPart,
			0.,
			1.);
	_progress.body = _progress.top;
	_progress.title = 1. - progress;
	_progress.scaleTitle = 1. + kTitleAdditionalScale * progress;

	_ministarsRect = starRect(_progress.top, 1.);
	_starRect = starRect(_progress.top, _progress.body);

	const auto &padding = st::boxRowPadding;
	const auto availableWidth = width() - padding.left() - padding.right();
	const auto titleTop = _starRect.top()
		+ _starRect.height()
		+ _titlePadding.top();
	const auto titlePathRect = _titlePath.boundingRect();
	const auto aboutTop = titleTop
		+ titlePathRect.height()
		+ _titlePadding.bottom();
	_about->resizeToWidth(availableWidth);
	_about->moveToLeft(padding.left(), aboutTop);
	_about->setOpacity(_progress.body);

	Ui::RpWidget::resizeEvent(e);
}

void TopBar::paintEvent(QPaintEvent *e) {
	Painter p(this);

	p.fillRect(e->rect(), Qt::transparent);

	const auto r = rect();

	const auto gradientPointTop = r.height() / 3. * 2.;
	auto gradient = QLinearGradient(
		QPointF(0, gradientPointTop),
		QPointF(r.width(), r.height() - gradientPointTop));
	gradient.setColorAt(0., st::premiumButtonBg1->c);
	gradient.setColorAt(.6, st::premiumButtonBg2->c);
	gradient.setColorAt(1., st::premiumButtonBg3->c);

	PainterHighQualityEnabler hq(p);
	if (_roundEdges) {
		const auto radius = st::boxRadius;
		p.setPen(Qt::NoPen);
		p.setBrush(gradient);
		p.drawRoundedRect(
			r + QMargins{ 0, 0, 0, radius + 1 },
			radius,
			radius);
	} else {
		p.fillRect(r, gradient);
	}

	p.setOpacity(_progress.body);
	p.translate(_starRect.center());
	p.scale(_progress.body, _progress.body);
	p.translate(-_starRect.center());
	if (_progress.top) {
		_ministars.paint(p, _ministarsRect);
	}
	p.resetTransform();

	_star.render(&p, _starRect);

	p.setPen(st::premiumButtonFg);

	const auto titlePathRect = _titlePath.boundingRect();

	// Title.
	p.setOpacity(1.);
	p.setFont(_titleFont);
	const auto fullStarRect = starRect(1., 1.);
	const auto fullTitleTop = fullStarRect.top()
		+ fullStarRect.height()
		+ _titlePadding.top();
	p.translate(
		anim::interpolate(
			(width() - titlePathRect.width()) / 2,
			_titlePosition.x(),
			_progress.title),
		anim::interpolate(fullTitleTop, _titlePosition.y(), _progress.title));

	p.translate(titlePathRect.center());
	p.scale(_progress.scaleTitle, _progress.scaleTitle);
	p.translate(-titlePathRect.center());
	p.fillPath(_titlePath, st::premiumButtonFg);
}

class Premium : public Section<Premium> {
public:
	Premium(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	[[nodiscard]] rpl::producer<QString> title() override;

	[[nodiscard]] QPointer<Ui::RpWidget> createPinnedToTop(
		not_null<QWidget*> parent) override;
	[[nodiscard]] QPointer<Ui::RpWidget> createPinnedToBottom(
		not_null<Ui::RpWidget*> parent) override;

	void showFinished() override;

	[[nodiscard]] bool hasFlexibleTopBar() const override;

	void setStepDataReference(std::any &data) override;

	[[nodiscard]] rpl::producer<> sectionShowBack() override final;

private:
	void setupContent();

	const not_null<Window::SessionController*> _controller;
	const QString _ref;

	QPointer<Ui::GradientButton> _subscribe;
	base::unique_qptr<Ui::FadeWrap<Ui::IconButton>> _back;
	base::unique_qptr<Ui::IconButton> _close;
	rpl::variable<bool> _backToggles;
	rpl::variable<Info::Wrap> _wrap;
	Fn<void(bool)> _setPaused;

	rpl::event_stream<> _showBack;
	rpl::event_stream<> _showFinished;

};

Premium::Premium(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent)
, _controller(controller)
, _ref(ResolveRef(controller->premiumRef())) {
	setupContent();
	_controller->session().api().premium().reload();
}

rpl::producer<QString> Premium::title() {
	return tr::lng_premium_summary_title();
}

bool Premium::hasFlexibleTopBar() const {
	return true;
}

rpl::producer<> Premium::sectionShowBack() {
	return _showBack.events();
}

void Premium::setStepDataReference(std::any &data) {
	const auto my = std::any_cast<SectionCustomTopBarData>(&data);
	if (my) {
		_backToggles = std::move(
			my->backButtonEnables
		) | rpl::map_to(true);
		_wrap = std::move(my->wrapValue);
	}
}

void Premium::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

	const auto &stDefault = st::settingsButton;
	const auto &stLabel = st::defaultFlatLabel;
	const auto iconSize = st::settingsPremiumIconDouble.size();
	const auto &titlePadding = st::settingsPremiumRowTitlePadding;
	const auto &descriptionPadding = st::settingsPremiumRowAboutPadding;

	AddSkip(content, stDefault.padding.top() + titlePadding.top());

	auto entryMap = EntryMap();
	auto iconContainers = std::vector<Ui::AbstractButton*>();
	iconContainers.reserve(int(entryMap.size()));

	const auto addRow = [&](Entry &entry) {
		const auto labelAscent = stLabel.style.font->ascent;
		const auto button = Ui::CreateChild<Ui::SettingsButton>(
			content,
			rpl::single(QString()));

		const auto label = content->add(
			object_ptr<Ui::FlatLabel>(
				content,
				std::move(entry.title) | rpl::map(Ui::Text::Bold),
				stLabel),
			titlePadding);
		label->setAttribute(Qt::WA_TransparentForMouseEvents);
		const auto description = content->add(
			object_ptr<Ui::FlatLabel>(
				content,
				std::move(entry.description),
				st::boxDividerLabel),
			descriptionPadding);
		description->setAttribute(Qt::WA_TransparentForMouseEvents);

		const auto dummy = Ui::CreateChild<Ui::AbstractButton>(content);
		dummy->setAttribute(Qt::WA_TransparentForMouseEvents);

		content->sizeValue(
		) | rpl::start_with_next([=](const QSize &s) {
			dummy->resize(s.width(), iconSize.height());
		}, dummy->lifetime());

		label->geometryValue(
		) | rpl::start_with_next([=](const QRect &r) {
			dummy->moveToLeft(0, r.y() + (r.height() - labelAscent));
		}, dummy->lifetime());

		rpl::combine(
			content->widthValue(),
			label->heightValue(),
			description->heightValue()
		) | rpl::start_with_next([=,
			topPadding = titlePadding,
			bottomPadding = descriptionPadding](
				int width,
				int topHeight,
				int bottomHeight) {
			button->resize(
				width,
				topPadding.top()
					+ topHeight
					+ topPadding.bottom()
					+ bottomPadding.top()
					+ bottomHeight
					+ bottomPadding.bottom());
		}, button->lifetime());
		label->topValue(
		) | rpl::start_with_next([=, padding = titlePadding.top()](int top) {
			button->moveToLeft(0, top - padding);
		}, button->lifetime());
		const auto arrow = Ui::CreateChild<Ui::IconButton>(
			button,
			st::backButton);
		arrow->setIconOverride(
			&st::menuIconSubmenuArrow,
			&st::menuIconSubmenuArrow);
		arrow->setAttribute(Qt::WA_TransparentForMouseEvents);
		button->sizeValue(
		) | rpl::start_with_next([=](const QSize &s) {
			arrow->moveToRight(0, (s.height() - arrow->height()) / 2);
		}, arrow->lifetime());

		const auto section = entry.section;
		button->setClickedCallback([=, controller = _controller] {
			_setPaused(true);
			const auto hidden = crl::guard(this, [=] {
				_setPaused(false);
			});

			if (section) {
				ShowPremiumPreviewToBuy(controller, *section, hidden);
				return;
			}
			controller->show(Box([=](not_null<Ui::GenericBox*> box) {
				DoubledLimitsPreviewBox(box, &controller->session());

				box->addTopButton(st::boxTitleClose, [=] {
					box->closeBox();
				});

				Data::AmPremiumValue(
					&controller->session()
				) | rpl::skip(1) | rpl::start_with_next([=] {
					box->closeBox();
				}, box->lifetime());

				if (controller->session().premium()) {
					box->addButton(tr::lng_close(), [=] {
						box->closeBox();
					});
				} else {
					const auto button = CreateSubscribeButton({
						controller,
						box,
						[] { return u"double_limits"_q; }
					});

					box->boxClosing(
					) | rpl::start_with_next(hidden, box->lifetime());

					box->setShowFinishedCallback([=] {
						button->startGlareAnimation();
					});

					box->setStyle(st::premiumPreviewDoubledLimitsBox);
					box->widthValue(
					) | rpl::start_with_next([=](int width) {
						const auto &padding =
							st::premiumPreviewDoubledLimitsBox.buttonPadding;
						button->resizeToWidth(width
							- padding.left()
							- padding.right());
						button->moveToLeft(padding.left(), padding.top());
					}, button->lifetime());
					box->addButton(
						object_ptr<Ui::AbstractButton>::fromRaw(button));
				}
			}));
		});

		iconContainers.push_back(dummy);
	};

	auto icons = std::vector<const style::icon *>();
	icons.reserve(int(entryMap.size()));
	{
		const auto &account = _controller->session().account();
		const auto mtpOrder = account.appConfig().get<Order>(
			"premium_promo_order",
			FallbackOrder());
		const auto processEntry = [&](Entry &entry) {
			icons.push_back(entry.icon);
			addRow(entry);
		};

		for (const auto &key : mtpOrder) {
			auto it = entryMap.find(key);
			if (it == end(entryMap)) {
				continue;
			}
			processEntry(it->second);
		}

		SendScreenShow(_controller, mtpOrder, _ref);
	}

	content->resizeToWidth(content->height());

	// Icons.
	Assert(iconContainers.size() > 2);
	const auto from = iconContainers.front()->y();
	const auto to = iconContainers.back()->y() + iconSize.height();
	auto gradient = QLinearGradient(0, 0, 0, to - from);
	gradient.setStops(Ui::Premium::FullHeightGradientStops());
	for (auto i = 0; i < int(icons.size()); i++) {
		const auto &iconContainer = iconContainers[i];

		const auto pointTop = iconContainer->y() - from;
		const auto pointBottom = pointTop + iconContainer->height();
		const auto ratioTop = pointTop / float64(to - from);
		const auto ratioBottom = pointBottom / float64(to - from);

		auto resultGradient = QLinearGradient(
			QPointF(),
			QPointF(0, pointBottom - pointTop));

		resultGradient.setColorAt(
			.0,
			anim::gradient_color_at(gradient, ratioTop));
		resultGradient.setColorAt(
			.1,
			anim::gradient_color_at(gradient, ratioBottom));

		const auto brush = QBrush(resultGradient);
		AddButtonIcon(
			iconContainer,
			stDefault,
			{ .icon = icons[i], .backgroundBrush = brush });
	}

	AddSkip(content, descriptionPadding.bottom());
#if 0
	AddSkip(content);
	AddDivider(content);
	AddSkip(content);

	content->add(
		object_ptr<Ui::FlatLabel>(
			content,
			tr::lng_premium_summary_bottom_subtitle(
			) | rpl::map(Ui::Text::Bold),
			stLabel),
		st::settingsSubsectionTitlePadding);
	content->add(
		object_ptr<Ui::FlatLabel>(
			content,
			tr::lng_premium_summary_bottom_about(Ui::Text::RichLangValue),
			st::aboutLabel),
		st::boxRowPadding);
	AddSkip(content, stDefault.padding.top() + stDefault.padding.bottom());
#endif

	Ui::ResizeFitChild(this, content);

}

QPointer<Ui::RpWidget> Premium::createPinnedToTop(
		not_null<QWidget*> parent) {
	auto title = _controller->session().premium()
		? tr::lng_premium_summary_title()
		: rpl::conditional(
			Data::AmPremiumValue(&_controller->session()),
			tr::lng_premium_summary_title_subscribed(),
			tr::lng_premium_summary_title());
	auto about = [&]() -> rpl::producer<TextWithEntities> {
		const auto gift = ParseGiftRef(_ref);
		if (gift.peerId) {
			auto &data = _controller->session().data();
			if (const auto peer = data.peer(gift.peerId)) {
				return (gift.me
					? tr::lng_premium_summary_subtitle_gift_me
					: tr::lng_premium_summary_subtitle_gift)(
						lt_count,
						rpl::single(float64(gift.months)),
						lt_user,
						rpl::single(Ui::Text::Bold(peer->name())),
						Ui::Text::RichLangValue);
			}
		}
		return rpl::conditional(
			Data::AmPremiumValue(&_controller->session()),
			_controller->session().api().premium().statusTextValue(),
			tr::lng_premium_summary_top_about(Ui::Text::RichLangValue));
	}();

	const auto content = Ui::CreateChild<TopBar>(
		parent.get(),
		_controller,
		std::move(title),
		std::move(about));
	_setPaused = [=](bool paused) {
		content->setPaused(paused);
		if (_subscribe) {
			_subscribe->setGlarePaused(paused);
		}
	};

	_wrap.value(
	) | rpl::start_with_next([=](Info::Wrap wrap) {
		content->setRoundEdges(wrap == Info::Wrap::Layer);
	}, content->lifetime());

	content->setMaximumHeight(st::introQrStepsTop);
	content->setMinimumHeight(st::infoLayerTopBarHeight);

	content->resize(content->width(), content->maximumHeight());

	_wrap.value(
	) | rpl::start_with_next([=](Info::Wrap wrap) {
		const auto isLayer = (wrap == Info::Wrap::Layer);
		_back = base::make_unique_q<Ui::FadeWrap<Ui::IconButton>>(
			content,
			object_ptr<Ui::IconButton>(
				content,
				isLayer
					? st::settingsPremiumLayerTopBarBack
					: st::settingsPremiumTopBarBack),
			st::infoTopBarScale);
		_back->setDuration(0);
		_back->toggleOn(isLayer
			? _backToggles.value() | rpl::type_erased()
			: rpl::single(true));
		_back->entity()->addClickHandler([=] {
			_showBack.fire({});
		});
		_back->toggledValue(
		) | rpl::start_with_next([=](bool toggled) {
			const auto &st = isLayer ? st::infoLayerTopBar : st::infoTopBar;
			content->setTextPosition(
				toggled ? st.back.width : st.titlePosition.x(),
				st.titlePosition.y());
		}, _back->lifetime());

		if (!isLayer) {
			_close = nullptr;
		} else {
			_close = base::make_unique_q<Ui::IconButton>(
				content,
				st::settingsPremiumTopBarClose);
			_close->addClickHandler([=] {
				_controller->parentController()->hideLayer();
				_controller->parentController()->hideSpecialLayer();
			});
			content->widthValue(
			) | rpl::start_with_next([=] {
				_close->moveToRight(0, 0);
			}, _close->lifetime());
		}
	}, content->lifetime());

	return Ui::MakeWeak(not_null<Ui::RpWidget*>{ content });
}

void Premium::showFinished() {
	_showFinished.fire({});
}

QPointer<Ui::RpWidget> Premium::createPinnedToBottom(
		not_null<Ui::RpWidget*> parent) {
	const auto content = Ui::CreateChild<Ui::RpWidget>(parent.get());

	if (ParseGiftRef(_ref).peerId) {
		return nullptr;
	}

	_subscribe = CreateSubscribeButton({
		_controller,
		content,
		[=] { return _ref; }
	});

	_showFinished.events(
	) | rpl::take(1) | rpl::start_with_next([=] {
		_subscribe->startGlareAnimation();
	}, _subscribe->lifetime());

	content->widthValue(
	) | rpl::start_with_next([=](int width) {
		const auto padding = st::settingsPremiumButtonPadding;
		_subscribe->resizeToWidth(width - padding.left() - padding.right());
	}, _subscribe->lifetime());

	const auto session = &_controller->session();
	rpl::combine(
		_subscribe->heightValue(),
		Data::AmPremiumValue(session),
		session->premiumPossibleValue()
	) | rpl::start_with_next([=](
			int buttonHeight,
			bool premium,
			bool premiumPossible) {
		const auto padding = st::settingsPremiumButtonPadding;
		const auto finalHeight = !premiumPossible
			? 0
			: !premium
			? (padding.top() + buttonHeight + padding.bottom())
			: 0;
		content->resize(content->width(), finalHeight);
		_subscribe->moveToLeft(padding.left(), padding.top());
		_subscribe->setVisible(!premium && premiumPossible);
	}, _subscribe->lifetime());

	return Ui::MakeWeak(not_null<Ui::RpWidget*>{ content });
}

} // namespace

Type PremiumId() {
	return Premium::Id();
}

void ShowPremium(not_null<Main::Session*> session, const QString &ref) {
	const auto active = Core::App().activeWindow();
	const auto controller = (active && active->isPrimary())
		? active->sessionController()
		: nullptr;
	if (controller && session == &controller->session()) {
		ShowPremium(controller, ref);
	} else {
		for (const auto &controller : session->windows()) {
			if (controller->window().isPrimary()) {
				ShowPremium(controller, ref);
			}
		}
	}
}

void ShowPremium(
		not_null<Window::SessionController*> controller,
		const QString &ref) {
	if (!controller->session().premiumPossible()) {
		controller->show(Box(PremiumUnavailableBox));
		return;
	}
	controller->setPremiumRef(ref);
	controller->showSettings(Settings::PremiumId());
}

void ShowGiftPremium(
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> peer,
		int months,
		bool me) {
	ShowPremium(controller, SerializeRef({ peer->id, months, me }));
}

void StartPremiumPayment(
		not_null<Window::SessionController*> controller,
		const QString &ref) {
	const auto account = &controller->session().account();
	const auto username = account->appConfig().get<QString>(
		"premium_bot_username",
		QString());
	const auto slug = account->appConfig().get<QString>(
		"premium_invoice_slug",
		QString());
	if (!username.isEmpty()) {
		controller->showPeerByLink(Window::SessionNavigation::PeerByLinkInfo{
			.usernameOrId = username,
			.resolveType = Window::ResolveType::BotStart,
			.startToken = ref,
			.startAutoSubmit = true,
		});
	} else if (!slug.isEmpty()) {
		UrlClickHandler::Open("https://t.me/$" + slug);
	}
}

QString LookupPremiumRef(PremiumPreview section) {
	for (const auto &[ref, entry] : EntryMap()) {
		if (entry.section == section) {
			return ref;
		}
	}
	return QString();
}

not_null<Ui::GradientButton*> CreateSubscribeButton(
		SubscribeButtonArgs &&args) {
	const auto result = Ui::CreateChild<Ui::GradientButton>(
		args.parent.get(),
		args.gradientStops
			? base::take(*args.gradientStops)
			: Ui::Premium::ButtonGradientStops());

	result->setClickedCallback([
			controller = args.controller,
			computeRef = args.computeRef] {
		SendScreenAccept(controller);
		StartPremiumPayment(controller, computeRef());
	});

	const auto &st = st::premiumPreviewBox.button;
	result->resize(args.parent->width(), st.height);

	const auto premium = &args.controller->session().api().premium();
	premium->reload();
	const auto computeCost = [=] {
		const auto amount = premium->monthlyAmount();
		const auto currency = premium->monthlyCurrency();
		const auto valid = (amount > 0) && !currency.isEmpty();
		return Ui::FillAmountAndCurrency(
			valid ? amount : 500,
			valid ? currency : "USD");
	};

	const auto label = Ui::CreateChild<Ui::FlatLabel>(
		result,
		args.text
			? base::take(*args.text)
			: tr::lng_premium_summary_button(
				lt_cost,
				premium->statusTextValue() | rpl::map(computeCost)),
		st::premiumPreviewButtonLabel);
	label->setAttribute(Qt::WA_TransparentForMouseEvents);
	rpl::combine(
		result->widthValue(),
		label->widthValue()
	) | rpl::start_with_next([=](int outer, int width) {
		label->moveToLeft(
			(outer - width) / 2,
			st::premiumPreviewBox.button.textTop,
			outer);
	}, label->lifetime());

	return result;
}

} // namespace Settings
