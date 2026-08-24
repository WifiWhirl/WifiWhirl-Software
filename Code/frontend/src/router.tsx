import { signal } from '@preact/signals';
import type { ComponentType } from 'preact';

// Minimal hash router: route = location.hash without the leading '#', default '/'.
function current(): string {
  const h = location.hash.replace(/^#/, '');
  return h || '/';
}

/**
 * @brief Reactive current hash route.
 */
export const route = signal(current());

// Swap the route; only the page content animates (CSS .route-fade), so the app
// shell (nav bars, header) stays live and is never snapshotted/redrawn.
window.addEventListener('hashchange', () => {
  route.value = current();
  window.scrollTo(0, 0);
});

/**
 * @brief Navigate to a hash route.
 * @param path Route path including the leading slash.
 */
export function navigate(path: string): void {
  location.hash = path;
}

/**
 * @brief Route table entry for the hash router.
 */
export interface RouteDef {
  path: string;
  component: ComponentType;
}

/**
 * @brief Find the component for a route path.
 * @param routes Ordered route table.
 * @param path Current hash path.
 * @returns Matching component or null.
 */
export function matchRoute(routes: RouteDef[], path: string): ComponentType | null {
  const hit = routes.find((r) => r.path === path);
  return hit ? hit.component : null;
}
