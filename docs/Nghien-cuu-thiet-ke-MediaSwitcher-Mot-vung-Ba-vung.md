# Nghiên cứu thiết kế MediaSwitcher: chế độ Một vùng và Ba vùng

**Phương án tối giản cho màn hình LED gồm màn chính và hai cánh**

Phiên bản 1.0  
Ngày 16/08/2026  
Phạm vi: MediaSwitcher trên Windows, Qt, libobs và Direct3D 11

---

## Tóm tắt điều hành

MediaSwitcher cần hỗ trợ một màn hình LED có tổng bề mặt hiển thị được chia thành ba phần: cánh trái, màn chính và cánh phải. Ba phần có thể phát ba nội dung khác nhau, nhưng bộ xử lý LED vẫn nhận một tín hiệu hình ảnh duy nhất từ máy tính. Đồng thời, người dùng hiện tại phải tiếp tục sử dụng chế độ một nội dung phủ toàn màn hình mà không bị thay đổi quy trình vận hành.

Kết luận của nghiên cứu là không nên sao chép toàn bộ mô hình phức tạp của Resolume, OBS Studio, vMix hoặc TouchDesigner. MediaSwitcher chỉ nên tiếp thu nguyên lý chung đã được các phần mềm này chứng minh: ghép nhiều nguồn vào một canvas, ánh xạ từng nguồn vào một hình chữ nhật và render canvas đó qua một output duy nhất.

Giải pháp tối giản gồm hai chế độ:

- **Một vùng:** giữ nguyên pipeline Preview, Program, CUT, FADE, playlist và output hiện tại.
- **Ba vùng:** tạo ba chương trình phát độc lập cho Trái, Chính và Phải; ghép chúng vào một canvas và xuất qua cùng một cửa sổ fullscreen.

Phiên bản đầu tiên của chế độ Ba vùng chỉ cần CUT, playlist, Play/Pause, Previous/Next, Loop và ba kiểu co giãn Fit, Fill, Stretch. Không cần Scene Editor, Cue đa vùng, MIDI, OSC, Web Controller, nhiều layer hoặc transition độc lập trong giai đoạn đầu.

> **Khuyến nghị:** triển khai chế độ Ba vùng như một tính năng tùy chọn. Workspace cũ luôn mở ở chế độ Một vùng. Nếu người dùng không bật Ba vùng, hiệu năng và hành vi phải giống phiên bản hiện tại.

## 1. Bối cảnh và bài toán thực tế

### 1.1 Cấu trúc màn hình LED

Màn hình vật lý gồm ba khu vực nhìn thấy độc lập:

```text
┌────────────┬────────────────────────┬────────────┐
│ CÁNH TRÁI  │       MÀN CHÍNH        │ CÁNH PHẢI │
│ Nội dung A │       Nội dung B       │ Nội dung C │
└────────────┴────────────────────────┴────────────┘
```

Về phía máy tính và bộ xử lý Colorlight, toàn bộ bề mặt vẫn có thể được xem là một canvas liên tục. Ví dụ:

```text
Cánh trái :  640 × 1080 pixel
Màn chính : 1920 × 1080 pixel
Cánh phải :  640 × 1080 pixel
Canvas    : 3200 × 1080 pixel
```

Tọa độ được tính tự động:

```text
LEFT  = x: 0,    y: 0, width: 640,  height: 1080
MAIN  = x: 640,  y: 0, width: 1920, height: 1080
RIGHT = x: 2560, y: 0, width: 640,  height: 1080
```

### 1.2 Hai nhu cầu phải cùng tồn tại

Nhu cầu thứ nhất là phát ba nội dung khác nhau. Nhu cầu thứ hai là khi không cần chia vùng, toàn bộ màn hình phải tiếp tục hiển thị một nội dung như hiện tại.

Vì vậy, giải pháp không được biến chế độ Một vùng thành một trường hợp giả lập bằng ba player. Chế độ Một vùng phải tiếp tục sử dụng đúng pipeline hiện có, còn chế độ Ba vùng chỉ được khởi tạo khi người dùng chủ động bật.

### 1.3 Mục tiêu sản phẩm

- Vận hành dễ hiểu với người không chuyên về video mapping.
- Không yêu cầu người dùng hiểu layer, slice, scene graph hoặc bus hình ảnh.
- Một output vật lý duy nhất tới bộ xử lý LED.
- Không mở ba cửa sổ fullscreen.
- Không giải mã nội dung khi vùng không sử dụng.
- Workspace cũ tương thích ngược.
- Chuyển về Một vùng nhanh và an toàn.
- Có test pattern để đối chiếu đúng pixel với LEDVISION hoặc LEDSetting.

