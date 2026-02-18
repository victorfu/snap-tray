const NAV_LABEL_OPEN = 'Open navigation';
const NAV_LABEL_CLOSE = 'Close navigation';

function toggleNavState(open) {
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
  toggle.setAttribute('aria-label', nextOpen ? NAV_LABEL_CLOSE : NAV_LABEL_OPEN);
  if (toggleLabel) {
    toggleLabel.textContent = nextOpen ? NAV_LABEL_CLOSE : NAV_LABEL_OPEN;
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

document.addEventListener('DOMContentLoaded', () => {
  document.documentElement.classList.remove('no-js');
  initNav();
  initReveal();
  initYear();
});
