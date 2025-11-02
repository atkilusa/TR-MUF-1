#!/usr/bin/env python3
import argparse
import math
from pathlib import Path
from typing import Iterable, List, Sequence, Tuple, Optional

# --------------------------------------------------------------------------------------
# Изображение и цвета
# --------------------------------------------------------------------------------------

WIDTH = 320
HEIGHT = 240

# Фон как раньше
BG_RGB = (246, 241, 234)

def css_hex_to_rgb(css: str) -> Tuple[int, int, int]:
    """Мини-парсер CSS hex: #RGB или #RRGGBB → (r,g,b)."""
    s = css.strip()
    if s.startswith("#"):
        s = s[1:]
    if len(s) == 3:
        r, g, b = (int(s[0]*2, 16), int(s[1]*2, 16), int(s[2]*2, 16))
    elif len(s) == 6:
        r, g, b = (int(s[0:2], 16), int(s[2:4], 16), int(s[4:6], 16))
    else:
        raise ValueError(f"Unsupported CSS hex color: {css!r}")
    return (r, g, b)

# Цвет текста по умолчанию — «голубой как в CSS»
FG_RGB = css_hex_to_rgb("#1e90ff")  # dodgerblue

DEFAULT_OUTPUT = Path("data/splash.bin")
SUPERSAMPLE = 12  # суперсэмплинг для антиалиасинга

Color = Tuple[int, int, int]
Point = Tuple[float, float]

# --------------------------------------------------------------------------------------
# Цветовые утилиты
# --------------------------------------------------------------------------------------

def rgb565(r: int, g: int, b: int) -> int:
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)

BG_565 = rgb565(*BG_RGB)

def blend(bg: Color, fg: Color, alpha: float) -> Color:
    clamped = min(1.0, max(0.0, alpha))
    return tuple(int(round(bg[i] + (fg[i] - bg[i]) * clamped)) for i in range(3))

# --------------------------------------------------------------------------------------
# Холст с суперсэмплингом
# --------------------------------------------------------------------------------------

class SuperSampleCanvas:
    __slots__ = ("width", "height", "scale", "hi_width", "hi_height", "mask")

    def __init__(self, width: int, height: int, scale: int) -> None:
        self.width = width
        self.height = height
        self.scale = scale
        self.hi_width = width * scale
        self.hi_height = height * scale
        self.mask: List[int] = [0] * (self.hi_width * self.hi_height)

    def clear(self) -> None:
        self.mask = [0] * (self.hi_width * self.hi_height)

    def _set_hi_px(self, x: int, y: int, value: int) -> None:
        if 0 <= x < self.hi_width and 0 <= y < self.hi_height:
            self.mask[y * self.hi_width + x] = 1 if value else 0

    def fill_polygon(self, points: Sequence[Point], value: int) -> None:
        """Заполняет один многоугольник (координаты в «низких» единицах; масштабируем сами)."""
        if not points:
            return

        s = self.scale
        pts = [(px * s, py * s) for (px, py) in points]
        n = len(pts)

        for y in range(self.hi_height):
            intersections: List[float] = []
            for i in range(n):
                x0, y0 = pts[i]
                x1, y1 = pts[(i + 1) % n]
                if y0 == y1:
                    continue  # горизонтальные рёбра пропускаем
                ymin, ymax = (y0, y1) if y0 < y1 else (y1, y0)
                # полууинтервал (min, max] — исключаем верхнюю вершину
                if not (y > ymin and y <= ymax):
                    continue
                t = (y - y0) / (y1 - y0)
                x_int = x0 + t * (x1 - x0)
                intersections.append(x_int)

            if not intersections:
                continue

            intersections.sort()
            for i in range(0, len(intersections), 2):
                if i + 1 >= len(intersections):
                    break
                x_left = intersections[i]
                x_right = intersections[i + 1]
                x_start = int(math.ceil(x_left))
                x_end = int(math.floor(x_right))
                for x in range(x_start, x_end + 1):
                    self._set_hi_px(x, y, value)

    def downsample(self) -> List[int]:
        result: List[int] = []
        block_area = self.scale * self.scale
        for y in range(self.height):
            for x in range(self.width):
                acc = 0
                base = (y * self.scale) * self.hi_width + x * self.scale
                for sy in range(self.scale):
                    row = base + sy * self.hi_width
                    for sx in range(self.scale):
                        acc += self.mask[row + sx]
                alpha = acc / block_area
                r, g, b = blend(BG_RGB, FG_RGB, alpha)
                result.append(rgb565(r, g, b))
        return result