### 1.4 Ngoài phạm vi phiên bản đầu

- Projection mapping tự do hoặc vùng đa giác.
- Nhiều hơn ba vùng.
- Layer lồng nhau và compositing phức tạp.
- Cue sân khấu đồng bộ nhiều vùng.
- MIDI, OSC, DMX hoặc điều khiển web.
- NDI output, streaming và recording.
- Edge blending hoặc warp hình học.
- Genlock nhiều GPU hoặc nhiều output vật lý.

## 2. Phương pháp nghiên cứu

Nghiên cứu sử dụng hai nhóm bằng chứng:

1. Tài liệu chính thức của Colorlight LEDVISION, Resolume, OBS Studio, vMix và TouchDesigner để xác định những mô hình vận hành đã được sử dụng trong thực tế.
2. Rà soát mã nguồn MediaSwitcher để tìm điểm có thể mở rộng mà không tạo pipeline trùng lặp.

Tiêu chí đánh giá gồm:

- Số bước người vận hành phải thực hiện.
- Số decoder hoạt động.
- Số render pass và cửa sổ output.
- Khả năng giữ nguyên chế độ hiện tại.
- Khả năng kiểm tra đúng tọa độ pixel.
- Mức độ thay đổi mã nguồn.
- Rủi ro mất output khi đổi chế độ hoặc mất EDID.

## 3. Kết quả nghiên cứu các phần mềm tham khảo

### 3.1 LEDVISION và LEDSetting

LEDVISION có ưu điểm là cho phép thiết lập số lượng màn, tọa độ bắt đầu và kích thước hiển thị. Đây là mô hình phù hợp để cấu hình phần cứng Colorlight, card gửi và card nhận. Tuy nhiên, phần quản lý chương trình phát không phải điểm mạnh khi cần preview nhanh, playlist độc lập và chuyển nguồn trực tiếp.

Bài học nên giữ lại:

- Kích thước và tọa độ phải là số pixel nguyên.
- Cần test pattern có tên vùng, viền và lưới pixel.
- Cấu hình hiển thị phải được lưu và có thể đọc lại.

Phần không nên sao chép:

- Trình biên tập chương trình nhiều cửa sổ với quá nhiều kiểu nội dung.
- Giao diện vừa cấu hình receiving card vừa vận hành media.
- Quy trình yêu cầu người vận hành chỉnh lại tọa độ trong buổi diễn.

Colorlight hiện tách phần phát nội dung và phần điều khiển màn hình thành LEDVISION và LEDSetting. Điều này củng cố nguyên tắc: MediaSwitcher chỉ phát canvas; LEDSetting tiếp tục chịu trách nhiệm cấu hình phần cứng.

### 3.2 Resolume Arena

Resolume dùng Composition làm không gian tổng, Layer/Group làm nguồn compositing và Slice để chọn phần nội dung gửi tới output. Một layer hoặc group có thể được route tới một slice. Đây là giải pháp linh hoạt cho sân khấu có nhiều bề mặt LED.

Bài học nên giữ lại:

- Một composition có thể phục vụ nhiều vùng.
- Mỗi vùng chỉ cần một hình chữ nhật nguồn và hình chữ nhật đích.
- Một cửa sổ output có thể chứa toàn bộ canvas.
- Fit, Fill, Stretch và Rotation nên là thuộc tính của vùng.

Phần không nên sao chép:

- Layer, Group, Deck, Column, Slice và hiệu ứng cùng xuất hiện trong UI.
- Routing tự do giữa mọi đối tượng.
- Mapping đa giác và projection mapping.
- Hệ thống tham số, MIDI, OSC và DMX trong phiên bản đầu.

### 3.3 OBS Studio và libobs

OBS sử dụng Source làm đơn vị đầu vào và Scene làm đơn vị chứa nhiều Source. Mỗi scene item có vị trí, scale, crop, visibility và thứ tự. Studio Mode tách Preview khỏi Program để người vận hành chuẩn bị nội dung trước khi đưa lên output.

MediaSwitcher đã sử dụng libobs, vì vậy có thể tiếp tục dùng obs_view và obs_display thay vì bổ sung một engine khác. Tuy nhiên, phiên bản tối giản không cần xây trình biên tập Scene như OBS Studio. Ba vùng chỉ cần ba viewport cố định.

Bài học nên giữ lại:

