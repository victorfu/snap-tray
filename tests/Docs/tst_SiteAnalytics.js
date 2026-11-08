// Website analytics contract tests (docs/assets/js/analytics.js).
// Usage: node tst_SiteAnalytics.js <repo-root>
'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const test = require('node:test');

const ROOT = path.resolve(process.argv[2] || path.join(__dirname, '..', '..'));
const analytics = require(path.join(ROOT, 'docs/assets/js/analytics.js'));

const HOST = 'snaptray.cc';
const REPO = 'victorfu/snap-tray';
const RULES = {
  mediums: ['community', 'social', 'article', 'video'],
  campaigns: {
    korea_launch_20261115: { market: 'KR', sources: ['geeknews'] },
    thailand_launch_20261115: { market: 'TH', sources: ['blognone', 'devhub', 'facebook'] }
  }
};
const KR_SEARCH = '?utm_source=geeknews&utm_medium=community&utm_campaign=korea_launch_20261115';
const TH_SEARCH = '?utm_source=blognone&utm_medium=community&utm_campaign=thailand_launch_20261115&utm_content=post01';
const T0 = 1_000_000_000_000;

function resolve(overrides) {
  return analytics.resolveAttribution({
    now: T0,
    search: '',
    referrer: '',
    ownHost: HOST,
    stored: null,
    rules: RULES,
    ...overrides
  });
}

test('recognized Korea and Thailand campaigns map to their target market', () => {
  const kr = resolve({ search: KR_SEARCH });
  assert.equal(kr.source, 'geeknews');
  assert.equal(kr.medium, 'community');
  assert.equal(kr.campaign, 'korea_launch_20261115');
  assert.equal(kr.market, 'KR');
  assert.equal(kr.content, '');

  const th = resolve({ search: TH_SEARCH });
  assert.equal(th.source, 'blognone');
  assert.equal(th.content, 'post01');
  assert.equal(th.market, 'TH');
});

test('unlisted UTM values are never passed through', () => {
  const unknownCampaign = resolve({ search: '?utm_source=me%40example.com&utm_campaign=free+text' });
  assert.deepEqual(
    [unknownCampaign.source, unknownCampaign.medium, unknownCampaign.campaign, unknownCampaign.market],
    ['other', 'other', 'other', 'unknown']
  );

  const badParts = resolve({
    search: '?utm_source=someone%40mail.com&utm_medium=email&utm_campaign=korea_launch_20261115&utm_content=hello@x'
  });
  assert.equal(badParts.campaign, 'korea_launch_20261115');
  assert.equal(badParts.source, 'other');
  assert.equal(badParts.medium, 'other');
  assert.equal(badParts.content, 'other');

  const tooLong = resolve({ search: `?utm_campaign=${'a'.repeat(65)}` });
  assert.equal(tooLong.campaign, 'other');

  const caseFolded = resolve({ search: '?utm_source=GeekNews&utm_medium=Community&utm_campaign=Korea_Launch_20261115' });
  assert.equal(caseFolded.source, 'geeknews');
  assert.equal(caseFolded.campaign, 'korea_launch_20261115');
});

test('attribution survives in-site and language navigation within the window', () => {
  const landing = resolve({ search: KR_SEARCH, referrer: 'https://news.hada.io/topic?id=1' });
  const stored = analytics.parseStoredAttribution(JSON.stringify(landing));
  const later = resolve({
    now: T0 + analytics.ATTRIBUTION_TTL_MS - 1,
    referrer: 'https://snaptray.cc/th/',
    stored
  });
  assert.equal(later.campaign, 'korea_launch_20261115');
  assert.equal(later.market, 'KR');
  assert.equal(later.firstSeen, T0);
  assert.equal(later.lastSeen, T0 + analytics.ATTRIBUTION_TTL_MS - 1);
});

test('a second recognized campaign replaces the first; an unrecognized one does not', () => {
  const stored = resolve({ search: KR_SEARCH });
  const replaced = resolve({ now: T0 + 1000, search: TH_SEARCH, stored });
  assert.equal(replaced.market, 'TH');

  const kept = resolve({ now: T0 + 1000, search: '?utm_campaign=spam', stored });
  assert.equal(kept.market, 'KR');
});

