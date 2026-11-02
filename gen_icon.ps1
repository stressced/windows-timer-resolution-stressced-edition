# Builds restimer.ico from R.png in the same style as TinyFanControl's T and Silicon
# Tuner's S: the glyph alone on a transparent background, no border, the shared
# purple gradient (#9661ff -> #6632e8 -> #4423ad), a soft top highlight, a soft
# bottom shadow and the three dark speed lines kept from the source.
# The glyph is cut out of R.png by colour (purple fill + the speed lines inside it),
# so the gray halo, the navy outline and the background are dropped.
# Only uses System.Drawing + the C# compiler that ships with .NET Framework.
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$root    = Split-Path -Parent $MyInvocation.MyCommand.Path
$srcPath = Join-Path $root 'R.png'
$icoPath = Join-Path $root 'restimer.ico'
$pngPath = Join-Path $root 'restimer.png'
if (-not (Test-Path $srcPath)) { throw "Source image not found: $srcPath" }

Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @'
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;

public static class GlyphCut {
    static double Clamp(double v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }

    // Returns a square ARGB bitmap (size x size) with the recoloured glyph.
    public static Bitmap Build(string path, int size, double fill) {
        Bitmap src = new Bitmap(path);
        int w = src.Width, h = src.Height;
        Bitmap s32 = src.Clone(new Rectangle(0, 0, w, h), PixelFormat.Format32bppArgb);
        src.Dispose();
        BitmapData sd = s32.LockBits(new Rectangle(0, 0, w, h), ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
        byte[] px = new byte[sd.Stride * h];
        Marshal.Copy(sd.Scan0, px, 0, px.Length);
        int st = sd.Stride;
        s32.UnlockBits(sd); s32.Dispose();

        // Purple coverage: the fill has B-G ~130-170, the navy outline ~35, grays ~0.
        double[] a = new double[w * h];
        double[] lum = new double[w * h];
        for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
            int i = y * st + x * 4;
            double b = px[i], g = px[i + 1], r = px[i + 2];
            a[y * w + x] = Clamp(((b - g) - 45.0) / 70.0);
            lum[y * w + x] = 0.299 * r + 0.587 * g + 0.114 * b;
        }

        // Speed lines: dark pixels with purple fill both just above and just below,
        // only in the lower-left corner of the stem where R.png draws them (the
        // navy outline elsewhere, e.g. around the counter, must not be picked up).
        double[] d = new double[w * h];
        int reach = h / 40;
        int lx0 = (int)(w * 0.15), lx1 = (int)(w * 0.35), ly0 = (int)(h * 0.642), ly1 = (int)(h * 0.75);
        for (int y = ly0; y < ly1; y++) for (int x = lx0; x < lx1; x++) {
            int k = y * w + x;
            if (lum[k] > 140 || a[k] > 0.95) continue;
            bool up = false, down = false;
            for (int j = 1; j <= reach && !up; j++)   if (a[k - j * w] > 0.9) up = true;
            for (int j = 1; j <= reach && !down; j++) if (a[k + j * w] > 0.9) down = true;
            if (up && down) d[k] = Clamp((140 - lum[k]) / 90.0);
        }

        int x0 = w, y0 = h, x1 = -1, y1 = -1;
        for (int y = 0; y < h; y++) for (int x = 0; x < w; x++)
            if (a[y * w + x] > 0.5) {
                if (x < x0) x0 = x; if (x > x1) x1 = x;
                if (y < y0) y0 = y; if (y > y1) y1 = y;
            }
        double bw = x1 - x0 + 1, bh = y1 - y0 + 1;

        Color[] stops = { ColorTranslator.FromHtml("#9661ff"), ColorTranslator.FromHtml("#6632e8"), ColorTranslator.FromHtml("#4423ad") };
        double[] pos = { 0, 0.52, 1 };
        Color hi = ColorTranslator.FromHtml("#ad85ff"), sh = ColorTranslator.FromHtml("#231059"), ln = ColorTranslator.FromHtml("#18122e");

        int cw = (int)bw, ch = (int)bh;
        Bitmap crop = new Bitmap(cw, ch, PixelFormat.Format32bppArgb);
        BitmapData cd = crop.LockBits(new Rectangle(0, 0, cw, ch), ImageLockMode.WriteOnly, PixelFormat.Format32bppArgb);
        byte[] o = new byte[cd.Stride * ch];
        for (int y = 0; y < ch; y++) for (int x = 0; x < cw; x++) {
            int k = (y + y0) * w + (x + x0);
            double al = Math.Max(a[k], d[k]);
            if (al <= 0) continue;
            double u = x / bw, v = y / bh;
            // objectBoundingBox gradient x1=0 y1=0 x2=.8 y2=1, as in the SVGs.
            double t = Clamp((u * 0.8 + v) / 1.64);
            int si = t < pos[1] ? 0 : 1;
            double f = (t - pos[si]) / (pos[si + 1] - pos[si]);
            double R = stops[si].R + (stops[si + 1].R - stops[si].R) * f;
            double G = stops[si].G + (stops[si + 1].G - stops[si].G) * f;
            double B = stops[si].B + (stops[si + 1].B - stops[si].B) * f;
            if (v < 0.55 - 0.55 * u) { R += (hi.R - R) * 0.13; G += (hi.G - G) * 0.13; B += (hi.B - B) * 0.13; }
            if (v > 1.0 - 0.45 * u)  { R += (sh.R - R) * 0.18; G += (sh.G - G) * 0.18; B += (sh.B - B) * 0.18; }
            double m = al > 0 ? d[k] / al : 0;
            R += (ln.R - R) * m; G += (ln.G - G) * m; B += (ln.B - B) * m;
            int oi = y * cd.Stride + x * 4;
            o[oi] = (byte)Math.Round(B); o[oi + 1] = (byte)Math.Round(G); o[oi + 2] = (byte)Math.Round(R);
            o[oi + 3] = (byte)Math.Round(al * 255);
        }
        Marshal.Copy(o, 0, cd.Scan0, o.Length);
        crop.UnlockBits(cd);

        Bitmap outBmp = new Bitmap(size, size, PixelFormat.Format32bppArgb);
        using (Graphics gr = Graphics.FromImage(outBmp)) {
            gr.InterpolationMode = System.Drawing.Drawing2D.InterpolationMode.HighQualityBicubic;
            gr.PixelOffsetMode = System.Drawing.Drawing2D.PixelOffsetMode.HighQuality;
            gr.CompositingQuality = System.Drawing.Drawing2D.CompositingQuality.HighQuality;
            double scale = size * fill / Math.Max(bw, bh);
            float dw = (float)(bw * scale), dh = (float)(bh * scale);
            gr.DrawImage(crop, new RectangleF((size - dw) / 2f, (size - dh) / 2f, dw, dh));
        }
        crop.Dispose();
        return outBmp;
    }
}
'@

