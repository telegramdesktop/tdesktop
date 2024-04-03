/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/sticker_set_box.h"

#include "data/data_document.h"
#include "data/data_session.h"
#include "data/data_file_origin.h"
#include "data/data_document_media.h"
#include "data/data_peer_values.h"
#include "data/stickers/data_stickers.h"
#include "data/stickers/data_custom_emoji.h"
#include "menu/menu_send.h"
#include "lang/lang_keys.h"
#include "ui/boxes/confirm_box.h"
#include "boxes/premium_preview_box.h"
#include "core/application.h"
#include "mtproto/sender.h"
#include "storage/storage_account.h"
#include "dialogs/ui/dialogs_layout.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/gradient_round_button.h"
#include "ui/image/image.h"
#include "ui/image/image_location_factory.h"
#include "ui/text/text_utilities.h"
#include "ui/text/custom_emoji_instance.h"
#include "ui/effects/path_shift_gradient.h"
#include "ui/emoji_config.h"
#include "ui/painter.h"
#include "ui/power_saving.h"
#include "ui/toast/toast.h"
#include "ui/widgets/popup_menu.h"
#include "ui/cached_round_corners.h"
#include "lottie/lottie_multi_player.h"
#include "lottie/lottie_animation.h"
#include "chat_helpers/compose/compose_show.h"
#include "chat_helpers/stickers_lottie.h"
#include "chat_helpers/stickers_list_widget.h"
#include "media/clip/media_clip_reader.h"
#include "window/window_controller.h"
#include "settings/settings_premium.h"
#include "base/unixtime.h"
#include "main/main_session.h"
#include "apiwrap.h"
#include "api/api_toggling_media.h"
#include "api/api_common.h"
#include "mainwidget.h"
#include "mainwindow.h"
#include "styles/style_layers.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_info.h"
#include "styles/style_menu_icons.h"
#include "styles/style_premium.h"

#include <QtWidgets/QApplication>
#include <QtGui/QClipboard>
#include <QtSvg/QSvgRenderer>

namespace {

constexpr auto kStickersPerRow = 5;
constexpr auto kEmojiPerRow = 8;
constexpr auto kMinRepaintDelay = crl::time(33);
constexpr auto kMinAfterScrollDelay = crl::time(33);
constexpr auto kGrayLockOpacity = 0.3;

using Data::StickersSet;
using Data::StickersPack;
using SetFlag = Data::StickersSetFlag;

[[nodiscard]] std::optional<QColor> ComputeImageColor(const QImage &frame) {
	if (frame.isNull()
		|| frame.format() != QImage::Format_ARGB32_Premultiplied) {
		return {};
	}
	auto sr = int64();
	auto sg = int64();
	auto sb = int64();
	auto sa = int64();
	const auto factor = frame.devicePixelRatio();
	const auto size = st::stickersPremiumLock.size() * factor;
	const auto width = std::min(frame.width(), size.width());
	const auto height = std::min(frame.height(), size.height());
	const auto skipx = (frame.width() - width) / 2;
	const auto radius = st::roundRadiusSmall;
	const auto skipy = std::max(frame.height() - height - radius, 0);
	const auto perline = frame.bytesPerLine();
	const auto addperline = perline - (width * 4);
	auto bits = static_cast<const uchar*>(frame.bits())
		+ perline * skipy
		+ sizeof(uint32) * skipx;
	for (auto y = 0; y != height; ++y) {
		for (auto x = 0; x != width; ++x) {
			sb += int(*bits++);
			sg += int(*bits++);
			sr += int(*bits++);
			sa += int(*bits++);
		}
		bits += addperline;
	}
	if (!sa) {
		return {};
	}
	return QColor(sr * 255 / sa, sg * 255 / sa, sb * 255 / sa, 255);

}

[[nodiscard]] QColor ComputeLockColor(const QImage &frame) {
	return ComputeImageColor(frame).value_or(st::windowSubTextFg->c);
}

void ValidatePremiumLockBg(QImage &image, const QImage &frame) {
	if (!image.isNull()) {
		return;
	}
	const auto factor = style::DevicePixelRatio();
	const auto size = st::stickersPremiumLock.size();
	image = QImage(
		size * factor,
		QImage::Format_ARGB32_Premultiplied);
	image.setDevicePixelRatio(factor);
	auto p = QPainter(&image);
	const auto color = ComputeLockColor(frame);
	p.fillRect(
		QRect(QPoint(), size),
		anim::color(color, st::windowSubTextFg, kGrayLockOpacity));
	p.end();

	image = Images::Circle(std::move(image));
}

void ValidatePremiumStarFg(QImage &image) {
	if (!image.isNull()) {
		return;
	}
	const auto factor = style::DevicePixelRatio();
	const auto size = st::stickersPremiumLock.size();
	image = QImage(
		size * factor,
		QImage::Format_ARGB32_Premultiplied);
	image.setDevicePixelRatio(factor);
	image.fill(Qt::transparent);
	auto p = QPainter(&image);
	auto star = QSvgRenderer(u":/gui/icons/settings/star.svg"_q);
	const auto skip = size.width() / 5.;
	const auto outer = QRectF(QPointF(), size).marginsRemoved(
		{ skip, skip, skip, skip });
	p.setBrush(st::premiumButtonFg);
	p.setPen(Qt::NoPen);
	star.render(&p, outer);
}

[[nodiscard]] TextForMimeData PrepareTextFromEmoji(
		not_null<DocumentData*> document) {
	const auto info = document->sticker();
	const auto text = info ? info->alt : QString();
	return {
		.expanded = text,
		.rich = {
			text,
			{
				EntityInText(
					EntityType::CustomEmoji,
					0,
					text.size(),
					Data::SerializeCustomEmojiId(document))
			},
		},
	};
}

} // namespace

StickerPremiumMark::StickerPremiumMark(not_null<Main::Session*> session) {
	style::PaletteChanged(
	) | rpl::start_with_next([=] {
		_lockGray = QImage();
		_star = QImage();
	}, _lifetime);

	Data::AmPremiumValue(
		session
	) | rpl::start_with_next([=](bool premium) {
		_premium = premium;
	}, _lifetime);
}

