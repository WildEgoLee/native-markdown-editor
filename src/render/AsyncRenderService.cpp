#include "AsyncRenderService.h"
#include "MathSvgRenderer.h"
#include "MermaidSvgRenderer.h"

#include <QMutexLocker>
#include <QThread>
#include <algorithm>

namespace MarkdownTool::Render {

class RenderTask : public QRunnable {
public:
    RenderTask(const CacheKey &key, quint64 generation, AsyncRenderService *service, std::weak_ptr<std::atomic<bool>> token)
        : m_key(key), m_generation(generation), m_service(service), m_token(token)
    {
        setAutoDelete(true);
    }

    void run() override {
        auto alive = m_token.lock();
        if (!alive || !alive->load() || !m_service || m_service->isShuttingDown()) {
            return;
        }

        QByteArray svgData;
        if (m_key.kind == RenderKind::Math) {
            svgData = MathSvgRenderer::renderToSvg(m_key.source);
        } else {
            svgData = MermaidSvgRenderer::renderToSvg(m_key.source);
        }

        alive = m_token.lock();
        if (!alive || !alive->load() || !m_service || m_service->isShuttingDown()) {
            return;
        }

        m_service->onRenderCompleted(m_key, svgData, m_generation);
    }

private:
    CacheKey m_key;
    quint64 m_generation = 0;
    AsyncRenderService *m_service = nullptr;
    std::weak_ptr<std::atomic<bool>> m_token;
};

AsyncRenderService::AsyncRenderService(QObject *parent)
    : QObject(parent),
      m_aliveToken(std::make_shared<std::atomic<bool>>(true))
{
    m_threadPool.setMaxThreadCount(std::max(2, QThread::idealThreadCount()));
}

AsyncRenderService::~AsyncRenderService() {
    m_isShuttingDown.store(true);
    if (m_aliveToken) {
        m_aliveToken->store(false);
    }
    m_threadPool.clear();
    bool finished = m_threadPool.waitForDone(5000);
    if (!finished) {
        qWarning("AsyncRenderService::~AsyncRenderService: thread pool did not finish within 5000ms");
    }
}

AsyncRenderService *AsyncRenderService::instance() {
    static AsyncRenderService s_instance;
    return &s_instance;
}

void AsyncRenderService::onRenderCompleted(const CacheKey &key, const QByteArray &svgData, quint64 generation) {
    if (m_isShuttingDown.load()) {
        return;
    }

    QMetaObject::invokeMethod(this, [this, key, svgData, generation, weakToken = std::weak_ptr<std::atomic<bool>>(m_aliveToken)]() {
        auto token = weakToken.lock();
        if (!token || !token->load() || m_isShuttingDown.load()) {
            return;
        }

        std::shared_ptr<QSvgRenderer> renderer;
        {
            QMutexLocker locker(&m_mutex);
            // Stale generation check: if clearCache() bumped the generation,
            // discard the result. DO NOT remove key from m_pendingRequests if a newer generation owns it!
            if (generation != m_currentGeneration) {
                if (m_pendingRequests.value(key, 0) == generation) {
                    m_pendingRequests.remove(key);
                }
                return;
            }

            renderer = std::make_shared<QSvgRenderer>(svgData);
            m_rendererCache[key] = renderer;
            qint64 byteSize = svgData.size();
            m_rendererByteSizes[key] = byteSize;
            m_totalCachedBytes += byteSize;

            if (m_pendingRequests.value(key, 0) == generation) {
                m_pendingRequests.remove(key);
            }

            // Update LRU
            m_lruList.removeAll(key);
            m_lruList.prepend(key);

            while (m_lruList.size() > m_maxCacheEntries) {
                CacheKey oldest = m_lruList.takeLast();
                m_rendererCache.remove(oldest);
                m_totalCachedBytes -= m_rendererByteSizes.take(oldest);
            }
        }

        emit renderReady(key.kind, key.source);
    }, Qt::QueuedConnection);
}

bool AsyncRenderService::isCached(const QString &source, RenderKind kind) const {
    QMutexLocker locker(&m_mutex);
    return m_rendererCache.contains(CacheKey{kind, source});
}

bool AsyncRenderService::isPending(const QString &source, RenderKind kind) const {
    QMutexLocker locker(&m_mutex);
    CacheKey key{kind, source};
    return m_pendingRequests.value(key, 0) == m_currentGeneration;
}

int AsyncRenderService::cacheCount() const {
    QMutexLocker locker(&m_mutex);
    return m_rendererCache.size();
}

int AsyncRenderService::maxCacheEntries() const {
    QMutexLocker locker(&m_mutex);
    return m_maxCacheEntries;
}

qint64 AsyncRenderService::totalCachedBytes() const {
    QMutexLocker locker(&m_mutex);
    return m_totalCachedBytes;
}

quint64 AsyncRenderService::currentGeneration() const {
    QMutexLocker locker(&m_mutex);
    return m_currentGeneration;
}

void AsyncRenderService::setMaxCacheEntries(int maxEntries) {
    QMutexLocker locker(&m_mutex);
    m_maxCacheEntries = std::max(4, maxEntries);
    while (m_lruList.size() > m_maxCacheEntries) {
        CacheKey oldest = m_lruList.takeLast();
        m_rendererCache.remove(oldest);
        m_totalCachedBytes -= m_rendererByteSizes.take(oldest);
    }
}

std::shared_ptr<QSvgRenderer> AsyncRenderService::getRenderer(const QString &source, RenderKind kind, bool allowSyncFallback) {
    if (m_isShuttingDown.load()) {
        return nullptr;
    }

    CacheKey key{kind, source};
    {
        QMutexLocker locker(&m_mutex);
        auto it = m_rendererCache.find(key);
        if (it != m_rendererCache.end()) {
            m_lruList.removeAll(key);
            m_lruList.prepend(key);
            return it.value();
        }
    }

    if (allowSyncFallback) {
        QByteArray svg = (kind == RenderKind::Math) ? MathSvgRenderer::renderToSvg(source)
                                                    : MermaidSvgRenderer::renderToSvg(source);
        auto renderer = std::make_shared<QSvgRenderer>(svg);
        {
            QMutexLocker locker(&m_mutex);
            m_rendererCache[key] = renderer;
            qint64 byteSize = svg.size();
            m_rendererByteSizes[key] = byteSize;
            m_totalCachedBytes += byteSize;
            m_lruList.removeAll(key);
            m_lruList.prepend(key);
            while (m_lruList.size() > m_maxCacheEntries) {
                CacheKey oldest = m_lruList.takeLast();
                m_rendererCache.remove(oldest);
                m_totalCachedBytes -= m_rendererByteSizes.take(oldest);
            }
            m_pendingRequests.remove(key);
        }
        return renderer;
    }

    requestRender(source, kind);
    return nullptr;
}

void AsyncRenderService::requestRender(const QString &source, RenderKind kind) {
    if (m_isShuttingDown.load()) {
        return;
    }

    CacheKey key{kind, source};
    quint64 gen = 0;
    {
        QMutexLocker locker(&m_mutex);
        if (m_rendererCache.contains(key)) {
            return;
        }
        if (m_pendingRequests.value(key, 0) == m_currentGeneration) {
            return;
        }
        m_pendingRequests[key] = m_currentGeneration;
        gen = m_currentGeneration;
    }

    m_threadPool.start(new RenderTask(key, gen, this, m_aliveToken));
}

void AsyncRenderService::clearCache() {
    QMutexLocker locker(&m_mutex);
    m_currentGeneration++;
    m_rendererCache.clear();
    m_rendererByteSizes.clear();
    m_totalCachedBytes = 0;
    m_lruList.clear();
    m_pendingRequests.clear();
    m_threadPool.clear();
}

} // namespace MarkdownTool::Render
