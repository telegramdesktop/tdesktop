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
#include "core/local_url_handlers.h" // Core::TryConvertUrlToLocal.
#include "core/ui_integration.h" // MarkedTextContext.
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_emoji_statuses.h"
#include "data/data_peer_values.h"
#include "data/data_session.h"
#include "data/stickers/data_custom_emoji.h" // SerializeCustomEmojiId.
#include "data/stickers/data_stickers.h"
#include "history/view/media/history_view_sticker.h" // EmojiSize.
#include "history/view/media/history_view_sticker_player.h"
#include "info/info_wrap_widget.h" // Info::Wrap.
#include "info/profile/info_profile_values.h"
#include "info/settings/info_settings_widget.h" // SectionCustomTopBarData.
#include "lang/lang_keys.h"
#include "main/main_app_config.h"
#include "main/main_session.h"
#include "settings/settings_common_session.h"
#include "ui/abstract_button.h"
#include "ui/basic_click_handlers.h"
#include "ui/effects/gradient.h"
#include "ui/effects/premium_graphics.h"
#include "ui/effects/premium_stars_colored.h"
#include "ui/effects/premium_top_bar.h"
#include "ui/layers/generic_box.h"
#include "ui/text/format_values.h"
#include "ui/text/text_utilities.h"
#include "ui/toast/toast.h"
#include "ui/widgets/checkbox.h" // Ui::RadiobuttonGroup.
#include "ui/widgets/gradient_round_button.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/fade_wrap.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/new_badges.h"
#include "ui/painter.h"
#include "ui/power_saving.h"
#include "ui/vertical_list.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "window/window_session_controller_link_info.h"
#include "base/unixtime.h"
#include "apiwrap.h"
#include "api/api_premium.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_premium.h"
#include "styles/style_info.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"