- Source và output là hai khái niệm tách biệt.
- Preview không được tự động thay đổi Program.
- Output fullscreen chỉ là nơi render, không sở hữu decoder.
- Callback render của obs_display là vị trí phù hợp để ghép ba vùng.

Phần không nên sao chép:

- Danh sách scene/source nhiều tầng.
- Filter chain, overlay, dock và studio layout phức tạp.
- Streaming, recording và virtual camera.

### 3.4 vMix

vMix có Input toàn cục, Preview, Output và Layer Designer. Các hành động được trừu tượng thành shortcut, cho phép bàn phím hoặc bộ điều khiển gọi cùng một chức năng.

Bài học phù hợp với MediaSwitcher là nguồn media nên tồn tại trong một catalog dùng chung; người dùng chọn nguồn cho vùng thay vì mở file mới trong từng cửa sổ. Tuy nhiên, phiên bản đầu không cần Mix phụ, trigger, script hoặc Web Controller.

### 3.5 TouchDesigner

Tài liệu TouchDesigner khuyến nghị ghép các đầu ra thành một canvas và dùng một Perform Window để đạt hiệu năng tốt hơn so với nhiều cửa sổ độc lập. Đây là bằng chứng trực tiếp cho lựa chọn một canvas, một output của MediaSwitcher.

Bài học nên giữ lại:

- Chỉ một cửa sổ output fullscreen.
- Kích thước canvas phải được biết trước.
- Đánh giá hiệu năng trong chế độ output thực tế, không dựa vào thumbnail UI.

## 4. Quyết định sản phẩm tối giản

### 4.1 Chỉ có hai Layout Mode

```cpp
enum class LayoutMode {
    Single,
    Triple
};
```

Không cung cấp tùy chọn tạo số vùng bất kỳ trong phiên bản đầu. Điều này giảm đáng kể số trường hợp kiểm thử, tránh phải xây Zone Editor và làm giao diện dễ hiểu.

### 4.2 Chế độ Một vùng

Chế độ Một vùng giữ nguyên:

```text
Source Catalog
      ↓
Preview
      ↓ CUT / FADE
Program
      ↓
Fullscreen Output
```

Các chức năng giữ nguyên gồm Preview, Program, CUT, FADE, playlist, preload, audio meter và fullscreen output.

### 4.3 Chế độ Ba vùng

Chế độ Ba vùng sử dụng:

```text
LEFT Player  ─┐
MAIN Player  ─┼─> Canvas Renderer ─> Một fullscreen output
RIGHT Player ─┘
```

Mỗi vùng có:

- Source hoặc playlist đang phát.
- Play, Pause, Previous, Next và Loop.
- Fit mode.
- Mute.
- Blackout riêng.

Phiên bản đầu sử dụng CUT khi thay source. FADE từng vùng được bổ sung sau khi CUT hoạt động ổn định.

### 4.4 Không có ba cặp Preview/Program

Ba cặp PVW/PGM sẽ tạo sáu player và làm người dùng khó hiểu. Với mục tiêu tối giản, click hoặc Quick Play vào một vùng sẽ thay trực tiếp nội dung vùng đó bằng CUT.

Nếu cần an toàn hơn, có thể giữ một Preview chung trong phiên bản sau:

```text
Chọn vùng → Chọn source → Preview chung → TAKE vào vùng đã chọn
```

Đây không phải yêu cầu của MVP.

## 5. Quy trình vận hành đề xuất

### 5.1 Thiết lập lần đầu

1. Chọn chế độ Ba vùng.
2. Nhập chiều rộng cánh trái.
3. Nhập chiều rộng màn chính.
4. Nhập chiều rộng cánh phải.
5. Nhập chiều cao chung.
6. Chọn màn hình output.
7. Mở Test Pattern.
8. Đối chiếu LEFT, MAIN và RIGHT với cấu hình Colorlight.
9. Lưu workspace.

Người dùng không phải nhập tọa độ X. MediaSwitcher tự tính:

```text
left.x  = 0
main.x  = left.width
right.x = left.width + main.width
canvas.width = left.width + main.width + right.width
```

### 5.2 Vận hành hằng ngày ở chế độ Một vùng

1. Mở workspace.
2. Chọn source vào Preview.
3. CUT hoặc FADE sang Program.
4. Mở fullscreen output.

Quy trình này không thay đổi so với hiện tại.

### 5.3 Vận hành hằng ngày ở chế độ Ba vùng

