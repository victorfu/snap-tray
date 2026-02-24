function resolveLang() {
  const source = (document.body?.dataset.lang || document.documentElement.lang || 'en').toLowerCase();
  if (source.startsWith('zh')) {
    return 'zh-tw';
  }
  return 'en';
}

function navLabels() {
  const lang = resolveLang();
  if (lang === 'zh-tw') {
    return {
      open: '開啟導覽選單',
      close: '關閉導覽選單'
    };
  }
  return {
    open: 'Open navigation',
    close: 'Close navigation'
  };
}

function toggleNavState(open) {
  const labels = navLabels();
  const header = document.querySelector('[data-header]');
  const nav = document.querySelector('[data-nav]');
  const toggle = document.querySelector('[data-nav-toggle]');
  const toggleLabel = toggle ? toggle.querySelector('[data-nav-toggle-label]') : null;

  if (!header || !nav || !toggle) {
    return;
  }

  const nextOpen = typeof open === 'boolean' ? open : !header.classList.contains('nav-open');
  header.classList.toggle('nav-open', nextOpen);
  nav.classList.toggle('is-open', nextOpen);
  toggle.setAttribute('aria-expanded', String(nextOpen));
  toggle.setAttribute('aria-label', nextOpen ? labels.close : labels.open);
  if (toggleLabel) {
    toggleLabel.textContent = nextOpen ? labels.close : labels.open;
  }
}

function initNav() {
  const header = document.querySelector('[data-header]');
  const nav = document.querySelector('[data-nav]');
  const toggle = document.querySelector('[data-nav-toggle]');
  if (!header || !nav || !toggle) {
    return;
  }

  toggleNavState(false);

  toggle.addEventListener('click', () => toggleNavState());

  nav.querySelectorAll('a').forEach((link) => {
    link.addEventListener('click', () => toggleNavState(false));
  });

  document.addEventListener('click', (event) => {
    if (!header.classList.contains('nav-open')) {
      return;
    }

    if (event.target instanceof Node && header.contains(event.target)) {
      return;
    }

    toggleNavState(false);
  });

  document.addEventListener('keydown', (event) => {
    if (event.key !== 'Escape' || !header.classList.contains('nav-open')) {
      return;
    }
    toggleNavState(false);
    toggle.focus();
  });

  window.addEventListener('resize', () => {
    if (window.innerWidth > 900) {
      toggleNavState(false);
    }
  });
}

function initReveal() {
  const items = document.querySelectorAll('[data-reveal]');
  if (!items.length || !('IntersectionObserver' in window)) {
    items.forEach((item) => item.classList.add('is-visible'));
    return;
  }

  const observer = new IntersectionObserver((entries, obs) => {
    entries.forEach((entry) => {
      if (!entry.isIntersecting) {
        return;
      }
      entry.target.classList.add('is-visible');
      obs.unobserve(entry.target);
    });
  }, {
    threshold: 0.15,
    rootMargin: '0px 0px -40px 0px'
  });

  items.forEach((item) => observer.observe(item));
}

function initYear() {
  const year = document.getElementById('current-year');
  if (year) {
    year.textContent = new Date().getFullYear();
  }
}

function slugifyHeading(text, index) {
  const base = text
    .toLowerCase()
    .trim()
    .replace(/[^\w\s-]/g, '')
    .replace(/\s+/g, '-')
    .replace(/-+/g, '-');

  if (!base) {
    return `section-${index + 1}`;
  }
  return base;
}

function initDocsSidebar() {
  const sidebar = document.querySelector('[data-docs-sidebar]');
  const toggle = document.querySelector('[data-docs-sidebar-toggle]');
  if (!sidebar || !toggle) {
    return;
  }

  const setSidebarOpen = (open) => {
    sidebar.classList.toggle('is-open', open);
    toggle.setAttribute('aria-expanded', String(open));
  };

  setSidebarOpen(false);

  toggle.addEventListener('click', () => {
    const next = !sidebar.classList.contains('is-open');
    setSidebarOpen(next);
  });

  sidebar.querySelectorAll('a').forEach((link) => {
    link.addEventListener('click', () => setSidebarOpen(false));
  });

  document.addEventListener('click', (event) => {
    if (!sidebar.classList.contains('is-open') || window.innerWidth > 900) {
      return;
    }

    if (event.target instanceof Node && (sidebar.contains(event.target) || toggle.contains(event.target))) {
      return;
    }

    setSidebarOpen(false);
  });

  document.addEventListener('keydown', (event) => {
    if (event.key === 'Escape') {
      setSidebarOpen(false);
    }
  });

  window.addEventListener('resize', () => {
    if (window.innerWidth > 900) {
      setSidebarOpen(false);
    }
  });
}

function initDocsToc() {
  const prose = document.querySelector('.docs-prose');
  const tocContainer = document.getElementById('docs-toc-nav');

  if (!prose || !tocContainer) {
    return;
  }

  const headings = Array.from(prose.querySelectorAll('h2, h3'));
  if (!headings.length) {
    return;
  }

  const usedIds = new Set();
  headings.forEach((heading, index) => {
    let id = heading.id || slugifyHeading(heading.textContent || '', index);
    while (usedIds.has(id)) {
      id = `${id}-${index + 1}`;
    }
    usedIds.add(id);
    heading.id = id;
  });

  const list = document.createElement('ul');
  list.className = 'docs-toc-list';

  const links = [];
  headings.forEach((heading) => {
    const item = document.createElement('li');
    const link = document.createElement('a');
    link.href = `#${heading.id}`;
    link.textContent = heading.textContent || heading.id;
    link.className = 'docs-toc-link';
    link.dataset.level = heading.tagName.toLowerCase();
    link.dataset.target = heading.id;
    item.appendChild(link);
    list.appendChild(item);
    links.push(link);
  });

  tocContainer.innerHTML = '';
  tocContainer.appendChild(list);

  const setActive = (id) => {
    links.forEach((link) => {
      link.classList.toggle('is-active', link.dataset.target === id);
    });
  };

  const visibleHeadings = new Map();
  if ('IntersectionObserver' in window) {
    const observer = new IntersectionObserver((entries) => {
      entries.forEach((entry) => {
        if (entry.isIntersecting) {
          visibleHeadings.set(entry.target.id, entry.boundingClientRect.top);
        } else {
          visibleHeadings.delete(entry.target.id);
        }
      });

      if (!visibleHeadings.size) {
        return;
      }

      const active = Array.from(visibleHeadings.entries())
        .sort((a, b) => Math.abs(a[1]) - Math.abs(b[1]))[0]?.[0];
      if (active) {
        setActive(active);
      }
    }, {
      rootMargin: '-24% 0px -62% 0px',
      threshold: [0, 1]
    });

    headings.forEach((heading) => observer.observe(heading));
  }

  if (window.location.hash) {
    setActive(window.location.hash.slice(1));
  } else if (headings[0]) {
    setActive(headings[0].id);
  }
}

document.addEventListener('DOMContentLoaded', () => {
  document.documentElement.classList.remove('no-js');
  initNav();
  initReveal();
  initYear();
  initDocsSidebar();
  initDocsToc();
});
