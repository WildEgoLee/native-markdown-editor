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
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
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
    return !currentHash.isEmpty() && currentHash != lastKnownHash;
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
        if (hasExternalModification(filePath, lastKnownHash)) {
            res.success = false;
            res.hadConflict = true;
            res.errorMessage = QStringLiteral("File was modified by another program.");
            return res;
        }
    }

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
    std::wstring wTarget = filePath.toStdWString();
    BOOL ok = MoveFileExW(wTemp.c_str(), wTarget.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    if (!ok) {
        DWORD err = GetLastError();
        tempFile.remove();
        res.success = false;
        res.errorMessage = QStringLiteral("MoveFileExW failed with Windows error code: ") + QString::number(err);
        return res;
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

} // namespace MarkdownTool::File
