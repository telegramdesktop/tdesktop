/****************************************************************************
**
** Copyright (C) 2013 Digia Plc and/or its subsidiary(-ies).
** Contact: http://www.qt-project.org/legal
**
** This file is part of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and Digia.  For licensing terms and
** conditions see http://qt.digia.com/licensing.  For further information
** use the contact form at http://qt.digia.com/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU Lesser General Public License version 2.1 requirements
** will be met: http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** In addition, as a special exception, Digia gives you certain additional
** rights.  These rights are described in the Digia Qt LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3.0 as published by the Free Software
** Foundation and appearing in the file LICENSE.GPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU General Public License version 3.0 requirements will be
** met: http://www.gnu.org/copyleft/gpl.html.
**
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include "avfmediaplayersession.h"
#include "avfmediaplayerservice.h"
#include "avfvideooutput.h"

#import <AVFoundation/AVFoundation.h>

QT_USE_NAMESPACE

//AVAsset Keys
static NSString* const AVF_TRACKS_KEY       = @"tracks";
static NSString* const AVF_PLAYABLE_KEY     = @"playable";

//AVPlayerItem keys
static NSString* const AVF_STATUS_KEY       = @"status";

//AVPlayer keys
static NSString* const AVF_RATE_KEY         = @"rate";
static NSString* const AVF_CURRENT_ITEM_KEY = @"currentItem";

static void *AVFMediaPlayerSessionObserverRateObservationContext = &AVFMediaPlayerSessionObserverRateObservationContext;
static void *AVFMediaPlayerSessionObserverStatusObservationContext = &AVFMediaPlayerSessionObserverStatusObservationContext;
static void *AVFMediaPlayerSessionObserverCurrentItemObservationContext = &AVFMediaPlayerSessionObserverCurrentItemObservationContext;

@interface AVFMediaPlayerSessionObserver : NSObject
{
@private
    AVFMediaPlayerSession *m_session;
    AVPlayer *m_player;
    AVPlayerItem *m_playerItem;
    AVPlayerLayer *m_playerLayer;
    NSURL *m_URL;
    bool m_audioAvailable;
    bool m_videoAvailable;
}

@property (readonly, getter=player) AVPlayer* m_player;
@property (readonly, getter=playerItem) AVPlayerItem* m_playerItem;
@property (readonly, getter=playerLayer) AVPlayerLayer* m_playerLayer;
@property (readonly, getter=audioAvailable) bool m_audioAvailable;
@property (readonly, getter=videoAvailable) bool m_videoAvailable;
@property (readonly, getter=session) AVFMediaPlayerSession* m_session;

- (AVFMediaPlayerSessionObserver *) initWithMediaPlayerSession:(AVFMediaPlayerSession *)session;
- (void) setURL:(NSURL *)url;
- (void) unloadMedia;
- (void) prepareToPlayAsset:(AVURLAsset *)asset withKeys:(NSArray *)requestedKeys;
- (void) assetFailedToPrepareForPlayback:(NSError *)error;
- (void) playerItemDidReachEnd:(NSNotification *)notification;
- (void) playerItemTimeJumped:(NSNotification *)notification;
- (void) observeValueForKeyPath:(NSString *)keyPath ofObject:(id)object
                         change:(NSDictionary *)change context:(void *)context;
- (void) detatchSession;
- (void) dealloc;
@end

@implementation AVFMediaPlayerSessionObserver

@synthesize m_player, m_playerItem, m_playerLayer, m_audioAvailable, m_videoAvailable, m_session;

- (AVFMediaPlayerSessionObserver *) initWithMediaPlayerSession:(AVFMediaPlayerSession *)session
{
    if (!(self = [super init]))
        return nil;

    self->m_session = session;
    return self;
}

- (void) setURL:(NSURL *)url
{
    if (m_URL != url)
    {
        [m_URL release];
        m_URL = [url copy];

        //Create an asset for inspection of a resource referenced by a given URL.
        //Load the values for the asset keys "tracks", "playable".

        AVURLAsset *asset = [AVURLAsset URLAssetWithURL:m_URL options:nil];
        NSArray *requestedKeys = [NSArray arrayWithObjects:AVF_TRACKS_KEY, AVF_PLAYABLE_KEY, nil];

        // Tells the asset to load the values of any of the specified keys that are not already loaded.
        [asset loadValuesAsynchronouslyForKeys:requestedKeys completionHandler:
         ^{
             dispatch_async( dispatch_get_main_queue(),
                           ^{
                                [self prepareToPlayAsset:asset withKeys:requestedKeys];
                            });
         }];
    }
}

- (void) unloadMedia
{
    if (m_player)
        [m_player setRate:0.0];
    if (m_playerItem) {
        [m_playerItem removeObserver:self forKeyPath:AVF_STATUS_KEY];

        [[NSNotificationCenter defaultCenter] removeObserver:self
                                                    name:AVPlayerItemDidPlayToEndTimeNotification
                                                  object:m_playerItem];
        [[NSNotificationCenter defaultCenter] removeObserver:self
                                                    name:AVPlayerItemTimeJumpedNotification
                                                    object:m_playerItem];
        m_playerItem = 0;
    }
}

- (void) prepareToPlayAsset:(AVURLAsset *)asset
                   withKeys:(NSArray *)requestedKeys
{
    //Make sure that the value of each key has loaded successfully.
    for (NSString *thisKey in requestedKeys)
    {
        NSError *error = nil;
        AVKeyValueStatus keyStatus = [asset statusOfValueForKey:thisKey error:&error];
#ifdef QT_DEBUG_AVF
        qDebug() << Q_FUNC_INFO << [thisKey UTF8String] << " status: " << keyStatus;
#endif
        if (keyStatus == AVKeyValueStatusFailed)
        {
            [self assetFailedToPrepareForPlayback:error];
            return;
        }
    }

    //Use the AVAsset playable property to detect whether the asset can be played.
#ifdef QT_DEBUG_AVF
    qDebug() << Q_FUNC_INFO << "isPlayable: " << [asset isPlayable];
#endif
    if (!asset.playable)
    {
        //Generate an error describing the failure.
        NSString *localizedDescription = NSLocalizedString(@"Item cannot be played", @"Item cannot be played description");
        NSString *localizedFailureReason = NSLocalizedString(@"The assets tracks were loaded, but could not be made playable.", @"Item cannot be played failure reason");
        NSDictionary *errorDict = [NSDictionary dictionaryWithObjectsAndKeys:
                localizedDescription, NSLocalizedDescriptionKey,
                localizedFailureReason, NSLocalizedFailureReasonErrorKey,
                nil];
        NSError *assetCannotBePlayedError = [NSError errorWithDomain:@"StitchedStreamPlayer" code:0 userInfo:errorDict];

        [self assetFailedToPrepareForPlayback:assetCannotBePlayedError];

        return;
    }

    m_audioAvailable = false;
    m_videoAvailable = false;

    //Check each track of asset for audio and video content
    NSArray *tracks = [asset tracks];
    for (AVAssetTrack *track in tracks) {
        if ([track hasMediaCharacteristic:AVMediaCharacteristicAudible])
            m_audioAvailable = true;
        if ([track hasMediaCharacteristic:AVMediaCharacteristicVisual])
            m_videoAvailable = true;
    }

    //At this point we're ready to set up for playback of the asset.
    //Stop observing our prior AVPlayerItem, if we have one.
    if (m_playerItem)
    {
        //Remove existing player item key value observers and notifications.
        [self unloadMedia];
    }

    //Create a new instance of AVPlayerItem from the now successfully loaded AVAsset.
    m_playerItem = [AVPlayerItem playerItemWithAsset:asset];

    //Observe the player item "status" key to determine when it is ready to play.
    [m_playerItem addObserver:self
                   forKeyPath:AVF_STATUS_KEY
                      options:NSKeyValueObservingOptionInitial | NSKeyValueObservingOptionNew
                      context:AVFMediaPlayerSessionObserverStatusObservationContext];

    //When the player item has played to its end time we'll toggle
    //the movie controller Pause button to be the Play button
    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(playerItemDidReachEnd:)
                                                 name:AVPlayerItemDidPlayToEndTimeNotification
                                               object:m_playerItem];

    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(playerItemTimeJumped:)
                                                 name:AVPlayerItemTimeJumpedNotification
                                               object:m_playerItem];


    //Clean up old player if we have one
    if (m_player) {
        [m_player setRate:0.0];
        [m_player removeObserver:self forKeyPath:AVF_CURRENT_ITEM_KEY];
        [m_player removeObserver:self forKeyPath:AVF_RATE_KEY];
        [m_player release];
        m_player = 0;

        if (m_playerLayer) {
            [m_playerLayer release];
            m_playerLayer = 0; //Will have been released
        }
    }

    //Get a new AVPlayer initialized to play the specified player item.
    m_player = [AVPlayer playerWithPlayerItem:m_playerItem];
    [m_player retain];

#if defined(Q_OS_OSX)
    //Set the initial volume on new player object
    if (self.session)
        m_player.volume = m_session->volume() / 100.0f;
#endif

    //Create a new player layer if we don't have one already
    if (!m_playerLayer)
    {
        m_playerLayer = [AVPlayerLayer playerLayerWithPlayer:m_player];
        [m_playerLayer retain];
        m_playerLayer.videoGravity = AVLayerVideoGravityResizeAspectFill;

        //Get the native size of the new item, and reset the bounds of the player layer
        AVAsset *asset = m_playerItem.asset;
        if (asset) {
            NSArray *tracks = [asset tracksWithMediaType:AVMediaTypeVideo];
            if ([tracks count]) {
                AVAssetTrack *videoTrack = [tracks objectAtIndex:0];
                m_playerLayer.anchorPoint = CGPointMake(0.0f, 0.0f);
                m_playerLayer.bounds = CGRectMake(0.0f, 0.0f, videoTrack.naturalSize.width, videoTrack.naturalSize.height);
            }
        }

    }

    //Observe the AVPlayer "currentItem" property to find out when any
    //AVPlayer replaceCurrentItemWithPlayerItem: replacement will/did
    //occur.
    [m_player addObserver:self
                          forKeyPath:AVF_CURRENT_ITEM_KEY
                          options:NSKeyValueObservingOptionInitial | NSKeyValueObservingOptionNew
                          context:AVFMediaPlayerSessionObserverCurrentItemObservationContext];

    //Observe the AVPlayer "rate" property to update the scrubber control.
    [m_player addObserver:self
                          forKeyPath:AVF_RATE_KEY
                          options:NSKeyValueObservingOptionInitial | NSKeyValueObservingOptionNew
                          context:AVFMediaPlayerSessionObserverRateObservationContext];

}

-(void) assetFailedToPrepareForPlayback:(NSError *)error
{
    Q_UNUSED(error)
    QMetaObject::invokeMethod(m_session, "processMediaLoadError", Qt::AutoConnection);
#ifdef QT_DEBUG_AVF
    qDebug() << Q_FUNC_INFO;
    qDebug() << [[error localizedDescription] UTF8String];
    qDebug() << [[error localizedFailureReason] UTF8String];
    qDebug() << [[error localizedRecoverySuggestion] UTF8String];
#endif
}

- (void) playerItemDidReachEnd:(NSNotification *)notification
{
    Q_UNUSED(notification)
    if (self.session)
        QMetaObject::invokeMethod(m_session, "processEOS", Qt::AutoConnection);
}

- (void) playerItemTimeJumped:(NSNotification *)notification
{
    Q_UNUSED(notification)
    if (self.session)
        QMetaObject::invokeMethod(m_session, "processPositionChange", Qt::AutoConnection);
}

- (void) observeValueForKeyPath:(NSString*) path
                       ofObject:(id)object
                         change:(NSDictionary*)change
                        context:(void*)context
{
    //AVPlayerItem "status" property value observer.
    if (context == AVFMediaPlayerSessionObserverStatusObservationContext)
    {
		AVPlayerStatus status = (AVPlayerStatus)[[change objectForKey:NSKeyValueChangeNewKey] integerValue];
        switch (status)
        {
            //Indicates that the status of the player is not yet known because
            //it has not tried to load new media resources for playback
            case AVPlayerStatusUnknown:
            {
                //QMetaObject::invokeMethod(m_session, "processLoadStateChange", Qt::AutoConnection);
            }
            break;

            case AVPlayerStatusReadyToPlay:
            {
                //Once the AVPlayerItem becomes ready to play, i.e.
                //[playerItem status] == AVPlayerItemStatusReadyToPlay,
                //its duration can be fetched from the item.
                if (self.session)
                    QMetaObject::invokeMethod(m_session, "processLoadStateChange", Qt::AutoConnection);
            }
            break;

            case AVPlayerStatusFailed:
            {
                AVPlayerItem *playerItem = (AVPlayerItem *)object;
                [self assetFailedToPrepareForPlayback:playerItem.error];

                if (self.session)
                    QMetaObject::invokeMethod(m_session, "processLoadStateChange", Qt::AutoConnection);
            }
            break;
        }
    }
    //AVPlayer "rate" property value observer.
    else if (context == AVFMediaPlayerSessionObserverRateObservationContext)
    {
        //QMetaObject::invokeMethod(m_session, "setPlaybackRate",  Qt::AutoConnection, Q_ARG(qreal, [m_player rate]));
    }
    //AVPlayer "currentItem" property observer.
    //Called when the AVPlayer replaceCurrentItemWithPlayerItem:
    //replacement will/did occur.
    else if (context == AVFMediaPlayerSessionObserverCurrentItemObservationContext)
    {
        AVPlayerItem *newPlayerItem = [change objectForKey:NSKeyValueChangeNewKey];
        if (m_playerItem != newPlayerItem)
        {
            m_playerItem = newPlayerItem;

            //Get the native size of the new item, and reset the bounds of the player layer
            //AVAsset *asset = m_playerItem.asset;
            AVAsset *asset = [m_playerItem asset];
            if (asset) {
                NSArray *tracks = [asset tracksWithMediaType:AVMediaTypeVideo];
                if ([tracks count]) {
                    AVAssetTrack *videoTrack = [tracks objectAtIndex:0];
                    m_playerLayer.anchorPoint = CGPointMake(0.0f, 0.0f);
                    m_playerLayer.bounds = CGRectMake(0.0f, 0.0f, videoTrack.naturalSize.width, videoTrack.naturalSize.height);
                }
            }

        }
        if (self.session)
            QMetaObject::invokeMethod(m_session, "processCurrentItemChanged", Qt::AutoConnection);
    }
    else
    {
        [super observeValueForKeyPath:path ofObject:object change:change context:context];
    }
}

- (void) detatchSession
{
#ifdef QT_DEBUG_AVF
        qDebug() << Q_FUNC_INFO;
#endif
        m_session = 0;
}

- (void) dealloc
{
#ifdef QT_DEBUG_AVF
        qDebug() << Q_FUNC_INFO;
#endif
    [self unloadMedia];

    if (m_player) {
        [m_player removeObserver:self forKeyPath:AVF_CURRENT_ITEM_KEY];
        [m_player removeObserver:self forKeyPath:AVF_RATE_KEY];
        [m_player release];
        m_player = 0;
    }

    if (m_playerLayer) {
        [m_playerLayer release];
        m_playerLayer = 0;
    }

    if (m_URL) {
        [m_URL release];
    }

    [super dealloc];
}

@end

AVFMediaPlayerSession::AVFMediaPlayerSession(AVFMediaPlayerService *service, QObject *parent)
    : QObject(parent)
    , m_service(service)
    , m_videoOutput(0)
    , m_state(QMediaPlayer::StoppedState)
    , m_mediaStatus(QMediaPlayer::NoMedia)
    , m_mediaStream(0)
    , m_muted(false)
    , m_tryingAsync(false)
    , m_volume(100)
    , m_rate(1.0)
    , m_duration(0)
    , m_videoAvailable(false)
    , m_audioAvailable(false)
{
    m_observer = [[AVFMediaPlayerSessionObserver alloc] initWithMediaPlayerSession:this];
}

AVFMediaPlayerSession::~AVFMediaPlayerSession()
{
#ifdef QT_DEBUG_AVF
    qDebug() << Q_FUNC_INFO;
#endif
    //Detatch the session from the sessionObserver (which could still be alive trying to communicate with this session).
    [(AVFMediaPlayerSessionObserver*)m_observer detatchSession];
    [(AVFMediaPlayerSessionObserver*)m_observer release];
}

void AVFMediaPlayerSession::setVideoOutput(AVFVideoOutput *output)
{
#ifdef QT_DEBUG_AVF
    qDebug() << Q_FUNC_INFO << output;
#endif

    if (m_videoOutput == output)
        return;

    //Set the current output layer to null to stop rendering
    if (m_videoOutput) {
        m_videoOutput->setLayer(0);
    }

    m_videoOutput = output;

    if (m_videoOutput && m_state != QMediaPlayer::StoppedState)
        m_videoOutput->setLayer([(AVFMediaPlayerSessionObserver*)m_observer playerLayer]);
}

void *AVFMediaPlayerSession::currentAssetHandle()
{
#ifdef QT_DEBUG_AVF
    qDebug() << Q_FUNC_INFO;
#endif
    AVAsset *currentAsset = [[(AVFMediaPlayerSessionObserver*)m_observer playerItem] asset];
    return currentAsset;
}

QMediaPlayer::State AVFMediaPlayerSession::state() const
{
    return m_state;
}

QMediaPlayer::MediaStatus AVFMediaPlayerSession::mediaStatus() const
{
    return m_mediaStatus;
}

QMediaContent AVFMediaPlayerSession::media() const
{
    return m_resources;
}

const QIODevice *AVFMediaPlayerSession::mediaStream() const
{
    return m_mediaStream;
}

void AVFMediaPlayerSession::setMedia(const QMediaContent &content, QIODevice *stream)
{
#ifdef QT_DEBUG_AVF
    qDebug() << Q_FUNC_INFO << content.canonicalUrl();
#endif

    m_resources = content;
    m_mediaStream = stream;

    QMediaPlayer::MediaStatus oldMediaStatus = m_mediaStatus;

    if (content.isNull() || content.canonicalUrl().isEmpty()) {
        [(AVFMediaPlayerSessionObserver*)m_observer unloadMedia];
        m_mediaStatus = QMediaPlayer::NoMedia;
        if (m_state != QMediaPlayer::StoppedState)
            Q_EMIT stateChanged(m_state = QMediaPlayer::StoppedState);

        if (m_mediaStatus != oldMediaStatus)
            Q_EMIT mediaStatusChanged(m_mediaStatus);
        Q_EMIT positionChanged(position());
        return;
    } else {

        m_mediaStatus = QMediaPlayer::LoadingMedia;
        if (m_mediaStatus != oldMediaStatus)
            Q_EMIT mediaStatusChanged(m_mediaStatus);
    }
    //Load AVURLAsset
    //initialize asset using content's URL
    NSString *urlString = [NSString stringWithUTF8String:content.canonicalUrl().toEncoded().constData()];
    NSURL *url = [NSURL URLWithString:urlString];
    [(AVFMediaPlayerSessionObserver*)m_observer setURL:url];
}

qint64 AVFMediaPlayerSession::position() const
{
    AVPlayerItem *playerItem = [(AVFMediaPlayerSessionObserver*)m_observer playerItem];

    if (!playerItem)
        return 0;

    CMTime time = [playerItem currentTime];
    return static_cast<quint64>(float(time.value) / float(time.timescale) * 1000.0f);
}

qint64 AVFMediaPlayerSession::duration() const
{
#ifdef QT_DEBUG_AVF
    qDebug() << Q_FUNC_INFO;
#endif
    AVPlayerItem *playerItem = [(AVFMediaPlayerSessionObserver*)m_observer playerItem];

    if (!playerItem)
        return 0;

    CMTime time = [playerItem duration];
    return static_cast<quint64>(float(time.value) / float(time.timescale) * 1000.0f);
}

int AVFMediaPlayerSession::bufferStatus() const
{
    //BUG: bufferStatus may be relevant?
#ifdef QT_DEBUG_AVF
    qDebug() << Q_FUNC_INFO;
#endif
    return 100;
}

int AVFMediaPlayerSession::volume() const
{
    return m_volume;
}

bool AVFMediaPlayerSession::isMuted() const
{
    return m_muted;
}

bool AVFMediaPlayerSession::isAudioAvailable() const
{
    return [(AVFMediaPlayerSessionObserver*)m_observer audioAvailable];
}

bool AVFMediaPlayerSession::isVideoAvailable() const
{
    return [(AVFMediaPlayerSessionObserver*)m_observer videoAvailable];
}

bool AVFMediaPlayerSession::isSeekable() const
{
    return true;
}

QMediaTimeRange AVFMediaPlayerSession::availablePlaybackRanges() const
{
    AVPlayerItem *playerItem = [(AVFMediaPlayerSessionObserver*)m_observer playerItem];

    if (playerItem) {
        QMediaTimeRange timeRanges;

        NSArray *ranges = [playerItem loadedTimeRanges];
        for (NSValue *timeRange in ranges) {
            CMTimeRange currentTimeRange = [timeRange CMTimeRangeValue];
            qint64 startTime = qint64(float(currentTimeRange.start.value) / currentTimeRange.start.timescale * 1000.0);
            timeRanges.addInterval(startTime, startTime + qint64(float(currentTimeRange.duration.value) / currentTimeRange.duration.timescale * 1000.0));
        }
        if (!timeRanges.isEmpty())
            return timeRanges;
    }
    return QMediaTimeRange(0, duration());
}

qreal AVFMediaPlayerSession::playbackRate() const
{
    return m_rate;
}

void AVFMediaPlayerSession::setPlaybackRate(qreal rate)
{
#ifdef QT_DEBUG_AVF
    qDebug() << Q_FUNC_INFO << rate;
#endif

    if (qFuzzyCompare(m_rate, rate))
        return;

    m_rate = rate;

    AVPlayer *player = [(AVFMediaPlayerSessionObserver*)m_observer player];

    if (player != 0 && m_state == QMediaPlayer::PlayingState) {
        [player setRate:m_rate];
    }
}

void AVFMediaPlayerSession::setPosition(qint64 pos)
{
#ifdef QT_DEBUG_AVF
    qDebug() << Q_FUNC_INFO << pos;
#endif

    if ( !isSeekable() || pos == position())
        return;

    AVPlayerItem *playerItem = [(AVFMediaPlayerSessionObserver*)m_observer playerItem];

    if (!playerItem)
        return;

    if (duration() > 0)
        pos = qMin(pos, duration());

    CMTime newTime = [playerItem currentTime];
    newTime.value = (pos / 1000.0f) * newTime.timescale;
    [playerItem seekToTime:newTime];

    //reset the EndOfMedia status position is changed after playback is finished
    if (m_mediaStatus == QMediaPlayer::EndOfMedia)
        processLoadStateChange();
}

void AVFMediaPlayerSession::play()
{
#ifdef QT_DEBUG_AVF
    qDebug() << Q_FUNC_INFO << "currently: " << m_state;
#endif

    if (m_state == QMediaPlayer::PlayingState)
        return;

    m_state = QMediaPlayer::PlayingState;

    if (m_videoOutput) {
        m_videoOutput->setLayer([(AVFMediaPlayerSessionObserver*)m_observer playerLayer]);
    }

    //reset the EndOfMedia status if the same file is played again
    if (m_mediaStatus == QMediaPlayer::EndOfMedia) {
        setPosition(0);
        processLoadStateChange();
    }

    if (m_mediaStatus == QMediaPlayer::LoadedMedia || m_mediaStatus == QMediaPlayer::BufferedMedia)
        [[(AVFMediaPlayerSessionObserver*)m_observer player] play];

    //processLoadStateChange();
    Q_EMIT stateChanged(m_state);
}

void AVFMediaPlayerSession::pause()
{
#ifdef QT_DEBUG_AVF
    qDebug() << Q_FUNC_INFO << "currently: " << m_state;
#endif

    if (m_state == QMediaPlayer::PausedState)
        return;

    m_state = QMediaPlayer::PausedState;

    if (m_videoOutput) {
        m_videoOutput->setLayer([(AVFMediaPlayerSessionObserver*)m_observer playerLayer]);
    }

    //reset the EndOfMedia status if the same file is played again
    if (m_mediaStatus == QMediaPlayer::EndOfMedia)
        processLoadStateChange();

    [[(AVFMediaPlayerSessionObserver*)m_observer player] pause];

    //processLoadStateChange();
    Q_EMIT stateChanged(m_state);
}

void AVFMediaPlayerSession::stop()
{
#ifdef QT_DEBUG_AVF
    qDebug() << Q_FUNC_INFO << "currently: " << m_state;
#endif

    if (m_state == QMediaPlayer::StoppedState)
        return;

    m_state = QMediaPlayer::StoppedState;
    m_rate = 0.0f;
    [[(AVFMediaPlayerSessionObserver*)m_observer player] setRate:m_rate];
    setPosition(0);

    if (m_videoOutput) {
        m_videoOutput->setLayer(0);
    }

    processLoadStateChange();
    Q_EMIT stateChanged(m_state);
    Q_EMIT positionChanged(position());
}

void AVFMediaPlayerSession::setVolume(int volume)
{
#ifdef QT_DEBUG_AVF
    qDebug() << Q_FUNC_INFO << volume;
#endif

    if (m_volume == volume)
        return;

    m_volume = volume;

#if defined(Q_OS_OSX)
    AVPlayer *player = [(AVFMediaPlayerSessionObserver*)m_observer player];
    if (player) {
        [[(AVFMediaPlayerSessionObserver*)m_observer player] setVolume:m_volume / 100.0f];
    }
#endif

    Q_EMIT volumeChanged(m_volume);
}

void AVFMediaPlayerSession::setMuted(bool muted)
{
#ifdef QT_DEBUG_AVF
    qDebug() << Q_FUNC_INFO << muted;
#endif
    if (m_muted == muted)
        return;

    m_muted = muted;
#if defined(Q_OS_OSX)
    [[(AVFMediaPlayerSessionObserver*)m_observer player] setMuted:m_muted];
#endif
    Q_EMIT mutedChanged(muted);
}

void AVFMediaPlayerSession::processEOS()
{
    //AVPlayerItem has reached end of track/stream
#ifdef QT_DEBUG_AVF
    qDebug() << Q_FUNC_INFO;
#endif
    Q_EMIT positionChanged(position());
    m_mediaStatus = QMediaPlayer::EndOfMedia;

    Q_EMIT stateChanged(m_state = QMediaPlayer::StoppedState);
    Q_EMIT mediaStatusChanged(m_mediaStatus);
}

void AVFMediaPlayerSession::processLoadStateChange()
{
    AVPlayerStatus currentStatus = [[(AVFMediaPlayerSessionObserver*)m_observer player] status];

#ifdef QT_DEBUG_AVF
    qDebug() << Q_FUNC_INFO << currentStatus;
#endif

    QMediaPlayer::MediaStatus newStatus = QMediaPlayer::NoMedia;
    bool isPlaying = (m_state != QMediaPlayer::StoppedState);

    if (currentStatus == AVPlayerStatusReadyToPlay) {
        qint64 currentDuration = duration();
        if (m_duration != currentDuration)
            Q_EMIT durationChanged(m_duration = currentDuration);

        if (m_audioAvailable != isAudioAvailable())
            Q_EMIT audioAvailableChanged(m_audioAvailable = !m_audioAvailable);

        if (m_videoAvailable != isVideoAvailable())
            Q_EMIT videoAvailableChanged(m_videoAvailable = !m_videoAvailable);

        newStatus = isPlaying ? QMediaPlayer::BufferedMedia : QMediaPlayer::LoadedMedia;

        if (m_state == QMediaPlayer::PlayingState && [(AVFMediaPlayerSessionObserver*)m_observer player]) {
            [[(AVFMediaPlayerSessionObserver*)m_observer player] setRate:m_rate];
            [[(AVFMediaPlayerSessionObserver*)m_observer player] play];
        }
    }

    if (newStatus != m_mediaStatus)
        Q_EMIT mediaStatusChanged(m_mediaStatus = newStatus);
}

void AVFMediaPlayerSession::processPositionChange()
{
    Q_EMIT positionChanged(position());
}

void AVFMediaPlayerSession::processMediaLoadError()
{
    Q_EMIT error(QMediaPlayer::FormatError, tr("Failed to load media"));
    Q_EMIT mediaStatusChanged(m_mediaStatus = QMediaPlayer::InvalidMedia);
    Q_EMIT stateChanged(m_state = QMediaPlayer::StoppedState);
}

void AVFMediaPlayerSession::processCurrentItemChanged()
{
#ifdef QT_DEBUG_AVF
    qDebug() << Q_FUNC_INFO;
#endif

    AVPlayerLayer *playerLayer = [(AVFMediaPlayerSessionObserver*)m_observer playerLayer];

    if (m_videoOutput && m_state != QMediaPlayer::StoppedState) {
        m_videoOutput->setLayer(playerLayer);
    }

}
