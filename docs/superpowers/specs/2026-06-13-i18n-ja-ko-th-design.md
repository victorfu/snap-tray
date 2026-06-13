# Design: JP / KR / TH website localization

- **Date:** 2026-06-13
- **Status:** Approved (design), pending implementation plan
- **Surface:** `docs/` Jekyll site (snaptray.cc)

## Goal

Add Japanese (`ja`), Korean (`ko`), and Thai (`th`) to the marketing site and
user documentation so incoming JP/KR/TH search and ad traffic lands on native
pages. The site already supports `en` + `zh-tw`; this extends the same i18n
mechanism to five languages and makes the language-handling code scale instead
of hardcoding each new locale.

## Scope

**In scope (translated into ja/ko/th):**

- Marketing pages: homepage (`index.md` → hero / features / faq / downloads
  includes), `screenshot-tool.md`, `screenshot-tool-mac.md`,
  `screenshot-tool-windows.md`, `privacy-policy.md`, `releases.md`.
- The `_data/i18n/*.yml` marketing/FAQ/comparison copy (~462 lines/language)
  that feeds those pages.
- User docs: the `docs/` tree — 17 prose pages (`getting-started`, `tutorials/*`,
  `region-capture`, `annotation-tools`, `pin-window`, `screen-canvas`,
  `recording`, `hotkeys`, `settings`, `cli`, `troubleshooting`, `faq`,
  `index`).
- Navigation labels for the docs sidebar (`docs_nav.yml`).

**Out of scope:**

- `developer/` docs (`build-from-source`, `architecture`, `release-packaging`,
  `index`) — stay English-only. They are contributor-facing.
- Regenerating `llms.txt` / `llms-full.txt`.
- Any net-new marketing copy beyond translating what already exists.
- Pushing to the live `pages` branch (implemented on `main`; sync is the
  user's manual step, consistent with the prior DMG-copy fix).

## Decisions

1. **Translation source:** Claude generates all three languages.
2. **Scaffolding:** refactor the language switcher + hreflang from hardcoded
   `en`/`zh-tw` to a data-driven loop, so future languages are
   "add files + one registry row".
3. **Missing-translation handling:** a language link/alternate appears for a
   page only when that page's `route_key` has a URL in that language. English-only
   developer pages therefore surface only EN/繁中 — never dead JP/KR/TH links.
4. **Branch:** implement and build-verify on `main`.

## Architecture

### A. Language registry + scalable refactor

- `_config.yml`: `supported_langs: [en, zh-tw, ja, ko, th]`.
- **New `_data/languages.yml`** — ordered registry, one row per language, the
  single source of truth for display + SEO loops:

  | code  | name (native) | hreflang | locale |
  |-------|---------------|----------|--------|
  | en    | EN            | en       | en-US  |
  | zh-tw | 繁中          | zh-TW    | zh-TW  |
  | ja    | 日本語        | ja       | ja-JP  |
  | ko    | 한국어        | ko       | ko-KR  |
  | th    | ไทย           | th       | th-TH  |

- **`_includes/language-switcher.html`**: loop `site.data.languages`; for each
  language resolve the page's URL via `routes[page.route_key][code]` (falling
  back to the existing non-route URL heuristic for `en`/`zh-tw` only). Render a
  link only when a URL exists for that language. The aria-label stays localized
  via `i18n[lang].language.label`.
- **`_includes/head.html`**: hreflang block loops the registry, emitting
  `<link rel="alternate" hreflang="…">` only for languages where the page
  exists, plus `x-default` = the `en` URL. `og:locale` continues to read
  `i18n[lang].site.locale`.
- **`assets/js/releases.js`**: add `ja`/`ko`/`th` entries to `RELEASE_I18N`
  (download-button + releases-page strings) and extend `resolveLang()` to map
  the `<meta name="snaptray-lang">` / `<html lang>` value to the new codes.

### B. Routing + page trees

