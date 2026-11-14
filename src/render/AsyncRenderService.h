#pragma once

#include <QObject>
#include <QString>
#include <QByteArray>
#include <QHash>
#include <QSet>
#include <QList>
#include <QMutex>
#include <QThreadPool>
#include <QtSvg/QSvgRenderer>
#include <memory>
#include <atomic>

namespace MarkdownTool::Render {

enum class RenderKind {
    Math,
    Mermaid,
};

struct CacheKey {
    RenderKind kind = RenderKind::Math;
    QString source;

    bool operator==(const CacheKey &other) const {
        return kind == other.kind && source == other.source;
    }
};

inline size_t qHash(const CacheKey &key, size_t seed = 0) noexcept {
    return qHashMulti(seed, static_cast<int>(key.kind), key.source);
}

class AsyncRenderService : public QObject {
    Q_OBJECT
public:
    static AsyncRenderService *instance();

    std::shared_ptr<QSvgRenderer> getRenderer(const QString &source, RenderKind kind, bool allowSyncFallback = false);
    bool isCached(const QString &source, RenderKind kind) const;
    void requestRender(const QString &source, RenderKind kind);
    void clearCache();

    int cacheCount() const;
    int maxCacheEntries() const { return m_maxCacheEntries; }
    void setMaxCacheEntries(int maxEntries);

    bool isShuttingDown() const { return m_isShuttingDown.load(); }

    void onRenderCompleted(const CacheKey &key, const QByteArray &svgData, quint64 generation);

signals:
    void renderReady(MarkdownTool::Render::RenderKind kind, const QString &source);

private:
    explicit AsyncRenderService(QObject *parent = nullptr);
    ~AsyncRenderService() override;

    mutable QMutex m_mutex;
    QHash<CacheKey, std::shared_ptr<QSvgRenderer>> m_rendererCache;
    QList<CacheKey> m_lruList;
    QSet<CacheKey> m_pendingRequests;

    quint64 m_currentGeneration = 0;
    int m_maxCacheEntries = 128;
    std::atomic<bool> m_isShuttingDown{false};
    QThreadPool m_threadPool;
};

} // namespace MarkdownTool::Render
