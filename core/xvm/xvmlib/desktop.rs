use colored::Colorize;

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
    use std::process::Command;

    pub static SHORTCUT_ROOT_DIR: &str = r"C:\ProgramData\Microsoft\Windows\Start Menu\Programs";

    pub fn shortcut_userdir() -> PathBuf {
        PathBuf::from(r"C:\Users\Public\Desktop")
    }

    pub fn shortcut_path_format(dir: &PathBuf, name: &str) -> PathBuf {
        dir.join(format!("{}.xvm.lnk", name))
    }

    pub fn create_shortcut(options: ShortcutOptions, shortcut_dir: &PathBuf) -> Result<(), String> {
        let shortcut_path = shortcut_path_format(shortcut_dir, &options.name);

        // 构建 PowerShell 脚本命令
        let mut command = format!(
            r#"
            $WshShell = New-Object -ComObject WScript.Shell;
            $Shortcut = $WshShell.CreateShortcut('{}');
            $Shortcut.TargetPath = '{}';
            "#,
            shortcut_path.display(),
            options.exec_path.display()
        );

        // 设置可选字段
        if let Some(description) = &options.description {
            command.push_str(&format!(r"$Shortcut.Description = '{}';", description));
        }
        if let Some(working_dir) = &options.working_dir {
            command.push_str(&format!(r"$Shortcut.WorkingDirectory = '{}';", working_dir.display()));
        }
        if let Some(icon_path) = &options.icon_path {
            command.push_str(&format!(r"$Shortcut.IconLocation = '{}';", icon_path.display()));
        }

        // 保存快捷方式
        command.push_str(r"$Shortcut.Save();");

        // 调用 PowerShell 执行命令
        let status = Command::new("powershell")
            .arg("-Command")
            .arg(command)
            .status()
            .map_err(|e| format!("Failed to execute PowerShell command: {:?}", e))?;

        if status.success() {
            Ok(())
        } else {
            Err(format!("PowerShell returned non-zero exit code: {:?}", status))
        }
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
        println!("try to remove [{}] desktop shortcut...", name.green());
        fs::remove_file(&path).map_err(|e| format!("Failed to delete shortcut: {:?}", e))?;
        println!("shortcut deleted: {}", path.display());
    }

    Ok(())
}