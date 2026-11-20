"""Collect public SnapTray launch observations or render a saved day's last run."""

import argparse
from datetime import date, datetime, time, timedelta, timezone
import json
from pathlib import Path
import sqlite3
import urllib.error
import urllib.request


TAIPEI = timezone(timedelta(hours=8))
API = "https://api.github.com/repos/victorfu/snap-tray"
TOPIC = "https://news.hada.io/topic?id=37528"


def now():
    return datetime.now(TAIPEI).isoformat(timespec="microseconds")


def github(path):
    request = urllib.request.Request(API + path, headers={"User-Agent": "SnapTray-launch-report"})
    with urllib.request.urlopen(request, timeout=30) as response:
        return json.load(response)


def pages(path):
    page = 1
    while True:
        rows = github(f"{path}?per_page=100&page={page}")
        yield from rows
        if len(rows) < 100:
            return
        page += 1


def snapshot():
    stars = github("")["stargazers_count"]
    assets = {}
    for release in pages("/releases"):
        # Embedded arrays may be incomplete; always enumerate the asset endpoint.
        for asset in pages(f"/releases/{release['id']}/assets"):
            assets[str(asset["id"])] = [release["tag_name"], asset["name"], asset["download_count"]]
    return {"stars": stars, "assets": assets}


def forum_status():
    # Never try other identities, cookies, or endpoints after an access denial.
    with urllib.request.urlopen(TOPIC, timeout=20) as response:
        return f"HTTP {response.status}；公開指標待人工核對"


def attempt(fn):
    try:
        return fn(), None
    except urllib.error.HTTPError as exc:
        return None, f"HTTP {exc.code}"
    except (OSError, ValueError, KeyError) as exc:
        return None, type(exc).__name__


def collect(db):
    started = now()
    data, error = attempt(snapshot)
    github_at = now()
    forum, forum_error = attempt(forum_status)
    checked = now()
    with db:
        db.execute("INSERT INTO runs VALUES (?, ?, ?, ?, ?, ?)",
                   (checked, started, github_at, json.dumps(data) if data is not None else None,
                    error, forum_error or forum))
    return checked[:10]


def previous(db, sql, params):
    row = db.execute(sql, params).fetchone()
    return (row[0], json.loads(row[1])) if row else (None, None)


def asset_delta(key, current, past):
    if past is None:
        return "—"
    old = past["assets"].get(key)
    if not old:
        return "新增 ID" if current else "—"
    if not current:
        return "已移除"
    change = current[2] - old[2]
    return str(change) if change >= 0 else "計數下降，待核對"


def cell(value):
    return str(value).replace("|", "&#124;").replace("\n", " ")


def render(db, day):
    row = db.execute("SELECT * FROM runs WHERE substr(checked,1,10)=? ORDER BY checked DESC LIMIT 1",
                     (day,)).fetchone()
    if not row:
        raise ValueError(f"No saved observation for {day}")
    checked, started, github_at, raw, error, forum = row
    end = datetime.combine(date.fromisoformat(day), time.min, TAIPEI)
    lines = [f"# SnapTray 公開成效報表 · {day}", "",
             f"觀測開始 {started}，完成 {checked}（Asia/Taipei）。",
             f"官網日報區間 [{(end - timedelta(days=1)).isoformat()}, {end.isoformat()})。GA4 尚無授權資料。",
             "GitHub 為本次執行期間的公開快照；各版本查詢時間略有差異。", "",
             "## 渠道", "",
             f"- 韓國 [GeekNews]({TOPIC})，活動 `korea_launch_20261115`。11/20 已刊登，精確時分待補。",
             f"- 本次論壇檢查 {checked}，{forum}。分數、留言數與新增留言無可核對快照。",
             "- 泰國 DevHub，活動 `thailand_launch_20261115`。依 #311 最近紀錄已送審，公開網址與刊登時間待確認。",
             "- 兩市場的活動造訪、各平台下載點擊及 Store 點擊均無法取得。", "",
             "## GitHub 全站", "", f"[來源]({API}) · [各版本資產]({API}/releases) · 檢查時間 {github_at}"]
    success = "SELECT github_at, payload FROM runs WHERE payload IS NOT NULL"
    last_at, _ = previous(db, success + " AND checked<=? ORDER BY checked DESC LIMIT 1", (checked,))
    if error:
        lines.append(f"\n無法取得完整快照（{error}）。上次成功時間：{last_at or '尚無'}。")
        return "\n".join(lines) + "\n"
    data = json.loads(raw)
    base_at, base = previous(db, success + " AND checked<=? ORDER BY checked LIMIT 1", (checked,))
    prev_at, prev = previous(db, success + " AND checked<? ORDER BY checked DESC LIMIT 1", (day,))
    # A first observation cannot establish past growth.
    if base_at == github_at:
        base = None
    lines += ["", f"首次可核對觀測 {base_at}（韓國刊登後）；前一觀測日最後成功時間 {prev_at or '尚無'}。",
              "下列為全站同期變化，無法拆分為韓國或泰國的貢獻。相同檔名以不同 ID 重建時，各列新增及移除。",
              "", f"Stars：{data['stars']}。相對基準：{data['stars']-base['stars'] if base else '—'}；"
              f"相對前一觀測日：{data['stars']-prev['stars'] if prev else '—'}。",
              "", "| Asset ID | 版本 | 檔名 | 累計下載 | 相對基準 | 相對前一觀測日 |",
              "|---|---|---|---:|---|---|"]
    keys = set(data["assets"])
    for other in [base, prev]:
        if other:
            keys.update(other["assets"])
    for key in sorted(keys, key=int, reverse=True):
        current = data["assets"].get(key)
        item = current or (prev or {}).get("assets", {}).get(key) or base["assets"][key]
        lines.append(f"| {key} | {cell(item[0])} | {cell(item[1])} | {current[2] if current else '—'} | "
                     f"{asset_delta(key, current, base)} | {asset_delta(key, current, prev)} |")
    return "\n".join(lines) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--render", metavar="YYYY-MM-DD", help="render without network access")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    db = sqlite3.connect(args.output / "observations.sqlite3", timeout=30)
    try:
        db.execute("CREATE TABLE IF NOT EXISTS runs (checked TEXT PRIMARY KEY, started TEXT, github_at TEXT,"
                   "payload TEXT, github_error TEXT, forum_status TEXT)")
        day = date.fromisoformat(args.render).isoformat() if args.render else collect(db)
        # Serialize writers so concurrent reruns publish the latest saved observation.
        db.execute("BEGIN IMMEDIATE")
        report = args.output / f"{day}.md"
        tmp = report.with_suffix(".md.tmp")
        tmp.write_text(render(db, day), encoding="utf-8")
        tmp.replace(report)
        db.commit()
        print(report.resolve())
    finally:
        db.close()


if __name__ == "__main__":
    main()