# --------------------------------------------------------------------------------------
# Заливка множества контуров по правилу non-zero winding (устраняет «белые точки»)
# --------------------------------------------------------------------------------------

def _fill_multipolygon_winding(canvas: SuperSampleCanvas, polys: Sequence[Sequence[Point]], value: int = 1) -> None:
    """polys — список контуров. Заливаем все вместе по правилу non-zero winding."""
    if not polys:
        return

    s = canvas.scale
    polys_hi: List[List[Tuple[float, float]]] = [
        [(px * s, py * s) for (px, py) in poly] for poly in polys if poly
    ]
    if not polys_hi:
        return

    EPS = 1e-7

    for y in range(canvas.hi_height):
        crossings: List[Tuple[float, int]] = []

        for pts in polys_hi:
            n = len(pts)
            for i in range(n):
                x0, y0 = pts[i]
                x1, y1 = pts[(i + 1) % n]

                if abs(y1 - y0) < EPS:
                    continue

                upward = y1 > y0
                ymin, ymax = (y0, y1) if y0 < y1 else (y1, y0)
                if not (y > ymin + EPS and y <= ymax + EPS):
                    continue

                t = (y - y0) / (y1 - y0)
                x = x0 + t * (x1 - x0)
                crossings.append((x, 1 if upward else -1))

        if not crossings:
            continue

        crossings.sort(key=lambda c: c[0])

        winding = 0
        fill_start_x: Optional[float] = None

        for x, dw in crossings:
            prev_winding = winding
            winding += dw
            now_filled = (prev_winding != 0)
            next_filled = (winding != 0)

            if (not now_filled) and next_filled:
                fill_start_x = x
            elif now_filled and (not next_filled) and fill_start_x is not None:
                x_left = fill_start_x
                x_right = x
                x_start = int(math.floor(min(x_left, x_right) + 1.0 - EPS))
                x_end = int(math.floor(max(x_left, x_right) + EPS))
                if x_end < 0 or x_start >= canvas.hi_width:
                    fill_start_x = None
                    continue
                x_start = max(x_start, 0)
                x_end = min(x_end, canvas.hi_width - 1)
                idx = y * canvas.hi_width + x_start
                for _x in range(x_start, x_end + 1):
                    canvas.mask[idx] = 1 if value else 0
                    idx += 1
                fill_start_x = None

# --------------------------------------------------------------------------------------
# Текст → контуры (через matplotlib)
# --------------------------------------------------------------------------------------

def _text_to_polygons(
    text: str,
    size: float,
    font_family: Optional[str] = None,
    font_path: Optional[str] = None,
    style: str = "italic",     # "normal" | "italic"
    weight: str = "normal",    # "normal" | "bold"
    italic_shear: float = 0.0  # если нет italic — можно докинуть наклон в градусах
) -> List[List[Point]]:
    """Генерирует список контуров текста (каждый контур — список (x, y)).
    Ось Y инвертируем (в нашем растре вниз, а у TextPath — вверх).
    Если italic_shear != 0 → наклоняем вручную.
    """
    from matplotlib.textpath import TextPath
    from matplotlib.font_manager import FontProperties
    import math

    if font_path:
        fp = FontProperties(fname=font_path)
    else:
        fp = FontProperties(family=font_family or "DejaVu Sans")

    if style:
        fp.set_style(style)
    if weight:
        fp.set_weight(weight)

    path = TextPath((0, 0), text, size=size, prop=fp, usetex=False)

    polys: List[List[Point]] = []
    shear = math.tan(math.radians(italic_shear)) if italic_shear else 0.0

    for poly in path.to_polygons():
        pts = []
        for (x, y) in poly:
            # инверсия Y
            y = -float(y)
            x = float(x)
            # наклон по X относительно Y, если shear задан
            if shear:
                x = x + y * shear
            pts.append((x, y))
        if len(pts) >= 3:
            polys.append(pts)

    return polys


def _bounds(polys: Sequence[Sequence[Point]]) -> Tuple[float, float, float, float]:
    minx = min(pt[0] for poly in polys for pt in poly)
    maxx = max(pt[0] for poly in polys for pt in poly)
    miny = min(pt[1] for poly in polys for pt in poly)
    maxy = max(pt[1] for poly in polys for pt in poly)
    return minx, miny, maxx, maxy

