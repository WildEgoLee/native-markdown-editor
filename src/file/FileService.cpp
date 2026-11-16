#include "FileService.h"

#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QCryptographicHash>
#include <QStandardPaths>
#include <QRandomGenerator>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace MarkdownTool::File {

QByteArray FileService::calculateFileHash(const QString &filePath) {
    if (filePath.isEmpty() || !QFile::exists(filePath)) {
        return QByteArray();
    }
    QFile file(filePath);
    bool opened = false;
    for (int retry = 0; retry < 15; ++retry) {
        if (file.open(QIODevice::ReadOnly)) {
            opened = true;
            break;
        }
#ifdef Q_OS_WIN
        Sleep(10);
#endif
    }
    if (!opened) {
        return QByteArray();
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (hash.addData(&file)) {
        return hash.result();
    }
    return QByteArray();
}

bool FileService::hasExternalModification(const QString &filePath, const QByteArray &lastKnownHash) {
    if (lastKnownHash.isEmpty() || !QFile::exists(filePath)) {
        return false;
    }
    QByteArray currentHash = calculateFileHash(filePath);
    if (currentHash.isEmpty()) {
        // Cannot read the file on disk to verify integrity: treat as potentially modified/unsafe
        return true;
    }
    return currentHash != lastKnownHash;
}

bool FileService::loadFile(const QString &filePath, Core::TextBuffer &buffer, QString &outError) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        outError = file.errorString();
        return false;
    }

    QByteArray bytes = file.readAll();
    file.close();

    buffer.loadFromBytes(bytes);
    return true;
}

