/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "window/themes/window_themes_cloud_list.h"

#include "window/themes/window_themes_embedded.h"
#include "window/themes/window_theme.h"
#include "window/window_session_controller.h"
#include "data/data_cloud_themes.h"
#include "data/data_file_origin.h"
#include "data/data_document.h"
#include "data/data_session.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "styles/style_settings.h"
#include "styles/style_boxes.h"

namespace Window {
namespace Theme {
namespace {

[[nodiscard]] QImage ColorsBackgroundFromImage(const QImage &source) {
	if (source.isNull()) {
		return source;
	}
	const auto from = source.size();
	const auto to = st::settingsThemePreviewSize * cIntRetinaFactor();
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
			? small.copy(0, (small.width() - takew) / 2, takew, takeh)
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
	if (!LoadFromContent(content, &instance)) {
		return std::nullopt;
	}
	auto result = CloudListColors();
	result.background = ColorsBackgroundFromImage(instance.background);
	result.sent = st::msgOutBg[instance.palette]->c;
	result.received = st::msgInBg[instance.palette]->c;
	result.radiobuttonBg = st::msgServiceBg[instance.palette]->c;
	result.radiobuttonActive
		= result.radiobuttonInactive
		= st::msgServiceFg[instance.palette]->c;
	return result;
}

} // namespace

CloudListColors ColorsFromScheme(const EmbeddedScheme &scheme) {
	auto result = CloudListColors();
	result.sent = scheme.sent;
	result.received = scheme.received;
	result.radiobuttonActive = scheme.radiobuttonActive;
	result.radiobuttonInactive = scheme.radiobuttonInactive;
	result.radiobuttonBg = QColor(255, 255, 255, 0);
	result.background = QImage(
		QSize(1, 1) * cIntRetinaFactor(),
		QImage::Format_ARGB32_Premultiplied);
	result.background.fill(scheme.background);
	return result;
}

CloudListColors ColorsFromScheme(
		const EmbeddedScheme &scheme,
		const Colorizer &colorizer) {
	if (!colorizer) {
		return ColorsFromScheme(scheme);
	}
	auto copy = scheme;
	Colorize(copy, colorizer);
	return ColorsFromScheme(copy);
}

CloudListCheck::CloudListCheck(const Colors &colors, bool checked)
: AbstractCheckView(st::defaultRadio.duration, checked, nullptr)
, _radio(st::defaultRadio, checked, [=] { update(); }) {
	setColors(colors);
}

void CloudListCheck::setColors(const Colors &colors) {
	_colors = colors;
	_radio.setToggledOverride(_colors.radiobuttonActive);
	_radio.setUntoggledOverride(_colors.radiobuttonInactive);
	update();
}

QSize CloudListCheck::getSize() const {
	return st::settingsThemePreviewSize;
}

void CloudListCheck::paint(
		Painter &p,
		int left,
		int top,
		int outerWidth) {
	if (_colors.background.isNull()) {
		return;
	}

	const auto received = QRect(
		st::settingsThemeBubblePosition,
		st::settingsThemeBubbleSize);
	const auto sent = QRect(
		outerWidth - received.width() - st::settingsThemeBubblePosition.x(),
		received.y() + received.height() + st::settingsThemeBubbleSkip,
		received.width(),
		received.height());
	const auto radius = st::settingsThemeBubbleRadius;

	PainterHighQualityEnabler hq(p);
	p.setPen(Qt::NoPen);

	p.drawImage(
		QRect(QPoint(), st::settingsThemePreviewSize),
		_colors.background);

	p.setBrush(_colors.received);
	p.drawRoundedRect(rtlrect(received, outerWidth), radius, radius);
	p.setBrush(_colors.sent);
	p.drawRoundedRect(rtlrect(sent, outerWidth), radius, radius);

	const auto skip = st::settingsThemeRadioBottom / 2;

	const auto radio = _radio.getSize();
	_radio.paint(
		p,
		(outerWidth - radio.width()) / 2,
		getSize().height() - radio.height() - st::settingsThemeRadioBottom,
		outerWidth);
}

QImage CloudListCheck::prepareRippleMask() const {
	return QImage();
}

bool CloudListCheck::checkRippleStartPosition(QPoint position) const {
	return false;
}

void CloudListCheck::checkedChangedHook(anim::type animated) {
	_radio.setChecked(checked(), animated);
}

int CountButtonsHeight(const std::vector<not_null<Ui::RpWidget*>> &buttons) {
	constexpr auto kPerRow = 4;
	const auto skip = (st::boxWideWidth
		- st::settingsSubsectionTitlePadding.left()
		- st::settingsSubsectionTitlePadding.right()
		- kPerRow * st::settingsThemePreviewSize.width())
		/ float64(kPerRow - 1);
	auto x = 0.;
	auto y = 0;

	auto index = 0;
	for (const auto button : buttons) {
		button->moveToLeft(int(std::round(x)), y);
		x += st::settingsThemePreviewSize.width() + skip;
		if (++index == kPerRow) {
			x = 0.;
			index = 0;
			y += st::settingsTheme.textPosition.y()
				+ st::settingsTheme.style.font->height
				+ st::themesSmallSkip;
		}
	}
	if (index) {
		return y
			+ st::settingsTheme.textPosition.y()
			+ st::settingsTheme.style.font->height;
	} else if (y) {
		return y - st::themesSmallSkip;
	}
	return 0;
}

void CloudListBox(
		not_null<GenericBox*> box,
		not_null<Window::SessionController*> window,
		std::vector<Data::CloudTheme> list) {
	using WaitingPair = std::pair<
		not_null<DocumentData*>,
		not_null<CloudListCheck*>>;
	box->setTitle(tr::lng_settings_bg_cloud_themes());
	box->setWidth(st::boxWideWidth);

	const auto currentId = Background()->themeObject().cloud.documentId;
	ranges::stable_sort(list, std::less<>(), [&](const Data::CloudTheme &t) {
		return !t.documentId ? 2 : (t.documentId == currentId) ? 0 : 1;
	});

	const auto content = box->addRow(
		object_ptr<Ui::RpWidget>(box),
		style::margins(
			st::settingsSubsectionTitlePadding.left(),
			0,
			st::settingsSubsectionTitlePadding.right(),
			0));
	const auto group = std::make_shared<Ui::RadiobuttonGroup>();
	const auto resolveCurrent = [=] {
		const auto currentId = Background()->themeObject().cloud.id;
		const auto i = currentId
			? ranges::find(list, currentId, &Data::CloudTheme::id)
			: end(list);
		group->setValue(i - begin(list));
	};

	resolveCurrent();
	auto checker = Background()->add_subscription([=](const BackgroundUpdate &update) {
		if (update.type == BackgroundUpdate::Type::ApplyingTheme
			|| update.type == BackgroundUpdate::Type::New) {
			resolveCurrent();
		}
	});
	group->setChangedCallback([=](int selected) {
		resolveCurrent();
	});
	Ui::AttachAsChild(box, std::move(checker));

	const auto waiting = std::make_shared<std::vector<WaitingPair>>();
	const auto fallback = std::make_shared<QImage>();
	const auto buttonsMap = std::make_shared<base::flat_map<
		not_null<CloudListCheck*>,
		not_null<Ui::Radiobutton*>>>();
	const auto getFallbackImage = [=] {
		if (fallback->isNull()) {
			*fallback = ColorsBackgroundFromImage(
				Background()->createCurrentImage());
		}
		return *fallback;
	};

	auto index = 0;
	auto buttons = ranges::view::all(
		list
	) | ranges::view::transform([&](const Data::CloudTheme &theme)
		-> not_null<Ui::RpWidget*> {
		if (!theme.documentId) {
			index++;
			return Ui::CreateChild<Ui::RpWidget>(content);
		}
		const auto document = window->session().data().document(
			theme.documentId);
		document->save(Data::FileOrigin(), QString()); // #TODO themes
		auto colors = ColorsFromTheme(
			document->filepath(),
			document->data());
		if (colors && colors->background.isNull()) {
			colors->background = getFallbackImage();
		}

		auto check = std::make_unique<CloudListCheck>(
			colors.value_or(CloudListColors()),
			false);
		const auto weak = check.get();
		const auto result = Ui::CreateChild<Ui::Radiobutton>(
			content,
			group,
			index++,
			theme.title,
			st::settingsTheme,
			std::move(check));
		result->addClickHandler([=] {
			if (result->isDisabled()) {
				return;
			}
			DocumentOpenClickHandler::Open(
				Data::FileOrigin(),
				document,
				nullptr);
		});
		if (!document->loaded()) {
			waiting->emplace_back(document, weak);
			buttonsMap->emplace(weak, result);
		}
		if (!colors) {
			result->setDisabled(true);
			result->setPointerCursor(false);
		}
		result->setCheckAlignment(style::al_top);
		result->resizeToWidth(st::settingsThemePreviewSize.width());
		weak->setUpdateCallback([=] { result->update(); });
		return result;
	}) | ranges::to_vector;

	const auto check = [=](WaitingPair pair) {
		const auto &[document, check] = pair;
		if (!document->loaded()) {
			return false;
		}
		auto colors = ColorsFromTheme(
			document->filepath(),
			document->data());
		if (colors) {
			if (colors->background.isNull()) {
				colors->background = getFallbackImage();
			}
			check->setColors(*colors);
			const auto i = buttonsMap->find(check);
			Assert(i != end(*buttonsMap));
			i->second->setDisabled(false);
			i->second->setPointerCursor(true);
		}
		return true;
	};
	auto &finished = window->session().downloaderTaskFinished();
	auto subscription = finished.add_subscription([=] {
		waiting->erase(ranges::remove_if(*waiting, check), end(*waiting));
	});
	Ui::AttachAsChild(box, std::move(subscription));

	const auto height = CountButtonsHeight(buttons);
	content->resize(content->width(), height);

	box->addButton(tr::lng_close(), [=] { box->closeBox(); });
}

} // namespace Theme
} // namespace Window
