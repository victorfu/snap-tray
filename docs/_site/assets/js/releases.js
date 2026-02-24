const REPO_OWNER = 'victorfu';
const REPO_NAME = 'snap-tray';
const API_URL = `https://api.github.com/repos/${REPO_OWNER}/${REPO_NAME}/releases`;

const RELEASE_I18N = {
  en: {
    locale: 'en-US',
    noReleaseNotes: 'No release notes provided.',
    noReleaseAvailable: 'No release available yet',
    browseReleases: 'Browse downloads in GitHub releases',
    unableToLoad: 'Unable to load releases right now. Visit GitHub releases and try again later.',
    preRelease: 'Pre-release',
    stable: 'Stable',
    downloadMac: 'Download macOS (.dmg)',
    downloadWindows: 'Download Windows (.exe)',
    viewOnGithub: 'View on GitHub',
    untitledRelease: 'Untitled release'
  },
  'zh-tw': {
    locale: 'zh-TW',
    noReleaseNotes: '此版本目前未提供說明。',
    noReleaseAvailable: '目前尚無可用版本',
    browseReleases: '請前往 GitHub releases 查看可下載版本',
    unableToLoad: '目前無法載入版本資訊，請稍後再試或前往 GitHub releases。',
    preRelease: '預覽版',
    stable: '穩定版',
    downloadMac: '下載 macOS (.dmg)',
    downloadWindows: '下載 Windows (.exe)',
    viewOnGithub: '在 GitHub 查看',
    untitledRelease: '未命名版本'
  }
};

let releasesCachePromise = null;

function resolveLang() {
  const source = (document.body?.dataset.lang || document.documentElement.lang || 'en').toLowerCase();
  if (source.startsWith('zh')) {
    return 'zh-tw';
  }
  return 'en';
}

const ACTIVE_LANG = resolveLang();
const STRINGS = RELEASE_I18N[ACTIVE_LANG] || RELEASE_I18N.en;

