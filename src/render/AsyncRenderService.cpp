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

void AsyncRenderService::evictIfNeededLocked() {
    while (!m_lruList.isEmpty() && m_lruList.size() > m_maxCacheEntries) {
        CacheKey oldest = m_lruList.takeLast();
        m_rendererCache.remove(oldest);
        m_totalCachedBytes -= m_rendererByteSizes.take(oldest);
    }
    while (m_lruList.size() > 1 && m_totalCachedBytes > m_maxCacheBytes) {
        CacheKey oldest = m_lruList.takeLast();
        m_rendererCache.remove(oldest);
        m_totalCachedBytes -= m_rendererByteSizes.take(oldest);
    }
}

void AsyncRenderService::commitToCacheLocked(const CacheKey &key, const std::shared_ptr<QSvgRenderer> &renderer, qint64 byteSize, quint64 generation) {
    // Stale generation check: if clearCache() bumped the generation,
    // discard the result. DO NOT remove key from m_pendingRequests if a newer generation owns it!
    if (generation != m_currentGeneration) {
        auto it = m_pendingRequests.find(key);
        if (it != m_pendingRequests.end() && it.value() == generation) {
            m_pendingRequests.erase(it);
        }
        return;
    }

    // Deduct old size if key already exists in cache to prevent duplicate counting
    if (m_rendererByteSizes.contains(key)) {
        m_totalCachedBytes -= m_rendererByteSizes.value(key);
    }

    m_rendererCache[key] = renderer;
    m_rendererByteSizes[key] = byteSize;
    m_totalCachedBytes += byteSize;

    auto it = m_pendingRequests.find(key);
    if (it != m_pendingRequests.end() && it.value() == generation) {
        m_pendingRequests.erase(it);
    }

    // Update LRU
    m_lruList.removeAll(key);
    m_lruList.prepend(key);

    evictIfNeededLocked();
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

        bool committed = false;
        {
            QMutexLocker locker(&m_mutex);
            if (generation == m_currentGeneration) {
                auto renderer = std::make_shared<QSvgRenderer>(svgData);
                commitToCacheLocked(key, renderer, svgData.size(), generation);
                committed = true;
            } else {
                auto it = m_pendingRequests.find(key);
                if (it != m_pendingRequests.end() && it.value() == generation) {
                    m_pendingRequests.erase(it);
                }
            }
        }

        if (committed) {
            emit renderReady(key.kind, key.source);
        }
    }, Qt::QueuedConnection);
}

bool AsyncRenderService::isCached(const QString &source, RenderKind kind) const {
    QMutexLocker locker(&m_mutex);
    return m_rendererCache.contains(CacheKey{kind, source});
}

bool AsyncRenderService::isPending(const QString &source, RenderKind kind) const {
    QMutexLocker locker(&m_mutex);
    CacheKey key{kind, source};
    auto it = m_pendingRequests.constFind(key);
    return (it != m_pendingRequests.cend() && it.value() == m_currentGeneration);
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

qint64 AsyncRenderService::maxCacheBytes() const {
    QMutexLocker locker(&m_mutex);
    return m_maxCacheBytes;
}

quint64 AsyncRenderService::currentGeneration() const {
    QMutexLocker locker(&m_mutex);
    return m_currentGeneration;
}

void AsyncRenderService::setMaxCacheEntries(int maxEntries) {
    QMutexLocker locker(&m_mutex);
    m_maxCacheEntries = std::max(4, maxEntries);
    evictIfNeededLocked();
}

void AsyncRenderService::setMaxCacheBytes(qint64 maxBytes) {
    QMutexLocker locker(&m_mutex);
    m_maxCacheBytes = std::max<qint64>(1024, maxBytes);
    evictIfNeededLocked();
}

std::shared_ptr<QSvgRenderer> AsyncRenderService::getRenderer(const QString &source, RenderKind kind, bool allowSyncFallback) {
    if (m_isShuttingDown.load()) {
        return nullptr;
    }

    CacheKey key{kind, source};
    quint64 startGen = 0;
    {
        QMutexLocker locker(&m_mutex);
        auto it = m_rendererCache.find(key);
        if (it != m_rendererCache.end()) {
            m_lruList.removeAll(key);
            m_lruList.prepend(key);
            return it.value();
        }
        startGen = m_currentGeneration;
    }

    if (allowSyncFallback) {
        QByteArray svg = (kind == RenderKind::Math) ? MathSvgRenderer::renderToSvg(source)
                                                    : MermaidSvgRenderer::renderToSvg(source);
        auto renderer = std::make_shared<QSvgRenderer>(svg);
        if (m_testPreCommitHook) {
            m_testPreCommitHook(startGen);
        }
        {
            QMutexLocker locker(&m_mutex);
            if (!m_isShuttingDown.load()) {
                commitToCacheLocked(key, renderer, svg.size(), startGen);
            }
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
        auto it = m_pendingRequests.constFind(key);
        if (it != m_pendingRequests.cend() && it.value() == m_currentGeneration) {
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
