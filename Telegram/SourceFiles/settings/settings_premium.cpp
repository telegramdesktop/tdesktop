/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/settings_premium.h"

#include "boxes/premium_preview_box.h"
#include "boxes/sticker_set_box.h"
#include "chat_helpers/stickers_lottie.h" // LottiePlayerFromDocument.
#include "core/application.h"
#include "core/click_handler_types.h"
#include "core/ui_integration.h" // MarkedTextContext.
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_peer_values.h"
#include "data/data_session.h"
#include "data/stickers/data_custom_emoji.h" // SerializeCustomEmojiId.
#include "data/stickers/data_stickers.h"
#include "history/view/media/history_view_sticker.h" // EmojiSize.
#include "info/info_wrap_widget.h" // Info::Wrap.
#include "info/profile/info_profile_values.h"
#include "info/settings/info_settings_widget.h" // SectionCustomTopBarData.
#include "lang/lang_keys.h"
#include "lottie/lottie_single_player.h"
#include "main/main_account.h"
#include "main/main_app_config.h"
#include "main/main_session.h"
#include "settings/settings_common.h"
#include "settings/settings_premium.h"
#include "ui/abstract_button.h"
#include "ui/basic_click_handlers.h"
#include "ui/effects/gradient.h"
#include "ui/effects/premium_graphics.h"
#include "ui/effects/premium_stars_colored.h"
#include "ui/layers/generic_box.h"
#include "ui/text/format_values.h"
#include "ui/text/text_utilities.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/gradient_round_button.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/fade_wrap.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_controller.h"
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

[[nodiscard]] QString Svg() {
	return u":/gui/icons/settings/star.svg"_q;
}

namespace Ref {
namespace Gift {

struct Data {
	PeerId peerId;
	int months;
	bool me;

	explicit operator bool() const {
		return peerId != 0;
	}
};

[[nodiscard]] QString Serialize(const Data &gift) {
	return QString::number(gift.peerId.value)
		+ ':'
		+ QString::number(gift.months)
		+ ':'
		+ QString::number(gift.me ? 1 : 0);
}

[[nodiscard]] Data Parse(QStringView data) {
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

} // namespace Gift

namespace EmojiStatus {

struct Data {
	PeerId peerId;

	explicit operator bool() const {
		return peerId != 0;
	}
};

[[nodiscard]] QString Serialize(const Data &gift) {
	return QString("profile_:%1").arg(QString::number(gift.peerId.value));
}

[[nodiscard]] Data Parse(QStringView data) {
	if (data.startsWith(u"profile_:"_q)) {
		const auto components = data.split(':');
		if (components.size() != 2) {
			return {};
		}
		return {
			.peerId = PeerId(components[1].toULongLong()),
		};
	}
	return {};
}

} // namespace EmojiStatus
} // namespace Ref

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

class TopBarAbstract : public Ui::RpWidget {
public:
	using Ui::RpWidget::RpWidget;

	void setRoundEdges(bool value);

	virtual void setPaused(bool paused) = 0;
	virtual void setTextPosition(int x, int y) = 0;

protected:
	void paintEdges(QPainter &p, const QBrush &brush) const;

	[[nodiscard]] QRectF starRect(
		float64 topProgress,
		float64 sizeProgress) const;

private:
	bool _roundEdges = true;

};

void TopBarAbstract::setRoundEdges(bool value) {
	_roundEdges = value;
	update();
}

void TopBarAbstract::paintEdges(QPainter &p, const QBrush &brush) const {
	const auto r = rect();
	if (_roundEdges) {
		PainterHighQualityEnabler hq(p);
		const auto radius = st::boxRadius;
		p.setPen(Qt::NoPen);
		p.setBrush(brush);
		p.drawRoundedRect(
			r + QMargins{ 0, 0, 0, radius + 1 },
			radius,
			radius);
	} else {
		p.fillRect(r, brush);
	}
}

QRectF TopBarAbstract::starRect(
		float64 topProgress,
		float64 sizeProgress) const {
	const auto starSize = st::settingsPremiumStarSize * sizeProgress;
	return QRectF(
		QPointF(
			(width() - starSize.width()) / 2,
			st::settingsPremiumStarTopSkip * topProgress),
		starSize);
};

class EmojiStatusTopBar final {
public:
	EmojiStatusTopBar(
		not_null<DocumentData*> document,
		Fn<void(QRect)> callback,
		QSizeF size);

