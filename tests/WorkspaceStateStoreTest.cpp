#include "app/WorkspaceStateStore.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>

#include <iostream>

namespace {
bool require(bool condition, const char* message) {
    if (condition) return true;
    std::cerr << message << '\n';
    return false;
}
}

int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    const QString testFilePath = QDir::current().absoluteFilePath(QStringLiteral("program1.ms"));
    QFile::remove(testFilePath);
    const std::filesystem::path filePath(testFilePath.toStdWString());
    const WorkspaceStateStore store(filePath);

    QFile probe(testFilePath);
    if (!probe.open(QIODevice::WriteOnly)) {
        std::cerr << "Không thể ghi file probe: " << probe.errorString().toStdString() << '\n';
        return 1;
    }
    probe.close();
    probe.remove();

    WorkspaceState original;
    original.sources = {
        {17, ObsCatalogSourceType::VideoFile, L"D:/Media/Chương trình.mp4", {}, {}},
        {29, ObsCatalogSourceType::RtspCamera, {}, "rtsp://camera.local/live", "Camera sân khấu"},
    };
    original.playlistSourceIds = {17, 17};
    original.previewSourceId = 29;
    original.programSourceId = 17;
    original.playlistLoop = false;
    original.playlistAutoNext = true;

    std::string error;
    if (!store.save(original, error)) {
        std::cerr << "Không thể lưu workspace: " << error << '\n';
        return 1;
    }

    WorkspaceState restored;
    if (store.load(restored, error) != WorkspaceLoadStatus::Loaded) {
        std::cerr << "Không thể đọc workspace: " << error << '\n';
        return 1;
    }
    if (!require(restored.sources.size() == 2, "Sai số lượng source sau round-trip.")) return 1;
    if (!require(restored.sources[0].path == original.sources[0].path, "Đường dẫn UTF-8/UTF-16 không được bảo toàn.")) return 1;
    if (!require(QString::fromStdWString(restored.sources[0].path.wstring()) ==
                     QStringLiteral("D:/Media/Chương trình.mp4"),
                 "Đường dẫn tiếng Việt bị mojibake.")) return 1;
    if (!require(restored.sources[1].displayName == original.sources[1].displayName,
                 "Tên camera tiếng Việt không được bảo toàn.")) return 1;
    if (!require(QString::fromUtf8(restored.sources[1].displayName.c_str()) == QStringLiteral("Camera sân khấu"),
                 "Tên camera tiếng Việt bị mojibake.")) return 1;
    if (!require(restored.playlistSourceIds == original.playlistSourceIds,
                 "Thứ tự hoặc source trùng trong playlist không được bảo toàn.")) return 1;
    if (!require(restored.previewSourceId == 29 && restored.programSourceId == 17,
                 "Selection không được bảo toàn.")) return 1;
    if (!require(!restored.playlistLoop && restored.playlistAutoNext,
                 "Tuỳ chọn playlist không được bảo toàn.")) return 1;

    QFile corrupt(QString::fromStdWString(filePath.wstring()));
    if (!require(corrupt.open(QIODevice::WriteOnly | QIODevice::Truncate),
                 "Không thể tạo workspace hỏng để kiểm thử.")) return 1;
    corrupt.write("{not-json");
    corrupt.close();
    if (!require(store.load(restored, error) == WorkspaceLoadStatus::Invalid,
                 "Workspace JSON hỏng không bị từ chối an toàn.")) return 1;

    QFile::remove(testFilePath);
    return 0;
}
