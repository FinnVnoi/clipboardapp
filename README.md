# ClipboardTxtApp

<p align="center">
  <strong>Local-first clipboard memory for Windows.</strong><br>
  Keep the things you copy close, searchable, and ready to paste.
</p>

<p align="center">
  <a href="https://github.com/FinnVnoi/clipboardapp/actions/workflows/ci.yml"><img src="https://github.com/FinnVnoi/clipboardapp/actions/workflows/ci.yml/badge.svg" alt="CI"></a>
  <a href="https://github.com/FinnVnoi/clipboardapp/releases"><img src="https://img.shields.io/github/v/release/FinnVnoi/clipboardapp?display_name=tag&sort=semver" alt="Latest release"></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-MIT-059669.svg" alt="MIT license"></a>
</p>

ClipboardTxtApp là tiện ích clipboard nhỏ gọn cho Windows. Ứng dụng chạy dưới system tray, lưu lịch sử text, file và hình ảnh trên máy, rồi giúp bạn tìm và chọn lại nội dung bằng bàn phím.

## Vì sao ClipboardTxtApp?

- **Local-first** — không cần server, không upload clipboard.
- **Keyboard-first** — mở thư viện bằng `Ctrl+Alt+V`, tìm kiếm và chọn nhanh.
- **Text, file, image** — một lịch sử chung cho những gì bạn vừa copy.
- **Cache có kiểm soát** — giới hạn số mục, kích thước file và thời gian lưu.
- **Windows-native** — app Win32 nhẹ, chạy nền dưới system tray.

## Tải xuống

Tải bản mới nhất tại [GitHub Releases](https://github.com/FinnVnoi/clipboardapp/releases).

Mỗi bản release Windows x64 gồm:

```text
ClipboardTxtApp.exe
README.md
LICENSE
```

Giải nén và chạy `ClipboardTxtApp.exe`. Ứng dụng không cần installer.

## Cách sử dụng

1. Chạy `ClipboardTxtApp.exe`; ứng dụng sẽ nằm dưới system tray.
2. Copy text, file hoặc hình ảnh như bình thường.
3. Nhấn `Ctrl+Alt+V` để mở thư viện clipboard.
4. Nhập từ khóa, dùng `↑` / `↓` để di chuyển và nhấn `Enter` để chọn.

Khi chọn text, app mặc định dán text thô. Bạn có thể bật tùy chọn gửi dưới dạng file `.txt` khi cần làm việc với hộp thoại chọn file.

## Phím tắt

| Phím | Chức năng |
| --- | --- |
| `Ctrl+Alt+V` | Mở thư viện clipboard và focus ô tìm kiếm |
| `↑` / `↓` | Di chuyển mục đang chọn |
| `Enter` | Chọn mục hiện tại |
| `Ctrl+Enter` hoặc `Ctrl+click` | Chọn/bỏ chọn một mục text trong multitext |
| `Shift+Enter` hoặc `Shift+click` | Pin/bỏ pin mục hiện tại |
| `Esc` | Đóng cửa sổ clipboard |

## Tính năng

- Tìm kiếm trong text, tiêu đề và đường dẫn file.
- Xem trước text, file TXT, file và hình ảnh trước khi chọn.
- Chọn nhiều mục text để ghép thành multitext.
- Pin mục quan trọng để giữ chúng ở đầu danh sách và tránh bị dọn cache.
- Tự đề xuất chuyển văn bản dài thành file TXT khi dán vào ứng dụng hỗ trợ file.
- Tự phát hiện hộp thoại chọn file trong các ứng dụng được cho phép.
- Tùy chọn khởi động cùng Windows.

## Dữ liệu và quyền riêng tư

ClipboardTxtApp lưu dữ liệu cục bộ tại:

```text
%APPDATA%\ClipboardTxtApp
```

```text
config.ini       Cấu hình ứng dụng
library.tsv      Lịch sử clipboard
generated\       File TXT, file và hình ảnh cache
```

> Ứng dụng không có server và không tự gửi dữ liệu clipboard lên mạng. File và hình ảnh bạn copy vẫn có thể chiếm dung lượng trong thư mục cache cho đến khi được dọn theo cài đặt.

## Cài đặt

Mở menu của biểu tượng system tray và chọn **Cài đặt**.

| Cài đặt | Mặc định | Mô tả |
| --- | ---: | --- |
| Ngưỡng ký tự | `1800` | Số ký tự để app coi là văn bản dài |
| Số slot clipboard | `30` | Số mục tối đa trong thư viện |
| File cache tối đa | `100 MB` | Dung lượng tối đa cho mỗi file hoặc hình ảnh mới |
| Số ngày giữ cache | `7` | File cũ hơn số ngày này bị xóa khi app khởi động; `0` là không tự xóa |
| Dấu phân cách multitext | Một dòng trống | Khoảng trắng, xuống dòng, dòng trống hoặc chuỗi tùy chỉnh |
| Khởi động cùng Windows | Tắt | Tự chạy app khi đăng nhập Windows |
| Luôn hỏi trước khi chuyển TXT | Bật | Hiện xác nhận trước khi chuyển text dài thành file |
| Tự phát hiện hộp Choose file | Bật | Hỗ trợ hộp thoại chọn file trong ứng dụng được phép |
| Ứng dụng được phép | Danh sách mặc định | Mỗi process nhập trên một dòng |

## Build từ mã nguồn

### Yêu cầu

- Windows
- C++ compiler hỗ trợ C++11
- Một trong hai lựa chọn:
  - MinGW `g++` trong `PATH`
  - CMake 3.20 trở lên và Visual Studio có workload Desktop development with C++

### MinGW

```bat
build.bat
```

Output:

```text
build\ClipboardTxtApp.exe
```

### CMake

```powershell
cmake -S . -B build-cmake
cmake --build build-cmake --config Release
```

Nếu app đang chạy, hãy thoát app trước khi build để Windows không khóa file `.exe`.

## CI/CD và release

- Mỗi push vào `main` và mỗi pull request đều được build trên Windows x64 bằng CMake.
- Workflow CI upload file `.exe` như build artifact.
- Push tag theo format `v*` sẽ tự build, đóng gói và tạo GitHub Release.

Tạo release mới:

```powershell
git tag v0.1.0
git push origin v0.1.0
```

## Cấu trúc mã nguồn

```text
src\
  main.cpp               Khởi động app, system tray và hotkey
  AppState.*             Cấu hình và tiện ích filesystem
  ClipboardMonitor.*     Theo dõi, đọc và ghi clipboard
  ClipboardLibrary.*     Quản lý lịch sử clipboard
  FileDialogAssist.*     Hỗ trợ hộp thoại chọn file và ô nhập liệu
  TextFileConverter.*    Tạo file TXT từ clipboard
  AllowedApps.*          Kiểm tra danh sách ứng dụng được phép
  Ui.*                   Giao diện cài đặt và cửa sổ chọn clipboard
```

## License

[MIT](LICENSE)
