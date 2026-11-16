#pragma once

#include "core/TextBuffer.h"

#include <QString>
#include <QDateTime>
#include <QByteArray>

namespace MarkdownTool::File {

struct SaveResult {
    bool success = false;
    QString errorMessage;
    bool hadConflict = false;
};

class FileService {
public:
    FileService() = default;

    static bool loadFile(const QString &filePath, Core::TextBuffer &buffer, QString &outError);
    static SaveResult safeSaveFile(const QString &filePath, const Core::TextBuffer &buffer, const QByteArray &lastKnownHash);
    static QByteArray calculateFileHash(const QString &filePath);
    static bool hasExternalModification(const QString &filePath, const QByteArray &lastKnownHash);

    // Crash recovery support
    static QString recoveryDirectory();
    static bool writeRecoverySnapshot(const QString &originalFilePath, const Core::TextBuffer &buffer);
    static void removeRecoverySnapshot(const QString &originalFilePath);
    static QString findRecoverySnapshot(const QString &originalFilePath);

    // Untitled document crash recovery support
    static bool writeUntitledRecoverySnapshot(const QString &untitledId, const Core::TextBuffer &buffer);
    static void removeUntitledRecoverySnapshot(const QString &untitledId);
    static QString findUntitledRecoverySnapshot(const QString &untitledId);
    static QStringList findUntitledRecoverySnapshots();
};

} // namespace MarkdownTool::File