void StickerPremiumMark::paint(
		QPainter &p,
		const QImage &frame,
		QImage &backCache,
		QPoint position,
		QSize singleSize,
		int outerWidth) {
	validateLock(frame, backCache);
	const auto &bg = frame.isNull() ? _lockGray : backCache;
	const auto factor = style::DevicePixelRatio();
	const auto radius = st::roundRadiusSmall;
	const auto point = position + QPoint(
		(_premium
			? (singleSize.width() - (bg.width() / factor) - radius)
			: (singleSize.width() - (bg.width() / factor)) / 2),
		singleSize.height() - (bg.height() / factor) - radius);
	p.drawImage(point, bg);
	if (_premium) {
		validateStar();
		p.drawImage(point, _star);
	} else {
		st::stickersPremiumLock.paint(p, point, outerWidth);
	}
}

void StickerPremiumMark::validateLock(
		const QImage &frame,
		QImage &backCache) {
	auto &image = frame.isNull() ? _lockGray : backCache;
	ValidatePremiumLockBg(image, frame);
}

void StickerPremiumMark::validateStar() {
	ValidatePremiumStarFg(_star);
}

class StickerSetBox::Inner final : public Ui::RpWidget {
public:
	Inner(
		QWidget *parent,
		std::shared_ptr<ChatHelpers::Show> show,
		const StickerSetIdentifier &set,
		Data::StickersType type);

	[[nodiscard]] bool loaded() const;
	[[nodiscard]] bool notInstalled() const;
	[[nodiscard]] bool premiumEmojiSet() const;
	[[nodiscard]] bool official() const;
	[[nodiscard]] rpl::producer<TextWithEntities> title() const;
	[[nodiscard]] QString shortName() const;
	[[nodiscard]] bool isEmojiSet() const;
	[[nodiscard]] uint64 setId() const;

	void install();
	[[nodiscard]] rpl::producer<uint64> setInstalled() const;
	[[nodiscard]] rpl::producer<uint64> setArchived() const;
	[[nodiscard]] rpl::producer<> updateControls() const;

	[[nodiscard]] rpl::producer<Error> errors() const;

	void archiveStickers();

	[[nodiscard]] Data::StickersType setType() const {
		return (_setFlags & SetFlag::Emoji)
			? Data::StickersType::Emoji
			: (_setFlags & SetFlag::Masks)
			? Data::StickersType::Masks
			: Data::StickersType::Stickers;
	}

	~Inner();

protected:
	void mousePressEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void contextMenuEvent(QContextMenuEvent *e) override;
	void paintEvent(QPaintEvent *e) override;
	void leaveEventHook(QEvent *e) override;

private:
	struct Element {
		not_null<DocumentData*> document;
		std::shared_ptr<Data::DocumentMedia> documentMedia;
		Lottie::Animation *lottie = nullptr;
		Media::Clip::ReaderPointer webm;
		Ui::Text::CustomEmoji *emoji = nullptr;
		Ui::Animations::Simple overAnimation;

		mutable QImage premiumLock;
	};

	void visibleTopBottomUpdated(int visibleTop, int visibleBottom) override;

	[[nodiscard]] Ui::MessageSendingAnimationFrom messageSentAnimationInfo(
		int index,
		not_null<DocumentData*> document) const;
	[[nodiscard]] QSize boundingBoxSize() const;

	void paintSticker(
		Painter &p,
		int index,
		QPoint position,
		bool paused,
		crl::time now) const;
	void setupLottie(int index);
	void setupWebm(int index);
	void clipCallback(
		Media::Clip::Notification notification,
		not_null<DocumentData*> document,
		int index);
	void setupEmoji(int index);
	[[nodiscard]] not_null<Ui::Text::CustomEmoji*> resolveCustomEmoji(
		not_null<DocumentData*> document);
	void customEmojiRepaint();

	void updateSelected();
	void setSelected(int selected);
	void startOverAnimation(int index, float64 from, float64 to);
	int stickerFromGlobalPos(const QPoint &p) const;

	void gotSet(const MTPmessages_StickerSet &set);
	void installDone(const MTPmessages_StickerSetInstallResult &result);

	void chosen(
		int index,
		not_null<DocumentData*> sticker,
		Api::SendOptions options);

	not_null<Lottie::MultiPlayer*> getLottiePlayer();

	void showPreview();
	void showPreviewAt(QPoint globalPos);

	void updateItems();
	void repaintItems(crl::time now = 0);

	const std::shared_ptr<ChatHelpers::Show> _show;
	const not_null<Main::Session*> _session;

	MTP::Sender _api;
	std::vector<Element> _elements;
	std::unique_ptr<Lottie::MultiPlayer> _lottiePlayer;

	base::flat_map<
		not_null<DocumentData*>,
		std::unique_ptr<Ui::Text::CustomEmoji>> _customEmoji;
	bool _repaintScheduled = false;

	StickersPack _pack;
	base::flat_map<EmojiPtr, StickersPack> _emoji;
	bool _loaded = false;
	uint64 _setId = 0;
	uint64 _setAccessHash = 0;
	uint64 _setHash = 0;
	DocumentId _setThumbnailDocumentId = 0;
	QString _setTitle, _setShortName;
	int _setCount = 0;
	Data::StickersSetFlags _setFlags;
	int _rowsCount = 0;
	int _perRow = 0;
	QSize _singleSize;
	TimeId _setInstallDate = TimeId(0);
	StickerType _setThumbnailType = StickerType::Webp;
	ImageWithLocation _setThumbnail;

	const std::unique_ptr<Ui::PathShiftGradient> _pathGradient;
	mutable StickerPremiumMark _premiumMark;

	int _visibleTop = 0;
	int _visibleBottom = 0;
	crl::time _lastScrolledAt = 0;
	crl::time _lastUpdatedAt = 0;
	base::Timer _updateItemsTimer;

	StickerSetIdentifier _input;
	QMargins _padding;

	mtpRequestId _installRequest = 0;

	int _selected = -1;

	base::Timer _previewTimer;
	int _previewShown = -1;

