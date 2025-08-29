use std::env;
use std::path::{ Path, PathBuf };
use std::process::{Command, exit};

fn default_xlings_bindir() -> PathBuf {
    match std::env::consts::OS {
        "windows" => PathBuf::from(r"C:\Users\Public\xlings\.xlings_data\bin"),
        "linux" => PathBuf::from("/home/xlings/.xlings_data/bin"),
        "macos" => PathBuf::from("/Users/xlings/.xlings_data/bin"),
        _ => panic!("Unsupported OS"),
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
            eprintln!("Failed to execute `xvm run {}`: {}", executable_name, e); // 这里还能使用 `executable_name`
            exit(1);
        }
    }
}