- `routes.yml`: for every in-scope route key, add `ja: /ja/…`, `ko: /ko/…`,
  `th: /th/…`. Developer route keys keep only `en`/`zh-tw`.
- New `ja/`, `ko/`, `th/` directories mirroring the in-scope subset of `zh-tw/`:
  - 6 marketing pages — thin wrappers: localized front-matter
    (`layout`, `description`, `permalink`, `lang`, `route_key`, `seo_title`)
    plus the shared `{% include %}` calls (privacy/releases carry their own
    prose where zh-tw does).
  - 17 user-doc prose pages — full translated bodies with localized front-matter
    (`layout: docs`, `title`, `seo_title`, `description`, `permalink`, `lang`,
    `route_key`, `doc_group`, `doc_order`).

### C. Translations

- `_data/i18n/{ja,ko,th}.yml` — full marketing/FAQ/comparison copy, including
  `site` (`locale`, `tagline`, `title_suffix`), `nav`, `language.label`, hero,
  features, downloads, FAQ, comparison tables, and the screenshot-tool landing
  sections.
- `docs_nav.yml` — add `ja`/`ko`/`th` to every group/item `title` map.
- All platform caveats carried verbatim in meaning into each language:
  Recording + OCR + GIF/WebP = macOS/Windows only; Linux is an Ubuntu 22.04 X11
  beta limited to capture/annotation; Wayland unsupported.

#### Translation glossary (locked before writing pages)

Brand name **SnapTray** stays Latin in every language. Core feature terms are
fixed up front so all pages agree:

| EN term         | ja              | ko          | th                       |
|-----------------|-----------------|-------------|--------------------------|
| Screenshot      | スクリーンショット | 스크린샷    | ภาพหน้าจอ                |
| Region capture  | 範囲キャプチャ   | 영역 캡처   | การจับภาพเฉพาะส่วน        |
| Annotation      | 注釈            | 주석        | การมาร์กอัป              |
| Pin window      | ピン留めウィンドウ | 핀 창       | หน้าต่างปักหมุด           |
| Screen Canvas   | スクリーンキャンバス | 스크린 캔버스 | สกรีนแคนวาส              |
| Recording       | 録画            | 녹화        | การบันทึกหน้าจอ          |
| Hotkeys         | ホットキー       | 단축키      | ปุ่มลัด                  |
| Settings        | 設定            | 설정        | การตั้งค่า               |
| System tray     | システムトレイ   | 시스템 트레이 | ถาดระบบ                 |
| Magnifier       | ルーペ           | 돋보기      | แว่นขยาย                 |
| OCR             | OCR（文字認識）  | OCR         | OCR                      |
| Auto blur       | 自動ぼかし       | 자동 블러   | เบลออัตโนมัติ            |

(Finalized/extended at translation time; this anchors the recurring vocabulary.)

### D. SEO

- Reciprocal `hreflang` alternates per page + `x-default=en`.
- `og:locale` = `ja_JP` / `ko_KR` / `th_TH` (derived from `site.locale`).
- Localized `seo_title` + `description` per page (the site already uses
  `seo_title`; EN targets 50–60 chars, CJK naturally shorter, Thai sized to fit).
- `jekyll-sitemap` auto-includes the new pages.

## Verification

- `cd docs && bundle exec jekyll build` passes with no Liquid/YAML errors.
- Spot-check rendered output for each new language: homepage, one user-doc page,
  the language switcher (correct links + active state), and `<link hreflang>` /
  `og:locale` tags in `<head>`.
- Confirm switcher on an English-only developer page shows only EN/繁中.

## Risks / follow-ups

- **Native proofread before heavy ad spend** — translations will be coherent and
  launch-ready, but a human pass on the homepage de-risks the marketing surface,
  Thai especially. Tracked as a post-merge follow-up, not a blocker.
- Live deploy requires syncing `main` → `pages`; out of scope here.