test('expired attribution and missing storage fall back to referrer classes', () => {
  const stored = resolve({ search: KR_SEARCH });
  const expired = resolve({ now: T0 + analytics.ATTRIBUTION_TTL_MS, referrer: 'https://snaptray.cc/ko/', stored });
  assert.deepEqual([expired.source, expired.medium, expired.market], ['unknown', 'unknown', 'unknown']);

  const direct = resolve({});
  assert.deepEqual([direct.source, direct.medium, direct.campaign], ['(direct)', '(none)', '']);

  const referral = resolve({ referrer: 'https://www.reddit.com/r/foo/comments/abc?token=secret' });
  assert.deepEqual([referral.source, referral.medium], ['reddit.com', 'referral']);

  const internalNoStore = resolve({ referrer: 'https://snaptray.cc/', stored: null });
  assert.equal(internalNoStore.source, 'unknown');

  const clockSkew = resolve({ now: T0 - 1, referrer: 'https://snaptray.cc/', stored });
  assert.equal(clockSkew.source, 'unknown');
});

test('stored attribution is validated before use', () => {
  assert.equal(analytics.parseStoredAttribution('not json'), null);
  assert.equal(analytics.parseStoredAttribution('{"source":"x"}'), null);
  assert.equal(analytics.parseStoredAttribution(''), null);
});

test('page location keeps only the path and recognized campaign tags', () => {
  assert.equal(
    analytics.sanitizePageLocation('https://snaptray.cc/ko/?email=a%40b.c&utm_campaign=x#frag', '?email=a%40b.c&utm_campaign=x', RULES),
    'https://snaptray.cc/ko/'
  );
  const landing = analytics.sanitizePageLocation(`https://snaptray.cc/th/${TH_SEARCH}&token=abc`, `${TH_SEARCH}&token=abc`, RULES);
  const url = new URL(landing);
  assert.equal(url.pathname, '/th/');
  assert.deepEqual([...url.searchParams.keys()].sort(), ['utm_campaign', 'utm_content', 'utm_medium', 'utm_source']);
  assert.equal(url.searchParams.get('utm_source'), 'blognone');
});

test('referrer is reduced to origin for other sites', () => {
  assert.equal(analytics.sanitizeReferrer('https://news.hada.io/topic?id=123&session=x', HOST), 'https://news.hada.io/');
  assert.equal(analytics.sanitizeReferrer('https://snaptray.cc/ko/?utm_source=x', HOST), 'https://snaptray.cc/ko/');
  assert.equal(analytics.sanitizeReferrer('', HOST), '');
  assert.equal(analytics.sanitizeReferrer('android-app://com.foo', HOST), '');
});

test('download links report platform and the clicked release tag', () => {
  const base = 'https://snaptray.cc/ko/';
  const cases = [
    ['https://github.com/victorfu/snap-tray/releases/download/v1.0.69/SnapTray-1.0.69.dmg', 'macos', 'v1.0.69'],
    ['https://github.com/victorfu/snap-tray/releases/download/v1.0.69/SnapTray-1.0.69-Setup.exe', 'windows', 'v1.0.69'],
    ['https://github.com/victorfu/snap-tray/releases/download/v1.0.60/SnapTray-1.0.60-x86_64.AppImage', 'linux', 'v1.0.60'],
    ['https://github.com/victorfu/snap-tray/releases/download/nightly%3Cx%3E/SnapTray.dmg', 'macos', 'unknown']
  ];
  cases.forEach(([href, platform, version]) => {
    assert.deepEqual(analytics.classifyLink(href, base, REPO), {
      name: 'download_click',
      params: { platform, release_version: version }
    });
  });
});

test('store, GitHub and non-download links are classified separately', () => {
  const base = 'https://snaptray.cc/';
  assert.deepEqual(analytics.classifyLink('https://apps.microsoft.com/detail/9p38m1hx9hvc', base, REPO),
    { name: 'store_click', params: { store: 'microsoft' } });
  assert.deepEqual(analytics.classifyLink('https://github.com/victorfu/snap-tray', base, REPO),
    { name: 'github_click', params: { destination_type: 'repository' } });
  assert.deepEqual(analytics.classifyLink('https://github.com/victorfu/snap-tray/releases/tag/v1.0.69', base, REPO),
    { name: 'github_click', params: { destination_type: 'releases' } });
  assert.deepEqual(analytics.classifyLink('https://github.com/victorfu/snap-tray/issues', base, REPO),
    { name: 'github_click', params: { destination_type: 'issues' } });
  assert.deepEqual(analytics.classifyLink('https://github.com/victorfu/snap-tray/releases/download/v1/checksums.txt', base, REPO),
    { name: 'github_click', params: { destination_type: 'release_asset' } });

  assert.equal(analytics.classifyLink('#download', base, REPO), null, 'jump-to-download is not a download');
  assert.equal(analytics.classifyLink('/ko/#download', base, REPO), null);
  assert.equal(analytics.classifyLink('#', base, REPO), null, 'unpopulated download button');
  assert.equal(analytics.classifyLink('https://github.com/victorfu/snap-tray-fork', base, REPO), null);
  assert.equal(analytics.classifyLink('https://github.com/other/repo', base, REPO), null);
  assert.equal(analytics.classifyLink('javascript:alert(1)', base, REPO), null);
});

