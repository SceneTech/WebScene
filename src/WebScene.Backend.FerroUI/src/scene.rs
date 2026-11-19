use crate::{ffi, Error};

/// Owned immutable scene data. Native leases are released after applying a revision.
#[derive(Clone)]
pub struct Scene {
    pub(crate) header: ffi::webscene_scene_header,
    pub(crate) commands: Vec<ffi::webscene_scene_command>,
    pub(crate) layers: Vec<ffi::webscene_canvas_layer>,
    pub(crate) canvas: Vec<ffi::webscene_canvas_command>,
    pub(crate) strings: Vec<String>,
}
impl Scene {
    pub fn revision(&self) -> u64 {
        self.header.revision
    }
    pub fn is_ready(&self) -> bool {
        self.header.flags & 4 != 0
    }
    pub fn viewport(&self) -> (f32, f32) {
        (self.header.viewport_width, self.header.viewport_height)
    }
    pub(crate) fn string(&self, index: u32) -> &str {
        self.strings
            .get(index as usize)
            .map(String::as_str)
            .unwrap_or("")
    }
    pub(crate) fn layer_string(&self, layer: &ffi::webscene_canvas_layer, index: u32) -> &str {
        if index >= layer.string_count {
            return "";
        }
        self.string(layer.string_offset + index)
    }
    // SAFETY: caller holds an acquired native scene lease throughout this copy.
    pub(crate) unsafe fn copy(view: &ffi::webscene_scene_view) -> Result<Self, Error> {
        // Engine ABI 3 retains the immutable scene-view ABI 2 (same as Avalonia).
        if view.abi_version != 2
            || (view.struct_size as usize) < size_of::<ffi::webscene_scene_view>()
        {
            return Err(Error("Invalid native scene ABI".into()));
        }
        unsafe fn slice<'a, T>(ptr: *const T, count: u32) -> Result<&'a [T], Error> {
            if count == 0 {
                return Ok(&[]);
            }
            if ptr.is_null() || (count as usize) > isize::MAX as usize / size_of::<T>() {
                return Err(Error("Invalid native scene array".into()));
            }
            Ok(std::slice::from_raw_parts(ptr, count as usize))
        }
        let bytes = slice(view.string_bytes.cast::<u8>(), view.string_byte_count)?;
        let strings = slice(view.strings, view.string_count)?
            .iter()
            .map(|s| {
                let start = s.byte_offset as usize;
                let end = start
                    .checked_add(s.byte_length as usize)
                    .ok_or_else(|| Error("String range overflow".into()))?;
                let bytes = bytes
                    .get(start..end)
                    .ok_or_else(|| Error("Invalid scene string range".into()))?;
                String::from_utf8(bytes.to_vec()).map_err(|e| Error(e.to_string()))
            })
            .collect::<Result<Vec<_>, _>>()?;
        let layers = slice(view.canvas_layers, view.header.canvas_layer_count)?.to_vec();
        for layer in &layers {
            if layer
                .command_offset
                .checked_add(layer.command_count)
                .is_none_or(|n| n > view.canvas_command_count)
                || layer
                    .string_offset
                    .checked_add(layer.string_count)
                    .is_none_or(|n| n > view.string_count)
            {
                return Err(Error("Invalid canvas layer range".into()));
            }
        }
        Ok(Self {
            header: view.header,
            commands: slice(view.commands, view.header.command_count)?.to_vec(),
            layers,
            canvas: slice(view.canvas_commands, view.canvas_command_count)?.to_vec(),
            strings,
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn validates_scene_v2_and_accepts_removal_without_command_payload() {
        let removal = ffi::webscene_canvas_layer {
            node_id: 7,
            flags: 2,
            ..Default::default()
        };
        let mut view = ffi::webscene_scene_view {
            struct_size: size_of::<ffi::webscene_scene_view>() as u32,
            abi_version: 2,
            header: ffi::webscene_scene_header {
                canvas_layer_count: 1,
                ..Default::default()
            },
            canvas_layers: &removal,
            ..Default::default()
        };
        assert_eq!(unsafe { Scene::copy(&view) }.unwrap().layers.len(), 1);
        view.abi_version = 3;
        assert!(unsafe { Scene::copy(&view) }.is_err());
        view.abi_version = 2;
        view.header.command_count = 1;
        assert!(unsafe { Scene::copy(&view) }.is_err());
        view.header.command_count = 0;
        let bad_string = ffi::webscene_scene_string {
            byte_offset: 4,
            byte_length: 8,
        };
        view.strings = &bad_string;
        view.string_count = 1;
        assert!(unsafe { Scene::copy(&view) }.is_err());
    }
}
