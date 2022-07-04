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
#include "lottie/lottie_common.h"
#include "lottie/lottie_emoji.h"
#include "ffmpeg/ffmpeg_emoji.h"
#include "chat_helpers/stickers_lottie.h"
#include "ui/text/text_block.h"
#include "ui/ui_utility.h"
#include "apiwrap.h"

#include "data/stickers/data_stickers.h"

namespace Data {
namespace {

using SizeTag = CustomEmojiManager::SizeTag;

[[nodiscard]] ChatHelpers::StickerLottieSize LottieSizeFromTag(SizeTag tag) {
	using LottieSize = ChatHelpers::StickerLottieSize;
	switch (tag) {
	case SizeTag::Normal: return LottieSize::MessageHistory;
	case SizeTag::Large: return LottieSize::EmojiInteraction;
	}
	Unexpected("SizeTag value in CustomEmojiManager-LottieSizeFromTag.");
}

[[nodiscard]] int SizeFromTag(SizeTag tag) {
	switch (tag) {
	case SizeTag::Normal: return Ui::Emoji::GetSizeNormal();
	case SizeTag::Large: return Ui::Emoji::GetSizeLarge();
	}
	Unexpected("SizeTag value in CustomEmojiManager-SizeFromTag.");
}

} // namespace

class CustomEmojiLoader final
	: public Ui::CustomEmoji::Loader
	, public base::has_weak_ptr {
public:
	CustomEmojiLoader(
		not_null<Session*> owner,
		const CustomEmojiId id,
		SizeTag tag);
	CustomEmojiLoader(not_null<DocumentData*> document, SizeTag tag);

	[[nodiscard]] bool resolving() const;
	void resolved(not_null<DocumentData*> document);

	QString entityData() override;

	void load(Fn<void(LoadResult)> loaded) override;
	bool loading() override;
	void cancel() override;
	Ui::CustomEmoji::Preview preview() override;

private:
	struct Resolve {
		Fn<void(LoadResult)> requested;
		QString entityData;
	};
	struct Process {
		std::shared_ptr<DocumentMedia> media;
		Fn<void(LoadResult)> loaded;
		base::has_weak_ptr guard;
		rpl::lifetime lifetime;
	};
	struct Requested {
		not_null<DocumentData*> document;
		std::unique_ptr<Process> process;
	};
	struct Lookup : Requested {
	};
	struct Load : Requested {
	};

	void check();
	[[nodiscard]] Storage::Cache::Key cacheKey(
		not_null<DocumentData*> document) const;
	void startCacheLookup(
		not_null<Lookup*> lookup,
		Fn<void(LoadResult)> loaded);
	void lookupDone(
		not_null<Lookup*> lookup,
		std::optional<Ui::CustomEmoji::Cache> result);
	void loadNoCache(
		not_null<DocumentData*> document,
		Fn<void(LoadResult)> loaded);

	[[nodiscard]] static std::variant<Resolve, Lookup, Load> InitialState(
		not_null<Session*> owner,
		const CustomEmojiId &id);

	std::variant<Resolve, Lookup, Load> _state;
	SizeTag _tag = SizeTag::Normal;

};

CustomEmojiLoader::CustomEmojiLoader(
	not_null<Session*> owner,
	const CustomEmojiId id,
	SizeTag tag)
: _state(InitialState(owner, id))
, _tag(tag) {
}

CustomEmojiLoader::CustomEmojiLoader(
	not_null<DocumentData*> document,
	SizeTag tag)
: _state(Lookup{ document })
, _tag(tag) {
}

bool CustomEmojiLoader::resolving() const {
	return v::is<Resolve>(_state);
}

void CustomEmojiLoader::resolved(not_null<DocumentData*> document) {
	Expects(resolving());

	auto requested = std::move(v::get<Resolve>(_state).requested);
	_state = Lookup{ document };
	if (requested) {
		load(std::move(requested));
	}
}

void CustomEmojiLoader::load(Fn<void(LoadResult)> loaded) {
	if (const auto resolve = std::get_if<Resolve>(&_state)) {
		resolve->requested = std::move(loaded);
	} else if (const auto lookup = std::get_if<Lookup>(&_state)) {
		if (!lookup->process) {
			startCacheLookup(lookup, std::move(loaded));
		} else {
			lookup->process->loaded = std::move(loaded);
		}
	} else if (const auto load = std::get_if<Load>(&_state)) {
		if (!load->process) {
			load->process = std::make_unique<Process>(Process{
				.media = load->document->createMediaView(),
				.loaded = std::move(loaded),
			});
			load->process->media->checkStickerLarge();
			if (load->process->media->loaded()) {
				check();
			} else {
				load->document->session().downloaderTaskFinished(
				) | rpl::start_with_next([=] {
					check();
				}, load->process->lifetime);
			}
		} else {
			load->process->loaded = std::move(loaded);
		}
	}
}

QString CustomEmojiLoader::entityData() {
	if (const auto resolve = std::get_if<Resolve>(&_state)) {
		return resolve->entityData;
	} else if (const auto lookup = std::get_if<Lookup>(&_state)) {
		return SerializeCustomEmojiId(lookup->document);
	} else if (const auto load = std::get_if<Load>(&_state)) {
		return SerializeCustomEmojiId(load->document);
	}
	Unexpected("State in CustomEmojiLoader::entityData.");
}

bool CustomEmojiLoader::loading() {
	if (const auto resolve = std::get_if<Resolve>(&_state)) {
		return (resolve->requested != nullptr);
	} else if (const auto lookup = std::get_if<Lookup>(&_state)) {
		return (lookup->process != nullptr);
	} else if (const auto load = std::get_if<Load>(&_state)) {
		return (load->process != nullptr);
	}
	return false;
}

Storage::Cache::Key CustomEmojiLoader::cacheKey(
		not_null<DocumentData*> document) const {
	const auto baseKey = document->bigFileBaseCacheKey();
	if (!baseKey) {
		return {};
	}
	return Storage::Cache::Key{
		baseKey.high,
		baseKey.low + ChatHelpers::LottieCacheKeyShift(
			0x0F,
			LottieSizeFromTag(_tag)),
	};
}

void CustomEmojiLoader::startCacheLookup(
		not_null<Lookup*> lookup,
		Fn<void(LoadResult)> loaded) {
	const auto document = lookup->document;
	const auto key = cacheKey(document);
	if (!key) {
		loadNoCache(document, std::move(loaded));
		return;
	}
	lookup->process = std::make_unique<Process>(Process{
		.loaded = std::move(loaded),
	});
	const auto weak = base::make_weak(&lookup->process->guard);
	document->owner().cacheBigFile().get(key, [=](QByteArray value) {
		auto cache = Ui::CustomEmoji::Cache::FromSerialized(value);
		crl::on_main(weak, [=, result = std::move(cache)]() mutable {
			lookupDone(lookup, std::move(result));
		});
	});
}

void CustomEmojiLoader::lookupDone(
		not_null<Lookup*> lookup,
		std::optional<Ui::CustomEmoji::Cache> result) {
	const auto document = lookup->document;
	if (!result) {
		loadNoCache(document, std::move(lookup->process->loaded));
		return;
	}
	const auto tag = _tag;
	auto loader = [=] {
		return std::make_unique<CustomEmojiLoader>(document, tag);
	};
	auto done = std::move(lookup->process->loaded);
	done(Ui::CustomEmoji::Cached(
		SerializeCustomEmojiId(document),
		std::move(loader),
		std::move(*result)));
}

void CustomEmojiLoader::loadNoCache(
		not_null<DocumentData*> document,
		Fn<void(LoadResult)> loaded) {
	_state = Load{ document };
	load(std::move(loaded));
}

void CustomEmojiLoader::check() {
	using namespace Ui::CustomEmoji;

	const auto load = std::get_if<Load>(&_state);
	Assert(load != nullptr);
	Assert(load->process != nullptr);

	const auto media = load->process->media.get();
	const auto document = media->owner();
	const auto data = media->bytes();
	const auto filepath = document->filepath();
	if (data.isEmpty() && filepath.isEmpty()) {
		return;
	}
	load->process->lifetime.destroy();

	const auto tag = _tag;
	const auto size = SizeFromTag(_tag);
	auto bytes = Lottie::ReadContent(data, filepath);
	auto loader = [=] {
		return std::make_unique<CustomEmojiLoader>(document, tag);
	};
	auto put = [=, key = cacheKey(document)](QByteArray value) {
		document->owner().cacheBigFile().put(key, std::move(value));
	};
	const auto type = document->sticker()->type;
	auto generator = [=, bytes = Lottie::ReadContent(data, filepath)]()
	-> std::unique_ptr<Ui::FrameGenerator> {
		switch (type) {
		case StickerType::Tgs:
			return std::make_unique<Lottie::EmojiGenerator>(bytes);
		case StickerType::Webm:
			return std::make_unique<FFmpeg::EmojiGenerator>(bytes);
		case StickerType::Webp:
			return std::make_unique<Ui::ImageFrameGenerator>(bytes);
		}
		Unexpected("Type in custom emoji sticker frame generator.");
	};
	auto renderer = std::make_unique<Renderer>(RendererDescriptor{
		.generator = std::move(generator),
		.put = std::move(put),
		.loader = std::move(loader),
		.size = SizeFromTag(_tag),
	});
	base::take(load->process)->loaded(Caching{
		std::move(renderer),
		SerializeCustomEmojiId(document),
	});
}

auto CustomEmojiLoader::InitialState(
	not_null<Session*> owner,
	const CustomEmojiId &id)
-> std::variant<Resolve, Lookup, Load> {
	const auto document = owner->document(id.id);
	if (!document->isNull()) {
		return Lookup{ document };
	}
	return Resolve{ .entityData = SerializeCustomEmojiId(id) };
}

void CustomEmojiLoader::cancel() {
	if (const auto lookup = std::get_if<Lookup>(&_state)) {
		base::take(lookup->process);
	} else if (const auto load = std::get_if<Load>(&_state)) {
		if (base::take(load->process)) {
			load->document->cancel();
		}
	}
}

Ui::CustomEmoji::Preview CustomEmojiLoader::preview() {
	using Preview = Ui::CustomEmoji::Preview;
	const auto make = [&](not_null<DocumentData*> document) -> Preview {
		const auto dimensions = document->dimensions;
		if (!document->inlineThumbnailIsPath()
			|| !dimensions.width()) {
			return {};
		}
		const auto scale = (SizeFromTag(_tag) * 1.)
			/ (style::DevicePixelRatio() * dimensions.width());
		return { document->createMediaView()->thumbnailPath(), scale };
	};
	if (const auto lookup = std::get_if<Lookup>(&_state)) {
		return make(lookup->document);
	} else if (const auto load = std::get_if<Load>(&_state)) {
		return make(load->document);
	}
	return {};
}

CustomEmojiManager::CustomEmojiManager(not_null<Session*> owner)
: _owner(owner)
, _repaintTimer([=] { invokeRepaints(); }) {
}

CustomEmojiManager::~CustomEmojiManager() = default;

std::unique_ptr<Ui::Text::CustomEmoji> CustomEmojiManager::create(
		QStringView data,
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
		auto loader = std::make_unique<CustomEmojiLoader>(
			_owner,
			parsed,
			SizeTag::Normal);
		if (loader->resolving()) {
			_loaders[parsed.id].push_back(base::make_weak(loader.get()));
		}
		const auto repaint = [=](
				not_null<Ui::CustomEmoji::Instance*> instance,
				Ui::CustomEmoji::RepaintRequest request) {
			repaintLater(instance, request);
		};
		j = i->second.emplace(
			parsed.id,
			std::make_unique<Ui::CustomEmoji::Instance>(Loading{
				std::move(loader),
				Ui::CustomEmoji::Preview()
			}, std::move(repaint))).first;
	}
	requestSetIfNeeded(parsed);