# Patreon and X marks for the "Stressced Software" links: the same Simple Icons
# paths (CC0 1.0, 24x24 viewBox) as Silicon Tuner's header, filled with a flat colour.
Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @"
using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.Globalization;

public static class SvgMark {
    // Minimal SVG path reader: M L H V C Z, absolute and relative, implicit repeats.
    static GraphicsPath Parse(string d) {
        var nums = new List<string>(); var cmds = new List<char>();
        var path = new GraphicsPath(FillMode.Winding);
        int i = 0; char cmd = 'M';
        float x = 0, y = 0, sx = 0, sy = 0;
        Func<float> num = () => {
            while (i < d.Length && (d[i] == ' ' || d[i] == ',' || d[i] == '\n' || d[i] == '\r')) i++;
            int s = i;
            if (i < d.Length && (d[i] == '-' || d[i] == '+')) i++;
            bool dot = false;
            while (i < d.Length && (char.IsDigit(d[i]) || (d[i] == '.' && !dot))) { if (d[i] == '.') dot = true; i++; }
            return float.Parse(d.Substring(s, i - s), CultureInfo.InvariantCulture);
        };
        while (true) {
            while (i < d.Length && (d[i] == ' ' || d[i] == ',')) i++;
            if (i >= d.Length) break;
            if (char.IsLetter(d[i])) cmd = d[i++];
            bool rel = char.IsLower(cmd);
            switch (char.ToUpper(cmd)) {
                case 'M': {
                    float nx = num(), ny = num(); if (rel) { nx += x; ny += y; }
                    path.StartFigure(); x = sx = nx; y = sy = ny;
                    cmd = rel ? 'l' : 'L'; break;
                }
                case 'L': { float nx = num(), ny = num(); if (rel) { nx += x; ny += y; } path.AddLine(x, y, nx, ny); x = nx; y = ny; break; }
                case 'H': { float nx = num(); if (rel) nx += x; path.AddLine(x, y, nx, y); x = nx; break; }
                case 'V': { float ny = num(); if (rel) ny += y; path.AddLine(x, y, x, ny); y = ny; break; }
                case 'C': {
                    float x1 = num(), y1 = num(), x2 = num(), y2 = num(), nx = num(), ny = num();
                    if (rel) { x1 += x; y1 += y; x2 += x; y2 += y; nx += x; ny += y; }
                    path.AddBezier(x, y, x1, y1, x2, y2, nx, ny); x = nx; y = ny; break;
                }
                case 'Z': path.CloseFigure(); x = sx; y = sy; break;
                default: throw new Exception("Unsupported path command " + cmd);
            }
        }
        return path;
    }

