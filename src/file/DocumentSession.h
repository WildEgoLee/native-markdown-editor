#pragma once

#include "FileService.h"
#include "core/TextBuffer.h"

#include <QString>
#include <QByteArray>
#include <QFileInfo>

namespace MarkdownTool::File {

class DocumentSession {
public:
    DocumentSession() = default;

    bool isUntitled() const { return m_filePath.isEmpty(); }
    const QString &filePath() const { return m_filePath; }
    const QByteArray &lastKnownHash() const { return m_lastKnownHash; }

    QString displayName() const {
        return m_filePath.isEmpty() ? QStringLiteral("未命名文档") : QFileInfo(m_filePath).fileName();
    }

    void resetToUntitled(Core::TextBuffer &buffer) {
        if (!m_filePath.isEmpty()) {
            FileService::removeRecoverySnapshot(m_filePath);
        }
        m_filePath.clear();
        m_lastKnownHash.clear();
        buffer.setText(QString());
    }

    bool load(const QString &targetPath, Core::TextBuffer &buffer, QString &outError) {
        if (!FileService::loadFile(targetPath, buffer, outError)) {
            return false;
        }
        if (!m_filePath.isEmpty() && m_filePath != targetPath) {
            FileService::removeRecoverySnapshot(m_filePath);
        }
        m_filePath = targetPath;
        m_lastKnownHash = FileService::calculateFileHash(targetPath);
        return true;
    }

    SaveResult save(const Core::TextBuffer &buffer, bool forceOverwrite = false) {
        if (m_filePath.isEmpty()) {
            SaveResult r;
            r.success = false;
            r.errorMessage = QStringLiteral("Document is untitled, please use Save As.");
            return r;
        }

        QByteArray checkHash = forceOverwrite ? QByteArray() : m_lastKnownHash;
        auto res = FileService::safeSaveFile(m_filePath, buffer, checkHash);
        if (res.success) {
            m_lastKnownHash = FileService::calculateFileHash(m_filePath);
            FileService::removeRecoverySnapshot(m_filePath);
        }
        return res;
    }

    SaveResult saveAs(const QString &newPath, const Core::TextBuffer &buffer) {
        // Transactional save-as: write new file first
        auto res = FileService::safeSaveFile(newPath, buffer, QByteArray());
        if (res.success) {
            if (!m_filePath.isEmpty() && m_filePath != newPath) {
                FileService::removeRecoverySnapshot(m_filePath);
            }
            m_filePath = newPath;
            m_lastKnownHash = FileService::calculateFileHash(newPath);
            FileService::removeRecoverySnapshot(m_filePath);
        }
        return res;
    }

private:
    QString m_filePath;
    QByteArray m_lastKnownHash;
};

} // namespace MarkdownTool::File