test('consent choice expires after a year and rejects malformed values', () => {
  const at = T0;
  const granted = JSON.stringify({ choice: 'granted', at });
  assert.equal(analytics.readConsent(granted, at + 1), 'granted');
  assert.equal(analytics.readConsent(JSON.stringify({ choice: 'denied', at }), at), 'denied');
  assert.equal(analytics.readConsent(granted, at + analytics.CONSENT_TTL_MS), null);
  assert.equal(analytics.readConsent('{"choice":"yes","at":1}', at), null);
  assert.equal(analytics.readConsent('garbage', at), null);
  assert.equal(analytics.readConsent(null, at), null);
});

test('event parameters carry attribution without empty values', () => {
  assert.deepEqual(analytics.attributionParams(resolve({})), {
    traffic_source: '(direct)',
    traffic_medium: '(none)',
    traffic_campaign: '(none)',
    traffic_content: '(none)',
    target_market: 'unknown'
  });
});

test('shipped campaign allow-list only uses safe identifiers', () => {
  const text = fs.readFileSync(path.join(ROOT, 'docs/_data/analytics_campaigns.yml'), 'utf8');
  const values = [...text.matchAll(/^\s*(?:- )?([a-z0-9_-]+)(?::.*)?$/gm)].map((match) => match[1]);
  assert.ok(values.includes('korea_launch_20261115'));
  assert.ok(values.includes('thailand_launch_20261115'));
  values.forEach((value) => assert.match(value, /^[a-z0-9][a-z0-9_-]{0,63}$/));
  [...text.matchAll(/^\s+market: (\S+)$/gm)].forEach((match) => assert.match(match[1], /^(KR|TH)$/));
});

test('analytics is off by default and every language has consent strings', () => {
  const config = fs.readFileSync(path.join(ROOT, 'docs/_config.yml'), 'utf8');
  assert.match(config, /^\s+ga4_measurement_id: "(G-[A-Z0-9]{4,16})?"$/m);
  ['en', 'zh-tw', 'ja', 'ko', 'th'].forEach((lang) => {
    const text = fs.readFileSync(path.join(ROOT, `docs/_data/i18n/${lang}.yml`), 'utf8');
    const block = text.match(/^analytics_consent:\n((?: {2}.+\n)+)/m);
    assert.ok(block, `${lang} has analytics_consent`);
    ['title', 'body', 'accept', 'decline', 'details', 'settings'].forEach((key) => {
      assert.match(block[1], new RegExp(`^  ${key}: .+$`, 'm'), `${lang}.${key}`);
    });
  });
});

// --- Browser wiring with a minimal fake DOM ---------------------------------

function fakeElement(attrs = {}) {
  const handlers = {};
  return {
    hidden: attrs.hidden ?? false,
    attrs,
    handlers,
    addEventListener(type, fn) { handlers[type] = fn; },
    getAttribute(name) { return this.attrs[name] ?? null; },
    click() { handlers.click && handlers.click({}); }
  };
}

function fakeStorage(initial = {}) {
  const data = { ...initial };
  return {
    data,
    getItem: (key) => (key in data ? data[key] : null),
    setItem: (key, value) => { data[key] = String(value); },
    removeItem: (key) => { delete data[key]; }
  };
}

