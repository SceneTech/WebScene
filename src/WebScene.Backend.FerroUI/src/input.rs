use ferroui_base::input::{Key, KeyModifiers, MouseButton, PointerPointProperties};
pub(crate) fn modifiers(m: KeyModifiers) -> u32 {
    u32::from(m.contains(KeyModifiers::SHIFT))
        | (u32::from(m.contains(KeyModifiers::CONTROL)) << 1)
        | (u32::from(m.contains(KeyModifiers::ALT)) << 2)
        | (u32::from(m.contains(KeyModifiers::META)) << 3)
}
pub(crate) fn button(b: MouseButton) -> u32 {
    match b {
        MouseButton::Middle => 1,
        MouseButton::Right => 2,
        MouseButton::XButton1 => 3,
        MouseButton::XButton2 => 4,
        _ => 0,
    }
}
pub(crate) fn pointer_flags(
    p: PointerPointProperties,
    m: KeyModifiers,
    changed: MouseButton,
) -> u32 {
    u32::from(p.is_left_button_pressed)
        | (u32::from(p.is_right_button_pressed) << 1)
        | (u32::from(p.is_middle_button_pressed) << 2)
        | (u32::from(p.is_x_button_1_pressed) << 3)
        | (u32::from(p.is_x_button_2_pressed) << 4)
        | (button(changed) << 8)
        | (modifiers(m) << 16)
}
pub(crate) fn key(k: Key) -> u32 {
    let n = k as i32;
    if n >= Key::A as i32 && n <= Key::Z as i32 {
        return (65 + n - Key::A as i32) as u32;
    }
    if n >= Key::D0 as i32 && n <= Key::D9 as i32 {
        return (48 + n - Key::D0 as i32) as u32;
    }
    if n >= Key::NumPad0 as i32 && n <= Key::NumPad9 as i32 {
        return (96 + n - Key::NumPad0 as i32) as u32;
    }
    if n >= Key::F1 as i32 && n <= Key::F24 as i32 {
        return (112 + n - Key::F1 as i32) as u32;
    }
    match k {
        Key::Back => 8,
        Key::Tab => 9,
        Key::Return => 13,
        Key::Escape => 27,
        Key::Space => 32,
        Key::PageUp => 33,
        Key::PageDown => 34,
        Key::End => 35,
        Key::Home => 36,
        Key::Left => 37,
        Key::Up => 38,
        Key::Right => 39,
        Key::Down => 40,
        Key::Insert => 45,
        Key::Delete => 46,
        Key::LeftShift | Key::RightShift => 16,
        Key::LeftCtrl | Key::RightCtrl => 17,
        Key::LeftAlt | Key::RightAlt => 18,
        Key::LWin | Key::RWin => 91,
        Key::CapsLock => 20,
        Key::OemSemicolon => 186,
        Key::OemPlus => 187,
        Key::OemComma => 188,
        Key::OemMinus => 189,
        Key::OemPeriod => 190,
        Key::OemQuestion => 191,
        Key::OemTilde => 192,
        Key::OemOpenBrackets => 219,
        Key::OemPipe => 220,
        Key::OemCloseBrackets => 221,
        Key::OemQuotes => 222,
        _ => 0,
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn modifiers_and_non_ascii_text_use_distinct_native_contracts() {
        assert_eq!(modifiers(KeyModifiers::SHIFT | KeyModifiers::ALT), 5);
        assert_eq!(key(Key::A), 65);
        assert_eq!(key(Key::D9), 57);
        assert_eq!(key(Key::F12), 123);
        assert_eq!(key(Key::Left), 37);
        // Rust chars match the native TEXT contract (one Unicode scalar, not UTF-16 units).
        assert_eq!(
            "é🙂".chars().map(|c| c as u32).collect::<Vec<_>>(),
            vec![0xe9, 0x1f642]
        );
    }
}
