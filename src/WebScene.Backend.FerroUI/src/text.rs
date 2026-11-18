//! Worker-safe text shaping. Uses the same HarfBuzz library as FerroUI, with font
//! tables supplied by Skia. No dispatcher or thread-local FerroUI services are used.
use harfbuzz_sys as hb;
use skia_safe::{
    Canvas, Color, Font, FontMgr, FontStyle, Paint, Point, TextBlob, TextBlobBuilder, Typeface,
};
use std::ffi::c_void;

pub(crate) struct ShapedText {
    blob: Option<TextBlob>,
    paint: Paint,
    width: f32,
    ascent: f32,
    descent: f32,
}
impl ShapedText {
    pub fn max_intrinsic_width(&self) -> f32 {
        self.width
    }
    pub fn alphabetic_baseline(&self) -> f32 {
        self.ascent
    }
    pub fn height(&self) -> f32 {
        self.ascent + self.descent
    }
    pub fn paint(&self, canvas: &Canvas, position: (f32, f32)) {
        if let Some(blob) = &self.blob {
            canvas.draw_text_blob(blob, (position.0, position.1 + self.ascent), &self.paint);
        }
    }
}

pub(crate) fn paragraph(
    text: &str,
    family: &str,
    size: f32,
    weight: i32,
    letter: f32,
    word: f32,
    color: Color,
) -> ShapedText {
    let mut paint = Paint::default();
    paint.set_color(color).set_anti_alias(true);
    paragraph_with_paint(text, family, size, weight, letter, word, &paint)
}
pub(crate) fn paragraph_with_paint(
    text: &str,
    family: &str,
    size: f32,
    weight: i32,
    letter: f32,
    word: f32,
    paint: &Paint,
) -> ShapedText {
    let manager = FontMgr::default();
    let style = FontStyle::new(
        skia_safe::font_style::Weight::from(weight),
        skia_safe::font_style::Width::NORMAL,
        skia_safe::font_style::Slant::Upright,
    );
    let families: Vec<_> = family
        .split(',')
        .map(|f| generic_family(f.trim().trim_matches(['\'', '"'])))
        .collect();
    let primary = families
        .iter()
        .find_map(|f| manager.match_family_style(f, style))
        .or_else(|| manager.match_family_style(generic_family("sans-serif"), style));
    let Some(primary) = primary else {
        return ShapedText {
            blob: None,
            paint: paint.clone(),
            width: 0.0,
            ascent: 0.0,
            descent: 0.0,
        };
    };
    let size = if size.is_finite() { size.max(0.0) } else { 0.0 };
    let (_, metrics) = Font::new(primary.clone(), size).metrics();
    let (mut ascent, mut descent) = (-metrics.ascent, metrics.descent);
    let mut builder = TextBlobBuilder::new();
    let mut x = 0.0;
    let bidi = unicode_bidi::BidiInfo::new(text, None);
    for paragraph in &bidi.paragraphs {
        let (levels, runs) = bidi.visual_runs(paragraph, paragraph.range.clone());
        for run in runs {
            let rtl = levels[run.start].is_rtl();
            let slice = &text[run.clone()];
            // Keep contiguous text with one face together so HarfBuzz can shape clusters,
            // kerning and ligatures. Unsupported scalars use the platform fallback face.
            let mut segments: Vec<(usize, usize, Typeface)> = vec![];
            for (offset, ch) in slice.char_indices() {
                let face = if primary.unichar_to_glyph(ch as i32) != 0 {
                    primary.clone()
                } else {
                    families
                        .iter()
                        .filter_map(|f| manager.match_family_style(f, style))
                        .find(|f| f.unichar_to_glyph(ch as i32) != 0)
                        .or_else(|| manager.match_family_style_character("", style, &[], ch as i32))
                        .unwrap_or_else(|| primary.clone())
                };
                if let Some(last) = segments
                    .last_mut()
                    .filter(|s| s.2.unique_id() == face.unique_id())
                {
                    last.1 = offset + ch.len_utf8();
                } else {
                    segments.push((offset, offset + ch.len_utf8(), face));
                }
            }
            if rtl {
                segments.reverse();
            }
            for (start, end, face) in segments {
                let font = Font::new(face.clone(), size);
                let (_, metrics) = font.metrics();
                ascent = ascent.max(-metrics.ascent);
                descent = descent.max(metrics.descent);
                let shaped = shape(&slice[start..end], &face, size, rtl, letter, word);
                let (glyphs, positions) = builder.alloc_run_pos(&font, shaped.len(), None);
                for (i, g) in shaped.iter().enumerate() {
                    glyphs[i] = g.glyph;
                    positions[i] = Point::new(x + g.x, g.y);
                    x += g.advance;
                }
            }
        }
    }
    ShapedText {
        blob: builder.make(),
        paint: paint.clone(),
        width: x,
        ascent,
        descent,
    }
}
fn generic_family(f: &str) -> &str {
    match f {
        "sans-serif" | "system-ui" => {
            if cfg!(target_os = "windows") {
                "Segoe UI"
            } else if cfg!(target_os = "macos") {
                "Helvetica"
            } else {
                "DejaVu Sans"
            }
        }
        "serif" => {
            if cfg!(target_os = "windows") || cfg!(target_os = "macos") {
                "Times New Roman"
            } else {
                "DejaVu Serif"
            }
        }
        "monospace" => {
            if cfg!(target_os = "windows") {
                "Consolas"
            } else if cfg!(target_os = "macos") {
                "Menlo"
            } else {
                "DejaVu Sans Mono"
            }
        }
        _ => f,
    }
}
struct Glyph {
    glyph: u16,
    x: f32,
    y: f32,
    advance: f32,
}
struct HbFont {
    face: *mut hb::hb_face_t,
    font: *mut hb::hb_font_t,
    buffer: *mut hb::hb_buffer_t,
}
impl Drop for HbFont {
    fn drop(&mut self) {
        unsafe {
            hb::hb_buffer_destroy(self.buffer);
            hb::hb_font_destroy(self.font);
            hb::hb_face_destroy(self.face);
        }
    }
}
unsafe extern "C" fn table(
    _: *mut hb::hb_face_t,
    tag: u32,
    user: *mut c_void,
) -> *mut hb::hb_blob_t {
    // HarfBuzz calls this only while the face owns the boxed Skia typeface. Copy each
    // table into a HarfBuzz blob, so neither a Rust allocation nor a Skia Data is borrowed.
    std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
        let face = &*(user as *const Typeface);
        let Some(data) = face.copy_table_data(tag) else {
            return hb::hb_blob_get_empty();
        };
        hb::hb_blob_create(
            data.as_ptr().cast(),
            data.len() as u32,
            hb::HB_MEMORY_MODE_DUPLICATE,
            std::ptr::null_mut(),
            None,
        )
    }))
    .unwrap_or(std::ptr::null_mut())
}
unsafe extern "C" fn drop_face(user: *mut c_void) {
    drop(Box::from_raw(user as *mut Typeface));
}
fn shape(
    text: &str,
    typeface: &Typeface,
    size: f32,
    rtl: bool,
    letter: f32,
    word: f32,
) -> Vec<Glyph> {
    if text.is_empty() {
        return vec![];
    }
    // All objects are local to this call. The callbacks own their font tables, and the
    // returned vector copies glyphs before HbFont destroys the native shaping buffer.
    unsafe {
        let face = hb::hb_face_create_for_tables(
            Some(table),
            Box::into_raw(Box::new(typeface.clone())).cast(),
            Some(drop_face),
        );
        let font = hb::hb_font_create(face);
        hb::hb_ot_font_set_funcs(font);
        hb::hb_font_set_scale(font, (size * 64.0) as i32, (size * 64.0) as i32);
        let buffer = hb::hb_buffer_create();
        let _guard = HbFont { face, font, buffer };
        hb::hb_buffer_add_utf8(
            buffer,
            text.as_ptr().cast(),
            text.len() as i32,
            0,
            text.len() as i32,
        );
        hb::hb_buffer_set_direction(
            buffer,
            if rtl {
                hb::HB_DIRECTION_RTL
            } else {
                hb::HB_DIRECTION_LTR
            },
        );
        hb::hb_buffer_guess_segment_properties(buffer);
        // CSS letter-spacing suppresses discretionary ligatures, preserving spacing slots.
        let feature = hb::hb_feature_t {
            tag: u32::from_be_bytes(*b"liga"),
            value: 0,
            start: 0,
            end: u32::MAX,
        };
        hb::hb_shape(
            font,
            buffer,
            if letter != 0.0 {
                &feature
            } else {
                std::ptr::null()
            },
            if letter != 0.0 { 1 } else { 0 },
        );
        let mut count = 0;
        let infos = hb::hb_buffer_get_glyph_infos(buffer, &mut count);
        let positions = hb::hb_buffer_get_glyph_positions(buffer, &mut count);
        if count == 0 {
            return vec![];
        }
        let infos = std::slice::from_raw_parts(infos, count as usize);
        let positions = std::slice::from_raw_parts(positions, count as usize);
        infos
            .iter()
            .zip(positions)
            .enumerate()
            .map(|(i, (info, pos))| {
                let last_in_cluster = infos
                    .get(i + 1)
                    .is_none_or(|next| next.cluster != info.cluster);
                let whitespace = text
                    .get(info.cluster as usize..)
                    .and_then(|s| s.chars().next())
                    .is_some_and(|c| c == ' ' || c == '\u{a0}');
                Glyph {
                    glyph: info.codepoint as u16,
                    x: pos.x_offset as f32 / 64.0,
                    y: -(pos.y_offset as f32) / 64.0,
                    advance: pos.x_advance as f32 / 64.0
                        + if last_in_cluster {
                            letter + if whitespace { word } else { 0.0 }
                        } else {
                            0.0
                        },
                }
            })
            .collect()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn shaping_shares_metrics_with_painted_glyphs_and_handles_fallback() {
        let text = "AV e\u{301} שלום 🙂";
        let normal = paragraph(text, "sans-serif", 24.0, 400, 0.0, 0.0, Color::BLACK);
        let spaced = paragraph(text, "sans-serif", 24.0, 400, 2.0, 3.0, Color::BLACK);
        assert!(normal.max_intrinsic_width() > 80.0);
        assert!(spaced.max_intrinsic_width() > normal.max_intrinsic_width());
        assert!(normal.height() > 20.0);
        assert!(normal.blob.is_some());
    }
}