function makeWindow(options = {}) {
  const accept = fakeElement();
  const decline = fakeElement();
  const banner = fakeElement({ hidden: true });
  banner.hidden = true;
  banner.querySelector = (sel) => (sel === '[data-analytics-accept]' ? accept : decline);
  const settings = fakeElement();
  settings.hidden = true;
  const docHandlers = {};
  const appended = [];
  const config = {
    measurementId: 'G-TEST1234',
    productionHost: HOST,
    repo: REPO,
    rules: RULES
  };
  const doc = {
    readyState: 'complete',
    referrer: options.referrer || '',
    cookie: '',
    body: { dataset: { lang: 'ko' } },
    documentElement: { lang: 'ko' },
    head: { appendChild: (node) => appended.push(node) },
    getElementById: (id) => {
      if (id === 'snaptray-analytics-config') {
        return options.noConfig ? null : { textContent: JSON.stringify(config) };
      }
      if (id === 'analytics-consent') return banner;
      return null;
    },
    querySelectorAll: (sel) => (sel === '[data-analytics-settings]' ? [settings] : []),
    createElement: () => ({}),
    addEventListener: (type, fn) => { docHandlers[type] = fn; }
  };
  const win = {
    document: doc,
    location: {
      hostname: options.hostname || HOST,
      href: `https://${options.hostname || HOST}/ko/${options.search || ''}`,
      search: options.search || ''
    },
    navigator: options.navigator || {},
    localStorage: options.localStorage || fakeStorage(),
    sessionStorage: options.sessionStorage || fakeStorage()
  };
  return { win, doc, banner, accept, decline, settings, docHandlers, appended };
}

function linkEvent(href, placement, extra = {}) {
  const link = {
    getAttribute: () => href,
    closest: (sel) => (sel === '[data-analytics-placement]' && placement
      ? { getAttribute: () => placement }
      : null)
  };
  return { type: 'click', button: 0, target: { closest: () => link }, ...extra };
}

function sentEvents(win) {
  return (win.dataLayer || []).map((args) => Array.from(args)).filter((args) => args[0] === 'event');
}

test('nothing loads before consent; the prompt is shown', () => {
  const env = makeWindow({ search: KR_SEARCH });
  analytics.boot(env.win);
  assert.equal(env.banner.hidden, false);
  assert.equal(env.settings.hidden, false);
  assert.equal(env.appended.length, 0, 'gtag.js not loaded');
  assert.equal(env.win.dataLayer, undefined);
  assert.equal(env.docHandlers.click, undefined);
  assert.deepEqual(env.win.sessionStorage.data, {}, 'no attribution stored before consent');
});

test('accepting loads GA with sanitized page data and tracks downloads', () => {
  const env = makeWindow({ search: `${KR_SEARCH}&email=a%40b.c`, referrer: 'https://news.hada.io/topic?id=9' });
  analytics.boot(env.win);
  env.accept.click();

  assert.equal(env.banner.hidden, true);
  assert.equal(env.appended.length, 1);
  assert.match(env.appended[0].src, /^https:\/\/www\.googletagmanager\.com\/gtag\/js\?id=G-TEST1234$/);
  assert.equal(JSON.parse(env.win.localStorage.data['snaptray.analyticsConsent']).choice, 'granted');

  const calls = env.win.dataLayer.map((args) => Array.from(args));
  const consent = calls.find((args) => args[0] === 'consent');
  assert.equal(consent[2].ad_storage, 'denied');
  const config = calls.find((args) => args[0] === 'config');
  assert.equal(config[2].send_page_view, false);
  assert.equal(config[2].allow_google_signals, false);
  assert.ok(!config[2].page_location.includes('email'), config[2].page_location);
  assert.equal(config[2].page_referrer, 'https://news.hada.io/');
  assert.equal(config[2].traffic_type, undefined);

  const pageView = sentEvents(env.win).find((args) => args[1] === 'page_view');
  assert.equal(pageView[2].traffic_campaign, 'korea_launch_20261115');
  assert.equal(pageView[2].target_market, 'KR');
  assert.equal(pageView[2].page_language, 'ko');

  const before = sentEvents(env.win).length;
  env.docHandlers.click(linkEvent('#download', 'hero'));
  assert.equal(sentEvents(env.win).length, before, 'jump link not counted');

  const event = linkEvent('https://github.com/victorfu/snap-tray/releases/download/v1.0.69/SnapTray-1.0.69.dmg', 'download_card');
  let prevented = false;
  event.preventDefault = () => { prevented = true; };
  env.docHandlers.click(event);
  const download = sentEvents(env.win).at(-1);
  assert.equal(download[1], 'download_click');
  assert.equal(download[2].platform, 'macos');
  assert.equal(download[2].release_version, 'v1.0.69');
  assert.equal(download[2].placement, 'download_card');
  assert.equal(download[2].traffic_source, 'geeknews');
  assert.equal(download[2].transport_type, 'beacon');
  assert.equal(prevented, false, 'navigation never blocked');

  env.docHandlers.auxclick(linkEvent('https://apps.microsoft.com/detail/9p38m1hx9hvc', null, { type: 'auxclick', button: 2 }));
  assert.equal(sentEvents(env.win).at(-1)[1], 'download_click', 'right click ignored');
  env.docHandlers.auxclick(linkEvent('https://apps.microsoft.com/detail/9p38m1hx9hvc', null, { type: 'auxclick', button: 1 }));
  assert.equal(sentEvents(env.win).at(-1)[1], 'store_click');
  assert.equal(sentEvents(env.win).at(-1)[2].placement, 'other');
});

