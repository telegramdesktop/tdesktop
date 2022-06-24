/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/stickers/data_custom_emoji.h"

#include "chat_helpers/stickers_emoji_pack.h"
#include "main/main_session.h"
#include "data/data_session.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_file_origin.h"
#include "data/stickers/data_stickers_set.h"
#include "lottie/lottie_common.h"
#include "lottie/lottie_single_player.h"
#include "chat_helpers/stickers_lottie.h"
#include "ui/text/text_block.h"
#include "apiwrap.h"

namespace Data {

struct CustomEmojiId {
	StickerSetIdentifier set;
	uint64 id = 0;
};

namespace {

[[nodiscard]] QString SerializeCustomEmojiId(const CustomEmojiId &id) {
	return QString::number(id.id)
		+ '@'
		+ QString::number(id.set.id)
		+ ':'
		+ QString::number(id.set.accessHash);
}

[[nodiscard]] CustomEmojiId ParseCustomEmojiData(QStringView data) {
	const auto parts = data.split('@');
	if (parts.size() != 2) {
		return {};
	}
	const auto id = parts[0].toULongLong();
	if (!id) {
		return {};
	}
	const auto second = parts[1].split(':');
	return {
		.set = {
			.id = second[0].toULongLong(),
			.accessHash = second[1].toULongLong(),
		},
		.id = id
	};
}

class CustomEmojiWithData {
public:
	explicit CustomEmojiWithData(const QString &data);

	QString entityData();

private:
	const QString _data;

};

CustomEmojiWithData::CustomEmojiWithData(const QString &data) : _data(data) {
}

QString CustomEmojiWithData::entityData() {
	return _data;
}

class DocumentCustomEmoji final : public CustomEmojiWithData {
public:
	DocumentCustomEmoji(
		const QString &data,
		not_null<DocumentData*> document,
		Fn<void()> update);

