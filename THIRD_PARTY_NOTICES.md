# Third-party data and artwork

- The skin catalog is derived from [ByMykel/CSGO-API](https://github.com/ByMykel/CSGO-API),
  commit `575aa6ff5bde0d23b32f385d1f612d55aa22df56`. Its MIT license is included
  in `licenses/CSGO-API.txt`. Snapshot checksums and image URLs are recorded in
  `assets/preview-manifest.json` in the source repository.
- Agent, music kit, sticker, charm and per-condition skin data use the same pinned
  CSGO-API snapshot. The outdated Train (Gold) sticker image URL is replaced by the
  corresponding `de_train_gold_1355_37` artwork URL from
  ByMykel/counter-strike-image-tracker commit `e2b28ef14cf41d5422b74875088ee8dc9dac5ef1`.
- Skin images are fetched from Valve's Steam economy CDN using the catalog's
  image URLs. Counter-Strike, item artwork and associated trademarks belong to
  Valve and the respective creators. The catalog's MIT license does not grant
  ownership of that artwork. This project is not affiliated with Valve.
- Dear ImGui v1.91.9b is distributed under MIT. Its license is included in the
  application archive as `IMGUI-LICENSE.txt`.

The preview selects a static inventory image for the chosen wear condition.
Some conditions share the same artwork. It is not an exact rendering of a
particular seed, float value within that condition, StatTrak counter, sticker
placement or in-game lighting. Agent, music kit, sticker and charm previews show
the individual item; sticker scraping and attachment placement are not rendered.

The lightweight package resizes the same artwork to a maximum edge of 384 pixels,
preserving transparency and aspect ratio. All catalog entries and available wear
condition images remain included. Enlarged images have less detail. Pillow is
used only by the build tooling and is not distributed with the Windows app.
