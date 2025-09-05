/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/chat/choose_theme_controller.h"

#include "boxes/background_box.h"
#include "boxes/transfer_gift_box.h"
#include "ui/dynamic_image.h"
#include "ui/dynamic_thumbnails.h"
#include "ui/rp_widget.h"
#include "ui/boxes/confirm_box.h"
#include "ui/chat/chat_theme.h"
#include "ui/chat/message_bubble.h"
#include "ui/layers/generic_box.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/shadow.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/buttons.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/painter.h"
#include "main/main_session.h"
#include "window/window_session_controller.h"
#include "window/themes/window_theme.h"
#include "data/stickers/data_custom_emoji.h"
#include "data/data_session.h"
#include "data/data_peer.h"
#include "data/data_cloud_themes.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "lang/lang_keys.h"
#include "apiwrap.h"
#include "styles/style_widgets.h"
#include "styles/style_layers.h" // boxTitle.
#include "styles/style_settings.h"
#include "styles/style_window.h"

#include <QtWidgets/QApplication>

namespace Ui {
namespace {

const auto kDisableElement = [] { return u"disable"_q; };

struct Preview {
	QImage preview;
	QRect userpic;
};

[[nodiscard]] Preview GeneratePreview(
		not_null<Ui::ChatTheme*> theme,
		const std::shared_ptr<Ui::DynamicImage> &takenUserpic) {
	const auto &background = theme->background();
	const auto &colors = background.colors;
	const auto size = st::chatThemePreviewSize;
	auto prepared = background.prepared;
	const auto paintPattern = [&](QPainter &p, bool inverted) {
		if (prepared.isNull()) {
			return;
		}
		const auto w = prepared.width();
		const auto h = prepared.height();
		const auto scaled = size.scaled(
			st::windowMinWidth / 2,
			st::windowMinHeight / 2,
			Qt::KeepAspectRatio);
		const auto use = (scaled.width() > w || scaled.height() > h)
			? scaled.scaled({ w, h }, Qt::KeepAspectRatio)
			: scaled;
		const auto good = QSize(
			std::max(use.width(), 1),
			std::max(use.height(), 1));
		auto small = prepared.copy(QRect(
			QPoint(
				(w - good.width()) / 2,
				(h - good.height()) / 2),
			good));
		if (inverted) {
			small = Ui::InvertPatternImage(std::move(small));
		}
		p.drawImage(
			QRect(QPoint(), size * style::DevicePixelRatio()),
			small);
	};
	auto userpic = QRect();
	const auto fullsize = size * style::DevicePixelRatio();
	auto result = background.waitingForNegativePattern()
		? QImage(
			fullsize,
			QImage::Format_ARGB32_Premultiplied)
		: Ui::GenerateBackgroundImage(
			fullsize,
			colors.empty() ? std::vector{ 1, QColor(0, 0, 0) } : colors,
			background.gradientRotation,
			background.patternOpacity,
			paintPattern);
	if (background.waitingForNegativePattern()) {
		result.fill(Qt::black);
	}
	result.setDevicePixelRatio(style::DevicePixelRatio());
	{
		auto p = QPainter(&result);
		const auto sent = QRect(
			QPoint(
				(size.width()
					- st::chatThemeBubbleSize.width()
					- st::chatThemeBubblePosition.x()),
				st::chatThemeBubblePosition.y()),
			st::chatThemeBubbleSize);
		const auto received = QRect(
			st::chatThemeBubblePosition.x(),
			sent.y() + sent.height() + st::chatThemeBubbleSkip,
			sent.width(),
			sent.height());
		const auto radius = st::chatThemeBubbleRadius;

		PainterHighQualityEnabler hq(p);
		p.setPen(Qt::NoPen);
		if (const auto pattern = theme->bubblesBackgroundPattern()) {
			auto bubble = pattern->pixmap.toImage().scaled(
				sent.size() * style::DevicePixelRatio(),
				Qt::IgnoreAspectRatio,
				Qt::SmoothTransformation
			).convertToFormat(QImage::Format_ARGB32_Premultiplied);
			const auto corners = Images::CornersMask(radius);
			p.drawImage(sent, Images::Round(std::move(bubble), corners));
		} else {
			p.setBrush(theme->palette()->msgOutBg()->c);
			p.drawRoundedRect(sent, radius, radius);
		}
		p.setBrush(theme->palette()->msgInBg()->c);
		p.drawRoundedRect(received, radius, radius);

		if (takenUserpic) {
			const auto border = 2 * st::lineWidth;
			const auto inner = received.marginsRemoved(
				{ border, border, border, border });
			userpic = inner;
			userpic.setWidth(userpic.height());

			st::chatThemeGiftTaken.paintInCenter(
				p,
				QRect(
					inner.x() + inner.width() - inner.height() - border,
					inner.y(),
					inner.height(),
					inner.height()),
				theme->palette()->msgFileInBg()->c);
		}
	}
	return {
		.preview = Images::Round(std::move(result), ImageRoundRadius::Large),
		.userpic = userpic,
	};
}

[[nodiscard]] QImage GenerateEmptyPreview() {
	auto result = QImage(
		st::chatThemePreviewSize * style::DevicePixelRatio(),
		QImage::Format_ARGB32_Premultiplied);
	result.fill(st::settingsThemeNotSupportedBg->c);
	result.setDevicePixelRatio(style::DevicePixelRatio());
	{
		auto p = QPainter(&result);
		p.setPen(st::menuIconFg);
		p.setFont(st::semiboldFont);
		const auto top = st::chatThemeEmptyPreviewTop;
		const auto width = st::chatThemePreviewSize.width();
		const auto height = st::chatThemePreviewSize.height() - top;
		p.drawText(
			QRect(0, top, width, height),
			tr::lng_chat_theme_none(tr::now),
			style::al_top);
	}
	return Images::Round(std::move(result), ImageRoundRadius::Large);
}

} // namespace

struct ChooseThemeController::Entry {
	QString token;
	Ui::ChatThemeKey key;
	std::shared_ptr<Ui::ChatTheme> theme;
	std::shared_ptr<Data::DocumentMedia> media;
	std::shared_ptr<Data::UniqueGift> gift;
	std::shared_ptr<Ui::DynamicImage> takenUserpic;
	std::unique_ptr<Ui::Text::CustomEmoji> custom;
	EmojiPtr emoji = nullptr;
	QImage preview;
	QRect userpic;
	QRect geometry;
	bool chosen = false;
};

ChooseThemeController::ChooseThemeController(
	not_null<RpWidget*> parent,
	not_null<Window::SessionController*> window,
	not_null<PeerData*> peer)
: _controller(window)
, _peer(peer)
, _wrap(std::make_unique<VerticalLayout>(parent))
, _topShadow(std::make_unique<PlainShadow>(parent))
, _content(_wrap->add(object_ptr<RpWidget>(_wrap.get())))
, _inner(CreateChild<RpWidget>(_content.get()))
, _disabledEmoji(Ui::Emoji::Find(QString::fromUtf8("\xe2\x9d\x8c")))
, _dark(Window::Theme::IsThemeDarkValue()) {
	init(parent->sizeValue());
}

ChooseThemeController::~ChooseThemeController() {
	_controller->clearPeerThemeOverride(_peer);
}

void ChooseThemeController::init(rpl::producer<QSize> outer) {
	using namespace rpl::mappers;

	const auto themes = &_controller->session().data().cloudThemes();
	if (themes->myGiftThemesTokens().empty()) {
		themes->myGiftThemesLoadMore();
	}

	const auto &list = themes->chatThemes();
	if (!list.empty()) {
		fill(list);
	} else {
		themes->refreshChatThemes();
		themes->chatThemesUpdated(
		) | rpl::take(1) | rpl::start_with_next([=] {
			fill(themes->chatThemes());
		}, lifetime());
	}

	const auto titleWrap = _wrap->insert(
		0,
		object_ptr<FixedHeightWidget>(
			_wrap.get(),
			st::boxTitle.style.font->height),
		st::chatThemeTitlePadding);
	auto title = CreateChild<FlatLabel>(
		titleWrap,
		tr::lng_chat_theme_title(),
		st::boxTitle);
	_wrap->paintRequest(
	) | rpl::start_with_next([=](QRect clip) {
		QPainter(_wrap.get()).fillRect(clip, st::windowBg);
	}, lifetime());

	const auto close = Ui::CreateChild<Ui::IconButton>(
		_wrap.get(),
		st::boxTitleClose);
	close->setClickedCallback([=] { this->close(); });
	rpl::combine(
		_wrap->widthValue(),
		titleWrap->positionValue()
	) | rpl::start_with_next([=](int width, QPoint position) {
		close->moveToRight(0, 0, width);
	}, close->lifetime());

	initButtons();
	initList();

	_inner->positionValue(
	) | rpl::start_with_next([=](QPoint position) {
		title->move(std::max(position.x(), 0), 0);
	}, title->lifetime());

	std::move(
		outer
	) | rpl::start_with_next([=](QSize outer) {
		_wrap->resizeToWidth(outer.width());
		_wrap->move(0, outer.height() - _wrap->height());
		const auto line = st::lineWidth;
		_topShadow->setGeometry(0, _wrap->y() - line, outer.width(), line);
	}, lifetime());

	rpl::combine(
		_shouldBeShown.value(),
		_forceHidden.value(),
		_1 && !_2
	) | rpl::start_with_next([=](bool shown) {
		_wrap->setVisible(shown);
		_topShadow->setVisible(shown);
	}, lifetime());
}

void ChooseThemeController::initButtons() {
	const auto controls = _wrap->add(object_ptr<RpWidget>(_wrap.get()));
	const auto apply = CreateChild<RoundButton>(
		controls,
		tr::lng_chat_theme_apply(),
		st::defaultLightButton);
	apply->setTextTransform(Ui::RoundButton::TextTransform::NoTransform);
	const auto choose = CreateChild<RoundButton>(
		controls,
		tr::lng_chat_theme_change_wallpaper(),
		st::defaultLightButton);
	choose->setTextTransform(Ui::RoundButton::TextTransform::NoTransform);

	const auto &margin = st::chatThemeButtonMargin;
	controls->resize(
		margin.left() + choose->width() + margin.right(),
		margin.top() + choose->height() + margin.bottom());
	rpl::combine(
		controls->widthValue(),
		apply->widthValue(),
		choose->widthValue(),
		_chosen.value()
	) | rpl::start_with_next([=](
			int outer,
			int applyWidth,
			int chooseWidth,
			QString chosen) {
		const auto was = _peer->themeToken();
		const auto now = (chosen == kDisableElement()) ? QString() : chosen;
		const auto changed = (now != was);
		apply->setVisible(changed);
		choose->setVisible(!changed);
		const auto shown = changed ? apply : choose;
		const auto shownWidth = changed ? applyWidth : chooseWidth;
		const auto inner = margin.left() + shownWidth + margin.right();
		const auto left = (outer - inner) / 2;
		shown->moveToLeft(left, margin.top());
	}, controls->lifetime());

	const auto setTheme = crl::guard(apply, [=](
			const QString &token,
			const std::shared_ptr<Ui::ChatTheme> &theme) {
		SetPeerTheme(_controller, _peer, token, theme);
		_controller->toggleChooseChatTheme(_peer);
	});
	const auto confirmTakeGiftTheme = crl::guard(apply, [=](
			const QString &token,
			const std::shared_ptr<Ui::ChatTheme> &theme,
			not_null<PeerData*> nowHasTheme) {
		_controller->show(Box([=](not_null<Ui::GenericBox*> box) {
			const auto confirmed = [=](Fn<void()> close) {
				setTheme(token, theme);
				close();
			};
			Ui::ConfirmBox(box, {
				.text = tr::lng_chat_theme_gift_replace(
					lt_name,
					rpl::single(Ui::Text::Bold(nowHasTheme->shortName())),
					Ui::Text::WithEntities),
				.confirmed = confirmed,
				.confirmText = tr::lng_box_yes(),
			});
		}));
	});
	apply->setClickedCallback([=] {
		if (const auto chosen = findChosen()) {
			const auto was = _peer->themeToken();
			const auto now = chosen->key ? _chosen.current() : QString();
			const auto user = chosen->gift
				? chosen->gift->themeUser
				: nullptr;
			if (was != now) {
				if (!user || user == _peer) {
					setTheme(now, chosen->theme);
				} else {
					confirmTakeGiftTheme(now, chosen->theme, user);
				}
			} else {
				_controller->toggleChooseChatTheme(_peer);
			}
		} else {
			_controller->toggleChooseChatTheme(_peer);
		}
	});
	choose->setClickedCallback([=] {
		_controller->show(Box<BackgroundBox>(_controller, _peer));
	});
}

void ChooseThemeController::paintEntry(QPainter &p, const Entry &entry) {
	const auto geometry = entry.geometry;
	p.drawImage(geometry, entry.preview);
	if (const auto userpic = entry.takenUserpic.get()) {
		userpic->subscribeToUpdates([=] {
			_inner->update();
		});
		p.drawImage(
			entry.userpic.translated(geometry.topLeft()),
			userpic->image(entry.userpic.height()));
	}

	const auto size = Ui::Emoji::GetSizeLarge();
	const auto factor = style::DevicePixelRatio();
	const auto esize = size / factor;
	const auto emojiLeft = geometry.x() + (geometry.width() - esize) / 2;
	const auto emojiTop = geometry.y()
		+ geometry.height()
		- esize
		- st::chatThemeEmojiBottom;
	const auto customSize = Ui::Text::AdjustCustomEmojiSize(esize);
	const auto customSkip = (esize - customSize) / 2;

	if (const auto emoji = entry.emoji) {
		Ui::Emoji::Draw(p, emoji, size, emojiLeft, emojiTop);
	} else if (const auto custom = entry.custom.get()) {
		custom->paint(p, {
			.textColor = st::windowFg->c,
			.position = { emojiLeft + customSkip, emojiTop + customSkip },
		});
	}

	if (entry.chosen) {
		auto hq = PainterHighQualityEnabler(p);
		auto pen = st::activeLineFg->p;
		const auto width = st::defaultInputField.borderActive;
		pen.setWidth(width);
		p.setPen(pen);
		const auto add = st::lineWidth + width;
		p.drawRoundedRect(
			entry.geometry.marginsAdded({ add, add, add, add }),
			st::roundRadiusLarge + add,
			st::roundRadiusLarge + add);
	}
}

void ChooseThemeController::initList() {
	_content->resize(
		_content->width(),
		(st::chatThemeEntryMargin.top()
			+ st::chatThemePreviewSize.height()
			+ st::chatThemeEntryMargin.bottom()));
	_inner->setMouseTracking(true);

	_inner->paintRequest(
	) | rpl::start_with_next([=](QRect clip) {
		auto p = QPainter(_inner.get());
		for (const auto &entry : _entries) {
			if (entry.preview.isNull() || !clip.intersects(entry.geometry)) {
				continue;
			}
			paintEntry(p, entry);
		}
	}, lifetime());
	const auto byPoint = [=](QPoint position) -> Entry* {
		for (auto &entry : _entries) {
			if (entry.geometry.contains(position)) {
				return &entry;
			}
		}
		return nullptr;
	};
	const auto chosenText = [=](const Entry *entry) {
		if (!entry) {
			return QString();
		} else if (entry->key) {
			return entry->token;
		} else {
			return kDisableElement();
		}
	};
	_inner->events(
	) | rpl::start_with_next([=](not_null<QEvent*> event) {
		const auto type = event->type();
		if (type == QEvent::MouseMove) {
			const auto mouse = static_cast<QMouseEvent*>(event.get());
			const auto skip = _inner->width() - _content->width();
			if (skip <= 0) {
				_dragStartPosition = _pressPosition = std::nullopt;
			} else if (_pressPosition.has_value()
				&& ((mouse->globalPos() - *_pressPosition).manhattanLength()
					>= QApplication::startDragDistance())) {
				_dragStartPosition = base::take(_pressPosition);
				_dragStartInnerLeft = _inner->x();
			}
			if (_dragStartPosition.has_value()) {
				const auto shift = mouse->globalPos().x()
					- _dragStartPosition->x();
				updateInnerLeft(_dragStartInnerLeft + shift);
			} else {
				_inner->setCursor(byPoint(mouse->pos())
					? style::cur_pointer
					: style::cur_default);
			}
		} else if (type == QEvent::MouseButtonPress) {
			const auto mouse = static_cast<QMouseEvent*>(event.get());
			if (mouse->button() == Qt::LeftButton) {
				_pressPosition = mouse->globalPos();
			}
			_pressed = chosenText(byPoint(mouse->pos()));
		} else if (type == QEvent::MouseButtonRelease) {
			_pressPosition = _dragStartPosition = std::nullopt;
			const auto mouse = static_cast<QMouseEvent*>(event.get());
			const auto entry = byPoint(mouse->pos());
			const auto chosen = chosenText(entry);
			if (entry && chosen == _pressed && chosen != _chosen.current()) {
				clearCurrentBackgroundState();
				if (const auto was = findChosen()) {
					was->chosen = false;
				}
				_chosen = chosen;
				entry->chosen = true;
				if (entry->theme || !entry->key) {
					_controller->overridePeerTheme(
						_peer,
						entry->theme,
						entry->token);
				}
				_inner->update();
			}
			_pressed = QString();
		} else if (type == QEvent::Wheel) {
			const auto wheel = static_cast<QWheelEvent*>(event.get());
			const auto was = _inner->x();
			updateInnerLeft((wheel->angleDelta().x() != 0)
				? (was + (wheel->pixelDelta().x()
					? wheel->pixelDelta().x()
					: wheel->angleDelta().x()))
				: (wheel->angleDelta().y() != 0)
				? (was + (wheel->pixelDelta().y()
					? wheel->pixelDelta().y()
					: wheel->angleDelta().y()))
				: was);
		}
	}, lifetime());

	_content->events(
	) | rpl::start_with_next([=](not_null<QEvent*> event) {
		const auto type = event->type();
		if (type == QEvent::KeyPress) {
			const auto key = static_cast<QKeyEvent*>(event.get());
			if (key->key() == Qt::Key_Escape) {
				close();
			}
		}
	}, lifetime());

	rpl::combine(
		_content->widthValue(),
		_inner->widthValue()
	) | rpl::start_with_next([=](int content, int inner) {
		if (!content || !inner) {
			return;
		} else if (!_entries.empty() && !_initialInnerLeftApplied) {
			applyInitialInnerLeft();
		} else {
			updateInnerLeft(_inner->x());
		}
	}, lifetime());
}

void ChooseThemeController::applyInitialInnerLeft() {
	if (const auto chosen = findChosen()) {
		updateInnerLeft(
			_content->width() / 2 - chosen->geometry.center().x());
	}
	_initialInnerLeftApplied = true;
}

void ChooseThemeController::updateInnerLeft(int now) {
	const auto skip = _content->width() - _inner->width();
	const auto clamped = (skip >= 0)
		? (skip / 2)
		: std::clamp(now, skip, 0);
	_inner->move(clamped, 0);
	const auto visibleTill = -clamped + _content->width();
	if (_giftsFinishAt - visibleTill < _content->width()) {
		_peer->owner().cloudThemes().myGiftThemesLoadMore();
	}
}

void ChooseThemeController::close() {
	if (const auto chosen = findChosen()) {
		if (_peer->themeToken() != chosen->token) {
			clearCurrentBackgroundState();
		}
	}
	_controller->toggleChooseChatTheme(_peer);
}

void ChooseThemeController::clearCurrentBackgroundState() {
	if (const auto entry = findChosen()) {
		if (entry->theme) {
			entry->theme->clearBackgroundState();
		}
	}
}

auto ChooseThemeController::findChosen() -> Entry* {
	const auto chosen = _chosen.current();
	if (chosen.isEmpty()) {
		return nullptr;
	}
	for (auto &entry : _entries) {
		if (!entry.key && chosen == kDisableElement()) {
			return &entry;
		} else if (chosen == entry.token) {
			return &entry;
		}
	}
	return nullptr;
}

auto ChooseThemeController::findChosen() const -> const Entry* {
	return const_cast<ChooseThemeController*>(this)->findChosen();
}

void ChooseThemeController::fill(
		const std::vector<Data::CloudTheme> &themes) {
	if (themes.empty()) {
		return;
	}
	const auto single = st::chatThemePreviewSize;
	const auto skip = st::chatThemeEntrySkip;
	const auto &margin = st::chatThemeEntryMargin;
	const auto initial = _peer->themeToken();
	if (initial.isEmpty()) {
		_chosen = kDisableElement();
	}

	const auto cloudThemes = &_controller->session().data().cloudThemes();
	rpl::combine(
		_dark.value(),
		rpl::single(
			rpl::empty
		) | rpl::then(cloudThemes->myGiftThemesUpdated())
	) | rpl::start_with_next([=](bool dark, auto) {
		if (!cloudThemes->myGiftThemesReady()) {
			return;
		}
		clearCurrentBackgroundState();
		if (_chosen.current().isEmpty() && !initial.isEmpty()) {
			_chosen = initial;
		}

		_cachingLifetime.destroy();
		const auto old = base::take(_entries);
		auto x = margin.left();
		_entries.push_back({
			.emoji = _disabledEmoji,
			.preview = GenerateEmptyPreview(),
			.geometry = QRect(QPoint(x, margin.top()), single),
			.chosen = (_chosen.current() == kDisableElement()),
		});
		Assert(_entries.front().emoji != nullptr);
		style::PaletteChanged(
		) | rpl::start_with_next([=] {
			_entries.front().preview = GenerateEmptyPreview();
		}, _cachingLifetime);

		const auto type = dark
			? Data::CloudThemeType::Dark
			: Data::CloudThemeType::Light;

		x += single.width() + skip;

		const auto owner = &_controller->session().data();
		const auto manager = &owner->customEmojiManager();
		const auto push = [&](
				const Data::CloudTheme &theme,
				const QString &token) {
			if (token.isEmpty() || !theme.settings.contains(type)) {
				return;
			}
			const auto key = ChatThemeKey{ theme.id, dark };
			const auto isChosen = (_chosen.current() == token);
			const auto themeUser = theme.unique
				? theme.unique->themeUser
				: nullptr;
			_entries.push_back({
				.token = token,
				.key = key,
				.gift = theme.unique,
				.takenUserpic = (themeUser
					? Ui::MakeUserpicThumbnail(themeUser, true)
					: nullptr),
				.custom = (theme.unique
					? manager->create(
						theme.unique->model.document,
						[=] { _inner->update(); },
						Data::CustomEmojiSizeTag::Large)
					: nullptr),
				.emoji = (theme.emoticon.isEmpty()
					? nullptr
					: Ui::Emoji::Find(theme.emoticon)),
				.geometry = QRect(QPoint(x, skip), single),
				.chosen = isChosen,
			});
			_controller->cachedChatThemeValue(
				theme,
				Data::WallPaper(0),
				type
			) | rpl::filter([=](const std::shared_ptr<ChatTheme> &data) {
				return data && (data->key() == key);
			}) | rpl::take(
				1
			) | rpl::start_with_next([=](std::shared_ptr<ChatTheme> &&data) {
				const auto key = data->key();
				const auto i = ranges::find(_entries, key, &Entry::key);
				if (i == end(_entries)) {
					return;
				}
				const auto theme = data.get();
				const auto token = i->token;
				i->theme = std::move(data);
				auto generated = GeneratePreview(theme, i->takenUserpic);
				i->preview = std::move(generated.preview);
				i->userpic = generated.userpic;
				if (_chosen.current() == token) {
					_controller->overridePeerTheme(_peer, i->theme, token);
				}
				_inner->update();

				if (!theme->background().isPattern
					|| !theme->background().prepared.isNull()) {
					return;
				}
				// Subscribe to pattern loading if needed.
				theme->repaintBackgroundRequests(
				) | rpl::filter([=] {
					const auto i = ranges::find(
						_entries,
						key,
						&Entry::key);
					return (i == end(_entries))
						|| !i->theme->background().prepared.isNull();
				}) | rpl::take(1) | rpl::start_with_next([=] {
					const auto i = ranges::find(
						_entries,
						key,
						&Entry::key);
					if (i == end(_entries)) {
						return;
					}
					auto generated = GeneratePreview(theme, i->takenUserpic);
					i->preview = std::move(generated.preview);
					i->userpic = generated.userpic;
					_inner->update();
				}, _cachingLifetime);
			}, _cachingLifetime);
			x += single.width() + skip;
		};

		_giftsFinishAt = 0;
		if (const auto now = cloudThemes->themeForToken(initial)) {
			push(*now, initial);
		}
		for (const auto &token : cloudThemes->myGiftThemesTokens()) {
			if (const auto found = cloudThemes->themeForToken(token)) {
				if (token != initial) {
					push(*found, token);
					_giftsFinishAt = x;
				}
			}
		}
		for (const auto &theme : themes) {
			if (const auto emoji = Ui::Emoji::Find(theme.emoticon)) {
				const auto token = emoji->text();
				if (token != initial) {
					push(theme, token);
				}
			}
		}

		const auto full = x - skip + margin.right();
		_inner->resize(
			full,
			margin.top() + single.height() + margin.bottom());

		if (!_initialInnerLeftApplied && _content->width() > 0) {
			applyInitialInnerLeft();
		}
	}, lifetime());
	_shouldBeShown = true;
}

bool ChooseThemeController::shouldBeShown() const {
	return _shouldBeShown.current();
}

rpl::producer<bool> ChooseThemeController::shouldBeShownValue() const {
	return _shouldBeShown.value();
}

int ChooseThemeController::height() const {
	return shouldBeShown() ? _wrap->height() : 0;
}

void ChooseThemeController::hide() {
	_forceHidden = true;
}

void ChooseThemeController::show() {
	_forceHidden = false;
}

void ChooseThemeController::raise() {
	_wrap->raise();
	_topShadow->raise();
}

void ChooseThemeController::setFocus() {
	_content->setFocus();
}

rpl::lifetime &ChooseThemeController::lifetime() {
	return _wrap->lifetime();
}

} // namespace Ui
