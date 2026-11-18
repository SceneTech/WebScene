use crate::{
    ffi::{webscene_canvas_layer as Layer, webscene_scene_command as Command},
    Error, Scene,
};
use skia_safe::{
    BlendMode, Canvas, ClipOp, Color, Matrix, Paint, Path, PathBuilder, PathFillType, Picture,
    PictureRecorder, Point, RRect, Rect, M44,
};
use std::{collections::BTreeMap, sync::Arc};

#[derive(Default)]
pub(crate) struct Frame {
    background: Option<Picture>,
    foreground: Option<Picture>,
    layers: Vec<RetainedLayer>,
}
impl Frame {
    pub fn draw(&self, canvas: &Canvas) {
        if let Some(p) = &self.background {
            canvas.draw_picture(p, None, None);
        }
        for l in &self.layers {
            let m = &l.meta;
            if m.width <= 0.0 || m.height <= 0.0 || m.bitmap_width == 0 || m.bitmap_height == 0 {
                continue;
            }
            let save = canvas.save();
            canvas.clip_rect(
                Rect::from_xywh(m.x, m.y, m.width, m.height),
                ClipOp::Intersect,
                false,
            );
            canvas.translate((m.x, m.y));
            canvas.scale((
                m.width / m.bitmap_width as f32,
                m.height / m.bitmap_height as f32,
            ));
            canvas.save_layer_alpha_f(
                Rect::from_wh(m.bitmap_width as f32, m.bitmap_height as f32),
                1.0,
            );
            canvas.draw_picture(&l.picture, None, None);
            canvas.restore_to_count(save);
        }
        if let Some(p) = &self.foreground {
            canvas.draw_picture(p, None, None);
        }
    }
}
#[derive(Clone)]
struct RetainedLayer {
    meta: Layer,
    picture: Picture,
}
#[derive(Default)]
pub(crate) struct Renderer {
    revision: u64,
    layers: BTreeMap<u32, RetainedLayer>,
    pub frame: Arc<Frame>,
}
impl Renderer {
    pub fn accepts(&self, scene: &Scene) -> bool {
        scene.header.flags & 1 != 0
            || scene.header.base_revision == self.revision
            || scene.header.revision == self.revision
    }
    pub fn apply(&mut self, scene: &Scene) -> Result<(), Error> {
        if scene.header.revision == self.revision && scene.header.flags & 1 == 0 {
            return Ok(());
        }
        let checkpoint = scene.header.flags & 1 != 0;
        // Build the entire candidate first. A failed layer must not partially mutate retained state.
        let mut layers = if checkpoint {
            BTreeMap::new()
        } else {
            self.layers.clone()
        };
        let (background, foreground) = if scene.header.flags & 2 != 0 {
            (Some(dom(scene, false)?), Some(dom(scene, true)?))
        } else if checkpoint {
            (None, None)
        } else {
            (self.frame.background.clone(), self.frame.foreground.clone())
        };
        for l in &scene.layers {
            if l.flags & 2 != 0 {
                layers.remove(&l.node_id);
            } else if l.flags & 1 == 0 {
                return Err(Error(format!("Unsupported canvas layer flags {}", l.flags)));
            }
        }
        // drawImage(canvas) can refer to a source later in DOM order. Resolve changed
        // source layers first. Cycles and self-copies see the previous retained version.
        let mut states = vec![0u8; scene.layers.len()];
        for i in 0..scene.layers.len() {
            compile_layer(i, scene, &mut layers, &mut states)?;
        }
        let mut ordered: Vec<_> = layers.values().cloned().collect();
        ordered.sort_by_key(|l| (l.meta.reserved as i32, l.meta.node_id));
        self.frame = Arc::new(Frame {
            background,
            foreground,
            layers: ordered,
        });
        self.layers = layers;
        self.revision = scene.header.revision;
        Ok(())
    }
}
pub(crate) fn rgba(n: u32) -> Color {
    Color::from_argb(n as u8, (n >> 24) as u8, (n >> 16) as u8, (n >> 8) as u8)
}
fn paint(c: Color) -> Paint {
    let mut p = Paint::default();
    p.set_color(c).set_anti_alias(true);
    p
}
fn rect(c: &Command) -> Rect {
    Rect::from_xywh(c.x, c.y, c.width, c.height)
}
fn unsupported(what: &str, kind: u32) -> Error {
    Error(format!(
        "FerroUI presenter does not support {what} command {kind}"
    ))
}
fn dom(scene: &Scene, foreground: bool) -> Result<Picture, Error> {
    let mut recorder = PictureRecorder::new();
    let canvas = recorder.begin_recording(
        Rect::from_wh(
            scene.header.viewport_width.max(1.0),
            scene.header.viewport_height.max(1.0),
        ),
        false,
    );
    let root = canvas.save();
    for (index, c) in scene.commands.iter().enumerate() {
        let rounded = rounded_at(scene, index);
        let mut p = paint(rgba(c.rgba));
        match c.kind {
            30 => {
                canvas.save_layer_alpha(None, c.rgba & 255);
            }
            31 | 13 | 16 | 20 => {
                if canvas.save_count() > root + 1 {
                    canvas.restore();
                }
            }
            12 => {
                canvas.save();
                canvas.clip_rrect(rounded, ClipOp::Intersect, true);
            }
            15 => {
                canvas.save();
                canvas.translate((c.x, c.y));
                canvas.scale((c.width, c.height));
                canvas.translate((-c.x, -c.y));
            }
            19 => {
                canvas.save();
                canvas.rotate(c.stroke_width, Some((c.x, c.y).into()));
            }
            1 | 9 if (c.kind == 9) == foreground => {
                canvas.draw_rect(rect(c), &p);
            }
            2 | 14 if (c.kind == 14) == foreground => {
                p.set_stroke_width((c.flags as f32 / 100.0).max(0.1));
                canvas.draw_line((c.x, c.y), (c.width, c.height), &p);
            }
            7 | 10 if (c.kind == 10) == foreground => {
                canvas.draw_rrect(rounded, &p);
            }
            8 | 11 if (c.kind == 11) == foreground => {
                p.set_style(skia_safe::paint::Style::Stroke)
                    .set_stroke_width(if c.stroke_width > 0.0 {
                        c.stroke_width
                    } else {
                        (c.flags & 0xffff) as f32 / 100.0
                    });
                canvas.draw_rrect(rounded, &p);
            }
            17 | 18 if (c.kind == 18) == foreground => {
                if c.stroke_width > 0.0 {
                    p.set_mask_filter(skia_safe::MaskFilter::blur(
                        skia_safe::BlurStyle::Normal,
                        c.stroke_width * 0.5,
                        None,
                    ));
                }
                canvas.draw_rrect(rounded, &p);
            }
            3 if foreground => {
                let parts: Vec<_> = scene.string(c.flags).splitn(7, '\t').collect();
                if parts.len() < 6 {
                    continue;
                }
                let size = parts[0].parse().unwrap_or(16.0);
                let text = parts[if parts.len() == 7 { 6 } else { 5 }];
                let para = crate::text::paragraph(
                    text,
                    parts[4],
                    size,
                    parts[2].parse().unwrap_or(400),
                    c.radius_top_left,
                    c.radius_top_right,
                    rgba(c.rgba),
                );
                let width = para.max_intrinsic_width();
                let x = c.x
                    + match parts[3] {
                        "center" => (c.width - width) * 0.5,
                        "right" | "end" => c.width - width,
                        _ => 0.0,
                    };
                para.paint(
                    canvas,
                    (x, c.y + ((c.height - para.height()) * 0.5).max(0.0)),
                );
            }
            4 | 5 if foreground => {
                let parts: Vec<_> = scene.string(c.flags).splitn(4, '\t').collect();
                if parts.len() != 4 {
                    continue;
                }
                let vb = numbers(parts[0]);
                if vb.len() < 4 || vb[2] <= 0.0 || vb[3] <= 0.0 {
                    continue;
                }
                if let Some(path) = Path::from_svg(parts[3]) {
                    let save = canvas.save();
                    canvas.rotate(
                        c.stroke_width,
                        Some((c.x + c.width * 0.5, c.y + c.height * 0.5).into()),
                    );
                    canvas.translate((c.x, c.y));
                    canvas.scale((c.width / vb[2], c.height / vb[3]));
                    canvas.translate((-vb[0], -vb[1]));
                    let transform = numbers(parts[2]);
                    if transform.len() == 6 {
                        canvas.concat(&matrix(&transform));
                    }
                    if c.kind == 5 {
                        p.set_style(skia_safe::paint::Style::Stroke)
                            .set_stroke_width(parts[1].parse().unwrap_or(1.0));
                    }
                    canvas.draw_path(&path, &p);
                    canvas.restore_to_count(save);
                }
            }
            6 if foreground => {
                svg(canvas, scene.string(c.flags), rect(c), c.stroke_width)?;
            }
            21 | 22 if (c.kind == 22) == foreground => {
                let resource = scene.string(c.flags);
                let gradient = resource
                    .strip_prefix("webscene-bg-v2\t")
                    .and_then(|s| s.split('\t').next())
                    .unwrap_or(resource);
                if let Some(shader) = linear_gradient(gradient, rect(c)) {
                    p.set_shader(shader);
                    canvas.draw_rrect(rounded, &p);
                } else {
                    return Err(Error(format!(
                        "Unsupported background gradient: {resource}"
                    )));
                }
            }
            33 | 34 if (c.kind == 34) == foreground => {
                border_polygon(canvas, c, &p);
            }
            1..=22 | 32..=34 => {}
            _ => return Err(unsupported("DOM", c.kind)),
        }
    }
    canvas.restore_to_count(root);
    recorder
        .finish_recording_as_picture(None)
        .ok_or_else(|| Error("Could not record DOM".into()))
}
fn numbers(s: &str) -> Vec<f32> {
    s.split(|c: char| c.is_whitespace() || c == ',' || c == '(' || c == ')')
        .filter_map(|v| v.parse().ok())
        .collect()
}
fn matrix(v: &[f32]) -> Matrix {
    Matrix::new_all(v[0], v[2], v[4], v[1], v[3], v[5], 0.0, 0.0, 1.0)
}
fn svg(canvas: &Canvas, resource: &str, destination: Rect, rotation: f32) -> Result<(), Error> {
    let Some(start) = resource.find('<') else {
        return Ok(());
    };
    if destination.width() <= 0.0 || destination.height() <= 0.0 {
        return Ok(());
    }
    // rust-skia's SVG feature pulls in a second HarfBuzz, which conflicts with FerroUI.
    // Rasterize SVG with resvg instead, then retain the image in the Skia picture.
    let mut options = resvg::usvg::Options::default();
    options.fontdb_mut().load_system_fonts();
    let tree = resvg::usvg::Tree::from_str(&resource[start..], &options)
        .map_err(|e| Error(format!("Invalid SVG: {e}")))?;
    let width = (destination.width() * 2.0).ceil() as u32;
    let height = (destination.height() * 2.0).ceil() as u32;
    if width == 0 || height == 0 || u64::from(width) * u64::from(height) > 16 * 1024 * 1024 {
        return Err(Error("SVG raster exceeds 16 megapixels".into()));
    }
    let mut pixels = resvg::tiny_skia::Pixmap::new(width, height)
        .ok_or_else(|| Error("Could not allocate SVG raster".into()))?;
    let size = tree.size();
    let scale = (width as f32 / size.width()).min(height as f32 / size.height());
    let transform = resvg::tiny_skia::Transform::from_row(
        scale,
        0.0,
        0.0,
        scale,
        (width as f32 - size.width() * scale) * 0.5,
        (height as f32 - size.height() * scale) * 0.5,
    );
    resvg::render(&tree, transform, &mut pixels.as_mut());
    let info = skia_safe::ImageInfo::new(
        (width as i32, height as i32),
        skia_safe::ColorType::RGBA8888,
        skia_safe::AlphaType::Premul,
        None,
    );
    let image = skia_safe::images::raster_from_data(
        &info,
        skia_safe::Data::new_copy(pixels.data()),
        width as usize * 4,
    )
    .ok_or_else(|| Error("Could not create SVG image".into()))?;
    let save = canvas.save();
    canvas.rotate(rotation, Some(destination.center()));
    canvas.draw_image_rect(&image, None, destination, &Paint::default());
    canvas.restore_to_count(save);
    Ok(())
}
fn color(s: &str) -> Color {
    s.parse::<csscolorparser::Color>()
        .map(|c| {
            let [r, g, b, a] = c.to_rgba8();
            Color::from_argb(a, r, g, b)
        })
        .unwrap_or(Color::BLACK)
}
fn split_components(s: &str) -> Vec<&str> {
    let (mut depth, mut start) = (0, 0);
    let mut parts = vec![];
    for (i, c) in s.char_indices() {
        match c {
            '(' => depth += 1,
            ')' => depth -= 1,
            ',' if depth == 0 => {
                parts.push(s[start..i].trim());
                start = i + 1;
            }
            _ => {}
        }
    }
    parts.push(s[start..].trim());
    parts
}
fn linear_gradient(s: &str, r: Rect) -> Option<skia_safe::Shader> {
    let s = s.strip_prefix("linear-gradient(")?.strip_suffix(')')?;
    let mut parts = split_components(s);
    let mut angle = 180.0f32;
    if let Some(a) = parts[0]
        .strip_suffix("deg")
        .and_then(|s| s.parse::<f32>().ok())
    {
        angle = a;
        parts.remove(0);
    } else if parts[0].starts_with("to ") {
        angle = match parts[0] {
            "to top" => 0.0,
            "to right" => 90.0,
            "to bottom" => 180.0,
            "to left" => 270.0,
            _ => return None,
        };
        parts.remove(0);
    }
    if parts.len() < 2 {
        return None;
    }
    let (mut colors, mut positions) = (vec![], vec![]);
    for (i, s) in parts.iter().enumerate() {
        let (value, pos) = if let Some((c, p)) = s.rsplit_once(' ') {
            if let Some(n) = p.strip_suffix('%').and_then(|n| n.parse::<f32>().ok()) {
                (c, n / 100.0)
            } else {
                (*s, i as f32 / (parts.len() - 1) as f32)
            }
        } else {
            (*s, i as f32 / (parts.len() - 1) as f32)
        };
        let c = value.parse::<csscolorparser::Color>().ok()?;
        let [r, g, b, a] = c.to_rgba8();
        colors.push(skia_safe::Color4f::from(Color::from_argb(a, r, g, b)));
        positions.push(pos);
    }
    let d = (angle.to_radians().sin(), -angle.to_radians().cos());
    let half = (r.width() * d.0.abs() + r.height() * d.1.abs()) * 0.5;
    let center = r.center();
    use skia_safe::gradient::{Colors, Gradient, Interpolation};
    skia_safe::gradient::shaders::linear_gradient(
        (
            (center.x - d.0 * half, center.y - d.1 * half),
            (center.x + d.0 * half, center.y + d.1 * half),
        ),
        &Gradient::new(
            Colors::new(&colors, Some(&positions), skia_safe::TileMode::Clamp, None),
            Interpolation::default(),
        ),
        None,
    )
}

