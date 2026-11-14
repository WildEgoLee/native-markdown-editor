#include "AsyncRenderService.h"
#include "MathSvgRenderer.h"
#include "MermaidSvgRenderer.h"

#include <QThreadPool>
#include <QRunnable>
#include <QMutexLocker>

namespace MarkdownTool::Render {

class RenderTask : public QRunnable {
public:
    RenderTask(const QString &source, RenderKind kind, AsyncRenderService *service)
        : m_source(source), m_kind(kind), m_service(service)
    {
        setAutoDelete(true);
    }

    void run() override {
        QByteArray svgData;
        if (m_kind == RenderKind::Math) {
            svgData = MathSvgRenderer::renderToSvg(m_source);
        } else {
            svgData = MermaidSvgRenderer::renderToSvg(m_source);
        }

        if (m_service) {
            m_service->onRenderCompleted(m_source, svgData);
        }
    }

private:
    QString m_source;
    RenderKind m_kind;
    AsyncRenderService *m_service = nullptr;
};

AsyncRenderService::AsyncRenderService(QObject *parent)
    : QObject(parent)
{
}

void AsyncRenderService::onRenderCompleted(const QString &source, const QByteArray &svgData) {
    QMetaObject::invokeMethod(this, [this, source, svgData]() {
        auto renderer = std::make_shared<QSvgRenderer>(svgData);
        {
            QMutexLocker locker(&m_mutex);
            m_rendererCache[source] = renderer;
            m_pendingRequests.remove(source);
        }
        emit renderReady(source);
    }, Qt::QueuedConnection);
}

AsyncRenderService *AsyncRenderService::instance() {
    static AsyncRenderService s_instance;
    return &s_instance;
}

bool AsyncRenderService::isCached(const QString &source) const {
    QMutexLocker locker(&m_mutex);
    return m_rendererCache.contains(source);
}

std::shared_ptr<QSvgRenderer> AsyncRenderService::getRenderer(const QString &source, RenderKind kind, bool allowSyncFallback) {
    {
        QMutexLocker locker(&m_mutex);
        if (m_rendererCache.contains(source)) {
            return m_rendererCache.value(source);
        }
    }

    if (allowSyncFallback) {
        QByteArray svg = (kind == RenderKind::Math) ? MathSvgRenderer::renderToSvg(source)
                                                    : MermaidSvgRenderer::renderToSvg(source);
        auto renderer = std::make_shared<QSvgRenderer>(svg);
        {
            QMutexLocker locker(&m_mutex);
            m_rendererCache[source] = renderer;
            m_pendingRequests.remove(source);
        }
        return renderer;
    }

    requestRender(source, kind);
    return nullptr;
}

void AsyncRenderService::requestRender(const QString &source, RenderKind kind) {
    {
        QMutexLocker locker(&m_mutex);
        if (m_rendererCache.contains(source) || m_pendingRequests.contains(source)) {
            return;
        }
        m_pendingRequests.insert(source);
    }

    QThreadPool::globalInstance()->start(new RenderTask(source, kind, this));
}

void AsyncRenderService::clearCache() {
    QMutexLocker locker(&m_mutex);
    m_rendererCache.clear();
    m_pendingRequests.clear();
}

} // namespace MarkdownTool::Render