	void setCenter(QPointF position);
	void paint(QPainter &p);

private:
	QPixmap paintedPixmap(const QSize &size) const;

	QRectF _rect;
	std::shared_ptr<Data::DocumentMedia> _media;
	std::unique_ptr<Lottie::SinglePlayer> _lottie;
	rpl::lifetime _lifetime;

};

EmojiStatusTopBar::EmojiStatusTopBar(
	not_null<DocumentData*> document,
	Fn<void(QRect)> callback,
	QSizeF size)
: _rect(QPointF(), size) {
	const auto sticker = document->sticker();
	Assert(sticker != nullptr);
	_media = document->createMediaView();
	_media->checkStickerLarge();
	_media->goodThumbnailWanted();

	rpl::single() | rpl::then(
		document->owner().session().downloaderTaskFinished()
	) | rpl::start_with_next([=] {
		if (!_media->loaded()) {
			return;
		}
		_lifetime.destroy();
		if (sticker->isLottie()) {
			_lottie = ChatHelpers::LottiePlayerFromDocument(
				_media.get(),
				ChatHelpers::StickerLottieSize::EmojiInteractionReserved7, //
				size.toSize(),
				Lottie::Quality::High);

			_lifetime = _lottie->updates(
			) | rpl::start_with_next([=](Lottie::Update update) {
				v::match(update.data, [&](const Lottie::Information &) {
					callback(_rect.toRect());
				}, [&](const Lottie::DisplayFrameRequest &) {
					callback(_rect.toRect());
				});
			});
		}
	}, _lifetime);
}

void EmojiStatusTopBar::setCenter(QPointF position) {
	const auto size = _rect.size();
	const auto shift = QPointF(size.width() / 2., size.height() / 2.);
	_rect = QRectF(QPointF(position - shift), QPointF(position + shift));
}

QPixmap EmojiStatusTopBar::paintedPixmap(const QSize &size) const {
	const auto good = _media->goodThumbnail();
	if (const auto image = _media->getStickerLarge()) {
		return image->pix(size);
	} else if (good) {
		return good->pix(size);
	} else if (const auto thumbnail = _media->thumbnail()) {
		return thumbnail->pix(size, { .options = Images::Option::Blur });
	}
	return QPixmap();
}

void EmojiStatusTopBar::paint(QPainter &p) {
	if (_lottie) {
		if (_lottie->ready()) {
			const auto info = _lottie->frameInfo({
				.box = (_rect.size() * style::DevicePixelRatio()).toSize(),
			});

			p.drawImage(_rect, info.image);
			_lottie->markFrameShown();
		}
	} else if (_media) {
		p.drawPixmap(_rect, paintedPixmap(_rect.size().toSize()), _rect);
	}
}

class TopBarUser final : public TopBarAbstract {
public:
	TopBarUser(
		not_null<QWidget*> parent,
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> peer);

	void setPaused(bool paused) override;
	void setTextPosition(int x, int y) override;

protected:
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

private:
	void updateTitle(
		DocumentData *document,
		TextWithEntities name,
		not_null<Window::SessionController*> controller) const;
	void updateAbout(DocumentData *document) const;

	object_ptr<Ui::RpWidget> _content;
	object_ptr<Ui::FlatLabel> _title;
	object_ptr<Ui::FlatLabel> _about;
	Ui::Premium::ColoredMiniStars _ministars;

	struct {
		object_ptr<Ui::RpWidget> widget;
		Ui::Text::String text;
		Ui::Animations::Simple animation;
		bool shown = false;
		QPoint position;
	} _smallTop;

	std::unique_ptr<EmojiStatusTopBar> _emojiStatus;
	QImage _imageStar;