    public static Bitmap Render(string d, int size, string color) {
        const int ss = 8;                      // supersample, then downscale
        int big = size * ss;
        using (var hi = new Bitmap(big, big, PixelFormat.Format32bppArgb)) {
            using (var g = Graphics.FromImage(hi))
            using (var p = Parse(d))
            using (var b = new SolidBrush(ColorTranslator.FromHtml(color))) {
                g.SmoothingMode = SmoothingMode.AntiAlias;
                g.ScaleTransform(big / 24f, big / 24f);
                g.FillPath(b, p);
            }
            var o = new Bitmap(size, size, PixelFormat.Format32bppArgb);
            using (var g = Graphics.FromImage(o)) {
                g.InterpolationMode = InterpolationMode.HighQualityBicubic;
                g.PixelOffsetMode = PixelOffsetMode.HighQuality;
                g.DrawImage(hi, new Rectangle(0, 0, size, size));
            }
            return o;
        }
    }
}
"@

# One .ico from a renderer scriptblock ($s -> 32bpp Bitmap of $s x $s).
function Save-Ico([string]$path, [int[]]$sizes, [scriptblock]$render) {
    $frames = @()
    foreach ($s in $sizes) {
        $bmp = & $render $s
        $ms = New-Object System.IO.MemoryStream
        if ($s -ge 256) {
            $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
        } else {
            $d = $bmp.LockBits((New-Object System.Drawing.Rectangle(0, 0, $s, $s)),
                               [System.Drawing.Imaging.ImageLockMode]::ReadOnly,
                               [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
            $px = New-Object byte[] ($d.Stride * $s)
            [System.Runtime.InteropServices.Marshal]::Copy($d.Scan0, $px, 0, $px.Length)
            $bmp.UnlockBits($d)
            $w = New-Object System.IO.BinaryWriter($ms)
            $w.Write([UInt32]40); $w.Write([Int32]$s); $w.Write([Int32]($s * 2))   # BITMAPINFOHEADER
            $w.Write([UInt16]1);  $w.Write([UInt16]32); $w.Write([UInt32]0); $w.Write([UInt32]0)
            $w.Write([Int32]0);   $w.Write([Int32]0);  $w.Write([UInt32]0); $w.Write([UInt32]0)
            for ($y = $s - 1; $y -ge 0; $y--) { $w.Write($px, $y * $d.Stride, $s * 4) }   # bottom-up BGRA
            $w.Write((New-Object byte[] ([int]([Math]::Floor(($s + 31) / 32) * 4) * $s)))  # AND mask
            $w.Flush()
        }
        $frames += ,$ms.ToArray()
        $bmp.Dispose()
    }

    $fs = New-Object System.IO.FileStream($path, [System.IO.FileMode]::Create)
    $bw = New-Object System.IO.BinaryWriter($fs)
    $bw.Write([UInt16]0); $bw.Write([UInt16]1); $bw.Write([UInt16]$sizes.Count)
    $offset = 6 + 16 * $sizes.Count
    for ($k = 0; $k -lt $sizes.Count; $k++) {
        $s = $sizes[$k]; $dim = if ($s -ge 256) { 0 } else { $s }
        $bw.Write([byte]$dim); $bw.Write([byte]$dim); $bw.Write([byte]0); $bw.Write([byte]0)
        $bw.Write([UInt16]1); $bw.Write([UInt16]32)
        $bw.Write([UInt32]$frames[$k].Length); $bw.Write([UInt32]$offset)
        $offset += $frames[$k].Length
    }
    foreach ($f in $frames) { $bw.Write($f) }
    $bw.Flush(); $fs.Close()
    Write-Host ("Wrote {0} ({1:N0} bytes)" -f $path, (Get-Item $path).Length)
}

function Get-Scaled([System.Drawing.Image]$img, [int]$s) {
    $bmp = New-Object System.Drawing.Bitmap($s, $s, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g   = [System.Drawing.Graphics]::FromImage($bmp)
    $g.InterpolationMode  = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $g.PixelOffsetMode    = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $g.CompositingQuality = [System.Drawing.Drawing2D.CompositingQuality]::HighQuality
    $g.DrawImage($img, (New-Object System.Drawing.Rectangle(0, 0, $s, $s)))
    $g.Dispose()
    return $bmp
}

# App icon: 1024 px master; the glyph spans ~92% of the canvas, like TinyFanControl's T.
$master = [GlyphCut]::Build($srcPath, 1024, 0.92)
Save-Ico $icoPath @(16, 20, 24, 32, 40, 48, 64, 128, 256) { param($s) Get-Scaled $master $s }
$preview = Get-Scaled $master 256
$preview.Save($pngPath, [System.Drawing.Imaging.ImageFormat]::Png); $preview.Dispose()
$master.Dispose()
Write-Host ("Wrote {0}" -f $pngPath)

# Link icons, one .ico per colour: Silicon Tuner's --text-secondary at rest and
# --accent on hover. Sizes cover the 12x12 DLU buttons from 100% to 300% scale.
$patreon = 'M22.957 7.21c-.004-3.064-2.391-5.576-5.191-6.482-3.478-1.125-8.064-.962-11.384.604C2.357 3.231 1.093 7.391 1.046 11.54c-.039 3.411.302 12.396 5.369 12.46 3.765.047 4.326-4.804 6.068-7.141 1.24-1.662 2.836-2.132 4.801-2.618 3.376-.836 5.678-3.501 5.673-7.031Z'
$xmark   = 'M14.234 10.162 22.977 0h-2.072l-7.591 8.824L7.251 0H.258l9.168 13.343L.258 24H2.33l8.016-9.318L16.749 24h6.993zm-2.837 3.299-.929-1.329L3.076 1.56h3.182l5.965 8.532.929 1.329 7.754 11.09h-3.182z'
$linkSizes = @(10, 12, 14, 16, 18, 20, 24, 28, 32, 40)
$iconDir = Join-Path $root 'icons'
New-Item -ItemType Directory -Force $iconDir | Out-Null
foreach ($m in @(@('patreon', $patreon), @('x', $xmark))) {
    $markPath = $m[1]
    # GetNewClosure: Save-Ico has locals of its own that a plain scriptblock would see.
    Save-Ico (Join-Path $iconDir "$($m[0]).ico")       $linkSizes ({ param($s) [SvgMark]::Render($markPath, $s, '#c4c4c4') }.GetNewClosure())
    Save-Ico (Join-Path $iconDir "$($m[0])_hover.ico") $linkSizes ({ param($s) [SvgMark]::Render($markPath, $s, '#fcee0a') }.GetNewClosure())
}
