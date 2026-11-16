#include "ImageService.h"

#include <QFileInfo>
#include <QDir>
#include <QUrl>
#include <QImageReader>
#include <QMutexLocker>
#include <algorithm>

namespace MarkdownTool::Render {

ImageService *ImageService::instance() {
    static ImageService s_instance;
    return &s_instance;
}

ImageService::ImageService() {
    m_cache.setMaxCost(50 * 1024 * 1024);
}

ImageService::~ImageService() = default;

void ImageService::setDocumentPath(const QString &docPath) {
    QMutexLocker locker(&m_mutex);
    if (m_documentPath != docPath) {
        m_documentPath = docPath;
        m_cache.clear();
    }
}

QString ImageService::documentPath() const {
    QMutexLocker locker(&m_mutex);
    return m_documentPath;
}

void ImageService::clearCache() {
    QMutexLocker locker(&m_mutex);
    m_cache.clear();
}

QString ImageService::resolveImagePath(const QString &rawPath) const {
    if (rawPath.trimmed().isEmpty()) {
        return QString();
    }

    QString path = rawPath.trimmed();

    if (path.startsWith(QStringLiteral("file:///"), Qt::CaseInsensitive)) {
        path = QUrl(path).toLocalFile();
    }

    QFileInfo fi(path);
    if (fi.isAbsolute() && fi.exists()) {
        return fi.canonicalFilePath();
    }

    QString docPath;
    {
        QMutexLocker locker(&m_mutex);
        docPath = m_documentPath;
    }
    if (!docPath.isEmpty()) {
        QDir docDir = QFileInfo(docPath).dir();
        QString candidate = docDir.filePath(path);
        if (QFile::exists(candidate)) {
            return QFileInfo(candidate).canonicalFilePath();
        }
    }

    QString curCandidate = QDir::current().filePath(path);
    if (QFile::exists(curCandidate)) {
        return QFileInfo(curCandidate).canonicalFilePath();
    }

    return path;
}

QSize ImageService::getImageSize(const QString &rawPath, qreal maxDisplayWidth) {
    QString resolved = resolveImagePath(rawPath);
    if (resolved.isEmpty() || !QFile::exists(resolved)) {
        return QSize(static_cast<int>(std::clamp(maxDisplayWidth, 200.0, 600.0)), 64);
    }

    {
        QMutexLocker locker(&m_mutex);
        if (QPixmap *cached = m_cache.object(resolved)) {
            QSize orig = cached->size();
            qreal w = orig.width();
            qreal h = orig.height();
            if (w > maxDisplayWidth && w > 0) {
                h = h * (maxDisplayWidth / w);
                w = maxDisplayWidth;
            }
            if (h > 550.0 && h > 0) {
                w = w * (550.0 / h);
                h = 550.0;
            }
            return QSize(std::max(10, static_cast<int>(w)), std::max(10, static_cast<int>(h)));
        }
    }

    QImageReader reader(resolved);
    QSize orig = reader.size();
    if (!orig.isValid() || orig.isEmpty()) {
        return QSize(static_cast<int>(std::clamp(maxDisplayWidth, 200.0, 600.0)), 64);
    }

    qreal w = orig.width();
    qreal h = orig.height();
    if (w > maxDisplayWidth && w > 0) {
        h = h * (maxDisplayWidth / w);
        w = maxDisplayWidth;
    }
    if (h > 550.0 && h > 0) {
        w = w * (550.0 / h);
        h = 550.0;
    }
    return QSize(std::max(10, static_cast<int>(w)), std::max(10, static_cast<int>(h)));
}

QPixmap ImageService::getImage(const QString &rawPath) {
    QString resolved = resolveImagePath(rawPath);
    if (resolved.isEmpty() || !QFile::exists(resolved)) {
        return QPixmap();
    }

    QMutexLocker locker(&m_mutex);
    if (QPixmap *cached = m_cache.object(resolved)) {
        return *cached;
    }

    QPixmap pix;
    if (pix.load(resolved)) {
        int cost = pix.width() * pix.height() * 4;
        m_cache.insert(resolved, new QPixmap(pix), cost);
        return pix;
    }

    return QPixmap();
}

} // namespace MarkdownTool::Render
