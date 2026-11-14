#include "AsyncRenderService.h"
#include "MathSvgRenderer.h"
#include "MermaidSvgRenderer.h"

#include <QMutexLocker>
#include <QThread>
#include <algorithm>

namespace MarkdownTool::Render {

class RenderTask : public QRunnable {
public:
    RenderTask(const CacheKey &key, quint64 generation, AsyncRenderService *service)
        : m_key(key), m_generation(generation), m_service(service)
    {
        setAutoDelete(true);
    }

    void run() override {
        if (!m_service || m_service->isShuttingDown()) {
            return;
        }

        QByteArray svgData;
        if (m_key.kind == RenderKind::Math) {
            svgData = MathSvgRenderer::renderToSvg(m_key.source);
        } else {
            svgData = MermaidSvgRenderer::renderToSvg(m_key.source);
        }

        if (m_service && !m_service->isShuttingDown()) {
            m_service->onRenderCompleted(m_key, svgData, m_generation);
        }
    }

private:
    CacheKey m_key;
    quint64 m_generation = 0;
    AsyncRenderService *m_service = nullptr;
};

AsyncRenderService::AsyncRenderService(QObject *parent)
    : QObject(parent)
{
    m_threadPool.setMaxThreadCount(std::max(2, QThread::idealThreadCount()));
}

AsyncRenderService::~AsyncRenderService() {
    m_isShuttingDown.store(true);
    m_threadPool.clear();
    m_threadPool.waitForDone(2000);
}

AsyncRenderService *AsyncRenderService::instance() {
    static AsyncRenderService s_instance;
    return &s_instance;
}

void AsyncRenderService::onRenderCompleted(const CacheKey &key, const QByteArray &svgData, quint64 generation) {
    if (m_isShuttingDown.load()) {
        return;
    }

    QMetaObject::invokeMethod(this, [this, key, svgData, generation]() {
        if (m_isShuttingDown.load()) {
            return;
        }

        std::shared_ptr<QSvgRenderer> renderer;
        {
            QMutexLocker locker(&m_mutex);
            // If generation changed due to clearCache(), discard result
            if (generation != m_currentGeneration) {
                m_pendingRequests.remove(key);
                return;
            }

            renderer = std::make_shared<QSvgRenderer>(svgData);
            m_rendererCache[key] = renderer;
            m_pendingRequests.remove(key);

            // Update LRU
            m_lruList.removeAll(key);
            m_lruList.prepend(key);

            while (m_lruList.size() > m_maxCacheEntries) {
                CacheKey oldest = m_lruList.takeLast();
                m_rendererCache.remove(oldest);
            }
        }

        emit renderReady(key.kind, key.source);
    }, Qt::QueuedConnection);
}

bool AsyncRenderService::isCached(const QString &source, RenderKind kind) const {
    QMutexLocker locker(&m_mutex);
    return m_rendererCache.contains(CacheKey{kind, source});
}

int AsyncRenderService::cacheCount() const {
    QMutexLocker locker(&m_mutex);
    return m_rendererCache.size();
}

void AsyncRenderService::setMaxCacheEntries(int maxEntries) {
    QMutexLocker locker(&m_mutex);
    m_maxCacheEntries = std::max(4, maxEntries);
    while (m_lruList.size() > m_maxCacheEntries) {
        CacheKey oldest = m_lruList.takeLast();
        m_rendererCache.remove(oldest);
    }
}

std::shared_ptr<QSvgRenderer> AsyncRenderService::getRenderer(const QString &source, RenderKind kind, bool allowSyncFallback) {
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
            m_lruList.removeAll(key);
            m_lruList.prepend(key);
            while (m_lruList.size() > m_maxCacheEntries) {
                CacheKey oldest = m_lruList.takeLast();
                m_rendererCache.remove(oldest);
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
        if (m_rendererCache.contains(key) || m_pendingRequests.contains(key)) {
            return;
        }
        m_pendingRequests.insert(key);
        gen = m_currentGeneration;
    }

    m_threadPool.start(new RenderTask(key, gen, this));
}

void AsyncRenderService::clearCache() {
    QMutexLocker locker(&m_mutex);
    m_currentGeneration++;
    m_rendererCache.clear();
    m_lruList.clear();
    m_pendingRequests.clear();
    m_threadPool.clear();
}

} // namespace MarkdownTool::Render
