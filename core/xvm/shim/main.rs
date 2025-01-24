use std::env;
use std::path::Path;
use std::process::{Command, exit};

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