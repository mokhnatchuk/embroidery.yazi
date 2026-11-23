# embroidery.yazi

Fast, native embroidery file preview plugin for [Yazi](https://yazi-rs.github.io/) backed by a lightweight C23 PNG renderer (`embroidery-preview`).

Renders instant cached previews with accurate Janome, Brother, and Tajima thread palettes and zero heavy runtime dependencies.

## Supported Formats

- **Native fast C23 renderer (`embroidery-preview`):** `.jef`, `.jef+`, `.pes`, `.pec`, `.dst`

## Dependencies

- **Required:** C23 compiler (`gcc >= 14` or `clang >= 19`) and `zlib` (`-lz`)

## Installation

### 1. Install the Yazi plugin

Using `ya pkg`:

```bash
ya pkg add mokhnatchuk/embroidery
```

Or clone manually into Yazi's plugins directory:

```bash
git clone https://github.com/mokhnatchuk/embroidery.yazi.git ~/.config/yazi/plugins/embroidery.yazi
```

### 2. Build and install `embroidery-preview`

Make sure `~/.local/bin` (or your chosen `PREFIX/bin`) is in your `PATH`, then build and install the C23 preview binary:

```bash
git clone https://github.com/mokhnatchuk/embroidery.yazi.git
cd embroidery.yazi
make test
make install
```

Custom installation prefix:

```bash
make install PREFIX=/usr/local
```

### 3. Configure `~/.config/yazi/yazi.toml`

Add the previewer, preloader, and (optionally) opener rules to `~/.config/yazi/yazi.toml`:

```toml
[opener]
embroidery = [
  { run = "embroidery-studio %s1", desc = "embroidery-studio", orphan = true, for = "unix" },
]

[open]
prepend_rules = [
  { url = "*.{jef,JEF,jef+,JEF+,pes,PES,pec,PEC,dst,DST}", use = "embroidery" },
]

[plugin]
prepend_preloaders = [
  { url = "*.{jef,JEF,jef+,JEF+,pes,PES,pec,PEC,dst,DST}", run = "embroidery" },
]
prepend_previewers = [
  { url = "*.{jef,JEF,jef+,JEF+,pes,PES,pec,PEC,dst,DST,exp,EXP,vp3,VP3}", run = "embroidery" },
]
```

## CLI Usage (`embroidery-preview`)

```bash
embroidery-preview <input.{jef,jef+,pes,pec,dst}> <output.png> [max_dimension]
```

- `max_dimension`: optional integer between `64` and `4096` (default: `512`).

## License

MIT — see [LICENSE](LICENSE).