def _offset_polys(polys: Sequence[Sequence[Point]], dx: float, dy: float) -> List[List[Point]]:
    return [[(x + dx, y + dy) for (x, y) in poly] for poly in polys]

# --------------------------------------------------------------------------------------
# Рендер логотипа: текст + подчёркивание
# --------------------------------------------------------------------------------------

def render_logo(
    height: float = 44.0,
    baseline_offset: float = -4.0,
    text: str = "AK Tex.",
    font_family: Optional[str] = "DejaVu Sans",
    font_path: Optional[str] = None,
    style: str = "italic",
    weight: str = "normal",
    italic_shear: float = 0.0,
    underline_gap: float = 0.06,        # зазор от baseline до ВЕРХНЕЙ кромки подчёркивания (в долях height)
    underline_thickness: float = 0.10,  # толщина линии (в долях height)
    underline_width: float = 0.94       # ширина линии относительно ширины текста
) -> Tuple[List[int], float]:
    """Рисует текст из шрифта с подчёркиванием. Возвращает (pixels565, text_width_float)."""
    canvas = SuperSampleCanvas(WIDTH, HEIGHT, SUPERSAMPLE)
    canvas.clear()

    polys = _text_to_polygons(
        text=text,
        size=height,
        font_family=font_family,
        font_path=font_path,
        style=style,
        weight=weight,
        italic_shear=italic_shear,
    )
    if not polys:
        return canvas.downsample(), 0.0

    minx, miny, maxx, maxy = _bounds(polys)
    text_w = maxx - minx
    text_h = maxy - miny

    # Центрируем bbox текста и смещаем baseline
    origin_x = (WIDTH - text_w) / 2.0 - minx
    origin_y = (HEIGHT - text_h) / 2.0 + baseline_offset - miny
    polys_pos = _offset_polys(polys, origin_x, origin_y)

    # Основной залив текстовых контуров
    _fill_multipolygon_winding(canvas, polys_pos, 1)

    # Подчёркивание: считаем от БАЗОВОЙ линии (baseline = origin_y)
    baseline_y = origin_y
    line_width = text_w * underline_width
    line_height = max(1.0, height * underline_thickness)
    line_y = baseline_y + height * underline_gap
    line_x = (WIDTH - line_width) / 2.0

    underline = [
        (line_x, line_y),
        (line_x + line_width, line_y),
        (line_x + line_width, line_y + line_height),
        (line_x, line_y + line_height),
    ]
    canvas.fill_polygon(underline, 1)

    return canvas.downsample(), text_w

# --------------------------------------------------------------------------------------
# Выгрузка и вспомогательные функции
# --------------------------------------------------------------------------------------

def write_binary(output_path: Path, pixels: Iterable[int]) -> Path:
    output_path = Path(output_path)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    with output_path.open("wb") as f:
        f.write(int(WIDTH).to_bytes(2, "little"))
        f.write(int(HEIGHT).to_bytes(2, "little"))
        for value in pixels:
            f.write(int(value).to_bytes(2, "little"))
    return output_path

def crop_vertical(
    pixels: Sequence[int], width: int, height: int, bg_value: int = BG_565, margin: int = 4
) -> Tuple[List[int], int]:
    top = 0
    bottom = height - 1
    while top < height and all(pixels[top * width + x] == bg_value for x in range(width)):
        top += 1
    while bottom >= 0 and all(pixels[bottom * width + x] == bg_value for x in range(width)):
        bottom -= 1
    if top > bottom:
        return list(pixels), height
    top = max(0, top - margin)
    bottom = min(height - 1, bottom + margin)
    cropped: List[int] = []
    for row in range(top, bottom + 1):
        start = row * width
        cropped.extend(pixels[start : start + width])
    return cropped, bottom - top + 1

def encode_rle(pixels: Sequence[int]) -> List[Tuple[int, int]]:
    runs: List[Tuple[int, int]] = []
    if not pixels:
        return runs
    current = pixels[0]
    count = 1
    for value in pixels[1:]:
        if value == current and count < 0xFFFF:
            count += 1
            continue
        while count > 0xFFFF:
            runs.append((0xFFFF, current))
            count -= 0xFFFF
        runs.append((count, current))
        current = value
        count = 1
    while count > 0xFFFF:
        runs.append((0xFFFF, current))
        count -= 0xFFFF
    runs.append((count, current))
    return runs

