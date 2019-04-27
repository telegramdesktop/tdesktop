/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "lottie/lottie_frame_renderer.h"

#include "lottie/lottie_animation.h"

#include <QImage>
#include <QPainter>
#include <QHash>
#include <QMutexLocker>
#include <QLoggingCategory>
#include <QThread>

#include <QJsonDocument>
#include <QJsonArray>

#include <QtBodymovin/private/bmconstants_p.h>
#include <QtBodymovin/private/bmbase_p.h>
#include <QtBodymovin/private/bmlayer_p.h>

#include "rasterrenderer/lottierasterrenderer.h"

Q_LOGGING_CATEGORY(lcLottieQtBodymovinRenderThread, "qt.lottieqt.bodymovin.render.thread");

namespace Lottie {
//
//FrameRenderer *FrameRenderer::_rendererInstance = nullptr;
//
//FrameRenderer::~FrameRenderer()
//{
//	QMutexLocker mlocker(&_mutex);
//    qDeleteAll(_animData);
//    qDeleteAll(_frameCache);
//}
//
//FrameRenderer *FrameRenderer::instance()
//{
//    if (!_rendererInstance)
//        _rendererInstance = new FrameRenderer;
//
//    return _rendererInstance;
//}
//
//void FrameRenderer::deleteInstance()
//{
//    delete _rendererInstance;
//    _rendererInstance = nullptr;
//}
//
//void FrameRenderer::registerAnimator(Animation *animator)
//{
//    QMutexLocker mlocker(&_mutex);
//
//    qCDebug(lcLottieQtBodymovinRenderThread) << "Register Animator:"
//                                       << static_cast<void*>(animator);
//
//    Entry *entry = new Entry;
//    entry->animator = animator;
//    entry->startFrame = animator->startFrame();
//    entry->endFrame = animator->endFrame();
//    entry->currentFrame = animator->startFrame();
//	entry->animDir = animator->direction();
//    entry->bmTreeBlueprint = new BMBase;
//    parse(entry->bmTreeBlueprint, animator->jsonSource());
//    _animData.insert(animator, entry);
//    _waitCondition.wakeAll();
//}
//
//void FrameRenderer::deregisterAnimator(Animation *animator)
//{
//    QMutexLocker mlocker(&_mutex);
//
//    qCDebug(lcLottieQtBodymovinRenderThread) << "Deregister Animator:"
//                                       << static_cast<void*>(animator);
//
//    Entry *entry = _animData.value(animator, nullptr);
//    if (entry) {
//        qDeleteAll(entry->frameCache);
//        delete entry->bmTreeBlueprint;
//        delete entry;
//        _animData.remove(animator);
//    }
//}
//
//bool FrameRenderer::gotoFrame(Animation *animator, int frame)
//{
//	QMutexLocker mlocker(&_mutex);
//    Entry *entry = _animData.value(animator, nullptr);
//    if (entry) {
//        qCDebug(lcLottieQtBodymovinRenderThread) << "Animator:"
//                                           << static_cast<void*>(animator)
//                                           << "Goto frame:" << frame;
//        entry->currentFrame = frame;
//		entry->animDir = animator->direction();
//        pruneFrameCache(entry);
//        _waitCondition.wakeAll();
//        return true;
//    }
//    return false;
//}
//
//FrameRenderer::FrameRenderer() : QThread() {
//	const QByteArray cacheStr = qgetenv("QLOTTIE_RENDER_CACHE_SIZE");
//	int cacheSize = cacheStr.toInt();
//	if (cacheSize > 0) {
//		qCDebug(lcLottieQtBodymovinRenderThread) << "Setting frame cache size to" << cacheSize;
//		_cacheSize = cacheSize;
//	}
//}
//
//void FrameRenderer::pruneFrameCache(Entry* e)
//{
//    QHash<int, BMBase*>::iterator it = e->frameCache.begin();
//
//    while (it != e->frameCache.end()) {
//        if (it.key() == e->currentFrame) {
//            ++it;
//        } else {
//            delete it.value();
//            it = e->frameCache.erase(it);
//        }
//    }
//}
//
//BMBase *FrameRenderer::getFrame(Animation *animator, int frameNumber)
//{
//    QMutexLocker mlocker(&_mutex);
//
//    Entry *entry = _animData.value(animator, nullptr);
//    if (entry)
//        return entry->frameCache.value(frameNumber, nullptr);
//    else
//        return nullptr;
//}
//
//void FrameRenderer::prerender(Entry *animEntry)
//{
//    while (animEntry->frameCache.count() < _cacheSize) {
//        if (!animEntry->frameCache.contains(animEntry->currentFrame)) {
//            BMBase *bmTree = new BMBase(*animEntry->bmTreeBlueprint);
//
//            for (BMBase *elem : bmTree->children()) {
//                if (elem->active(animEntry->currentFrame))
//                    elem->updateProperties( animEntry->currentFrame);
//            }
//
//            animEntry->frameCache.insert( animEntry->currentFrame, bmTree);
//        }
//
//        qCDebug(lcLottieQtBodymovinRenderThread) << "Animator:"
//                                           << static_cast<void*>(animEntry->animator)
//                                           << "Frame drawn to cache. FN:"
//                                           << animEntry->currentFrame;
//        emit frameReady(animEntry->animator,  animEntry->currentFrame);
//
//        animEntry->currentFrame += animEntry->animDir;
//
//        if (animEntry->currentFrame > animEntry->endFrame) {
//            animEntry->currentFrame = animEntry->startFrame;
//        } else if (animEntry->currentFrame < animEntry->startFrame) {
//            animEntry->currentFrame = animEntry->endFrame;
//        }
//    }
//}
//
//void FrameRenderer::frameRendered(Animation *animator, int frameNumber)
//{
//	QMutexLocker mlocker(&_mutex);
//	Entry *entry = _animData.value(animator, nullptr);
//    if (entry) {
//        qCDebug(lcLottieQtBodymovinRenderThread) << "Animator:" << static_cast<void*>(animator)
//                                           << "Remove frame from cache" << frameNumber;
//
//        BMBase *root = entry->frameCache.value(frameNumber, nullptr);
//        delete root;
//        entry->frameCache.remove(frameNumber);
//        _waitCondition.wakeAll();
//    }
//}
//
//void FrameRenderer::run()
//{
//    qCDebug(lcLottieQtBodymovinRenderThread) << "rendering thread" << QThread::currentThread();
//
//	while (!isInterruptionRequested()) {
//		QMutexLocker mlocker(&_mutex);
//
//		for (Entry *e : qAsConst(_animData))
//			prerender(e);
//
//		_waitCondition.wait(&_mutex);
//	}
//}
//
//int FrameRenderer::parse(BMBase* rootElement, const QByteArray &jsonSource)
//{
//	QJsonDocument doc = QJsonDocument::fromJson(jsonSource);
//	QJsonObject rootObj = doc.object();
//
//	if (rootObj.empty())
//		return -1;
//
//	QJsonArray jsonLayers = rootObj.value(QLatin1String("layers")).toArray();
//	QJsonArray::const_iterator jsonLayerIt = jsonLayers.constEnd();
//	while (jsonLayerIt != jsonLayers.constBegin()) {
//		jsonLayerIt--;
//		QJsonObject jsonLayer = (*jsonLayerIt).toObject();
//		BMLayer *layer = BMLayer::construct(jsonLayer);
//		if (layer) {
//			layer->setParent(rootElement);
//			// Mask layers must be rendered before the layers they affect to
//			// although they appear before in layer hierarchy. For this reason
//			// move a mask after the affected layers, so it will be rendered first
//			if (layer->isMaskLayer())
//				rootElement->prependChild(layer);
//			else
//				rootElement->appendChild(layer);
//		}
//	}
//
//	return 0;
//}

} // namespace Lottie
