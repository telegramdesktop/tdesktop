/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/boxes/about_cocoon_box.h"

#include "lang/lang_keys.h"
#include "ui/controls/feature_list.h"
#include "ui/effects/ministar_particles.h"
#include "ui/layers/generic_box.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/text/text_utilities.h"
#include "ui/top_background_gradient.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/shadow.h"
#include "styles/style_chat.h"
#include "styles/style_credits.h"
#include "styles/style_info.h" // infoStarsUnderstood
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"

namespace Ui {
namespace {

void AddCocoonBoxCover(not_null<Ui::VerticalLayout*> container) {
	const auto cover = container->add(object_ptr<Ui::RpWidget>(container));

	static const auto gradientEdge = QColor(0x06, 0x11, 0x29);
	static const auto gradientCenter = QColor(0x09, 0x16, 0x43);

	static const auto gradientLeft = QColor(0x68, 0xc9, 0xff);
	static const auto gradientRight = QColor(0xcb, 0x57, 0xff);

	static const auto textColor = QColor(0xb8, 0xc9, 0xef);
	static const auto boldColor = QColor(0xff, 0xff, 0xff);

	const auto logoTop = st::cocoonLogoTop;
	const auto logoSize = st::cocoonLogoSize;
	const auto ratio = style::DevicePixelRatio();
	auto logo = QImage(u":/gui/art/cocoon.webp"_q).scaled(
		QSize(logoSize, logoSize) * ratio,
		Qt::IgnoreAspectRatio,
		Qt::SmoothTransformation);
	logo.setDevicePixelRatio(ratio);
	const auto titleTop = logoTop + logoSize + st::cocoonTitleTop;
	const auto subtitleTop = titleTop
		+ st::cocoonTitleFont->height
		+ st::cocoonSubtitleTop;

	const auto boldToColorized = [](QString text) {
		auto result = tr::rich(text);
		auto &entities = result.entities;
		for (auto i = entities.begin(); i != entities.end(); ++i) {
			if (i->type() == EntityType::Bold) {
				i = entities.insert(
					i,
					EntityInText(
						EntityType::Colorized,
						i->offset(),
						i->length()));
				++i;
			}
		}
		return result;
	};

	struct State {
		QImage gradient;
		Ui::Animations::Basic animation;
		std::optional<Ui::StarParticles> particles;
		style::owned_color subtitleFg = style::owned_color{ textColor };
		style::owned_color subtitleBoldFg = style::owned_color{ boldColor };
		style::FlatLabel subtitleSt = st::cocoonSubtitle;
	};
	const auto state = cover->lifetime().make_state<State>();
	state->subtitleSt.textFg = state->subtitleFg.color();
	state->subtitleSt.palette.linkFg = state->subtitleBoldFg.color();
	state->animation.init([=] {
		cover->update();
		if (anim::Disabled()) {
			state->animation.stop();
		}
	});

	constexpr auto kParticlesCount = 50;
	state->particles.emplace(
		Ui::StarParticles::Type::RadialInside,
		kParticlesCount,
		st::cocoonLogoSize / 12);
	state->particles->setSpeed(0.05);
	auto particleColors = std::vector<QColor>();
	constexpr auto kParticleColors = 12;
	particleColors.reserve(kParticleColors);
	for (auto i = 0; i != kParticleColors; ++i) {
		particleColors.push_back(anim::color(
			gradientLeft,
			gradientRight,
			float64(i) / (kParticleColors - 1)));
	}
	state->particles->setColors(std::move(particleColors));

	const auto subtitle = CreateChild<Ui::FlatLabel>(
		cover,
		tr::lng_translate_cocoon_subtitle(
			lt_text,
			tr::lng_translate_cocoon_explain(boldToColorized),
			tr::rich),
		state->subtitleSt);
	subtitle->setTryMakeSimilarLines(true);

	cover->widthValue() | rpl::on_next([=](int width) {
		const auto available = width
			- st::boxRowPadding.left()
			- st::boxRowPadding.right();
		subtitle->resizeToWidth(available);
		subtitle->moveToLeft(st::boxRowPadding.left(), subtitleTop);

		const auto bottom = st::cocoonSubtitleBottom;
		cover->resize(width, subtitle->y() + subtitle->height() + bottom);
	}, cover->lifetime());

	cover->paintRequest() | rpl::on_next([=] {
		auto p = Painter(cover);

		const auto width = cover->width();
		const auto ratio = style::DevicePixelRatio();
		if (state->gradient.size() != cover->size() * ratio) {
			state->gradient = Ui::CreateTopBgGradient(
				cover->size(),
				gradientCenter,
				gradientEdge);

			auto font = st::cocoonTitleFont->f;
			font.setWeight(QFont::Bold);
			auto metrics = QFontMetrics(font);

			const auto text = tr::lng_translate_cocoon_title(tr::now);
			const auto textw = metrics.horizontalAdvance(text);
			const auto left = (width - textw) / 2;

			auto q = QPainter(&state->gradient);
			auto hq = PainterHighQualityEnabler(q);

			auto gradient = QLinearGradient(left, 0, left + textw, 0);
			gradient.setStops({
				{ 0., gradientLeft },
				{ 1., gradientRight },
			});
			auto pen = QPen(QBrush(gradient), 1.);
			q.setPen(pen);
			q.setFont(font);
			q.setBrush(Qt::NoBrush);

			q.drawText(left, titleTop + metrics.ascent(), text);
		}
		p.drawImage(0, 0, state->gradient);

		const auto logoRect = QRect(
			(width - logoSize) / 2,
			logoTop,
			logoSize,
			logoSize);
		const auto paddingAdd = int(base::SafeRound(logoTop * 1.2));
		const auto particlesRect = logoRect.marginsAdded(
			{ paddingAdd, paddingAdd, paddingAdd, paddingAdd });

		state->particles->paint(p, particlesRect, crl::now(), false);
		if (!anim::Disabled() && !state->animation.animating()) {
			state->animation.start();
		}
		p.drawImage(logoRect, logo);
	}, cover->lifetime());
}

struct CocoonLinkInfo {
	QString text;
	QString link;
};

[[nodiscard]] CocoonLinkInfo CocoonMention() {
	const auto mention = tr::lng_translate_cocoon_private_mention(tr::now);
	const auto username = QString(mention).replace('@', QString());
	const auto link = u"tg://resolve?domain="_q + username;
	return { mention, link };
}

[[nodiscard]] CocoonLinkInfo CocoonDomain() {
	const auto domain = tr::lng_translate_cocoon_everyone_domain(tr::now);
	const auto link = u"https://"_q + domain;
	return { domain, link };
}

[[nodiscard]] CocoonLinkInfo CocoonDirect() {
	const auto text = tr::lng_translate_cocoon_text_link(tr::now);
	const auto link = u"https://"_q + text;
	return { text, link };
}

} // namespace

void AboutCocoonBox(not_null<Ui::GenericBox*> box) {
	box->setWidth(st::boxWideWidth);
	box->setStyle(st::stakeBox);
	box->setNoContentMargin(true);

	const auto container = box->verticalLayout();
	AddCocoonBoxCover(container);

	AddUniqueCloseButton(box);

	const auto mention = CocoonMention();
	const auto domain = CocoonDomain();
	const auto direct = CocoonDirect();
	const auto features = std::vector<Ui::FeatureListEntry>{
		{
			st::menuIconLock,
			tr::lng_translate_cocoon_private_title(tr::now),
			tr::lng_translate_cocoon_private_text(
				tr::now,
				lt_mention,
				tr::link(mention.text, mention.link),
				tr::rich),
		},
		{
			st::menuIconStats,
			tr::lng_translate_cocoon_efficient_title(tr::now),
			tr::lng_translate_cocoon_efficient_text(tr::now, tr::rich),
		},
		{
			st::menuIconGiftPremium,
			tr::lng_translate_cocoon_everyone_title(tr::now),
			tr::lng_translate_cocoon_everyone_text(
				tr::now,
				lt_domain,
				tr::link(domain.text, domain.link),
				tr::rich),
		},
	};
	auto margin = QMargins(0, st::defaultVerticalListSkip, 0, 0);
	for (const auto &feature : features) {
		box->addRow(
			MakeFeatureListEntry(box, feature),
			st::boxRowPadding + margin);
		margin = {};
	}

	box->addRow(
		object_ptr<Ui::PlainShadow>(box),
		st::cocoonJoinSeparatorPadding);

	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			tr::lng_translate_cocoon_text(
				lt_link,
				rpl::single(tr::link(direct.text, direct.link)),
				tr::rich),
			st::cocoonJoinLabel),
		style::al_top);

	box->addButton(rpl::single(QString()), [=] {
		box->closeBox();
	})->setText(rpl::single(Ui::Text::IconEmoji(
		&st::infoStarsUnderstood
	).append(' ').append(tr::lng_translate_cocoon_done(tr::now))));
}

void AddUniqueCloseButton(not_null<GenericBox*> box) {
	const auto close = Ui::CreateChild<IconButton>(
		box,
		st::uniqueCloseButton);
	close->show();
	close->raise();
	box->widthValue() | rpl::on_next([=](int width) {
		close->moveToRight(0, 0, width);
		close->raise();
	}, close->lifetime());
	close->setClickedCallback([=] {
		box->closeBox();
	});
}

} // namespace Ui
