//! Prints the current wallpaper of the first screen: `cargo run -p wpc-kde --example current`
fn main() -> Result<(), wpc_kde::Error> {
    let w = wpc_kde::Plasma::connect()?.wallpaper(0)?;
    println!("{w:#?}");
    if let Some(img) = &w.image {
        println!(
            "resolved: {:?}",
            wpc_kde::resolve_image(img, wpc_kde::dark_mode())
        );
    }
    Ok(())
}
