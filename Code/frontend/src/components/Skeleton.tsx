interface PageSkeletonProps {
  cards?: number;
  rows?: number;
  actions?: boolean;
}

/**
 * @brief Generic loading skeleton for page-level config forms.
 */
export function PageSkeleton({ cards = 1, rows = 3, actions = true }: PageSkeletonProps) {
  return (
    <div aria-busy="true">
      <div class="skeleton sk-line sk-title" />
      {Array.from({ length: cards }, (_, card) => (
        <div key={card} class="card">
          <div class="skeleton sk-line" style={`width:${card % 2 ? 38 : 48}%`} />
          <div class="sk-rows">
            {Array.from({ length: rows }, (_, row) => (
              <div key={row} class="sk-row">
                <div class="skeleton sk-line" style={`width:${row % 2 ? 34 : 44}%`} />
                <div class="skeleton sk-line" style={`width:${row % 2 ? 42 : 32}%`} />
              </div>
            ))}
          </div>
        </div>
      ))}
      {actions && (
        <div class="actions">
          <div class="skeleton sk-button" />
          <div class="skeleton sk-button" />
        </div>
      )}
    </div>
  );
}

/**
 * @brief Loading skeleton for automation queue rows.
 */
export function QueueSkeleton() {
  return (
    <div class="sk-rows">
      {[0, 1].map((i) => (
        <div key={i} class="sk-row">
          <div>
            <div class="skeleton sk-line" style="width:150px" />
            <div class="skeleton sk-line" style="width:210px;margin-top:8px" />
          </div>
          <div class="skeleton sk-button" />
        </div>
      ))}
    </div>
  );
}
