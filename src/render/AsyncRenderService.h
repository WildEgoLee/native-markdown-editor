#pragma once

#include <QObject>
#include <QString>
#include <QByteArray>
#include <QHash>
#include <QList>
#include <QMutex>
#include <QThreadPool>
#include <QtSvg/QSvgRenderer>
#include <memory>
#include <atomic>
#include <functional>

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
    explicit AsyncRenderService(QObject *parent = nullptr);
    ~AsyncRenderService() override;

    static AsyncRenderService *instance();

    std::shared_ptr<QSvgRenderer> getRenderer(const QString &source, RenderKind kind, bool allowSyncFallback = false);
    bool isCached(const QString &source, RenderKind kind) const;
    bool isPending(const QString &source, RenderKind kind) const;
    void requestRender(const QString &source, RenderKind kind);
    void clearCache();

    int cacheCount() const;
    int maxCacheEntries() const;
    void setMaxCacheEntries(int maxEntries);

    qint64 totalCachedBytes() const;
    qint64 maxCacheBytes() const;
    void setMaxCacheBytes(qint64 maxBytes);

    quint64 currentGeneration() const;
    bool isShuttingDown() const { return m_isShuttingDown.load(); }

    void onRenderCompleted(const CacheKey &key, const QByteArray &svgData, quint64 generation);

    // Test synchronization hook to deterministically test cross-generation race condition before lock re-entry
    void setTestPreCommitHook(std::function<void(quint64)> hook) {
        m_testPreCommitHook = std::move(hook);
    }

signals:
    void renderReady(MarkdownTool::Render::RenderKind kind, const QString &source);

private:
    void commitToCacheLocked(const CacheKey &key, const std::shared_ptr<QSvgRenderer> &renderer, qint64 byteSize, quint64 generation);
    void evictIfNeededLocked();

    std::function<void(quint64)> m_testPreCommitHook;

    mutable QMutex m_mutex;
    QHash<CacheKey, std::shared_ptr<QSvgRenderer>> m_rendererCache;
    QHash<CacheKey, qint64> m_rendererByteSizes;
    QList<CacheKey> m_lruList;
    QHash<CacheKey, quint64> m_pendingRequests;

    quint64 m_currentGeneration = 1;
    int m_maxCacheEntries = 128;
    qint64 m_totalCachedBytes = 0;
    qint64 m_maxCacheBytes = 32 * 1024 * 1024; // 32 MB default memory budget
    std::atomic<bool> m_isShuttingDown{false};
    std::shared_ptr<std::atomic<bool>> m_aliveToken;
    QThreadPool m_threadPool;
};

} // namespace MarkdownTool::Render
