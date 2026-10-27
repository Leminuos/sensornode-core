# Môi trường test otad trên host

Bộ công cụ chạy và kiểm thử `sensornode-otad` trên máy build x86. Các thành phần phụ thuộc phần cứng được thay bằng file giả còn code của otad chạy đúng như trên board, kể cả phần đọc U-Boot env qua libubootenv.

## Yêu cầu

```bash
sudo apt install cmake g++ pkg-config python3 \
    nlohmann-json3-dev libsqlite3-dev libmosquitto-dev libubootenv-dev
```

Tuỳ chọn:

| Gói                              | Dùng khi                                                   |
|----------------------------------|------------------------------------------------------------|
| `libubootenv-tool`               | xem hoặc sửa env bằng `fw_printenv` / `fw_setenv`          |
| `socat`                          | gõ lệnh JSON trực tiếp vào socket thay vì dùng `otactl.py` |
| `mosquitto`, `mosquitto-clients` | chạy `MqttTest`, thiếu thì nhóm này bị skip                |

## Bắt đầu nhanh

```bash
tools/build-host.sh
(cd build && ctest --output-on-failure)
```

Kết quả mong đợi:

```
1/1 Test #1: otad-smoke .......................   Passed   10.3 sec
100% tests passed, 0 tests failed out of 1
```

## Test tự động

`smoke_test.py` gồm 41 test, mỗi test chạy một otad riêng với môi trường sạch và nói chuyện với nó qua control socket.

Chạy trực tiếp, không qua CTest:

```bash
python3 tests/otad/smoke_test.py                        # toàn bộ
python3 tests/otad/smoke_test.py -k StateMachineTest    # một nhóm
python3 tests/otad/smoke_test.py -k test_busy           # một test
python3 tests/otad/smoke_test.py --otad path/to/sensornode-otad
```

Khi một test fail, xem log của otad tại `build/otad-smoke/<tên test>/otad.log`.

## Chạy thủ công

Mở hai terminal.

**Terminal 1** - chạy otad:

```bash
tests/otad/run-otad.sh -f                                   # fake installer
tests/otad/run-otad.sh -t tests/otad/fake-swupdate/fail.sh  # SWUpdate giả báo lỗi
```

`run-otad.sh` tạo môi trường trong `build/otad-test/` nếu chưa có, rồi chạy otad với các đường dẫn trỏ vào đó. Mọi tham số được chuyển thẳng cho otad.

| Biến môi trường | Mặc định                     |
|-----------------|------------------------------|
| `OTAD_BIN`      | `build/otad/sensornode-otad` |
| `OTAD_TEST_DIR` | `build/otad-test`            |

**Terminal 2** - gửi lệnh:

```bash
tests/otad/otactl.py status
tests/otad/otactl.py --until done -t 10 '{"cmd":"install","url":"http://host/x.swu"}'
tests/otad/otactl.py -t 0
```

Ví dụ output:

```
  0.00  hello    ota=idle
  0.00  state    ota=downloading 0%
  0.00  reply    install ok=True
  0.20  progress ota=downloading 10%
  ...
  4.40  state    ota=done 100%
```

## Kịch bản môi trường

Tắt otad, tạo lại môi trường bằng `otadenv.py`, rồi chạy lại `run-otad.sh`.

| Kịch bản                        | Lệnh                                                                      |
|---------------------------------|---------------------------------------------------------------------------|
| Chạy slot A, ổn định (mặc định) | `tests/otad/otadenv.py build/otad-test`                                   |
| Đã cài vào B, chờ reboot        | `tests/otad/otadenv.py build/otad-test active_slot=B ustate=1`            |
| Đã reboot sang B, đang boot thử | `tests/otad/otadenv.py build/otad-test --running B ustate=1 boot_count=1` |
| Version firmware khác           | `tests/otad/otadenv.py build/otad-test --sw-version 0.3.0`                |
| Env chứa giá trị hỏng           | `tests/otad/otadenv.py build/otad-test boot_count=abc`                    |

Lệnh `status` đọc lại env mỗi lần gọi, nên có thể sửa env ngay khi otad đang chạy:

```bash
fw_setenv -c build/otad-test/fw_env.config ustate 1
```

## Manifest qua MQTT

`MqttTest` tự chạy một `mosquitto` riêng trên port ngẫu nhiên, không đụng broker của máy.

Chạy thủ công với broker của máy (port 1883):

```bash
tests/otad/run-otad.sh -f -m 127.0.0.1                          # terminal 1
tests/otad/otactl.py -t 0                                       # terminal 2, nghe event

# terminal 3: publish manifest retained như ota_server.py
mosquitto_pub -h 127.0.0.1 -t ota/latest -r -q 1 \
    -m '{"version":"0.3.0","url":"http://host:8000/release/x-0.3.0.swu"}'
mosquitto_pub -h 127.0.0.1 -t ota/latest -r -n                  # xoá retained
```

