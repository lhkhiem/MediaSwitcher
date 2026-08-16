#include "CopyrightSettings.h"

#include "common/config/AppInfo.h"

#include <QFile>
#include <QStringConverter>
#include <QTextStream>

namespace {
constexpr qint64 kMaximumCopyrightFileSize = 64 * 1024;

QString defaultDetails() {
    return QStringLiteral(
        "%1\n\n"
        "THÔNG TIN BẢN QUYỀN\n\n"
        "Tên ứng dụng: %2\n"
        "Phiên bản: %3\n"
        "Nhà phát triển / Tác giả: %4\n"
        "Đơn vị phát triển: %5\n\n"
        "MÔ TẢ\n\n%6\n\n"
        "GIẤY PHÉP\n\n%7")
        .arg(AppInfo::COPYRIGHT,
             AppInfo::FULL_NAME,
             AppInfo::VERSION,
             AppInfo::DEVELOPER,
             AppInfo::COMPANY,
             AppInfo::DESCRIPTION,
             AppInfo::LICENSE);
}

QString firstContentLine(const QString& text) {
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (const QString& line : lines) {
        const QString trimmed = line.trimmed();
        if (!trimmed.isEmpty()) return trimmed;
    }
    return {};
}
}

CopyrightInfo CopyrightSettings::load() {
    QFile file(QStringLiteral(":/legal/copyright.txt"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text) || file.size() > kMaximumCopyrightFileSize) {
        const QString details = defaultDetails();
        return {AppInfo::COPYRIGHT, details};
    }

    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    QString details = stream.readAll();
    if (details.startsWith(QChar::ByteOrderMark)) details.remove(0, 1);
    details = details.trimmed();
    const QString footer = firstContentLine(details);
    if (footer.isEmpty()) {
        const QString fallback = defaultDetails();
        return {AppInfo::COPYRIGHT, fallback};
    }
    return {footer, details};
}