	base::unique_qptr<Ui::PopupMenu> _menu;

	rpl::event_stream<uint64> _setInstalled;
	rpl::event_stream<uint64> _setArchived;
	rpl::event_stream<> _updateControls;
	rpl::event_stream<Error> _errors;

};

StickerSetBox::StickerSetBox(
	QWidget *parent,
	std::shared_ptr<ChatHelpers::Show> show,
	const StickerSetIdentifier &set,
	Data::StickersType type)
: _show(std::move(show))
, _session(&_show->session())
, _set(set)
, _type(type) {
}

StickerSetBox::StickerSetBox(
	QWidget *parent,
	std::shared_ptr<ChatHelpers::Show> show,
	not_null<Data::StickersSet*> set)
: StickerSetBox(parent, std::move(show), set->identifier(), set->type()) {
}

QPointer<Ui::BoxContent> StickerSetBox::Show(
		std::shared_ptr<ChatHelpers::Show> show,
		not_null<DocumentData*> document) {
	if (const auto sticker = document->sticker()) {
		if (sticker->set) {
			auto box = Box<StickerSetBox>(
				show,
				sticker->set,
				sticker->setType);
			const auto result = QPointer<Ui::BoxContent>(box.data());
			show->showBox(std::move(box));
			return result;
		}
	}
	return nullptr;
}

void StickerSetBox::prepare() {
	setTitle(tr::lng_contacts_loading());

	_inner = setInnerWidget(
		object_ptr<Inner>(this, _show, _set, _type),
		st::stickersScroll);
	_session->data().stickers().updated(
		_type
	) | rpl::start_with_next([=] {
		updateButtons();
	}, lifetime());

	setDimensions(
		st::boxWideWidth,
		(_type == Data::StickersType::Emoji
			? st::emojiSetMaxHeight
			: st::stickersMaxHeight));

	updateTitleAndButtons();

	_inner->updateControls(
	) | rpl::start_with_next([=] {
		updateTitleAndButtons();
	}, lifetime());

	_inner->setInstalled(
	) | rpl::start_with_next([=](uint64 setId) {
		if (_inner->setType() == Data::StickersType::Masks) {
			showToast(tr::lng_masks_installed(tr::now));
		} else if (_inner->setType() == Data::StickersType::Emoji) {
			auto &stickers = _session->data().stickers();
			stickers.notifyEmojiSetInstalled(setId);
		} else if (_inner->setType() == Data::StickersType::Stickers) {
			auto &stickers = _session->data().stickers();
			stickers.notifyStickerSetInstalled(setId);
		}
		closeBox();
	}, lifetime());

	_inner->errors(
	) | rpl::start_with_next([=](Error error) {
		handleError(error);
	}, lifetime());

	_inner->setArchived(
	) | rpl::start_with_next([=](uint64 setId) {
		const auto type = _inner->setType();
		if (type == Data::StickersType::Emoji) {
			return;
		}

		showToast((type == Data::StickersType::Masks)
				? tr::lng_masks_has_been_archived(tr::now)
				: tr::lng_stickers_has_been_archived(tr::now));

		auto &order = (type == Data::StickersType::Masks)
			? _session->data().stickers().maskSetsOrderRef()
			: _session->data().stickers().setsOrderRef();
		const auto index = order.indexOf(setId);
		if (index != -1) {
			order.removeAt(index);

			auto &local = _session->local();
			if (type == Data::StickersType::Masks) {
				local.writeInstalledMasks();
				local.writeArchivedMasks();
			} else {
				local.writeInstalledStickers();
				local.writeArchivedStickers();
			}
		}

		_session->data().stickers().notifyUpdated(type);

		closeBox();
	}, lifetime());
}

void StickerSetBox::addStickers() {
	_inner->install();
}

void StickerSetBox::copyStickersLink() {
	const auto part = _inner->isEmojiSet() ? u"addemoji"_q : "addstickers";
	const auto url = _session->createInternalLinkFull(
		part + '/' + _inner->shortName());
	QGuiApplication::clipboard()->setText(url);
}

void StickerSetBox::handleError(Error error) {
	const auto guard = gsl::finally(crl::guard(this, [=] {
		closeBox();
	}));

	switch (error) {
	case Error::NotFound:
		_show->showBox(
			Ui::MakeInformBox(tr::lng_stickers_not_found(tr::now)));
		break;
	default: Unexpected("Error in StickerSetBox::handleError.");
	}
}

void StickerSetBox::updateTitleAndButtons() {
	setTitle(_inner->title());
	updateButtons();
}

