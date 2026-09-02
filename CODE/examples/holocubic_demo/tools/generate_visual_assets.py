#!/usr/bin/env python3

import argparse
import math
import random
import struct
from pathlib import Path


WIDTH = 160
HEIGHT = 112


class Canvas:
    def __init__(self):
        self.pixels = [[[0.0, 0.0, 0.0] for _ in range(WIDTH)]
                       for _ in range(HEIGHT)]

    def blend(self, x, y, color, alpha):
        if not (0 <= x < WIDTH and 0 <= y < HEIGHT):
            return
        alpha = max(0.0, min(1.0, alpha))
        pixel = self.pixels[y][x]
        for channel in range(3):
            pixel[channel] = (pixel[channel] * (1.0 - alpha) +
                              color[channel] * alpha)

    def soft_disc(self, center_x, center_y, radius, color, opacity=1.0,
                  softness=0.18):
        extent = int(radius * (1.0 + softness * 3.0)) + 2
        for y in range(max(0, int(center_y) - extent),
                       min(HEIGHT, int(center_y) + extent + 1)):
            for x in range(max(0, int(center_x) - extent),
                           min(WIDTH, int(center_x) + extent + 1)):
                distance = math.hypot(x - center_x, y - center_y)
                edge = radius * max(softness, 0.01)
                alpha = max(0.0, min(1.0, (radius + edge - distance) / edge))
                self.blend(x, y, color, alpha * opacity)

    def line(self, x0, y0, x1, y1, color, width=1, opacity=1.0):
        steps = max(abs(x1 - x0), abs(y1 - y0), 1)
        radius = max(0.75, width / 2.0)
        for step in range(steps + 1):
            ratio = step / steps
            x = x0 + ((x1 - x0) * ratio)
            y = y0 + ((y1 - y0) * ratio)
            self.soft_disc(x, y, radius, color, opacity, 0.2)

    def add_glow(self, center_x, center_y, radius, color, strength=1.0):
        limit = int(radius * 2.4)
        for y in range(max(0, int(center_y) - limit),
                       min(HEIGHT, int(center_y) + limit + 1)):
            for x in range(max(0, int(center_x) - limit),
                           min(WIDTH, int(center_x) + limit + 1)):
                distance = math.hypot(x - center_x, y - center_y)
                alpha = math.exp(-((distance / radius) ** 2) * 1.8)
                self.blend(x, y, color, alpha * strength * 0.32)

    def add_stars(self, count, seed, color=(130, 205, 255)):
        rng = random.Random(seed)
        for _ in range(count):
            x = rng.randrange(8, WIDTH - 8)
            y = rng.randrange(5, HEIGHT - 10)
            radius = rng.choice((0.7, 0.9, 1.2))
            self.soft_disc(x, y, radius, color, rng.uniform(0.35, 0.85), 0.1)

    def write_rgb565(self, path):
        data = bytearray()
        for row in self.pixels:
            for red, green, blue in row:
                red = max(0, min(255, int(red + 0.5)))
                green = max(0, min(255, int(green + 0.5)))
                blue = max(0, min(255, int(blue + 0.5)))
                if max(red, green, blue) < 3:
                    red = green = blue = 0
                value = ((red * 31 + 127) // 255) << 11
                value |= ((green * 63 + 127) // 255) << 5
                value |= (blue * 31 + 127) // 255
                data.extend(struct.pack("<H", value))
        path.write_bytes(data)

    def write_ppm(self, path):
        data = bytearray(f"P6\n{WIDTH} {HEIGHT}\n255\n", "ascii")
        for row in self.pixels:
            for pixel in row:
                for value in pixel:
                    value = max(0, min(255, int(value + 0.5)))
                    data.append(0 if value < 3 else value)
        path.write_bytes(data)


def draw_sun(canvas, x, y, radius, intensity=1.0):
    canvas.add_glow(x, y, radius * 1.35, (255, 155, 18), intensity)
    canvas.add_glow(x, y, radius * 0.9, (255, 225, 55), intensity)
    limit = int(radius) + 2
    for py in range(max(0, int(y) - limit), min(HEIGHT, int(y) + limit + 1)):
        for px in range(max(0, int(x) - limit), min(WIDTH, int(x) + limit + 1)):
            distance = math.hypot(px - x, py - y)
            if distance <= radius:
                edge = max(0.0, min(1.0, (radius - distance) / 2.0))
                vertical = max(0.0, min(1.0, (py - (y - radius)) / (radius * 2)))
                color = (255, 250 - int(55 * vertical), 72 - int(35 * vertical))
                canvas.blend(px, py, color, max(0.28, edge) * intensity)


def draw_cloud(canvas, center_x, center_y, scale=1.0, darkness=0.0,
               opacity=1.0):
    blobs = [
        (-49, 5, 21, 15, 0.76), (48, 6, 22, 15, 0.72),
        (-35, -3, 25, 20, 0.84), (30, -7, 27, 22, 0.82),
        (-17, -18, 27, 26, 0.93), (8, -26, 29, 31, 1.00),
        (13, -7, 33, 25, 0.88),
    ]

    base_x_radius = 61 * scale
    base_y_radius = 19 * scale
    for y in range(HEIGHT):
        for x in range(WIDTH):
            normalized_x = (x - center_x) / base_x_radius
            normalized_y = (y - (center_y + 10 * scale)) / base_y_radius
            distance_sq = normalized_x ** 2 + normalized_y ** 2
            if distance_sq >= 1.15:
                continue
            sphere = math.sqrt(max(0.0, 1.0 - min(1.0, distance_sq)))
            edge = max(0.0, min(1.0, (1.15 - distance_sq) / 0.18))
            shade = max(0.18, min(0.78, 0.30 + sphere * 0.44 - darkness))
            color = (105 + 125 * shade, 129 + 120 * shade,
                     158 + 96 * shade)
            canvas.blend(x, y, color, edge * opacity)

    for dx, dy, radius_x, radius_y, light in blobs:
        blob_x = center_x + dx * scale
        blob_y = center_y + dy * scale
        radius_x *= scale
        radius_y *= scale
        left = max(0, int(blob_x - radius_x * 1.08))
        right = min(WIDTH, int(blob_x + radius_x * 1.08) + 1)
        top = max(0, int(blob_y - radius_y * 1.08))
        bottom = min(HEIGHT, int(blob_y + radius_y * 1.08) + 1)
        for y in range(top, bottom):
            for x in range(left, right):
                normalized_x = (x - blob_x) / radius_x
                normalized_y = (y - blob_y) / radius_y
                distance_sq = normalized_x ** 2 + normalized_y ** 2
                if distance_sq >= 1.12:
                    continue
                sphere = math.sqrt(max(0.0, 1.0 - min(1.0, distance_sq)))
                edge = max(0.0, min(1.0, (1.12 - distance_sq) / 0.17))
                top_light = max(0.0, -normalized_y)
                side_light = max(0.0, -normalized_x)
                texture = (math.sin((x * 1.73) + (y * 0.91)) +
                           math.sin((x * 0.47) - (y * 1.37))) * 2.2
                shade = max(0.16, min(1.0,
                                      0.28 + sphere * 0.46 +
                                      top_light * 0.20 + side_light * 0.06 -
                                      darkness))
                color = (104 + 151 * shade * light + texture,
                         126 + 129 * shade * light + texture,
                         157 + 98 * shade * light + texture)
                canvas.blend(x, y, color, edge * opacity)


def draw_precipitation(canvas, kind):
    if kind == "rain":
        for x, y, length in ((42, 79, 22), (66, 82, 25), (92, 80, 24),
                             (118, 82, 21)):
            canvas.add_glow(x - 4, y + length / 2, 6, (15, 145, 255), 0.5)
            canvas.line(x, y, x - 8, y + length, (50, 185, 255), 2, 0.95)
    elif kind == "snow":
        for x, y, radius in ((43, 84, 2.4), (64, 97, 2.0), (84, 82, 2.2),
                             (105, 99, 2.5), (126, 84, 2.0)):
            canvas.add_glow(x, y, 5, (115, 215, 255), 0.38)
            canvas.soft_disc(x, y, radius, (225, 250, 255), 0.95, 0.14)
    elif kind == "storm":
        canvas.add_glow(82, 85, 17, (80, 90, 255), 0.75)
        canvas.line(87, 69, 75, 87, (247, 242, 158), 4, 1.0)
        canvas.line(75, 87, 84, 87, (247, 242, 158), 4, 1.0)
        canvas.line(84, 87, 68, 108, (247, 242, 158), 4, 1.0)


def build_weather(kind):
    canvas = Canvas()
    if kind == "clear":
        canvas.add_stars(7, 11, (255, 196, 65))
        for angle in range(0, 360, 45):
            radians = math.radians(angle)
            canvas.line(80 + int(math.cos(radians) * 34),
                        54 + int(math.sin(radians) * 34),
                        80 + int(math.cos(radians) * 42),
                        54 + int(math.sin(radians) * 42),
                        (255, 184, 40), 2, 0.8)
        draw_sun(canvas, 80, 54, 27)
    elif kind == "cloudy":
        draw_sun(canvas, 112, 35, 23)
        draw_cloud(canvas, 77, 66, 0.88, 0.0)
    elif kind == "overcast":
        canvas.add_glow(80, 60, 39, (55, 118, 165), 0.4)
        draw_cloud(canvas, 80, 60, 1.03, 0.18)
    elif kind == "fog":
        draw_cloud(canvas, 80, 50, 0.82, 0.13, 0.72)
        for y, width, opacity in ((69, 110, 0.46), (82, 137, 0.64),
                                  (95, 105, 0.45)):
            canvas.add_glow(80, y, width / 3.2, (133, 210, 231), opacity)
            canvas.line(80 - width // 2, y, 80 + width // 2, y,
                        (190, 230, 237), 3, opacity)
    elif kind == "rain":
        canvas.add_glow(80, 54, 41, (20, 90, 160), 0.5)
        draw_cloud(canvas, 80, 54, 0.94, 0.25)
        draw_precipitation(canvas, "rain")
    elif kind == "snow":
        canvas.add_glow(80, 55, 40, (70, 160, 220), 0.48)
        draw_cloud(canvas, 80, 53, 0.92, 0.09)
        draw_precipitation(canvas, "snow")
    elif kind == "storm":
        canvas.add_glow(80, 55, 43, (61, 48, 180), 0.62)
        draw_cloud(canvas, 80, 52, 0.97, 0.42)
        draw_precipitation(canvas, "storm")
    else:
        canvas.add_glow(80, 58, 35, (35, 82, 105), 0.28)
        draw_cloud(canvas, 80, 58, 0.90, 0.48, 0.72)
        canvas.line(52, 91, 108, 91, (65, 111, 127), 2, 0.55)
    return canvas


def build_clock_orb():
    canvas = Canvas()
    canvas.add_stars(26, 2308)
    center_x = 80
    center_y = 57
    radius = 34
    canvas.add_glow(center_x, center_y, 46, (26, 125, 255), 0.9)
    for y in range(center_y - radius - 2, center_y + radius + 3):
        for x in range(center_x - radius - 2, center_x + radius + 3):
            distance = math.hypot(x - center_x, y - center_y)
            if distance > radius:
                continue
            normalized_x = (x - center_x) / radius
            normalized_y = (y - center_y) / radius
            sphere_z = math.sqrt(max(0.0, 1.0 - normalized_x ** 2 -
                                     normalized_y ** 2))
            light = max(0.0, (normalized_x * -0.40) +
                        (normalized_y * -0.32) + (sphere_z * 0.86))
            rim = max(0.0, 1.0 - distance / radius)
            color = (55 + 145 * light,
                     105 + 135 * light,
                     160 + 95 * light)
            canvas.blend(x, y, color, min(1.0, rim * 4.0))
    for x, y, radius, opacity in ((69, 48, 5, 0.16), (91, 65, 7, 0.18),
                                  (76, 74, 3, 0.19), (92, 41, 3, 0.12)):
        canvas.soft_disc(x, y, radius, (21, 55, 104), opacity, 0.3)
    canvas.add_glow(80, 57, 35, (75, 188, 255), 0.22)
    return canvas


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--preview-dir", type=Path)
    args = parser.parse_args()
    args.output_dir.mkdir(parents=True, exist_ok=True)
    if args.preview_dir:
        args.preview_dir.mkdir(parents=True, exist_ok=True)

    for kind in ("clear", "cloudy", "overcast", "fog", "rain", "snow",
                 "storm", "offline"):
        canvas = build_weather(kind)
        canvas.write_rgb565(args.output_dir / f"weather_{kind}.rgb565")
        if args.preview_dir:
            canvas.write_ppm(args.preview_dir / f"weather_{kind}.ppm")

    clock = build_clock_orb()
    clock.write_rgb565(args.output_dir / "clock_orb.rgb565")
    if args.preview_dir:
        clock.write_ppm(args.preview_dir / "clock_orb.ppm")


if __name__ == "__main__":
    main()