1. Chọn playlist cho LEFT.
2. Chọn playlist hoặc camera cho MAIN.
3. Chọn playlist cho RIGHT.
4. Kiểm tra thumbnail ba vùng.
5. Mở fullscreen output.
6. Dùng Play/Pause/Previous/Next riêng cho từng vùng.

### 5.4 Blackout và tình huống khẩn cấp

Cần có hai cấp blackout:

- Blackout Zone: chỉ đưa vùng được chọn về màu đen.
- Blackout All: đưa toàn canvas về màu đen ngay lập tức.

Blackout không được đóng decoder hoặc thay playlist. Khi bỏ blackout, nội dung tiếp tục từ trạng thái hiện tại.

## 6. Giao diện tối giản

### 6.1 Lựa chọn bố cục

```text
Bố cục màn hình

(•) Một vùng
( ) Ba vùng
```

### 6.2 Giao diện Một vùng

Giữ nguyên giao diện hiện tại. Không hiển thị các điều khiển LEFT, MAIN hoặc RIGHT.

### 6.3 Giao diện Ba vùng

```text
┌────────────────┬──────────────────┬────────────────┐
│      TRÁI      │      CHÍNH       │      PHẢI      │
│   Thumbnail    │    Thumbnail     │   Thumbnail    │
│ Tên nội dung   │ Tên nội dung     │ Tên nội dung   │
│  ◀  ▶  ■  ⏭   │  ◀  ▶  ■  ⏭    │  ◀  ▶  ■  ⏭   │
│ Loop  Mute     │ Loop  Audio      │ Loop  Mute     │
└────────────────┴──────────────────┴────────────────┘

        [MỞ OUTPUT] [TEST PATTERN] [BLACKOUT ALL]
```

Không cho kéo thay đổi kích thước vùng trong màn hình Live. Kích thước chỉ được chỉnh trong Settings.

### 6.4 Trạng thái màu

- Xanh lá: player đang phát.
- Vàng: đang pause hoặc buffering.
- Đỏ: lỗi source hoặc mất output.
- Xám: chưa có source.
- Đen: blackout.

Màu chỉ hỗ trợ nhận biết; mọi trạng thái phải có chữ để bảo đảm khả năng tiếp cận.

## 7. Kiến trúc kỹ thuật tối giản

### 7.1 Thành phần cần bổ sung

```text
LayoutSettings
TripleZoneController
CanvasRenderer
OutputTargetSettings
```

Không cần tạo hệ thống Scene, Cue hoặc Command Bus mới trong MVP.

### 7.2 Dữ liệu bố cục

```cpp
enum class FitMode {
    Fit,
    Fill,
    Stretch
};

struct ZoneRect {
    int x;
    int y;
    int width;
    int height;
};

struct TripleLayout {
    int canvasWidth;
    int canvasHeight;
    ZoneRect left;
    ZoneRect main;
    ZoneRect right;
};
```

### 7.3 Ba playback backend

```cpp
std::unique_ptr<ObsPlaybackBackend> m_leftPlayer;
std::unique_ptr<ObsPlaybackBackend> m_mainPlayer;
std::unique_ptr<ObsPlaybackBackend> m_rightPlayer;
```

Chỉ tạo ba object này khi `LayoutMode::Triple` được kích hoạt. Khi quay lại `Single`, phải đóng và giải phóng chúng.

### 7.4 CanvasRenderer

```cpp
void CanvasRenderer::render(uint32_t width, uint32_t height)
{
    clearBlack();

    if (m_layoutMode == LayoutMode::Single) {
        renderFullscreen(m_singleProgram, width, height);
        return;
    }

    renderZone(m_leftPlayer.get(),  m_layout.left);
    renderZone(m_mainPlayer.get(),  m_layout.main);
    renderZone(m_rightPlayer.get(), m_layout.right);
}
```

Mỗi `renderZone` đặt viewport, projection và transform cho vùng rồi gọi `obs_view_render`. Sau khi render phải khôi phục graphics state trước khi chuyển sang vùng kế tiếp.

### 7.5 Giới hạn vùng

Nội dung không được tràn sang vùng bên cạnh. Renderer cần dùng viewport và scissor/crop tương ứng. Nền canvas được xóa về màu đen trước mỗi frame để phần letterbox luôn ổn định.

### 7.6 Không mở ba cửa sổ

Ba player không đồng nghĩa ba output. Chỉ `ObsProgramOutputWindow` hoặc output window thay thế gọi CanvasRenderer. Cách này giảm rủi ro lệch frame và phù hợp với một input duy nhất của bộ xử lý LED.