void StickerSetBox::updateButtons() {
	clearButtons();
	if (_inner->loaded()) {
		const auto type = _inner->setType();
		const auto share = [=] {
			copyStickersLink();
			showToast(type == Data::StickersType::Emoji
					? tr::lng_stickers_copied_emoji(tr::now)
					: tr::lng_stickers_copied(tr::now));
		};
		if (_inner->notInstalled()) {
			if (!_session->premium()
				&& _session->premiumPossible()
				&& _inner->premiumEmojiSet()) {
				const auto &st = st::premiumPreviewDoubledLimitsBox;
				setStyle(st);
				auto button = CreateUnlockButton(
					this,
					tr::lng_premium_unlock_emoji());
				button->resizeToWidth(st::boxWideWidth
					- st.buttonPadding.left()
					- st.buttonPadding.left());
				button->setClickedCallback([=] {
					using namespace ChatHelpers;
					const auto usage = WindowUsage::PremiumPromo;
					if (const auto window = _show->resolveWindow(usage)) {
						Settings::ShowPremium(window, u"animated_emoji"_q);
					}
				});
				addButton(std::move(button));
			} else {
				auto addText = (type == Data::StickersType::Emoji)
					? tr::lng_stickers_add_emoji()
					: (type == Data::StickersType::Masks)
					? tr::lng_stickers_add_masks()
					: tr::lng_stickers_add_pack();
				addButton(std::move(addText), [=] { addStickers(); });
				addButton(tr::lng_cancel(), [=] { closeBox(); });
			}

			if (!_inner->shortName().isEmpty()) {
				const auto top = addTopButton(st::infoTopBarMenu);
				const auto menu =
					std::make_shared<base::unique_qptr<Ui::PopupMenu>>();
				top->setClickedCallback([=] {
					*menu = base::make_unique_q<Ui::PopupMenu>(
						top,
						st::popupMenuWithIcons);
					(*menu)->addAction(
						((type == Data::StickersType::Emoji)
							? tr::lng_stickers_share_emoji
							: (type == Data::StickersType::Masks)
							? tr::lng_stickers_share_masks
							: tr::lng_stickers_share_pack)(tr::now),
						[=] { share(); closeBox(); },
						&st::menuIconShare);
					(*menu)->popup(QCursor::pos());
					return true;
				});
			}
		} else if (_inner->official()) {
			addButton(tr::lng_about_done(), [=] { closeBox(); });
		} else {
			auto shareText = (type == Data::StickersType::Emoji)
				? tr::lng_stickers_share_emoji()
				: (type == Data::StickersType::Masks)
				? tr::lng_stickers_share_masks()
				: tr::lng_stickers_share_pack();
			addButton(std::move(shareText), std::move(share));
			addButton(tr::lng_cancel(), [=] { closeBox(); });

			if (!_inner->shortName().isEmpty()) {
				const auto top = addTopButton(st::infoTopBarMenu);
				const auto archive = [=] {
					_inner->archiveStickers();
				};
				const auto remove = [=] {
					const auto session = &_show->session();
					auto box = ChatHelpers::MakeConfirmRemoveSetBox(
						session,
						st::boxLabel,
						_inner->setId());
					if (box) {
						_show->showBox(std::move(box));
					}
				};
				const auto menu =
					std::make_shared<base::unique_qptr<Ui::PopupMenu>>();
				top->setClickedCallback([=] {
					*menu = base::make_unique_q<Ui::PopupMenu>(
						top,
						st::popupMenuWithIcons);
					if (type == Data::StickersType::Emoji) {
						(*menu)->addAction(
							tr::lng_custom_emoji_remove_pack_button(tr::now),
							remove,
							&st::menuIconRemove);
					} else {
						(*menu)->addAction(
							(type == Data::StickersType::Masks
								? tr::lng_masks_archive_pack(tr::now)
								: tr::lng_stickers_archive_pack(tr::now)),
							archive,
							&st::menuIconArchive);
					}
					(*menu)->popup(QCursor::pos());
					return true;
				});
			}
		}
	} else {
		addButton(tr::lng_cancel(), [=] { closeBox(); });
	}
	update();
}

void StickerSetBox::resizeEvent(QResizeEvent *e) {
	BoxContent::resizeEvent(e);
	_inner->resize(width(), _inner->height());
}

StickerSetBox::Inner::Inner(
	QWidget *parent,
	std::shared_ptr<ChatHelpers::Show> show,
	const StickerSetIdentifier &set,
	Data::StickersType type)
: RpWidget(parent)
, _show(std::move(show))
, _session(&_show->session())
, _api(&_session->mtp())
, _setId(set.id)
, _setAccessHash(set.accessHash)
, _setShortName(set.shortName)
, _pathGradient(std::make_unique<Ui::PathShiftGradient>(
	st::windowBgRipple,
	st::windowBgOver,
	[=] { repaintItems(); }))
, _premiumMark(_session)
, _updateItemsTimer([=] { updateItems(); })
, _input(set)
, _padding((type == Data::StickersType::Emoji)
	? st::emojiSetPadding
	: st::stickersPadding)
, _previewTimer([=] { showPreview(); }) {
	setAttribute(Qt::WA_OpaquePaintEvent);

	_api.request(MTPmessages_GetStickerSet(
		Data::InputStickerSet(_input),
		MTP_int(0) // hash
	)).done([=](const MTPmessages_StickerSet &result) {
		gotSet(result);
	}).fail([=] {
		_loaded = true;
		_errors.fire(Error::NotFound);
	}).send();

	_session->api().updateStickers();

	_session->downloaderTaskFinished(
	) | rpl::start_with_next([=] {
		updateItems();
	}, lifetime());

	setMouseTracking(true);
}

void StickerSetBox::Inner::gotSet(const MTPmessages_StickerSet &set) {
	_pack.clear();
	_emoji.clear();
	_elements.clear();
	_selected = -1;
	setCursor(style::cur_default);
	const auto owner = &_session->data();
	const auto premiumPossible = _session->premiumPossible();
	set.match([&](const MTPDmessages_stickerSet &data) {
		const auto &v = data.vdocuments().v;
		_pack.reserve(v.size());
		_elements.reserve(v.size());
		for (const auto &item : v) {
			const auto document = owner->processDocument(item);
			const auto sticker = document->sticker();
			if (!sticker) {
				continue;
			}
			_pack.push_back(document);
			if (!document->isPremiumSticker() || premiumPossible) {
				_elements.push_back({
					document,
					document->createMediaView(),
				});
			}
		}
		for (const auto &pack : data.vpacks().v) {
			pack.match([&](const MTPDstickerPack &pack) {
				if (const auto emoji = Ui::Emoji::Find(qs(pack.vemoticon()))) {
					const auto original = emoji->original();
					auto &stickers = pack.vdocuments().v;

					auto p = StickersPack();
					p.reserve(stickers.size());
					for (auto j = 0, c = int(stickers.size()); j != c; ++j) {
						auto doc = _session->data().document(stickers[j].v);
						if (!doc || !doc->sticker()) continue;

						p.push_back(doc);
					}
					_emoji[original] = std::move(p);
				}
			});
		}
		data.vset().match([&](const MTPDstickerSet &set) {
			_setTitle = _session->data().stickers().getSetTitle(
				set);
			_setShortName = qs(set.vshort_name());
			_setId = set.vid().v;
			_setAccessHash = set.vaccess_hash().v;
			_setHash = set.vhash().v;
			_setCount = set.vcount().v;
			_setFlags = Data::ParseStickersSetFlags(set);
			_setInstallDate = set.vinstalled_date().value_or(0);
			_setThumbnailDocumentId = set.vthumb_document_id().value_or_empty();
			_setThumbnail = [&] {
				if (const auto thumbs = set.vthumbs()) {
					for (const auto &thumb : thumbs->v) {
						const auto result = Images::FromPhotoSize(
							_session,
							set,
							thumb);
						if (result.location.valid()) {
							_setThumbnailType
								= Data::ThumbnailTypeFromPhotoSize(thumb);
							return result;
						}
					}
				}
				return ImageWithLocation();
			}();
			const auto &sets = _session->data().stickers().sets();
			const auto it = sets.find(_setId);
			if (it != sets.cend()) {
				const auto set = it->second.get();
				const auto clientFlags = set->flags
					& (SetFlag::Featured
						| SetFlag::NotLoaded
						| SetFlag::Unread
						| SetFlag::Special);
				_setFlags |= clientFlags;
				set->flags = _setFlags;
				set->installDate = _setInstallDate;
				set->stickers = _pack;
				set->emoji = _emoji;
				set->setThumbnail(_setThumbnail, _setThumbnailType);
			}
		});
	}, [&](const MTPDmessages_stickerSetNotModified &data) {
		LOG(("API Error: Unexpected messages.stickerSetNotModified."));
	});

	if (_pack.isEmpty()) {
		_errors.fire(Error::NotFound);
		return;
	}
	_perRow = isEmojiSet() ? kEmojiPerRow : kStickersPerRow;
	_rowsCount = (_pack.size() + _perRow - 1) / _perRow;
	_singleSize = isEmojiSet() ? st::emojiSetSize : st::stickersSize;

	resize(
		_padding.left() + _perRow * _singleSize.width(),
		_padding.top() + _rowsCount * _singleSize.height() + _padding.bottom());

	_loaded = true;
	updateSelected();
	_updateControls.fire({});
}

