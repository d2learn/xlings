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

fn main() {
    let mut args: Vec<String> = env::args().collect();

    let program_path = &args[0];
    let executable_name = Path::new(program_path)
        .file_stem()
        .unwrap_or_default()
        .to_string_lossy()
        .to_string();

    args.remove(0);

    let mut command = Command::new("xvm");
    command.arg("run");
    command.arg(executable_name.clone());

    command.arg("--args");
    command.args(&args);

    // append XLINGS_BINDIR to PATH
    let path_var = env::var_os("PATH").unwrap_or_default();
    let mut paths = env::split_paths(&path_var).collect::<Vec<_>>();
    paths.push(default_xlings_bindir());
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