#[derive(Clone)]
struct State {
    fill: String,
    stroke: String,
    width: f32,
    alpha: f32,
    font: String,
    align: String,
    baseline: String,
    blend: BlendMode,
    cap: skia_safe::paint::Cap,
    join: skia_safe::paint::Join,
    miter: f32,
    dash: Vec<f32>,
    dash_offset: f32,
    shadow: Color,
    blur: f32,
    shadow_x: f32,
    shadow_y: f32,
}
impl Default for State {
    fn default() -> Self {
        Self {
            fill: "black".into(),
            stroke: "black".into(),
            width: 1.0,
            alpha: 1.0,
            font: "10px sans-serif".into(),
            align: "start".into(),
            baseline: "alphabetic".into(),
            blend: BlendMode::SrcOver,
            cap: skia_safe::paint::Cap::Butt,
            join: skia_safe::paint::Join::Miter,
            miter: 10.0,
            dash: vec![],
            dash_offset: 0.0,
            shadow: Color::TRANSPARENT,
            blur: 0.0,
            shadow_x: 0.0,
            shadow_y: 0.0,
        }
    }
}
impl State {
    fn paint(&self, fill: bool) -> Paint {
        let mut p = paint(color(if fill { &self.fill } else { &self.stroke }));
        p.set_alpha_f(p.alpha_f() * self.alpha)
            .set_blend_mode(self.blend);
        if !fill {
            p.set_style(skia_safe::paint::Style::Stroke)
                .set_stroke_width(self.width)
                .set_stroke_cap(self.cap)
                .set_stroke_join(self.join)
                .set_stroke_miter(self.miter);
            if !self.dash.is_empty() {
                let mut dash = self.dash.clone();
                if !dash.len().is_multiple_of(2) {
                    dash.extend(self.dash.iter().copied());
                }
                p.set_path_effect(skia_safe::PathEffect::dash(&dash, self.dash_offset));
            }
        }
        if self.shadow.a() != 0 {
            p.set_image_filter(skia_safe::image_filters::drop_shadow(
                (self.shadow_x, self.shadow_y),
                (self.blur * 0.5, self.blur * 0.5),
                self.shadow,
                None,
                None,
                None,
            ));
        }
        p
    }
}
fn canvas_layer(
    scene: &Scene,
    l: &Layer,
    layers: &BTreeMap<u32, RetainedLayer>,
) -> Result<Picture, Error> {
    let mut recorder = PictureRecorder::new();
    let canvas = recorder.begin_recording(
        Rect::from_wh(l.bitmap_width.max(1) as f32, l.bitmap_height.max(1) as f32),
        false,
    );
    let root = canvas.save();
    let mut state = State::default();
    let mut stack = vec![];
    let mut path = PathBuilder::new();
    for c in &scene.canvas[l.command_offset as usize..(l.command_offset + l.command_count) as usize]
    {
        // Generated from the ABI union: every command initializes its eight doubles.
        let v = unsafe { c.data.values }.map(|n| n as f32);
        let resource = scene.layer_string(l, c.resource_id);
        let r = Rect::from_xywh(v[0], v[1], v[2], v[3]);
        let before = canvas.local_to_device_as_3x3();
        match c.kind {
            1 => {
                stack.push(state.clone());
                canvas.save();
            }
            2 => {
                if let Some(s) = stack.pop() {
                    state = s;
                    canvas.restore();
                }
            }
            3 => {
                canvas.reset_matrix();
            }
            4 => {
                canvas.set_matrix(&M44::from(matrix(&v)));
            }
            5 => {
                canvas.concat(&matrix(&v));
            }
            6 => {
                canvas.translate((v[0], v[1]));
            }
            7 => {
                canvas.scale((v[0], v[1]));
            }
            8 => {
                canvas.rotate(v[0].to_degrees(), None);
            }
            9 => {
                path = PathBuilder::new();
            }
            10 => {
                path.close();
            }
            11 => {
                path.move_to((v[0], v[1]));
            }
            12 => {
                path.line_to((v[0], v[1]));
            }
            13 => {
                path.cubic_to((v[0], v[1]), (v[2], v[3]), (v[4], v[5]));
            }
            14 => {
                path.quad_to((v[0], v[1]), (v[2], v[3]));
            }
            15 => {
                arc(&mut path, &v, false);
            }
            16 => {
                path.arc_to_tangent((v[0], v[1]), (v[2], v[3]), v[4].max(0.0));
            }
            17 => {
                path.add_rect(r, None, None);
            }
            18 => {
                path.set_fill_type(if c.flags & (1 << 16) != 0 {
                    PathFillType::EvenOdd
                } else {
                    PathFillType::Winding
                });
                canvas.clip_path(&path.snapshot(), ClipOp::Intersect, true);
            }
            19 => {
                state.dash = v[1..1 + v[0].clamp(0.0, 7.0) as usize].to_vec();
            }
            20 | 21 => {
                path.set_fill_type(if c.flags & (1 << 16) != 0 {
                    PathFillType::EvenOdd
                } else {
                    PathFillType::Winding
                });
                canvas.draw_path(&path.snapshot(), &state.paint(c.kind == 21));
            }
            22 | 23 => {
                canvas.draw_rect(r, &state.paint(c.kind == 22));
            }
            24 => {
                let mut p = Paint::default();
                p.set_blend_mode(BlendMode::Clear);
                canvas.draw_rect(r, &p);
            }
            25 | 26 => {
                let (size, family) = font(&state.font);
                let p = state.paint(c.kind == 25);
                let para =
                    crate::text::paragraph_with_paint(resource, &family, size, 400, 0.0, 0.0, &p);
                let width = para.max_intrinsic_width();
                let x = v[0]
                    + match state.align.as_str() {
                        "center" => -width * 0.5,
                        "right" | "end" => -width,
                        _ => 0.0,
                    };
                let y = v[1]
                    - match state.baseline.as_str() {
                        "top" | "hanging" => 0.0,
                        "middle" => para.height() * 0.5,
                        "bottom" | "ideographic" => para.height(),
                        _ => para.alphabetic_baseline(),
                    };
                let save = canvas.save();
                if c.flags & (1 << 17) != 0 && v[2] > 0.0 && width > v[2] {
                    canvas.translate((v[0], 0.0));
                    canvas.scale((v[2] / width, 1.0));
                    canvas.translate((-v[0], 0.0));
                }
                para.paint(canvas, (x, y));
                canvas.restore_to_count(save);
            }
            27 => {
                if let Some(source) = layers.get(&c.resource_id) {
                    if v[2] != 0.0 && v[3] != 0.0 {
                        let save = canvas.save();
                        canvas.clip_rect(
                            Rect::from_xywh(v[4], v[5], v[6], v[7]),
                            ClipOp::Intersect,
                            false,
                        );
                        canvas.translate((v[4], v[5]));
                        canvas.scale((v[6] / v[2], v[7] / v[3]));
                        canvas.translate((-v[0], -v[1]));
                        canvas.draw_picture(&source.picture, None, Some(&state.paint(true)));
                        canvas.restore_to_count(save);
                    }
                }
            }
            28 | 29 => {
                if let Some(path) = Path::from_svg(resource) {
                    canvas.draw_path(&path, &state.paint(c.kind == 28));
                }
            }
            30 => {
                arc(&mut path, &v, true);
            }
            31 => {
                if v[2] != 0.0 && v[3] != 0.0 {
                    let save = canvas.save();
                    canvas.clip_rect(
                        Rect::from_xywh(v[4], v[5], v[6], v[7]),
                        ClipOp::Intersect,
                        false,
                    );
                    canvas.translate((v[4], v[5]));
                    canvas.scale((v[6] / v[2], v[7] / v[3]));
                    canvas.translate((-v[0], -v[1]));
                    let vb = numbers(resource.split('\t').next().unwrap_or(""));
                    if vb.len() >= 4 {
                        svg(canvas, resource, Rect::from_wh(vb[2], vb[3]), 0.0)?;
                    }
                    canvas.restore_to_count(save);
                }
            }
            40 => state.fill = resource.into(),
            41 => state.stroke = resource.into(),
            42 => state.width = v[0],
            43 => {
                state.cap = match resource {
                    "round" => skia_safe::paint::Cap::Round,
                    "square" => skia_safe::paint::Cap::Square,
                    _ => skia_safe::paint::Cap::Butt,
                }
            }
            44 => {
                state.join = match resource {
                    "round" => skia_safe::paint::Join::Round,
                    "bevel" => skia_safe::paint::Join::Bevel,
                    _ => skia_safe::paint::Join::Miter,
                }
            }
            45 => state.miter = v[0],
            46 => state.alpha = v[0].clamp(0.0, 1.0),
            47 => state.dash_offset = v[0],
            48 => state.font = resource.into(),
            49 => state.align = resource.into(),
            50 => state.baseline = resource.into(),
            51 | 52 => {} // Sampling options do not affect vector pictures.
            53 => state.blend = blend(resource)?,
            54 => state.shadow = color(resource),
            55 => state.blur = v[0].max(0.0),
            56 => state.shadow_x = v[0],
            57 => state.shadow_y = v[0],
            _ => return Err(unsupported("Canvas2D", c.kind)),
        }
        // Canvas paths are fixed in device space at construction, unlike Skia paths.
        if matches!(c.kind, 2..=8) {
            let after = canvas.local_to_device_as_3x3();
            if let Some(inv) = after.invert() {
                path.transform(&Matrix::concat(&inv, &before));
            }
        }
    }
    canvas.restore_to_count(root);
    recorder
        .finish_recording_as_picture(None)
        .ok_or_else(|| Error("Could not record canvas".into()))
}
fn font(s: &str) -> (f32, String) {
    let Some(pos) = s.find("px") else {
        return (10.0, "sans-serif".into());
    };
    let size = s[..pos]
        .split_whitespace()
        .last()
        .and_then(|s| s.parse().ok())
        .unwrap_or(10.0);
    (size, s[pos + 2..].trim().to_string())
}
fn arc(path: &mut PathBuilder, v: &[f32; 8], ellipse: bool) {
    let (rx, ry, rotation, start, end, ccw) = if ellipse {
        (v[2], v[3], v[4], v[5], v[6], v[7] != 0.0)
    } else {
        (v[2], v[2], 0.0, v[3], v[4], v[5] != 0.0)
    };
    if rx < 0.0 || ry < 0.0 {
        return;
    }
    let tau = std::f32::consts::TAU;
    let raw = end - start;
    let sweep = if !ccw && raw >= tau {
        tau
    } else if ccw && -raw >= tau {
        -tau
    } else if ccw {
        -(-raw).rem_euclid(tau)
    } else {
        raw.rem_euclid(tau)
    };
    let mut arc = PathBuilder::new();
    arc.arc_to(
        Rect::from_xywh(-rx, -ry, rx * 2.0, ry * 2.0),
        start.to_degrees(),
        sweep.to_degrees(),
        true,
    );
    let mut m = Matrix::rotate_deg(rotation.to_degrees());
    m.post_translate((v[0], v[1]));
    arc.transform(&m);
    path.add_path(&arc.snapshot(), Some(skia_safe::path::AddPathMode::Extend));
}
fn blend(s: &str) -> Result<BlendMode, Error> {
    Ok(match s {
        "source-over" => BlendMode::SrcOver,
        "source-in" => BlendMode::SrcIn,
        "source-out" => BlendMode::SrcOut,
        "source-atop" => BlendMode::SrcATop,
        "destination-over" => BlendMode::DstOver,
        "destination-in" => BlendMode::DstIn,
        "destination-out" => BlendMode::DstOut,
        "destination-atop" => BlendMode::DstATop,
        "copy" => BlendMode::Src,
        "xor" => BlendMode::Xor,
        "lighter" => BlendMode::Plus,
        "multiply" => BlendMode::Multiply,
        "screen" => BlendMode::Screen,
        "overlay" => BlendMode::Overlay,
        "darken" => BlendMode::Darken,
        "lighten" => BlendMode::Lighten,
        "color-dodge" => BlendMode::ColorDodge,
        "color-burn" => BlendMode::ColorBurn,
        "hard-light" => BlendMode::HardLight,
        "soft-light" => BlendMode::SoftLight,
        "difference" => BlendMode::Difference,
        "exclusion" => BlendMode::Exclusion,
        "hue" => BlendMode::Hue,
        "saturation" => BlendMode::Saturation,
        "color" => BlendMode::Color,
        "luminosity" => BlendMode::Luminosity,
        _ => return Err(Error(format!("Unsupported composite mode {s}"))),
    })
}