rpl::producer<uint64> StickerSetBox::Inner::setInstalled() const {
	return _setInstalled.events();
}

rpl::producer<uint64> StickerSetBox::Inner::setArchived() const {
	return _setArchived.events();
}

rpl::producer<> StickerSetBox::Inner::updateControls() const {
	return _updateControls.events();
}

rpl::producer<StickerSetBox::Error> StickerSetBox::Inner::errors() const {
	return _errors.events();
}

void StickerSetBox::Inner::installDone(
		const MTPmessages_StickerSetInstallResult &result) {
	auto &stickers = _session->data().stickers();
	auto &sets = stickers.setsRef();
	const auto type = setType();

	const bool wasArchived = (_setFlags & SetFlag::Archived);
	if (wasArchived && type != Data::StickersType::Emoji) {
		const auto index = ((type == Data::StickersType::Masks)
			? stickers.archivedMaskSetsOrderRef()
			: stickers.archivedSetsOrderRef()).indexOf(_setId);
		if (index >= 0) {
			((type == Data::StickersType::Masks)
				? stickers.archivedMaskSetsOrderRef()
				: stickers.archivedSetsOrderRef()).removeAt(index);
		}
	}
	_setInstallDate = base::unixtime::now();
	_setFlags &= ~SetFlag::Archived;
	_setFlags |= SetFlag::Installed;
	auto it = sets.find(_setId);
	if (it == sets.cend()) {
		it = sets.emplace(
			_setId,
			std::make_unique<StickersSet>(
				&_session->data(),
				_setId,
				_setAccessHash,
				_setHash,
				_setTitle,
				_setShortName,
				_setCount,
				_setFlags,
				_setInstallDate)).first;
	} else {
		it->second->flags = _setFlags;
		it->second->installDate = _setInstallDate;
	}
	const auto set = it->second.get();
	set->thumbnailDocumentId = _setThumbnailDocumentId;
	set->setThumbnail(_setThumbnail, _setThumbnailType);
	set->stickers = _pack;
	set->emoji = _emoji;

	auto &order = (type == Data::StickersType::Emoji)
		? stickers.emojiSetsOrderRef()
		: (type == Data::StickersType::Masks)
		? stickers.maskSetsOrderRef()
		: stickers.setsOrderRef();
	const auto insertAtIndex = 0, currentIndex = int(order.indexOf(_setId));
	if (currentIndex != insertAtIndex) {
		if (currentIndex > 0) {
			order.removeAt(currentIndex);
		}
		order.insert(insertAtIndex, _setId);
	}

	const auto customIt = sets.find(Data::Stickers::CustomSetId);
	if (customIt != sets.cend()) {
		const auto custom = customIt->second.get();
		for (const auto sticker : std::as_const(_pack)) {
			const int removeIndex = custom->stickers.indexOf(sticker);
			if (removeIndex >= 0) {
				custom->stickers.removeAt(removeIndex);
			}
		}
		if (custom->stickers.isEmpty()) {
			sets.erase(customIt);
		}
	}

	if (result.type() == mtpc_messages_stickerSetInstallResultArchive) {
		stickers.applyArchivedResult(
			result.c_messages_stickerSetInstallResultArchive());
	} else {
		auto &storage = _session->local();
		if (wasArchived && type != Data::StickersType::Emoji) {
			if (type == Data::StickersType::Masks) {
				storage.writeArchivedMasks();
			} else {
				storage.writeArchivedStickers();
			}
		}
		if (type == Data::StickersType::Emoji) {
			storage.writeInstalledCustomEmoji();
		} else if (type == Data::StickersType::Masks) {
			storage.writeInstalledMasks();
		} else {
			storage.writeInstalledStickers();
		}
		stickers.notifyUpdated(type);
	}
	_setInstalled.fire_copy(_setId);
}

void StickerSetBox::Inner::mousePressEvent(QMouseEvent *e) {
	if (e->button() != Qt::LeftButton) {
		return;
	}
	const auto index = stickerFromGlobalPos(e->globalPos());
	if (index < 0 || index >= _pack.size()) {
		return;
	}
	_previewTimer.callOnce(QApplication::startDragTime());
}

void StickerSetBox::Inner::mouseMoveEvent(QMouseEvent *e) {
	updateSelected();
	if (_previewShown >= 0) {
		showPreviewAt(e->globalPos());
	}
}

