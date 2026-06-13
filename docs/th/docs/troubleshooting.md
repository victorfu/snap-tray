---
last_modified_at: 2026-06-13
layout: docs
title: การแก้ไขปัญหา
seo_title: "การแก้ไขปัญหา SnapTray: แก้ไขสิทธิ์ ปุ่มลัด และการบันทึก"
description: แก้ไขปัญหาสิทธิ์ การเริ่มต้น การติดตั้ง Qt การเข้ารหัส และปุ่มลัด รวมถึงปัญหาการบันทึกบน macOS และ Windows
permalink: /th/docs/troubleshooting/
lang: th
route_key: docs_troubleshooting
doc_group: advanced
doc_order: 2
---

## การจับภาพไม่เริ่มต้น

### macOS

- ตรวจสอบสิทธิ์ Screen Recording ใน `System Settings > Privacy & Security > Screen Recording`
- ตรวจสอบสิทธิ์ Accessibility ใน `System Settings > Privacy & Security > Accessibility`
- รีสตาร์ท SnapTray หลังจากเปลี่ยนสิทธิ์ใดๆ

### Windows

- อัปเดตไดรเวอร์ GPU หากการจับภาพล้มเหลวหรือตัวเลือกส่วนไม่ปรากฏขึ้น
- ยืนยันว่า runtime dependencies ที่จำเป็นถูกติดตั้งสำหรับบิลด์การพัฒนาในเครื่อง
- ตรวจสอบสิทธิ์ไมโครโฟนเฉพาะเมื่อบันทึกเสียง

## ไอคอนถาดระบบไม่ปรากฏ

- รีสตาร์ท SnapTray
- ยืนยันว่าแอปไม่ถูกบล็อกโดยการแจ้งเตือนความปลอดภัยของ OS
- หากใช้บิลด์การพัฒนาในเครื่อง รันสคริปต์บิลด์แพลตฟอร์มอีกครั้งและยืนยัน dependencies

หากแอปยังไม่ปรากฏ บิลด์ใหม่ด้วย:

- macOS/Linux beta: `./scripts/build.sh`
- Windows: `scripts\build.bat`

## Gatekeeper บล็อกการเปิดแอปบน macOS

รีลีสที่ได้รับการลงนามและรับรองอย่างเป็นทางการควรเปิดได้โดยไม่มีคำเตือน Gatekeeper

หากต้องการยืนยัน DMG ด้วยตนเอง:

```bash
spctl -a -vv -t open "dist/SnapTray-<version>-macOS.dmg"
```

สำหรับบิลด์ ad-hoc หรือการพัฒนาในเครื่องเท่านั้น คุณสามารถล้างแอตทริบิวต์ quarantine:

```bash
xattr -cr /Applications/SnapTray.app
```

## Windows แอปแสดง missing DLL หรือ Qt plugin errors

หากเห็นข้อความเช่น `Qt6Core.dll was not found` หรือ `no Qt platform plugin could be initialized` ให้ติดตั้ง Qt dependencies ด้วย `windeployqt`:

```batch
C:\Qt\6.10.1\msvc2022_64\bin\windeployqt.exe build\bin\SnapTray-Debug.exe
```

ใช้เส้นทางการติดตั้ง Qt เดียวกันกับที่คุณส่งไปยัง `CMAKE_PREFIX_PATH`

## ปุ่มลัดไม่ตอบสนอง

- เปิด Settings > Hotkeys และยืนยันว่าการดำเนินการยังคงกำหนดอยู่
- กำหนดการดำเนินการใหม่และทดสอบทันที
- ตรวจสอบการชนกันกับยูทิลิตี้ปุ่มลัดส่วนกลางอื่น
- บน Windows 11 ปิด `Settings > Accessibility > Keyboard > Use the Print screen key to open screen capture` ก่อนกำหนด `Print Screen` ให้กับ SnapTray
- เก็บการดำเนินการที่ใช้บ่อยที่สุดไว้บนคีย์คอมโบเดียวง่ายๆ

### Linux beta: แอปออกใน Wayland

Ubuntu 22.04 beta รองรับเฉพาะ X11 session หน้าจอเข้าสู่ระบบ ให้เลือก X11 session ก่อนเปิด SnapTray

### Linux beta: ปุ่มลัดไม่ลงทะเบียน

ปุ่มลัดส่วนกลางต้องการ X11 session และอาจขัดแย้งกับปุ่มลัดของ desktop environment เปิด Settings > Hotkeys เพื่อดูว่าปุ่มลัดใดล้มเหลวและกำหนดลำดับคีย์ใหม่

## ปัญหาการบันทึก (เฉพาะ macOS และ Windows)

Linux beta ไม่รวมการบันทึก สำหรับปัญหาการเริ่มต้น ปุ่มลัด หรือ Wayland ของ Linux beta ใช้หมายเหตุ Linux beta ด้านบน

- ลดเฟรมเรตหากพบ dropped frames
- ควรใช้ MP4 สำหรับการบันทึกยาว
- ตรวจสอบแหล่งเสียงและอุปกรณ์ที่เลือกอีกครั้ง
- บน macOS การจับเสียงระบบต้องการ macOS 13+ หรืออุปกรณ์เสียงเสมือนเช่น BlackHole

## ข้อผิดพลาดการบิลด์หรือการเปิดในเครื่อง

สำหรับสภาพแวดล้อมการพัฒนา ยืนยัน toolchain ทั้งหมดด้วยสคริปต์ repository:

- macOS/Linux beta: `./scripts/build.sh` แล้ว `./scripts/run-tests.sh`
- Windows: `scripts\build.bat` แล้ว `scripts\run-tests.bat`

หากดีบักปัญหาการบรรจุหรือการลงนาม ดำเนินการต่อใน [Release & Packaging](/developer/release-packaging/)
