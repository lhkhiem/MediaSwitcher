#include "app/WorkspaceStateStore.h"
#include "engine/obs/ObsPlaylist.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>

#include <iostream>
#include <cmath>

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
    original.fpsNumerator = 60000;
    original.fpsDenominator = 1001;
    original.programRenderMode = 1;
    original.fadeDurationMs = 1500;
    original.thumbnailWidth = 220;
    original.sourceTypeFilter = 2;
    original.previewPositionMs = 12345;
    original.programPositionMs = 67890;
    original.previewLoop = true;
    original.programLoop = false;
    original.previewVolume = 0.42f;
    original.programVolume = 0.73f;
    original.previewMuted = true;
    original.programMuted = false;
    original.outputScreenName = "Màn hình LED";
    original.layoutMode = 1;
    original.canvasWidth = 3840;
    original.canvasHeight = 1080;
    original.activeZoneIndex = 2;
    original.blackoutAll = true;
    original.zones[0] = {0, 0, 960, 1080, 1, 17, 4500, true, 0.8f, true, false};
    original.zones[1] = {960, 0, 1920, 1080, 0, 29, 0, false, 1.0f, false, false};
    original.zones[2] = {2880, 0, 960, 1080, 2, 17, 9200, false, 0.5f, true, true};

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
    if (!require(restored.fpsNumerator == 60000 && restored.fpsDenominator == 1001 &&
                     restored.programRenderMode == 1 && restored.fadeDurationMs == 1500,
                 "Thiết lập project không được bảo toàn.")) return 1;
    if (!require(restored.thumbnailWidth == 220 && restored.sourceTypeFilter == 2,
                 "Thiết lập giao diện không được bảo toàn.")) return 1;
    if (!require(restored.previewPositionMs == 12345 && restored.programPositionMs == 67890 &&
                     restored.previewLoop && !restored.programLoop,
                 "Cue playback không được bảo toàn.")) return 1;
    if (!require(std::abs(restored.previewVolume - 0.42f) < 0.001f &&
                     std::abs(restored.programVolume - 0.73f) < 0.001f &&
                     restored.previewMuted && !restored.programMuted,
                 "Thiết lập âm thanh không được bảo toàn.")) return 1;
    if (!require(restored.outputScreenName == original.outputScreenName,
                 "Tên màn hình output không được bảo toàn.")) return 1;
    if (!require(restored.layoutMode == 1 && restored.canvasWidth == 3840 && restored.canvasHeight == 1080 &&
                     restored.activeZoneIndex == 2 && restored.blackoutAll,
                 "Cấu hình tổng thể 3 vùng không được bảo toàn.")) return 1;
    if (!require(restored.zones[0].width == 960 && restored.zones[0].sourceId == 17 && restored.zones[0].positionMs == 4500 &&
                     restored.zones[1].x == 960 && restored.zones[1].sourceId == 29 && !restored.zones[1].muted &&
                     restored.zones[2].x == 2880 && restored.zones[2].renderMode == 2 && restored.zones[2].blackout,
                 "Chi tiết từng vùng trong chế độ 3 vùng không được bảo toàn.")) return 1;

    QFile legacy(testFilePath);
    if (!require(legacy.open(QIODevice::WriteOnly | QIODevice::Truncate),
                 "Không thể tạo workspace v1 để kiểm thử migration.")) return 1;
    legacy.write(R"({"format":"MediaSwitcherWorkspace","version":1,"sources":[],"playlist":{"sourceIds":[],"loop":true,"autoNext":false},"selection":{}})");
    legacy.close();
    if (!require(store.load(restored, error) == WorkspaceLoadStatus::Loaded &&
                     restored.playlistLoop && !restored.playlistAutoNext && restored.fadeDurationMs == 700,
                 "Workspace v1 không được migrate bằng giá trị mặc định an toàn.")) return 1;

    QFile corrupt(QString::fromStdWString(filePath.wstring()));
    if (!require(corrupt.open(QIODevice::WriteOnly | QIODevice::Truncate),
                 "Không thể tạo workspace hỏng để kiểm thử.")) return 1;
    corrupt.write("{not-json");
    corrupt.close();
    if (!require(store.load(restored, error) == WorkspaceLoadStatus::Invalid,
                 "Workspace JSON hỏng không bị từ chối an toàn.")) return 1;

    const QString relinkPath = QDir::current().absoluteFilePath(QStringLiteral("media-relinked.mp4"));
    QFile relinkFile(relinkPath);
    if (!require(relinkFile.open(QIODevice::WriteOnly), "Không thể tạo file kiểm thử relink.")) return 1;
    relinkFile.write("test");
    relinkFile.close();
    ObsSourceCatalog catalog;
    const uint64_t sourceId = catalog.add(std::filesystem::path(L"D:/Media/missing.mp4"));
    if (!require(catalog.relinkLocalFile(sourceId, std::filesystem::path(relinkPath.toStdWString())),
                 "Không thể relink source tới file hợp lệ.")) return 1;
    const auto relinked = catalog.find(sourceId);
    if (!require(relinked && relinked->id == sourceId &&
                     QString::fromStdWString(relinked->path.wstring()) == relinkPath,
                 "Relink không giữ nguyên source ID hoặc đường dẫn mới.")) return 1;
    QFile::remove(relinkPath);

    // Kiểm thử hoạt động của ObsPlaylist: thêm, điều hướng, lặp, xoá và di chuyển
    ObsPlaylist playlist;
    if (!require(playlist.empty() && playlist.size() == 0, "Playlist mới khởi tạo phải rỗng.")) return 1;
    playlist.addSource(101);
    playlist.addSource(102);
    playlist.addSource(103);
    if (!require(playlist.size() == 3 && playlist.currentSourceId() == 101, "Playlist không thêm đúng 3 source.")) return 1;
    if (!require(playlist.advance() && playlist.currentSourceId() == 102, "Playlist không chuyển tiếp đến source thứ 2.")) return 1;
    if (!require(playlist.advance() && playlist.currentSourceId() == 103, "Playlist không chuyển tiếp đến source thứ 3.")) return 1;
    playlist.setLoop(false);
    if (!require(!playlist.advance(), "Playlist loop=false không được advance khi đã ở cuối.")) return 1;
    playlist.setLoop(true);
    if (!require(playlist.advance() && playlist.currentSourceId() == 101, "Playlist loop=true không quay lại đầu khi hết.")) return 1;
    if (!require(playlist.previous() && playlist.currentSourceId() == 103, "Playlist loop=true previous từ đầu không lùi về cuối.")) return 1;
    if (!require(playlist.move(2, 0) && playlist.sourceIdAt(0) == 103, "Di chuyển mục playlist thất bại.")) return 1;
    if (!require(playlist.removeAt(0) && playlist.size() == 2, "Xoá mục playlist thất bại.")) return 1;

    ObsPlaylist singleItemPlaylist;
    singleItemPlaylist.addSource(200);
    singleItemPlaylist.setLoop(false);
    if (!require(!singleItemPlaylist.advance(), "Playlist 1 mục loop=false không được advance.")) return 1;
    singleItemPlaylist.setLoop(true);
    if (!require(singleItemPlaylist.advance() && singleItemPlaylist.currentSourceId() == 200,
                 "Playlist 1 mục loop=true phải advance thành công và giữ nguyên source.")) return 1;

    QFile::remove(testFilePath);
    return 0;
}
