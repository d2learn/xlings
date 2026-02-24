use std::env;
use std::path::{ Path, PathBuf };
use std::process::{Command, exit};

fn default_xlings_bindir() -> PathBuf {
    if let Ok(subos) = env::var("XLINGS_SUBOS") {
        return PathBuf::from(subos).join("bin");
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
    xlings_home.join("subos").join("default").join("bin")
}

/// Detect if running from a package layout and return (subos_dir, pkg_root).
/// Supports two layouts:
///   Legacy:  .../data/bin/<shim>        → subos_dir=.../data,           pkg_root=...
///   SubOS:   .../subos/<name>/bin/<shim> → subos_dir=.../subos/<name>, pkg_root=...
fn detect_package_layout() -> Option<(PathBuf, PathBuf)> {
    let exe_path = std::env::current_exe()
        .ok()
        .or_else(|| env::args().next().map(PathBuf::from));
    let path_buf = exe_path?;
    let path_str = path_buf.to_string_lossy();

    let bin_dir = path_buf.parent()?;
    let subos_dir = bin_dir.parent()?;

    if path_str.contains("/data/bin/") || path_str.contains("\\data\\bin\\") {
        let pkg_root = subos_dir.parent()?;
        return Some((subos_dir.to_path_buf(), pkg_root.to_path_buf()));
    }

    if path_str.contains("/subos/") || path_str.contains("\\subos\\") {
        let subos_parent = subos_dir.parent()?;
        let pkg_root = subos_parent.parent()?;
        return Some((subos_dir.to_path_buf(), pkg_root.to_path_buf()));
    }

    None
}

fn xlings_subcommand_alias(name: &str) -> Option<&'static str> {
    match name {
        "xim" | "xinstall" => Some("install"),
        "xsubos"           => Some("subos"),
        "xself"            => Some("self"),
        _                  => None,
    }
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

    let pkg = detect_package_layout();
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

    if let Some(subcmd) = xlings_subcommand_alias(&executable_name) {
        command.arg("xlings");
        command.arg("--args");
        command.arg(subcmd);
    } else {
        command.arg(executable_name.clone());
        command.arg("--args");
    }
    command.args(&args);

    if let Some((ref subos_dir, ref pkg_root)) = pkg {
        command.env("XLINGS_HOME", pkg_root);
        command.env("XLINGS_DATA", pkg_root.join("data"));
        command.env("XLINGS_SUBOS", subos_dir);
    }

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
            let run_target = xlings_subcommand_alias(&executable_name)
                .map(|_| "xlings".to_string())
                .unwrap_or_else(|| executable_name.clone());
            eprintln!("Failed to execute `xvm run {}`: {}", run_target, e);
            if e.kind() == std::io::ErrorKind::NotFound {
                eprintln!("  hint: xvm not found in PATH. Set XLINGS_HOME or XLINGS_SUBOS.");
            }
            exit(1);
        }
    }
}