fn rounded_at(scene: &Scene, index: usize) -> RRect {
    let c = &scene.commands[index];
    let mut x = [
        c.radius_top_left,
        c.radius_top_right,
        c.radius_bottom_right,
        c.radius_bottom_left,
    ];
    let metadata = index
        .checked_sub(1)
        .map(|i| &scene.commands[i])
        .filter(|m| m.kind == 32 && m.node_id == c.node_id && rect(m) == rect(c));
    if metadata.is_none() && matches!(c.kind, 7 | 8 | 10 | 11) && x.iter().all(|v| *v <= 0.0) {
        x.fill((c.flags >> 16) as f32 / 100.0);
    }
    let y = metadata
        .map(|m| {
            [
                m.radius_top_left,
                m.radius_top_right,
                m.radius_bottom_right,
                m.radius_bottom_left,
            ]
        })
        .unwrap_or(x);
    RRect::new_rect_radii(
        rect(c),
        &std::array::from_fn(|i| Point::new(x[i].max(0.0), y[i].max(0.0))),
    )
}
fn border_polygon(canvas: &Canvas, c: &Command, p: &Paint) {
    let r = rect(c);
    let inner = Rect::new(
        r.left + c.radius_bottom_left.clamp(0.0, c.width),
        r.top + c.radius_top_left.clamp(0.0, c.height),
        r.right - c.radius_top_right.clamp(0.0, c.width),
        r.bottom - c.radius_bottom_right.clamp(0.0, c.height),
    );
    let points = match c.flags {
        1 => [
            (r.left, r.top),
            (r.right, r.top),
            (inner.right, inner.top),
            (inner.left, inner.top),
        ],
        2 => [
            (r.right, r.top),
            (r.right, r.bottom),
            (inner.right, inner.bottom),
            (inner.right, inner.top),
        ],
        4 => [
            (r.right, r.bottom),
            (r.left, r.bottom),
            (inner.left, inner.bottom),
            (inner.right, inner.bottom),
        ],
        8 => [
            (r.left, r.bottom),
            (r.left, r.top),
            (inner.left, inner.top),
            (inner.left, inner.bottom),
        ],
        _ => return,
    };
    let mut path = PathBuilder::new();
    path.move_to(points[0]);
    for p in &points[1..] {
        path.line_to(*p);
    }
    path.close();
    canvas.draw_path(&path.snapshot(), p);
}

