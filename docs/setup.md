Hướng dẫn tạo installer
Bước 1 — Cài Inno Setup (1 lần duy nhất)
Tải miễn phí tại: https://jrsoftware.org/isdl.php → cài bản Inno Setup 6

Bước 2 — Build app rồi tạo installer
bat
rebuild.bat    ← build app mới nhất (xóa cache)
package.bat    ← tạo file EXE installer
Output
installer\output\MediaSwitcher-Setup-v1.0.0.exe
Cấu trúc file đã tạo:

File	Mô tả


installer/setup.iss
Script Inno Setup — đóng gói toàn bộ DLL, plugin, translations


package.bat
Chạy một lệnh để tạo installer EXE


rebuild.bat
Clean build trước khi đóng gói