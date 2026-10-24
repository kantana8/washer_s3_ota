# washer_s3_ota

ໂຄງການລະບົບຄວບຄຸມເຄື່ອງຊັກຜ້າຢອດຫຼຽນ/ສະແກນ QR ສຳລັບບອດຈໍລຸ້ນໃໝ່ **LCDWIKI ES3C35P (3.5" ESP32-S3, ST77922 QSPI, 16MB Flash + 8MB PSRAM)** ພ້ອມລະບົບອັບເດດເຟີມແວໄຮ້ສາຍໄລຍະໄກ (Remote Cloud OTA via WiFi/MQTT) ແລະ ລະບົບສຽງອອກທັງລຳໂພງໃນຕົວບອດ (ES8311) ແລະ ຂາ Buzzer ພາຍນອກ.

## 🌟 ຈຸດເດັ່ນຂອງລະບົບ (Features)
- **ຮອງຮັບບອດຈໍລຸ້ນໃໝ່ ES3C35P (ESP32-S3):**
  - ຂັບຈໍພາບ **ST77922 TDDI (320x480)** ຜ່ານ **QSPI Hardware DMA 80MHz** ພ້ອມລະບົບໝຸນຈໍລວງຂວາງ **480x320 Landscape** ອັດຕະໂນມັດ (Layout ຄືກັບຈໍ 3.5C ລຸ້ນເດີມທຸກປະການ).
  - ຮອງຮັບຈໍສຳຜັດ **Incell Capacitive Touch (I2C Address `0x55`)** ຜ່ານຂາ `SDA = IO38`, `SCL = IO39`, `RST = IO48`, `INT = IO47`.
  - ຮອງຮັບຊິບສຽງ **ES8311 I2S Audio Codec + FM8002E Amplifier** ໃນຕົວບອດ ເຮັດໃຫ້ສຽງຕິ໊ດ (Beep) ດັງອອກທັງລຳໂພງທີ່ສຽບໃສ່ບອດ ແລະ ຂາ Buzzer ພາຍນອກພ້ອມກັນ.
- **Dual Partition OTA (16MB Flash):** ແບ່ງ Partition ຂະໜາດ 6MB x 2 (`partitions_ota.csv`) ເພື່ອຮອງຮັບ 16MB Flash ຂອງບອດ ES3C35P ຢ່າງເຕັມປະສິດທິພາບ ຫາກໄຟດັບ ຫຼື ເນັດຫຼຸດກາງຄັນບອດຈະບໍ່ Brick.
- **Remote OTA Engine:** ສັ່ງອັບເດດເຟີມແວຜ່ານ MQTT Topic `tid/<terminal_id>/ota` ພ້ອມໜ້າຈໍສະແດງ Progress Bar ຕົວຈິງ 0% ຫາ 100%.
- **LaoQR Payment:** ຮອງຮັບການຊຳລະເງິນຜ່ານ LaoQR Code (BCEL One) ແບບໄດນາມິກຕາມໂໝດການຊັກ.
- **Hardware-Precise Relay:** ແຍກ FreeRTOS Task ຄຸມ Relay ເປັນອິດສະຫຼະ (Pulse 80ms HIGH / 120ms LOW).
- **Power-Loss Recovery:** ບັນທຶກເວລາ ແລະ ສະຖານະລົງ Flash Memory (`Preferences`) ເມື່ອໄຟມາກັບຄືນຈະນັບຖອຍຫຼັງຕໍ່ທັນທີ (Resume Wash).
- **Dual WiFi Profiles:** ຮອງຮັບ 2 WiFi (ຫຼັກ + ສຳຮອງ) ພ້ອມລະບົບ Auto-failover ແລະ Auto-fallback.
- **Lao Font Shaping:** ຮອງຮັບ Font ພາສາລາວ ແລະ ລະບົບຈັດສະຫຼະຊ້ອນ (Vowel Shaper) 100%.

---

## 🔌 ແຜນຜັງການຕໍ່ສາຍພາຍນອກ (Pinout ສຳລັບບອດ ES3C35P)

ຢູ່ດ້ານຫຼັງຂອງບອດ **ES3C35P** ຊ່ອງສຽບ 4-Pin ຂວາລຸ່ມສຸດ (**扩展接口 / Expansion Header** ທີ່ຂຽນວ່າ `IO45 | IO46 | GND | 3.3V`):

| ຂາເທິງບອດ ES3C35P | ການນຳໃຊ້ໃນລະບົບ | ລາຍລະອຽດ |
| :--- | :--- | :--- |
| **`IO45`** | **Relay Pin** | ສົ່ງສັນຍານ Pulse (80ms HIGH / 120ms LOW) ໄປຫາບອດເຄື່ອງຊັກຜ້າ |
| **`IO46`** | **Buzzer Pin** | ສົ່ງສັນຍານ PWM 2200Hz ໄປຫາ Buzzer ພາຍນອກ (ດັງພ້ອມກັບລຳໂພງ SPEAKER ໃນຕົວບອດ) |
| **`GND`** | **Ground** | ສາຍດິນຮ່ວມ (Common Ground) |
| **`3.3V`** | **3.3V Output** | ໄຟລ້ຽງ 3.3V |
| **`SPEAKER` (2-Pin ດ້ານເທິງ)** | **ລຳໂພງໃນຕົວບອດ** | ສຽບລຳໂພງທີ່ແຖມມາກັບບອດ ລະບົບຈະຂັບສຽງ Beep ຜ່ານຊິບ ES8311 + FM8002E ອັດຕະໂນມັດ |

---

## 🛠️ ຂໍ້ມູນດ້ານເຕັກນິກພາຍໃນບອດ (Internal Hardware Mapping)
- **MCU:** ESP32-S3 Dual-Core LX7 240MHz, 16MB QIO Flash + 8MB OPI PSRAM (N16R8)
- **Display (ST77922 QSPI):** `CS=IO10`, `CLK=IO12`, `D0=IO11`, `D1=IO13`, `D2=IO14`, `D3=IO9`, `BL=IO41`
- **Touch (ST77922 Incell I2C `0x55`):** `SDA=IO38`, `SCL=IO39`, `RST=IO48`, `INT=IO47`
- **Audio (ES8311 I2C `0x18` + I2S0):** `PA_EN=IO1` (Active LOW), `MCLK=IO17`, `BCLK=IO18`, `WS=IO21`, `DOUT=IO15`, `DIN=IO16`

---

## 🚀 ຄຳສັ່ງ Build ແລະ Upload
```powershell
pio run
pio run -t upload
pio device monitor -b 115200
```
