# EchoDesk consumer website

The static consumer website starts at `docs/index.html` and is designed for
GitHub Pages' **Deploy from a branch → `/docs`** option. It has no build step,
backend, external JavaScript dependency or root-relative asset path.

## Local preview

From the repository root:

```powershell
py -m http.server 4173 --bind 127.0.0.1 --directory .
```

Then open `http://127.0.0.1:4173/docs/`.

## Source layout

- `index.html`: complete page content and semantic structure.
- `download.html`: Windows and macOS download, setup and release-verification page.
- `css/`: visual system, motion and responsive rules.
- `js/`: navigation/theme controls, product interaction, flip cards and
  progressive reveal animation.
- `assets/logo/`: the repository's existing EchoDesk logo and favicon.
- `assets/products/`: optimised WebP product visualisations.
- `.site-tests/`: local Playwright checks. GitHub Pages' Jekyll pipeline ignores
  the dot-prefixed folder.
- `downloads/`: release-publishing notes; large binaries live in GitHub
  Releases rather than Git history.

## Publishing EchoDesk Desktop

The download page targets these versioned GitHub Release assets:

- `releases/download/v0.2.6/EchoDesk-Desktop-v0.2.6-win-x64-setup.exe`
- `releases/download/v0.2.6/EchoDesk-Desktop-v0.2.6-win-x64.zip`
- `releases/download/v0.2.6/EchoDesk-Desktop-v0.2.6-macos-universal.dmg`
- `releases/download/v0.2.6/EchoDesk-Desktop-v0.2.6-macos-universal.zip`

Upload only the final Windows files produced by `desktop/build.ps1` and the
final universal macOS files produced by `companion/build.sh`. Update every
displayed file size and SHA-256 value in `download.html` from those exact
release files, then run the browser suite. Never rename an older release to
fill a new versioned path, and never claim an ad-hoc-signed Mac build is
notarized.

## Product visual provenance

No STEP, STL, 3MF, OBJ, GLB or glTF enclosure asset is present in the repository.
For that reason the hero is an explicitly labelled, interactive **concept
visual**, not a CAD-accurate 3D model. It uses a static image fallback by design
and requires no WebGL. Replace `assets/products/echodesk-mini-concept.webp` with
a verified enclosure render when final CAD or product photography is available.

The temporary hero asset was generated with OpenAI's built-in image generation
tool using the existing Freenove display photograph as the hardware scale and
screen-proportion reference. The prompt requested a compact matte-black desktop
touchscreen, neutral studio scene, blank screen, no text/logos/watermark, and
explicitly treated the result as a concept render. The image was converted to an
optimised 1536×1024 WebP for the site.

The blue Mini and desk images are existing repository product visualisations,
also labelled in the interface. `echodesk-os-preview.svg` and
`performance-centre-preview.svg` are the verified EchoDesk OS interface previews;
concept-only utility panels are labelled separately.

## Purchase behaviour

The repository contains no legitimate retail EchoDesk checkout. Mini purchase
actions therefore open a clearly labelled availability email to
`matthew@andomail.com`; they do not place an order. EchoDesk Professional remains
disabled and marked Coming Soon.

## Validation

Run the browser suite with Playwright 1.56.1 and installed Chrome:

```powershell
$qa = Join-Path $env:TEMP 'echodesk-site-qa'
npm install --prefix $qa --no-audit --no-fund '@playwright/test@1.56.1'
$env:NODE_PATH = Join-Path $qa 'node_modules'
& (Join-Path $qa 'node_modules/.bin/playwright.cmd') test --config docs/.site-tests/playwright.config.js
```

The suite checks 375×812, 768×1024 and 1440×1000 viewports, mobile navigation,
hash navigation, flip-card input, theme controls, the rotating headline,
keyboard product control, reduced motion, horizontal overflow, console errors,
failed requests and same-origin HTTP errors.