	void paint(QPainter &p, int x, int y, const QColor &preview);

private:
	not_null<DocumentData*> _document;
	std::shared_ptr<Data::DocumentMedia> _media;
	std::unique_ptr<Lottie::SinglePlayer> _lottie;
	Fn<void()> _update;
	rpl::lifetime _lifetime;

};

DocumentCustomEmoji::DocumentCustomEmoji(
	const QString &data,
	not_null<DocumentData*> document,
	Fn<void()> update)
: CustomEmojiWithData(data)
, _document(document)
, _update(update) {
}

void DocumentCustomEmoji::paint(QPainter &p, int x, int y, const QColor &preview) {
	if (!_media) {
		_media = _document->createMediaView();
		_media->automaticLoad(_document->stickerSetOrigin(), nullptr);
	}
	if (_media->loaded() && !_lottie) {
		const auto size = Ui::Emoji::GetSizeNormal();
		_lottie = ChatHelpers::LottiePlayerFromDocument(
			_media.get(),
			nullptr,
			ChatHelpers::StickerLottieSize::MessageHistory,
			QSize(size, size),
			Lottie::Quality::High);
		_lottie->updates() | rpl::start_with_next(_update, _lifetime);
	}
	if (_lottie && _lottie->ready()) {
		const auto frame = _lottie->frame();
		p.drawImage(
			QRect(
				x,
				y,
				frame.width() / frame.devicePixelRatio(),
				frame.height() / frame.devicePixelRatio()),
			frame);
		_lottie->markFrameShown();
	}
}

} // namespace

class CustomEmojiLoader final
	: public Ui::CustomEmoji::Loader
	, public base::has_weak_ptr {
public:
	CustomEmojiLoader(not_null<Session*> owner, const CustomEmojiId id);

	[[nodiscard]] bool resolving() const;
	void resolved(not_null<DocumentData*> document);

	void load(Fn<void(Ui::CustomEmoji::Caching)> ready) override;
	void cancel() override;
	Ui::CustomEmoji::Preview preview() override;

private:
	struct Resolve {
		Fn<void(Ui::CustomEmoji::Caching)> requested;
	};
	struct Process {
		std::shared_ptr<DocumentMedia> media;
		Fn<void(Ui::CustomEmoji::Caching)> callback;
		rpl::lifetime lifetime;
	};
	struct Load {
		not_null<DocumentData*> document;
		std::unique_ptr<Process> process;
	};

	[[nodiscard]] static std::variant<Resolve, Load> InitialState(
		not_null<Session*> owner,
		const CustomEmojiId &id);

	std::variant<Resolve, Load> _state;

};

CustomEmojiLoader::CustomEmojiLoader(
	not_null<Session*> owner,
	const CustomEmojiId id)
: _state(InitialState(owner, id)) {
}

bool CustomEmojiLoader::resolving() const {
	return v::is<Resolve>(_state);
}

void CustomEmojiLoader::resolved(not_null<DocumentData*> document) {
	Expects(resolving());

	auto requested = std::move(v::get<Resolve>(_state).requested);
	_state = Load{ document };
	if (requested) {
		load(std::move(requested));
	}
}

void CustomEmojiLoader::load(Fn<void(Ui::CustomEmoji::Caching)> ready) {
	if (const auto resolve = std::get_if<Resolve>(&_state)) {
		resolve->requested = std::move(ready);
	} else if (const auto load = std::get_if<Load>(&_state)) {
		if (!load->process) {
			load->process = std::make_unique<Process>(Process{
				.media = load->document->createMediaView(),
				.callback = std::move(ready),
			});
			load->process->media->checkStickerLarge();
		} else {
			load->process->callback = std::move(ready);
		}
	}
}

auto CustomEmojiLoader::InitialState(
	not_null<Session*> owner,
	const CustomEmojiId &id)
-> std::variant<Resolve, Load> {
	const auto document = owner->document(id.id);
	if (!document->isNull()) {
		return Load{ document };
	}
	return Resolve();
}

void CustomEmojiLoader::cancel() {
	if (const auto load = std::get_if<Load>(&_state)) {
		if (base::take(load->process)) {
			load->document->cancel();
		}
	}
}

Ui::CustomEmoji::Preview CustomEmojiLoader::preview() {
	if (const auto load = std::get_if<Load>(&_state)) {
		if (const auto process = load->process.get()) {
			const auto dimensions = load->document->dimensions;
			if (!dimensions.width()) {
				return {};
			}
			const auto scale = (Ui::Emoji::GetSizeNormal() * 1.)
				/ (style::DevicePixelRatio() * dimensions.width());
			return { process->media->thumbnailPath(), scale };
		}
	}
	return {};
}

CustomEmojiManager::CustomEmojiManager(not_null<Session*> owner)
: _owner(owner) {
}

CustomEmojiManager::~CustomEmojiManager() = default;

std::unique_ptr<Ui::Text::CustomEmoji> CustomEmojiManager::create(
		const QString &data,
		Fn<void()> update) {
	const auto parsed = ParseCustomEmojiData(data);
	if (!parsed.id || !parsed.set.id) {
		return nullptr;
	}
	auto i = _instances.find(parsed.set.id);
	if (i == end(_instances)) {
		i = _instances.emplace(parsed.set.id).first;
	}
	auto j = i->second.find(parsed.id);
	if (j == end(i->second)) {
		using Loading = Ui::CustomEmoji::Loading;
		auto loader = std::make_unique<CustomEmojiLoader>(_owner, parsed);
		if (loader->resolving()) {
			_loaders[parsed.id].push_back(base::make_weak(loader.get()));
		}
		j = i->second.emplace(
			parsed.id,
			std::make_unique<Ui::CustomEmoji::Instance>(data, Loading{
				std::move(loader),
				Ui::CustomEmoji::Preview()
			})).first;
	}
	requestSetIfNeeded(parsed);

	return std::make_unique<Ui::CustomEmoji::Object>(j->second.get());
}

void CustomEmojiManager::requestSetIfNeeded(const CustomEmojiId &id) {
	const auto setId = id.set.id;
	auto i = _sets.find(setId);
	if (i == end(_sets)) {
		i = _sets.emplace(setId).first;
	}
	if (i->second.documents.contains(id.id)) {
		return;
	} else if (!i->second.waiting.emplace(id.id).second
		|| i->second.requestId) {
		return;
	}
	const auto api = &_owner->session().api();
	i->second.requestId = api->request(MTPmessages_GetStickerSet(
		InputStickerSet(id.set),
		MTP_int(i->second.hash)
	)).done([=](const MTPmessages_StickerSet &result) {
		const auto i = _sets.find(setId);
		Assert(i != end(_sets));
		i->second.requestId = 0;
		result.match([&](const MTPDmessages_stickerSet &data) {
			data.vset().match([&](const MTPDstickerSet &data) {
				i->second.hash = data.vhash().v;
			});
			for (const auto &entry : data.vdocuments().v) {
				const auto document = _owner->processDocument(entry);
				const auto id = document->id;
				i->second.documents.emplace(id);
				i->second.waiting.remove(id);
				if (const auto loaders = _loaders.take(id)) {
					for (const auto &weak : *loaders) {
						if (const auto strong = weak.get()) {
							strong->resolved(document);
						}
					}
				}
			}
		}, [&](const MTPDmessages_stickerSetNotModified &) {
		});
		for (const auto &id : base::take(i->second.waiting)) {
			DEBUG_LOG(("API Error: Sticker '%1' not found for emoji.").arg(id));
		}
	}).fail([=] {
		const auto i = _sets.find(setId);
		Assert(i != end(_sets));
		i->second.requestId = 0;
		LOG(("API Error: Failed getting set '%1' for emoji.").arg(setId));
	}).send();
}

Main::Session &CustomEmojiManager::session() const {
	return _owner->session();
}

Session &CustomEmojiManager::owner() const {
	return *_owner;
}

void FillTestCustomEmoji(
		not_null<Main::Session*> session,
		TextWithEntities &text) {
	const auto pack = &session->emojiStickersPack();
	const auto begin = text.text.constData(), end = begin + text.text.size();
	for (auto ch = begin; ch != end;) {
		auto length = 0;
		if (const auto emoji = Ui::Emoji::Find(ch, end, &length)) {
			if (const auto found = pack->stickerForEmoji(emoji)) {
				Assert(found.document->sticker() != nullptr);
				if (found.document->sticker()->set.id) {
					text.entities.push_back({
						EntityType::CustomEmoji,
						(ch - begin),
						length,
						SerializeCustomEmojiId({
							found.document->sticker()->set,
							found.document->id,
						}),
					});
				}
			}
			ch += length;
		} else if (ch->isHighSurrogate()
			&& (ch + 1 != end)
			&& (ch + 1)->isLowSurrogate()) {
			ch += 2;
		} else {
			++ch;
		}
	}
	ranges::stable_sort(
		text.entities,
		ranges::less(),
		&EntityInText::offset);
}

} // namespace Data
