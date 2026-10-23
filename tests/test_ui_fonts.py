"""Check generated cmap coverage, not just font names or generator comments."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]


def coverage(source):
    result = set()
    maps = source.split("static const lv_font_fmt_txt_cmap_t cmaps[]", 1)[1]
    maps = maps.split("};", 1)[0]
    for block in re.findall(r"\{([^{}]+)\}", maps):
        start = int(re.search(r"range_start = (\d+)", block)[1])
        length = int(re.search(r"range_length = (\d+)", block)[1])
        name = re.search(r"unicode_list = (\w+)", block)[1]
        if name == "NULL":
            result.update(range(start, start + length))
        else:
            values = re.search(rf"{name}\[\] = \{{(.*?)\}};", source, re.S)[1]
            result.update(start + int(v, 0) for v in re.findall(r"0x[\da-fA-F]+|\d+", values))
    return result


class UiFontTests(unittest.TestCase):
    def test_every_size_has_same_unicode_coverage_and_icon_fallback(self):
        reference = None
        for size in (14, 18, 24, 28, 32, 48):
            with self.subTest(size=size):
                source = (ROOT / f"src/ui/fonts/UiFontDe{size}.c").read_text(encoding="utf-8")
                supported = coverage(source)
                self.assertTrue(set(range(0xA0, 0x100)) <= supported)
                self.assertTrue({ord(c) for c in "ÄÖÜäöüß€éèñç√×÷−±²³µ°·…–—‘’“”‹›«»"} <= supported)
                self.assertTrue({ord(c) for c in "😀🙂👍❤⚙🏠📻📡🔊🔇📅📝📞📧🔒💾🗑✅❌⚠▶⏸⏹"} <= supported)
                self.assertIn('.get_glyph_dsc = hp_font_get_glyph_dsc', source)
                self.assertIn(f".fallback = &lv_font_montserrat_{size}", source)
                if reference is None:
                    reference = supported
                self.assertEqual(supported, reference)

    def test_standard_widget_theme_uses_unicode_font_before_ui_creation(self):
        theme = (ROOT / "src/ui/UiTheme.cpp").read_text(encoding="utf-8")
        begin = theme.split("void UiTheme::begin()", 1)[1].split("void UiTheme::styleScreen", 1)[0]
        install=theme.split("void UiTheme::applyLvglTheme()",1)[1].split("void UiTheme::begin()",1)[0]
        self.assertIn("lv_theme_default_init", install)
        self.assertIn("&hp_font_de_14", install)
        self.assertIn("accent()", install)
        self.assertIn("surface()", install)
        self.assertIn("applyLvglTheme()", begin)
        shell = (ROOT / "src/ui/UiShell.cpp").read_text(encoding="utf-8").split("bool UiShell::begin()", 1)[1]
        self.assertLess(shell.index("DisplayUiGuard"), shell.index("UiTheme::instance().begin()"))
        self.assertLess(shell.index("UiTheme::instance().begin()"), shell.index("root_=lv_obj_create"))

    def test_emoji_modifiers_do_not_create_placeholder_boxes(self):
        theme = (ROOT / "src/ui/UiTheme.cpp").read_text(encoding="utf-8")
        callback=theme.split('extern "C" bool hp_font_get_glyph_dsc',1)[1].split('namespace hp',1)[0]
        for code in ("0xFE0E","0xFE0F","0x200D","0x1F3FB","0x1F3FF"):
            self.assertIn(code,callback)
        self.assertIn('*glyph={}',callback)
        self.assertIn('lv_font_get_glyph_dsc_fmt_txt',callback)
