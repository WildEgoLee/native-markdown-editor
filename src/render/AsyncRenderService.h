#pragma once

#include <QObject>
#include <QString>
#include <QByteArray>
#include <QHash>
#include <QSet>
#include <QMutex>
#include <QtSvg/QSvgRenderer>
#include <memory>

namespace MarkdownTool::Render {

enum class RenderKind {
    Math,
    Mermaid,
};

class AsyncRenderService : public QObject {
    Q_OBJECT
public:
    static AsyncRenderService *instance();

    std::shared_ptr<QSvgRenderer> getRenderer(const QString &source, RenderKind kind, bool allowSyncFallback = false);
    bool isCached(const QString &source) const;
    void requestRender(const QString &source, RenderKind kind);
    void clearCache();

    void onRenderCompleted(const QString &source, const QByteArray &svgData);

signals:
    void renderReady(const QString &sourceKey);

private:
    explicit AsyncRenderService(QObject *parent = nullptr);
    ~AsyncRenderService() override = default;

    mutable QMutex m_mutex;
    QHash<QString, std::shared_ptr<QSvgRenderer>> m_rendererCache;
    QSet<QString> m_pendingRequests;
};

} // namespace MarkdownTool::Render