namespace Settings {
namespace {

using SectionCustomTopBarData = Info::Settings::SectionCustomTopBarData;

[[nodiscard]] Data::PremiumSubscriptionOptions SubscriptionOptionsForRows(
		Data::PremiumSubscriptionOptions result) {
	for (auto &option : result) {
		const auto total = option.costTotal;
		const auto perMonth = option.costPerMonth;

		option.costTotal = tr::lng_premium_gift_per(
			tr::now,
			lt_cost,
			perMonth);
		option.costPerMonth = tr::lng_premium_subscribe_total(
			tr::now,
			lt_cost,
			total);

		if (option.duration == tr::lng_months(tr::now, lt_count, 1)) {
			option.costPerMonth = QString();
			option.duration = tr::lng_premium_subscribe_months_1(tr::now);
		} else if (option.duration == tr::lng_months(tr::now, lt_count, 6)) {
			option.duration = tr::lng_premium_subscribe_months_6(tr::now);
		} else if (option.duration == tr::lng_years(tr::now, lt_count, 1)) {
			option.duration = tr::lng_premium_subscribe_months_12(tr::now);
		}
	}
	return result;
}

[[nodiscard]] int TopTransitionSkip() {
	return (st::settingsButton.padding.top()
		+ st::settingsPremiumRowTitlePadding.top()) / 2;
}

namespace Ref {
namespace Gift {

struct Data {
	PeerId peerId;
	int months = 0;
	bool me = false;

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
	PremiumFeature section = PremiumFeature::DoubleLimits;
	bool newBadge = false;
};

using Order = std::vector<QString>;

[[nodiscard]] Order FallbackOrder() {
	return Order{
		u"stories"_q,
		u"more_upload"_q,
		u"double_limits"_q,
		u"last_seen"_q,
		u"voice_to_text"_q,
		u"faster_download"_q,
		u"translations"_q,
		u"animated_emoji"_q,
		u"emoji_status"_q,
		u"saved_tags"_q,
		//u"peer_colors"_q,
		u"wallpapers"_q,
		u"profile_badge"_q,
		u"message_privacy"_q,
		u"advanced_chat_management"_q,
		u"no_ads"_q,
		//u"app_icons"_q,
		u"infinite_reactions"_q,
		u"animated_userpics"_q,
		u"premium_stickers"_q,
		u"business"_q,
		u"effects"_q,
	};
}

[[nodiscard]] base::flat_map<QString, Entry> EntryMap() {
	return base::flat_map<QString, Entry>{
		{
			u"saved_tags"_q,
			Entry{
				&st::settingsPremiumIconTags,
				tr::lng_premium_summary_subtitle_tags_for_messages(),
				tr::lng_premium_summary_about_tags_for_messages(),
				PremiumFeature::TagsForMessages,
			},
		},
		{
			u"last_seen"_q,
			Entry{
				&st::settingsPremiumIconLastSeen,
				tr::lng_premium_summary_subtitle_last_seen(),
				tr::lng_premium_summary_about_last_seen(),
				PremiumFeature::LastSeen,
			},
		},
		{
			u"message_privacy"_q,
			Entry{
				&st::settingsPremiumIconPrivacy,
				tr::lng_premium_summary_subtitle_message_privacy(),
				tr::lng_premium_summary_about_message_privacy(),
				PremiumFeature::MessagePrivacy,
			},
		},
		{
			u"wallpapers"_q,
			Entry{
				&st::settingsPremiumIconWallpapers,
				tr::lng_premium_summary_subtitle_wallpapers(),
				tr::lng_premium_summary_about_wallpapers(),
				PremiumFeature::Wallpapers,
			},
		},
		{
			u"stories"_q,
			Entry{
				&st::settingsPremiumIconStories,
				tr::lng_premium_summary_subtitle_stories(),
				tr::lng_premium_summary_about_stories(),
				PremiumFeature::Stories,
			},
		},
		{
			u"double_limits"_q,
			Entry{
				&st::settingsPremiumIconDouble,
				tr::lng_premium_summary_subtitle_double_limits(),
				tr::lng_premium_summary_about_double_limits(),
				PremiumFeature::DoubleLimits,
			},
		},
		{
			u"more_upload"_q,
			Entry{
				&st::settingsPremiumIconFiles,
				tr::lng_premium_summary_subtitle_more_upload(),
				tr::lng_premium_summary_about_more_upload(),
				PremiumFeature::MoreUpload,
			},
		},
		{
			u"faster_download"_q,
			Entry{
				&st::settingsPremiumIconSpeed,
				tr::lng_premium_summary_subtitle_faster_download(),
				tr::lng_premium_summary_about_faster_download(),
				PremiumFeature::FasterDownload,
			},
		},
		{
			u"voice_to_text"_q,
			Entry{
				&st::settingsPremiumIconVoice,
				tr::lng_premium_summary_subtitle_voice_to_text(),
				tr::lng_premium_summary_about_voice_to_text(),
				PremiumFeature::VoiceToText,
			},
		},
		{
			u"no_ads"_q,
			Entry{
				&st::settingsPremiumIconChannelsOff,
				tr::lng_premium_summary_subtitle_no_ads(),
				tr::lng_premium_summary_about_no_ads(),
				PremiumFeature::NoAds,
			},
		},
		{
			u"emoji_status"_q,
			Entry{
				&st::settingsPremiumIconStatus,
				tr::lng_premium_summary_subtitle_emoji_status(),
				tr::lng_premium_summary_about_emoji_status(),
				PremiumFeature::EmojiStatus,
			},
		},
		{
			u"infinite_reactions"_q,
			Entry{
				&st::settingsPremiumIconLike,
				tr::lng_premium_summary_subtitle_infinite_reactions(),
				tr::lng_premium_summary_about_infinite_reactions(),
				PremiumFeature::InfiniteReactions,
			},
		},
		{
			u"premium_stickers"_q,
			Entry{
				&st::settingsIconStickers,
				tr::lng_premium_summary_subtitle_premium_stickers(),
				tr::lng_premium_summary_about_premium_stickers(),
				PremiumFeature::Stickers,
			},
		},
		{
			u"animated_emoji"_q,
			Entry{
				&st::settingsIconEmoji,
				tr::lng_premium_summary_subtitle_animated_emoji(),
				tr::lng_premium_summary_about_animated_emoji(),
				PremiumFeature::AnimatedEmoji,
			},
		},
		{
			u"advanced_chat_management"_q,
			Entry{
				&st::settingsIconChat,
				tr::lng_premium_summary_subtitle_advanced_chat_management(),
				tr::lng_premium_summary_about_advanced_chat_management(),
				PremiumFeature::AdvancedChatManagement,
			},
		},
		{
			u"profile_badge"_q,
			Entry{
				&st::settingsPremiumIconStar,
				tr::lng_premium_summary_subtitle_profile_badge(),
				tr::lng_premium_summary_about_profile_badge(),
				PremiumFeature::ProfileBadge,
			},
		},
		{
			u"animated_userpics"_q,
			Entry{
				&st::settingsPremiumIconPlay,
				tr::lng_premium_summary_subtitle_animated_userpics(),
				tr::lng_premium_summary_about_animated_userpics(),
				PremiumFeature::AnimatedUserpics,
			},
		},
		{
			u"translations"_q,
			Entry{
				&st::settingsPremiumIconTranslations,
				tr::lng_premium_summary_subtitle_translation(),
				tr::lng_premium_summary_about_translation(),
				PremiumFeature::RealTimeTranslation,
			},
		},
		{
			u"business"_q,
			Entry{
				&st::settingsPremiumIconBusiness,
				tr::lng_premium_summary_subtitle_business(),
				tr::lng_premium_summary_about_business(),
				PremiumFeature::Business,
				true,
			},
		},
		{
			u"effects"_q,
			Entry{
				&st::settingsPremiumIconEffects,
				tr::lng_premium_summary_subtitle_effects(),
				tr::lng_premium_summary_about_effects(),
				PremiumFeature::Effects,
				true,
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

class EmojiStatusTopBar final {
public:
	EmojiStatusTopBar(
		not_null<DocumentData*> document,
		Fn<void(QRect)> callback,
		QSizeF size);

	void setCenter(QPointF position);
	void setPaused(bool paused);
	void paint(QPainter &p);

private:
	QRectF _rect;
	std::shared_ptr<Data::DocumentMedia> _media;
	std::unique_ptr<HistoryView::StickerPlayer> _player;
	bool _paused = false;
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
			_player = std::make_unique<HistoryView::LottiePlayer>(
				ChatHelpers::LottiePlayerFromDocument(
				_media.get(),
				ChatHelpers::StickerLottieSize::EmojiInteractionReserved7, //
				size.toSize(),
				Lottie::Quality::High));
		} else if (sticker->isWebm()) {
			_player = std::make_unique<HistoryView::WebmPlayer>(
				_media->owner()->location(),
				_media->bytes(),
				size.toSize());
		} else if (sticker) {
			_player = std::make_unique<HistoryView::StaticStickerPlayer>(
				_media->owner()->location(),
				_media->bytes(),
				size.toSize());
		}
		if (_player) {
			_player->setRepaintCallback([=] { callback(_rect.toRect()); });
		} else {
			callback(_rect.toRect());
		}
	}, _lifetime);
}

void EmojiStatusTopBar::setCenter(QPointF position) {
	const auto size = _rect.size();
	const auto shift = QPointF(size.width() / 2., size.height() / 2.);
	_rect = QRectF(QPointF(position - shift), QPointF(position + shift));
}

void EmojiStatusTopBar::setPaused(bool paused) {
	_paused = paused;
}

void EmojiStatusTopBar::paint(QPainter &p) {
	if (_player && _player->ready()) {
		const auto frame = _player->frame(
			_rect.size().toSize(),
			(_media->owner()->emojiUsesTextColor()
				? st::profileVerifiedCheckBg->c
				: QColor(0, 0, 0, 0)),
			false,
			crl::now(),
			_paused || On(PowerSaving::kEmojiStatus));

		p.drawImage(_rect.toRect(), frame.image);
		if (!_paused) {
			_player->markFrameShown();
		}
	}
}

class TopBarUser final : public Ui::Premium::TopBarAbstract {
public:
	TopBarUser(
		not_null<QWidget*> parent,
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> peer,
		rpl::producer<> showFinished);

	void setPaused(bool paused) override;
	void setTextPosition(int x, int y) override;

	rpl::producer<int> additionalHeight() const override;

protected:
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

private:
	void updateTitle(
		DocumentData *document,
		TextWithEntities name,
		not_null<Window::SessionController*> controller);
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

	QRectF _starRect;

};

TopBarUser::TopBarUser(
	not_null<QWidget*> parent,
	not_null<Window::SessionController*> controller,
	not_null<PeerData*> peer,
	rpl::producer<> showFinished)
: TopBarAbstract(parent, st::userPremiumCover)
, _content(this)
, _title(_content, st::settingsPremiumUserTitle)
, _about(_content, st::userPremiumCover.about)
, _ministars(_content, true)
, _smallTop({
	.widget = object_ptr<Ui::RpWidget>(this),
	.text = Ui::Text::String(
		st::boxTitle.style,
		tr::lng_premium_summary_title(tr::now)),
}) {
	_starRect = TopBarAbstract::starRect(1., 1.);

	rpl::single() | rpl::then(
		style::PaletteChanged()
	) | rpl::start_with_next([=] {
		TopBarAbstract::computeIsDark();
		update();
	}, lifetime());

	auto documentValue = Info::Profile::EmojiStatusIdValue(
		peer
	) | rpl::map([=](EmojiStatusId id) -> DocumentData* {
		const auto documentId = id.collectible
			? id.collectible->documentId
			: id.documentId;
		const auto document = documentId
			? controller->session().data().document(documentId).get()
			: nullptr;
		return (document && document->sticker()) ? document : nullptr;
	});

	rpl::combine(
		std::move(documentValue),
		Info::Profile::NameValue(peer)
	) | rpl::start_with_next([=](
			DocumentData *document,
			const QString &name) {
		if (document) {
			_emojiStatus = std::make_unique<EmojiStatusTopBar>(
				document,
				[=](QRect r) { _content->update(std::move(r)); },
				HistoryView::Sticker::EmojiSize());
			_imageStar = QImage();
		} else {
			_emojiStatus = nullptr;
			_imageStar = Ui::Premium::GenerateStarForLightTopBar(_starRect);
		}

		updateTitle(document, { name }, controller);
		updateAbout(document);

		auto event = QResizeEvent(size(), size());
		resizeEvent(&event);
		update();
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

	const auto smallTopShadow = Ui::CreateChild<Ui::FadeShadow>(
		_smallTop.widget.data());
	smallTopShadow->setDuration(st::infoTopBarDuration);
	rpl::combine(
		rpl::single(
			false
		) | rpl::then(std::move(showFinished) | rpl::map_to(true)),
		sizeValue()
	) | rpl::start_with_next([=](bool showFinished, const QSize &size) {
		_content->resize(size.width(), maximumHeight());
		const auto skip = TopTransitionSkip();
		_content->moveToLeft(0, size.height() - _content->height() - skip);

		_smallTop.widget->resize(size.width(), minimumHeight());
		smallTopShadow->resizeToWidth(size.width());
		smallTopShadow->moveToLeft(
			0,
			_smallTop.widget->height() - smallTopShadow->height());
		const auto shown = (minimumHeight() * 2 > size.height());
		if (_smallTop.shown != shown) {
			_smallTop.shown = shown;
			if (!showFinished) {
				_smallTop.widget->update();
				smallTopShadow->toggle(_smallTop.shown, anim::type::instant);
			} else {
				_smallTop.animation.start(
					[=] { _smallTop.widget->update(); },
					_smallTop.shown ? 0. : 1.,
					_smallTop.shown ? 1. : 0.,
					st::infoTopBarDuration);
				smallTopShadow->toggle(_smallTop.shown, anim::type::normal);
			}
		}
	}, lifetime());

	_smallTop.widget->paintRequest(
	) | rpl::start_with_next([=] {
		Painter p(_smallTop.widget);

		p.setOpacity(_smallTop.animation.value(_smallTop.shown ? 1. : 0.));
		TopBarAbstract::paintEdges(p);

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
		auto p = QPainter(_content);

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
		not_null<Window::SessionController*> controller) {
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
	const auto owner = &document->owner();
	const auto &sets = owner->stickers().sets();
	const auto setId = stickerInfo->set.id;
	const auto it = sets.find(setId);
	if (it == sets.cend()) {
		return;
	}
	const auto set = it->second.get();
	const auto coloredId = owner->customEmojiManager().coloredSetId();

	const auto text = (set->thumbnailDocumentId ? QChar('0') : QChar())
		+ set->title;
	const auto linkIndex = 1;
	const auto entityEmojiData = Data::SerializeCustomEmojiId(
		set->thumbnailDocumentId);
	const auto entities = EntitiesInText{
		{ EntityType::CustomEmoji, 0, 1, entityEmojiData },
		Ui::Text::Link(text, linkIndex).entities.front(),
	};
	auto title = (setId == coloredId)
		? tr::lng_premium_emoji_status_title_colored(
			tr::now,
			lt_user,
			std::move(name),
			Ui::Text::WithEntities)
		: tr::lng_premium_emoji_status_title(
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
		setPaused(true);
		const auto box = controller->show(Box<StickerSetBox>(
			controller->uiShow(),
			stickerSetIdentifier,
			Data::StickersType::Emoji));

		box->boxClosing(
		) | rpl::start_with_next(crl::guard(this, [=] {
			setPaused(false);
		}), box->lifetime());
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
	if (_emojiStatus) {
		_emojiStatus->setPaused(paused);
	}
}

void TopBarUser::setTextPosition(int x, int y) {
	_smallTop.position = { x, y };
}

rpl::producer<int> TopBarUser::additionalHeight() const {
	return rpl::never<int>();
}

void TopBarUser::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);

	TopBarAbstract::paintEdges(p);
}

void TopBarUser::resizeEvent(QResizeEvent *e) {
	_starRect = TopBarAbstract::starRect(1., 1.);

	_ministars.setCenter(_starRect.toRect());

	if (_emojiStatus) {
		_emojiStatus->setCenter(_starRect.center());
	}
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
	void setupSubscriptionOptions(not_null<Ui::VerticalLayout*> container);

	const not_null<Window::SessionController*> _controller;
	const QString _ref;

	QPointer<Ui::GradientButton> _subscribe;
	base::unique_qptr<Ui::FadeWrap<Ui::IconButton>> _back;
	base::unique_qptr<Ui::IconButton> _close;
	rpl::variable<bool> _backToggles;
	rpl::variable<Info::Wrap> _wrap;
	Fn<void(bool)> _setPaused;

	std::shared_ptr<Ui::RadiobuttonGroup> _radioGroup;

	rpl::event_stream<> _showBack;
	rpl::event_stream<> _showFinished;
	rpl::variable<QString> _buttonText;

};

Premium::Premium(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent)
, _controller(controller)
, _ref(ResolveRef(controller->premiumRef()))
, _radioGroup(std::make_shared<Ui::RadiobuttonGroup>()) {
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

void Premium::setupSubscriptionOptions(
		not_null<Ui::VerticalLayout*> container) {
	const auto isEmojiStatus = (!!Ref::EmojiStatus::Parse(_ref));
	const auto isGift = (!!Ref::Gift::Parse(_ref));

	const auto options = container->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			container,
			object_ptr<Ui::VerticalLayout>(container)));
	const auto skip = container->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			container,
			object_ptr<Ui::VerticalLayout>(container)));
	const auto content = options->entity();

	Ui::AddSkip(content, st::settingsPremiumOptionsPadding.top());

	const auto apiPremium = &_controller->session().api().premium();
	Ui::Premium::AddGiftOptions(
		content,
		_radioGroup,
		SubscriptionOptionsForRows(apiPremium->subscriptionOptions()),
		st::premiumSubscriptionOption,
		true);

	Ui::AddSkip(content, st::settingsPremiumOptionsPadding.bottom());
	Ui::AddDivider(content);

	const auto lastSkip = TopTransitionSkip() * (isEmojiStatus ? 1 : 2);

	Ui::AddSkip(content, lastSkip - st::defaultVerticalListSkip);
	Ui::AddSkip(skip->entity(), lastSkip);

	if (isEmojiStatus || isGift) {
		options->toggle(false, anim::type::instant);
		skip->toggle(true, anim::type::instant);
		return;
	}
	auto toggleOn = rpl::combine(
		Data::AmPremiumValue(&_controller->session()),
		apiPremium->statusTextValue(
		) | rpl::map([=] {
			return apiPremium->subscriptionOptions().size() < 2;
		})
	) | rpl::map([=](bool premium, bool noOptions) {
		return !premium && !noOptions;
	});
	options->toggleOn(rpl::duplicate(toggleOn), anim::type::instant);
	skip->toggleOn(std::move(
		toggleOn
	) | rpl::map([](bool value) { return !value; }), anim::type::instant);
}

void Premium::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

	setupSubscriptionOptions(content);

	auto buttonCallback = [=](PremiumFeature section) {
		_setPaused(true);
		const auto hidden = crl::guard(this, [=] { _setPaused(false); });

		ShowPremiumPreviewToBuy(_controller, section, hidden);
	};
	AddSummaryPremium(content, _controller, _ref, std::move(buttonCallback));
#if 0
	Ui::AddSkip(content);
	Ui::AddDivider(content);
	Ui::AddSkip(content);

	content->add(
		object_ptr<Ui::FlatLabel>(
			content,
			tr::lng_premium_summary_bottom_subtitle(
			) | Ui::Text::ToBold(),
			stLabel),
		st::defaultSubsectionTitlePadding);
	content->add(
		object_ptr<Ui::FlatLabel>(
			content,
			tr::lng_premium_summary_bottom_about(Ui::Text::RichLangValue),
			st::aboutLabel),
		st::boxRowPadding);
	Ui::AddSkip(
		content,
		stDefault.padding.top() + stDefault.padding.bottom());
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

	const auto content = [&]() -> Ui::Premium::TopBarAbstract* {
		if (peerWithPremium) {
			return Ui::CreateChild<TopBarUser>(
				parent.get(),
				_controller,
				peerWithPremium,
				_showFinished.events());
		}
		const auto weak = base::make_weak(_controller);
		const auto clickContextOther = [=] {
			return QVariant::fromValue(ClickHandlerContext{
				.sessionWindow = weak,
				.botStartAutoSubmit = true,
			});
		};
		return Ui::CreateChild<Ui::Premium::TopBar>(
			parent.get(),
			st::defaultPremiumCover,
			Ui::Premium::TopBarDescriptor{
				.clickContextOther = clickContextOther,
				.title = std::move(title),
				.about = std::move(about),
			});
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

	const auto calculateMaximumHeight = [=] {
		return isEmojiStatus
			? st::settingsPremiumUserHeight + TopTransitionSkip()
			: st::settingsPremiumTopHeight;
	};

	content->setMaximumHeight(calculateMaximumHeight());
	content->setMinimumHeight(st::infoLayerTopBarHeight);

	content->resize(content->width(), content->maximumHeight());
	content->additionalHeight(
	) | rpl::start_with_next([=](int additionalHeight) {
		const auto wasMax = (content->height() == content->maximumHeight());
		content->setMaximumHeight(calculateMaximumHeight()
			+ additionalHeight);
		if (wasMax) {
			content->resize(content->width(), content->maximumHeight());
		}
	}, content->lifetime());

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

	const auto emojiStatusData = Ref::EmojiStatus::Parse(_ref);
	const auto session = &_controller->session();

	auto buttonText = [&]() -> std::optional<rpl::producer<QString>> {
		if (emojiStatusData) {
			auto &data = session->data();
			if (const auto peer = data.peer(emojiStatusData.peerId)) {
				return Info::Profile::EmojiStatusIdValue(
					peer
				) | rpl::map([=](EmojiStatusId id) {
					return id
						? tr::lng_premium_emoji_status_button()
						: _buttonText.value();
						// : tr::lng_premium_summary_user_button();
				}) | rpl::flatten_latest();
			}
		}
		return _buttonText.value();
	}();

	_subscribe = CreateSubscribeButton({
		_controller,
		content,
		[ref = _ref] { return ref; },
		std::move(buttonText),
		std::nullopt,
		[=, options = session->api().premium().subscriptionOptions()] {
			const auto value = _radioGroup->current();
			return (value < options.size() && value >= 0)
				? options[value].botUrl
				: QString();
		},
	});
#if 0
	if (emojiStatusData) {
		// "Learn More" should open the general Premium Settings
		// so we override the button callback.
		// To have ability to jump back to the User Premium Settings
		// we should replace the ref explicitly.
		_subscribe->setClickedCallback([=] {
			const auto ref = _ref;
			const auto controller = _controller;
			ShowPremium(controller, QString());
			controller->setPremiumRef(ref);
		});
	} else {
#endif
	{
		const auto callback = [=](int value) {
			auto &api = _controller->session().api();
			const auto options = api.premium().subscriptionOptions();
			if (options.empty()) {
				return;
			}
			Assert(value < options.size() && value >= 0);
			auto text = tr::lng_premium_subscribe_button(
				tr::now,
				lt_cost,
				options[value].costPerMonth);
			_buttonText = std::move(text);
		};
		_radioGroup->setChangedCallback(callback);
		callback(0);
	}

	_showFinished.events(
	) | rpl::take(1) | rpl::start_with_next([=] {
		_subscribe->startGlareAnimation();
	}, _subscribe->lifetime());

	content->widthValue(
	) | rpl::start_with_next([=](int width) {
		const auto padding = st::settingsPremiumButtonPadding;
		_subscribe->resizeToWidth(width - padding.left() - padding.right());
	}, _subscribe->lifetime());

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

template <>
struct SectionFactory<Premium> : AbstractSectionFactory {
	object_ptr<AbstractSection> create(
		not_null<QWidget*> parent,
		not_null<Window::SessionController*> controller,
		not_null<Ui::ScrollArea*> scroll,
		rpl::producer<Container> containerValue
	) const final override {
		return object_ptr<Premium>(parent, controller);
	}
	bool hasCustomTopBar() const final override {
		return true;
	}

	[[nodiscard]] static const std::shared_ptr<SectionFactory> &Instance() {
		static const auto result = std::make_shared<SectionFactory>();
		return result;
	}
};

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
	if (const auto unique = peer->emojiStatusId().collectible.get()) {
		Core::ResolveAndShowUniqueGift(controller->uiShow(), unique->slug);
	} else {
		ShowPremium(controller, Ref::EmojiStatus::Serialize({ peer->id }));
	}
}

void StartPremiumPayment(
		not_null<Window::SessionController*> controller,
		const QString &ref) {
	const auto session = &controller->session();
	const auto username = session->appConfig().get<QString>(
		u"premium_bot_username"_q,
		QString());
	const auto slug = session->appConfig().get<QString>(
		u"premium_invoice_slug"_q,
		QString());
	if (!username.isEmpty()) {
		controller->showPeerByLink(Window::PeerByLinkInfo{
			.usernameOrId = username,
			.resolveType = Window::ResolveType::BotStart,
			.startToken = ref,
			.startAutoSubmit = true,
		});
	} else if (!slug.isEmpty()) {
		UrlClickHandler::Open("https://t.me/$" + slug);
	}
}

QString LookupPremiumRef(PremiumFeature section) {
	for (const auto &[ref, entry] : EntryMap()) {
		if (entry.section == section) {
			return ref;
		}
	}
	return QString();
}

void ShowPremiumPromoToast(
		std::shared_ptr<ChatHelpers::Show> show,
		TextWithEntities textWithLink,
		const QString &ref) {
	ShowPremiumPromoToast(show, [=](
			not_null<Main::Session*> session) {
		Expects(&show->session() == session);

		return show->resolveWindow();
	}, std::move(textWithLink), ref);
}

void ShowPremiumPromoToast(
		std::shared_ptr<Main::SessionShow> show,
		Fn<Window::SessionController*(
			not_null<Main::Session*>)> resolveWindow,
		TextWithEntities textWithLink,
		const QString &ref) {
	using WeakToast = base::weak_ptr<Ui::Toast::Instance>;
	const auto toast = std::make_shared<WeakToast>();
	(*toast) = show->showToast({
		.text = std::move(textWithLink),
		.filter = crl::guard(&show->session(), [=](
				const ClickHandlerPtr &,
				Qt::MouseButton button) {
			if (button == Qt::LeftButton) {
				if (const auto strong = toast->get()) {
					strong->hideAnimated();
					(*toast) = nullptr;
					if (const auto controller = resolveWindow(
							&show->session())) {
						Settings::ShowPremium(controller, ref);
					}
					return true;
				}
			}
			return false;
		}),
		.adaptive = true,
		.duration = Ui::Toast::kDefaultDuration * 2,
	});
}

not_null<Ui::RoundButton*> CreateLockedButton(
		not_null<QWidget*> parent,
		rpl::producer<QString> text,
		const style::RoundButton &st,
		rpl::producer<bool> locked) {
	const auto result = Ui::CreateChild<Ui::RoundButton>(
		parent.get(),
		rpl::single(QString()),
		st);

	const auto labelSt = result->lifetime().make_state<style::FlatLabel>(
		st::defaultFlatLabel);
	labelSt->style.font = st.style.font;
	labelSt->textFg = st.textFg;

	const auto label = Ui::CreateChild<Ui::FlatLabel>(
		result,
		std::move(text),
		*labelSt);
	label->setAttribute(Qt::WA_TransparentForMouseEvents);

	const auto icon = Ui::CreateChild<Ui::RpWidget>(result);
	icon->setAttribute(Qt::WA_TransparentForMouseEvents);
	icon->resize(st::stickersPremiumLock.size());
	icon->paintRequest() | rpl::start_with_next([=] {
		auto p = QPainter(icon);
		st::stickersPremiumLock.paint(p, 0, 0, icon->width());
	}, icon->lifetime());

	rpl::combine(
		result->widthValue(),
		label->widthValue(),
		std::move(locked)
	) | rpl::start_with_next([=](int outer, int inner, bool locked) {
		if (locked) {
			icon->show();
			inner += icon->width();
			label->move(
				(outer - inner) / 2 + icon->width(),
				st::similarChannelsLock.textTop);
			icon->move(
				(outer - inner) / 2,
				st::similarChannelsLock.textTop);
		} else {
			icon->hide();
			label->move(
				(outer - inner) / 2,
				st::similarChannelsLock.textTop);
		}
	}, result->lifetime());

	return result;
}

not_null<Ui::GradientButton*> CreateSubscribeButton(
		SubscribeButtonArgs &&args) {
	Expects(args.show || args.controller);

	auto show = args.show ? std::move(args.show) : args.controller->uiShow();
	auto resolve = [show](not_null<Main::Session*> session) {
		Expects(session == &show->session());

		return show->resolveWindow();
	};
	return CreateSubscribeButton(
		std::move(show),
		std::move(resolve),
		std::move(args));
}

not_null<Ui::GradientButton*> CreateSubscribeButton(
		std::shared_ptr<::Main::SessionShow> show,
		Fn<Window::SessionController*(
			not_null<::Main::Session*>)> resolveWindow,
		SubscribeButtonArgs &&args) {
	const auto result = Ui::CreateChild<Ui::GradientButton>(
		args.parent.get(),
		args.gradientStops
			? base::take(*args.gradientStops)
			: Ui::Premium::ButtonGradientStops());

	result->setClickedCallback([
			show,
			resolveWindow,
			promo = args.showPromo,
			computeRef = args.computeRef,
			computeBotUrl = args.computeBotUrl] {
		const auto window = resolveWindow(
			&show->session());
		if (!window) {
			return;
		} else if (promo) {
			Settings::ShowPremium(window, computeRef());
			return;
		}
		const auto url = computeBotUrl ? computeBotUrl() : QString();
		if (!url.isEmpty()) {
			const auto local = Core::TryConvertUrlToLocal(url);
			if (local.isEmpty()) {
				return;
			}
			UrlClickHandler::Open(
				local,
				QVariant::fromValue(ClickHandlerContext{
					.sessionWindow = base::make_weak(window),
					.botStartAutoSubmit = true,
				}));
		} else {
			SendScreenAccept(window);
			StartPremiumPayment(window, computeRef());
		}
	});

	const auto &st = st::premiumPreviewBox.button;
	result->resize(args.parent->width(), st.height);

	const auto premium = &show->session().api().premium();
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

std::vector<PremiumFeature> PremiumFeaturesOrder(
		not_null<Main::Session*> session) {
	const auto mtpOrder = session->appConfig().get<Order>(
		"premium_promo_order",
		FallbackOrder());
	return ranges::views::all(
		mtpOrder
	) | ranges::views::transform([](const QString &s) {
		if (s == u"more_upload"_q) {
			return PremiumFeature::MoreUpload;
		} else if (s == u"faster_download"_q) {
			return PremiumFeature::FasterDownload;
		} else if (s == u"voice_to_text"_q) {
			return PremiumFeature::VoiceToText;
		} else if (s == u"no_ads"_q) {
			return PremiumFeature::NoAds;
		} else if (s == u"emoji_status"_q) {
			return PremiumFeature::EmojiStatus;
		} else if (s == u"infinite_reactions"_q) {
			return PremiumFeature::InfiniteReactions;
		} else if (s == u"saved_tags"_q) {
			return PremiumFeature::TagsForMessages;
		} else if (s == u"last_seen"_q) {
			return PremiumFeature::LastSeen;
		} else if (s == u"message_privacy"_q) {
			return PremiumFeature::MessagePrivacy;
		} else if (s == u"premium_stickers"_q) {
			return PremiumFeature::Stickers;
		} else if (s == u"animated_emoji"_q) {
			return PremiumFeature::AnimatedEmoji;
		} else if (s == u"advanced_chat_management"_q) {
			return PremiumFeature::AdvancedChatManagement;
		} else if (s == u"profile_badge"_q) {
			return PremiumFeature::ProfileBadge;
		} else if (s == u"animated_userpics"_q) {
			return PremiumFeature::AnimatedUserpics;
		} else if (s == u"translations"_q) {
			return PremiumFeature::RealTimeTranslation;
		} else if (s == u"wallpapers"_q) {
			return PremiumFeature::Wallpapers;
		} else if (s == u"effects"_q) {
			return PremiumFeature::Effects;
		}
		return PremiumFeature::kCount;
	}) | ranges::views::filter([](PremiumFeature type) {
		return (type != PremiumFeature::kCount);
	}) | ranges::to_vector;
}

void AddSummaryPremium(
		not_null<Ui::VerticalLayout*> content,
		not_null<Window::SessionController*> controller,
		const QString &ref,
		Fn<void(PremiumFeature)> buttonCallback) {
	const auto &stDefault = st::settingsButton;
	const auto &stLabel = st::defaultFlatLabel;
	const auto iconSize = st::settingsPremiumIconDouble.size();
	const auto &titlePadding = st::settingsPremiumRowTitlePadding;
	const auto &descriptionPadding = st::settingsPremiumRowAboutPadding;

	auto entryMap = EntryMap();
	auto iconContainers = std::vector<Ui::AbstractButton*>();
	iconContainers.reserve(int(entryMap.size()));

	const auto addRow = [&](Entry &entry) {
		const auto labelAscent = stLabel.style.font->ascent;
		const auto button = Ui::CreateChild<Ui::SettingsButton>(
			content.get(),
			rpl::single(QString()));

		const auto label = content->add(
			object_ptr<Ui::FlatLabel>(
				content,
				std::move(entry.title) | Ui::Text::ToBold(),
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

		if (entry.newBadge) {
			Ui::NewBadge::AddAfterLabel(content, label);
		}
		const auto dummy = Ui::CreateChild<Ui::AbstractButton>(content.get());
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
		button->setClickedCallback([=] { buttonCallback(section); });

		iconContainers.push_back(dummy);
	};

	auto icons = std::vector<const style::icon *>();
	icons.reserve(int(entryMap.size()));
	{
		const auto session = &controller->session();
		const auto mtpOrder = session->appConfig().get<Order>(
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

		SendScreenShow(controller, mtpOrder, ref);
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

	Ui::AddSkip(content, descriptionPadding.bottom());
}

std::unique_ptr<Ui::RpWidget> MakeEmojiStatusPreview(
		not_null<QWidget*> parent,
		not_null<DocumentData*> document) {
	auto result = std::make_unique<Ui::RpWidget>(parent);

	const auto raw = result.get();
	const auto size = HistoryView::Sticker::EmojiSize();
	const auto emoji = raw->lifetime().make_state<EmojiStatusTopBar>(
		document,
		[=](QRect r) { raw->update(std::move(r)); },
		size);
	raw->paintRequest() | rpl::start_with_next([=] {
		auto p = QPainter(raw);
		emoji->paint(p);
	}, raw->lifetime());

	raw->sizeValue() | rpl::start_with_next([=](QSize size) {
		emoji->setCenter(QPointF(size.width() / 2., size.height() / 2.));
	}, raw->lifetime());

	return result;
}

} // namespace Settings
