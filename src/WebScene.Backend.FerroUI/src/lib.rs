#![doc = include_str!("../README.md")]
mod engine;
mod input;
mod render;
mod resources;
mod scene;
mod text;
mod view;

#[allow(
    non_camel_case_types,
    non_snake_case,
    non_upper_case_globals,
    dead_code,
    clippy::all
)]
mod ffi {
    include!(concat!(env!("OUT_DIR"), "/bindings.rs"));
}

pub use engine::{Engine, Error, RuntimeOptions};
pub use resources::{FileHttpLoader, ResourceLoader, ResourceRequest, ResourceResponse};
pub use scene::Scene;
pub use view::WebSceneView;
