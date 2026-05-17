import type { Money } from "./api";

type MonthRow = {
  month: string;
  inflow: Money;
  outflow: Money;
  net: Money;
  savings_rate_pct: number;
};

// Dependency-free responsive SVG: grouped inflow/outflow bars per month
// (shared scale), with a savings-rate % label under each month. Native
// <title> tooltips give the exact figures.
export function TrendsChart({ months }: { months: MonthRow[] }) {
  const W = 480;
  const H = 170;
  const padX = 8;
  const padTop = 16;
  const padBottom = 34;
  const plotH = H - padTop - padBottom;

  const maxMinor = Math.max(
    1,
    ...months.map((m) =>
      Math.max(m.inflow.minor_units, m.outflow.minor_units),
    ),
  );
  const ccy = months[0]?.inflow.currency ?? "";
  const slot = (W - padX * 2) / months.length;
  const barW = Math.min(18, (slot - 8) / 2);
  const y = (minor: number) => padTop + plotH - (minor / maxMinor) * plotH;

  return (
    <svg
      className="chart"
      viewBox={`0 0 ${W} ${H}`}
      role="img"
      aria-label="Tendances mensuelles"
    >
      {/* baseline */}
      <line
        x1={padX}
        x2={W - padX}
        y1={padTop + plotH}
        y2={padTop + plotH}
        className="axis"
      />
      {/* max gridline + label */}
      <line
        x1={padX}
        x2={W - padX}
        y1={padTop}
        y2={padTop}
        className="grid"
      />
      <text x={padX} y={padTop - 5} className="tick">
        {(maxMinor / 100).toFixed(0)} {ccy}
      </text>

      {months.map((m, i) => {
        const cx = padX + slot * i + slot / 2;
        const inX = cx - barW - 1;
        const outX = cx + 1;
        return (
          <g key={m.month}>
            <rect
              className="bar-in"
              x={inX}
              y={y(m.inflow.minor_units)}
              width={barW}
              height={padTop + plotH - y(m.inflow.minor_units)}
            >
              <title>
                {m.month} · entrées {m.inflow.display}
              </title>
            </rect>
            <rect
              className="bar-out"
              x={outX}
              y={y(m.outflow.minor_units)}
              width={barW}
              height={padTop + plotH - y(m.outflow.minor_units)}
            >
              <title>
                {m.month} · sorties {m.outflow.display} · net{" "}
                {m.net.display}
              </title>
            </rect>
            <text x={cx} y={H - 19} className="xlab">
              {m.month.slice(2)}
            </text>
            <text x={cx} y={H - 6} className="rate">
              {m.savings_rate_pct.toFixed(0)}%
            </text>
          </g>
        );
      })}
    </svg>
  );
}
