---
last_modified_at: 2026-06-13
layout: docs
title: CLI
seo_title: "CLI SnapTray: ระบบอัตโนมัติสกรีนช็อตและการบันทึกเฉพาะแพลตฟอร์ม"
description: อินเทอร์เฟซระบบอัตโนมัติอย่างเป็นทางการสำหรับสคริปต์การจับภาพ การบันทึกเฉพาะแพลตฟอร์ม การปักหมุด และเวิร์กโฟลว์การตั้งค่า
permalink: /th/docs/cli/
lang: th
route_key: docs_cli
doc_group: advanced
doc_order: 1
---

CLI คืออินเทอร์เฟซระบบอัตโนมัติอย่างเป็นทางการของ SnapTray ใช้เพื่อสคริปต์เวิร์กโฟลว์การจับภาพในเครื่อง ควบคุมแอปที่กำลังทำงานผ่าน IPC และจัดการการตั้งค่าจาก shell หรืองาน CI

## ติดตั้ง CLI helper

### macOS

เปิด Settings > General แล้วใช้การดำเนินการติดตั้ง CLI เพื่อสร้าง `/usr/local/bin/snaptray` ซึ่งต้องใช้สิทธิ์ผู้ดูแลระบบ

### Windows

เปิด Settings > General แล้วใช้การดำเนินการติดตั้ง CLI เพื่อเพิ่มไดเรกทอรีไฟล์ปฏิบัติการของ SnapTray ไปยัง `PATH` ของผู้ใช้ปัจจุบัน เปิด terminal ใหม่หลังจากติดตั้งหรือถอนการติดตั้ง

### Linux beta

เปิด Settings > General แล้วใช้การดำเนินการติดตั้ง CLI เพื่อสร้าง wrapper `~/.local/bin/snaptray` สำหรับ AppImage ตรวจสอบให้แน่ใจว่า `~/.local/bin` อยู่ใน `PATH` ของคุณ แล้วเปิด terminal ใหม่หลังจากติดตั้งหรือถอนการติดตั้ง

### บิลด์ Windows แบบบรรจุแล้ว

ตัวติดตั้งบางตัวอาจเปิดเผย `snaptray` ผ่าน `PATH` หรือ App Execution Alias อยู่แล้ว แต่ขั้นตอนการติดตั้งและลบในแอปคือพฤติกรรมมาตรฐานที่อธิบายโดยโค้ดแอปปัจจุบัน

## ตารางคำสั่ง

| คำสั่ง | คำอธิบาย | ต้องการ instance หลักของ SnapTray |
|---|---|---|
| `full` | จับภาพหน้าจอเต็มใต้เคอร์เซอร์ หรือหน้าจอที่เลือกด้วย `-n` | ไม่ |
| `screen` | จับภาพหน้าจอที่ระบุ หรือแสดงรายการหน้าจอด้วย `--list` | ไม่ |
| `region` | จับภาพ `-r x,y,width,height` จากหน้าจอที่เลือก | ไม่ |
| `gui` | เปิด GUI การจับภาพเฉพาะส่วน | ใช่ |
| `canvas` | สลับโหมดสกรีนแคนวาส | ใช่ |
| `record` | เริ่ม หยุด หรือสลับการบันทึก (เฉพาะ macOS/Windows) | ใช่ |
| `pin` | ปักหมุดไฟล์ภาพหรือภาพจากคลิปบอร์ด | ใช่ |
| `config` | แสดงรายการ ดึง ตั้งค่า หรือรีเซ็ตการตั้งค่า; ไม่มีตัวเลือกจะเปิดหน้าต่าง Settings | บางส่วน |

## ตัวอย่างคำสั่ง

