const REPO_OWNER = 'victorfu';
const REPO_NAME = 'snap-tray';
const API_URL = `https://api.github.com/repos/${REPO_OWNER}/${REPO_NAME}/releases`;

async function fetchReleases() {
  try {
    const response = await fetch(API_URL);
    if (!response.ok) {
      throw new Error(`HTTP error! status: ${response.status}`);
    }
    return await response.json();
  } catch (error) {
    console.error('Failed to fetch releases:', error);
    return [];
  }
}

function getDownloadLinks(release) {
  const assets = release.assets || [];
  return {
    macos: assets.find(a => a.name.endsWith('.dmg'))?.browser_download_url,
    windows: assets.find(a => a.name.includes('Setup.exe') || a.name.endsWith('.exe'))?.browser_download_url
  };
}

async function populateDownloadButtons() {
  const releases = await fetchReleases();
  if (releases.length === 0) return;

  const latest = releases.find(r => !r.prerelease) || releases[0];
  const links = getDownloadLinks(latest);
  const version = latest.tag_name;

  // Update macOS button
  const macBtn = document.getElementById('download-macos');
  const noMacRelease = document.getElementById('no-macos-release');
  if (macBtn && links.macos) {
    macBtn.href = links.macos;
    macBtn.querySelector('.version').textContent = version;
    macBtn.style.display = 'inline-flex';
    if (noMacRelease) noMacRelease.style.display = 'none';
  }

  // Update Windows button
  const winBtn = document.getElementById('download-windows');
  const noWinRelease = document.getElementById('no-windows-release');
  if (winBtn && links.windows) {
    winBtn.href = links.windows;
    winBtn.querySelector('.version').textContent = version;
    winBtn.style.display = 'inline-flex';
    if (noWinRelease) noWinRelease.style.display = 'none';
  }
}

function formatDate(dateString) {
  const date = new Date(dateString);
  return date.toLocaleDateString('en-US', {
    year: 'numeric',
    month: 'long',
    day: 'numeric'
  });
}

function parseMarkdown(text) {
  if (!text) return '';

  // Convert markdown to basic HTML
  return text
    // Headers
    .replace(/^### (.*$)/gim, '<h4>$1</h4>')
    .replace(/^## (.*$)/gim, '<h3>$1</h3>')
    .replace(/^# (.*$)/gim, '<h2>$1</h2>')
    // Bold
    .replace(/\*\*(.*?)\*\*/gim, '<strong>$1</strong>')
    // Italic
    .replace(/\*(.*?)\*/gim, '<em>$1</em>')
    // Links
    .replace(/\[(.*?)\]\((.*?)\)/gim, '<a href="$2" target="_blank" rel="noopener">$1</a>')
    // Line breaks
    .replace(/\n/gim, '<br>')
    // Lists
    .replace(/^\s*[-*]\s+(.*)$/gim, '<li>$1</li>')
    .replace(/(<li>.*<\/li>)/gim, '<ul>$1</ul>')
    // Clean up multiple ul tags
    .replace(/<\/ul><br><ul>/gim, '');
}

async function renderReleasesPage() {
  const container = document.getElementById('releases-list');
  if (!container) return;

  const releases = await fetchReleases();

  if (releases.length === 0) {
    container.innerHTML = '<p class="loading">No releases available yet. Check back soon!</p>';
    return;
  }

  container.innerHTML = releases.map(release => {
    const links = getDownloadLinks(release);
    const date = formatDate(release.published_at);

    return `
      <article class="release-item">
        <header>
          <h2>${release.name || release.tag_name}</h2>
          <span class="release-date">${date}</span>
          ${release.prerelease ? '<span class="badge prerelease">Pre-release</span>' : ''}
        </header>
        <div class="release-body">
          ${parseMarkdown(release.body)}
        </div>
        <div class="release-downloads">
          ${links.macos ? `<a href="${links.macos}" class="btn btn-secondary">Download macOS (.dmg)</a>` : ''}
          ${links.windows ? `<a href="${links.windows}" class="btn btn-secondary">Download Windows (.exe)</a>` : ''}
        </div>
      </article>
    `;
  }).join('');
}

// Initialize on page load
document.addEventListener('DOMContentLoaded', () => {
  populateDownloadButtons();
  renderReleasesPage();
});
