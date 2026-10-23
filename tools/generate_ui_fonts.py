"""Regenerate the static Unicode supplements; no runtime font allocation."""
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
SIZES = (14, 18, 24, 28, 32, 48)
SYMBOLS = "€√×÷−±²³µ°·…–—‘’“”‹›«»"
EMOJI_SYMBOLS = "😀😃😄😁😅😂🙂🙃😉😊😍🥰😎🤔😴😢😭😡👍👎👏🙏💪👋❤️💔⭐🌟✨🔥💧💡🎉🎵🎶🎧📻📡🔊🔉🔈🔇🎤🏠🏡⚙🔧🛠🔨🔍🔎🔒🔓🔑🛡✅❌❎⚠❗❓ℹ➕➖✖➗✔✕☑☒🔌🔋🪫☀☁⛅🌤🌥🌦🌧⛈🌩🌨❄☔🌡🌬💨🌈🌙🌍🌎🌏🕒⏰⏱⏲⌛⏳📅🗓📆📝📋📒📖📂📁📄💾🗑📧✉📨📩📬📞☎📱💻🖥⌨🖨📷🖼📸🎥🎬▶⏸⏹⏺⏮⏭⏪⏩🔁🔂🔀🔄🔃⬅➡⬆⬇↔↕↩↪🔙🔜🔝🚀🚗🚲🚶📍🗺🏢🛒💰💳📊📈📉📶🛜🔗🧮🎮🏆⚡☕🍽💤"


def main():
    npx = shutil.which("npx.cmd") or shutil.which("npx")
    if not npx:
        raise SystemExit("Node.js/npx is required to regenerate fonts")
    for size in SIZES:
        subprocess.run([
            npx, "--yes", "lv_font_conv@1.5.3", "--size", str(size),
            "--bpp", "4", "--format", "lvgl", "--font",
            "managed_components/lvgl__lvgl/scripts/built_in_font/Montserrat-Medium.ttf",
            "--range", "0xA0-0xFF", "--symbols", SYMBOLS,
            "--font", "tools/assets/NotoEmoji.ttf", "--symbols", EMOJI_SYMBOLS,
            "--no-compress", "--no-prefilter", "--force-fast-kern-format",
            "--lv-include", "lvgl.h", "--lv-font-name", f"hp_font_de_{size}",
            "--lv-fallback", f"lv_font_montserrat_{size}",
            "--output", f"src/ui/fonts/UiFontDe{size}.c",
        ], cwd=ROOT, check=True)
        output = ROOT / f"src/ui/fonts/UiFontDe{size}.c"
        text = output.read_text(encoding="utf-8")
        text = text.replace('.get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,',
                            '.get_glyph_dsc = hp_font_get_glyph_dsc,')
        text = text.replace('/*Initialize a public general font descriptor*/',
                            'extern bool hp_font_get_glyph_dsc(const lv_font_t *, lv_font_glyph_dsc_t *, uint32_t, uint32_t);\n'
                            '/*Initialize a public general font descriptor*/')
        output.write_text(text, encoding="utf-8")


if __name__ == "__main__":
    main()