| Publish                                    | otad làm gì                                        |
|--------------------------------------------|----------------------------------------------------|
| version mới hơn `sw-versions`              | event `available`, `install` không url cài bản này |
| version bằng hoặc cũ hơn                   | `available` thành null                             |
| payload rỗng (`-n`)                        | `available` thành null                             |
| JSON sai, thiếu field, url có khoảng trắng | log `manifest rejected`, giữ bản cũ                |

## SWUpdate giả

Dùng với `-t tests/otad/fake-swupdate/<script>`. Mỗi script nhận url làm tham số đầu tiên, giống `09-swupdate-args`.

| Script        | Hành vi                                                | Kiểm tra                   |
|---------------|--------------------------------------------------------|----------------------------|
| `ok.sh`       | thành công sau 1 giây, in signal mask và process group | spawn đúng thiết lập       |
| `fail.sh`     | exit 1 kèm dòng `[ERROR]` như SWUpdate thật            | lấy lý do lỗi từ output    |
| `selfkill.sh` | tự chết bằng SIGKILL                                   | báo lỗi khi bị signal kill |
| `hang.sh`     | treo vô hạn, có process con                            | tắt otad giữa lúc cài      |
| `bg.sh`       | thoát ngay, để lại process cháu giữ pipe output        | không chờ EOF của pipe     |
| `noisy.sh`    | 50 000 dòng và một dòng dài hơn 1024 ký tự             | output lớn, cắt dòng dài   |
| `progress.py` | mở progress socket như SWUpdate thật, xem bảng dưới    | `ProgressClient`           |

`progress.py` lấy đường dẫn socket từ biến `FAKE_SWU_PROGRESS`, phải trùng với `-p` của otad. Kịch bản chọn bằng query của url:

| Url                      | Hành vi                                                      |
|--------------------------|--------------------------------------------------------------|
| `...x.swu` (`mode=ok`)   | tải 0..100%, cài 2 step, exit 0                              |
| `...x.swu?mode=late`     | để lại socket file cũ không ai listen, 0.5 giây sau mới bind |
| `...x.swu?mode=badapi`   | ack với API major 1, otad phải bỏ progress nhưng vẫn cài     |
| `...x.swu?mode=nosocket` | không mở socket, exit 0                                      |
| `...x.swu?mode=fail`     | lỗi giữa step 1, exit 1                                      |

Chạy thủ công:

```bash
FAKE_SWU_PROGRESS=$PWD/build/otad-test/swupdateprog \
    tests/otad/run-otad.sh -t tests/otad/fake-swupdate/progress.py -p build/otad-test/swupdateprog
tests/otad/otactl.py --until done -t 10 '{"cmd":"install","url":"http://host/x.swu?mode=late"}'
```

Với `--fake-installer`, url chứa chữ `fail` sẽ lỗi ở mốc 50% của bước cài.

## Reboot giả

Dùng với `-r tests/otad/fake-reboot/<script>` thay cho `systemctl reboot`.

| Script    | Hành vi                                                       |
|-----------|---------------------------------------------------------------|
| `ok.sh`   | gửi SIGTERM cho otad như systemd khi reboot thật, rồi exit 0  |
| `fail.sh` | exit 1 với `Failed to reboot: Access denied`                  |
| `hang.sh` | treo, để thử `reboot` lần hai và tắt otad khi lệnh còn chạy   |

```bash
tests/otad/run-otad.sh -f -r tests/otad/fake-reboot/ok.sh     # terminal 1
tests/otad/otactl.py --until done -t 10 '{"cmd":"install","url":"http://host/x.swu"}'
tests/otad/otactl.py reboot                                   # otad thoát với exit 0
```

Sau một lần cài xong, env trong `build/otad-test` vẫn ở trạng thái cũ (fake installer không ghi env), nên chạy lại otad sẽ về `idle`.

## Cấu trúc thư mục

```
tests/
+-- CMakeLists.txt        đăng ký otad-smoke với CTest, bỏ qua khi cross-compile
+-- otad/
    +-- README.md
    +-- otadenv.py        sinh môi trường giả lập
    +-- otactl.py         client cho control socket
    +-- run-otad.sh       chạy otad với môi trường giả lập
    +-- smoke_test.py     test tự động
    +-- fake-swupdate/    script thay thế 09-swupdate-args
    +-- fake-reboot/      script thay thế systemctl reboot
```

File sinh ra lúc chạy đều nằm trong `build/`:

| Thư mục                    | Tạo bởi         | Nội dung                          |
|----------------------------|-----------------|-----------------------------------|
| `build/otad-test/`         | `run-otad.sh`   | môi trường cho chạy thủ công      |
| `build/otad-smoke/<test>/` | `smoke_test.py` | môi trường và `otad.log` mỗi test |