## 8. Tích hợp với mã nguồn hiện tại

### 8.1 Canvas đang bị cố định

`ObsContext.cpp` hiện khai báo `OBS_WIDTH = 1920` và `OBS_HEIGHT = 1080`. Hai hằng số cần chuyển thành `ObsVideoConfig` được nạp trước `obs_reset_video`.

```cpp
struct ObsVideoConfig {
    uint32_t width;
    uint32_t height;
    ObsVideoFrameRate frameRate;
};
```

Không đổi canvas khi output hoặc decoder đang hoạt động. Khi người dùng đổi Layout Mode hoặc kích thước, ứng dụng phải dừng output, reset video có kiểm soát rồi mở lại.

### 8.2 Output hiện chỉ render một backend

`ObsProgramOutputWindow::draw` hiện lấy một backend và gọi `backend->render(width, height)`. Nên thay provider này bằng một interface nhỏ:

```cpp
class IProgramRenderer {
public:
    virtual ~IProgramRenderer() = default;
    virtual void render(uint32_t width, uint32_t height) = 0;
};
```

Single renderer gọi backend hiện tại. Triple renderer gọi ba backend theo ba viewport. Cửa sổ output không cần biết layout đang là Single hay Triple.

### 8.3 Chọn màn hình output

Mã hiện tại dùng màn hình thứ hai theo chỉ số. Chỉ số màn hình có thể đổi sau khi rút cáp hoặc khởi động lại. Settings cần lưu tên màn hình, geometry và resolution; nếu không tìm thấy đúng output, ứng dụng cảnh báo thay vì tự đưa Program lên màn hình chính.

### 8.4 ResourceManager

Giới hạn ba playback hiện tại phù hợp với ba player Program nhưng không còn chỗ cho preload hoặc fade. Trong MVP Triple Mode:

- Tắt preload toàn cục.
- Chỉ sử dụng CUT.
- Giới hạn đúng ba player.

Khi bổ sung FADE, mới mở rộng giới hạn tạm thời cho outgoing/incoming của vùng đang chuyển.

## 9. Fit, Fill và Stretch

### 9.1 Fit

Giữ đúng tỷ lệ và hiển thị toàn bộ nội dung. Có thể xuất hiện viền đen.

```text
Ưu điểm: không mất nội dung, không méo hình.
Nhược điểm: có viền đen nếu tỷ lệ khác vùng LED.
```

### 9.2 Fill

Giữ đúng tỷ lệ, phóng lớn tới khi lấp đầy vùng và cắt phần thừa.

```text
Ưu điểm: không có viền đen.
Nhược điểm: có thể mất phần ở mép video.
```

### 9.3 Stretch

Co giãn độc lập theo chiều ngang và chiều dọc.

```text
Ưu điểm: luôn lấp đầy toàn bộ vùng.
Nhược điểm: có thể làm méo hình.
```

Mặc định nên dùng `Fit` cho nội dung thông thường và `Fill` cho visual nền. `Stretch` cần có cảnh báo nhỏ trong tooltip.

## 10. Âm thanh

Mặc định chỉ MAIN phát âm thanh:

```text
LEFT  = Mute
MAIN  = Audio On
RIGHT = Mute
```

Cho phép người dùng bật âm thanh từng vùng, nhưng cần tránh việc cùng một video phát ba đường audio đồng thời. Trong MVP, nếu một vùng được bật audio thì các vùng còn lại tự động mute. Đây là quy tắc đơn giản và ngăn âm thanh cộng dồn.

Khi quay về Single Mode, Program audio hiện tại tiếp tục làm master.

## 11. Lưu cấu hình và tương thích ngược

### 11.1 Cấu hình đề xuất

```json
{
  "layoutMode": "single",
  "canvas": {
    "width": 3200,
    "height": 1080,
    "fpsNumerator": 60,
    "fpsDenominator": 1
  },
  "tripleLayout": {
    "leftWidth": 640,
    "mainWidth": 1920,
    "rightWidth": 640,
    "height": 1080
  },
  "output": {
    "screenName": "Colorlight",
    "expectedWidth": 3200,
    "expectedHeight": 1080
  }
}
```

### 11.2 Workspace cũ

Nếu workspace không có `layoutMode`, ứng dụng phải mặc định:

```text
layoutMode = Single
```

Không tự động tạo playlist trái/phải và không thay đổi canvas đã sử dụng trước đó.

### 11.3 Lưu nguyên tử

