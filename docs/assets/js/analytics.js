/*
 * Website analytics for snaptray.cc (GA4, opt-in).
 *
 * Contract: docs/developer/site-analytics.md. Nothing here runs unless the
 * production build emitted #snaptray-analytics-config, the page is served from
 * the configured host, the browser sends no GPC/DNT signal and the visitor
 * chose "Allow". Tracking never blocks or
 * delays navigation and never sends query strings, form input or full URLs of
 * other sites.
 */
(function (root, factory) {
  const core = factory();
  if (typeof module === 'object' && module.exports) {
    module.exports = core;
  } else {
    root.SnapTrayAnalytics = core;
    core.boot(root);
  }
}(typeof window !== 'undefined' ? window : globalThis, function () {
  'use strict';

  const UTM_KEYS = ['utm_source', 'utm_medium', 'utm_campaign', 'utm_content'];
  const VALUE_PATTERN = /^[a-z0-9][a-z0-9_-]{0,63}$/;
  const CONTENT_PATTERN = /^[a-z]{1,16}[0-9]{1,3}$/;
  const VERSION_PATTERN = /^v?\d+(\.\d+){1,3}([-.][0-9a-z.]{1,24})?$/i;
  const MEASUREMENT_ID_PATTERN = /^G-[A-Z0-9]{4,16}$/;
  const OTHER = 'other';
  const UNKNOWN = 'unknown';
  const DIRECT = '(direct)';
  const NONE = '(none)';
  const ATTRIBUTION_TTL_MS = 30 * 60 * 1000;
  const CONSENT_TTL_MS = 365 * 24 * 60 * 60 * 1000;
  const CONSENT_KEY = 'snaptray.analyticsConsent';
  const ATTRIBUTION_KEY = 'snaptray.attribution';
  const TEST_TRAFFIC_KEY = 'snaptray.analyticsTest';
  const TEST_TRAFFIC_PARAM = 'analytics_test';
  const STORE_HOST = 'apps.microsoft.com';
  const GITHUB_HOST = 'github.com';
  const ASSET_PLATFORMS = [
    { suffix: '.dmg', platform: 'macos' },
    { suffix: '.exe', platform: 'windows' },
    { suffix: '.appimage', platform: 'linux' }
  ];
  const GITHUB_DESTINATIONS = { '': 'repository', releases: 'releases', issues: 'issues', blob: 'source', tree: 'source' };

  function normalizeValue(raw) {
    if (typeof raw !== 'string') return null;
    const value = raw.trim().toLowerCase();
    return VALUE_PATTERN.test(value) ? value : null;
  }

  // Maps the landing URL's UTM tags onto the allow-list. Returns null when the
  // URL carries no UTM tag at all. Unlisted values never leave the page as-is.
  function campaignFromSearch(search, rules) {
    const params = new URLSearchParams(search || '');
    if (!UTM_KEYS.some((key) => params.has(key))) return null;

    const raw = {};
    UTM_KEYS.forEach((key) => { raw[key] = normalizeValue(params.get(key)); });
    const rule = raw.utm_campaign && Object.prototype.hasOwnProperty.call(rules.campaigns, raw.utm_campaign)
      ? rules.campaigns[raw.utm_campaign]
      : null;

    if (!rule) {
      return { source: OTHER, medium: OTHER, campaign: OTHER, content: '', market: UNKNOWN, recognized: false };
    }

    let content = '';
    if (params.has('utm_content')) {
      content = raw.utm_content && CONTENT_PATTERN.test(raw.utm_content) ? raw.utm_content : OTHER;
    }

    return {
      source: rule.sources.includes(raw.utm_source) ? raw.utm_source : OTHER,
      medium: rules.mediums.includes(raw.utm_medium) ? raw.utm_medium : OTHER,
      campaign: raw.utm_campaign,
      content,
      market: rule.market,
      recognized: true
    };
  }

  function parseUrl(value, base) {
    try {
      return new URL(value, base);
    } catch (error) {
      return null;
    }
  }

  function referrerKind(referrer, ownHost) {
    const url = referrer ? parseUrl(referrer) : null;
    if (!url || !/^https?:$/.test(url.protocol)) return { kind: 'direct' };
    if (url.hostname === ownHost) return { kind: 'internal' };
    return { kind: 'external', host: url.hostname.replace(/^www\./, '') };
  }

  /*
   * Attribution rules (tab-scoped, never a persistent visitor ID):
   * 1. A recognized campaign in the URL starts a new attribution, replacing any earlier one.
   * 2. Otherwise a stored attribution younger than 30 minutes since the last page view is kept,
   *    so in-site navigation and language switches do not reset the source.
   * 3. Otherwise an unrecognized UTM tag yields "other", an external referrer yields
   *    "<host> / referral", no referrer yields "(direct) / (none)", and an internal referrer
   *    without stored attribution (expired, new tab, storage blocked) yields "unknown".
   */
  function resolveAttribution(input) {
    const now = input.now;
    const landing = campaignFromSearch(input.search, input.rules);
    const stored = input.stored;

    if (landing && landing.recognized) {
      return { ...stripRecognized(landing), firstSeen: now, lastSeen: now };
    }
    if (stored && typeof stored.lastSeen === 'number' && now - stored.lastSeen >= 0 && now - stored.lastSeen < ATTRIBUTION_TTL_MS) {
      return { ...stored, lastSeen: now };
    }
    if (landing) {
      return { ...stripRecognized(landing), firstSeen: now, lastSeen: now };
    }

    const ref = referrerKind(input.referrer, input.ownHost);
    const base = { campaign: '', content: '', market: UNKNOWN, firstSeen: now, lastSeen: now };
    if (ref.kind === 'external') return { ...base, source: ref.host, medium: 'referral' };
    if (ref.kind === 'direct') return { ...base, source: DIRECT, medium: NONE };
    return { ...base, source: UNKNOWN, medium: UNKNOWN };
  }

  function stripRecognized(landing) {
    const { recognized, ...rest } = landing;
    return rest;
  }

  function parseStoredAttribution(text) {
    if (!text) return null;
    try {
      const value = JSON.parse(text);
      const fields = ['source', 'medium', 'campaign', 'content', 'market'];
      if (!value || fields.some((field) => typeof value[field] !== 'string') || typeof value.lastSeen !== 'number') {
        return null;
      }
      return value;
    } catch (error) {
      return null;
    }
  }

  // Page URL sent to GA: origin + path only. Recognized campaign tags are kept on
  // the landing page so GA's own acquisition reports still see the campaign.
  function sanitizePageLocation(href, search, rules) {
    const url = parseUrl(href);
    if (!url) return '';
    const landing = campaignFromSearch(search, rules);
    const out = `${url.origin}${url.pathname}`;
    if (!landing || !landing.recognized) return out;

    const params = new URLSearchParams();
    params.set('utm_campaign', landing.campaign);
    if (landing.source !== OTHER) params.set('utm_source', landing.source);
    if (landing.medium !== OTHER) params.set('utm_medium', landing.medium);
    if (landing.content && landing.content !== OTHER) params.set('utm_content', landing.content);
    return `${out}?${params.toString()}`;
  }

  // Referrer sent to GA: other sites are reduced to their origin; same-site
  // referrers keep only their path.
  function sanitizeReferrer(referrer, ownHost) {
    const url = referrer ? parseUrl(referrer) : null;
    if (!url || !/^https?:$/.test(url.protocol)) return '';
    return url.hostname === ownHost ? `${url.origin}${url.pathname}` : `${url.origin}/`;
  }

  // Classifies an activated link. Release version and platform come from the
  // clicked asset URL itself, so a Linux fallback to an older release reports
  // that older tag rather than the site-wide latest.
  function classifyLink(href, baseHref, repoPath) {
    const url = parseUrl(href, baseHref);
    if (!url || !/^https?:$/.test(url.protocol)) return null;

    if (url.hostname === STORE_HOST) {
      return { name: 'store_click', params: { store: 'microsoft' } };
    }
    if (url.hostname !== GITHUB_HOST) return null;

    const path = url.pathname.replace(/\/+$/, '');
    const prefix = `/${repoPath}`;
    if (path.toLowerCase() !== prefix.toLowerCase() && !path.toLowerCase().startsWith(`${prefix.toLowerCase()}/`)) {
      return null;
    }

    const rest = path.slice(prefix.length).split('/').filter(Boolean);
    if (rest[0] === 'releases' && rest[1] === 'download' && rest.length === 4) {
      const asset = decodeSafe(rest[3]).toLowerCase();
      const match = ASSET_PLATFORMS.find((entry) => asset.endsWith(entry.suffix));
      if (match) {
        const tag = decodeSafe(rest[2]);
        return {
          name: 'download_click',
          params: { platform: match.platform, release_version: VERSION_PATTERN.test(tag) ? tag : UNKNOWN }
        };
      }
      return { name: 'github_click', params: { destination_type: 'release_asset' } };
    }

    const section = rest[0] || '';
    const destination = Object.prototype.hasOwnProperty.call(GITHUB_DESTINATIONS, section)
      ? GITHUB_DESTINATIONS[section]
      : OTHER;
    return { name: 'github_click', params: { destination_type: destination } };
  }

  function decodeSafe(value) {
    try {
      return decodeURIComponent(value);
    } catch (error) {
      return value;
    }
  }

  function readConsent(text, now) {
    if (!text) return null;
    try {
      const value = JSON.parse(text);
      if (!value || (value.choice !== 'granted' && value.choice !== 'denied') || typeof value.at !== 'number') return null;
      if (now - value.at < 0 || now - value.at >= CONSENT_TTL_MS) return null;
      return value.choice;
    } catch (error) {
      return null;
    }
  }

  function attributionParams(attribution) {
    return {
      traffic_source: attribution.source,
      traffic_medium: attribution.medium,
      traffic_campaign: attribution.campaign || NONE,
      traffic_content: attribution.content || NONE,
      target_market: attribution.market
    };
  }

  function safeStorage(win, name) {
    try {
      const storage = win[name];
      const probe = '__snaptray_probe__';
      storage.setItem(probe, probe);
      storage.removeItem(probe);
      return storage;
    } catch (error) {
      return null;
    }
  }

  function storageGet(storage, key) {
    try {
      return storage ? storage.getItem(key) : null;
    } catch (error) {
      return null;
    }
  }

  function storageSet(storage, key, value) {
    try {
      if (storage) storage.setItem(key, value);
    } catch (error) {
      // Storage full or blocked: analytics degrades, the site keeps working.
    }
  }

  function storageRemove(storage, key) {
    try {
      if (storage) storage.removeItem(key);
    } catch (error) {
      // Ignore.
    }
  }

  function deleteGaCookies(doc, hostname) {
    const names = doc.cookie.split(';')
      .map((part) => part.split('=')[0].trim())
      .filter((name) => name === '_ga' || name.startsWith('_ga_'));
    const domains = ['', hostname, `.${hostname.split('.').slice(-2).join('.')}`];
    names.forEach((name) => {
      domains.forEach((domain) => {
        doc.cookie = `${name}=; Max-Age=0; path=/${domain ? `; domain=${domain}` : ''}`;
      });
    });
  }

  function boot(win) {
    const doc = win.document;
    if (!doc) return;

    const start = () => {
      try {
        run(win, doc);
      } catch (error) {
        // Analytics must never break the page.
      }
    };
    if (doc.readyState === 'loading') {
      doc.addEventListener('DOMContentLoaded', start);
    } else {
      start();
    }
  }

  function run(win, doc) {
    const configNode = doc.getElementById('snaptray-analytics-config');
    if (!configNode) return;

    let config;
    try {
      config = JSON.parse(configNode.textContent);
    } catch (error) {
      return;
    }
    if (!config || !MEASUREMENT_ID_PATTERN.test(config.measurementId || '')) return;
    if (win.location.hostname !== config.productionHost) return;
    // Global Privacy Control / Do Not Track always win: no prompt, no settings link, no loading.
    const nav = win.navigator || {};
    if (nav.globalPrivacyControl === true || nav.doNotTrack === '1') return;

    const rules = {
      campaigns: (config.rules && config.rules.campaigns) || {},
      mediums: (config.rules && config.rules.mediums) || []
    };
    const local = safeStorage(win, 'localStorage');
    const session = safeStorage(win, 'sessionStorage');
    const banner = doc.getElementById('analytics-consent');
    const settingsButtons = Array.from(doc.querySelectorAll('[data-analytics-settings]'));
    const pageLanguage = (doc.body && doc.body.dataset.lang) || doc.documentElement.lang || UNKNOWN;
    const search = win.location.search;

    let started = false;
    let granted = false;
    let attribution = null;

    function gtag() {
      win.dataLayer.push(arguments);
    }

    function setBannerVisible(visible) {
      if (banner) banner.hidden = !visible;
    }

    function saveChoice(choice) {
      storageSet(local, CONSENT_KEY, JSON.stringify({ choice, at: Date.now() }));
    }

    function eventParams(extra) {
      return { page_language: pageLanguage, ...attributionParams(attribution), ...extra };
    }

    function onLinkActivate(event) {
      if (!granted) return;
      if (event.type === 'auxclick' && event.button !== 1) return;
      const link = event.target && event.target.closest ? event.target.closest('a[href]') : null;
      if (!link) return;
      const result = classifyLink(link.getAttribute('href'), win.location.href, config.repo);
      if (!result) return;
      const placementNode = link.closest('[data-analytics-placement]');
      const placement = placementNode ? placementNode.getAttribute('data-analytics-placement') : OTHER;
      try {
        gtag('event', result.name, eventParams({ ...result.params, placement, transport_type: 'beacon' }));
      } catch (error) {
        // Never block the download.
      }
    }

    function startTracking() {
      win[`ga-disable-${config.measurementId}`] = false;
      granted = true;
      if (started) {
        gtag('consent', 'update', { analytics_storage: 'granted' });
        return;
      }
      started = true;

      const isTest = new URLSearchParams(search).get(TEST_TRAFFIC_PARAM) === '1' || storageGet(session, TEST_TRAFFIC_KEY) === '1';
      if (isTest) storageSet(session, TEST_TRAFFIC_KEY, '1');

      attribution = resolveAttribution({
        now: Date.now(),
        search,
        referrer: doc.referrer,
        ownHost: config.productionHost,
        stored: parseStoredAttribution(storageGet(session, ATTRIBUTION_KEY)),
        rules
      });
      storageSet(session, ATTRIBUTION_KEY, JSON.stringify(attribution));

      win.dataLayer = win.dataLayer || [];
      gtag('consent', 'default', {
        analytics_storage: 'granted',
        ad_storage: 'denied',
        ad_user_data: 'denied',
        ad_personalization: 'denied'
      });
      gtag('js', new Date());
      const pageConfig = {
        send_page_view: false,
        page_location: sanitizePageLocation(win.location.href, search, rules),
        page_referrer: sanitizeReferrer(doc.referrer, config.productionHost),
        allow_google_signals: false,
        allow_ad_personalization_signals: false
      };
      if (isTest) {
        pageConfig.traffic_type = 'internal';
        pageConfig.debug_mode = true;
      }
      gtag('config', config.measurementId, pageConfig);
      gtag('event', 'page_view', eventParams({}));

      const script = doc.createElement('script');
      script.async = true;
      script.src = `https://www.googletagmanager.com/gtag/js?id=${encodeURIComponent(config.measurementId)}`;
      doc.head.appendChild(script);

      doc.addEventListener('click', onLinkActivate, true);
      doc.addEventListener('auxclick', onLinkActivate, true);
    }

    function stopTracking() {
      granted = false;
      // Consent mode alone still permits cookieless measurements from the loaded tag.
      win[`ga-disable-${config.measurementId}`] = true;
      if (started) gtag('consent', 'update', { analytics_storage: 'denied' });
      storageRemove(session, ATTRIBUTION_KEY);
      storageRemove(session, TEST_TRAFFIC_KEY);
      deleteGaCookies(doc, win.location.hostname);
    }

    if (banner) {
      const accept = banner.querySelector('[data-analytics-accept]');
      const decline = banner.querySelector('[data-analytics-decline]');
      if (accept) {
        accept.addEventListener('click', () => {
          saveChoice('granted');
          setBannerVisible(false);
          startTracking();
        });
      }
      if (decline) {
        decline.addEventListener('click', () => {
          saveChoice('denied');
          setBannerVisible(false);
          stopTracking();
        });
      }
    }
    settingsButtons.forEach((button) => {
      button.hidden = false;
      button.addEventListener('click', () => setBannerVisible(true));
    });

    win.addEventListener('storage', (event) => {
      if (!local || event.storageArea !== local) return;
      if (event.key !== CONSENT_KEY && event.key !== null) return;
      const choice = readConsent(event.newValue, Date.now());
      if (choice !== 'granted') {
        // Do not write the shared choice back: this is another tab's update.
        stopTracking();
        setBannerVisible(choice === null);
      }
    });

    const choice = readConsent(storageGet(local, CONSENT_KEY), Date.now());
    if (choice === 'granted') {
      startTracking();
    } else if (choice === null) {
      setBannerVisible(true);
    }
  }

  return {
    ATTRIBUTION_TTL_MS,
    CONSENT_TTL_MS,
    campaignFromSearch,
    resolveAttribution,
    parseStoredAttribution,
    sanitizePageLocation,
    sanitizeReferrer,
    classifyLink,
    readConsent,
    attributionParams,
    boot
  };
}));
