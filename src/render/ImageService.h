#pragma once

#include <QString>
#include <QPixmap>
#include <QSize>
#include <QCache>
#include <QMutex>

namespace MarkdownTool::Render {

class ImageService {
public:
    static ImageService *instance();

    void setDocumentPath(const QString &docPath);
    QString documentPath() const;

    void setStagingAssetsDir(const QString &stagingDir);
    QString stagingAssetsDir() const;

    QString resolveImagePath(const QString &rawPath) const;
    QPixmap getImage(const QString &rawPath);
    QSize getImageSize(const QString &rawPath, qreal maxDisplayWidth = 800.0);

    void clearCache();

private:
    ImageService();
    ~ImageService();

    mutable QMutex m_mutex;
    QString m_documentPath;
    QString m_stagingAssetsDir;
    QCache<QString, QPixmap> m_cache;
};

} // namespace MarkdownTool::Render