Ghi cấu hình ra file tạm, flush thành công rồi thay thế file cấu hình chính. Nếu ứng dụng bị tắt trong lúc lưu, workspace cũ vẫn phải đọc được. Chỉ lưu metadata và đường dẫn; không sao chép file media vào workspace.

## 12. Chuyển đổi giữa Một vùng và Ba vùng

### 12.1 Single sang Triple

1. Xác nhận không có FADE đang chạy.
2. Chuyển output về đen.
3. Dừng output window.
4. Đóng pipeline Single cần reset.
5. Áp dụng canvas Triple.
6. Khởi tạo ba player.
7. Mở lại output và bỏ blackout khi sẵn sàng.

### 12.2 Triple sang Single

1. Chuyển output về đen.
2. Đóng LEFT, MAIN và RIGHT player.
3. Giải phóng decoder và audio monitor.
4. Khôi phục pipeline PVW/PGM hiện tại.
5. Áp dụng canvas Single.
6. Mở lại output.

### 12.3 Không đổi chế độ khi đang live

Mặc định chỉ cho đổi Layout Mode khi output đã đóng. Có thể bổ sung quy trình đổi có blackout tự động sau, nhưng không cần trong MVP.

## 13. Hiệu năng và tài nguyên

### 13.1 Nguyên tắc

- Ba vùng tạo tối đa ba decoder hoạt động trong MVP.
- Không preload item tiếp theo.
- Thumbnail dùng ảnh nhỏ, không giữ decoder dài hạn.
- UI không render video full resolution nếu thumbnail không cần thiết.
- Canvas output chỉ render một lần mỗi frame.
- Không copy frame từ GPU về CPU để ghép canvas.

### 13.2 Nội dung cùng xuất hiện ở hai vùng

Tối ưu một decoder dùng nhiều render là hướng tốt, nhưng không bắt buộc trong phiên bản đầu nếu việc chia sẻ playback state làm tăng rủi ro. MVP có thể tạo hai player độc lập khi cùng đường dẫn xuất hiện ở LEFT và RIGHT; sau khi hệ thống ổn định mới tối ưu SourceSession dùng chung.

Quyết định này ưu tiên mã dễ kiểm chứng hơn tối ưu sớm. Tuy nhiên cần ghi log để đo trường hợp source trùng lặp và đánh giá nhu cầu thực tế.

### 13.3 Chỉ số phải theo dõi

- FPS output thực tế.
- Số frame bị trễ.
- CPU toàn tiến trình.
- RAM.
- GPU dedicated/shared memory.
- Số decoder hoạt động.
- Thời gian mở source.
- Trạng thái output và độ phân giải màn hình.

## 14. Xử lý lỗi

### 14.1 Source lỗi

Nếu một source không mở được:

- Vùng đó hiển thị màu đen hoặc frame lỗi có nhãn.
- Hai vùng còn lại tiếp tục phát.
- Không đóng toàn bộ output.
- Cho phép Retry hoặc chọn source khác.

### 14.2 Mất màn hình output

Nếu EDID hoặc cáp HDMI bị mất:

- Không chuyển output sang màn hình điều khiển.
- Giữ player hoạt động trong thời gian chờ ngắn có giới hạn.
- Hiển thị cảnh báo rõ ràng.
- Khi output trở lại, xác nhận resolution trước khi mở lại fullscreen.

### 14.3 Sai kích thước canvas

Nếu resolution màn hình hệ điều hành khác canvas:

```text
Canvas mong đợi : 3200 × 1080 @ 60 Hz
Output phát hiện: 1920 × 1080 @ 60 Hz
```

Ứng dụng phải cảnh báo và không tự scale trong chế độ Pixel Perfect. Tùy chọn `Cho phép scale` chỉ dành cho kiểm thử.

### 14.4 Player treo hoặc buffering

Mỗi vùng có trạng thái riêng. Không để một RTSP đang reconnect chặn render của hai file local ở vùng khác. Các thao tác open/close phải không chặn UI thread.

## 15. Test Pattern

Test Pattern là chức năng bắt buộc vì giúp tách lỗi mapping phần mềm khỏi lỗi cấu hình Colorlight.

Mẫu kiểm tra gồm:

- Nền khác màu cho LEFT, MAIN và RIGHT.
- Tên vùng cỡ chữ lớn.
- Tọa độ góc trên trái và kích thước vùng.
- Viền một pixel hoặc viền cấu hình được.
- Lưới mỗi 32 hoặc 64 pixel.
- Dấu tâm vùng.
- Thanh màu cơ bản.

Ví dụ:

```text
LEFT
x=0, y=0
640 × 1080
```

Test Pattern được render trực tiếp, không cần tạo file video.

## 16. Kế hoạch triển khai tối giản

### Giai đoạn 1: Layout Settings

- Thêm `LayoutMode`.
- Thêm kích thước canvas cấu hình được.
- Tính tự động ba Zone.
- Validate tổng chiều rộng và chiều cao.
- Workspace cũ mặc định Single.

### Giai đoạn 2: Test Pattern

- Render nền và nhãn ba vùng.
- Chọn output chính xác.
- Cảnh báo mismatch resolution.
- Kiểm thử với processor thật.

### Giai đoạn 3: Triple CUT Playback

- Tạo ba `ObsPlaybackBackend`.
- Render ba viewport trong một draw callback.
- Audio MAIN mặc định.
- Mỗi vùng chọn source và CUT trực tiếp.

### Giai đoạn 4: Playlist cơ bản

- Playlist riêng cho từng vùng.
- Previous, Next, Loop và Auto Next.
- Không preload.
- Không fade.

### Giai đoạn 5: Ổn định

- Stress test 12 giờ.
- Kiểm tra hot-plug output.
- Kiểm tra file 1080p, 4K, ảnh và RTSP.
- Đo CPU, RAM, GPU và frame drop.

### Giai đoạn sau MVP

- Một Preview chung cho vùng được chọn.
- FADE riêng từng vùng.
- Chia sẻ decoder khi cùng source và cùng playback clock.
- Cue đổi cả ba vùng.
- Shortcut hoặc Stream Deck.

## 17. Tiêu chí nghiệm thu

### 17.1 Tương thích Một vùng

- Workspace cũ mở được mà không cần chuyển đổi thủ công.
- Preview, Program, CUT, FADE và playlist hoạt động như trước.
- Không tạo LEFT/RIGHT decoder khi ở Single.
- Fullscreen output không thay đổi tỷ lệ ngoài ý muốn.

### 17.2 Ba vùng

- LEFT, MAIN và RIGHT hiển thị đúng tọa độ pixel.
- Ba vùng phát ba file khác nhau đồng thời.
- Pause một vùng không ảnh hưởng hai vùng còn lại.
- Next playlist một vùng không thay đổi vùng khác.
- Blackout Zone không đóng player.
- Blackout All tác động trong cùng một frame.
- Mặc định chỉ MAIN có audio.

### 17.3 Hiệu năng

- Không quá ba decoder hoạt động trong MVP Triple Mode.
- UI không bị block khi đổi source.
- Output giữ FPS mục tiêu trong cấu hình phần cứng được hỗ trợ.
- Không tăng RAM liên tục trong thử nghiệm 12 giờ.
- Không tạo thêm cửa sổ output hoặc desktop capture trung gian.

### 17.4 Khả năng phục hồi

- Source lỗi chỉ làm đen vùng liên quan.
- Mất output không đưa video lên màn hình điều khiển.
- Cấu hình không hỏng khi ứng dụng bị tắt đột ngột lúc lưu.
- Khởi động lại đọc đúng Layout Mode và kích thước vùng.

## 18. Rủi ro và biện pháp giảm thiểu

### Rủi ro 1: Processor không nhận resolution tùy chỉnh

Biện pháp: hỗ trợ canvas được đóng gói vào resolution chuẩn như 3840 × 2160; ba Zone nằm trong phần active area, phần còn lại màu đen.

### Rủi ro 2: Ba decoder vượt khả năng GPU

Biện pháp: sử dụng hardware decoding khi tương thích, không preload, giới hạn FPS và cung cấp panel chẩn đoán. Xây preset chất lượng thấp hơn cho máy yếu.

### Rủi ro 3: Nội dung tràn vùng

Biện pháp: bắt buộc viewport/scissor và test pattern kiểm tra đường biên.

### Rủi ro 4: Âm thanh phát nhiều lần

Biện pháp: chỉ cho một Audio Master trong MVP, mặc định MAIN.

### Rủi ro 5: Phá vỡ chế độ hiện tại

Biện pháp: giữ pipeline Single riêng; thêm Triple bằng feature flag và kiểm thử hồi quy Single trước mỗi bản phát hành.

### Rủi ro 6: Mất EDID làm đổi chỉ số màn hình

Biện pháp: lưu định danh và geometry output, không dựa vào `screens.at(1)`, không tự fallback sang primary display.

## 19. Quyết định cuối cùng

Thiết kế tối giản phù hợp nhất cho MediaSwitcher là:

```text
Hai chế độ: Single và Triple
Ba vùng cố định: LEFT, MAIN, RIGHT
Một canvas
Một fullscreen output
Ba player tối đa trong Triple MVP
CUT trước, FADE sau
Một Audio Master
Không Scene Editor
Không Cue trong MVP
Không remote control trong MVP
```

Thiết kế này giải quyết đúng nhu cầu màn chính và hai cánh mà không biến MediaSwitcher thành phần mềm media server phức tạp. Chế độ Một vùng tiếp tục ổn định như hiện tại; chế độ Ba vùng là một lựa chọn bổ sung và chỉ tiêu thụ tài nguyên khi được bật.

## Phụ lục A. Ma trận chức năng MVP

| Chức năng | Một vùng | Ba vùng MVP |
|---|---|---|
| Fullscreen output | Giữ nguyên | Một canvas chung |
| Preview/Program | Giữ nguyên | Chưa cần |
| CUT | Giữ nguyên | Có, theo vùng |
| FADE | Giữ nguyên | Sau MVP |
| Playlist | Giữ nguyên | Một playlist mỗi vùng |
| Preload | Giữ nguyên | Tắt trong MVP |
| Audio | Program master | MAIN master |
| Test Pattern | Có thể bổ sung | Bắt buộc |
| Số decoder mục tiêu | Theo pipeline hiện tại | Tối đa ba |
| Output window | Một | Một |

## Phụ lục B. Checklist trước khi lập trình

- Xác nhận độ phân giải pixel thực tế của ba phần LED.
- Xác nhận processor nhận resolution tùy chỉnh hay cần canvas đóng gói 4K.
- Xác nhận refresh rate 50, 59.94 hoặc 60 Hz.
- Xác nhận GPU có giải mã đồng thời ba nội dung mục tiêu.
- Sao lưu cấu hình LEDVISION/LEDSetting.
- Chốt quy tắc audio MAIN mặc định.
- Chốt hành vi vùng khi source lỗi: đen hoặc giữ frame cuối.
- Chốt Single canvas có dùng cùng kích thước Triple hay dùng profile riêng.
- Viết test hồi quy cho pipeline Single trước khi thêm Triple.

## Phụ lục C. Nguồn tham khảo

1. Colorlight, LEDVISION và LEDSetting: https://en.colorlightinside.com/product/download/381?language=en
2. Colorlight, LEDVISION User Manual V8.0: https://download.colorlightinside.com/LEDVISION-User%20Manual%20V8.0.pdf
3. Resolume, Advanced Output: https://www.resolume.com/support/advanced-output
4. Resolume, Input Maps: https://www.resolume.com/support/en/input-maps
5. Resolume, Slice Routing: https://www.resolume.com/support/en/slice-routing
6. Resolume, Groups: https://resolume.com/support/en/groups
7. OBS Studio, Power of Projectors: https://obsproject.com/kb/power-of-projectors
8. OBS Studio, Scene API Reference: https://docs.obsproject.com/reference-scenes
9. OBS Studio, Rendering Graphics: https://docs.obsproject.com/graphics
10. OBS Studio, Core API Views: https://docs.obsproject.com/reference-core
11. vMix, Features và Layer/MultiView: https://www.vmix.com/software/features.aspx
12. vMix, Shortcut Function Reference: https://uploads.vmix.com/help27/ShortcutFunctionReference.html
13. TouchDesigner, Multiple Monitors: https://docs.derivative.ca/Multiple_Monitors
14. TouchDesigner, Perfect Playback: https://docs.derivative.ca/Perfect_Playback

## Phụ lục D. Vị trí mã nguồn MediaSwitcher đã rà soát

- `src/engine/obs/ObsContext.cpp`: canvas libobs hiện cố định 1920 × 1080.
- `src/engine/obs/ObsPlaybackBackend.cpp`: mỗi backend sở hữu source, view và hàm render.
- `src/ui/ObsProgramOutputWindow.cpp`: output hiện gọi render một backend.
- `src/ui/ObsDualMediaTestWindow.cpp`: fullscreen hiện chọn màn hình thứ hai theo chỉ số.
- `src/engine/input/ResourceManager.h`: giới hạn tổng playback hiện là ba.
- `docs/MediaSwitcher - Product Vision.md`: mục tiêu Stability First, Performance First và Simple UI.
- `docs/MediaSwitcher – Resource & Playback Architecture.md`: nguyên tắc Source khác Playback Instance và giới hạn tài nguyên.

