/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/settings_premium.h"

#include "base/random.h"
#include "core/application.h"
#include "data/data_peer_values.h"
#include "info/info_wrap_widget.h" // Info::Wrap.
#include "info/settings/info_settings_widget.h" // SectionCustomTopBarData.
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "settings/settings_common.h"
#include "settings/settings_premium.h"
#include "ui/abstract_button.h"
#include "ui/basic_click_handlers.h"
#include "ui/effects/animation_value_f.h"
#include "ui/effects/gradient.h"
#include "ui/effects/premium_graphics.h"
#include "ui/text/text_utilities.h"
#include "ui/text/format_values.h"
#include "ui/widgets/gradient_round_button.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/fade_wrap.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_controller.h"
#include "main/main_session.h"
#include "main/main_account.h"
#include "main/main_app_config.h"
#include "window/window_session_controller.h"
#include "base/unixtime.h"
#include "apiwrap.h"
#include "api/api_premium.h"
#include "styles/style_boxes.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_info.h"
#include "styles/style_intro.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"

#include <QSvgRenderer>

namespace Settings {
namespace {

using SectionCustomTopBarData = Info::Settings::SectionCustomTopBarData;

constexpr auto kBodyAnimationPart = 0.90;
constexpr auto kTitleAnimationPart = 0.15;

constexpr auto kTitleAdditionalScale = 0.15;

constexpr auto kDeformationMax = 0.1;

struct Entry {
	const style::icon *icon;
	rpl::producer<QString> title;
	rpl::producer<QString> description;
};

using Order = std::vector<QString>;

[[nodiscard]] Order FallbackOrder() {
	return Order{
		QString("double_limits"),
		QString("more_upload"),
		QString("faster_download"),
		QString("voice_to_text"),
		QString("no_ads"),
		QString("unique_reactions"),
		QString("premium_stickers"),
		QString("advanced_chat_management"),
		QString("profile_badge"),
		QString("animated_userpics"),
	};
}

[[nodiscard]] base::flat_map<QString, Entry> EntryMap() {
	return base::flat_map<QString, Entry>{
		{
			QString("double_limits"),
			Entry{
				&st::settingsPremiumIconDouble,
				tr::lng_premium_summary_subtitle_double_limits(),
				tr::lng_premium_summary_about_double_limits(),
			},
		},
		{
			QString("more_upload"),
			Entry{
				&st::settingsPremiumIconFiles,
				tr::lng_premium_summary_subtitle_more_upload(),
				tr::lng_premium_summary_about_more_upload(),
			},
		},
		{
			QString("faster_download"),
			Entry{
				&st::settingsPremiumIconSpeed,
				tr::lng_premium_summary_subtitle_faster_download(),
				tr::lng_premium_summary_about_faster_download(),
			},
		},
		{
			QString("voice_to_text"),
			Entry{
				&st::settingsPremiumIconVoice,
				tr::lng_premium_summary_subtitle_voice_to_text(),
				tr::lng_premium_summary_about_voice_to_text(),
			},
		},
		{
			QString("no_ads"),
			Entry{
				&st::settingsPremiumIconChannelsOff,
				tr::lng_premium_summary_subtitle_no_ads(),
				tr::lng_premium_summary_about_no_ads(),
			},
		},
		{
			QString("unique_reactions"),
			Entry{
				&st::settingsPremiumIconLike,
				tr::lng_premium_summary_subtitle_unique_reactions(),
				tr::lng_premium_summary_about_unique_reactions(),
			},
		},
		{
			QString("premium_stickers"),
			Entry{
				&st::settingsIconStickers,
				tr::lng_premium_summary_subtitle_premium_stickers(),
				tr::lng_premium_summary_about_premium_stickers(),
			},
		},
		{
			QString("advanced_chat_management"),
			Entry{
				&st::settingsIconChat,
				tr::lng_premium_summary_subtitle_advanced_chat_management(),
				tr::lng_premium_summary_about_advanced_chat_management(),
			},
		},
		{
			QString("profile_badge"),
			Entry{
				&st::settingsPremiumIconStar,
				tr::lng_premium_summary_subtitle_profile_badge(),
				tr::lng_premium_summary_about_profile_badge(),
			},
		},
		{
			QString("animated_userpics"),
			Entry{
				&st::settingsPremiumIconPlay,
				tr::lng_premium_summary_subtitle_animated_userpics(),
				tr::lng_premium_summary_about_animated_userpics(),
			},
		},
	};
}

[[nodiscard]] not_null<Ui::RpWidget*> CreateSubscribeButton(
		not_null<Window::SessionController*> controller,
		not_null<Ui::RpWidget*> parent,
		Fn<void()> callback) {
	const auto result = Ui::CreateChild<Ui::GradientButton>(
		parent.get(),
		Ui::Premium::ButtonGradientStops());

	result->setClickedCallback(std::move(callback));

	const auto &st = st::premiumPreviewBox.button;
	result->resize(parent->width(), st.height);

	const auto premium = &controller->session().api().premium();
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
		tr::lng_premium_summary_button(
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

	parent->widthValue(
	) | rpl::start_with_next([=](int width) {
		const auto padding = st::settingsPremiumButtonPadding;
		result->resizeToWidth(width - padding.left() - padding.right());
		result->moveToLeft(padding.left(), padding.top(), width);
	}, result->lifetime());

	return result;
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

class MiniStars final {
public:
	MiniStars(Fn<void(const QRect &r)> updateCallback);

	void paint(Painter &p, const QRectF &rect);

private:
	struct MiniStar {
		crl::time birthTime = 0;
		crl::time deathTime = 0;
		int angle = 0;
		float64 size = 0.;
		float64 alpha = 0.;
		float64 sinFactor = 0.;
	};

	struct Interval {
		int from = 0;
		int length = 0;
	};

	void createStar(crl::time now);
	[[nodiscard]] int angle() const;
	[[nodiscard]] crl::time timeNow() const;
	[[nodiscard]] int randomInterval(const Interval &interval) const;

	const std::vector<Interval> _availableAngles;
	const Interval _lifeLength;
	const Interval _deathTime;
	const Interval _size;
	const Interval _alpha;
	const Interval _sinFactor;

	const float64 _appearProgressTill;
	const float64 _disappearProgressAfter;
	const float64 _distanceProgressStart;

	QSvgRenderer _sprite;

	Ui::Animations::Basic _animation;

	std::vector<MiniStar> _ministars;

	crl::time _nextBirthTime = 0;

	QRect _rectToUpdate;

};

MiniStars::MiniStars(Fn<void(const QRect &r)> updateCallback)
: _availableAngles({
	Interval{ -10, 40 },
	Interval{ 180 + 10 - 40, 40 },
	Interval{ 180 + 15, 50 },
	Interval{ -15 - 50, 50 },
})
, _lifeLength({ 150, 200 })
, _deathTime({ 1500, 2000 })
, _size({ 10, 20 })
, _alpha({ 40, 60 })
, _sinFactor({ 10, 190 })
, _appearProgressTill(0.2)
, _disappearProgressAfter(0.8)
, _distanceProgressStart(0.5)
, _sprite(u":/gui/icons/settings/starmini.svg"_q)
, _animation([=](crl::time now) {
	if (now > _nextBirthTime) {
		createStar(now);
		_nextBirthTime = now + randomInterval(_lifeLength);
	}
	if (_rectToUpdate.isValid()) {
		updateCallback(base::take(_rectToUpdate));
	}
}) {
	if (anim::Disabled()) {
		const auto from = _deathTime.from + _deathTime.length;
		for (auto i = -from; i < 0; i += randomInterval(_lifeLength)) {
			createStar(i);
		}
		updateCallback(_rectToUpdate);
	} else {
		_animation.start();
	}
}

int MiniStars::randomInterval(const Interval &interval) const {
	return interval.from + base::RandomIndex(interval.length);
}

crl::time MiniStars::timeNow() const {
	return anim::Disabled() ? 0 : crl::now();
}

void MiniStars::paint(Painter &p, const QRectF &rect) {
	const auto center = rect.center();
	const auto opacity = p.opacity();
	for (const auto &ministar : _ministars) {
		const auto progress = (timeNow() - ministar.birthTime)
			/ float64(ministar.deathTime - ministar.birthTime);
		if (progress > 1.) {
			continue;
		}
		const auto appearProgress = std::clamp(
			progress / _appearProgressTill,
			0.,
			1.);
		const auto rsin = float(std::sin(ministar.angle * M_PI / 180.));
		const auto rcos = float(std::cos(ministar.angle * M_PI / 180.));
		const auto end = QPointF(
			rect.width() / 1.5 * rcos,
			rect.height() / 1.5 * rsin);

		const auto alphaProgress = 1.
			- (std::clamp(progress - _disappearProgressAfter, 0., 1.)
				/ (1. - _disappearProgressAfter));
		p.setOpacity(ministar.alpha
			* alphaProgress
			* appearProgress
			* opacity);

		const auto deformResult = progress * 360;
		const auto rsinDeform = float(
			std::sin(ministar.sinFactor * deformResult * M_PI / 180.));
		const auto deformH = 1. + kDeformationMax * rsinDeform;
		const auto deformW = 1. / deformH;

		const auto distanceProgress = _distanceProgressStart + progress;
		const auto starSide = ministar.size * appearProgress;
		const auto widthFade = (std::abs(rcos) >= std::abs(rsin));
		const auto starWidth = starSide
			* (widthFade ? alphaProgress : 1.)
			* deformW;
		const auto starHeight = starSide
			* (!widthFade ? alphaProgress : 1.)
			* deformH;
		const auto renderRect = QRectF(
			center.x()
				+ anim::interpolateF(0, end.x(), distanceProgress)
				- starWidth / 2.,
			center.y()
				+ anim::interpolateF(0, end.y(), distanceProgress)
				- starHeight / 2.,
			starWidth,
			starHeight);
		_sprite.render(&p, renderRect);
		_rectToUpdate |= renderRect.toRect();
	}
	p.setOpacity(opacity);
}

int MiniStars::angle() const {
	const auto &interval = _availableAngles[
		base::RandomIndex(_availableAngles.size())];
	return base::RandomIndex(interval.length) + interval.from;
}

void MiniStars::createStar(crl::time now) {
	auto ministar = MiniStar{
		.birthTime = now,
		.deathTime = now + randomInterval(_deathTime),
		.angle = angle(),
		.size = float64(randomInterval(_size)),
		.alpha = float64(randomInterval(_alpha)) / 100.,
		.sinFactor = randomInterval(_sinFactor) / 100.
			* (base::RandomIndex(2) == 1 ? 1. : -1.),
	};
	for (auto i = 0; i < _ministars.size(); i++) {
		if (ministar.birthTime > _ministars[i].deathTime) {
			_ministars[i] = ministar;
			return;
		}
	}
	_ministars.push_back(ministar);
}

class TopBar final : public Ui::RpWidget {
public:
	TopBar(not_null<QWidget*> parent, rpl::producer<bool> premiumValue);

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
	const style::TextStyle &_aboutSt;
	MiniStars _ministars;
	QSvgRenderer _star;
	Ui::Text::String _about;

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

TopBar::TopBar(not_null<QWidget*> parent, rpl::producer<bool> premiumValue)
: Ui::RpWidget(parent)
, _titleFont(st::boxTitle.style.font)
, _titlePadding(st::settingsPremiumTitlePadding)
, _aboutSt(st::settingsPremiumAboutTextStyle)
, _ministars([=](const QRect &r) { update(r); })
, _star(u":/gui/icons/settings/star.svg"_q) {
	std::move(
		premiumValue
	) | rpl::start_with_next([=](bool premium) {
		_titlePath = QPainterPath();
		_titlePath.addText(
			0,
			_titleFont->ascent,
			_titleFont,
			(premium
				? tr::lng_premium_summary_title_subscribed
				: tr::lng_premium_summary_title)(tr::now));
		const auto &about = premium
			? tr::lng_premium_summary_top_about_subscribed
			: tr::lng_premium_summary_top_about;
		_about.setMarkedText(
			_aboutSt,
			about(tr::now, Ui::Text::RichLangValue));
		update();
	}, lifetime());
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

	Ui::RpWidget::resizeEvent(e);
}

void TopBar::paintEvent(QPaintEvent *e) {
	Painter p(this);

	p.fillRect(e->rect(), Qt::transparent);

	const auto r = rect();
	auto pathTop = QPainterPath();
	if (_roundEdges) {
		pathTop.addRoundedRect(r, st::boxRadius, st::boxRadius);
	} else {
		pathTop.addRect(r);
	}
	auto pathBottom = QPainterPath();
	pathBottom.addRect(
		QRect(
			QPoint(r.x(), r.y() + r.height() - st::boxRadius),
			QSize(r.width(), st::boxRadius)));

	const auto gradientPointTop = r.height() / 3. * 2.;
	auto gradient = QLinearGradient(
		QPointF(0, gradientPointTop),
		QPointF(r.width(), r.height() - gradientPointTop));
	gradient.setColorAt(0., st::premiumButtonBg1->c);
	gradient.setColorAt(.6, st::premiumButtonBg2->c);
	gradient.setColorAt(1., st::premiumButtonBg3->c);

	PainterHighQualityEnabler hq(p);
	p.fillPath(pathTop + pathBottom, gradient);

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

	const auto &padding = st::boxRowPadding;
	const auto availableWidth = width() - padding.left() - padding.right();
	const auto titleTop = _starRect.top()
		+ _starRect.height()
		+ _titlePadding.top();
	const auto titlePathRect = _titlePath.boundingRect();
	const auto aboutTop = titleTop
		+ titlePathRect.height()
		+ _titlePadding.bottom();

	p.setFont(_aboutSt.font);
	_about.draw(p, padding.left(), aboutTop, availableWidth, style::al_top);

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

	[[nodiscard]] bool hasFlexibleTopBar() const override;
	[[nodiscard]] const Ui::RoundRect *bottomSkipRounding() const override;

	void setStepDataReference(std::any &data) override;

	[[nodiscard]] rpl::producer<> sectionShowBack() override final;

private:
	void setupContent();

	const not_null<Window::SessionController*> _controller;
	const QString _ref;

	base::unique_qptr<Ui::FadeWrap<Ui::IconButton>> _back;
	base::unique_qptr<Ui::IconButton> _close;
	rpl::variable<bool> _backToggles;
	rpl::variable<Info::Wrap> _wrap;
	std::optional<Ui::RoundRect> _bottomSkipRounding;

	rpl::event_stream<> _showBack;

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

const Ui::RoundRect *Premium::bottomSkipRounding() const {
	return _bottomSkipRounding ? &*_bottomSkipRounding : nullptr;
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

	const auto addRow = [&](
			rpl::producer<QString> &&title,
			rpl::producer<QString> &&text) {
		const auto labelAscent = stLabel.style.font->ascent;

		const auto label = content->add(
			object_ptr<Ui::FlatLabel>(
				content,
				std::move(title) | rpl::map(Ui::Text::Bold),
				stLabel),
			titlePadding);
		content->add(
			object_ptr<Ui::FlatLabel>(
				content,
				std::move(text),
				st::boxDividerLabel),
			descriptionPadding);

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
			addRow(base::take(entry.title), base::take(entry.description));
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
	gradient.setColorAt(0.0, st::premiumIconBg1->c);
	gradient.setColorAt(.28, st::premiumIconBg2->c);
	gradient.setColorAt(.55, st::premiumButtonBg2->c);
	gradient.setColorAt(1.0, st::premiumButtonBg1->c);
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
	const auto content = Ui::CreateChild<TopBar>(
		parent.get(),
		Data::AmPremiumValue(&_controller->session()));

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
		_back->toggleOn(_backToggles.value());
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

QPointer<Ui::RpWidget> Premium::createPinnedToBottom(
		not_null<Ui::RpWidget*> parent) {
	const auto content = Ui::CreateChild<Ui::RpWidget>(parent.get());

	const auto button = CreateSubscribeButton(_controller, content, [=] {
		SendScreenAccept(_controller);
		StartPremiumPayment(_controller, _ref);
	});
	auto text = _controller->session().api().premium().statusTextValue();
	const auto status = Ui::CreateChild<Ui::DividerLabel>(
		content,
		object_ptr<Ui::FlatLabel>(
			content,
			rpl::duplicate(text),
			st::boxDividerLabel),
		st::settingsPremiumStatusPadding,
		RectPart::Top);
	content->widthValue(
	) | rpl::start_with_next([=](int width) {
		status->resizeToWidth(width);
	}, status->lifetime());

	rpl::combine(
		button->heightValue(),
		status->heightValue(),
		std::move(text),
		Data::AmPremiumValue(&_controller->session())
	) | rpl::start_with_next([=](
			int buttonHeight,
			int statusHeight,
			const TextWithEntities &text,
			bool premium) {
		const auto padding = st::settingsPremiumButtonPadding;
		const auto finalHeight = !premium
			? (padding.top() + buttonHeight + padding.bottom())
			: text.text.isEmpty()
			? 0
			: statusHeight;
		content->resize(content->width(), finalHeight);
		button->moveToLeft(padding.left(), padding.top());
		status->moveToLeft(0, 0);
		button->setVisible(!premium);
		status->setVisible(premium && !text.text.isEmpty());
		if (!premium || text.text.isEmpty()) {
			_bottomSkipRounding.reset();
		} else if (!_bottomSkipRounding) {
			_bottomSkipRounding.emplace(st::boxRadius, st::boxDividerBg);
		}
	}, button->lifetime());

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
	controller->setPremiumRef(ref);
	controller->showSettings(Settings::PremiumId());
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

} // namespace Settings
