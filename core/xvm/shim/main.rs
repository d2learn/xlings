use std::env;
use std::path::{ Path, PathBuf };
use std::process::{Command, exit};

fn default_xlings_bindir() -> PathBuf {
    if let Ok(data) = env::var("XLINGS_DATA") {
        return PathBuf::from(data).join("bin");
    }
    let xlings_home = env::var("XLINGS_HOME")
        .map(PathBuf::from)
        .unwrap_or_else(|_| {
            let home = if cfg!(target_os = "windows") {
                env::var("USERPROFILE").unwrap_or_else(|_| r"C:\Users\Public".to_string())
            } else {
                env::var("HOME").unwrap_or_else(|_| "/tmp".to_string())
            };
            PathBuf::from(home).join(".xlings")
        });
    xlings_home.join("data").join("bin")
}

/// If the current executable is under .../data/bin/, return (data_dir, pkg_root). Caller sets XLINGS_* on command
/// and should run pkg_root/bin/xvm to avoid recursing into data/bin/xvm shim.
fn detect_package_data_bin() -> Option<(PathBuf, PathBuf)> {
    let exe_path = std::env::current_exe()
        .ok()
        .or_else(|| env::args().next().map(PathBuf::from));
    let path_buf = exe_path?;
    let path_str = path_buf.to_string_lossy();
    let in_data_bin = path_str.contains("/data/bin/") || path_str.contains("\\data\\bin\\");
    if !in_data_bin {
        return None;
    }
    let data_bin = path_buf.parent()?;
    let data_dir = data_bin.parent()?;
    let pkg_root = data_dir.parent()?;
    Some((data_dir.to_path_buf(), pkg_root.to_path_buf()))
}

fn main() {
    let mut args: Vec<String> = env::args().collect();

    let program_path = &args[0];
    let executable_name = Path::new(program_path)
        .file_stem()
        .unwrap_or_default()
        .to_string_lossy()
        .to_string();

    args.remove(0);

    let pkg = detect_package_data_bin();
    let xvm_bin: String = pkg
        .as_ref()
        .and_then(|(_, pkg_root)| {
            let bin_xvm = pkg_root.join("bin").join(if cfg!(target_os = "windows") { "xvm.exe" } else { "xvm" });
            if bin_xvm.exists() {
                bin_xvm.to_str().map(String::from)
            } else {
                None
            }
        })
        .unwrap_or_else(|| "xvm".to_string());

    let mut command = Command::new(&xvm_bin);
    command.arg("run");
    command.arg(executable_name.clone());
    command.arg("--args");
    command.args(&args);

    if let Some((ref data_dir, ref pkg_root)) = pkg {
        command.env("XLINGS_DATA", data_dir);
        command.env("XLINGS_HOME", pkg_root);
    }

    // append XLINGS_BINDIR to PATH (use package data/bin when in package mode)
    let path_var = env::var_os("PATH").unwrap_or_default();
    let mut paths = env::split_paths(&path_var).collect::<Vec<_>>();
    let bindir = pkg.as_ref().map(|(d, _)| d.join("bin")).unwrap_or_else(default_xlings_bindir);
    paths.push(bindir);
    let new_path = env::join_paths(paths).expect("Failed to join paths");
    command.env("PATH", new_path);

    command.stdin(std::process::Stdio::inherit());
    command.stdout(std::process::Stdio::inherit());
    command.stderr(std::process::Stdio::inherit());

    match command.status() {
        Ok(status) => {
            exit(status.code().unwrap_or(1));
        }
        Err(e) => {
            eprintln!("Failed to execute `xvm run {}`: {}", executable_name, e);
            if e.kind() == std::io::ErrorKind::NotFound {
                eprintln!("  hint: xvm not found in PATH. Set XLINGS_HOME or XLINGS_DATA and ensure xvm is in the corresponding bin directory.");
            }
            exit(1);
        }
    }
}