```bash
# ความช่วยเหลือและเวอร์ชัน
snaptray --help
snaptray --version
snaptray full --help

# คำสั่งจับภาพในเครื่อง
snaptray full                         # จับภาพหน้าจอใต้เคอร์เซอร์
snaptray full -c                      # คัดลอกการจับภาพเต็มหน้าจอไปยังคลิปบอร์ด
snaptray full -d 1000 -o shot.png     # หน่วงเวลา 1 วินาที แล้วบันทึก
snaptray full -n 1 -o screen1.png     # จับภาพหน้าจอ 1 ไปยังไฟล์
snaptray full --raw > shot.png        # เขียนไบต์ PNG ไปยัง stdout
snaptray screen --list                # แสดงรายการหน้าจอที่ใช้ได้
snaptray screen 0 -c                  # จับภาพหน้าจอ 0 (ไวยากรณ์แบบ positional)
snaptray screen -n 1 -o screen1.png   # จับภาพหน้าจอ 1 (ไวยากรณ์แบบ option)
snaptray screen 1 -o screen1.png      # จับภาพหน้าจอ 1 ไปยังไฟล์
snaptray region -r 0,0,800,600 -c     # จับภาพส่วนจากหน้าจอ 0 ไปยังคลิปบอร์ด
snaptray region -n 1 -r 100,100,400,300 -o region.png
snaptray region -r 100,100,400,300 -o region.png

# คำสั่ง IPC
snaptray gui                          # เปิดตัวเลือกส่วน
snaptray gui -d 2000                  # เปิดหลังจาก 2 วินาที
snaptray canvas                       # สลับสกรีนแคนวาส
snaptray record start                 # เริ่มบันทึก (เฉพาะ macOS/Windows)
snaptray record stop                  # หยุดบันทึก (เฉพาะ macOS/Windows)
snaptray record                       # สลับการบันทึก (เฉพาะ macOS/Windows)
snaptray record start -n 1            # เริ่มบันทึกเต็มหน้าจอบนหน้าจอ 1 (เฉพาะ macOS/Windows)
snaptray pin -f image.png             # ปักหมุดไฟล์ภาพ
snaptray pin -c --center              # ปักหมุดภาพจากคลิปบอร์ดตรงกลาง
snaptray pin -f image.png -x 200 -y 120
snaptray config                       # เปิดหน้าต่าง Settings

# คำสั่ง config ในเครื่อง
snaptray config --list
snaptray config --get hotkey
snaptray config --set files/filenamePrefix SnapTray
snaptray config --reset
```

## หมายเหตุพฤติกรรม

- คำสั่งจับภาพ (`full`, `screen`, `region`) บันทึก PNG เป็นค่าเริ่มต้น
- `--clipboard` คัดลอกแทนการบันทึก `--raw` เขียนไบต์ PNG ไปยัง stdout
- `--output` มีความสำคัญเหนือ `--path` หากไม่ระบุทั้งคู่ SnapTray จะสร้างชื่อไฟล์ในไดเรกทอรีสกรีนช็อตที่กำหนดค่า
- `screen` รองรับทั้ง `snaptray screen 1` และ `snaptray screen -n 1`
- `region` ต้องการ `-r/--region` ใช้พิกเซลเชิงตรรกะสัมพัทธ์กับหน้าจอที่เลือก และสี่เหลี่ยมผืนผ้าต้องพอดีกับหน้าจอนั้น
- `record` รับ `start`, `stop` หรือ `toggle` บน macOS และ Windows ไม่มีการดำเนินการหมายถึง `toggle` ในการใช้งานปัจจุบัน `-n/--screen` ถูกใช้เฉพาะโดย `record start`
- Linux beta ไม่รวมการบันทึก คำสั่ง `record` ถูกซ่อนจากเอาต์พุต help; การเรียกโดยตรงจะส่งคืน recording error
- `pin` ต้องการ `--file` หรือ `--clipboard` อย่างใดอย่างหนึ่งเท่านั้น `--file` ต้องเป็นภาพที่อ่านได้ การวางตำแหน่งแบบกำหนดเองจะใช้เฉพาะเมื่อมีทั้ง `-x` และ `-y`; มิฉะนั้นหมุดจะอยู่ตรงกลาง
- `config --set` รับค่า positional เดียว `config --reset` ล้างร้านข้อมูลการตั้งค่าทั้งหมด

## รหัสส่งคืน

| รหัส | ความหมาย |
|---|---|
| `0` | สำเร็จ |
| `1` | ข้อผิดพลาดทั่วไป |
| `2` | อาร์กิวเมนต์ไม่ถูกต้อง |
| `3` | ข้อผิดพลาดไฟล์ |
| `4` | ข้อผิดพลาด instance (แอปหลักไม่ทำงาน) |
| `5` | Recording error รวมถึงการเรียก `record` โดยตรงบน Linux beta |

## เอกสารที่เกี่ยวข้อง

- [เริ่มต้นใช้งาน](/th/docs/getting-started/)
- [การแก้ไขปัญหา](/th/docs/troubleshooting/)