SaveResult FileService::safeSaveFile(const QString &filePath, const Core::TextBuffer &buffer, const QByteArray &lastKnownHash) {
    SaveResult res;

    // Check conflict
    if (!lastKnownHash.isEmpty() && QFile::exists(filePath)) {
        QByteArray currentHash = calculateFileHash(filePath);
        if (currentHash.isEmpty()) {
            res.success = false;
            res.hadConflict = true;
            res.errorMessage = QStringLiteral("无法读取目标文件进行完整性校验（可能被独占或权限受限），已终止保存以防覆盖。");
            return res;
        }
        if (currentHash != lastKnownHash) {
            res.success = false;
            res.hadConflict = true;
            res.errorMessage = QStringLiteral("File was modified by another program.");
            return res;
        }
    }

    // Read-only file protection: explicitly refuse to overwrite read-only files
#ifdef Q_OS_WIN
    std::wstring wTarget = filePath.toStdWString();
    DWORD origAttrs = INVALID_FILE_ATTRIBUTES;
    if (QFile::exists(filePath)) {
        origAttrs = GetFileAttributesW(wTarget.c_str());
        if (origAttrs != INVALID_FILE_ATTRIBUTES && (origAttrs & FILE_ATTRIBUTE_READONLY)) {
            res.success = false;
            res.errorMessage = QStringLiteral("目标文件被设置为只读，已拒绝写入以保护用户数据。");
            return res;
        }
    }
#else
    if (QFile::exists(filePath)) {
        QFileInfo targetInfo(filePath);
        if (!targetInfo.isWritable()) {
            res.success = false;
            res.errorMessage = QStringLiteral("目标文件被设置为只读，已拒绝写入以保护用户数据。");
            return res;
        }
    }
#endif

    QFileInfo fi(filePath);
    QDir dir = fi.dir();
    if (!dir.exists()) {
        if (!dir.mkpath(QStringLiteral("."))) {
            res.success = false;
            res.errorMessage = QStringLiteral("Cannot create target directory.");
            return res;
        }
    }

    // Generate unique temporary filename in the same directory (crucial for same-filesystem atomic rename)
    QString tempFileName = QStringLiteral(".%1.tmp_%2").arg(fi.fileName()).arg(QRandomGenerator::global()->generate());
    QString tempFilePath = dir.filePath(tempFileName);

    QFile tempFile(tempFilePath);
    if (!tempFile.open(QIODevice::WriteOnly)) {
        res.success = false;
        res.errorMessage = QStringLiteral("Cannot open temporary file for writing: ") + tempFile.errorString();
        return res;
    }

    QByteArray dataToSave = buffer.toBytes();
    qint64 written = tempFile.write(dataToSave);
    if (written != dataToSave.size()) {
        tempFile.close();
        tempFile.remove();
        res.success = false;
        res.errorMessage = QStringLiteral("Failed to write all bytes to temporary file.");
        return res;
    }

    tempFile.flush();
    tempFile.close();

    // Atomic replace
#ifdef Q_OS_WIN
    std::wstring wTemp = tempFilePath.toStdWString();

    BOOL ok = FALSE;
    DWORD err = 0;
    // Retry up to 30 times with 10ms backoff (~300ms total) to overcome transient locks from
    // Windows Defender, Windows Search Indexer, or active QFileSystemWatcher on CI runners.
    for (int retry = 0; retry < 30; ++retry) {
        ok = MoveFileExW(wTemp.c_str(), wTarget.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
        if (ok) {
            break;
        }
        err = GetLastError();
        if (err != ERROR_SHARING_VIOLATION && err != ERROR_ACCESS_DENIED) {
            break;
        }
        Sleep(10);
    }

    // Fallback: If MoveFileExW still failed with access/sharing violation, try ReplaceFileW
    if (!ok && (err == ERROR_SHARING_VIOLATION || err == ERROR_ACCESS_DENIED)) {
        for (int retry = 0; retry < 10; ++retry) {
            if (ReplaceFileW(wTarget.c_str(), wTemp.c_str(), nullptr, REPLACEFILE_IGNORE_MERGE_ERRORS, nullptr, nullptr)) {
                ok = TRUE;
                break;
            }
            err = GetLastError();
            Sleep(10);
        }
    }

    if (!ok) {
        tempFile.remove();
        res.success = false;
        res.errorMessage = QStringLiteral("MoveFileExW failed with Windows error code: ") + QString::number(err);
        return res;
    }

    // Preserve original file attributes (such as hidden, system, archive)
    if (origAttrs != INVALID_FILE_ATTRIBUTES) {
        SetFileAttributesW(wTarget.c_str(), origAttrs);
    }
#else
    if (QFile::exists(filePath)) {
        QFile::remove(filePath);
    }
    if (!QFile::rename(tempFilePath, filePath)) {
        tempFile.remove();
        res.success = false;
        res.errorMessage = QStringLiteral("Atomic rename failed.");
        return res;
    }
#endif

    res.success = true;
    return res;
}

QString FileService::recoveryDirectory() {
    QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir dir(appData + QStringLiteral("/recovery"));
    if (!dir.exists()) {
        dir.mkpath(QStringLiteral("."));
    }
    return dir.absolutePath();
}

static QString getRecoveryFileName(const QString &originalFilePath) {
    QByteArray hash = QCryptographicHash::hash(originalFilePath.toUtf8(), QCryptographicHash::Sha1).toHex();
    return QStringLiteral("rec_%1.md").arg(QString::fromUtf8(hash));
}

bool FileService::writeRecoverySnapshot(const QString &originalFilePath, const Core::TextBuffer &buffer) {
    if (originalFilePath.isEmpty()) {
        return false;
    }
    QString recDir = recoveryDirectory();
    QString targetRecPath = QDir(recDir).filePath(getRecoveryFileName(originalFilePath));
    QString tempRecPath = targetRecPath + QStringLiteral(".tmp_%1").arg(QRandomGenerator::global()->generate());

    QFile file(tempRecPath);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }

    QByteArray data = buffer.toBytes();
    qint64 written = file.write(data);
    if (written != data.size()) {
        file.close();
        file.remove();
        return false;
    }

    file.flush();
    file.close();

#ifdef Q_OS_WIN
    std::wstring wTemp = tempRecPath.toStdWString();
    std::wstring wTarget = targetRecPath.toStdWString();
    BOOL ok = MoveFileExW(wTemp.c_str(), wTarget.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    if (!ok) {
        file.remove();
        return false;
    }
#else
    if (QFile::exists(targetRecPath)) {
        QFile::remove(targetRecPath);
    }
    if (!QFile::rename(tempRecPath, targetRecPath)) {
        file.remove();
        return false;
    }
#endif
    return true;
}

void FileService::removeRecoverySnapshot(const QString &originalFilePath) {
    if (originalFilePath.isEmpty()) {
        return;
    }
    QString recPath = QDir(recoveryDirectory()).filePath(getRecoveryFileName(originalFilePath));
    if (QFile::exists(recPath)) {
        QFile::remove(recPath);
    }
}

QString FileService::findRecoverySnapshot(const QString &originalFilePath) {
    if (originalFilePath.isEmpty()) {
        return QString();
    }
    QString recPath = QDir(recoveryDirectory()).filePath(getRecoveryFileName(originalFilePath));
    if (QFile::exists(recPath)) {
        return recPath;
    }
    return QString();
}

static QString getUntitledRecoveryFileName(const QString &untitledId) {
    return QStringLiteral("rec_untitled_%1.md").arg(untitledId);
}

bool FileService::writeUntitledRecoverySnapshot(const QString &untitledId, const Core::TextBuffer &buffer) {
    if (untitledId.isEmpty()) {
        return false;
    }
    QString recDir = recoveryDirectory();
    QString targetRecPath = QDir(recDir).filePath(getUntitledRecoveryFileName(untitledId));
    QString tempRecPath = targetRecPath + QStringLiteral(".tmp_%1").arg(QRandomGenerator::global()->generate());

    QFile file(tempRecPath);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }

    QByteArray data = buffer.toBytes();
    qint64 written = file.write(data);
    if (written != data.size()) {
        file.close();
        file.remove();
        return false;
    }

    file.flush();
    file.close();

#ifdef Q_OS_WIN
    std::wstring wTemp = tempRecPath.toStdWString();
    std::wstring wTarget = targetRecPath.toStdWString();
    BOOL ok = MoveFileExW(wTemp.c_str(), wTarget.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    if (!ok) {
        file.remove();
        return false;
    }
#else
    if (QFile::exists(targetRecPath)) {
        QFile::remove(targetRecPath);
    }
    if (!QFile::rename(tempRecPath, targetRecPath)) {
        file.remove();
        return false;
    }
#endif
    return true;
}

void FileService::removeUntitledRecoverySnapshot(const QString &untitledId) {
    if (untitledId.isEmpty()) {
        return;
    }
    QString recPath = QDir(recoveryDirectory()).filePath(getUntitledRecoveryFileName(untitledId));
    if (QFile::exists(recPath)) {
        QFile::remove(recPath);
    }
}

QString FileService::findUntitledRecoverySnapshot(const QString &untitledId) {
    if (untitledId.isEmpty()) {
        return QString();
    }
    QString recPath = QDir(recoveryDirectory()).filePath(getUntitledRecoveryFileName(untitledId));
    if (QFile::exists(recPath)) {
        return recPath;
    }
    return QString();
}

QStringList FileService::findUntitledRecoverySnapshots() {
    QDir dir(recoveryDirectory());
    QStringList filters;
    filters << QStringLiteral("rec_untitled_*.md");
    QFileInfoList list = dir.entryInfoList(filters, QDir::Files, QDir::Time);
    QStringList result;
    for (const auto &info : list) {
        result.append(info.absoluteFilePath());
    }
    return result;
}

QString FileService::untitledDraftAssetsDirectory(const QString &untitledId) {
    if (untitledId.isEmpty()) {
        return QString();
    }
    QString recDir = recoveryDirectory();
    QDir dir(recDir + QStringLiteral("/draft_assets/") + untitledId);
    if (!dir.exists()) {
        dir.mkpath(QStringLiteral("."));
    }
    return dir.absolutePath();
}

void FileService::removeUntitledDraftAssetsDirectory(const QString &untitledId) {
    if (untitledId.isEmpty()) {
        return;
    }
    QString recDir = recoveryDirectory();
    QDir dir(recDir + QStringLiteral("/draft_assets/") + untitledId);
    if (dir.exists()) {
        dir.removeRecursively();
    }
}

QString FileService::extractUntitledIdFromSnapshotPath(const QString &snapshotPath) {
    QFileInfo fi(snapshotPath);
    QString name = fi.fileName();
    if (name.startsWith(QStringLiteral("rec_untitled_")) && name.endsWith(QStringLiteral(".md"))) {
        return name.mid(13, name.length() - 16);
    }
    return QString();
}

bool AssetRelocationTransaction::prepare(const QString &untitledId, const QString &targetDocumentPath) {
    rollback();
    m_untitledId = untitledId;
    m_committed = false;

    if (untitledId.isEmpty() || targetDocumentPath.isEmpty()) {
        return false;
    }

    QString recDir = FileService::recoveryDirectory();
    QDir draftAssetsDir(recDir + QStringLiteral("/draft_assets/") + untitledId + QStringLiteral("/assets"));
    if (!draftAssetsDir.exists()) {
        return true; // Nothing to relocate
    }

    QDir targetDir = QFileInfo(targetDocumentPath).dir();
    if (!targetDir.mkpath(QStringLiteral("assets"))) {
        return false;
    }
    QString targetAssetsPath = targetDir.filePath(QStringLiteral("assets"));

    QFileInfoList files = draftAssetsDir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
    for (const auto &fi : files) {
        QString destFile = QDir(targetAssetsPath).filePath(fi.fileName());
        if (QFile::exists(destFile)) {
            QString backupName = QStringLiteral(".bak_%1_%2").arg(QRandomGenerator::global()->generate()).arg(fi.fileName());
            QString backupPath = QDir(targetAssetsPath).filePath(backupName);
            if (QFile::exists(backupPath)) {
                QFile::remove(backupPath);
            }
            if (!QFile::rename(destFile, backupPath)) {
                rollback();
                return false;
            }
            m_backups.append({destFile, backupPath});
        }

        if (!QFile::copy(fi.absoluteFilePath(), destFile)) {
            rollback();
            return false;
        }
        m_transferred.append({destFile});
    }

    // Write persistent transaction journal to survive process crash
    if (!m_backups.isEmpty() || !m_transferred.isEmpty()) {
        QString journalName = QStringLiteral(".relocation_journal_%1.tmp").arg(QRandomGenerator::global()->generate());
        m_journalPath = QDir(targetAssetsPath).filePath(journalName);
        QFile journal(m_journalPath);
        if (journal.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream out(&journal);
            for (const auto &b : m_backups) {
                out << "BACKUP:" << b.originalPath << "|" << b.backupPath << "\n";
            }
            for (const auto &t : m_transferred) {
                out << "TRANSFERRED:" << t.finalPath << "\n";
            }
            journal.close();
        }
    }

    return true;
}

void AssetRelocationTransaction::rollback() {
    if (m_committed) {
        return;
    }

    // 1. Remove all newly copied destination files
    for (const auto &item : m_transferred) {
        if (QFile::exists(item.finalPath)) {
            QFile::remove(item.finalPath);
        }
    }
    m_transferred.clear();

    // 2. Restore all pre-existing destination backups with retry and fallback
    for (const auto &item : m_backups) {
        if (QFile::exists(item.backupPath)) {
            if (QFile::exists(item.originalPath)) {
                QFile::remove(item.originalPath);
            }
            bool restored = false;
            for (int r = 0; r < 10; ++r) {
                if (QFile::rename(item.backupPath, item.originalPath)) {
                    restored = true;
                    break;
                }
#ifdef Q_OS_WIN
                Sleep(10);
#endif
            }
            if (!restored) {
                if (QFile::copy(item.backupPath, item.originalPath)) {
                    QFile::remove(item.backupPath);
                } else {
                    qWarning() << "[AssetRelocationTransaction::rollback] Failed to restore backup:"
                               << item.backupPath << "-> backup preserved on disk to protect user data.";
                }
            }
        }
    }
    m_backups.clear();

    // 3. Remove transaction journal
    if (!m_journalPath.isEmpty() && QFile::exists(m_journalPath)) {
        QFile::remove(m_journalPath);
        m_journalPath.clear();
    }
}

void AssetRelocationTransaction::commit() {
    if (m_committed) {
        return;
    }

    // 1. Remove backup files
    for (const auto &item : m_backups) {
        if (QFile::exists(item.backupPath)) {
            QFile::remove(item.backupPath);
        }
    }
    m_backups.clear();
    m_transferred.clear();

    // 2. Remove transaction journal
    if (!m_journalPath.isEmpty() && QFile::exists(m_journalPath)) {
        QFile::remove(m_journalPath);
        m_journalPath.clear();
    }

    // 3. Clean up draft assets folder
    if (!m_untitledId.isEmpty()) {
        FileService::removeUntitledDraftAssetsDirectory(m_untitledId);
    }

    m_committed = true;
}

bool FileService::relocateDraftAssets(const QString &untitledId, const QString &targetDocumentPath) {
    AssetRelocationTransaction tx;
    if (!tx.prepare(untitledId, targetDocumentPath)) {
        return false;
    }
    tx.commit();
    return true;
}

} // namespace MarkdownTool::File