	QRectF _ministarsRect;
	QRectF _starRect;

};

TopBarUser::TopBarUser(
	not_null<QWidget*> parent,
	not_null<Window::SessionController*> controller,
	not_null<PeerData*> peer)
: TopBarAbstract(parent)
, _content(this)
, _title(_content, st::settingsPremiumUserTitle)
, _about(_content, st::settingsPremiumUserAbout)
, _ministars(_content)
, _smallTop({
	.widget = object_ptr<Ui::RpWidget>(this),
	.text = Ui::Text::String(
		st::boxTitle.style,
		tr::lng_premium_summary_title(tr::now)),
}) {
	_starRect = TopBarAbstract::starRect(1., 1.);

	auto documentValue = Info::Profile::EmojiStatusIdValue(
		peer
	) | rpl::map([=](DocumentId id) -> DocumentData* {
		const auto document = id
			? controller->session().data().document(id).get()
			: nullptr;
		return (document && document->sticker()) ? document : nullptr;
	});

	rpl::combine(
		std::move(documentValue),
		Info::Profile::NameValue(peer)
	) | rpl::start_with_next([=](
			DocumentData *document,
			TextWithEntities name) {
		if (document) {
			_emojiStatus = std::make_unique<EmojiStatusTopBar>(
				document,
				[=](QRect r) { update(std::move(r)); },
				HistoryView::Sticker::EmojiSize());
			_imageStar = QImage();
		} else {
			auto svg = QSvgRenderer(Svg());

			const auto size = _starRect.size().toSize();
			auto frame = QImage(
				size * style::DevicePixelRatio(),
				QImage::Format_ARGB32_Premultiplied);
			frame.setDevicePixelRatio(style::DevicePixelRatio());

			auto mask = frame;
			mask.fill(Qt::transparent);
			{
				Painter p(&mask);
				auto gradient = QLinearGradient(
					0,
					size.height(),
					size.width(),
					0);
				gradient.setStops(Ui::Premium::ButtonGradientStops());
				p.setPen(Qt::NoPen);
				p.setBrush(gradient);
				p.drawRect(0, 0, size.width(), size.height());
			}
			frame.fill(Qt::transparent);
			{
				Painter q(&frame);
				svg.render(&q, QRect(QPoint(), size));
				q.setCompositionMode(QPainter::CompositionMode_SourceIn);
				q.drawImage(0, 0, mask);
			}
			_imageStar = std::move(frame);

			_emojiStatus = nullptr;
		}

		updateTitle(document, name, controller);
		updateAbout(document);
	}, lifetime());

	rpl::combine(
		_title->sizeValue(),
		_about->sizeValue(),
		_content->sizeValue()
	) | rpl::start_with_next([=](
			const QSize &titleSize,
			const QSize &aboutSize,
			const QSize &size) {
		const auto rect = TopBarAbstract::starRect(1., 1.);
		const auto &padding = st::settingsPremiumUserTitlePadding;
		_title->moveToLeft(
			(size.width() - titleSize.width()) / 2,
			rect.top() + rect.height() + padding.top());
		_about->moveToLeft(
			(size.width() - aboutSize.width()) / 2,
			_title->y() + titleSize.height() + padding.bottom());

		const auto aboutBottom = _about->y() + _about->height();
		const auto height = (aboutBottom > st::settingsPremiumUserHeight)
			? aboutBottom + padding.bottom()
			: st::settingsPremiumUserHeight;
		{
			const auto was = maximumHeight();
			const auto now = height;
			if (was != now) {
				setMaximumHeight(now);
				if (was == size.height()) {
					resize(size.width(), now);
				}
			}
		}

		_content->resize(size.width(), maximumHeight());
	}, lifetime());

	sizeValue(
	) | rpl::start_with_next([=](const QSize &size) {
		_content->resize(size.width(), maximumHeight());
		_content->moveToLeft(0, -(_content->height() - size.height()));

		_smallTop.widget->resize(size.width(), minimumHeight());
		const auto shown = (minimumHeight() * 2 > size.height());
		if (_smallTop.shown != shown) {
			_smallTop.shown = shown;
			_smallTop.animation.start(
				[=] { _smallTop.widget->update(); },
				_smallTop.shown ? 0. : 1.,
				_smallTop.shown ? 1. : 0.,
				st::infoTopBarDuration);
		}
	}, lifetime());

	_smallTop.widget->paintRequest(
	) | rpl::start_with_next([=] {
		Painter p(_smallTop.widget);

		p.setOpacity(_smallTop.animation.value(_smallTop.shown ? 1. : 0.));
		paintEdges(p, st::boxBg);

		p.setPen(st::boxTitleFg);
		_smallTop.text.drawLeft(
			p,
			_smallTop.position.x(),
			_smallTop.position.y(),
			width(),
			width());
	}, lifetime());

	_content->paintRequest(
	) | rpl::start_with_next([=] {
		Painter p(_content);

		_ministars.paint(p);

		if (_emojiStatus) {
			_emojiStatus->paint(p);
		} else if (!_imageStar.isNull()) {
			p.drawImage(_starRect.topLeft(), _imageStar);
		}
	}, lifetime());

}

void TopBarUser::updateTitle(
		DocumentData *document,
		TextWithEntities name,
		not_null<Window::SessionController*> controller) const {
	if (!document) {
		return _title->setMarkedText(
			tr::lng_premium_summary_user_title(
				tr::now,
				lt_user,
				std::move(name),
				Ui::Text::WithEntities));
	}
	const auto stickerInfo = document->sticker();
	if (!stickerInfo) {
		return;
	}
	const auto &sets = document->owner().stickers().sets();
	const auto it = sets.find(stickerInfo->set.id);
	if (it == sets.cend()) {
		return;
	}
	const auto set = it->second.get();

	const auto text = (set->thumbnailDocumentId ? QChar('0') : QChar())
		+ set->title;
	const auto linkIndex = 1;
	const auto entityEmojiData = Data::SerializeCustomEmojiId(
		{ set->thumbnailDocumentId });
	const auto entities = EntitiesInText{
		{ EntityType::CustomEmoji, 0, 1, entityEmojiData },
		Ui::Text::Link(text, linkIndex).entities.front(),
	};
	auto title = tr::lng_premium_emoji_status_title(
		tr::now,
		lt_user,
		std::move(name),
		lt_link,
		{ .text = text, .entities = entities, },
		Ui::Text::WithEntities);
	const auto context = Core::MarkedTextContext{
		.session = &controller->session(),
		.customEmojiRepaint = [=] { _title->update(); },
	};
	_title->setMarkedText(std::move(title), context);
	auto link = std::make_shared<LambdaClickHandler>([=,
			stickerSetIdentifier = stickerInfo->set] {
		controller->show(
			Box<StickerSetBox>(
				controller,
				stickerSetIdentifier,
				Data::StickersType::Emoji),
			Ui::LayerOption::KeepOther);
	});
	_title->setLink(linkIndex, std::move(link));
}

void TopBarUser::updateAbout(DocumentData *document) const {
	_about->setMarkedText((document
		? tr::lng_premium_emoji_status_about
		: tr::lng_premium_summary_user_about)(
			tr::now,
			Ui::Text::RichLangValue));
}

void TopBarUser::setPaused(bool paused) {
	_ministars.setPaused(paused);
}

void TopBarUser::setTextPosition(int x, int y) {
	_smallTop.position = { x, y };
}

void TopBarUser::paintEvent(QPaintEvent *e) {
	Painter p(this);

	TopBarAbstract::paintEdges(p, st::boxBg);
}

void TopBarUser::resizeEvent(QResizeEvent *e) {
	_starRect = TopBarAbstract::starRect(1., 1.);

	const auto &rect = _starRect;
	const auto center = rect.center();
	const auto size = QSize(
		rect.width() * Ui::Premium::MiniStars::kSizeFactor,
		rect.height());
	const auto ministarsRect = QRect(
		QPoint(center.x() - size.width(), center.y() - size.height()),
		QPoint(center.x() + size.width(), center.y() + size.height()));
	_ministars.setPosition(ministarsRect.topLeft());
	_ministars.setSize(ministarsRect.size());

	if (_emojiStatus) {
		_emojiStatus->setCenter(_starRect.center());
	}
}

class TopBar final : public TopBarAbstract {
public:
	TopBar(
		not_null<QWidget*> parent,
		not_null<Window::SessionController*> controller,
		rpl::producer<QString> title,
		rpl::producer<TextWithEntities> about);