	return std::make_unique<Ui::CustomEmoji::Object>(
		j->second.get(),
		std::move(update));
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

void CustomEmojiManager::repaintLater(
		not_null<Ui::CustomEmoji::Instance*> instance,
		Ui::CustomEmoji::RepaintRequest request) {
	auto &bunch = _repaints[request.duration];
	if (bunch.when < request.when) {
		bunch.when = request.when;
	}
	bunch.instances.emplace_back(instance);
	scheduleRepaintTimer();
}

void CustomEmojiManager::scheduleRepaintTimer() {
	if (_repaintTimerScheduled) {
		return;
	}
	_repaintTimerScheduled = true;
	Ui::PostponeCall(this, [=] {
		_repaintTimerScheduled = false;

		auto next = crl::time();
		for (const auto &[duration, bunch] : _repaints) {
			if (!next || next > bunch.when) {
				next = bunch.when;
			}
		}
		if (next && (!_repaintNext || _repaintNext > next)) {
			const auto now = crl::now();
			if (now >= next) {
				_repaintNext = 0;
				_repaintTimer.cancel();
				invokeRepaints();
			} else {
				_repaintNext = next;
				_repaintTimer.callOnce(next - now);
			}
		}
	});
}

void CustomEmojiManager::invokeRepaints() {
	_repaintNext = 0;
	const auto now = crl::now();
	for (auto i = begin(_repaints); i != end(_repaints);) {
		if (i->second.when > now) {
			++i;
			continue;
		}
		auto bunch = std::move(i->second);
		i = _repaints.erase(i);
		for (const auto &weak : bunch.instances) {
			if (const auto strong = weak.get()) {
				strong->repaint();
			}
		}
	}
	scheduleRepaintTimer();
}

Main::Session &CustomEmojiManager::session() const {
	return _owner->session();
}

Session &CustomEmojiManager::owner() const {
	return *_owner;
}

QString SerializeCustomEmojiId(const CustomEmojiId &id) {
	return QString::number(id.set.id)
		+ '.'
		+ QString::number(id.set.accessHash)
		+ ':'
		+ QString::number(id.selfId)
		+ '/'
		+ QString::number(id.id);
}

QString SerializeCustomEmojiId(not_null<DocumentData*> document) {
	const auto sticker = document->sticker();
	return SerializeCustomEmojiId({
		.selfId = document->session().userId().bare,
		.id = document->id,
		.set = sticker ? sticker->set : StickerSetIdentifier(),
	});
}

CustomEmojiId ParseCustomEmojiData(QStringView data) {
	const auto components = data.split('.');
	if (components.size() != 2) {
		return {};
	}
	const auto parts = components[1].split(':');
	if (parts.size() != 2) {
		return {};
	}
	const auto endings = parts[1].split('/');
	if (endings.size() != 2) {
		return {};
	}
	return {
		.selfId = endings[0].toULongLong(),
		.id = endings[1].toULongLong(),
		.set = {
			.id = components[0].toULongLong(),
			.accessHash = parts[0].toULongLong(),
		},
	};
}

} // namespace Data
