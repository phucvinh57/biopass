#!/usr/bin/env bun
// Renders static star-history SVGs (light + dark) for phucvinh57/biopass by
// pulling stargazer timestamps directly from the GitHub REST API. This
// replaces the api.star-history.com embed, which now requires the viewer's
// own token to resolve stargazer timestamps for this repo (and whose shared
// token pool is frequently rate-limited anyway).

const OWNER = "phucvinh57";
const REPO = "biopass";
const TOKEN = process.env.STAR_HISTORY_TOKEN || process.env.GITHUB_TOKEN;

if (!TOKEN) {
  console.error(
    "Missing STAR_HISTORY_TOKEN/GITHUB_TOKEN env var (needed to read stargazer timestamps).",
  );
  process.exit(1);
}

async function fetchStarTimestamps() {
  const perPage = 100;
  const timestamps = [];
  for (let page = 1; ; page++) {
    const res = await fetch(
      `https://api.github.com/repos/${OWNER}/${REPO}/stargazers?per_page=${perPage}&page=${page}`,
      {
        headers: {
          Accept: "application/vnd.github.star+json",
          Authorization: `Bearer ${TOKEN}`,
          "X-GitHub-Api-Version": "2022-11-28",
        },
      },
    );
    if (!res.ok) {
      throw new Error(`GitHub API error ${res.status}: ${await res.text()}`);
    }
    const batch = await res.json();
    if (batch.length === 0) break;
    for (const entry of batch) timestamps.push(new Date(entry.starred_at));
    if (batch.length < perPage) break;
  }
  return timestamps.sort((a, b) => a - b);
}

const DAY_MS = 86_400_000;
const dayStart = (d) => new Date(Date.UTC(d.getUTCFullYear(), d.getUTCMonth(), d.getUTCDate()));

// Collapses raw star timestamps into one cumulative-count point per calendar
// day, anchored with a zero point the day before the first star and a final
// point at today. Bucketing by day (rather than plotting one point per star)
// keeps the path light and the curve smooth even for repos with star bursts.
function buildDailyPoints(timestamps) {
  const today = dayStart(new Date());
  if (timestamps.length === 0) {
    return [
      { date: new Date(today.getTime() - DAY_MS), count: 0 },
      { date: today, count: 0 },
    ];
  }

  const counts = new Map();
  for (const ts of timestamps) {
    const key = dayStart(ts).getTime();
    counts.set(key, (counts.get(key) || 0) + 1);
  }

  const days = [...counts.keys()].sort((a, b) => a - b);
  const points = [{ date: new Date(days[0] - DAY_MS), count: 0 }];
  let running = 0;
  for (const key of days) {
    running += counts.get(key);
    points.push({ date: new Date(key), count: running });
  }
  if (points[points.length - 1].date.getTime() < today.getTime()) {
    points.push({ date: today, count: running });
  }
  return points;
}

// "Nice" step sizes for both the y-axis gridlines and milestone markers.
function niceStep(roughStep) {
  const magnitude = 10 ** Math.floor(Math.log10(Math.max(1, roughStep)));
  const residual = roughStep / magnitude;
  const step = residual > 5 ? 10 : residual > 2 ? 5 : residual > 1 ? 2 : 1;
  return step * magnitude;
}

function computeMilestones(points, maxCount) {
  if (maxCount < 20) return [];
  const step = niceStep(maxCount / 4);
  const milestones = [];
  for (let threshold = step; threshold < maxCount * 0.92; threshold += step) {
    const hit = points.find((p) => p.count >= threshold);
    if (hit) milestones.push({ count: threshold, date: hit.date });
  }
  return milestones;
}