void StickerSetBox::Inner::showPreviewAt(QPoint globalPos) {
	const auto index = stickerFromGlobalPos(globalPos);
	if (index >= 0
		&& index < _pack.size()
		&& index != _previewShown) {
		_previewShown = index;
		_show->showMediaPreview(
			Data::FileOriginStickerSet(_setId, _setAccessHash),
			_pack[_previewShown]);
	}
}

void StickerSetBox::Inner::leaveEventHook(QEvent *e) {
	setSelected(-1);
}

void StickerSetBox::Inner::mouseReleaseEvent(QMouseEvent *e) {
	if (_previewShown >= 0) {
		_previewShown = -1;
		return;
	}
	if (!_previewTimer.isActive()) {
		return;
	}
	_previewTimer.cancel();
	const auto index = stickerFromGlobalPos(e->globalPos());
	if (index < 0 || index >= _pack.size()) {
		return;
	}
	chosen(index, _pack[index], {});
}

void StickerSetBox::Inner::chosen(
		int index,
		not_null<DocumentData*> sticker,
		Api::SendOptions options) {
	const auto animation = options.scheduled
		? Ui::MessageSendingAnimationFrom()
		: messageSentAnimationInfo(index, sticker);
	_show->processChosenSticker({
		.document = sticker,
		.options = options,
		.messageSendingFrom = animation,
	});
}

auto StickerSetBox::Inner::messageSentAnimationInfo(
	int index,
	not_null<DocumentData*> document) const
-> Ui::MessageSendingAnimationFrom {
	if (index < 0 || index >= _pack.size() || _pack[index] != document) {
		return {};
	}
	const auto row = index / _perRow;
	const auto column = index % _perRow;
	const auto left = _padding.left() + column * _singleSize.width();
	const auto top = _padding.top() + row * _singleSize.height();
	const auto rect = QRect(QPoint(left, top), _singleSize);
	const auto size = ChatHelpers::ComputeStickerSize(
		document,
		boundingBoxSize());
	const auto innerPos = QPoint(
		(rect.width() - size.width()) / 2,
		(rect.height() - size.height()) / 2);
	return {
		.type = Ui::MessageSendingAnimationFrom::Type::Sticker,
		.localId = _session->data().nextLocalMessageId(),
		.globalStartGeometry = mapToGlobal(
			QRect(rect.topLeft() + innerPos, size)),
	};
}

void StickerSetBox::Inner::contextMenuEvent(QContextMenuEvent *e) {
	const auto index = stickerFromGlobalPos(e->globalPos());
	if (index < 0
		|| index >= _pack.size()
		|| setType() == Data::StickersType::Masks) {
		return;
	}
	_previewTimer.cancel();
	_menu = base::make_unique_q<Ui::PopupMenu>(
		this,
		st::popupMenuWithIcons);
	const auto type = _show->sendMenuType();
	if (setType() == Data::StickersType::Emoji) {
		if (const auto t = PrepareTextFromEmoji(_pack[index]); !t.empty()) {
			_menu->addAction(tr::lng_mediaview_copy(tr::now), [=] {
				if (auto data = TextUtilities::MimeDataFromText(t)) {
					QGuiApplication::clipboard()->setMimeData(data.release());
				}
			}, &st::menuIconCopy);
		}
	} else if (type != SendMenu::Type::Disabled) {
		const auto document = _pack[index];
		const auto sendSelected = [=](Api::SendOptions options) {
			chosen(index, document, options);
		};
		SendMenu::FillSendMenu(
			_menu.get(),
			type,
			SendMenu::DefaultSilentCallback(sendSelected),
			SendMenu::DefaultScheduleCallback(this, type, sendSelected),
			SendMenu::DefaultWhenOnlineCallback(sendSelected));

		const auto show = _show;
		const auto toggleFavedSticker = [=] {
			Api::ToggleFavedSticker(
				show,
				document,
				Data::FileOriginStickerSet(Data::Stickers::FavedSetId, 0));
		};
		const auto isFaved = document->owner().stickers().isFaved(document);
		_menu->addAction(
			(isFaved
				? tr::lng_faved_stickers_remove
				: tr::lng_faved_stickers_add)(tr::now),
			toggleFavedSticker,
			(isFaved
				? &st::menuIconUnfave
				: &st::menuIconFave));
	}
	if (_menu->empty()) {
		_menu = nullptr;
	} else {
		_menu->popup(QCursor::pos());
	}
}

void StickerSetBox::Inner::updateSelected() {
	auto selected = stickerFromGlobalPos(QCursor::pos());
	setSelected(setType() == Data::StickersType::Masks ? -1 : selected);
}

void StickerSetBox::Inner::setSelected(int selected) {
	if (_selected != selected) {
		startOverAnimation(_selected, 1., 0.);
		_selected = selected;
		startOverAnimation(_selected, 0., 1.);
		setCursor(_selected >= 0 ? style::cur_pointer : style::cur_default);
	}
}

void StickerSetBox::Inner::startOverAnimation(int index, float64 from, float64 to) {
	if (index < 0 || index >= _elements.size()) {
		return;
	}
	_elements[index].overAnimation.start([=] {
		const auto row = index / _perRow;
		const auto column = index % _perRow;
		const auto left = _padding.left() + column * _singleSize.width();
		const auto top = _padding.top() + row * _singleSize.height();
		rtlupdate(left, top, _singleSize.width(), _singleSize.height());
	}, from, to, st::emojiPanDuration);
}

void StickerSetBox::Inner::showPreview() {
	_previewShown = -1;
	showPreviewAt(QCursor::pos());
}

not_null<Lottie::MultiPlayer*> StickerSetBox::Inner::getLottiePlayer() {
	if (!_lottiePlayer) {
		_lottiePlayer = std::make_unique<Lottie::MultiPlayer>(
			Lottie::Quality::Default,
			Lottie::MakeFrameRenderer());
		_lottiePlayer->updates(
		) | rpl::start_with_next([=] {
			updateItems();
		}, lifetime());
	}
	return _lottiePlayer.get();
}

