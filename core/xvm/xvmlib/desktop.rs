use std::fs;
use std::path::PathBuf;

/// 描述快捷方式的选项
pub struct ShortcutOptions {
    pub name: String,           // 快捷方式的显示名称
    pub exec_path: PathBuf,     // 要执行的程序路径/目标路径
    pub description: Option<String>, // 快捷方式的描述
    pub icon_path: Option<PathBuf>,  // 图标路径
    pub working_dir: Option<PathBuf>, // 工作目录（仅 Windows 有效）
    pub terminal: bool,         // 是否需要终端运行（仅 Linux 有效）
}

#[cfg(windows)]
mod windows_desktop {
    use super::ShortcutOptions;
    use std::path::PathBuf;
    use windows::{
        core::PCWSTR,
        Win32::{
            Foundation::S_OK,
            System::Com::{CoInitializeEx, CoCreateInstance, CLSCTX_INPROC_SERVER, COINIT_APARTMENTTHREADED},
            UI::Shell::{IShellLinkW, ShellLink, IPersistFile},
        },
    };

    pub static SHORTCUT_ROOT_DIR: &str = r"C:\ProgramData\Microsoft\Windows\Start Menu\Programs";

    pub fn shortcut_userdir() -> PathBuf {
        PathBuf::from(r"C:\Users\Public\Desktop")
    }

    pub fn shortcut_path_format(dir: &PathBuf, name: &str) -> PathBuf {
        dir.join(format!("{}.xvm.lnk", name))
    }

    pub fn create_shortcut(options: ShortcutOptions, shortcut_dir: &PathBuf) -> Result<(), String> {
        let shortcut_path = shortcut_path_format(shortcut_dir, &options.name);
        unsafe {
            // Initialize COM library
            CoInitializeEx(None, COINIT_APARTMENTTHREADED)
                .map_err(|e| format!("Failed to initialize COM library: {:?}", e))?;

            // Create an instance of IShellLink
            let shell_link: IShellLinkW = CoCreateInstance(&ShellLink, None, CLSCTX_INPROC_SERVER)
                .map_err(|e| format!("Failed to create IShellLink instance: {:?}", e))?;

            // Set the target path for the shortcut
            shell_link
                .SetPath(PCWSTR::from_raw(
                    options.exec_path.as_os_str().encode_wide().collect::<Vec<u16>>().as_ptr(),
                ))
                .map_err(|e| format!("Failed to set shortcut target path: {:?}", e))?;

            // Set the description (optional)
            if let Some(desc) = options.description {
                shell_link
                    .SetDescription(PCWSTR::from_raw(desc.encode_utf16().collect::<Vec<u16>>().as_ptr()))
                    .map_err(|e| format!("Failed to set shortcut description: {:?}", e))?;
            }

            // Set the working directory (optional)
            if let Some(dir) = options.working_dir {
                shell_link
                    .SetWorkingDirectory(PCWSTR::from_raw(
                        dir.as_os_str().encode_wide().collect::<Vec<u16>>().as_ptr(),
                    ))
                    .map_err(|e| format!("Failed to set working directory: {:?}", e))?;
            }

            // Set the icon (optional)
            if let Some(icon) = options.icon_path {
                shell_link
                    .SetIconLocation(PCWSTR::from_raw(
                        icon.as_os_str().encode_wide().collect::<Vec<u16>>().as_ptr(),
                    ), 0)
                    .map_err(|e| format!("Failed to set shortcut icon: {:?}", e))?;
            }

            // Query IPersistFile to save the shortcut as a .lnk file
            let persist_file: IPersistFile = shell_link.cast().map_err(|e| format!("{:?}", e))?;

            // Convert the shortcut path to UTF-16 and save it
            let shortcut_path_wide: Vec<u16> =
                shortcut_path.as_os_str().encode_wide().chain(Some(0)).collect();
            persist_file
                .Save(PCWSTR::from_raw(shortcut_path_wide.as_ptr()), true)
                .map_err(|e| format!("Failed to save shortcut file: {:?}", e))?;
        }

        Ok(())
    }
}

#[cfg(unix)]
mod linux_desktop {
    use super::ShortcutOptions;
    use std::fs::File;
    use std::io::Write;
    use std::os::unix::fs::PermissionsExt;
    use std::path::PathBuf;

    pub static SHORTCUT_ROOT_DIR: &str = "/usr/share/applications";

    pub fn shortcut_userdir() -> PathBuf {
        let homedir = std::env::var("HOME").unwrap_or_else(|_| "/root".to_string());
        PathBuf::from(homedir).join(".local/share/applications")
    }

    pub fn shortcut_path_format(dir: &PathBuf, name: &str) -> PathBuf {
        dir.join(format!("{}.xvm.desktop", name))
    }

    pub fn create_shortcut(options: ShortcutOptions, shortcut_dir: &PathBuf) -> Result<(), String> {
        // Create .desktop file content
        let desktop_entry = format!(
            r#"[Desktop Entry]
Version=1.0
Name={}
Exec={}
Icon={}
Type=Application
Terminal={}
"#,
            options.name,
            options.exec_path.display(),
            options.icon_path
                .as_ref()
                .map(|p| p.display().to_string())
                .unwrap_or_else(|| "".to_string()),
            if options.terminal { "true" } else { "false" }
        );

        let path = shortcut_path_format(shortcut_dir, &options.name);

        // Write to the .desktop file
        let mut file = File::create(&path).map_err(|e| format!("Failed to create file: {:?}", e))?;
        file.write_all(desktop_entry.as_bytes())
            .map_err(|e| format!("Failed to write to file: {:?}", e))?;

        // Set executable permissions
        let permissions = std::fs::Permissions::from_mode(0o755);
        std::fs::set_permissions(&path, permissions).map_err(|e| format!("Failed to set permissions: {:?}", e))?;

        // Refresh desktop database
        let _ = std::process::Command::new("update-desktop-database")
            .arg(shortcut_dir)
            .output()
            .map_err(|e| format!("Failed to update desktop database: {:?}", e))?;

        Ok(())
    }
}

#[cfg(windows)]
pub use windows_desktop::{create_shortcut, SHORTCUT_ROOT_DIR, shortcut_path_format, shortcut_userdir};

#[cfg(unix)]
pub use linux_desktop::{create_shortcut, SHORTCUT_ROOT_DIR, shortcut_path_format, shortcut_userdir};

pub fn delete_shortcut(dir: &PathBuf, name: &str) -> Result<(), String> {
    let path = shortcut_path_format(dir, name);
    if path.exists() {
        fs::remove_file(&path).map_err(|e| format!("Failed to delete shortcut: {:?}", e))?;
    }

    Ok(())
}