def write_rle_inc(path: Path, width: int, height: int, runs: Sequence[Tuple[int, int]]) -> Path:
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    lines: List[str] = [
        "// Auto-generated by tools/make_splash_bin.py RLE export",
        "// Width and height for builtin splash logo image",
        "enum {",
        f"    kBuiltinLogoWidth = {width},",
        f"    kBuiltinLogoHeight = {height},",
        "};",
        "static const LogoRun kBuiltinLogoRle[] = {",
    ]
    lines.extend(f"    {{ {count}, 0x{value:04X} }}," for count, value in runs)
    lines.append("};")
    lines.append("static const size_t kBuiltinLogoRleCount = sizeof(kBuiltinLogoRle) / sizeof(kBuiltinLogoRle[0]);")
    lines.append("")
    path.write_text("\n".join(lines), encoding="utf-8")
    return path

# --------------------------------------------------------------------------------------
# CLI
# --------------------------------------------------------------------------------------

def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Generate the AK Tex splash image for LittleFS")
    parser.add_argument("-o", "--output", type=Path, default=DEFAULT_OUTPUT,
                        help="Output path for splash.bin (default: %(default)s)")
    parser.add_argument("--rle-inc", type=Path, default=None,
                        help="Optional output path for the builtin RLE include file")

    parser.add_argument("--text", type=str, default="AK Tex.",
                        help='Text to render (default: "%(default)s")')
    parser.add_argument("--height", type=float, default=44.0,
                        help="Font size / nominal height in px (default: %(default)s)")
    parser.add_argument("--baseline-offset", type=float, default=-4.0,
                        help="Baseline vertical offset (default: %(default)s)")

    parser.add_argument("--font-family", type=str, default="DejaVu Sans",
                        help="Font family name (default: %(default)s)")
    parser.add_argument("--font-path", type=str, default=None,
                        help="Path to specific TTF/OTF font (overrides --font-family)")
    parser.add_argument("--style", type=str, default="italic", choices=["normal", "italic"],
                        help="Font style (default: %(default)s)")
    parser.add_argument("--weight", type=str, default="normal", choices=["normal", "bold"],
                        help="Font weight (default: %(default)s)")
    parser.add_argument("--italic-shear", type=float, default=0.0,
                        help="Additional italic shear in degrees (default: %(default)s)")

    parser.add_argument("--underline-gap", type=float, default=0.06,
                        help="Gap from baseline to underline (font-height units)")
    parser.add_argument("--underline-thickness", type=float, default=0.10,
                        help="Underline thickness (font-height units)")
    parser.add_argument("--underline-width", type=float, default=0.94,
                        help="Underline width ratio to text width")

    parser.add_argument("--fg", type=str, default="#1e90ff",
                        help="CSS hex for foreground color, e.g. #1e90ff (default: %(default)s)")

    return parser.parse_args()

def main() -> None:
    args = parse_args()

    # Применяем заданный голубой цвет текста
    global FG_RGB
    FG_RGB = css_hex_to_rgb(args.fg)

    pixels, width_px = render_logo(
        height=args.height,
        baseline_offset=args.baseline_offset,
        text=args.text,
        font_family=args.font_family,
        font_path=args.font_path,
        style=args.style,
        weight=args.weight,
        italic_shear=args.italic_shear,
        underline_gap=args.underline_gap,
        underline_thickness=args.underline_thickness,
        underline_width=args.underline_width,
    )
    output_path = write_binary(args.output, pixels)
    payload_bytes = WIDTH * HEIGHT * 2
    total_bytes = payload_bytes + 4
    print(f"Splash image saved to {output_path} ({WIDTH}x{HEIGHT}, {total_bytes} bytes, logo width {width_px:.1f}px)")
    if args.rle_inc:
        cropped_pixels, cropped_height = crop_vertical(pixels, WIDTH, HEIGHT)
        runs = encode_rle(cropped_pixels)
        rle_path = write_rle_inc(args.rle_inc, WIDTH, cropped_height, runs)
        print(f"Builtin logo RLE exported to {rle_path} ({WIDTH}x{cropped_height}, {len(runs)} runs)")

if __name__ == "__main__":
    main()