int32 StickerSetBox::Inner::stickerFromGlobalPos(const QPoint &p) const {
	QPoint l(mapFromGlobal(p));
	if (rtl()) l.setX(width() - l.x());
	int32 row = (l.y() >= _padding.top()) ? qFloor((l.y() - _padding.top()) / _singleSize.height()) : -1;
	int32 col = (l.x() >= _padding.left()) ? qFloor((l.x() - _padding.left()) / _singleSize.width()) : -1;
	if (row >= 0 && col >= 0 && col < _perRow) {
		int32 result = row * _perRow + col;
		return (result < _pack.size()) ? result : -1;
	}
	return -1;
}

void StickerSetBox::Inner::paintEvent(QPaintEvent *e) {
	Painter p(this);

	_repaintScheduled = false;

	p.fillRect(e->rect(), st::boxBg);
	if (_elements.empty()) {
		return;
	}

	int32 from = qFloor(e->rect().top() / _singleSize.height()), to = qFloor(e->rect().bottom() / _singleSize.height()) + 1;

	_pathGradient->startFrame(0, width(), width() / 2);

	const auto now = crl::now();
	const auto paused = On(PowerSaving::kStickersPanel)
		|| _show->paused(ChatHelpers::PauseReason::Layer);
	for (int32 i = from; i < to; ++i) {
		for (int32 j = 0; j < _perRow; ++j) {
			int32 index = i * _perRow + j;
			if (index >= _elements.size()) {
				break;
			}
			const auto pos = QPoint(
				_padding.left() + j * _singleSize.width(),
				_padding.top() + i * _singleSize.height());
			paintSticker(p, index, pos, paused, now);
		}
	}

	if (_lottiePlayer && !paused) {
		_lottiePlayer->markFrameShown();
	}
}

bool StickerSetBox::Inner::isEmojiSet() const {
	return (_setFlags & Data::StickersSetFlag::Emoji);
}

uint64 StickerSetBox::Inner::setId() const {
	return _setId;
}

QSize StickerSetBox::Inner::boundingBoxSize() const {
	if (isEmojiSet()) {
		using namespace Data;
		const auto size = FrameSizeFromTag(CustomEmojiSizeTag::Large)
			/ style::DevicePixelRatio();
		return { size, size };
	}
	return QSize(
		_singleSize.width() - st::roundRadiusSmall * 2,
		_singleSize.height() - st::roundRadiusSmall * 2);
}

void StickerSetBox::Inner::visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) {
	if (_visibleTop != visibleTop || _visibleBottom != visibleBottom) {
		_visibleTop = visibleTop;
		_visibleBottom = visibleBottom;
		_lastScrolledAt = crl::now();
		update();
	}
	const auto pauseInRows = [&](int fromRow, int tillRow) {
		Expects(fromRow <= tillRow);

		for (auto i = fromRow; i != tillRow; ++i) {
			for (auto j = 0; j != _perRow; ++j) {
				const auto index = i * _perRow + j;
				if (index >= _elements.size()) {
					break;
				}
				if (const auto lottie = _elements[index].lottie) {
					_lottiePlayer->pause(lottie);
				} else if (auto &webm = _elements[index].webm) {
					webm = nullptr;
				}
			}
		}
	};
	const auto rowsTop = _padding.top();
	const auto singleHeight = _singleSize.height();
	const auto rowsBottom = rowsTop + _rowsCount * singleHeight;
	if (visibleTop >= rowsTop + singleHeight && visibleTop < rowsBottom) {
		const auto pauseHeight = (visibleTop - rowsTop);
		const auto pauseRows = std::min(
			pauseHeight / singleHeight,
			_rowsCount);
		pauseInRows(0, pauseRows);
	}
	if (visibleBottom > rowsTop
		&& visibleBottom + singleHeight <= rowsBottom) {
		const auto pauseHeight = (rowsBottom - visibleBottom);
		const auto pauseRows = std::min(
			pauseHeight / singleHeight,
			_rowsCount);
		pauseInRows(_rowsCount - pauseRows, _rowsCount);
	}
}

void StickerSetBox::Inner::setupLottie(int index) {
	auto &element = _elements[index];

	element.lottie = ChatHelpers::LottieAnimationFromDocument(
		getLottiePlayer(),
		element.documentMedia.get(),
		ChatHelpers::StickerLottieSize::StickerSet,
		boundingBoxSize() * style::DevicePixelRatio());
}

void StickerSetBox::Inner::setupWebm(int index) {
	auto &element = _elements[index];

	const auto document = element.document;
	auto callback = [=](Media::Clip::Notification notification) {
		clipCallback(notification, document, index);
	};
	element.webm = Media::Clip::MakeReader(
		element.documentMedia->owner()->location(),
		element.documentMedia->bytes(),
		std::move(callback));
}

void StickerSetBox::Inner::clipCallback(
		Media::Clip::Notification notification,
		not_null<DocumentData*> document,
		int index) {
	const auto i = (index < _elements.size()
		&& _elements[index].document == document)
		? (_elements.begin() + index)
		: ranges::find(_elements, document, &Element::document);
	if (i == end(_elements)) {
		return;
	}
	using namespace Media::Clip;
	switch (notification) {
	case Notification::Reinit: {
		auto &webm = i->webm;
		if (webm->state() == State::Error) {
			webm.setBad();
		} else if (webm->ready() && !webm->started()) {
			const auto size = ChatHelpers::ComputeStickerSize(
				i->document,
				boundingBoxSize());
			webm->start({ .frame = size, .keepAlpha = true });
		}
	} break;

	case Notification::Repaint: break;
	}

	updateItems();
}

void StickerSetBox::Inner::setupEmoji(int index) {
	auto &element = _elements[index];
	element.emoji = resolveCustomEmoji(element.document);
}

not_null<Ui::Text::CustomEmoji*> StickerSetBox::Inner::resolveCustomEmoji(
		not_null<DocumentData*> document) {
	const auto i = _customEmoji.find(document);
	if (i != end(_customEmoji)) {
		return i->second.get();
	}
	auto emoji = document->session().data().customEmojiManager().create(
		document,
		[=] { customEmojiRepaint(); },
		Data::CustomEmojiManager::SizeTag::Large);
	return _customEmoji.emplace(
		document,
		std::move(emoji)
	).first->second.get();
}

