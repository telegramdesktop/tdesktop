/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <QHash>
#include <QThread>
#include <QMutex>
#include <QWaitCondition>

class BMBase;
class QImage;

namespace Lottie {

class Animation;
//
//class FrameRenderer : public QThread {
//    Q_OBJECT
//
//    struct Entry {
//        Animation* animator = nullptr;
//        BMBase *bmTreeBlueprint = nullptr;
//        int startFrame = 0;
//        int endFrame = 0;
//        int currentFrame = 0;
//        int animDir = 1;
//        QHash<int, BMBase*> frameCache;
//    };
//
//public:
//    ~FrameRenderer();
//
//	FrameRenderer(const FrameRenderer &other) = delete;
//    void operator=(const FrameRenderer &other) = delete;
//
//    static FrameRenderer *instance();
//    static void deleteInstance();
//
//    BMBase *getFrame(Animation *animator, int frameNumber);
//
//signals:
//    void frameReady(Animation *animator, int frameNumber);
//
//public slots:
//    void registerAnimator(Animation *animator);
//    void deregisterAnimator(Animation *animator);
//
//    bool gotoFrame(Animation *animator, int frame);
//
//    void frameRendered(Animation *animator, int frameNumber);
//
//protected:
//    void run() override;
//
//    int parse(BMBase* rootElement, const QByteArray &jsonSource);
//
//    void prerender(Entry *animEntry);
//
//protected:
//    QHash<Animation*, Entry*> _animData;
//    int _cacheSize = 2;
//    int _currentFrame = 0;
//
//    Animation *_animation = nullptr;
//    QHash<int, QImage*> _frameCache;
//
//private:
//	FrameRenderer();
//
//    void pruneFrameCache(Entry* e);
//
//private:
//    static FrameRenderer *_rendererInstance;
//
//    QMutex _mutex;
//    QWaitCondition _waitCondition;
//};

} // namespace Lottie