// Smooths a polyline by drawing quadratic Beziers between the midpoints of
// consecutive points, using each real data point as the control point. This
// stays visually close to the raw (monotonic) data with no spline overshoot.
function smoothPath(coords) {
  if (coords.length < 3) {
    return coords.map((c, i) => `${i === 0 ? "M" : "L"}${c.x.toFixed(2)},${c.y.toFixed(2)}`).join(" ");
  }
  let d = `M${coords[0].x.toFixed(2)},${coords[0].y.toFixed(2)}`;
  for (let i = 1; i < coords.length; i++) {
    const prev = coords[i - 1];
    const curr = coords[i];
    const midX = (prev.x + curr.x) / 2;
    const midY = (prev.y + curr.y) / 2;
    if (i === 1) {
      d += ` L${midX.toFixed(2)},${midY.toFixed(2)}`;
    } else {
      d += ` Q${prev.x.toFixed(2)},${prev.y.toFixed(2)} ${midX.toFixed(2)},${midY.toFixed(2)}`;
    }
  }
  const last = coords[coords.length - 1];
  d += ` L${last.x.toFixed(2)},${last.y.toFixed(2)}`;
  return d;
}

function renderSvg(points, theme) {
  const width = 900;
  const height = 420;
  const header = 78;
  const padding = { top: header + 20, right: 30, bottom: 40, left: 55 };
  const plotW = width - padding.left - padding.right;
  const plotH = height - padding.top - padding.bottom;

  const palette =
    theme === "dark"
      ? {
          bg: "#0d1117",
          card: "#161b22",
          grid: "#30363d",
          axis: "#8b949e",
          text: "#e6edf3",
          subtext: "#8b949e",
          gradFrom: "#a371f7",
          gradTo: "#58a6ff",
          milestone: "#e3b341",
        }
      : {
          bg: "#ffffff",
          card: "#f6f8fa",
          grid: "#e5e7eb",
          axis: "#57606a",
          text: "#1f2328",
          subtext: "#57606a",
          gradFrom: "#6639ba",
          gradTo: "#0969da",
          milestone: "#9a6700",
        };

  const totalStars = points[points.length - 1].count;
  const firstStarDate = points[0].date;
  const milestones = computeMilestones(points, totalStars);

  const minDate = points[0].date.getTime();
  const maxDate = points[points.length - 1].date.getTime();
  const dateSpan = Math.max(1, maxDate - minDate);
  const maxCount = Math.max(1, totalStars);

  const x = (d) => padding.left + ((d - minDate) / dateSpan) * plotW;
  const y = (c) => padding.top + plotH - (c / maxCount) * plotH;

  const coords = points.map((p) => ({ x: x(p.date.getTime()), y: y(p.count) }));
  const linePath = smoothPath(coords);
  const floorY = (padding.top + plotH).toFixed(2);
  const areaPath = `${linePath} L${coords[coords.length - 1].x.toFixed(2)},${floorY} L${coords[0].x.toFixed(2)},${floorY} Z`;

  const yTicks = 4;
  const gridLines = Array.from({ length: yTicks + 1 }, (_, i) => {
    const count = Math.round((maxCount / yTicks) * i);
    const yy = y(count).toFixed(2);
    return (
      `<line x1="${padding.left}" y1="${yy}" x2="${width - padding.right}" y2="${yy}" stroke="${palette.grid}" stroke-width="1" />` +
      `<text x="${padding.left - 10}" y="${yy}" text-anchor="end" dominant-baseline="middle" font-size="11" fill="${palette.axis}">${count}</text>`
    );
  }).join("");

  const fmtMonth = (d) => new Date(d).toLocaleDateString("en-US", { year: "numeric", month: "short" });
  const xTickCount = 5;
  const xLabels = Array.from({ length: xTickCount + 1 }, (_, i) => {
    const t = minDate + (dateSpan / xTickCount) * i;
    const xx = x(t).toFixed(2);
    return `<text x="${xx}" y="${height - padding.bottom + 20}" text-anchor="middle" font-size="11" fill="${palette.axis}">${fmtMonth(t)}</text>`;
  }).join("");

  const milestoneMarkers = milestones
    .map(({ count, date }) => {
      const mx = x(date.getTime());
      const my = y(count);
      return (
        `<line x1="${mx.toFixed(2)}" y1="${my.toFixed(2)}" x2="${mx.toFixed(2)}" y2="${floorY}" stroke="${palette.milestone}" stroke-width="1" stroke-dasharray="3,3" opacity="0.6" />` +
        `<circle cx="${mx.toFixed(2)}" cy="${my.toFixed(2)}" r="4" fill="${palette.milestone}" stroke="${palette.card}" stroke-width="1.5" />` +
        `<text x="${mx.toFixed(2)}" y="${(my - 10).toFixed(2)}" text-anchor="middle" font-size="10" font-weight="600" fill="${palette.milestone}">${count}</text>`
      );
    })
    .join("");

  const endX = coords[coords.length - 1].x;
  const endY = coords[coords.length - 1].y;
  const endLabelAnchor = endX > width - 140 ? "end" : "start";
  const endLabelX = endX > width - 140 ? endX - 12 : endX + 12;

  const fmtFull = (d) => d.toLocaleDateString("en-US", { year: "numeric", month: "long", day: "numeric" });

  return `<svg xmlns="http://www.w3.org/2000/svg" width="${width}" height="${height}" viewBox="0 0 ${width} ${height}" font-family="-apple-system, BlinkMacSystemFont, 'Segoe UI', Helvetica, Arial, sans-serif">
  <defs>
    <linearGradient id="line-${theme}" x1="0" y1="0" x2="1" y2="0">
      <stop offset="0%" stop-color="${palette.gradFrom}" />
      <stop offset="100%" stop-color="${palette.gradTo}" />
    </linearGradient>
    <linearGradient id="area-${theme}" x1="0" y1="0" x2="0" y2="1">
      <stop offset="0%" stop-color="${palette.gradTo}" stop-opacity="0.28" />
      <stop offset="100%" stop-color="${palette.gradTo}" stop-opacity="0" />
    </linearGradient>
  </defs>

  <rect width="${width}" height="${height}" rx="12" fill="${palette.bg}" />
  <rect width="${width}" height="${height}" rx="12" fill="none" stroke="${palette.grid}" stroke-width="1" />

  <text x="28" y="40" font-size="30" font-weight="700" fill="${palette.text}">${totalStars.toLocaleString()}</text>
  <text x="28" y="40" font-size="16" fill="${palette.subtext}" dx="${String(totalStars).length * 19 + 10}">★ stars</text>
  <text x="28" y="62" font-size="13" fill="${palette.subtext}">${OWNER}/${REPO} · growing since ${fmtFull(firstStarDate)}</text>
  <line x1="0" y1="${header}" x2="${width}" y2="${header}" stroke="${palette.grid}" stroke-width="1" />

  ${gridLines}
  ${xLabels}
  <path d="${areaPath}" fill="url(#area-${theme})" stroke="none" />
  <path d="${linePath}" fill="none" stroke="url(#line-${theme})" stroke-width="2.5" stroke-linecap="round" />
  ${milestoneMarkers}

  <circle cx="${endX.toFixed(2)}" cy="${endY.toFixed(2)}" r="7" fill="${palette.gradTo}" opacity="0.25" />
  <circle cx="${endX.toFixed(2)}" cy="${endY.toFixed(2)}" r="3.5" fill="${palette.gradTo}" stroke="${palette.bg}" stroke-width="1.5" />
  <text x="${endLabelX.toFixed(2)}" y="${(endY - 10).toFixed(2)}" text-anchor="${endLabelAnchor}" font-size="12" font-weight="600" fill="${palette.text}">${totalStars.toLocaleString()}</text>

  <line x1="${padding.left}" y1="${floorY}" x2="${width - padding.right}" y2="${floorY}" stroke="${palette.axis}" stroke-width="1" />
</svg>`;
}

const timestamps = await fetchStarTimestamps();
const points = buildDailyPoints(timestamps);

await Bun.write("assets/star-history-light.svg", renderSvg(points, "light"));
await Bun.write("assets/star-history-dark.svg", renderSvg(points, "dark"));

console.log(`Rendered star-history SVGs from ${timestamps.length} stargazers.`);