test('declining after accepting stops events and clears stored data', () => {
  const env = makeWindow({ search: KR_SEARCH });
  analytics.boot(env.win);
  env.accept.click();
  env.doc.cookie = '_ga=GA1.1.1; _ga_TEST1234=GS1; other=1';
  env.settings.click();
  assert.equal(env.banner.hidden, false);
  env.decline.click();

  assert.equal(JSON.parse(env.win.localStorage.data['snaptray.analyticsConsent']).choice, 'denied');
  assert.equal(env.win.sessionStorage.data['snaptray.attribution'], undefined);
  const consentUpdate = env.win.dataLayer.map((args) => Array.from(args)).at(-1);
  assert.deepEqual(consentUpdate, ['consent', 'update', { analytics_storage: 'denied' }]);

  const before = sentEvents(env.win).length;
  env.docHandlers.click(linkEvent('https://apps.microsoft.com/detail/9p38m1hx9hvc', 'download_card'));
  assert.equal(sentEvents(env.win).length, before);
});

test('stored consent, privacy signals, test traffic and host checks', () => {
  const now = Date.now();
  const granted = makeWindow({
    search: '?analytics_test=1',
    localStorage: fakeStorage({ 'snaptray.analyticsConsent': JSON.stringify({ choice: 'granted', at: now }) })
  });
  analytics.boot(granted.win);
  assert.equal(granted.banner.hidden, true);
  assert.equal(granted.appended.length, 1);
  const config = granted.win.dataLayer.map((args) => Array.from(args)).find((args) => args[0] === 'config');
  assert.equal(config[2].traffic_type, 'internal');
  assert.equal(granted.win.sessionStorage.data['snaptray.analyticsTest'], '1');

  const denied = makeWindow({
    localStorage: fakeStorage({ 'snaptray.analyticsConsent': JSON.stringify({ choice: 'denied', at: now }) })
  });
  analytics.boot(denied.win);
  assert.equal(denied.banner.hidden, true);
  assert.equal(denied.appended.length, 0);

  const gpc = makeWindow({ navigator: { globalPrivacyControl: true } });
  analytics.boot(gpc.win);
  assert.equal(gpc.banner.hidden, true, 'GPC: no prompt, nothing loaded');
  assert.equal(gpc.settings.hidden, true, 'GPC: no opt-in entry point');
  assert.equal(gpc.appended.length, 0);

  const dntWithStoredGrant = makeWindow({
    navigator: { doNotTrack: '1' },
    localStorage: fakeStorage({ 'snaptray.analyticsConsent': JSON.stringify({ choice: 'granted', at: now }) })
  });
  analytics.boot(dntWithStoredGrant.win);
  assert.equal(dntWithStoredGrant.appended.length, 0, 'DNT overrides an earlier Allow');

  const preview = makeWindow({ hostname: 'victorfu.github.io' });
  analytics.boot(preview.win);
  assert.equal(preview.banner.hidden, true);
  assert.equal(preview.settings.hidden, true);

  const disabled = makeWindow({ noConfig: true });
  analytics.boot(disabled.win);
  assert.equal(disabled.banner.hidden, true);
});

test('blocked storage still lets the visitor accept and download', () => {
  const throwing = {
    getItem() { throw new Error('blocked'); },
    setItem() { throw new Error('blocked'); },
    removeItem() { throw new Error('blocked'); }
  };
  const env = makeWindow({ localStorage: throwing, sessionStorage: throwing, referrer: 'https://snaptray.cc/' });
  analytics.boot(env.win);
  assert.equal(env.banner.hidden, false);
  env.accept.click();
  const pageView = sentEvents(env.win).find((args) => args[1] === 'page_view');
  assert.equal(pageView[2].traffic_source, 'unknown', 'degraded, not invented');
});
