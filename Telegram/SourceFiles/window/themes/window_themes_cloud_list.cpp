/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "window/themes/window_themes_cloud_list.h"

#include "base/call_delayed.h"
#include "ui/chat/choose_theme_controller.h"
#include "ui/emoji_config.h"
#include "window/themes/window_themes_embedded.h"
#include "window/themes/window_theme_editor_box.h"
#include "window/themes/window_themes_chat.h"
#include "window/themes/window_theme.h"
#include "window/window_session_controller.h"
#include "window/window_controller.h"
#include "data/data_cloud_themes.h"
#include "data/data_file_origin.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_session.h"
#include "ui/chat/chat_theme.h"
#include "ui/image/image_prepare.h"
#include "ui/rect.h"
#include "ui/widgets/popup_menu.h"
#include "ui/toast/toast.h"
#include "ui/style/style_palette_colorizer.h"
#include "ui/boxes/confirm_box.h"
#include "ui/painter.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "core/application.h"
#include "styles/style_settings.h"
#include "styles/style_boxes.h"
#include "styles/style_chat.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_menu_icons.h"

#include <QtGui/QGuiApplication>
#include <QtGui/QClipboard>

namespace Window {
namespace Theme {
namespace {

constexpr auto kFakeCloudThemeId = 0xFFFFFFFFFFFFFFFAULL;
constexpr auto kShowPerRow = 4;

[[nodiscard]] Data::CloudTheme FakeCloudTheme(const Object &object) {
	auto result = Data::CloudTheme();
	result.id = result.documentId = kFakeCloudThemeId;
	result.slug = object.pathAbsolute;
	return result;
}

[[nodiscard]] bool ContainsThemeWithEmoticon(
		const std::vector<Data::CloudTheme> &list,
		const QString &emoticon) {
	const auto emoji = Ui::Emoji::Find(emoticon);
	if (!emoji) {
		return false;
	}
	return ranges::contains(list, emoji, [](const Data::CloudTheme &theme) {
		return Ui::Emoji::Find(theme.emoticon);
	});
}

[[nodiscard]] QImage ColorsBackgroundFromImage(const QImage &source) {
	if (source.isNull()) {
		return source;
	}
	const auto from = source.size();
	const auto to = st::settingsThemePreviewSize * style::DevicePixelRatio();
	if (to.width() * from.height() > to.height() * from.width()) {
		const auto small = (from.width() > to.width())
			? source.scaledToWidth(to.width(), Qt::SmoothTransformation)
			: source;
		const auto takew = small.width();
		const auto takeh = std::max(
			takew * to.height() / to.width(),
			1);
		return (small.height() != takeh)
			? small.copy(0, (small.height() - takeh) / 2, takew, takeh)
			: small;
	} else {
		const auto small = (from.height() > to.height())
			? source.scaledToHeight(to.height(), Qt::SmoothTransformation)
			: source;
		const auto takeh = small.height();
		const auto takew = std::max(
			takeh * to.width() / to.height(),
			1);
		return (small.width() != takew)
			? small.copy((small.width() - takew) / 2, 0, takew, takeh)
			: small;
	}
}

[[nodiscard]] std::optional<CloudListColors> ColorsFromTheme(
		const QString &path,
		const QByteArray &theme) {
	const auto content = [&] {
		if (!theme.isEmpty()) {
			return theme;
		}
		auto file = QFile(path);
		return file.open(QIODevice::ReadOnly)
			? file.readAll()
			: QByteArray();
	}();
	if (content.isEmpty()) {
		return std::nullopt;
	}
	auto instance = Instance();
	if (!LoadFromContent(content, &instance, nullptr)) {
		return std::nullopt;
	}
	auto result = CloudListColors();
	result.background = ColorsBackgroundFromImage(instance.background);
	result.sent = st::msgOutBg[instance.palette]->c;
	result.received = st::msgInBg[instance.palette]->c;
	result.radiobuttonActive
		= result.radiobuttonInactive
		= st::msgServiceFg[instance.palette]->c;
	return result;
}

[[nodiscard]] CloudListColors ColorsFromCurrentTheme() {
	auto result = CloudListColors();
	auto background = Background()->createCurrentImage();
	result.background = ColorsBackgroundFromImage(background);
	result.sent = st::msgOutBg->c;
	result.received = st::msgInBg->c;
	result.radiobuttonActive
		= result.radiobuttonInactive
		= st::activeButtonBg->c;
	return result;
}

} // namespace

CloudListColors ColorsFromScheme(const EmbeddedScheme &scheme) {
	auto result = CloudListColors();
	result.sent = scheme.sent;
	result.received = scheme.received;
	result.radiobuttonActive
		= result.radiobuttonInactive
		= scheme.accentColor;
	result.background = QImage(
		QSize(1, 1) * style::DevicePixelRatio(),
		QImage::Format_ARGB32_Premultiplied);
	result.background.fill(scheme.background);
	return result;
}

CloudListColors ColorsFromScheme(
		const EmbeddedScheme &scheme,
		const style::colorizer &colorizer) {
	if (!colorizer) {
		return ColorsFromScheme(scheme);
	}
	auto copy = scheme;
	Colorize(copy, colorizer);
	if (const auto accent = style::colorize(copy.accentColor, colorizer)) {
		copy.accentColor = *accent;
	}
	return ColorsFromScheme(copy);
}

CloudListCheck::CloudListCheck(const Colors &colors, bool checked)
: CloudListCheck(checked) {
	setColors(colors);
}

CloudListCheck::CloudListCheck(bool checked)
: AbstractCheckView(st::defaultRadio.duration, checked, nullptr) {
}

void CloudListCheck::setPreview(QImage preview, const QColor &outline) {
	_preview = std::move(preview);
	_outline = outline;
	update();
}

void CloudListCheck::setColors(const Colors &colors) {
	_colors = colors;
	if (!_colors->background.isNull()) {
		const auto size = st::settingsThemePreviewSize
			* style::DevicePixelRatio();
		_backgroundFull = (_colors->background.size() == size)
			? _colors->background
			: _colors->background.scaled(
				size,
				Qt::IgnoreAspectRatio,
				Qt::SmoothTransformation);
		_backgroundCacheWidth = -1;

	}
	update();
}

QSize CloudListCheck::getSize() const {
	return st::settingsThemePreviewSize;
}

void CloudListCheck::validateBackgroundCache(int width) {
	if (_backgroundCacheWidth == width || width <= 0) {
		return;
	}
	_backgroundCacheWidth = width;
	const auto skip = st::settingsThemeOutlineSkip;
	const auto imageWidth = width * style::DevicePixelRatio();
	const auto imageHeight = (st::settingsThemePreviewSize.height()
		- 2 * skip) * style::DevicePixelRatio();
	_backgroundCache = _backgroundFull.copy(
		(_backgroundFull.width() - imageWidth) / 2,
		(_backgroundFull.height() - imageHeight) / 2,
		imageWidth,
		imageHeight);
	_backgroundCache = Images::Round(
		std::move(_backgroundCache),
		ImageRoundRadius::Large);
	_backgroundCache.setDevicePixelRatio(style::DevicePixelRatio());
}

void CloudListCheck::paint(QPainter &p, int left, int top, int outerWidth) {
	if (!_preview.isNull()) {
		const auto skip = st::settingsThemeOutlineSkip;
		const auto card = QRect(
			0,
			0,
			outerWidth,
			st::settingsThemePreviewSize.height()
		) - Margins(skip);
		p.drawImage(card, _preview);
		if (_colors) {
			_colors->radiobuttonActive = _outline;
		} else {
			auto colors = Colors();
			colors.radiobuttonActive = _outline;
			_colors = std::move(colors);
		}
		auto hq = PainterHighQualityEnabler(p);
		paintOutline(p, outerWidth);
		return;
	}
	if (!_colors) {
		return;
	} else if (_colors->background.isNull()) {
		paintNotSupported(p, left, top, outerWidth);
	} else {
		paintWithColors(p, left, top, outerWidth);
	}
}

void CloudListCheck::paintNotSupported(
		QPainter &p,
		int left,
		int top,
		int outerWidth) {
	PainterHighQualityEnabler hq(p);
	p.setPen(Qt::NoPen);
	p.setBrush(st::settingsThemeNotSupportedBg);

	const auto skip = st::settingsThemeOutlineSkip;
	const auto height = st::settingsThemePreviewSize.height();
	const auto rect = QRect(0, 0, outerWidth, height) - Margins(skip);
	const auto radius = st::roundRadiusLarge;
	p.drawRoundedRect(rect, radius, radius);
	st::settingsThemeNotSupportedIcon.paintInCenter(p, rect);
	paintOutline(p, outerWidth);
}

void CloudListCheck::paintWithColors(
		QPainter &p,
		int left,
		int top,
		int outerWidth) {
	Expects(_colors.has_value());

	auto hq = PainterHighQualityEnabler(p);

	const auto skip = st::settingsThemeOutlineSkip;
	const auto card = QRect(
		0,
		0,
		outerWidth,
		st::settingsThemePreviewSize.height()
	) - Margins(skip);
	validateBackgroundCache(card.width());
	p.drawImage(card, _backgroundCache);

	const auto received = QRect(
		card.topLeft() + st::settingsThemeBubblePosition,
		st::settingsThemeBubbleSize);
	const auto sent = QRect(
		card.x() + card.width()
			- received.width()
			- st::settingsThemeBubblePosition.x(),
		received.y() + received.height() + st::settingsThemeBubbleSkip,
		received.width(),
		received.height());
	const auto radius = st::settingsThemeBubbleRadius;

	p.setPen(Qt::NoPen);
	p.setBrush(_colors->received);
	p.drawRoundedRect(style::rtlrect(received, outerWidth), radius, radius);
	p.setBrush(_colors->sent);
	p.drawRoundedRect(style::rtlrect(sent, outerWidth), radius, radius);

	paintOutline(p, outerWidth);
}

void CloudListCheck::paintOutline(QPainter &p, int outerWidth) {
	const auto toggled = currentAnimationValue();
	if (toggled <= 0.) {
		return;
	}
	const auto width = float64(st::settingsThemeOutlineWidth);
	const auto inset = width / 2.
		+ st::settingsThemeOutlineSkip * (1. - toggled);
	const auto radius = st::settingsThemeOutlineRadius
		- (inset - width / 2.);
	auto pen = QPen(_colors->radiobuttonActive);
	pen.setWidthF(width);
	p.setPen(pen);
	p.setBrush(Qt::NoBrush);
	p.setOpacity(toggled);
	p.drawRoundedRect(
		QRectF(0, 0, outerWidth, getSize().height()).adjusted(
			inset,
			inset,
			-inset,
			-inset),
		radius,
		radius);
	p.setOpacity(1.);
}

QImage CloudListCheck::prepareRippleMask() const {
	return QImage();
}

bool CloudListCheck::checkRippleStartPosition(QPoint position) const {
	return false;
}

CloudList::CloudList(
	not_null<QWidget*> parent,
	not_null<Window::SessionController*> window)
: _window(window)
, _owned(parent)
, _outer(_owned.data())
, _group(std::make_shared<Ui::RadiobuttonGroup>()) {
	setup();
}

void CloudList::showAll() {
	_showAll = true;
}

object_ptr<Ui::RpWidget> CloudList::takeWidget() {
	return std::move(_owned);
}

rpl::producer<bool> CloudList::empty() const {
	using namespace rpl::mappers;

	return _count.value() | rpl::map(_1 == 0);
}

rpl::producer<bool> CloudList::allShown() const {
	using namespace rpl::mappers;

	return rpl::combine(
		_showAll.value(),
		_count.value(),
		_1 || (_2 <= kShowPerRow));
}

void CloudList::setup() {
	_group->setChangedCallback([=](int selected) {
		const auto i = ranges::find_if(_elements, [&](const Element &e) {
			return (groupValueForId(e.theme.id) == selected)
				&& !e.theme.emoticon.isEmpty()
				&& !e.theme.settings.empty();
		});
		if (i != end(_elements)) {
			return;
		}
		_group->setValue(groupValueForId(appliedElementId()));
	});

	auto cloudListChanges = rpl::single(rpl::empty) | rpl::then(
		_window->session().data().cloudThemes().updated()
	);

	auto themeChanges = rpl::single(BackgroundUpdate(
		BackgroundUpdate::Type::ApplyingTheme,
		Background()->tile()
	)) | rpl::then(
		Background()->updates()
	) | rpl::filter([](const BackgroundUpdate &update) {
		return (update.type == BackgroundUpdate::Type::ApplyingTheme);
	});

	rpl::combine(
		std::move(cloudListChanges),
		std::move(themeChanges),
		allShown()
	) | rpl::map([=] {
		return collectAll();
	}) | rpl::on_next([=](std::vector<Data::CloudTheme> &&list) {
		rebuildUsing(std::move(list));
	}, _outer->lifetime());

	_outer->widthValue(
	) | rpl::on_next([=](int width) {
		updateGeometry();
	}, _outer->lifetime());
}

std::vector<Data::CloudTheme> CloudList::collectAll() const {
	const auto &object = Background()->themeObject();
	const auto isDefault = IsEmbeddedTheme(object.pathAbsolute);
	auto result = _window->session().data().cloudThemes().list();
	if (!isDefault) {
		const auto i = ranges::find(
			result,
			object.cloud.id,
			&Data::CloudTheme::id);
		if (i == end(result)
			&& !ContainsThemeWithEmoticon(result, object.cloud.emoticon)) {
			if (object.cloud.id) {
				result.push_back(object.cloud);
			} else {
				result.push_back(FakeCloudTheme(object));
			}
		}
	}
	return result;
}

void CloudList::rebuildUsing(std::vector<Data::CloudTheme> &&list) {
	const auto fullCount = int(list.size());
	const auto changed = applyChangesFrom(std::move(list));
	_count = fullCount;
	if (changed) {
		updateGeometry();
	}
}

bool CloudList::applyChangesFrom(std::vector<Data::CloudTheme> &&list) {
	if (list.empty()) {
		if (_elements.empty()) {
			return false;
		}
		_elements.clear();
		return true;
	}
	auto changed = false;
	const auto limit = _showAll.current() ? list.size() : kShowPerRow;
	ranges::stable_sort(list, std::less<>(), [](const Data::CloudTheme &t) {
		return t.documentId ? 0 : 1;
	});
	if (removeStaleUsing(list)) {
		changed = true;
	}
	if (insertTillLimit(list, limit)) {
		changed = true;
	}
	_group->setValue(groupValueForId(appliedElementId()));
	return changed;
}

bool CloudList::removeStaleUsing(const std::vector<Data::CloudTheme> &list) {
	const auto check = [&](Element &element) {
		const auto j = ranges::find(
			list,
			element.theme.id,
			&Data::CloudTheme::id);
		if (j == end(list)) {
			return true;
		}
		refreshElementUsing(element, *j);
		return false;
	};
	const auto from = ranges::remove_if(_elements, check);
	if (from == end(_elements)) {
		return false;
	}
	_elements.erase(from, end(_elements));
	return true;
}

bool CloudList::insertTillLimit(
		const std::vector<Data::CloudTheme> &list,
		int limit) {
	const auto insertCount = (limit - int(_elements.size()));
	if (insertCount < 0) {
		_elements.erase(end(_elements) + insertCount, end(_elements));
		return true;
	} else if (!insertCount) {
		return false;
	}
	const auto isGood = [](const Data::CloudTheme &theme) {
		return (theme.documentId != 0);
	};
	auto positionForGood = ranges::find_if(_elements, [&](const Element &e) {
		return !isGood(e.theme);
	}) - begin(_elements);
	auto positionForBad = end(_elements) - begin(_elements);

	auto insertElements = ranges::views::all(
		list
	) | ranges::views::filter([&](const Data::CloudTheme &theme) {
		const auto i = ranges::find(_elements, theme.id, &Element::id);
		return (i == end(_elements));
	}) | ranges::views::take(insertCount);

	for (const auto &theme : insertElements) {
		const auto good = isGood(theme);
		insert(good ? positionForGood : positionForBad, theme);
		if (good) {
			++positionForGood;
		}
		++positionForBad;
	}
	return true;
}

void CloudList::insert(int index, const Data::CloudTheme &theme) {
	const auto id = theme.id;
	const auto value = groupValueForId(id);
	const auto checked = _group->hasValue() && (_group->current() == value);
	auto check = std::make_unique<CloudListCheck>(checked);
	const auto raw = check.get();
	auto button = std::make_unique<Ui::Radiobutton>(
		_outer,
		_group,
		value,
		theme.emoticon.isEmpty() ? theme.title : QString(),
		st::settingsTheme,
		std::move(check));
	button->setCheckAlignment(style::al_top);
	button->setAllowTextLines(2);
	button->setTextBreakEverywhere();
	button->show();
	button->setAcceptBoth(true);
	button->addClickHandler([=](Qt::MouseButton button) {
		const auto i = ranges::find(_elements, id, &Element::id);
		if (i == end(_elements)
			|| id == kFakeCloudThemeId
			|| i->waiting) {
			return;
		}
		const auto &cloud = i->theme;
		if (button == Qt::RightButton) {
			showMenu(*i);
		} else if (cloud.documentId) {
			++*_applyGeneration;
			_window->session().data().cloudThemes().applyFromDocument(cloud);
		} else if (!cloud.emoticon.isEmpty() && !cloud.settings.empty()) {
			const auto generation = ++*_applyGeneration;
			const auto check = _applyGeneration;
			base::call_delayed(st::defaultRadio.duration, _window, [=] {
				if (*check == generation) {
					ApplyChatTheme(_window, cloud, IsNightMode());
				}
			});
		} else {
			++*_applyGeneration;
			_window->session().data().cloudThemes().showPreview(
				&_window->window(),
				cloud);
		}
	});
	auto &element = *_elements.insert(
		begin(_elements) + index,
		Element{ theme, raw, std::move(button) });
	refreshColors(element);
}

void CloudList::refreshElementUsing(
		Element &element,
		const Data::CloudTheme &data) {
	const auto colorsChanged = (element.theme.documentId != data.documentId)
		|| ((element.id() == kFakeCloudThemeId)
			&& (element.theme.slug != data.slug));
	const auto titleChanged = (element.theme.title != data.title);
	element.theme = data;
	if (colorsChanged || !data.emoticon.isEmpty()) {
		setWaiting(element, false);
		refreshColors(element);
	}
	if (titleChanged) {
		element.button->setText(data.title);
	}
}

void CloudList::refreshColors(Element &element) {
	const auto currentId = Background()->themeObject().cloud.id;
	const auto &theme = element.theme;
	const auto document = theme.documentId
		? _window->session().data().document(theme.documentId).get()
		: nullptr;
	if (!theme.emoticon.isEmpty() && !theme.settings.empty()) {
		requestPreview(element);
		setWaiting(element, false);
		return;
	}
	if (element.id() == kFakeCloudThemeId
		|| ((element.id() == currentId)
			&& (!document || !document->isTheme()))) {
		element.check->setColors(ColorsFromCurrentTheme());
	} else if (document) {
		element.media = document ? document->createMediaView() : nullptr;
		document->save(
			Data::FileOriginTheme(theme.id, theme.accessHash),
			QString());
		if (element.media->loaded()) {
			refreshColorsFromDocument(element);
		} else {
			setWaiting(element, true);
			subscribeToDownloadFinished();
		}
	} else {
		element.check->setColors(CloudListColors());
	}
}

void CloudList::requestPreview(Element &element) {
	const auto dark = Window::Theme::IsNightMode();
	const auto variant = ChatThemeVariant(element.theme, dark);
	if (!variant) {
		element.check->setColors(CloudListColors());
		return;
	}
	const auto id = element.id();
	const auto theme = element.theme;
	const auto accent = theme.settings.find(*variant)->second.accentColor;
	const auto key = Ui::ChatThemeKey{
		theme.id,
		(*variant == Data::CloudThemeType::Dark),
	};
	const auto size = st::settingsThemePreviewSize
		- QSize(st::settingsThemeOutlineSkip, st::settingsThemeOutlineSkip)
			* 2;
	element.previewLifetime.destroy();
	_window->cachedChatThemeValue(
		theme,
		Data::WallPaper(0),
		*variant
	) | rpl::filter([=](const std::shared_ptr<Ui::ChatTheme> &data) {
		return data && (data->key() == key);
	}) | rpl::take(1) | rpl::on_next([=](
			std::shared_ptr<Ui::ChatTheme> &&data) {
		const auto i = ranges::find(_elements, id, &Element::id);
		if (i == end(_elements)) {
			return;
		}
		const auto raw = data.get();
		i->chatTheme = std::move(data);
		i->check->setPreview(
			Ui::GenerateChatThemePreview(
				raw,
				Ui::Emoji::Find(theme.emoticon),
				size),
			accent);
		if (!raw->background().isPattern
			|| !raw->background().prepared.isNull()) {
			return;
		}
		raw->repaintBackgroundRequests(
		) | rpl::filter([=] {
			const auto i = ranges::find(_elements, id, &Element::id);
			return (i == end(_elements))
				|| !i->chatTheme->background().prepared.isNull();
		}) | rpl::take(1) | rpl::on_next([=] {
			const auto i = ranges::find(_elements, id, &Element::id);
			if (i != end(_elements)) {
				i->check->setPreview(
					Ui::GenerateChatThemePreview(
						i->chatTheme.get(),
						Ui::Emoji::Find(theme.emoticon),
						size),
					accent);
			}
		}, i->previewLifetime);
	}, element.previewLifetime);
}

void CloudList::showMenu(Element &element) {
	if (_contextMenu) {
		_contextMenu = nullptr;
		return;
	}
	_contextMenu = base::make_unique_q<Ui::PopupMenu>(
		element.button.get(),
		st::popupMenuWithIcons);
	const auto cloud = element.theme;
	if (const auto slug = element.theme.slug; !slug.isEmpty()) {
		_contextMenu->addAction(tr::lng_theme_share(tr::now), [=] {
			QGuiApplication::clipboard()->setText(
				_window->session().createInternalLinkFull("addtheme/" + slug));
			_window->window().showToast({
				.text = { tr::lng_background_link_copied(tr::now) },
				.iconLottie = u"toast/voip_invite"_q,
				.iconLottieSize = st::toastLottieIconSize,
			});
		}, &st::menuIconShare);
	}
	if (cloud.documentId
		&& cloud.createdBy == _window->session().userId()
		&& Background()->themeObject().cloud.id == cloud.id) {
		_contextMenu->addAction(tr::lng_theme_edit(tr::now), [=] {
			StartEditor(&_window->window(), cloud);
		}, &st::menuIconChangeColors);
	}
	const auto id = cloud.id;
	_contextMenu->addAction(tr::lng_theme_delete(tr::now), [=] {
		const auto remove = [=](Fn<void()> &&close) {
			close();
			if (Background()->themeObject().cloud.id == id
				|| id == kFakeCloudThemeId) {
				if (Background()->editingTheme().has_value()) {
					Background()->clearEditingTheme(
						ClearEditing::KeepChanges);
					_window->window().showRightColumn(nullptr);
				}
				ResetToSomeDefault();
				KeepApplied();
			}
			if (id != kFakeCloudThemeId) {
				_window->session().data().cloudThemes().remove(id);
			}
		};
		_window->window().show(Ui::MakeConfirmBox({
			.text = tr::lng_theme_delete_sure(),
			.confirmed = remove,
			.confirmText = tr::lng_theme_delete(),
		}));
	}, &st::menuIconDelete);
	_contextMenu->popup(QCursor::pos());
}

void CloudList::setWaiting(Element &element, bool waiting) {
	element.waiting = waiting;
	element.button->setPointerCursor(!waiting
		&& (element.theme.documentId
			|| amCreator(element.theme)
			|| !element.theme.settings.empty()));
}

bool CloudList::amCreator(const Data::CloudTheme &theme) const {
	return (_window->session().userId() == theme.createdBy);
}

uint64 CloudList::appliedElementId() const {
	const auto &cloud = Background()->themeObject().cloud;
	const auto id = cloud.id ? cloud.id : kFakeCloudThemeId;
	if (ranges::contains(_elements, id, &Element::id)) {
		return id;
	}
	const auto emoji = Ui::Emoji::Find(cloud.emoticon);
	if (!emoji) {
		return id;
	}
	const auto i = ranges::find(_elements, emoji, [](const Element &element) {
		return Ui::Emoji::Find(element.theme.emoticon);
	});
	return (i != end(_elements)) ? i->id() : id;
}

void CloudList::refreshColorsFromDocument(Element &element) {
	Expects(element.media != nullptr);
	Expects(element.media->loaded());

	const auto id = element.id();
	const auto path = element.media->owner()->filepath();
	const auto data = base::take(element.media)->bytes();
	crl::async([=, guard = element.generating.make_guard()]() mutable {
		crl::on_main(std::move(guard), [
			=,
			result = ColorsFromTheme(path, data)
		]() mutable {
			const auto i = ranges::find(_elements, id, &Element::id);
			if (i == end(_elements) || !result) {
				return;
			}
			auto &element = *i;
			if (result->background.isNull()) {
				result->background = ColorsFromCurrentTheme().background;
			}
			element.check->setColors(*result);
			setWaiting(element, false);
		});
	});
}

void CloudList::subscribeToDownloadFinished() {
	if (_downloadFinishedLifetime) {
		return;
	}
	_window->session().downloaderTaskFinished(
	) | rpl::on_next([=] {
		auto &&waiting = _elements | ranges::views::filter(&Element::waiting);
		const auto still = ranges::count_if(waiting, [&](Element &element) {
			if (!element.media) {
				element.waiting = false;
				return false;
			} else if (!element.media->loaded()) {
				return true;
			}
			refreshColorsFromDocument(element);
			element.waiting = false;
			return false;
		});
		if (!still) {
			_downloadFinishedLifetime.destroy();
		}
	}, _downloadFinishedLifetime);
}

int CloudList::groupValueForId(uint64 id) {
	const auto i = _groupValueById.find(id);
	if (i != end(_groupValueById)) {
		return i->second;
	}
	const auto result = int(_idByGroupValue.size());
	_groupValueById.emplace(id, result);
	_idByGroupValue.push_back(id);
	return result;
}

void CloudList::updateGeometry() {
	const auto width = _outer->width();
	if (!width) {
		return;
	}
	const auto height = resizeGetHeight(width);
	if (height != _outer->height()) {
		_outer->resize(width, height);
	}
}

int CloudList::resizeGetHeight(int newWidth) {
	const auto minSkip = st::settingsThemeMinSkip;
	const auto single = std::min(
		st::settingsThemePreviewSize.width(),
		(newWidth - minSkip * (kShowPerRow - 1)) / kShowPerRow);
	const auto skip = (newWidth - kShowPerRow * single)
		/ float64(kShowPerRow - 1);

	auto x = 0.;
	auto y = 0;

	auto index = 0;
	auto rowHeight = 0;
	for (const auto &element : _elements) {
		const auto button = element.button.get();
		button->resizeToWidth(single);
		button->moveToLeft(int(base::SafeRound(x)), y);
		accumulate_max(rowHeight, button->height());
		x += single + skip;
		if (++index == kShowPerRow) {
			x = 0.;
			index = 0;
			y += rowHeight + st::themesSmallSkip;
			rowHeight = 0;
		}
	}
	return rowHeight
		? (y + rowHeight)
		: (y > 0)
		? (y - st::themesSmallSkip)
		: 0;
}

} // namespace Theme
} // namespace Window