void StickerSetBox::Inner::customEmojiRepaint() {
	if (_repaintScheduled) {
		return;
	}
	_repaintScheduled = true;
	update();
}

void StickerSetBox::Inner::paintSticker(
		Painter &p,
		int index,
		QPoint position,
		bool paused,
		crl::time now) const {
	if (const auto over = _elements[index].overAnimation.value((index == _selected) ? 1. : 0.)) {
		p.setOpacity(over);
		auto tl = position;
		if (rtl()) tl.setX(width() - tl.x() - _singleSize.width());
		Ui::FillRoundRect(p, QRect(tl, _singleSize), st::emojiPanHover, Ui::StickerHoverCorners);
		p.setOpacity(1);
	}

	const auto &element = _elements[index];
	const auto document = element.document;
	const auto &media = element.documentMedia;
	const auto sticker = document->sticker();
	media->checkStickerSmall();

	if (sticker->setType == Data::StickersType::Emoji) {
		const_cast<Inner*>(this)->setupEmoji(index);
	} else if (media->loaded()) {
		if (sticker->isLottie() && !element.lottie) {
			const_cast<Inner*>(this)->setupLottie(index);
		} else if (sticker->isWebm() && !element.webm) {
			const_cast<Inner*>(this)->setupWebm(index);
		}
	}

	const auto premium = document->isPremiumSticker();
	const auto size = ChatHelpers::ComputeStickerSize(
		document,
		boundingBoxSize());
	const auto ppos = position + QPoint(
		(_singleSize.width() - size.width()) / 2,
		(_singleSize.height() - size.height()) / 2);
	auto lottieFrame = QImage();
	if (element.emoji) {
		element.emoji->paint(p, {
			.textColor = st::windowFg->c,
			.now = now,
			.position = ppos,
			.paused = paused,
		});
	} else if (element.lottie && element.lottie->ready()) {
		lottieFrame = element.lottie->frame();
		p.drawImage(
			QRect(ppos, lottieFrame.size() / style::DevicePixelRatio()),
			lottieFrame);

		_lottiePlayer->unpause(element.lottie);
	} else if (element.webm && element.webm->started()) {
		p.drawImage(ppos, element.webm->current({
			.frame = size,
			.keepAlpha = true,
		}, paused ? 0 : now));
	} else if (const auto image = media->getStickerSmall()) {
		const auto pixmap = image->pix(size);
		p.drawPixmapLeft(ppos, width(), pixmap);
		if (premium) {
			lottieFrame = pixmap.toImage().convertToFormat(
				QImage::Format_ARGB32_Premultiplied);
		}
	} else {
		ChatHelpers::PaintStickerThumbnailPath(
			p,
			media.get(),
			QRect(ppos, size),
			_pathGradient.get());
	}
	if (premium) {
		_premiumMark.paint(
			p,
			lottieFrame,
			element.premiumLock,
			position,
			_singleSize,
			width());
	}
}

bool StickerSetBox::Inner::loaded() const {
	return _loaded && !_pack.isEmpty();
}

bool StickerSetBox::Inner::premiumEmojiSet() const {
	return (_setFlags & SetFlag::Emoji)
		&& !_pack.empty()
		&& _pack.front()->isPremiumEmoji();
}

bool StickerSetBox::Inner::notInstalled() const {
	if (!_loaded) {
		return false;
	}
	const auto &sets = _session->data().stickers().sets();
	const auto it = sets.find(_setId);
	if ((it == sets.cend())
		|| !(it->second->flags & SetFlag::Installed)
		|| (it->second->flags & SetFlag::Archived)) {
		return !_pack.empty();
	}
	return false;
}

bool StickerSetBox::Inner::official() const {
	return _loaded && _setShortName.isEmpty();
}

rpl::producer<TextWithEntities> StickerSetBox::Inner::title() const {
	if (!_loaded) {
		return tr::lng_contacts_loading() | Ui::Text::ToWithEntities();
	} else if (_pack.isEmpty()) {
		return tr::lng_attach_failed() | Ui::Text::ToWithEntities();
	}
	auto text = TextWithEntities{ _setTitle };
	TextUtilities::ParseEntities(text, TextParseMentions);
	return rpl::single(text);
}

QString StickerSetBox::Inner::shortName() const {
	return _setShortName;
}

void StickerSetBox::Inner::install() {
	if (_installRequest) {
		return;
	}
	_installRequest = _api.request(MTPmessages_InstallStickerSet(
		Data::InputStickerSet(_input),
		MTP_bool(false)
	)).done([=](const MTPmessages_StickerSetInstallResult &result) {
		installDone(result);
	}).fail([=] {
		_errors.fire(Error::NotFound);
	}).send();
}

void StickerSetBox::Inner::archiveStickers() {
	_api.request(MTPmessages_InstallStickerSet(
		Data::InputStickerSet(_input),
		MTP_boolTrue()
	)).done([=](const MTPmessages_StickerSetInstallResult &result) {
		if (result.type() == mtpc_messages_stickerSetInstallResultSuccess) {
			_setArchived.fire_copy(_setId);
		}
	}).fail([=] {
		_show->showToast(Lang::Hard::ServerError());
	}).send();
}

void StickerSetBox::Inner::updateItems() {
	const auto now = crl::now();

	const auto delay = std::max(
		_lastScrolledAt + kMinAfterScrollDelay - now,
		_lastUpdatedAt + kMinRepaintDelay - now);
	if (delay <= 0) {
		repaintItems(now);
	} else if (!_updateItemsTimer.isActive()
		|| _updateItemsTimer.remainingTime() > kMinRepaintDelay) {
		_updateItemsTimer.callOnce(std::max(delay, kMinRepaintDelay));
	}
}

void StickerSetBox::Inner::repaintItems(crl::time now) {
	_lastUpdatedAt = now ? now : crl::now();
	update();
}

StickerSetBox::Inner::~Inner() = default;