function escapeHtml(input) {
  return String(input || '')
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
    .replace(/\"/g, '&quot;')
    .replace(/'/g, '&#39;');
}

function formatInline(text) {
  let safe = escapeHtml(text);
  safe = safe.replace(/`([^`]+)`/g, '<code>$1</code>');
  safe = safe.replace(/\*\*([^*]+)\*\*/g, '<strong>$1</strong>');
  safe = safe.replace(/\*([^*]+)\*/g, '<em>$1</em>');
  safe = safe.replace(/\[([^\]]+)\]\((https?:\/\/[^\s)]+)\)/g, '<a href="$2" target="_blank" rel="noopener">$1</a>');
  return safe;
}

function parseMarkdown(body) {
  if (!body || !body.trim()) {
    return `<p>${STRINGS.noReleaseNotes}</p>`;
  }

  const lines = body.split('\n');
  const html = [];
  let inList = false;

  function closeList() {
    if (inList) {
      html.push('</ul>');
      inList = false;
    }
  }

  lines.forEach((rawLine) => {
    const line = rawLine.trim();

    if (!line) {
      closeList();
      return;
    }

    if (line.startsWith('### ')) {
      closeList();
      html.push(`<h4>${formatInline(line.slice(4))}</h4>`);
      return;
    }

    if (line.startsWith('## ')) {
      closeList();
      html.push(`<h3>${formatInline(line.slice(3))}</h3>`);
      return;
    }

    if (line.startsWith('# ')) {
      closeList();
      html.push(`<h2>${formatInline(line.slice(2))}</h2>`);
      return;
    }

    if (line.startsWith('- ') || line.startsWith('* ')) {
      if (!inList) {
        html.push('<ul>');
        inList = true;
      }
      html.push(`<li>${formatInline(line.slice(2))}</li>`);
      return;
    }

    closeList();
    html.push(`<p>${formatInline(line)}</p>`);
  });

  closeList();
  return html.join('');
}

function formatDate(dateString) {
  const date = new Date(dateString);
  return date.toLocaleDateString(STRINGS.locale, {
    year: 'numeric',
    month: 'long',
    day: 'numeric'
  });
}

function getDownloadLinks(release) {
  const assets = release.assets || [];
  return {
    macos: assets.find((asset) => asset.name.endsWith('.dmg'))?.browser_download_url,
    windows: assets.find((asset) => asset.name.includes('Setup.exe') || asset.name.endsWith('.exe'))?.browser_download_url
  };
}

async function fetchReleases() {
  if (!releasesCachePromise) {
    releasesCachePromise = (async () => {
      try {
        const response = await fetch(API_URL, {
          headers: {
            Accept: 'application/vnd.github+json'
          }
        });

        if (!response.ok) {
          throw new Error(`GitHub API returned ${response.status}`);
        }

        return await response.json();
      } catch (error) {
        console.error('Failed to fetch releases:', error);
        return [];
      }
    })();
  }

  return releasesCachePromise;
}

function setDownloadVisibility(element, shouldShow) {
  if (!element) return;
  element.hidden = !shouldShow;
}

async function populateDownloadButtons() {
  const releases = await fetchReleases();
  const latest = releases.find((release) => !release.prerelease) || releases[0];

  const macButton = document.getElementById('download-macos');
  const macFallback = document.getElementById('no-macos-release');
  const windowsButton = document.getElementById('download-windows');
  const windowsFallback = document.getElementById('no-windows-release');

  if (!latest) {
    if (macFallback) macFallback.textContent = STRINGS.noReleaseAvailable;
    if (windowsFallback) windowsFallback.textContent = STRINGS.browseReleases;
    return;
  }

  const links = getDownloadLinks(latest);
  const version = latest.tag_name || '';

  if (macButton && links.macos) {
    macButton.href = links.macos;
    const versionNode = macButton.querySelector('.version');
    if (versionNode) versionNode.textContent = version;
    setDownloadVisibility(macButton, true);
    if (macFallback) macFallback.hidden = true;
  }

  if (windowsButton && links.windows) {
    windowsButton.href = links.windows;
    const versionNode = windowsButton.querySelector('.version');
    if (versionNode) versionNode.textContent = version;
    setDownloadVisibility(windowsButton, true);
    if (windowsFallback) windowsFallback.hidden = true;
  }
}

async function renderReleasesPage() {
  const container = document.getElementById('releases-list');
  if (!container) return;

  const releases = await fetchReleases();
  if (!releases.length) {
    container.innerHTML = `<p class="loading">${STRINGS.unableToLoad}</p>`;
    return;
  }

  container.innerHTML = releases.map((release) => {
    const links = getDownloadLinks(release);
    const date = formatDate(release.published_at);
    const title = escapeHtml(release.name || release.tag_name || STRINGS.untitledRelease);

    return `
      <article class="release-item">
        <header class="release-header">
          <h2>${title}</h2>
          <div class="release-meta">
            <span class="release-date">${date}</span>
            ${release.prerelease ? `<span class="badge prerelease">${STRINGS.preRelease}</span>` : `<span class="badge stable">${STRINGS.stable}</span>`}
          </div>
        </header>
        <div class="release-body">${parseMarkdown(release.body)}</div>
        <div class="release-downloads">
          ${links.macos ? `<a href="${links.macos}" class="btn btn-ghost" target="_blank" rel="noopener">${STRINGS.downloadMac}</a>` : ''}
          ${links.windows ? `<a href="${links.windows}" class="btn btn-ghost" target="_blank" rel="noopener">${STRINGS.downloadWindows}</a>` : ''}
          <a href="${release.html_url}" class="btn btn-link" target="_blank" rel="noopener">${STRINGS.viewOnGithub}</a>
        </div>
      </article>
    `;
  }).join('');
}

document.addEventListener('DOMContentLoaded', () => {
  populateDownloadButtons();
  renderReleasesPage();
});