fn compile_layer(
    index: usize,
    scene: &Scene,
    layers: &mut BTreeMap<u32, RetainedLayer>,
    states: &mut [u8],
) -> Result<(), Error> {
    let l = &scene.layers[index];
    if states[index] != 0 || l.flags & 2 != 0 {
        return Ok(());
    }
    states[index] = 1;
    for c in &scene.canvas[l.command_offset as usize..(l.command_offset + l.command_count) as usize]
    {
        if c.kind == 27 && c.resource_id != l.node_id {
            if let Some(source) = scene
                .layers
                .iter()
                .position(|other| other.node_id == c.resource_id)
            {
                compile_layer(source, scene, layers, states)?;
            }
        }
    }
    let picture = canvas_layer(scene, l, layers)?;
    layers.insert(l.node_id, RetainedLayer { meta: *l, picture });
    states[index] = 2;
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::ffi;
    fn scene(revision: u64, base: u64, flags: u32) -> Scene {
        Scene {
            header: ffi::webscene_scene_header {
                revision,
                base_revision: base,
                flags,
                viewport_width: 40.0,
                viewport_height: 40.0,
                ..Default::default()
            },
            commands: vec![],
            layers: vec![],
            canvas: vec![],
            strings: vec![],
        }
    }
    fn command(kind: u32, values: [f64; 8]) -> ffi::webscene_canvas_command {
        ffi::webscene_canvas_command {
            kind,
            data: ffi::webscene_canvas_command_data { values },
            ..Default::default()
        }
    }
    fn layer(id: u32, offset: u32, count: u32) -> Layer {
        Layer {
            node_id: id,
            flags: 1,
            command_offset: offset,
            command_count: count,
            width: 40.0,
            height: 40.0,
            bitmap_width: 40,
            bitmap_height: 40,
            ..Default::default()
        }
    }
    fn pixel(renderer: &Renderer, x: i32, y: i32) -> Color {
        let mut s = skia_safe::surfaces::raster_n32_premul((40, 40)).unwrap();
        s.canvas().clear(Color::TRANSPARENT);
        renderer.frame.draw(s.canvas());
        s.peek_pixels().unwrap().get_color((x, y))
    }
    #[test]
    fn clear_rect_is_isolated_from_dom_and_layer_removals_preserve_background() {
        let mut s = scene(1, 0, 3);
        s.commands.push(Command {
            kind: 1,
            width: 40.0,
            height: 40.0,
            rgba: 0xff0000ff,
            ..Default::default()
        });
        s.layers.push(layer(1, 0, 2));
        s.canvas
            .push(command(22, [0.0, 0.0, 40.0, 40.0, 0.0, 0.0, 0.0, 0.0]));
        s.canvas
            .push(command(24, [5.0, 5.0, 20.0, 20.0, 0.0, 0.0, 0.0, 0.0]));
        let mut renderer = Renderer::default();
        renderer.apply(&s).unwrap();
        assert_eq!(pixel(&renderer, 10, 10), Color::RED);
        assert_eq!(pixel(&renderer, 1, 1), Color::BLACK);
        let mut remove = scene(2, 1, 0);
        remove.layers.push(Layer {
            node_id: 1,
            flags: 2,
            ..Default::default()
        });
        renderer.apply(&remove).unwrap();
        assert_eq!(pixel(&renderer, 1, 1), Color::RED);
    }
    #[test]
    fn dependency_order_compiles_draw_image_source_before_destination() {
        let mut s = scene(1, 0, 1);
        s.layers = vec![layer(1, 0, 1), layer(2, 1, 1)];
        s.layers[1].width = 0.0; // offscreen source
        let mut draw = command(27, [0.0, 0.0, 40.0, 40.0, 0.0, 0.0, 40.0, 40.0]);
        draw.resource_id = 2;
        s.canvas = vec![
            draw,
            command(22, [0.0, 0.0, 40.0, 40.0, 0.0, 0.0, 0.0, 0.0]),
        ];
        let mut renderer = Renderer::default();
        renderer.apply(&s).unwrap();
        assert_eq!(pixel(&renderer, 10, 10), Color::BLACK);
    }
    #[test]
    fn rejected_revision_and_failed_compile_do_not_mutate_frame() {
        let mut renderer = Renderer::default();
        let mut initial = scene(1, 0, 3);
        initial.commands.push(Command {
            kind: 1,
            width: 40.0,
            height: 40.0,
            rgba: 0x00ff00ff,
            ..Default::default()
        });
        renderer.apply(&initial).unwrap();
        assert!(!renderer.accepts(&scene(3, 2, 0)));
        let mut invalid = scene(2, 1, 2);
        invalid.commands.push(Command {
            kind: 999,
            ..Default::default()
        });
        assert!(renderer.apply(&invalid).is_err());
        assert_eq!(pixel(&renderer, 1, 1), Color::GREEN);
        assert!(renderer.accepts(&scene(2, 1, 0)));
        let mut reset = scene(1, 0, 3);
        reset.commands.push(Command {
            kind: 1,
            width: 40.0,
            height: 40.0,
            rgba: 0xff0000ff,
            ..Default::default()
        });
        renderer.apply(&reset).unwrap();
        assert_eq!(pixel(&renderer, 1, 1), Color::RED);
    }
    #[test]
    fn path_coordinates_survive_transform_changes() {
        let mut s = scene(1, 0, 1);
        s.layers.push(layer(1, 0, 5));
        s.canvas = vec![
            command(9, [0.0; 8]),
            command(17, [0.0, 0.0, 10.0, 10.0, 0.0, 0.0, 0.0, 0.0]),
            command(6, [20.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0]),
            command(21, [0.0; 8]),
            command(19, [0.0; 8]),
        ];
        let mut renderer = Renderer::default();
        renderer.apply(&s).unwrap();
        assert_eq!(pixel(&renderer, 5, 5), Color::BLACK);
        assert_eq!(pixel(&renderer, 25, 5), Color::TRANSPARENT);
    }
}