	void setPaused(bool paused) override;
	void setTextPosition(int x, int y) override;

protected:
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

private:
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

};

TopBar::TopBar(
	not_null<QWidget*> parent,
	not_null<Window::SessionController*> controller,
	rpl::producer<QString> title,
	rpl::producer<TextWithEntities> about)
: TopBarAbstract(parent)
, _titleFont(st::boxTitle.style.font)
, _titlePadding(st::settingsPremiumTitlePadding)
, _about(this, std::move(about), st::settingsPremiumAbout)
, _ministars([=](const QRect &r) { update(r); })
, _star(Svg()) {
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

void TopBar::setTextPosition(int x, int y) {
	_titlePosition = { x, y };
}

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

	TopBarAbstract::paintEdges(p, gradient);

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
	PainterHighQualityEnabler hq(p);
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
			&st::settingsPremiumArrow,
			&st::settingsPremiumArrowOver);
		arrow->setAttribute(Qt::WA_TransparentForMouseEvents);
		button->sizeValue(
		) | rpl::start_with_next([=](const QSize &s) {
			const auto &point = st::settingsPremiumArrowShift;
			arrow->moveToRight(
				-point.x(),
				point.y() + (s.height() - arrow->height()) / 2);
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
		const auto gift = Ref::Gift::Parse(_ref);
		if (gift) {
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

	const auto emojiStatusData = Ref::EmojiStatus::Parse(_ref);
	const auto isEmojiStatus = (!!emojiStatusData);

	auto peerWithPremium = [&]() -> PeerData* {
		if (isEmojiStatus) {
			auto &data = _controller->session().data();
			if (const auto peer = data.peer(emojiStatusData.peerId)) {
				return peer;
			}
		}
		return nullptr;
	}();

	const auto content = [&]() -> TopBarAbstract* {
		if (peerWithPremium) {
			return Ui::CreateChild<TopBarUser>(
				parent.get(),
				_controller,
				peerWithPremium);
		}
		return Ui::CreateChild<TopBar>(
			parent.get(),
			_controller,
			std::move(title),
			std::move(about));
	}();
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

	content->setMaximumHeight(isEmojiStatus
		? st::settingsPremiumUserHeight
		: st::settingsPremiumTopHeight);
	content->setMinimumHeight(st::infoLayerTopBarHeight);

	content->resize(content->width(), content->maximumHeight());

	_wrap.value(
	) | rpl::start_with_next([=](Info::Wrap wrap) {
		const auto isLayer = (wrap == Info::Wrap::Layer);
		_back = base::make_unique_q<Ui::FadeWrap<Ui::IconButton>>(
			content,
			object_ptr<Ui::IconButton>(
				content,
				isEmojiStatus
					? (isLayer ? st::infoTopBarBack : st::infoLayerTopBarBack)
					: (isLayer
						? st::settingsPremiumLayerTopBarBack
						: st::settingsPremiumTopBarBack)),
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
				isEmojiStatus
					? st::infoTopBarClose
					: st::settingsPremiumTopBarClose);
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

	if (Ref::Gift::Parse(_ref)) {
		return nullptr;
	}

	auto buttonText = [&]() -> std::optional<rpl::producer<QString>> {
		if (const auto emojiData = Ref::EmojiStatus::Parse(_ref)) {
			auto &data = _controller->session().data();
			if (const auto peer = data.peer(emojiData.peerId)) {
				return Info::Profile::EmojiStatusIdValue(
					peer
				) | rpl::map([](DocumentId id) {
					return id
						? tr::lng_premium_emoji_status_button()
						: tr::lng_premium_summary_user_button();
				}) | rpl::flatten_latest();
			}
		}
		return std::nullopt;
	}();

	_subscribe = CreateSubscribeButton({
		_controller,
		content,
		[ref = _ref] { return ref; },
		std::move(buttonText),
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
	ShowPremium(controller, Ref::Gift::Serialize({ peer->id, months, me }));
}

void ShowEmojiStatusPremium(
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> peer) {
	ShowPremium(controller, Ref::EmojiStatus::Serialize({ peer->id }));
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
